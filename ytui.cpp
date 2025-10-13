#include "ytui.h"
#include "config.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <thread>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

// === Global State Definition ===
std::vector<Video> results, queue;
std::vector<std::string> history;
std::vector<Subscription> subscriptions;
std::string query, status_msg;
int selection = 0;
UiMode mode = UiMode::SEARCH;

//==============================================================================
// --- File Utilities (formerly files.cpp) ---
//==============================================================================
namespace files {
    std::string get_home_path(const char* subpath) {
        const char* home = getenv("HOME");
        if (!home) return "";
        return std::string(home) + subpath;
    }

    void setup_dirs() {
        mkdir(get_home_path(CACHE_DIR).c_str(), 0755);
        mkdir(get_home_path(VIDEO_DIR).c_str(), 0755);
    }

    bool is_cached(const Video& v) {
        struct stat buffer;
        std::string path_base = get_home_path(VIDEO_DIR) + "/" + v.id;
        return (stat((path_base + ".mkv").c_str(), &buffer) == 0 || stat((path_base + ".webm").c_str(), &buffer) == 0);
    }

    std::vector<std::string> load_history() {
        std::vector<std::string> hist;
        std::ifstream f(get_home_path(HISTORY_FILE));
        std::string line;
        while (std::getline(f, line)) if (!line.empty()) hist.push_back(line);
        return hist;
    }

    void save_history(const std::vector<std::string>& hist) {
        std::ofstream f(get_home_path(HISTORY_FILE));
        for (const auto& s : hist) f << s << "\n";
    }

    std::vector<Video> load_queue() {
        std::vector<Video> q;
        std::ifstream f(get_home_path(QUEUE_FILE));
        std::string line;
        while (std::getline(f, line)) {
            size_t delim = line.find("|||");
            if (delim != std::string::npos)
                q.push_back({line.substr(0, delim), line.substr(delim + 3), ""});
        }
        return q;
    }

    void save_queue(const std::vector<Video>& q) {
        std::ofstream f(get_home_path(QUEUE_FILE));
        for (const auto& v : q) f << v.id << "|||" << v.title << "\n";
    }

    std::vector<Subscription> load_subscriptions() {
        std::vector<Subscription> subs;
        std::ifstream f(get_home_path(SUBSCRIPTIONS_FILE));
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t delim = line.find(' ');
            if (delim != std::string::npos) {
                subs.push_back({line.substr(0, delim), line.substr(delim + 1)});
            }
        }
        return subs;
    }
}

//==============================================================================
// --- YouTube Interaction (formerly yt.cpp) ---
//==============================================================================
namespace yt {
    struct PipeCloser { void operator()(FILE* fp) const { if (fp) pclose(fp); } };

    static std::string exec(const char* cmd) {
        char buffer[128];
        std::string result = "";
        std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd, "r"));
        if (!pipe) return "popen() failed!";
        while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) result += buffer;
        return result;
    }

    static std::vector<Video> parse_yt_dlp_output(const std::string& cmd) {
        std::vector<Video> videos;
        std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"));
        if (!pipe) return videos;

        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe.get())) {
            std::string_view line(buf);
            if (line.back() == '\n') line.remove_suffix(1);
            size_t delim1 = line.find("|||");
            if (delim1 == std::string_view::npos) continue;
            size_t delim2 = line.rfind("|||");
            if (delim2 == delim1) continue;
            videos.push_back({
                std::string(line.substr(0, delim1)),
                std::string(line.substr(delim1 + 3, delim2 - (delim1 + 3))),
                std::string(line.substr(delim2 + 3))
            });
        }
        return videos;
    }

    std::vector<Video> search(const std::string& q) {
        std::string escaped_query;
        for(char c : q) {
            if(c == '\'') escaped_query += "'\\''";
            else escaped_query += c;
        }
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "yt-dlp --no-warnings --flat-playlist --print \"%%(id)s|||%%(title)s|||%%(channel)s\" 'ytsearch30:%s' 2>/dev/null", escaped_query.c_str());
        return parse_yt_dlp_output(cmd);
    }

    std::vector<Video> fetch_channel_videos(const std::string& channel_url) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "yt-dlp --no-warnings --flat-playlist -I 0:30 --print \"%%(id)s|||%%(title)s|||%%(channel)s\" '%s' 2>/dev/null", channel_url.c_str());
        return parse_yt_dlp_output(cmd);
    }

    void download(const Video& v) {
        std::string cmd = "yt-dlp " + std::string(YTDLP_ARGS) +
                          " -o '" + files::get_home_path(VIDEO_DIR) + "/%(id)s.%(ext)s'" +
                          " 'https://www.youtube.com/watch?v=" + v.id + "' >/dev/null 2>&1 &";
        system(cmd.c_str());
    }

    std::string fetch_description(const std::string& video_id) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "yt-dlp --get-description 'https://www.youtube.com/watch?v=%s'", video_id.c_str());
        return exec(cmd);
    }

    void get_video_context(Video& v) {
        if (v.channel.empty()) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "yt-dlp --print \"%%(channel)s\" 'https://www.youtube.com/watch?v=%s'", v.id.c_str());
            v.channel = exec(cmd);
            if(!v.channel.empty()) v.channel.pop_back();
        }
    }
}

//==============================================================================
// --- MPV Control (formerly mpv.cpp) ---
//==============================================================================
namespace mpv {
    static int sock = -1;

    bool connect(const char* socket_path) {
        for (int i = 0; i < 10; ++i) {
            sock = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock < 0) return false;
            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
            if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) return true;
            close(sock);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return false;
    }

    void disconnect() { if (sock >= 0) close(sock); }

    void cmd(const std::string_view& cmd_str) {
        if (sock < 0) return;
        struct iovec iov[2];
        iov[0].iov_base = (void*)cmd_str.data();
        iov[0].iov_len = cmd_str.length();
        iov[1].iov_base = (void*)"\n";
        iov[1].iov_len = 1;
        writev(sock, iov, 2);
    }

    void toggle_pause() { cmd("{\"command\":[\"cycle\",\"pause\"]}"); }
    void seek(int seconds) {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"command\":[\"seek\",%d]}", seconds);
        cmd(buf);
    }
    void playlist_next() { cmd("{\"command\":[\"playlist-next\"]}"); }
    void playlist_prev() { cmd("{\"command\":[\"playlist-prev\"]}"); }
    void toggle_mute() { cmd("{\"command\":[\"cycle\",\"mute\"]}"); }
}

//==============================================================================
// --- UI Drawing (formerly ui.cpp) ---
//==============================================================================
namespace ui {
    static void draw_video_list(int y, int h, int w, const std::vector<Video>& items, bool is_results_list) {
        int list_size = items.size();
        if (list_size == 0) return;
        int start_index = std::max(0, selection - h / 2);
        for (int i = 0; i < h && (start_index + i) < list_size; ++i) {
            int index = start_index + i;
            if (index == selection) attron(A_REVERSE);
            std::string prefix = is_results_list ? (files::is_cached(items[index]) ? "[C] " : "    ") : (index == 0 ? "[>] " : "    ");
            mvprintw(y + i, 2, "%.*s", w - 4, (prefix + items[index].title).c_str());
            if (index == selection) attroff(A_REVERSE);
        }
    }

    static void draw_subscription_list(int y, int h, int w) {
        int list_size = subscriptions.size();
        if (list_size == 0) return;
        int start_index = std::max(0, selection - h / 2);
        for (int i = 0; i < h && (start_index + i) < list_size; ++i) {
            int index = start_index + i;
            if (index == selection) attron(A_REVERSE);
            mvprintw(y + i, 2, "%.*s", w - 4, subscriptions[index].name.c_str());
            if (index == selection) attroff(A_REVERSE);
        }
    }

    void init() {
        initscr(); cbreak(); noecho();
        keypad(stdscr, TRUE); nodelay(stdscr, TRUE); curs_set(1);
    }

    void cleanup() { endwin(); }

    void draw(UiMode mode, const std::string& q, const std::string& status) {
        erase();
        int h, w;
        getmaxyx(stdscr, h, w);
        const char* mode_str = (mode == UiMode::SEARCH) ? "SEARCH" : (mode == UiMode::QUEUE) ? "QUEUE" : "SUBSCRIPTIONS";
        attron(A_BOLD);
        mvprintw(0, 1, "ytui | %s", mode_str);
        attroff(A_BOLD);
        mvprintw(0, w / 2 - 15, "Now Playing: %.40s", queue.empty() ? "<none>" : queue[0].title.c_str());

        int list_y = (mode == UiMode::SEARCH) ? 4 : 2;
        int list_h = h - list_y - 2;

        if (mode == UiMode::SEARCH) {
            mvprintw(2, 1, "> %s", q.c_str());
            curs_set(1); move(2, 3 + q.length());
            if (results.empty() && q.empty() && !history.empty()) {
                mvprintw(list_y, 1, "--- Search History ---");
                for(size_t i = 0; i < history.size() && (int)i < list_h -1; ++i)
                    mvprintw(list_y + 1 + i, 2, "%.*s", w - 4, history[i].c_str());
            } else {
                draw_video_list(list_y, list_h, w, results, true);
            }
        } else if (mode == UiMode::QUEUE) {
            curs_set(0);
            draw_video_list(list_y, list_h, w, queue, false);
        } else if (mode == UiMode::SUBSCRIPTIONS) {
            curs_set(0);
            draw_subscription_list(list_y, list_h, w);
        }

        attron(A_DIM);
        mvprintw(h - 2, 1, "%.*s", w-2, status.c_str());
        mvprintw(h - 1, 1, "q:quit TAB:view j/k:nav ENTER:play a:add x:del d:desc y:yank c:chan");
        attroff(A_DIM);
        refresh();
    }
}

//==============================================================================
// --- Main Application Logic & Input Handling ---
//==============================================================================

static void add_history(const std::string& s) {
    history.erase(std::remove(history.begin(), history.end(), s), history.end());
    history.insert(history.begin(), s);
    if (history.size() > 50) history.resize(50);
}

static void play_video(const Video& v, bool append) {
    std::string path = files::is_cached(v) ? (files::get_home_path(VIDEO_DIR) + "/" + v.id + ".mkv") : ("ytdl://" + v.id);
    std::string cmd_str = "{\"command\":[\"loadfile\",\"" + path + "\",\"" + (append ? "append-play" : "replace") + "\"]}";
    mpv::cmd(cmd_str);
    yt::download(v);
}

static void add_to_queue(const Video& v) {
    if (std::find_if(queue.begin(), queue.end(), [&](const Video& qv){ return qv.id == v.id; }) == queue.end()) {
        queue.push_back(v);
        play_video(v, true);
        status_msg = "Appended: " + v.title;
    } else {
        status_msg = "Already in queue: " + v.title;
    }
}

static void remove_from_queue(int index) {
    if (index < 0 || (size_t)index >= queue.size()) return;
    status_msg = "Removed: " + queue[index].title;
    queue.erase(queue.begin() + index);
    mpv::cmd("{\"command\":[\"playlist-remove\"," + std::to_string(index) + "]}");
    if (selection >= (int)queue.size() && !queue.empty()) selection--;
}

static void handle_search_input(int ch) {
    switch(ch) {
        case KEY_BACKSPACE: case 127: if (!query.empty()) query.pop_back(); break;
        case '\n':
            if (!query.empty()) {
                results = yt::search(query);
                add_history(query);
                selection = 0;
                status_msg = "Found " + std::to_string(results.size()) + " results for: " + query;
            }
            break;
        default: if (ch >= 32 && ch <= 126) query += (char)ch; break;
    }
}

static void handle_list_nav(int ch, int list_size) {
    switch(ch) {
        case KEY_MOVE_DOWN: if (selection < list_size - 1) selection++; break;
        case KEY_MOVE_UP:   if (selection > 0) selection--; break;
    }
}

static void show_description(const Video& v) {
    std::string desc = yt::fetch_description(v.id);
    ui::cleanup();
    printf("\n--- Description for: %s ---\n\n%s", v.title.c_str(), desc.c_str());
    printf("\n--- Press Enter to continue ---");
    while(getchar() != '\n');
    ui::init();
}

static void yank_url(const Video& v) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo -n 'https://www.youtube.com/watch?v=%s' | %s", v.id.c_str(), CLIPBOARD_CMD);
    system(cmd);
    status_msg = "Copied URL to clipboard: " + v.title;
}

static void fetch_and_show_channel(Video& v) {
    yt::get_video_context(v);
    if (v.channel.empty()) {
        status_msg = "Could not determine channel for: " + v.title;
        return;
    }
    status_msg = "Fetching videos from channel: " + v.channel;
    results = yt::fetch_channel_videos("ytsearch1:\"" + v.channel + "\" uploads");
    mode = UiMode::SEARCH;
    selection = 0;
}

static void handle_input() {
    int ch = getch();
    if (ch == ERR) return;

    if (ch == KEY_QUIT) {
        files::save_queue(queue);
        files::save_history(history);
        mpv::cmd("{\"command\":[\"quit\"]}");
        mpv::disconnect();
        ui::cleanup();
        exit(0);
    }
    if (ch == KEY_TOGGLE_VIEW) {
        mode = static_cast<UiMode>((static_cast<int>(mode) + 1) % 3);
        selection = 0;
        return;
    }

    switch(ch) {
        case KEY_TOGGLE_PAUSE: mpv::toggle_pause(); return;
        case KEY_SEEK_FORWARD: mpv::seek(5); return;
        case KEY_SEEK_BACKWARD: mpv::seek(-5); return;
        case KEY_PLAYLIST_NEXT: mpv::playlist_next(); return;
        case KEY_PLAYLIST_PREV: mpv::playlist_prev(); return;
        case KEY_TOGGLE_MUTE: mpv::toggle_mute(); return;
    }

    if (mode == UiMode::SEARCH) {
        if (results.empty()) {
            handle_search_input(ch);
        } else {
            handle_list_nav(ch, results.size());
            switch(ch) {
                case KEY_PLAY_ITEM:
                    queue.insert(queue.begin(), results[selection]);
                    play_video(queue[0], false);
                    status_msg = "Playing: " + results[selection].title;
                    break;
                case KEY_APPEND_TO_QUEUE: add_to_queue(results[selection]); break;
                case KEY_SHOW_DESCRIPTION: show_description(results[selection]); break;
                case KEY_YANK_URL: yank_url(results[selection]); break;
                case KEY_FETCH_CHANNEL: fetch_and_show_channel(results[selection]); break;
                case KEY_FETCH_RELATED:
                    query = results[selection].title;
                    results = yt::search(query);
                    selection = 0;
                    status_msg = "Showing results related to: " + query;
                    break;
                case KEY_BACKSPACE: results.clear(); query = ""; break;
            }
        }
    } else if (mode == UiMode::QUEUE) {
        handle_list_nav(ch, queue.size());
        if (queue.empty()) return;
        switch(ch) {
            case KEY_PLAY_ITEM:
                std::rotate(queue.begin(), queue.begin() + selection, queue.end());
                play_video(queue[0], false);
                for(size_t i = 1; i < queue.size(); ++i) play_video(queue[i], true);
                selection = 0;
                break;
            case KEY_REMOVE_ITEM: remove_from_queue(selection); break;
            case KEY_SHOW_DESCRIPTION: show_description(queue[selection]); break;
            case KEY_YANK_URL: yank_url(queue[selection]); break;
            case KEY_FETCH_CHANNEL: fetch_and_show_channel(queue[selection]); break;
        }
    } else if (mode == UiMode::SUBSCRIPTIONS) {
        handle_list_nav(ch, subscriptions.size());
        if (subscriptions.empty()) return;
        if (ch == KEY_PLAY_ITEM) {
            status_msg = "Fetching videos for: " + subscriptions[selection].name;
            results = yt::fetch_channel_videos(subscriptions[selection].url);
            mode = UiMode::SEARCH;
            selection = 0;
        }
    }
}

int main() {
    files::setup_dirs();
    history = files::load_history();
    queue = files::load_queue();
    subscriptions = files::load_subscriptions();

    char mpv_cmd_with_args[512];
    snprintf(mpv_cmd_with_args, sizeof(mpv_cmd_with_args), MPV_ARGS, MPV_SOCK_PATH);
    std::string final_mpv_cmd = std::string("mpv ") + mpv_cmd_with_args + " >/dev/null 2>&1 &";
    system(final_mpv_cmd.c_str());

    if (!mpv::connect(MPV_SOCK_PATH)) {
        fprintf(stderr, "Fatal: Failed to connect to mpv socket at %s. Is mpv running?\n", MPV_SOCK_PATH);
        return 1;
    }

    ui::init();
    status_msg = "Welcome to ytui! Type to search, or TAB to change view.";

    while (true) {
        ui::draw(mode, query, status_msg);
        handle_input();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    return 0;
}
