#include "ui.h"
#include "config.h"
#include "types.h"
#include "youtube.h"
#include "utils.h"

#include <locale.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <algorithm>
#include <functional>
#include <iterator>

namespace {
std::vector<Video> collect_download_items() {
    std::vector<Video> out;
    auto cached = scan_video_cache();
    out.insert(out.end(), cached.begin(), cached.end());

    for (const auto &d : downloads) {
        if (std::any_of(cached.begin(), cached.end(),
                        [&](const Video &c) { return c.id == d.v.id; }))
            continue;

        Video candidate = d.v;
        candidate.path = VIDEO_CACHE + "/" + candidate.id + ".mkv";
        out.push_back(candidate);
    }
    return out;
}

void render_status_bar() {
    int h, w;
    getmaxyx(stdscr, h, w);
    int y = h - 1;

    move(y, 0);
    clrtoeol();

    if (time(nullptr) - status_time < 3 && !status_msg.empty()) {
        attron(A_BOLD);
        int start = w - (int)status_msg.length() - 2;
        if (start < 2) start = 2;
        mvprintw(y, start, "%s", status_msg.c_str());
        attroff(A_BOLD);
    }

    int info_x = w/2;
    std::string info;
    if (focus == DOWNLOADS) {
        int total = 0, active = 0;
        auto cached = scan_video_cache();
        total = cached.size();
        for (const auto &d : downloads)
            if (d.pid > 0) active++;
        info = std::to_string(total) + " files";
        if (active > 0) info += " | " + std::to_string(active) + " active";
    }
    if (!info.empty()) {
        attron(A_DIM);
        mvprintw(y, info_x, "%s", info.c_str());
        attroff(A_DIM);
    }
}

void update_download_statuses() {
    auto cached = scan_video_cache();
    for (auto &d : downloads) {
        bool found = std::any_of(cached.begin(), cached.end(),
                                 [&](const Video &c) { return c.id == d.v.id; });
        d.done = found;
        if (found) d.pid = 0;
    }
}

void remember_channel_origin(Focus target, size_t selection) {
    channel_return_focus = target;
    channel_return_sel = selection;
    channel_return_active = true;
}

void render_video_list_section(int y, int h, const std::string &title,
                               const std::vector<Video> &items,
                               bool active, size_t offset) {
    int w = getmaxx(stdscr);
    std::string logo = "-=YTUI=-";
    if (active) attron(COLOR_PAIR(1) | A_BOLD);
    else attron(A_DIM);
    mvprintw(0, (w - (int)logo.length()) / 2, "%s", logo.c_str());
    mvprintw(y, 0, "--- %s ", title.c_str());
    for (int i = title.length() + 5; i < w; i++) addch('-');
    attroff(COLOR_PAIR(1) | A_BOLD | A_DIM);

    size_t max_display = visible_count((size_t)h, items.size());
    for (size_t i = 0; i < max_display; i++) {
        size_t idx = i + offset;
        if (idx >= items.size()) break;

        bool selected = active && sel == idx;
        if (selected) attron(A_REVERSE | A_BOLD);

        std::string prefix = is_video_downloaded(items[idx]) ? "* " : "o ";
        std::string num = std::to_string(idx + 1) + ". ";
        mvprintw(y + 1 + i, 2, "%s %s", prefix.c_str(),num.c_str());

        int max_w = w - prefix.length() - num.length() - 3;
        std::string disp_title = items[idx].title;
        if ((int)disp_title.length() > max_w)
            disp_title = disp_title.substr(0, max_w - 3) + "...";
        printw("%s", disp_title.c_str());


        if (selected) attroff(A_REVERSE | A_BOLD);
    }
}

void render_search_view() {
    int h = getmaxy(stdscr);
    int w = getmaxx(stdscr);
    int margin = std::min(4, std::max(0, w / 10));
    int left = margin;
    int right = w - margin - 1;
    if (right - left < 12) {
        left = 1;
        right = w - 2;
    }
    if (right >= w) right = w - 1;
    if (right <= left) right = left + 1;

    int top = std::max(0, std::min(2, h - 4));
    int bottom = top + 2;
    if (bottom >= h - 1) bottom = std::max(top + 1, h - 2);

    std::string header = insert_mode ? "Search (editing)" : "Search";
    int header_width = (int)header.size();
    int header_x = std::max(1, left + std::max(0, (right - left - header_width) / 2));
    if (header_x + header_width >= w) header_x = std::max(1, w - header_width - 1);
    int header_y = std::max(0, top - 1);
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(header_y, header_x, "%s", header.c_str());
    attroff(A_BOLD | COLOR_PAIR(1));

    std::string help = "Tab edit  Enter search  Up/Down history";
    if ((int)help.size() > w - left - 1) help.resize(std::max(0, w - left - 1));
    int help_y = std::min(bottom + 1, h - 2);
    if (help_y < 0) help_y = 0;
    attron(A_DIM);
    mvprintw(help_y, left, "%s", help.c_str());
    attroff(A_DIM);

    int inner_width = std::max(0, right - left - 1);
    int inner_height = std::max(0, bottom - top - 1);
    mvaddch(top, left, ACS_ULCORNER);
    mvhline(top, left + 1, ACS_HLINE, inner_width);
    mvaddch(top, right, ACS_URCORNER);
    mvvline(top + 1, left, ACS_VLINE, inner_height);
    mvvline(top + 1, right, ACS_VLINE, inner_height);
    mvaddch(bottom, left, ACS_LLCORNER);
    mvhline(bottom, left + 1, ACS_HLINE, inner_width);
    mvaddch(bottom, right, ACS_LRCORNER);
    mvhline(top + 1, left + 1, ' ', inner_width);

    int text_start = left + 2;
    if (text_start >= right) text_start = right - 1;
    if (text_start <= left) text_start = left + 1;
    int text_width = std::max(0, right - text_start);
    size_t caret = std::min(query_pos, query.size());
    size_t window_width = text_width > 0 ? (size_t)text_width : (size_t)0;
    size_t window_start = 0;
    if (window_width > 0 && query.size() > window_width) {
        if (caret >= window_width) window_start = caret - window_width + 1;
        if (window_start + window_width > query.size()) window_start = query.size() - window_width;
    }
    bool clipped_left = window_start > 0;
    bool clipped_right = window_width > 0 && window_start + window_width < query.size();
    std::string display;
    if (window_width > 0) {
        display = query.substr(window_start, std::min(window_width, query.size() - window_start));
        if (display.size() < window_width) display.append(window_width - display.size(), ' ');
        if (clipped_left && !display.empty()) display.front() = '<';
        if (clipped_right && !display.empty()) display.back() = '>';
    }

    if (!query.empty() && !display.empty()) {
        if (insert_mode) attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(top + 1, text_start, "%s", display.c_str());
        if (insert_mode) attroff(COLOR_PAIR(1) | A_BOLD);
    } else if (!insert_mode && window_width > 0) {
        std::string placeholder = "Type to search";
        if ((int)placeholder.size() > text_width) placeholder.resize(std::max(0, text_width));
        attron(A_DIM);
        mvprintw(top + 1, text_start, "%s", placeholder.c_str());
        attroff(A_DIM);
    }

    int cx = text_start + (int)(caret - window_start);
    cx = std::max(text_start, std::min(cx, right - 2));
    if (insert_mode) {
        if (query.empty()) mvaddch(top + 1, cx, ' ' | A_UNDERLINE | A_BOLD | COLOR_PAIR(1));
        move(top + 1, cx);
        curs_set(1);
    } else {
        curs_set(0);
    }

    int history_title_y = std::min(help_y + 2, h - 2);
    if (history_title_y < h) {
        attron(A_BOLD);
        mvprintw(history_title_y, left, "RECENT SEARCHES");
        attroff(A_BOLD);
    }
    int list_y = history_title_y + 1;
    for (size_t i = 0; i < search_hist.size() && i < 10; i++) {
        int row = list_y + (int)i;
        if (row >= h - 1) break;
        bool selh = (search_hist_idx == (int)i) && !insert_mode;
        if (selh) attron(A_REVERSE | A_BOLD);
        mvprintw(row, left + 2, "%2zu. %s", i + 1, search_hist[i].c_str());
        if (selh) attroff(A_REVERSE | A_BOLD);
    }
}

void render_subscriptions_view(int h, int w) {
    if (subs.empty()) load_subs();

    std::vector<Video> dummy;
    render_video_list_section(1, h - 4, "SUBSCRIPTIONS", dummy, true, 0);

    if (subs.empty()) {
        mvprintw(3, 4, "No subscriptions added yet.");
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

        mvprintw(2 + i, 2, "%c %2zu. %s", marker, i + 1, name.c_str());

        if (selch) attroff(A_REVERSE | A_BOLD);
    }
}

void enter_subscription_channel(size_t index) {
    if (index >= subs.size()) return;

    subs_channel_idx = static_cast<int>(index);

    remember_channel_origin(SUBSCRIPTIONS, index);

    if (subs_cache.size() <= index) subs_cache.resize(index + 1);
    auto &cache = subs_cache[index];
    if (cache.empty()) {
        cache = fetch_videos(subs[index].url, MAX_LIST_ITEMS);
    }

    const std::string &url = subs[index].url;
    const std::vector<Video> *prefetched = cache.empty() ? nullptr : &cache;
    enter_channel_view(url, prefetched);
}

} // namespace

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
    curs_set(1);
}

void cleanup_ui() {
    endwin();
}

void draw() {
    clear();
    int h, w;
    getmaxyx(stdscr, h, w);

    auto render_videos = [&](const std::string &title, const std::vector<Video> &videos) {
        render_video_list_section(1, h - 2, title, videos, true, 0);
    };

    switch (focus) {
        case HOME: {
            render_videos("History", history);
            break;
        }
        case SEARCH:
            render_search_view();
            break;
        case DOWNLOADS: {
            auto items = collect_download_items();
            render_videos("DOWNLOADS", items);
            break;
        }
        case SUBSCRIPTIONS:
            render_subscriptions_view(h, w);
            break;
        case RESULTS:
            render_videos("RESULTS", res);
            break;
        case CHANNEL: {
            render_videos("CHANNEL", channel_videos);
            break;
        }
    }

    update_download_statuses();
    render_status_bar();

    refresh();
}

bool handle_input() {
    int ch = getch();
    if (ch == ERR) return true;

    if (ch == APP_KEY_QUIT) return false;

    auto reset_search_state = [&]() {
        insert_mode = false;
        search_hist_idx = -1;
        query_pos = query.size();
        curs_set(0);
    };

    auto set_focus = [&](Focus target) {
        focus = target;
        sel = 0;
        channel_return_active = false;
        if (target != CHANNEL) subs_channel_idx = -1;
        reset_search_state();
    };

    bool search_insert = (focus == SEARCH && insert_mode);
    if (!search_insert) {
        if (ch == 'j' || ch == 'J') ch = APP_KEY_DOWN;
        else if (ch == 'k' || ch == 'K') ch = APP_KEY_UP;
        else if (ch == 'h' || ch == 'H') ch = KEY_LEFT;
        else if (ch == 'l' || ch == 'L') ch = KEY_RIGHT;
    }

    const bool navDown = (ch == APP_KEY_DOWN || ch == KEY_DOWN);
    const bool navUp = (ch == APP_KEY_UP || ch == KEY_UP);
    const bool navBack = (ch == KEY_LEFT);
    const bool navSelect = (ch == '\n' || ch == '\r' || ch == KEY_RIGHT);

    auto move_selection = [&](size_t size) {
        if (size == 0) { sel = 0; return false; }
        if (sel >= size) sel = size - 1;
        if (navDown && sel + 1 < size) { ++sel; return true; }
        if (navUp && sel > 0) { --sel; return true; }
        return false;
    };

    auto handle_list_navigation = [&](size_t size,
                                      const std::function<void()> &onBack,
                                      const std::function<void()> &onSelect) {
        if (move_selection(size)) return true;
        if (navBack && onBack) { onBack(); return true; }
        if (navSelect && onSelect && size > 0 && sel < size) { onSelect(); return true; }
        return false;
    };

    auto handle_video_list = [&](const std::vector<Video> &list,
                                 const std::function<void()> &onBack,
                                 const std::function<void()> &onSelect,
                                 const std::function<void()> &onDownload = std::function<void()>()) {
        if (handle_list_navigation(list.size(), onBack, onSelect)) return true;
        if (ch == APP_KEY_DOWNLOAD && sel < list.size()) {
            if (onDownload) onDownload();
            else enqueue_download(list[sel]);
            return true;
        }
        return false;
    };

    if (focus == HOME && ch == KEY_RIGHT) { set_focus(SEARCH); return true; }

    if (focus != SEARCH) {
        if (ch == APP_KEY_CHANNEL) {
            if (focus == RESULTS && sel < res.size()) {
                remember_channel_origin(RESULTS, sel);
                show_channel_for(res[sel]);
            } else if (focus == DOWNLOADS) {
                auto items = collect_download_items();
                if (sel < items.size()) {
                    remember_channel_origin(DOWNLOADS, sel);
                    show_channel_for(items[sel]);
                }
            }
            return true;
        }

        if (ch == 'r') {
            if (focus == SUBSCRIPTIONS && sel < subs.size()) {
                size_t idx = sel;
                if (subs_cache.size() <= idx) subs_cache.resize(idx + 1);
                subs_cache[idx] = fetch_videos(subs[idx].url, MAX_LIST_ITEMS);
                std::string name = subs[idx].name.empty() ? subs[idx].url : subs[idx].name;
                set_status("Prefetched channel: " + name);
                return true;
            }
            if (focus == CHANNEL && !channel_url.empty()) {
                channel_videos = fetch_videos(channel_url, MAX_LIST_ITEMS);
                sel = 0;
                if (subs_channel_idx >= 0) {
                    if (subs_cache.size() <= (size_t)subs_channel_idx) subs_cache.resize(subs_channel_idx + 1);
                    subs_cache[subs_channel_idx] = channel_videos;
                }
                if (channel_videos.empty()) set_status("No videos found for channel");
                else set_status("Refreshed channel: " + channel_name);
                return true;
            }
        }
    }

    if (!search_insert) {
        if (ch == APP_KEY_SUBS) { set_focus(SUBSCRIPTIONS); return true; }
        if (ch == APP_KEY_DOWNLOADS) { set_focus(DOWNLOADS); return true; }
        if (ch == APP_KEY_HOME) { set_focus(HOME); return true; }
        if (ch == APP_KEY_SEARCH) { set_focus(SEARCH); return true; }
    }

    if (focus == SEARCH) {
        if (ch == '\t') { insert_mode = !insert_mode; query_pos = query.size(); curs_set(insert_mode ? 1 : 0); return true; }

        if (!insert_mode) {
            if (navDown && !search_hist.empty()) { search_hist_idx = std::min((int)search_hist.size() - 1, search_hist_idx + 1); if (search_hist_idx < 0) search_hist_idx = 0; return true; }
            if (navUp && !search_hist.empty()) { if (search_hist_idx > 0) --search_hist_idx; else search_hist_idx = (int)search_hist.size() - 1; return true; }
            if (navSelect) {
                if (search_hist_idx >= 0 && search_hist_idx < (int)search_hist.size()) query = search_hist[search_hist_idx];
                if (!query.empty()) {
                    res = fetch_videos(query, MAX_LIST_ITEMS);
                    add_search_hist(query);
                    set_focus(RESULTS);
                }
                return true;
            }
        } else {
            if (navSelect) {
                if (!query.empty()) {
                    res = fetch_videos(query, MAX_LIST_ITEMS);
                    add_search_hist(query);
                    set_focus(RESULTS);
                } else {
                    insert_mode = false;
                    query_pos = query.size();
                    curs_set(0);
                }
                return true;
            }
            if (ch == KEY_LEFT) { if (query_pos > 0) --query_pos; return true; }
            if (ch == KEY_RIGHT) { if (query_pos < query.size()) ++query_pos; return true; }
            if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) { if (query_pos > 0) { query.erase(query_pos - 1, 1); --query_pos; } return true; }
            if (ch == KEY_DC) { if (query_pos < query.size()) query.erase(query_pos, 1); return true; }
            if (ch >= 32 && ch <= 126) { query.insert(query_pos, 1, (char)ch); ++query_pos; return true; }
        }
    }

    if (focus != SEARCH) {
        auto restore_channel_origin = [&]() {
            reset_search_state();
            Focus target = channel_return_active ? channel_return_focus : SUBSCRIPTIONS;
            size_t selection = channel_return_active ? channel_return_sel : (subs_channel_idx >= 0 ? (size_t)subs_channel_idx : 0);
            channel_return_active = false;
            auto clamp = [&](size_t sz) { return sz == 0 ? (size_t)0 : std::min(selection, sz - 1); };
            subs_channel_idx = -1;
            switch (target) {
                case SUBSCRIPTIONS: focus = SUBSCRIPTIONS; sel = subs.empty() ? 0 : clamp(subs.size()); break;
                case RESULTS: focus = RESULTS; sel = res.empty() ? 0 : clamp(res.size()); break;
                case DOWNLOADS: {
                    focus = DOWNLOADS;
                    auto items = collect_download_items();
                    sel = items.empty() ? 0 : clamp(items.size());
                    break;
                }
                case SEARCH: focus = SEARCH; sel = 0; break;
                case HOME: default: focus = (target==HOME?HOME:target); sel = (target==HOME?0:selection); break;
            }
        };

        std::vector<Video> temp_items;
        const std::vector<Video> *list = nullptr;
        std::function<void()> onBack, onSelect, onDownload;

        if (focus == RESULTS) {
            list = &res;
            onBack = [&]{ set_focus(SEARCH); };
            onSelect = [&]{ play(res[sel]); };
        } else if (focus == CHANNEL) {
            if (navBack || ch == 27) { restore_channel_origin(); return true; }
            if (channel_videos.empty()) return true;
            list = &channel_videos;
            onSelect = [&]{ play(channel_videos[sel]); };
            onDownload = [&]{ enqueue_download(channel_videos[sel]); };
        } else if (focus == DOWNLOADS) {
            temp_items = collect_download_items();
            list = &temp_items;
            onBack = [&]{ set_focus(HOME); };
            onSelect = [&]{ play((*list)[sel]); };
            onDownload = [&]{ enqueue_download((*list)[sel]); };
        } else if (focus == SUBSCRIPTIONS) {
            if (subs.empty()) return true;
            if (handle_list_navigation(subs.size(), [&]{ set_focus(HOME); }, [&]{ enter_subscription_channel(sel); })) return true;
            list = nullptr;
        }

        if (list && handle_video_list(*list, onBack, onSelect, onDownload)) return true;
        return true;
    }

    return true;
}
