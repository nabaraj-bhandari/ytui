#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include "types.h"

void mkdirs();
bool file_exists(const std::string &path);
void set_status(const std::string &msg);

// Play
#include "types.h"
void play(const Video &v);

// Search history (string list)
void load_search_hist();
void save_search_hist();
void add_search_hist(const std::string &s);

// Video history
void load_history();
void save_history();

// Subscriptions
void load_subs();
void save_subs();

// Scan the VIDEO_CACHE directory and return discovered videos
std::vector<Video> scan_video_cache();

// Return true if the given video appears in the VIDEO_CACHE (match by id or title)
bool is_video_downloaded(const Video &v);

// Return path to cached file for given video id, or empty string if not present
std::string find_cached_path_by_id(const std::string &id);

// Utility encoding for safe persistence
std::string esc(const std::string &s);
std::string unesc(const std::string &s);

// Downloads
int enqueue_download(const Video &v);
void ensure_video_cache();
// Channel cache
void load_channel_cache();
void save_channel_cache();
std::string channel_cache_lookup(const std::string &key);
void channel_cache_store(const std::string &key, const std::string &url);

// Video metadata persistence (id -> channel_url | channel_name)
void load_video_meta();
void save_video_meta();
std::string video_meta_lookup_channel_url(const std::string &id);
std::string video_meta_lookup_channel_name(const std::string &id);
void video_meta_store(const std::string &id, const std::string &channel_url, const std::string &channel_name);

// UI helper: compute how many items may be visible given available rows and total items
size_t visible_count(size_t available_rows, size_t total_items);

// Subscription helpers
bool is_subscribed(const std::string &url, const std::string &name);
void subscribe_channel(const std::string &name, const std::string &url);
void unsubscribe_channel(const std::string &url, const std::string &name);
bool toggle_subscription(const std::string &name, const std::string &url);

#endif
