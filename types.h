#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>

struct Video {
    std::string id, title, path, channel_url, channel_name;
    bool operator==(const Video &v) const { return id == v.id; }
};

struct Channel {
    std::string name, url;
};

struct Download {
    Video v;
    int pid;
    bool done;
};

enum Focus { HOME, DOWNLOADS, SUBSCRIPTIONS, CHANNEL, SEARCH, RESULTS };

#endif
