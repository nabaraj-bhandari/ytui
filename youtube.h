#ifndef YOUTUBE_H
#define YOUTUBE_H

#include "config.h"
#include <string>
#include "types.h"
#include <vector>

std::vector<Video> fetch_videos(const std::string &source, int count = MAX_LIST_ITEMS);
int download(const Video &v);
int spawn_background(const std::string &cmd);
// Actions
void show_channel();
void show_channel_for(const Video &v);
void enter_channel_view(const std::string &url, const std::vector<Video> *prefetched = nullptr);
std::string resolve_channel_url_for_video(const std::string &video_id);

#endif
