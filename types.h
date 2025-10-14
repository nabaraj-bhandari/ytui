#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>
#include <map>

struct Video { 
    std::string id, title, path, channel_url, channel_name; 
    bool operator==(const Video &v) const { return id == v.id; }
};

struct Channel {
    std::string name, url;
};

enum Focus { HOME, DOWNLOADS, PROFILE, SUBSCRIPTIONS, CHANNEL, SEARCH, RESULTS };

// Global state
extern std::vector<std::string> search_hist;
extern std::vector<Video> res;
extern std::vector<Video> history;
struct Download { Video v; int pid; bool done; };
extern std::vector<Download> downloads;
extern std::vector<Channel> subs;
extern std::vector<Video> subs_videos;
extern std::vector<Video> channel_videos;
extern std::string channel_name;
extern std::string channel_url;
extern std::map<std::string, std::string> channel_cache;
extern int subs_channel_idx; // -1 = showing channel list; >=0 = showing videos for that channel
extern std::vector<std::vector<Video>> subs_cache;
extern std::string query;
extern size_t sel;
extern size_t query_pos;
extern std::string status_msg;
extern time_t status_time;
extern Focus focus;
extern bool insert_mode;
// Index into search history when on SEARCH page (interactive selection)
extern int search_hist_idx;

#endif
