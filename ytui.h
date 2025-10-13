#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <ncurses.h>

// === Data Structures ===
struct Video {
    std::string id;
    std::string title;
    std::string channel;
};

struct Subscription {
    std::string name;
    std::string url;
};

enum class UiMode { SEARCH, QUEUE, SUBSCRIPTIONS };

// === Global State (declared here, defined in ytui.cpp) ===
extern std::vector<Video> results, queue;
extern std::vector<std::string> history;
extern std::vector<Subscription> subscriptions;
extern int selection;

// === Function Declarations ===

// --- File Utilities (files.cpp) ---
namespace files {
    std::string get_home_path(const char* subpath);
    void setup_dirs();
    bool is_cached(const Video& v);
    std::vector<std::string> load_history();
    void save_history(const std::vector<std::string>& history);
    std::vector<Video> load_queue();
    void save_queue(const std::vector<Video>& queue);
    std::vector<Subscription> load_subscriptions();
}

// --- MPV Control (mpv.cpp) ---
namespace mpv {
    bool connect(const char* socket_path);
    void disconnect();
    void cmd(const std::string_view& cmd_str);
    void toggle_pause();
    void seek(int seconds);
    void playlist_next();
    void playlist_prev();
    void toggle_mute();
}

// --- YouTube Interaction (yt.cpp) ---
namespace yt {
    std::vector<Video> search(const std::string& query);
    std::vector<Video> fetch_channel_videos(const std::string& channel_url);
    void download(const Video& v);
    std::string fetch_description(const std::string& video_id);
    void get_video_context(Video& v);
}

// --- UI Drawing (ui.cpp) ---
namespace ui {
    void init();
    void cleanup();
    void draw(UiMode mode, const std::string& query, const std::string& status);
}
