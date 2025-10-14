// Project headers
#include "ui.h"
#include "config.h"
#include "types.h"
#include "youtube.h"
#include "utils.h"

// Standard / system headers
#include <locale.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <algorithm>

// Use global MAX_LIST_ITEMS from config.h

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

    // Key hints (mode+hints on left)
    std::string left;
    switch (focus) {
        case HOME: left = "a:Home s:Search d:Downloads w:Subs"; break;
        case SEARCH: left = insert_mode ? "TAB:Toggle Enter:Search" : "TAB:Toggle j/k:Up/Down"; break;
        case RESULTS: left = "Enter=Play D=Download"; break;
        case CHANNEL: left = "Enter=Play D=Download S=ToggleSub"; break;
        case DOWNLOADS: left = "Enter=Play D=Download"; break;
        case PROFILE: left = "Enter=Play D=Download"; break;
        case SUBSCRIPTIONS: left = "r=Refresh Enter=Open"; break;
    }

    if (!left.empty()) {
        attron(A_DIM);
        mvprintw(y, 2, "%s", left.c_str());
        attroff(A_DIM);
    }

    // Short-lived info on the right
    if (time(nullptr) - status_time < 3 && !status_msg.empty()) {
        attron(A_BOLD);
        int start = w - (int)status_msg.length() - 2;
        if (start < (int)left.length() + 4) start = (int)left.length() + 4; // avoid overlap
        mvprintw(y, start, "%s", status_msg.c_str());
        attroff(A_BOLD);
    }

    // Compact info
    int info_x = w/2;
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

    size_t max_display = visible_count((size_t)h, items.size());
    for (size_t i = 0; i < max_display; i++) {
        size_t idx = i + offset;
        if (idx >= items.size()) break;

        bool selected = active && sel == idx;
        if (selected) attron(A_REVERSE | A_BOLD);

        // Mark downloaded videos with '*' and non-downloaded with 'o' for all video lists
        std::string prefix = is_video_downloaded(items[idx]) ? "* " : "o ";

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

// Draw the search UI (header, input box, recent searches)
static void draw_search_ui() {
    const int box_x = 4;
    const std::string label = "SEARCH: ";

    if (insert_mode) {
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(3, box_x, "%s%s", label.c_str(), query.c_str());
        attroff(A_BOLD | COLOR_PAIR(1));
        int cx = box_x + (int)label.length() + (int)query_pos;
        move(3, cx);
        curs_set(1);
    } else {
        attron(A_DIM);
        mvprintw(3, box_x, "%s%s", label.c_str(), query.c_str());
        attroff(A_DIM);
        curs_set(0);
    }

    // recent searches
    mvprintw(5, box_x, "RECENT SEARCHES: ");
    for (size_t i = 0; i < search_hist.size() && i < 10; i++) {
        int row = 6 + i;
        bool selh = (search_hist_idx == (int)i) && !insert_mode;
        if (selh) attron(A_REVERSE | A_BOLD);
        mvprintw(row, box_x + 2, "%2zu. %s", i + 1, search_hist[i].c_str());
        if (selh) attroff(A_REVERSE | A_BOLD);
    }
}

// Draw downloads list and overlay file sizes
static void draw_downloads_ui(int h, int w) {
    auto vids = get_download_items();
    draw_section(2, h - 4, "DOWNLOADS", vids, true, 0);

    size_t display_count = visible_count((size_t)h - 4, vids.size());
    for (size_t i = 0; i < display_count; ++i) {
        const auto &v = vids[i];
        struct stat st{};
        if (stat(v.path.c_str(), &st) == 0 && st.st_size > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%5.1fMB", (double)st.st_size / (1024 * 1024));
            mvprintw(3 + i, w - 9, "%s", buf);
        }
    }
}

// Draw subscriptions list or the cached channel videos for a subscription
static void draw_subscriptions_ui(int h, int w) {
    if (subs.empty()) load_subs();

    auto draw_channel_list = [&](void) {
        std::vector<Video> dummy;
        draw_section(2, h - 4, "SUBSCRIPTIONS", dummy, true, 0);
        size_t display_count = visible_count((size_t)h - 4, subs.size());
        for (size_t i = 0; i < display_count; ++i) {
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
        mvprintw(4, 4, "No videos loaded for this channel.");
}

void draw() {
    clear();
    int h, w;
    getmaxyx(stdscr, h, w);

    if (focus == HOME) {
        std::string logo = "YTUI";
        mvprintw(3, (w - (int)logo.length()) / 2, "%s", logo.c_str());
    }
    else if (focus == SEARCH) {
        draw_search_ui();
    }
    else if (focus == DOWNLOADS) {
        draw_downloads_ui(h, w);
    }
    else if (focus == SUBSCRIPTIONS) {
        draw_subscriptions_ui(h, w);
    }
    else if (focus == RESULTS) {
        draw_section(2, h - 4, "RESULTS", res, focus == RESULTS, 0);
    }
    else if (focus == CHANNEL) {
        draw_section(2, h - 4, "CHANNEL: " + channel_name, channel_videos, true, 0);
        bool is_sub = is_subscribed(channel_url, channel_name);
        std::string badge = is_sub ? "[SUB]" : "[+SUB]";
        mvprintw(2, getmaxx(stdscr) - (int)badge.length() - 2, "%s", badge.c_str());
    }
    else if (focus == PROFILE) {
        draw_section(2, h - 4, "PROFILE - History", history, focus == PROFILE, 0);
    }

    refresh_download_statuses();
    draw_statusline();

    refresh();
}

bool handle_input() {
    int ch = getch();
    if(ch == ERR) return true;

    // Global quit
    if(ch == APP_KEY_QUIT) return false;

    if (!(focus == SEARCH && insert_mode)) {
        if (ch == 'j' || ch == 'J') ch = APP_KEY_DOWN;
        else if (ch == 'k' || ch == 'K') ch = APP_KEY_UP;
        else if (ch == 'l' || ch == 'L') ch = '\n';
        else if (ch == 'h' || ch == 'H') ch = 27; // ESC / back
    }

    // Mode-specific actions
    if(focus != SEARCH) {
        if(ch == APP_KEY_CHANNEL) {
            if(focus == RESULTS && sel < res.size()) { show_channel_for(res[sel]); }
            else if(focus == PROFILE && sel < history.size()) { show_channel_for(history[sel]); }
            else if(focus == DOWNLOADS) {
                auto items = get_download_items();
                if(sel < items.size()) show_channel_for(items[sel]);
            }
            return true;
        }
        // allow refresh of subscriptions listing
        if(ch == 'r' && focus == SUBSCRIPTIONS && subs_channel_idx >= 0) {
            if(subs_cache.size() > (size_t)subs_channel_idx) {
                subs_cache[subs_channel_idx] = fetch_videos(subs[subs_channel_idx].url, 10);
                set_status("Refreshed channel: " + subs[subs_channel_idx].name);
            }
            return true;
        }
    }

    if(!(focus == SEARCH && insert_mode)) {
        if(ch == APP_KEY_SUBS) { focus = SUBSCRIPTIONS; sel = 0; return true; }
        if(ch == APP_KEY_DOWNLOADS) { focus = DOWNLOADS; sel = 0; return true; }
        if(ch == APP_KEY_PROFILE) { focus = PROFILE; sel = 0; return true; }
        if(ch == APP_KEY_HOME) { focus = HOME; sel = 0; return true; }
        if(ch == APP_KEY_SEARCH) { focus = SEARCH; sel = 0; return true; }
    }

    // Handle input based on focus
    if(focus == SEARCH) {
        if(ch == '\t') { insert_mode = !insert_mode; query_pos = query.size(); curs_set(insert_mode ? 1 : 0); return true; }

        if(!insert_mode) {
            // navigate recent search list
            if((ch == APP_KEY_DOWN || ch == KEY_DOWN) && !search_hist.empty()) {
                if(search_hist_idx < (int)search_hist.size() - 1) search_hist_idx++;
                else search_hist_idx = 0;
                return true;
            }
            if((ch == APP_KEY_UP || ch == KEY_UP) && !search_hist.empty()) {
                if(search_hist_idx > 0) search_hist_idx--;
                else search_hist_idx = (int)search_hist.size() - 1;
                return true;
            }
            if(ch == '\n' || ch == '\r') {
                if(search_hist_idx >= 0 && search_hist_idx < (int)search_hist.size()) {
                    query = search_hist[search_hist_idx];
                }
                if(!query.empty()) {
                  res = fetch_videos(query, MAX_LIST_ITEMS);
                  add_search_hist(query);
                  focus = RESULTS; sel = 0;
                }
                return true;
            }
        } else {
            // insert mode: Enter to run and exit, cursored editing
            if(ch == '\n' || ch == '\r') {
                if(!query.empty()) {
                  res = fetch_videos(query, MAX_LIST_ITEMS);
                  add_search_hist(query);
                  focus = RESULTS; sel = 0;
                }
                insert_mode = false; curs_set(0);
                return true;
            }
            // left/right movement
            if(ch == KEY_LEFT) { if(query_pos > 0) query_pos--; return true; }
            if(ch == KEY_RIGHT) { if(query_pos < query.size()) query_pos++; return true; }
            if(ch == KEY_BACKSPACE || ch == 127 || ch == 8) { if(query_pos > 0) { query.erase(query_pos - 1, 1); query_pos--; } return true; }
            if(ch == KEY_DC) { if(query_pos < query.size()) query.erase(query_pos, 1); return true; }
            if(ch >= 32 && ch <= 126) { query.insert(query_pos, 1, (char)ch); query_pos++; return true; }
        }
    } else if(focus == RESULTS) {
        if(ch == APP_KEY_DOWN && sel < res.size() - 1) sel++;
        else if(ch == APP_KEY_UP && sel > 0) sel--;
        else if((ch == '\n' || ch == '\r') && sel < res.size()) play(res[sel]);
        else if(ch == APP_KEY_DOWNLOAD && sel < res.size()) {
            const Video &v = res[sel];
            enqueue_download(v);
        }

    } else if(focus == PROFILE) {
        if(ch == APP_KEY_DOWN && sel < history.size() - 1) sel++;
        else if(ch == APP_KEY_UP && sel > 0) sel--;
        else if((ch == '\n' || ch == '\r') && sel < history.size()) play(history[sel]);
        else if(ch == APP_KEY_DOWNLOAD && sel < history.size()) {
            const Video &v = history[sel];
            enqueue_download(v);
        }
    }
    else if (focus == CHANNEL) {
        if (channel_videos.empty()) return true;
        if((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < channel_videos.size() - 1) sel++;
        else if((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
        else if(ch == 27) { // ESC back to subscriptions
            focus = SUBSCRIPTIONS; sel = 0; subs_channel_idx = -1; return true;
        }
        else if((ch == '\n' || ch == '\r') && sel < channel_videos.size()) play(channel_videos[sel]);
        else if(ch == APP_KEY_DOWNLOAD && sel < channel_videos.size()) enqueue_download(channel_videos[sel]);
        else if(ch == APP_KEY_SUB_TOGGLE) {
            bool now_sub = toggle_subscription(channel_name, channel_url);
            set_status(now_sub ? std::string("Subscribed: ") + channel_name : std::string("Unsubscribed: ") + channel_name);
            return true;
        }
    }
    else if (focus == DOWNLOADS) {
        auto items = get_download_items();
        if ((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < items.size() - 1) sel++;
        else if ((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
        else if ((ch == '\n' || ch == '\r') && sel < items.size()) {
            const auto &v = items[sel];
            if (file_exists(v.path)) play(v);
            else play(v); // fallback to remote stream
        } else if (ch == APP_KEY_DOWNLOAD && sel < items.size()) {
            enqueue_download(items[sel]);
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
            } else if (ch == APP_KEY_DOWNLOAD && subs_cache.size() > (size_t)subs_channel_idx && sel < subs_cache[subs_channel_idx].size()) {
                enqueue_download(subs_cache[subs_channel_idx][sel]);
            }
        }
    }

    return true;
}
