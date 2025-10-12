#include <ncurses.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <sstream>
#include <fstream>

// MPV IPC Socket Path
const std::string MPV_SOCKET = "/tmp/mpv-socket";

// Playlist structure
struct Video {
    std::string title;
    std::string url;
};

// Global state
std::vector<Video> playlist;
int current_selection = 0;
int current_playing = -1;
bool is_playing = false;
std::string current_title = "No track playing";

// MPV IPC Communication
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

// Initialize hardcoded playlist
void init_playlist() {
    // Example hardcoded URLs - replace with real YouTube URLs for testing
    playlist = {
        {"Lofi Hip Hop Radio", "https://www.youtube.com/watch?v=jfKfPfyJRdk"},
        {"Synthwave Mix", "https://www.youtube.com/watch?v=4xDzrJKXOOY"},
        {"Jazz for Work", "https://www.youtube.com/watch?v=kgx4WGK0oNU"}
    };
}

// Draw the TUI
void draw_ui() {
    clear();
    
    int height, width;
    getmaxyx(stdscr, height, width);
    
    // Top bar - current playing
    attron(A_REVERSE);
    mvprintw(0, 0, "%*s", width, " ");
    std::string status = is_playing ? "▶ " : "⏸ ";
    mvprintw(0, 2, "%s%s", status.c_str(), current_title.c_str());
    attroff(A_REVERSE);
    
    // Playlist
    mvprintw(2, 2, "Playlist:");
    for (size_t i = 0; i < playlist.size(); i++) {
        if (i == current_selection) {
            attron(A_REVERSE);
        }
        if (i == current_playing) {
            mvprintw(4 + i, 4, "▶ %s", playlist[i].title.c_str());
        } else {
            mvprintw(4 + i, 4, "  %s", playlist[i].title.c_str());
        }
        if (i == current_selection) {
            attroff(A_REVERSE);
        }
    }
    
    // Bottom bar - keybindings
    mvprintw(height - 2, 2, "j/k: navigate | ENTER: play | p: pause | h/l: prev/next | q: quit");
    
    refresh();
}

// Handle key input
bool handle_input() {
    int ch = getch();
    
    switch (ch) {
        case 'j':
            if (current_selection < (int)playlist.size() - 1) {
                current_selection++;
            }
            break;
            
        case 'k':
            if (current_selection > 0) {
                current_selection--;
            }
            break;
            
        case '\n': // ENTER
        case '\r':
            if (current_selection >= 0 && current_selection < (int)playlist.size()) {
                mpv.play_video(playlist[current_selection].url);
                current_playing = current_selection;
                current_title = playlist[current_selection].title;
                is_playing = true;
            }
            break;
            
        case 'p':
            mpv.toggle_pause();
            is_playing = !is_playing;
            break;
            
        case 'l':
            mpv.next_track();
            if (current_playing < (int)playlist.size() - 1) {
                current_playing++;
                current_title = playlist[current_playing].title;
            }
            break;
            
        case 'h':
            mpv.prev_track();
            if (current_playing > 0) {
                current_playing--;
                current_title = playlist[current_playing].title;
            }
            break;
            
        case 'q':
            return false;
    }
    
    return true;
}

// Start MPV daemon
void start_mpv() {
    // Start mpv with IPC socket
    std::string cmd = "mpv --no-video --idle --input-ipc-server=" + MPV_SOCKET + " &";
    system(cmd.c_str());
    sleep(1); // Wait for mpv to start
}

int main() {
    // Start MPV
    start_mpv();
    
    // Connect to MPV
    if (!mpv.connect()) {
        fprintf(stderr, "Failed to connect to MPV. Make sure mpv is running.\n");
        return 1;
    }
    
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100); // Non-blocking input
    
    // Initialize playlist
    init_playlist();
    
    // Main loop
    bool running = true;
    while (running) {
        draw_ui();
        running = handle_input();
    }
    
    // Cleanup
    mpv.quit();
    endwin();
    
    return 0;
}
