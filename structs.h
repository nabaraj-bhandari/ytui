#pragma once
#include <string>
#include <unistd.h>

struct Video {
    std::string id;
    std::string title;
    std::string channel;
    std::string duration;
    std::string views;
};

struct Subscription {
    std::string name;
    std::string url;
};

struct Download {
    std::string video_id;
    pid_t pid;
};

enum class UiMode { SEARCH, QUEUE, SUBSCRIPTIONS };
