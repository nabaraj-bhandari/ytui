#include "ui.h"

#include "config.h"
#include "globals.h"
#include "types.h"
#include "utils.h"
#include "youtube.h"

#include <algorithm>
#include <ctime>
#include <functional>
#include <iterator>
#include <locale.h>
#include <ncurses.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

size_t &scroll_for_focus(Focus f) {
    switch (f) {
        case HOME: return history_scroll;
        case DOWNLOADS: return downloads_scroll;
        case SUBSCRIPTIONS: return subs_scroll;
        case CHANNEL: return channel_scroll;
        case RESULTS: return results_scroll;
        default: {
            static size_t dummy = 0;
            return dummy;
        }
    }
}

bool focus_has_video_content(Focus f) {
    return f == HOME || f == DOWNLOADS || f == RESULTS || f == CHANNEL;
}

void render_status_bar(const std::vector<Video> *cached_downloads = nullptr) {
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
        if (cached_downloads) total = static_cast<int>(cached_downloads->size());
        else total = static_cast<int>(scan_video_cache().size());
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

void remember_channel_origin(Focus target, size_t selection) {
    channel_return_focus = target;
    channel_return_sel = selection;
    channel_return_active = true;
}

void render_video_list_section(int y, int h, const std::string &title,
                               const std::vector<Video> &items,
                               bool active, size_t &offset) {
    int w = getmaxx(stdscr);
    std::string logo = "-=YTUI=-";
    if (active) attron(COLOR_PAIR(1) | A_BOLD);
    else attron(A_DIM);
    mvprintw(0, (w - (int)logo.length()) / 2, "%s", logo.c_str());
    mvprintw(y, 0, "--- %s ", title.c_str());
    for (int i = title.length() + 5; i < w; i++) addch('-');
    attroff(COLOR_PAIR(1) | A_BOLD | A_DIM);

    size_t max_display = visible_count((size_t)h, items.size());

    int status_row = getmaxy(stdscr) - 1;
    int last_row_allowed = status_row - 1;
    if (last_row_allowed < y + 1) {
        max_display = 0;
    } else {
        size_t room = (size_t)(last_row_allowed - (y + 1) + 1);
        if (max_display > room) max_display = room;
    }

    if (active) {
        if (items.empty()) sel = 0;
        else if (sel >= items.size()) sel = items.size() - 1;
    }

    if (items.size() <= max_display) {
        offset = 0;
    } else if (max_display > 0) {
        if (sel < offset) offset = sel;
        size_t max_offset = items.size() - max_display;
        if (sel >= offset + max_display) offset = sel - max_display + 1;
        if (offset > max_offset) offset = max_offset;
    } else {
        offset = 0;
    }

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

    if (max_display > 0 && items.size() > max_display) {
        int top_row = y + 1;
        int bottom_row = y + 1 + (int)max_display - 1;
        if (offset > 0) mvprintw(top_row, w - 2, "^");
        if (offset + max_display < items.size()) mvprintw(bottom_row, w - 2, "v");
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

    bool highlight_input = (!insert_mode && search_hist_idx < 0);

    std::string header = insert_mode ? "Search (editing)" : "Search";
    int header_width = (int)header.size();
    int header_x = std::max(1, left + std::max(0, (right - left - header_width) / 2));
    if (header_x + header_width >= w) header_x = std::max(1, w - header_width - 1);
    int header_y = std::max(0, top - 1);
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(header_y, header_x, "%s", header.c_str());
    attroff(A_BOLD | COLOR_PAIR(1));

    std::string help = insert_mode
        ? "Enter search  Esc normal  Left/Right move"
        : "Enter edit  Esc normal  j/k navigate";
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
    if (highlight_input) {
        attron(A_REVERSE);
        mvhline(top + 1, left + 1, ' ', inner_width);
        attroff(A_REVERSE);
    } else {
        mvhline(top + 1, left + 1, ' ', inner_width);
    }

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
        else if (highlight_input) attron(A_REVERSE);
        mvprintw(top + 1, text_start, "%s", display.c_str());
        if (insert_mode) attroff(COLOR_PAIR(1) | A_BOLD);
        else if (highlight_input) attroff(A_REVERSE);
    } else if (!insert_mode && window_width > 0) {
        std::string placeholder = "Type to search";
        if ((int)placeholder.size() > text_width) placeholder.resize(std::max(0, text_width));
        if (highlight_input) attron(A_REVERSE);
        attron(A_DIM);
        mvprintw(top + 1, text_start, "%s", placeholder.c_str());
        attroff(A_DIM);
        if (highlight_input) attroff(A_REVERSE);
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

    std::string logo = "-=YTUI=-";
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, (w - (int)logo.length()) / 2, "%s", logo.c_str());
    std::string title = "SUBSCRIPTIONS";
    mvprintw(1, 0, "--- %s ", title.c_str());
    for (int i = (int)title.length() + 5; i < w; ++i) addch('-');
    attroff(COLOR_PAIR(1) | A_BOLD);

    if (subs.empty()) {
        mvprintw(3, 4, "No subscriptions added yet.");
        return;
    }

    size_t display_count = visible_count((size_t)h - 4, subs.size());
    if (!subs.empty() && sel >= subs.size()) sel = subs.size() - 1;
    if (subs.empty()) sel = 0;

    if (subs.size() <= display_count) {
        subs_scroll = 0;
    } else if (display_count > 0) {
        if (sel < subs_scroll) subs_scroll = sel;
        size_t max_offset = subs.size() - display_count;
        if (sel >= subs_scroll + display_count) subs_scroll = sel - display_count + 1;
        if (subs_scroll > max_offset) subs_scroll = max_offset;
    } else {
        subs_scroll = 0;
    }

    int row_start = 2;
    for (size_t i = 0; i < display_count; ++i) {
        size_t idx = subs_scroll + i;
        if (idx >= subs.size()) break;

        bool selch = (sel == idx);
        if (selch) attron(A_REVERSE | A_BOLD);

        std::string name = subs[idx].name.empty() ? subs[idx].url : subs[idx].name;
        if ((int)name.size() > w - 10)
            name = name.substr(0, w - 13) + "...";

        const bool is_active_channel = (subs_channel_idx == (int)idx);
        const char marker = is_active_channel ? '>' : ' ';

        mvprintw(row_start + (int)i, 2, "%c %2zu. %s", marker, idx + 1, name.c_str());

        if (selch) attroff(A_REVERSE | A_BOLD);
    }

    if (display_count > 0 && subs.size() > display_count) {
        if (subs_scroll > 0) mvprintw(row_start, w - 2, "^");
        if (subs_scroll + display_count < subs.size()) mvprintw(row_start + (int)display_count - 1, w - 2, "v");
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

    bool cache_initialized = false;
    std::vector<Video> cached_videos;
    auto ensure_cache = [&]() -> const std::vector<Video>& {
        if (!cache_initialized) {
            cached_videos = scan_video_cache();
            cache_initialized = true;
        }
        return cached_videos;
    };

    switch (focus) {
        case HOME: {
            render_video_list_section(1, h - 2, "HISTORY", history, true, history_scroll);
            break;
        }
        case SEARCH:
            render_search_view();
            break;
        case DOWNLOADS: {
            const auto &cache = ensure_cache();
            auto items = collect_download_items(cache);
            render_video_list_section(1, h - 2, "DOWNLOADS", items, true, downloads_scroll);
            break;
        }
        case SUBSCRIPTIONS:
            render_subscriptions_view(h, w);
            break;
        case RESULTS:
            render_video_list_section(1, h - 2, "RESULTS", res, true, results_scroll);
            break;
        case CHANNEL: {
            render_video_list_section(1, h - 2, "CHANNEL", channel_videos, true, channel_scroll);
            break;
        }
    }

    const std::vector<Video> *status_cache = nullptr;
    if (!downloads.empty()) {
        const auto &cache = ensure_cache();
        update_download_statuses(cache);
        if (focus == DOWNLOADS) status_cache = &cache;
    } else if (focus == DOWNLOADS) {
        const auto &cache = ensure_cache();
        status_cache = &cache;
    }

    render_status_bar(status_cache);

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
        scroll_for_focus(target) = 0;
        reset_search_state();
        if (!focus_has_video_content(target)) hide_thumbnail();
    };

    bool cached_files_initialized = false;
    std::vector<Video> cached_files;
    auto ensure_cached_files = [&]() -> const std::vector<Video>& {
        if (!cached_files_initialized) {
            cached_files = scan_video_cache();
            cached_files_initialized = true;
        }
        return cached_files;
    };

    bool download_items_initialized = false;
    std::vector<Video> download_items_cache;
    auto ensure_download_items = [&]() -> const std::vector<Video>& {
        if (!download_items_initialized) {
            download_items_cache = collect_download_items(ensure_cached_files());
            download_items_initialized = true;
        }
        return download_items_cache;
    };

    auto refresh_thumbnail = [&]() {
        if (focus == HOME) {
            if (history.empty()) { hide_thumbnail(); return; }
            if (sel >= history.size()) sel = history.size() - 1;
            show_thumbnail(history[sel]);
            preload_thumbnails(history, sel + 1);
            return;
        }
        if (focus == DOWNLOADS) {
            const auto &items = ensure_download_items();
            if (items.empty()) { hide_thumbnail(); return; }
            if (sel >= items.size()) sel = items.size() - 1;
            show_thumbnail(items[sel]);
            preload_thumbnails(items, sel + 1);
            return;
        }
        if (focus == RESULTS) {
            if (res.empty()) { hide_thumbnail(); return; }
            if (sel >= res.size()) sel = res.size() - 1;
            show_thumbnail(res[sel]);
            preload_thumbnails(res, sel + 1);
            return;
        }
        if (focus == CHANNEL) {
            if (channel_videos.empty()) { hide_thumbnail(); return; }
            if (sel >= channel_videos.size()) sel = channel_videos.size() - 1;
            show_thumbnail(channel_videos[sel]);
            preload_thumbnails(channel_videos, sel + 1);
            return;
        }
        hide_thumbnail();
    };

    const bool search_insert = (focus == SEARCH && insert_mode);
    const bool allow_vim_nav = !search_insert;

    const bool navDown = (ch == KEY_DOWN) || (allow_vim_nav && (ch == 'j' || ch == 'J'));
    const bool navUp = (ch == KEY_UP) || (allow_vim_nav && (ch == 'k' || ch == 'K'));
    const bool navBack = allow_vim_nav && (ch == 'h' || ch == 'H');
    const bool navSelect = (ch == '\n' || ch == '\r') || (allow_vim_nav && (ch == 'l' || ch == 'L'));

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
        if (move_selection(list.size())) {
            if (!list.empty() && sel < list.size()) {
                show_thumbnail(list[sel]);
                preload_thumbnails(list, sel + 1);
            } else {
                hide_thumbnail();
            }
            return true;
        }
        if (navBack && onBack) { onBack(); return true; }
        if (navSelect && onSelect && !list.empty() && sel < list.size()) { onSelect(); return true; }
        if (ch == APP_KEY_DOWNLOAD && sel < list.size()) {
            if (onDownload) onDownload();
            else enqueue_download(list[sel]);
            return true;
        }
        return false;
    };

    if (focus != SEARCH) {
        if (ch == APP_KEY_CHANNEL) {
            if (focus == RESULTS && sel < res.size()) {
                remember_channel_origin(RESULTS, sel);
                show_channel_for(res[sel]);
            } else if (focus == DOWNLOADS) {
                const auto &items = ensure_download_items();
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
                else set_status("Refreshed channel");
                return true;
            }
        }
    }

    if (!search_insert) {
        if (ch == APP_KEY_SUBS) { set_focus(SUBSCRIPTIONS); return true; }
        if (ch == APP_KEY_DOWNLOADS) {
            set_focus(DOWNLOADS);
            refresh_thumbnail();
            return true;
        }
        if (ch == APP_KEY_HOME) {
            set_focus(HOME);
            refresh_thumbnail();
            return true;
        }
        if (ch == APP_KEY_SEARCH) { set_focus(SEARCH); return true; }
    }

    if (focus == SEARCH) {
        if (ch == '\t') {
            insert_mode = !insert_mode;
            query_pos = query.size();
            if (insert_mode) {
                search_hist_idx = -1;
                curs_set(1);
            } else {
                search_hist_idx = -1;
                curs_set(0);
            }
            return true;
        }

        if (ch == 27 && insert_mode) {
            insert_mode = false;
            search_hist_idx = -1;
            query_pos = query.size();
            curs_set(0);
            return true;
        }

        if (!insert_mode) {
            if (navDown) {
                if (search_hist_idx < 0 && !search_hist.empty()) search_hist_idx = 0;
                else if (search_hist_idx >= 0 && search_hist_idx + 1 < (int)search_hist.size()) ++search_hist_idx;
                return true;
            }
            if (navUp) {
                if (search_hist_idx > 0) --search_hist_idx;
                else if (search_hist_idx == 0) search_hist_idx = -1;
                else if (search_hist_idx < 0 && !search_hist.empty()) search_hist_idx = (int)search_hist.size() - 1;
                return true;
            }
            if (navSelect) {
                if (search_hist_idx >= 0 && search_hist_idx < (int)search_hist.size()) {
                    query = search_hist[search_hist_idx];
                    if (!query.empty()) {
                        res = fetch_videos(query, MAX_LIST_ITEMS);
                        add_search_hist(query);
                        set_focus(RESULTS);
                        refresh_thumbnail();
                    }
                } else {
                    insert_mode = true;
                    search_hist_idx = -1;
                    query_pos = query.size();
                    curs_set(1);
                }
                return true;
            }
        } else {
            if (navSelect) {
                if (!query.empty()) {
                    res = fetch_videos(query, MAX_LIST_ITEMS);
                    add_search_hist(query);
                    set_focus(RESULTS);
                    refresh_thumbnail();
                } else {
                    insert_mode = false;
                    search_hist_idx = -1;
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
                    const auto &items = ensure_download_items();
                    sel = items.empty() ? 0 : clamp(items.size());
                    break;
                }
                case SEARCH: focus = SEARCH; sel = 0; break;
                case HOME: default: focus = (target==HOME?HOME:target); sel = (target==HOME?0:selection); break;
            }
            scroll_for_focus(focus) = 0;
            if (focus_has_video_content(focus)) refresh_thumbnail();
            else hide_thumbnail();
        };

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
            const auto &items = ensure_download_items();
            list = &items;
            onBack = [&]{ set_focus(HOME); refresh_thumbnail(); };
            onSelect = [&]{ play(items[sel]); };
            onDownload = [&]{ enqueue_download(items[sel]); };
        } else if (focus == HOME) {
            if (history.empty()) return true;
            list = &history;
            onSelect = [&]{ play(history[sel]); };
            onDownload = [&]{ enqueue_download(history[sel]); };
        } else if (focus == SUBSCRIPTIONS) {
            if (subs.empty()) return true;
            if (handle_list_navigation(subs.size(), [&]{ set_focus(HOME); refresh_thumbnail(); }, [&]{ enter_subscription_channel(sel); })) return true;
            list = nullptr;
        }

        if (list && handle_video_list(*list, onBack, onSelect, onDownload)) return true;
        return true;
    }

    return true;
}
