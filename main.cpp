#include <ncurses.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include <cstdlib>

const std::string CACHE = std::string(getenv("HOME")) + "/.cache/ytui";
const std::string HISTORY = CACHE + "/history.txt";

struct Video {
    std::string id;
    std::string title;
};

std::vector<std::string> history;
std::vector<Video> results;
std::string query = "";
size_t sel = 0;
bool in_results = false;

void save_history() {
    mkdir(CACHE.c_str(), 0755);
    std::ofstream f(HISTORY);
    for (const auto& s : history) f << s << "\n";
}

void load_history() {
    std::ifstream f(HISTORY);
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) history.push_back(line);
}

void add_history(const std::string& q) {
    auto it = std::find(history.begin(), history.end(), q);
    if (it != history.end()) history.erase(it);
    history.insert(history.begin(), q);
    if (history.size() > 50) history.resize(50);
    save_history();
}

std::vector<Video> search(const std::string& q) {
    std::vector<Video> res;
    std::string cmd = "yt-dlp --no-warnings --flat-playlist --print \"%(id)s|||%(title)s\" \"ytsearch20:" + q + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return res;
    
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        size_t d = line.find("|||");
        if (d != std::string::npos) {
            Video v;
            v.id = line.substr(0, d);
            v.title = line.substr(d + 3);
            if (!v.title.empty() && v.title.back() == '\n')
                v.title.pop_back();
            res.push_back(v);
        }
    }
    pclose(pipe);
    return res;
}

void play(const std::string& id) {
    std::string cmd = "mpv \"https://www.youtube.com/watch?v=" + id + "\" >/dev/null 2>&1 &";
    system(cmd.c_str());
}

void draw() {
    clear();
    
    if (!in_results) {
        mvprintw(0, 0, "Search: %s", query.c_str());
        
        if (query.empty() && !history.empty()) {
            mvprintw(2, 0, "History (1-9 to load):");
            for (size_t i = 0; i < history.size() && i < 9; i++)
                mvprintw(3 + i, 2, "%zu. %s", i + 1, history[i].c_str());
        }
        
        int row, col;
        getyx(stdscr, row, col);
        move(0, 8 + query.length());
        curs_set(1);
    } else {
        curs_set(0);
        mvprintw(0, 0, "Results: \"%s\" (j/k to move, ENTER to play, ESC to search)", query.c_str());
        
        for (size_t i = 0; i < results.size(); i++) {
            if (i == sel) attron(A_REVERSE);
            mvprintw(2 + i, 2, "%zu. %s", i + 1, results[i].title.c_str());
            if (i == sel) attroff(A_REVERSE);
        }
    }
    
    refresh();
}

bool input() {
    int ch = getch();
    if (ch == ERR) return true;
    
    if (!in_results) {
        if (ch >= '1' && ch <= '9') {
            int idx = ch - '1';
            if (idx < (int)history.size()) {
                query = history[idx];
                results = search(query);
                add_history(query);
                if (!results.empty()) {
                    in_results = true;
                    sel = 0;
                }
            }
        } else if (ch == '\n' || ch == '\r') {
            if (!query.empty()) {
                results = search(query);
                add_history(query);
                if (!results.empty()) {
                    in_results = true;
                    sel = 0;
                }
            }
        } else if (ch == 27 || ch == 'q') {
            return false;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!query.empty()) query.pop_back();
        } else if (ch >= 32 && ch <= 126) {
            query += (char)ch;
        }
    } else {
        if (ch == 'j' && sel < results.size() - 1) sel++;
        else if (ch == 'k' && sel > 0) sel--;
        else if (ch == '\n' || ch == '\r') {
            play(results[sel].id);
        } else if (ch == 27) {
            in_results = false;
            query = "";
            sel = 0;
        } else if (ch == 'q') {
            return false;
        }
    }
    
    return true;
}

int main() {
    load_history();
    
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);
    nodelay(stdscr, TRUE);
    
    bool run = true;
    while (run) {
        draw();
        run = input();
        napms(16);
    }
    
    endwin();
    return 0;
}
