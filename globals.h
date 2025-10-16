#ifndef GLOBALS_H
#define GLOBALS_H

#include <cstddef>
#include <ctime>
#include <string>
#include <sys/types.h>
#include <vector>

#include "types.h"

extern std::vector<std::string> search_hist;
extern std::vector<Video> res;
extern std::vector<Video> history;
extern std::vector<Download> downloads;
extern std::vector<Channel> subs;
extern std::vector<Video> channel_videos;
extern std::string channel_url;
extern int subs_channel_idx;
extern std::vector<std::vector<Video>> subs_cache;
extern std::string query;
extern size_t sel;
extern size_t query_pos;
extern std::string status_msg;
extern time_t status_time;
extern Focus focus;
extern bool insert_mode;
extern int search_hist_idx;
extern bool channel_return_active;
extern Focus channel_return_focus;
extern size_t channel_return_sel;
extern size_t history_scroll;
extern size_t results_scroll;
extern size_t downloads_scroll;
extern size_t channel_scroll;
extern size_t subs_scroll;
extern pid_t thumbnail_pid;

#endif
