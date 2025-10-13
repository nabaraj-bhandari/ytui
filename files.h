#pragma once
#include "structs.h"
#include <string>
#include <vector>

namespace files {
    std::string get_home_path(const char* subpath);
    void setup_dirs();
    bool is_cached(const Video& v);
    void save_queue(const std::vector<Video>& queue);
    void save_history(const std::vector<std::string>& history);
    std::vector<Video> load_queue();
    std::vector<std::string> load_history();
    std::vector<Subscription> load_subscriptions();
}
