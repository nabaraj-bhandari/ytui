#include "youtube.h"
#include "config.h"
#include "utils.h"

// System headers
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <cstdio>


std::vector<Video> fetch_videos(const std::string &source, int count) {
    std::vector<Video> r;
    set_status("Fetching...");
    // Enforce an upper bound to avoid overflowing UI lists
    if(count > MAX_LIST_ITEMS) count = MAX_LIST_ITEMS;
    std::string cmd = "yt-dlp --no-warnings --flat-playlist --print \"%(id)s|||%(title)s|||%(channel_url)s|||%(channel)s\" ";
    cmd += (source.find("youtube.com") != std::string::npos || source.find("youtu.be") != std::string::npos)
        ? "-I 0:" + std::to_string(count) + " \"" + source + "\" 2>/dev/null"
        : "\"ytsearch" + std::to_string(count) + ":" + source + "\" 2>/dev/null";
    FILE *p = popen(cmd.c_str(), "r");
    if(!p) { set_status("Fetch failed"); return r; }
    char buf[4096];
    while(fgets(buf, sizeof(buf), p)) {
        std::string line(buf);
        if(!line.empty() && line.back() == '\n') line.pop_back();
        size_t d1 = line.find("|||");
        if(d1 == std::string::npos) continue;
        size_t d2 = line.find("|||", d1 + 3);
        size_t d3 = line.find("|||", d2 + 3);
        std::string id = line.substr(0, d1);
        std::string title = line.substr(d1 + 3, d2 != std::string::npos ? d2 - d1 - 3 : std::string::npos);
        std::string curl = d2 != std::string::npos ? line.substr(d2 + 3, d3 != std::string::npos ? d3 - d2 - 3 : std::string::npos) : std::string();
        std::string cname = d3 != std::string::npos ? line.substr(d3 + 3) : std::string();

        r.push_back({id, title, "", curl, cname});
    }
    pclose(p);
    set_status("Found " + std::to_string(r.size()) + " videos");
    return r;
}

int spawn_background(const std::string &cmd) {
    FILE *p = popen((cmd + " >/dev/null 2>&1 & echo $!").c_str(), "r");
    int pid = (p && fscanf(p, "%d", &pid) == 1) ? pid : -1;
    if(p) pclose(p);
    return pid;
}

int download(const Video &v) {
    ensure_video_cache();
    std::string cmd = "yt-dlp -f \"" + std::string(YTDL_FMT) + "\" --restrict-filenames -o \"" + VIDEO_CACHE + "/%(title)s%(id)s.mkv\" \"https://www.youtube.com/watch?v=" + v.id + "\"";
    return spawn_background(cmd);
}

void show_channel() {
    if(!res.empty() && sel < res.size()) {
        show_channel_for(res[sel]);
        return;
    } else if(subs_channel_idx >= 0 && subs.size() > (size_t)subs_channel_idx) {
        Video v;
        v.title = subs[subs_channel_idx].name;
        v.channel_url = subs[subs_channel_idx].url;
        show_channel_for(v);
        return;
    }
    set_status("No channel URL available");
}

void show_channel_for(const Video &v) {
    std::string url = v.channel_url;
    if(url.empty() && !v.id.empty()) url = resolve_channel_url_for_video(v.id);
    if(url.empty()) {
        set_status("No channel URL available");
        return;
    }

    if(!channel_return_active && focus != CHANNEL) {
        channel_return_focus = focus;
        if(focus == SUBSCRIPTIONS && subs_channel_idx >= 0)
            channel_return_sel = static_cast<size_t>(subs_channel_idx);
        else
            channel_return_sel = sel;
        channel_return_active = true;
    }

    enter_channel_view(url);
}

void enter_channel_view(const std::string &url, const std::vector<Video> *prefetched) {
    if(url.empty()) {
        set_status("No channel URL available");
        return;
    }
    channel_url = url;

    if(prefetched) {
        channel_videos = *prefetched;
    } else {
        channel_videos = fetch_videos(url, MAX_LIST_ITEMS);
    }
    focus = CHANNEL;
    sel = 0;

    if(channel_videos.empty()) set_status("No videos found for channel");
    else set_status("Inside a channel");
}

std::string resolve_channel_url_for_video(const std::string &video_id) {
    std::string cmd = "yt-dlp --no-warnings --print \"%(channel_url)s\" https://www.youtube.com/watch?v=" + video_id + " 2>/dev/null";
    FILE *p = popen(cmd.c_str(), "r");
    if(!p) return std::string();
    char buf[1024];
    std::string out;
    if(fgets(buf, sizeof(buf), p)) {
        out = std::string(buf);
        if(!out.empty() && out.back() == '\n') out.pop_back();
    }
    pclose(p);
    return out;
}

