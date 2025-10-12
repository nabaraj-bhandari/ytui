// ytui.cpp -- minimal robust ncurses YouTube search + mpv IPC player
// Build: make
// Requires: mpv, yt-dlp, ncurses

#include <ncurses.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cerrno>
#include <algorithm>
#include <iostream>
#include <memory>
#include <array>
#include <chrono>
#include <thread>

static const std::string MPV_SOCKET = "/tmp/ytui-mpv-socket";
static const int SEARCH_COUNT = 12;    // how many results to fetch
static const int DRAW_SLEEP_MS = 20;   // UI sleep between iterations

volatile sig_atomic_t resized = 0;

void handle_winch(int) { resized = 1; }

struct Video {
    std::string id;
    std::string title;
    std::string url() const { return "https://www.youtube.com/watch?v=" + id; }
};

// run shell-safe command and capture stdout lines via popen
static std::vector<std::string> run_cmd_capture(const std::string &cmd) {
    std::vector<std::string> lines;
    std::array<char, 4096> buffer{};
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) return lines;
    while (fgets(buffer.data(), (int)buffer.size(), fp)) {
        std::string s(buffer.data());
        // remove trailing newline(s)
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        lines.push_back(s);
    }
    pclose(fp);
    return lines;
}

// Escape a string for a single-quoted shell argument
static std::string shell_escape_single(const std::string &s) {
    if (s.empty()) return "''";
    std::string out;
    out.reserve(s.size() + 4);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out += "'\\''"; // close, escape, reopen
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

class MPV {
    int sockfd = -1;
public:
    MPV() = default;
    ~MPV() { disconnect(); }

    bool connect_with_retries(int attempts = 8, int wait_ms = 250) {
        for (int i = 0; i < attempts; ++i) {
            if (connect_socket()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        }
        return false;
    }

    bool connect_socket() {
        if (sockfd >= 0) return true;
        sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) return false;
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, MPV_SOCKET.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sockfd);
            sockfd = -1;
            return false;
        }
        // make writes non-blocking? we keep blocking writes as small cmds.
        return true;
    }

    void disconnect() {
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }
    }

    bool send_command(const std::string &json) {
        if (sockfd < 0) return false;
        std::string s = json;
        if (s.empty()) return false;
        if (s.back() != '\n') s.push_back('\n');
        ssize_t written = write(sockfd, s.c_str(), s.size());
        return written == (ssize_t)s.size();
    }

    
    bool load_url(const std::string &url, bool append = false) {
        std::string mode = append ? "append-play" : "replace";
        std::string json = std::string("{\"command\": [\"loadfile\", \"") +
                          url + "\", \"" + mode + "\"]}";
        return send_command(json);
    }

    bool play_url_direct(const std::string &url) {
        // simple loadfile replace (stop previous)
        std::string json = "{\"command\": [\"loadfile\", \"" + url + "\"]}";
        return send_command(json);
    }

    bool quit() {
        return send_command("{\"command\": [\"quit\"]}");
    }
};

// truncate utf-8 string to max_bytes safely (not splitting multi-byte chars)
static std::string utf8_truncate(const std::string &s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    size_t i = 0;
    while (i < max_bytes) {
        unsigned char c = (unsigned char)s[i];
        size_t len = 1;
        if (c < 0x80) len = 1;
        else if ((c >> 5) == 0x6) len = 2;
        else if ((c >> 4) == 0xE) len = 3;
        else if ((c >> 3) == 0x1E) len = 4;
        else break;
        if (i + len > max_bytes) break;
        i += len;
    }
    return s.substr(0, i) + "...";
}

class UI {
    WINDOW *win = nullptr;
public:
    UI() { initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); nodelay(stdscr, TRUE); curs_set(0); }
    ~UI() {
        if (win) delwin(win);
        endwin();
    }

    void clear_and_create() {
        if (win) { delwin(win); win = nullptr; }
        int h, w; getmaxyx(stdscr, h, w);
        win = newwin(h, w, 0, 0);
    }

    void draw_search(const std::string &query) {
        werase(win);
        int h, w; getmaxyx(win, h, w);
        box(win, 0, 0);
        mvwprintw(win, 1, 2, "Search YouTube (type, Enter to search, q to quit):");
        // show query on second line
        std::string qdisp = query.empty() ? "<empty>" : query;
        // truncate if too long
        if (qdisp.size() > (size_t)(w - 6)) qdisp = utf8_truncate(qdisp, w - 6);
        mvwprintw(win, 3, 4, "%s", qdisp.c_str());
        // position cursor
        wmove(win, 3, 4 + (int)qdisp.size());
        curs_set(1);
        wrefresh(win);
    }

    void draw_results(const std::string &query, const std::vector<Video> &results, size_t sel) {
        werase(win);
        int h, w; getmaxyx(win, h, w);
        box(win, 0, 0);
        curs_set(0);
        std::string header = "Results for: \"" + query + "\" (j/k:move Enter:play /:search q:quit)";
        if ((int)header.size() > w - 4) header = header.substr(0, w - 7) + "...";
        mvwprintw(win, 1, 2, "%s", header.c_str());
        int max_display = h - 4;
        for (int i = 0; i < max_display && (size_t)i < results.size(); ++i) {
            size_t idx = i;
            if (idx >= results.size()) break;
            if (idx == sel) wattron(win, A_REVERSE);
            std::string t = results[idx].title;
            if (t.size() > (size_t)(w - 8)) t = utf8_truncate(t, w - 8);
            mvwprintw(win, 3 + i, 4, "%2zu. %s", idx + 1, t.c_str());
            if (idx == sel) wattroff(win, A_REVERSE);
        }
        wrefresh(win);
    }

    int getch_nonblocking() {
        return wgetch(win);
    }
};

static std::vector<Video> search_youtube(const std::string &query) {
    std::vector<Video> vids;
    if (query.empty()) return vids;

    // Build command: yt-dlp --no-warnings --flat-playlist --print "%(id)s|||%(title)s" "ytsearchN:QUERY"
    std::string escaped = shell_escape_single(query);
    std::string cmd = "yt-dlp --no-warnings --flat-playlist --print \"%(id)s|||%(title)s\" \"ytsearch" +
                      std::to_string(SEARCH_COUNT) + ":" + escaped + "\" 2>/dev/null";
    auto lines = run_cmd_capture(cmd);
    for (auto &ln : lines) {
        auto pos = ln.find("|||");
        if (pos == std::string::npos) continue;
        Video v;
        v.id = ln.substr(0, pos);
        v.title = ln.substr(pos + 3);
        vids.push_back(v);
    }
    return vids;
}

int main() {
    // Install signal handler for resize
    struct sigaction sa{};
    sa.sa_handler = handle_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    UI ui;
    ui.clear_and_create();

    // Start mpv in background with socket
    // Use a dedicated socket path to avoid clobbering user's mpv
    std::string mpv_cmd = "mpv --idle --input-ipc-server=" + MPV_SOCKET + " --really-quiet >/dev/null 2>&1 &";
    int rc = system(mpv_cmd.c_str());
    (void)rc; // ignore immediate rc; we'll try to connect

    MPV mpv;
    if (!mpv.connect_with_retries(16, 200)) {
        // Could not connect to mpv - present an error and exit
        endwin();
        std::cerr << "ERROR: Failed to connect to mpv IPC socket at " << MPV_SOCKET << "\n";
        std::cerr << "Make sure mpv and --input-ipc-server are available and writable.\n";
        return 1;
    }

    std::string query;
    std::vector<Video> results;
    size_t sel = 0;
    bool in_search = true;
    bool running = true;

    while (running) {
        if (resized) {
            resized = 0;
            ui.clear_and_create();
        }

        if (in_search) ui.draw_search(query);
        else ui.draw_results(query, results, sel);

        int ch = ui.getch_nonblocking();
        if (ch == ERR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(DRAW_SLEEP_MS));
            continue;
        }

        if (in_search) {
            if (ch == '\n' || ch == '\r') {
                // perform search
                results = search_youtube(query);
                sel = 0;
                in_search = false;
            } else if (ch == 27) { // ESC
                query.clear();
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (!query.empty()) query.pop_back();
            } else if (ch == 'q' || ch == 'Q') {
                running = false;
            } else if (ch >= 32 && ch <= 126) {
                // printable
                query.push_back((char)ch);
            } else if (ch == '/') {
                // already in search
            }
        } else {
            if (ch == 'j' || ch == KEY_DOWN) {
                if (sel + 1 < results.size()) sel++;
            } else if (ch == 'k' || ch == KEY_UP) {
                if (sel > 0) sel--;
            } else if (ch == '\n' || ch == '\r') {
                if (sel < results.size()) {
                    // play selection (replace)
                    mpv.play_url_direct(results[sel].url());
                }
            } else if (ch == '/') {
                in_search = true;
                query.clear();
            } else if (ch == 'q' || ch == 'Q') {
                running = false;
            } else if (ch == 'r') {
                // re-run search
                results = search_youtube(query);
                if (sel >= results.size()) sel = results.empty() ? 0 : results.size() - 1;
            }
        }
    }

    // attempt graceful mpv quit
    mpv.quit();
    // UI destructor will cleanup ncurses
    return 0;
}

