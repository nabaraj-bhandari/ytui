#include "types.h"
#include <thread>
#include <map>

// Global state definitions
std::vector<std::string> search_hist;
std::vector<Video> res;
std::vector<Video> history;
std::vector<Download> downloads;
std::vector<Channel> subs;
std::vector<Video> subs_videos;
std::string query;
size_t sel = 0;
std::string status_msg;
time_t status_time = 0;
Focus focus = HOME;
bool insert_mode = false;
size_t query_pos = 0;
int subs_channel_idx = -1;
std::vector<std::vector<Video>> subs_cache;
int search_hist_idx = -1;
std::vector<Video> channel_videos;
std::string channel_name;
std::string channel_url;
// channel cache: key -> channel_url
std::map<std::string, std::string> channel_cache;

