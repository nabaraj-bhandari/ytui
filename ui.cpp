#include "ui.h"
#include "structs.h"
#include "files.h"
#include "process.h"
#include <ncurses.h>
#include <algorithm>

// Global state externs
extern UiMode mode;
extern std::string query, status_msg;
extern std::vector<Video> results, queue;
extern std::vector<std::string> history;
extern std::vector<Subscription> subscriptions;
extern proc::DownloadManager downloader;
extern int selection;
extern bool show_help;

namespace ui {
    void init() {
        initscr(); cbreak(); noecho();
        keypad(stdscr, TRUE);
        curs_set(1);
        
        // **FIX**: Replace nodelay() and manual sleep with a blocking getch() with a timeout.
        // This makes typing instant while keeping CPU usage low and allowing background tasks.
        timeout(50); // Wait up to 50ms for a key before returning ERR

        start_color();
        use_default_colors();
        init_pair(1, COLOR_YELLOW, -1); // Headers
        init_pair(2, COLOR_CYAN, -1);   // Details
        init_pair(3, COLOR_GREEN, -1);  // Playing
        init_pair(4, COLOR_BLUE, -1);   // Downloading
    }

    void cleanup() { endwin(); }

    void draw_help() {
        int h, w; getmaxyx(stdscr, h, w);
        int box_h = 10, box_w = 50;
        WINDOW* win = newwin(box_h, box_w, (h-box_h)/2, (w-box_w)/2);
        box(win, 0, 0);
        wattron(win, A_BOLD); mvwprintw(win, 1, 2, "Keybindings"); wattroff(win, A_BOLD);
        mvwprintw(win, 3, 2, "j/k, PgUp/PgDn, Home/End   Navigate lists");
        mvwprintw(win, 4, 2, "ENTER                        Play/Select");
        mvwprintw(win, 5, 2, "a                            Append to queue");
        mvwprintw(win, 6, 2, "d/y/c/r                      Video actions");
        mvwprintw(win, 7, 2, "J/K (in Queue)               Reorder item");
        mvwprintw(win, 8, 2, "TAB                          Cycle views | ? Close help");
        wrefresh(win);
        delwin(win);
    }
    
    void draw() {
        erase();
        int h, w;
        getmaxyx(stdscr, h, w);

        const char* mode_str = (mode == UiMode::SEARCH)?"SEARCH":(mode == UiMode::QUEUE)?"QUEUE":"SUBSCRIPTIONS";
        attron(COLOR_PAIR(1) | A_BOLD); mvprintw(0, 1, "ytui | %s", mode_str); attroff(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, w / 2 - 20, "Now Playing: %.40s", queue.empty() ? "<none>" : queue[0].title.c_str());

        int list_y = 1, list_h = h - 3;
        if (mode == UiMode::SEARCH) {
            mvprintw(2, 1, "> %s", query.c_str());
            curs_set(1); move(2, 3 + query.length());
            list_y = 4; list_h = h - 6;
        } else {
            curs_set(0);
            list_y = 2; list_h = h - 4;
        }

        auto draw_item = [&](int y, const Video& v, bool is_selected, std::string prefix) {
            if (is_selected) {
                attron(A_REVERSE);
                mvprintw(y, 1, "%.*s", w-2, (prefix + v.title).c_str());
                attroff(A_REVERSE);
                attron(COLOR_PAIR(2));
                mvprintw(y + 1, 3, "%.*s", w-4, ("Channel: " + v.channel + " | Views: " + v.views).c_str());
                attroff(COLOR_PAIR(2));
                return 2;
            } else {
                mvprintw(y, 1, "%.*s", w-2, (prefix + " [" + v.duration + "] " + v.title).c_str());
                return 1;
            }
        };

        int y_pos = list_y;
        if (mode == UiMode::SEARCH || mode == UiMode::QUEUE) {
            const auto& items = (mode == UiMode::SEARCH) ? results : queue;
            for (size_t i = 0; i < items.size() && y_pos < list_y + list_h; ++i) {
                const auto& v = items[i];
                std::string prefix;
                if (mode == UiMode::QUEUE && i == 0) prefix = "▶ ";
                else if (downloader.is_downloading(v.id)) prefix = "↓ ";
                else if (files::is_cached(v)) prefix = "[C] ";
                else if (mode == UiMode::QUEUE) prefix = "● ";
                else prefix = "  ";
                y_pos += draw_item(y_pos, v, (int)i == selection, prefix);
            }
        } else if (mode == UiMode::SUBSCRIPTIONS) {
            for (size_t i = 0; i < subscriptions.size() && y_pos < list_y + list_h; ++i) {
                if ((int)i == selection) attron(A_REVERSE);
                mvprintw(y_pos, 2, "%.*s", w-4, subscriptions[i].name.c_str());
                if ((int)i == selection) attroff(A_REVERSE);
                y_pos++;
            }
        }
        
        attron(A_DIM); mvprintw(h - 2, 1, "%.*s", w-2, status_msg.c_str()); attroff(A_DIM);
        mvprintw(h - 1, 1, "q:Quit | TAB:View | ?:Help");

        if (show_help) draw_help();
        refresh();
    }
}
