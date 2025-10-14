#include "ui.h"
#include "config.h"
#include "types.h"
#include "youtube.h"
#include "utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ncurses.h>
#include <locale.h>
#include <ctime>
#include <algorithm>

void init_ui() {
    setlocale(LC_ALL, "");
    initscr();
    start_color();
    use_default_colors();
    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_BLACK, COLOR_CYAN);
    init_pair(3, COLOR_GREEN, -1);
    init_pair(4, COLOR_MAGENTA, -1);
    cbreak(); 
    noecho(); 
    keypad(stdscr, TRUE); 
    nodelay(stdscr, TRUE); 
    curs_set(1); // Set cursor visibility to 1 for better visibility
}

void cleanup_ui() {
    endwin();
}

// Build a list of downloads for display: on-disk files (done) followed by active/queued downloads
static std::vector<Video> get_download_items() {
    std::vector<Video> out;
    auto cached = scan_video_cache();
    out.insert(out.end(), cached.begin(), cached.end());

    for (const auto &d : downloads) {
        if (std::any_of(cached.begin(), cached.end(),
                        [&](const Video &c) { return c.id == d.v.id; }))
            continue;

        Video vv = d.v;
        vv.path = VIDEO_CACHE + "/" + vv.id + ".mkv";
        out.push_back(vv);
    }

    return out;
}

static void draw_statusline() {
    int h, w;
    getmaxyx(stdscr, h, w);
    int y = h - 1;
    
    move(y, 0);
    clrtoeol();
    
    // Mode indicator
    const char* mode_str;
    int color;
    
    if (focus == SEARCH && insert_mode) {
        mode_str = "INSERT";
        color = COLOR_PAIR(3) | A_BOLD;
    } else {
        mode_str = "NORMAL";
        color = COLOR_PAIR(1) | A_BOLD;
    }
    
    attron(color);
    mvprintw(y, 0, " %s ", mode_str);
    attroff(color);
    
    // Status message
    if (time(nullptr) - status_time < 3 && !status_msg.empty()) {
        attron(A_DIM);
        mvprintw(y, 12, "%s", status_msg.c_str());
        attroff(A_DIM);
    }

    // Key hints
    std::string hints;
    switch (focus) {
        case HOME: hints = "a:Home  s:Search  d:Downloads  w:Subs"; break;
        case SEARCH: hints = insert_mode ? "ESC:Exit insert  Enter:Run" : "i:Insert 1-9:Quick"; break;
        case RESULTS: hints = "Enter:Play  D:Download"; break;
        case DOWNLOADS: hints = "Enter:Play local"; break;
        case PROFILE: hints = "Enter:Play"; break;
        case SUBSCRIPTIONS: hints = "r:Refresh  Enter:Open channel"; break;
    }

    if (!hints.empty()) {
        attron(A_DIM);
        mvprintw(y, w - hints.length() - 2, "%s", hints.c_str());
        attroff(A_DIM);
    }

    // Compact info
    int info_x = 8;
    std::string info;
    if (focus == DOWNLOADS) {
        int total = 0, active = 0;
        auto cached = scan_video_cache();
        total = cached.size();
        for (const auto &d : downloads)
            if (d.pid > 0) active++;
        info = std::to_string(total) + " files ";
        if (active > 0) info += "| " + std::to_string(active) + " active";
    }
    if (!info.empty()) {
        attron(A_DIM);
        mvprintw(y, info_x, "%s", info.c_str());
        attroff(A_DIM);
    }
}

// Update download statuses: mark done if file exists in cache
static void refresh_download_statuses() {
    auto cached = scan_video_cache();
    for (auto &d : downloads) {
        bool found = std::any_of(cached.begin(), cached.end(),
                                 [&](const Video &c) { return c.id == d.v.id; });
        d.done = found;
        if (found) d.pid = 0;
    }
}

static void draw_section(int y, int h, const std::string &title,
                         const std::vector<Video> &items,
                         bool active, size_t offset) {
    int w = getmaxx(stdscr);
    if (active) attron(COLOR_PAIR(1) | A_BOLD);
    else attron(A_DIM);
    mvprintw(y, 0, "--- %s ", title.c_str());
    for (int i = title.length() + 5; i < w; i++) addch('-');
    attroff(COLOR_PAIR(1) | A_BOLD | A_DIM);

    for (size_t i = 0; i < items.size() && i < (size_t)h; i++) {
        size_t idx = i + offset;
        if (idx >= items.size()) break;

        bool selected = active && sel == idx;
        if (selected) attron(A_REVERSE | A_BOLD);

        std::string prefix = "  ";
        if (&items == &history)
            prefix = (idx == 0) ? "> " : "  ";
        else if (&items == &res) {
            // Check if the video is downloaded by ID, not by stored path
            static auto cached = scan_video_cache(); // small optimization, optional
            bool downloaded = std::any_of(
                cached.begin(), cached.end(),
                [&](const Video &c){ return c.id == items[idx].id; });
            prefix = downloaded ? "* " : "o ";
        }

        mvprintw(y + 1 + i, 0, "%s", prefix.c_str());
        std::string num = std::to_string(idx + 1) + ". ";
        printw("%s", num.c_str());

        int max_w = w - prefix.length() - num.length() - 2;
        std::string disp_title = items[idx].title;
        if ((int)disp_title.length() > max_w)
            disp_title = disp_title.substr(0, max_w - 3) + "...";
        printw("%s", disp_title.c_str());

        if (selected) attroff(A_REVERSE | A_BOLD);
    }
}

void draw() {
    clear();
    int h, w;
    getmaxyx(stdscr, h, w);

    if (focus == HOME) {
        std::string logo = "YTUI";
        mvprintw(3, (w - (int)logo.length()) / 2, "%s", logo.c_str());
        attron(A_BOLD);
        mvprintw(6, 4, "Keys:");
        attroff(A_BOLD);
        mvprintw(8, 6, "Shift+H: Home  Shift+F: Search  Shift+D: Downloads");
        mvprintw(9, 6, "Shift+S: Subscriptions  Shift+P: Profile");
        mvprintw(10, 6, "d: Download selected in Results  Enter: Play selected");
    } 
    else if (focus == SEARCH) {
        mvprintw(3, 4, "Search: %s", query.c_str());
        if (insert_mode) {
            int cx = 4 + 8 + (int)query_pos;
            move(3, cx);
            curs_set(1);
        } else {
            curs_set(0);
        }
        mvprintw(5, 4, "Recent: ");
        for (size_t i = 0; i < search_hist.size() && i < 5; i++)
            mvprintw(6 + i, 6, "%zu. %s", i + 1, search_hist[i].c_str());
        mvprintw(11, 6, "r   : Refresh subscriptions (on Subscriptions page)");
    }
    else if (focus == DOWNLOADS) {
        auto vids = get_download_items();
        draw_section(2, h - 4, "DOWNLOADS", vids, true, 0);

        // overlay file sizes
        for (size_t i = 0; i < vids.size() && i < (size_t)h - 4; ++i) {
            const auto &v = vids[i];
            struct stat st{};
            if (stat(v.path.c_str(), &st) == 0 && st.st_size > 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%5.1fMB", (double)st.st_size / (1024 * 1024));
                mvprintw(3 + i, w - 9, "%s", buf);
            }
        }
    }
    else if (focus == SUBSCRIPTIONS) {
        if (subs.empty()) load_subs();

        auto draw_channel_list = [&](void) {
            std::vector<Video> dummy;
            draw_section(2, h - 4, "SUBSCRIPTIONS", dummy, true, 0);
            for (size_t i = 0; i < subs.size() && i < (size_t)h - 4; ++i) {
                bool selch = (sel == i);
                if (selch) attron(A_REVERSE | A_BOLD);
                std::string name = subs[i].name.empty() ? subs[i].url : subs[i].name;
                if ((int)name.size() > w - 8)
                    name = name.substr(0, w - 11) + "...";
                mvprintw(3 + i, 2, "%2zu. %s", i + 1, name.c_str());
                if (selch) attroff(A_REVERSE | A_BOLD);
            }
        };

        if (subs_channel_idx < 0)
            draw_channel_list();
        else if (subs_cache.size() > (size_t)subs_channel_idx && !subs_cache[subs_channel_idx].empty())
            draw_section(2, h - 4, "CHANNEL: " + subs[subs_channel_idx].name,
                         subs_cache[subs_channel_idx], true, 0);
        else
            mvprintw(4, 4, "No videos loaded. Press Enter to load latest 10 videos for this channel.");
    }
    else if (focus == RESULTS) {
        draw_section(2, h - 4, "RESULTS", res, focus == RESULTS, 0);
    }
    else if (focus == PROFILE) {
        draw_section(2, h - 4, "PROFILE - History", history, focus == PROFILE, 0);
    }

    refresh_download_statuses();
    draw_statusline();

    if (show_help) {
        int hh = 10, ww = 50;
        int y0 = (h - hh) / 2, x0 = (w - ww) / 2;
        attron(A_DIM);
        for (int y = 1; y < h - 1; ++y)
            for (int x = 1; x < w - 1; ++x)
                mvaddch(y, x, ' ');
        attroff(A_DIM);
        mvaddch(y0, x0, ACS_ULCORNER);
        mvhline(y0, x0 + 1, ACS_HLINE, ww - 2);
        mvaddch(y0, x0 + ww - 1, ACS_URCORNER);
        mvvline(y0 + 1, x0, ACS_VLINE, hh - 2);
        mvvline(y0 + 1, x0 + ww - 1, ACS_VLINE, hh - 2);
        mvaddch(y0 + hh - 1, x0, ACS_LLCORNER);
        mvhline(y0 + hh - 1, x0 + 1, ACS_HLINE, ww - 2);
        mvaddch(y0 + hh - 1, x0 + ww - 1, ACS_LRCORNER);
        attron(A_BOLD);
        mvprintw(y0 + 1, x0 + 2, "YTUI Help");
        attroff(A_BOLD);
        mvprintw(y0 + 3, x0 + 2, "a:Home   s:Search   D:Downloads");
        mvprintw(y0 + 4, x0 + 2, "w:Subscriptions   p:Profile");
        mvprintw(y0 + 6, x0 + 2, "In Search: i=insert, ESC=exit, Enter=run");
        mvprintw(y0 + 7, x0 + 2, "Results: Enter=play, d=download, I=info");
    }
    refresh();
}

bool handle_input() {
    int ch = getch(); 
    if(ch == ERR) return true;

    // Global quit
    if(ch == APP_KEY_QUIT) return false;

    // Mode-specific actions
    if(focus != SEARCH) {
        if(ch == APP_KEY_RELATED) { show_related(); return true; }
        if(ch == APP_KEY_DESC) { show_description(); return true; }
        if(ch == APP_KEY_COPY) { copy_url(); return true; }
        if(ch == APP_KEY_CHANNEL) { show_channel(); return true; }
        // allow refresh of subscriptions listing
        if(ch == 'r' && focus == SUBSCRIPTIONS && subs_channel_idx >= 0) {
            if(subs_cache.size() > (size_t)subs_channel_idx) {
                subs_cache[subs_channel_idx] = fetch_videos(subs[subs_channel_idx].url, 10);
                set_status("Refreshed channel: " + subs[subs_channel_idx].name);
            }
            return true;
        }
    }
    
    // Direct single-letter jumps — but do not trigger while user is typing in Search insert mode
    if(!(focus == SEARCH && insert_mode)) {
        if(ch == APP_KEY_SUBS) { focus = SUBSCRIPTIONS; sel = 0; return true; }
        if(ch == APP_KEY_DOWNLOADS) { focus = DOWNLOADS; sel = 0; return true; }
        if(ch == APP_KEY_PROFILE) { focus = PROFILE; sel = 0; return true; }
        if(ch == APP_KEY_HOME) { focus = HOME; sel = 0; return true; }
        if(ch == APP_KEY_SEARCH) { focus = SEARCH; sel = 0; return true; }
    }
    
    // Handle input based on focus
    if(focus == SEARCH) {
        if(!insert_mode) {
            // enter insert mode only with literal 'i'
            if(ch == 'i') { insert_mode = true; query_pos = query.size(); return true; }
            // quick history selection in normal search mode
            if(ch >= '1' && ch <= '9' && query.empty()) {
                int i = ch - '1';
                if(i < (int)search_hist.size()) {
                    query = search_hist[i];
                    res = fetch_videos(query);
                    add_search_hist(query);
                    if(!res.empty()) { focus = RESULTS; sel = 0; }
                }
                return true;
            }
            if(ch == '\n' || ch == '\r') {
                if(!query.empty()) {
                    if(query == "/trending") show_trending();
                    else {
                        res = fetch_videos(query, 30);
                        add_search_hist(query);
                        focus = RESULTS; sel = 0;
                    }
                }
                return true;
            }
        } else {
            // insert mode: ESC to exit, Enter to run and exit, cursored editing
            if(ch == 27) { insert_mode = false; return true; }
            if(ch == '\n' || ch == '\r') {
                if(!query.empty()) {
                    if(query == "/trending") show_trending();
                    else {
                        res = fetch_videos(query, 30);
                        add_search_hist(query);
                        focus = RESULTS; sel = 0;
                    }
                }
                insert_mode = false;
                return true;
            }
            // left/right movement
            if(ch == KEY_LEFT) { if(query_pos > 0) query_pos--; return true; }
            if(ch == KEY_RIGHT) { if(query_pos < query.size()) query_pos++; return true; }
            // backspace (delete before cursor)
            if(ch == KEY_BACKSPACE || ch == 127 || ch == 8) { if(query_pos > 0) { query.erase(query_pos - 1, 1); query_pos--; } return true; }
            // delete at cursor
            if(ch == KEY_DC) { if(query_pos < query.size()) query.erase(query_pos, 1); return true; }
            // printable chars: insert at cursor
            if(ch >= 32 && ch <= 126) { query.insert(query_pos, 1, (char)ch); query_pos++; return true; }
        }
    } else if(focus == RESULTS) {
        if(ch == APP_KEY_DOWN && sel < res.size() - 1) sel++;
        else if(ch == APP_KEY_UP && sel > 0) sel--;
        else if((ch == '\n' || ch == '\r') && sel < res.size()) play(res[sel]);
        else if(ch == APP_KEY_DOWNLOAD && sel < res.size()) {
            const Video &v = res[sel];
            // ensure VIDEO_CACHE exists
            mkdir(VIDEO_CACHE.c_str(), 0755);
            // spawn download and track pid
            int pid = download(v);
            Download dl2; dl2.v = v; dl2.pid = pid; dl2.done = false; dl2.v.path = VIDEO_CACHE + "/" + v.id + ".mkv";
            downloads.insert(downloads.begin(), dl2);
            set_status("Downloading: " + v.title);
        }
        
    } else if(focus == PROFILE) {
        if(ch == APP_KEY_DOWN && sel < history.size() - 1) sel++;
        else if(ch == APP_KEY_UP && sel > 0) sel--;
        else if((ch == '\n' || ch == '\r') && sel < history.size()) play(history[sel]);
    }
    else if (focus == DOWNLOADS) {
        auto items = get_download_items();
        if ((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < items.size() - 1) sel++;
        else if ((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
        else if ((ch == '\n' || ch == '\r') && sel < items.size()) {
            const auto &v = items[sel];
            if (file_exists(v.path)) play(v);
            else play(v); // fallback to remote stream
        }
    }
    else if(focus == SUBSCRIPTIONS) {
        if(subs_channel_idx < 0) {
            if((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < subs.size() - 1) sel++;
            else if((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
            else if((ch == '\n' || ch == '\r') && sel < subs.size()) {
                subs_channel_idx = sel;
                // ensure cache has slot and fetch synchronously
                if(subs_cache.size() <= (size_t)subs_channel_idx) subs_cache.resize(subs_channel_idx + 1);
                subs_cache[subs_channel_idx] = fetch_videos(subs[subs_channel_idx].url, 10);
                sel = 0;
            }
        } else {
            // within channel videos
            if((ch == APP_KEY_DOWN || ch == KEY_DOWN) && subs_cache.size() > (size_t)subs_channel_idx && sel < subs_cache[subs_channel_idx].size() - 1) sel++;
            else if((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
            else if(ch == 27) { // ESC to go back to channel list
                subs_channel_idx = -1; sel = 0;
            }
            else if((ch == '\n' || ch == '\r') && subs_cache.size() > (size_t)subs_channel_idx && sel < subs_cache[subs_channel_idx].size()) {
                play(subs_cache[subs_channel_idx][sel]);
            }
        }
    }
    
    return true;
}
