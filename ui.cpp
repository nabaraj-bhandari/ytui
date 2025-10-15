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
#include <functional>
#include <iterator>

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

    // Short-lived info on the right
    if (time(nullptr) - status_time < 3 && !status_msg.empty()) {
        attron(A_BOLD);
        int start = w - (int)status_msg.length() - 2;
        if (start < 2) start = 2;
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

using RowDecorator = std::function<void(size_t, int, int)>;

static void draw_section(int y, int h, const std::string &title,
                         const std::vector<Video> &items,
                         bool active, size_t offset,
                         const RowDecorator &decorate = RowDecorator()) {
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

        if (decorate) decorate(idx, y + 1 + (int)i, w);

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

// Draw subscriptions list or the cached channel videos for a subscription
static void draw_subscriptions_ui(int h, int w) {
    if (subs.empty()) load_subs();

    std::vector<Video> dummy;
    draw_section(2, h - 4, "SUBSCRIPTIONS", dummy, true, 0);

    if (subs.empty()) {
        mvprintw(4, 4, "No subscriptions added yet.");
        return;
    }

    size_t display_count = visible_count((size_t)h - 4, subs.size());
    for (size_t i = 0; i < display_count; ++i) {
        bool selch = (sel == i);
        if (selch) attron(A_REVERSE | A_BOLD);

        std::string name = subs[i].name.empty() ? subs[i].url : subs[i].name;
        if ((int)name.size() > w - 10)
            name = name.substr(0, w - 13) + "...";

        const bool is_active_channel = (subs_channel_idx == (int)i);
        const char marker = is_active_channel ? '>' : ' ';

        mvprintw(3 + i, 2, "%c %2zu. %s", marker, i + 1, name.c_str());

        if (selch) attroff(A_REVERSE | A_BOLD);
    }
}

static void open_subscription_channel(size_t index) {
    if (index >= subs.size()) return;

    subs_channel_idx = static_cast<int>(index);

    if (subs_cache.size() <= index) subs_cache.resize(index + 1);
    auto &cache = subs_cache[index];
    if (cache.empty()) {
        cache = fetch_videos(subs[index].url, MAX_LIST_ITEMS);
    }

    const std::string &url = subs[index].url;
    std::string name = subs[index].name.empty() ? url : subs[index].name;
    const std::vector<Video> *prefetched = cache.empty() ? nullptr : &cache;
    enter_channel_view(name, url, prefetched);
}

void draw() {
    clear();
    int h, w;
    getmaxyx(stdscr, h, w);

    auto draw_videos = [&](const std::string &title, const std::vector<Video> &videos,
                           const RowDecorator &decorator = RowDecorator()) {
        draw_section(2, h - 4, title, videos, true, 0, decorator);
    };

    switch (focus) {
        case HOME: {
            std::string logo = "YTUI";
            mvprintw(3, (w - (int)logo.length()) / 2, "%s", logo.c_str());
            break;
        }
        case SEARCH:
            draw_search_ui();
            break;
        case DOWNLOADS: {
            auto items = get_download_items();
            RowDecorator decorator = [&](size_t idx, int row, int width) {
                const auto &v = items[idx];
                struct stat st{};
                if (stat(v.path.c_str(), &st) == 0 && st.st_size > 0) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%5.1fMB", (double)st.st_size / (1024 * 1024));
                    mvprintw(row, width - 9, "%s", buf);
                }
            };
            draw_videos("DOWNLOADS", items, decorator);
            break;
        }
        case SUBSCRIPTIONS:
            draw_subscriptions_ui(h, w);
            break;
        case RESULTS:
            draw_videos("RESULTS", res);
            break;
        case CHANNEL: {
            draw_videos("CHANNEL: " + channel_name, channel_videos);
            bool is_sub = is_subscribed(channel_url, channel_name);
            std::string badge = is_sub ? "[SUB]" : "[+SUB]";
            mvprintw(2, getmaxx(stdscr) - (int)badge.length() - 2, "%s", badge.c_str());
            break;
        }
        case PROFILE:
            draw_videos("PROFILE - History", history);
            break;
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

    auto reset_search_state = [&]() {
        insert_mode = false;
        search_hist_idx = -1;
        query_pos = query.size();
        curs_set(0);
    };

    auto set_focus = [&](Focus target) {
        focus = target;
        sel = 0;
        reset_search_state();
    };

    bool search_insert = (focus == SEARCH && insert_mode);
    if (!search_insert) {
        if (ch == 'j' || ch == 'J') ch = APP_KEY_DOWN;
        else if (ch == 'k' || ch == 'K') ch = APP_KEY_UP;
        else if (ch == 'h' || ch == 'H') ch = KEY_LEFT;
        else if (ch == 'l' || ch == 'L') ch = KEY_RIGHT;
    }

    if (focus == HOME && ch == KEY_RIGHT) { set_focus(SEARCH); return true; }

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
        if(ch == 'r') {
            if(focus == SUBSCRIPTIONS && sel < subs.size()) {
                size_t idx = sel;
                if(subs_cache.size() <= idx) subs_cache.resize(idx + 1);
                subs_cache[idx] = fetch_videos(subs[idx].url, MAX_LIST_ITEMS);
                std::string name = subs[idx].name.empty() ? subs[idx].url : subs[idx].name;
                set_status("Prefetched channel: " + name);
                return true;
            }
            if(focus == CHANNEL && !channel_url.empty()) {
                channel_videos = fetch_videos(channel_url, MAX_LIST_ITEMS);
                sel = 0;
                if(subs_channel_idx >= 0) {
                    if(subs_cache.size() <= (size_t)subs_channel_idx) subs_cache.resize(subs_channel_idx + 1);
                    subs_cache[subs_channel_idx] = channel_videos;
                }
                if(channel_videos.empty()) set_status("No videos found for channel");
                else set_status("Refreshed channel: " + channel_name);
                return true;
            }
        }
    }

    if(!search_insert) {
        if(ch == APP_KEY_SUBS) { set_focus(SUBSCRIPTIONS); return true; }
        if(ch == APP_KEY_DOWNLOADS) { set_focus(DOWNLOADS); return true; }
        if(ch == APP_KEY_PROFILE) { set_focus(PROFILE); return true; }
        if(ch == APP_KEY_HOME) { set_focus(HOME); return true; }
        if(ch == APP_KEY_SEARCH) { set_focus(SEARCH); return true; }
    }

    // Handle input based on focus
    if(focus == SEARCH) {
        if(ch == '\t') { insert_mode = !insert_mode; query_pos = query.size(); curs_set(insert_mode ? 1 : 0); return true; }

        if(!insert_mode) {
            if(ch == KEY_LEFT) { set_focus(HOME); return true; }
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
            if(ch == '\n' || ch == '\r' || ch == KEY_RIGHT) {
                if(search_hist_idx >= 0 && search_hist_idx < (int)search_hist.size()) {
                    query = search_hist[search_hist_idx];
                }
                if(!query.empty()) {
                    res = fetch_videos(query, MAX_LIST_ITEMS);
                    add_search_hist(query);
                    set_focus(RESULTS);
                }
                return true;
            }
        } else {
            // insert mode: Enter to run and exit, cursored editing
            if(ch == '\n' || ch == '\r') {
                if(!query.empty()) {
                    res = fetch_videos(query, MAX_LIST_ITEMS);
                    add_search_hist(query);
                    set_focus(RESULTS);
                } else {
                    reset_search_state();
                }
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
        if((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < res.size() - 1) sel++;
        else if((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
        else if(ch == KEY_LEFT) { set_focus(SEARCH); return true; }
        else if((ch == '\n' || ch == '\r' || ch == KEY_RIGHT) && sel < res.size()) play(res[sel]);
        else if(ch == APP_KEY_DOWNLOAD && sel < res.size()) {
            const Video &v = res[sel];
            enqueue_download(v);
        }

    } else if(focus == PROFILE) {
        if((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < history.size() - 1) sel++;
        else if((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
        else if(ch == KEY_LEFT) { set_focus(HOME); return true; }
        else if((ch == '\n' || ch == '\r' || ch == KEY_RIGHT) && sel < history.size()) play(history[sel]);
        else if(ch == APP_KEY_DOWNLOAD && sel < history.size()) {
            const Video &v = history[sel];
            enqueue_download(v);
        }
    }
    else if (focus == CHANNEL) {
        if(ch == KEY_LEFT || ch == 27) { // ESC/left back to subscriptions
            reset_search_state();
            focus = SUBSCRIPTIONS;
            if(subs_channel_idx >= 0 && subs.size() > (size_t)subs_channel_idx)
                sel = (size_t)subs_channel_idx;
            else sel = 0;
            return true;
        }

        if (channel_videos.empty()) return true;

        if((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < channel_videos.size() - 1) sel++;
        else if((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
        else if((ch == '\n' || ch == '\r' || ch == KEY_RIGHT) && sel < channel_videos.size()) play(channel_videos[sel]);
        else if(ch == APP_KEY_DOWNLOAD && sel < channel_videos.size()) enqueue_download(channel_videos[sel]);
        else if(ch == APP_KEY_SUB_TOGGLE) {
            bool now_sub = toggle_subscription(channel_name, channel_url);
            subs_cache.clear();
            if(now_sub) {
                auto it = std::find_if(subs.begin(), subs.end(), [&](const Channel &c){ return c.url == channel_url || c.name == channel_name; });
                if(it != subs.end()) subs_channel_idx = static_cast<int>(std::distance(subs.begin(), it));
                set_status(std::string("Subscribed: ") + channel_name);
            } else {
                subs_channel_idx = -1;
                set_status(std::string("Unsubscribed: ") + channel_name);
            }
            return true;
        }
    }
    else if (focus == DOWNLOADS) {
        auto items = get_download_items();
        if ((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < items.size() - 1) sel++;
        else if ((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
        else if (ch == KEY_LEFT) { set_focus(HOME); return true; }
        else if ((ch == '\n' || ch == '\r') && sel < items.size()) {
            const auto &v = items[sel];
            if (file_exists(v.path)) play(v);
            else play(v); // fallback to remote stream
        } else if ((ch == KEY_RIGHT) && sel < items.size()) {
            const auto &v = items[sel];
            if (file_exists(v.path)) play(v);
            else play(v);
        } else if (ch == APP_KEY_DOWNLOAD && sel < items.size()) {
            enqueue_download(items[sel]);
        }
    }
    else if(focus == SUBSCRIPTIONS) {
        if(subs.empty()) return true;

        if((ch == APP_KEY_DOWN || ch == KEY_DOWN) && sel < subs.size() - 1) sel++;
        else if((ch == APP_KEY_UP || ch == KEY_UP) && sel > 0) sel--;
        else if(ch == KEY_LEFT) { set_focus(HOME); return true; }
        else if((ch == '\n' || ch == '\r' || ch == KEY_RIGHT) && sel < subs.size()) {
            open_subscription_channel(sel);
            return true;
        }
    }

    return true;
}
