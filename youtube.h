#ifndef YOUTUBE_H
#define YOUTUBE_H

#include "types.h"
#include <vector>

std::vector<Video> fetch_videos(const std::string &source, int count = 30);
int download(const Video &v);
int spawn_background(const std::string &cmd);
// Actions
void copy_url();
void show_related();
void show_channel();

#endif
