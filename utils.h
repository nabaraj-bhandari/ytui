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
size_t visible_count(size_t available_rows, size_t total_items);

// Subscription helpers
bool is_subscribed(const std::string &url, const std::string &name);

#endif
