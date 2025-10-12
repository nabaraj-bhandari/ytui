#include <ncurses.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>

// MPV IPC Socket Path
const std::string MPV_SOCKET = "/tmp/mpv-socket";
const std::string CACHE_DIR = std::string(getenv("HOME")) + "/.cache/ytui";
const std::string PLAYLIST_FILE = CACHE_DIR + "/playlist.txt";
const std::string HISTORY_FILE = CACHE_DIR + "/history.txt";

// UI Modes
enum class UIMode {
    PLAYLIST,
    SEARCH_INPUT,
    SEARCH_RESULTS
};

// Video structure
struct Video {
    std::string title;
    std::string url;
    std::string video_id;
};

// Global state
std::vector<Video> playlist;
std::vector<Video> search_results;
size_t current_selection = 0;
size_t scroll_offset = 0;
int current_playing = -1;
bool is_playing = false;
std::string current_title = "No track playing";
UIMode current_mode = UIMode::PLAYLIST;
std::string search_query = "";
std::string status_message = "";

// Windows
WINDOW *header_win = nullptr;
WINDOW *main_win = nullptr;
WINDOW *footer_win = nullptr;

// MPV IPC Client
class MPVClient {
private:
    int sock = -1;
    
public:
    bool connect() {
        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) return false;
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, MPV_SOCKET.c_str(), sizeof(addr.sun_path) - 1);
        
        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            sock = -1;
            return false;
        }
        return true;
    }
    
    void send_command(const std::string& cmd) {
        if (sock < 0) return;
        std::string full_cmd = cmd + "\n";
        write(sock, full_cmd.c_str(), full_cmd.length());
    }
    
    void play_video(const std::string& url) {
        send_command("{\"command\": [\"loadfile\", \"" + url + "\"]}");
    }
    
    void toggle_pause() {
        send_command("{\"command\": [\"cycle\", \"pause\"]}");
    }
    
    void next_track() {
        send_command("{\"command\": [\"playlist-next\"]}");
    }
    
    void prev_track() {
        send_command("{\"command\": [\"playlist-prev\"]}");
    }
    
    void volume_up() {
        send_command("{\"command\": [\"add\", \"volume\", \"5\"]}");
    }
    
    void volume_down() {
        send_command("{\"command\": [\"add\", \"volume\", \"-5\"]}");
    }
    
    void quit() {
        send_command("{\"command\": [\"quit\"]}");
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
    }
    
    ~MPVClient() {
        if (sock >= 0) close(sock);
    }
};

MPVClient mpv;

// Cache directory management
void ensure_cache_dir() {
    mkdir(CACHE_DIR.c_str(), 0755);
}

// Save playlist to cache
void save_playlist() {
    std::ofstream file(PLAYLIST_FILE);
    if (!file.is_open()) return;
    
    for (const auto& video : playlist) {
        file << video.video_id << "|||" << video.title << "\n";
    }
    file.close();
}

// Load playlist from cache
void load_playlist() {
    std::ifstream file(PLAYLIST_FILE);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        size_t delim = line.find("|||");
        if (delim != std::string::npos) {
            Video v;
            v.video_id = line.substr(0, delim);
            v.title = line.substr(delim + 3);
            v.url = "https://www.youtube.com/watch?v=" + v.video_id;
            playlist.push_back(v);
        }
    }
    file.close();
}

// Add to history
void add_to_history(const Video& video) {
    std::ofstream file(HISTORY_FILE, std::ios::app);
    if (!file.is_open()) return;
    
    file << video.video_id << "|||" << video.title << "\n";
    file.close();
}

// YouTube search
std::vector<Video> search_youtube(const std::string& query) {
    std::vector<Video> results;
    std::string temp_file = "/tmp/ytui-search.txt";
    std::string cmd = "yt-dlp --no-warnings --flat-playlist --print \"%(id)s|||%(title)s\" \"ytsearch10:" + query + "\" > " + temp_file + " 2>&1";
    
    system(cmd.c_str());
    
    std::ifstream file(temp_file);
    std::string line;
    
    while (std::getline(file, line)) {
        size_t delim = line.find("|||");
        if (delim != std::string::npos) {
            Video v;
            v.video_id = line.substr(0, delim);
            v.title = line.substr(delim + 3);
            v.url = "https://www.youtube.com/watch?v=" + v.video_id;
            results.push_back(v);
        }
    }
    
    file.close();
    unlink(temp_file.c_str());
    
    return results;
}

// Initialize windows
void init_windows() {
    int height, width;
    getmaxyx(stdscr, height, width);
    
    header_win = newwin(3, width, 0, 0);
    main_win = newwin(height - 5, width, 3, 0);
    footer_win = newwin(2, width, height - 2, 0);
}

// Draw functions
void draw_header() {
    if (!header_win) return;
    
    werase(header_win);
    box(header_win, 0, 0);
    
    std::string status = is_playing ? "▶" : "⏸";
    mvwprintw(header_win, 1, 2, "%s Now Playing: %s", status.c_str(), current_title.c_str());
    
    wrefresh(header_win);
}

void draw_main() {
    if (!main_win) return;
    
    werase(main_win);
    box(main_win, 0, 0);
    
    int height, width;
    getmaxyx(main_win, height, width);
    int max_items = height - 4;
    
    if (current_mode == UIMode::SEARCH_INPUT) {
        mvwprintw(main_win, 1, 2, "Search YouTube:");
        mvwprintw(main_win, 2, 2, "> %s", search_query.c_str());
        
        if (!status_message.empty()) {
            mvwprintw(main_win, 4, 2, "%s", status_message.c_str());
        }
        
        curs_set(1);
        wmove(main_win, 2, 4 + search_query.length());
    } else {
        curs_set(0);
        
        if (current_mode == UIMode::SEARCH_RESULTS) {
            mvwprintw(main_win, 1, 2, "Search Results: \"%s\" (%zu found)", search_query.c_str(), search_results.size());
            
            // Adjust scroll offset
            if (current_selection < scroll_offset) {
                scroll_offset = current_selection;
            } else if (current_selection >= scroll_offset + max_items) {
                scroll_offset = current_selection - max_items + 1;
            }
            
            for (size_t i = 0; i < search_results.size() && i < (size_t)max_items; i++) {
                size_t idx = i + scroll_offset;
                if (idx >= search_results.size()) break;
                
                if (idx == current_selection) {
                    wattron(main_win, A_REVERSE);
                }
                
                std::string title = search_results[idx].title;
                if (title.length() > (size_t)(width - 10)) {
                    title = title.substr(0, width - 13) + "...";
                }
                
                mvwprintw(main_win, 3 + i, 4, "%zu. %s", idx + 1, title.c_str());
                
                if (idx == current_selection) {
                    wattroff(main_win, A_REVERSE);
                }
            }
        } else {
            // Playlist mode
            mvwprintw(main_win, 1, 2, "Playlist (%zu tracks)", playlist.size());
            
            if (playlist.empty()) {
                mvwprintw(main_win, 3, 4, "Empty playlist. Press '/' to search YouTube.");
            } else {
                // Adjust scroll offset
                if (current_selection < scroll_offset) {
                    scroll_offset = current_selection;
                } else if (current_selection >= scroll_offset + max_items) {
                    scroll_offset = current_selection - max_items + 1;
                }
                
                for (size_t i = 0; i < playlist.size() && i < (size_t)max_items; i++) {
                    size_t idx = i + scroll_offset;
                    if (idx >= playlist.size()) break;
                    
                    if (idx == current_selection) {
                        wattron(main_win, A_REVERSE);
                    }
                    
                    std::string prefix = (static_cast<int>(idx) == current_playing) ? "▶" : " ";
                    std::string title = playlist[idx].title;
                    if (title.length() > (size_t)(width - 10)) {
                        title = title.substr(0, width - 13) + "...";
                    }
                    
                    mvwprintw(main_win, 3 + i, 4, "%s %s", prefix.c_str(), title.c_str());
                    
                    if (idx == current_selection) {
                        wattroff(main_win, A_REVERSE);
                    }
                }
            }
            
            if (!status_message.empty()) {
                mvwprintw(main_win, height - 2, 2, "%s", status_message.c_str());
            }
        }
    }
    
    wrefresh(main_win);
}

void draw_footer() {
    if (!footer_win) return;
    
    werase(footer_win);
    
    std::string hints;
    if (current_mode == UIMode::SEARCH_INPUT) {
        hints = "Type to search | ENTER: search | ESC: cancel";
    } else if (current_mode == UIMode::SEARCH_RESULTS) {
        hints = "j/k: navigate | ENTER: play now | A: add to queue | a: add all | ESC: back | q: quit";
    } else {
        hints = "/: search | j/k: move | ENTER: play | p: pause | h/l: prev/next | +/-: volume | d: delete | s: save | q: quit";
    }
    
    mvwprintw(footer_win, 0, 2, "%s", hints.c_str());
    wrefresh(footer_win);
}

void draw_ui() {
    draw_header();
    draw_main();
    draw_footer();
    refresh();
}

// Input handling
bool handle_input() {
    int ch = getch();
    
    if (ch == ERR) {
        return true;
    }
    
    // Search input mode
    if (current_mode == UIMode::SEARCH_INPUT) {
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (!search_query.empty()) {
                status_message = "Searching YouTube...";
                draw_ui();
                
                search_results = search_youtube(search_query);
                status_message = search_results.empty() ? "No results found" : "";
                current_mode = UIMode::SEARCH_RESULTS;
                current_selection = 0;
                scroll_offset = 0;
            }
        } else if (ch == 27) { // ESC
            current_mode = UIMode::PLAYLIST;
            search_query = "";
            status_message = "";
            curs_set(0);
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!search_query.empty()) {
                search_query.pop_back();
            }
        } else if (ch >= 32 && ch <= 126) {
            search_query += static_cast<char>(ch);
        }
        return true;
    }
    
    // Search results mode
    if (current_mode == UIMode::SEARCH_RESULTS) {
        if (ch == 'j') {
            if (current_selection < search_results.size() - 1) {
                current_selection++;
            }
        } else if (ch == 'k') {
            if (current_selection > 0) {
                current_selection--;
            }
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (current_selection < search_results.size()) {
                Video selected = search_results[current_selection];
                playlist.insert(playlist.begin(), selected);
                mpv.play_video(playlist[0].url);
                current_playing = 0;
                current_title = playlist[0].title;
                is_playing = true;
                add_to_history(selected);
                save_playlist();
                current_selection = 0;
                scroll_offset = 0;
                status_message = "Now playing: " + selected.title;
            }
        } else if (ch == 'A') {
            if (current_selection < search_results.size()) {
                playlist.push_back(search_results[current_selection]);
                save_playlist();
                status_message = "Added to playlist";
            }
        } else if (ch == 'a') {
            for (const auto& video : search_results) {
                playlist.push_back(video);
            }
            save_playlist();
            status_message = "Added all " + std::to_string(search_results.size()) + " videos";
        } else if (ch == 27) { // ESC
            current_mode = UIMode::PLAYLIST;
            current_selection = 0;
            scroll_offset = 0;
        } else if (ch == 'q') {
            return false;
        }
        return true;
    }
    
    // Playlist mode
    if (ch == 'j') {
        if (current_selection < playlist.size() - 1) {
            current_selection++;
        }
    } else if (ch == 'k') {
        if (current_selection > 0) {
            current_selection--;
        }
    } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        if (current_selection < playlist.size()) {
            mpv.play_video(playlist[current_selection].url);
            current_playing = static_cast<int>(current_selection);
            current_title = playlist[current_selection].title;
            is_playing = true;
            add_to_history(playlist[current_selection]);
            status_message = "Playing: " + current_title;
        }
    } else if (ch == 'p' || ch == ' ') {
        mpv.toggle_pause();
        is_playing = !is_playing;
        status_message = is_playing ? "Playing" : "Paused";
    } else if (ch == 'l') {
        mpv.next_track();
        if (current_playing < static_cast<int>(playlist.size()) - 1) {
            current_playing++;
            current_title = playlist[current_playing].title;
            is_playing = true;
        }
    } else if (ch == 'h') {
        mpv.prev_track();
        if (current_playing > 0) {
            current_playing--;
            current_title = playlist[current_playing].title;
            is_playing = true;
        }
    } else if (ch == '+' || ch == '=') {
        mpv.volume_up();
        status_message = "Volume +5%";
    } else if (ch == '-' || ch == '_') {
        mpv.volume_down();
        status_message = "Volume -5%";
    } else if (ch == 'd') {
        if (current_selection < playlist.size()) {
            std::string deleted = playlist[current_selection].title;
            playlist.erase(playlist.begin() + current_selection);
            save_playlist();
            if (current_selection >= playlist.size() && current_selection > 0) {
                current_selection--;
            }
            status_message = "Deleted: " + deleted;
        }
    } else if (ch == 's') {
        save_playlist();
        status_message = "Playlist saved";
    } else if (ch == '/') {
        current_mode = UIMode::SEARCH_INPUT;
        search_query = "";
        status_message = "";
    } else if (ch == 'q') {
        return false;
    }
    
    return true;
}

void start_mpv() {
    std::string cmd = "mpv --idle --input-ipc-server=" + MPV_SOCKET + " --really-quiet >/dev/null 2>&1 &";
    system(cmd.c_str());
    sleep(1);
}

int main() {
    // Setup cache directory
    ensure_cache_dir();
    
    // Start MPV
    start_mpv();
    
    if (!mpv.connect()) {
        fprintf(stderr, "Failed to connect to MPV. Make sure mpv is installed.\n");
        return 1;
    }
    
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    
    // Initialize windows
    init_windows();
    
    // Load saved playlist
    load_playlist();
    
    // Main loop
    bool running = true;
    while (running) {
        draw_ui();
        running = handle_input();
        napms(16); // ~60 FPS
    }
    
    // Save playlist before exit
    save_playlist();
    
    // Cleanup
    mpv.quit();
    if (header_win) delwin(header_win);
    if (main_win) delwin(main_win);
    if (footer_win) delwin(footer_win);
    endwin();
    
    return 0;
}
