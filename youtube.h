#pragma once
#include "structs.h"
#include <string>
#include <vector>

namespace youtube {
    std::vector<Video> search(const std::string& query);
    std::vector<Video> fetch_channel_videos(const std::string& channel_url);
    pid_t download(const Video& v);
    std::string fetch_description(const std::string& video_id);
    void get_video_context(Video& v);
}
