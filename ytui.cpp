#include "config.h"
#include "structs.h"
#include "files.h"
#include "youtube.h"
#include "process.h"
#include "ui.h"

#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>

#include <ncurses.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string_view>

// === Global State ===
UiMode mode = UiMode::SEARCH;
std::string query, status_msg;
std::vector<Video> results, queue;
std::vector<std::string> history;
std::vector<Subscription> subscriptions;
proc::DownloadManager downloader;
int selection = 0;
bool show_help = false;

namespace mpv {
    static int sock = -1;
    bool connect(const char* path) {
        for (int i = 0; i < 10; ++i) {
            sock = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock < 0) return false;
            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
            if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) return true;
            close(sock);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return false;
    }
    void cmd(const std::string_view& str) {
        if (sock < 0) return;
        struct iovec iov[2] = {{ (void*)str.data(), str.length() }, { (void*)"\n", 1 }};
        writev(sock, iov, 2);
    }
    void toggle_pause() { cmd("{\"command\":[\"cycle\",\"pause\"]}"); }
}

void play_video(const Video& v, bool append) {
    std::string path = files::is_cached(v) ? (files::get_home_path(VIDEO_DIR) + "/" + v.id + ".mkv") : ("ytdl://" + v.id);
    std::string cmd_str = "{\"command\":[\"loadfile\",\"" + path + "\",\"" + (append ? "append-play" : "replace") + "\"]}";
    mpv::cmd(cmd_str);
    if (!files::is_cached(v) && !downloader.is_downloading(v.id)) {
        downloader.add(v.id, youtube::download(v));
    }
}

void add_to_queue(const Video& v) {
    if (std::find_if(queue.begin(), queue.end(), [&](const auto& qv){ return qv.id == v.id; }) == queue.end()) {
        queue.push_back(v);
        play_video(v, true);
        status_msg = "Appended: " + v.title;
    }
}

void remove_from_queue(int index) {
    if (index < 0 || (size_t)index >= queue.size()) return;
    mpv::cmd("{\"command\":[\"playlist-remove\"," + std::to_string(index) + "]}");
    queue.erase(queue.begin() + index);
    if (!queue.empty() && selection >= (int)queue.size()) selection--;
}

void handle_input() {
    int ch = getch();
    if (ch == ERR) return; // ERR is returned on timeout if no key is pressed

    if (ch == KEY_TOGGLE_HELP) { show_help = !show_help; return; }
    if (show_help) return;

    int list_size = 0;
    if (mode == UiMode::SEARCH) list_size = results.size();
    else if (mode == UiMode::QUEUE) list_size = queue.size();
    else if (mode == UiMode::SUBSCRIPTIONS) list_size = subscriptions.size();

    switch (ch) {
        case KEY_QUIT: exit(0);
        case KEY_TOGGLE_VIEW:
            mode = static_cast<UiMode>((static_cast<int>(mode) + 1) % 3);
            selection = 0;
            return;
        case KEY_TOGGLE_PAUSE: mpv::toggle_pause(); return;
        case KEY_SEEK_FORWARD: mpv::cmd("{\"command\":[\"seek\",5]}"); return;
        case KEY_SEEK_BACKWARD: mpv::cmd("{\"command\":[\"seek\",-5]}"); return;
        case KEY_MOVE_DOWN: if(selection < list_size - 1) selection++; break;
        case KEY_MOVE_UP: if(selection > 0) selection--; break;
        case KEY_NPAGE: selection = std::min(list_size - 1, selection + 10); break;
        case KEY_PPAGE: selection = std::max(0, selection - 10); break;
        case KEY_HOME: selection = 0; break;
        case KEY_END: if(list_size > 0) selection = list_size - 1; break;
    }

    if (mode == UiMode::SEARCH) {
        if (results.empty()) { // Search input mode
             if (ch == '\n' && !query.empty()) {
                results = youtube::search(query);
                selection = 0;
                history.erase(std::remove(history.begin(), history.end(), query), history.end());
                history.insert(history.begin(), query);
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (!query.empty()) query.pop_back();
            } else if (ch >= 32 && ch <= 126) {
                query += (char)ch;
            }
        } else { // Results list mode
            if (ch == KEY_PLAY_ITEM) {
                queue.insert(queue.begin(), results[selection]);
                play_video(queue[0], false);
            } else if (ch == KEY_APPEND_TO_QUEUE) {
                add_to_queue(results[selection]);
            }
        }
    } else if (mode == UiMode::QUEUE) {
        if (!queue.empty()) {
            if (ch == KEY_PLAY_ITEM) {
                std::rotate(queue.begin(), queue.begin() + selection, queue.end());
                play_video(queue[0], false);
                for (size_t i = 1; i < queue.size(); ++i) play_video(queue[i], true);
                selection = 0;
            } else if (ch == KEY_REMOVE_ITEM) {
                remove_from_queue(selection);
            } else if (ch == KEY_MOVE_ITEM_DOWN && selection < (int)queue.size() - 1) {
                std::swap(queue[selection], queue[selection+1]);
                selection++;
            } else if (ch == KEY_MOVE_ITEM_UP && selection > 0) {
                std::swap(queue[selection], queue[selection-1]);
                selection--;
            }
        }
    } else if (mode == UiMode::SUBSCRIPTIONS) {
        if (ch == KEY_PLAY_ITEM && !subscriptions.empty()) {
            results = youtube::fetch_channel_videos(subscriptions[selection].url);
            mode = UiMode::SEARCH;
            selection = 0;
        }
    }
    
    const Video* selected_video = nullptr;
    if (mode == UiMode::SEARCH && !results.empty()) selected_video = &results[selection];
    else if (mode == UiMode::QUEUE && !queue.empty()) selected_video = &queue[selection];

    if (selected_video) {
        switch(ch) {
            case KEY_SHOW_DESCRIPTION: {
                std::string desc = youtube::fetch_description(selected_video->id);
                ui::cleanup();
                proc::spawn_pager(desc);
                ui::init();
                break;
            }
            case KEY_YANK_URL: {
                proc::spawn_clipboard("https://www.youtube.com/watch?v=" + selected_video->id);
                status_msg = "Copied URL to clipboard";
                break;
            }
            case KEY_FETCH_CHANNEL: {
                Video temp_v = *selected_video;
                youtube::get_video_context(temp_v);
                if (!temp_v.channel.empty()) {
                    results = youtube::fetch_channel_videos("ytsearch1:\"" + temp_v.channel + "\" uploads");
                    mode = UiMode::SEARCH;
                    selection = 0;
                }
                break;
            }
            case KEY_FETCH_RELATED: {
                results = youtube::search(selected_video->title);
                mode = UiMode::SEARCH;
                selection = 0;
                break;
            }
        }
    }
}

void cleanup() {
    files::save_queue(queue);
    files::save_history(history);
    mpv::cmd("{\"command\":[\"quit\"]}");
    ui::cleanup();
}

int main() {
    files::setup_dirs();
    history = files::load_history();
    queue = files::load_queue();
    subscriptions = files::load_subscriptions();
    atexit(cleanup);
    
    proc::launch_daemon({MPV_EXECUTABLE, "--input-ipc-server=" + std::string(MPV_SOCK_PATH),
        "--ytdl-format=" + std::string(YTDL_FORMAT), "--idle", "--really-quiet", "--hwdec=auto", "--fullscreen=yes"});

    if (!mpv::connect(MPV_SOCK_PATH)) {
        fprintf(stderr, "Fatal: Failed to connect to mpv socket at %s\n", MPV_SOCK_PATH);
        return 1;
    }

    ui::init();
    status_msg = "Welcome to ytui! Type to search, or press '?' for help.";

    while (true) {
        downloader.check_finished();
        ui::draw();
        handle_input();
        // **FIX**: The manual sleep is removed. Responsiveness is now handled by timeout() in ui::init().
    }
    return 0;
}
