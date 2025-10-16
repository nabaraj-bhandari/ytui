#include "youtube.h"

#include "config.h"
#include "globals.h"
#include "utils.h"

#include <cstdio>
#include <sstream>
#include <utility>

namespace {

constexpr char FIELD_DELIM[] = "|||";

class Pipe {
public:
    Pipe(const std::string &command, const char *mode = "r")
        : handle_(popen(command.c_str(), mode)) {}

    ~Pipe() {
        if (handle_) pclose(handle_);
    }

    Pipe(const Pipe &) = delete;
    Pipe &operator=(const Pipe &) = delete;

    Pipe(Pipe &&other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Pipe &operator=(Pipe &&other) noexcept {
        if (this != &other) {
            if (handle_) pclose(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    FILE *get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    FILE *handle_ = nullptr;
};

std::string build_fetch_command(const std::string &source, int count) {
    std::ostringstream cmd;
    cmd << "yt-dlp --no-warnings --flat-playlist --print \"%(id)s|||%(title)s|||%(channel_url)s|||%(channel)s\" ";
    if (source.find("youtube.com") != std::string::npos ||
        source.find("youtu.be") != std::string::npos) {
        cmd << "-I 0:" << count << " \"" << source << "\" 2>/dev/null";
    } else {
        cmd << "\"ytsearch" << count << ":" << source << "\" 2>/dev/null";
    }
    return cmd.str();
}

bool read_line(FILE *handle, std::string &out) {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), handle)) return false;
    out.assign(buf);
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return true;
}

void append_video_from_line(std::vector<Video> &list, const std::string &line) {
    const size_t d1 = line.find(FIELD_DELIM);
    if (d1 == std::string::npos) return;
    const size_t d2 = line.find(FIELD_DELIM, d1 + 3);
    const size_t d3 = d2 == std::string::npos ? std::string::npos
                                              : line.find(FIELD_DELIM, d2 + 3);

    Video video;
    video.id.assign(line, 0, d1);

    if (d2 != std::string::npos) {
        video.title.assign(line, d1 + 3, d2 - (d1 + 3));
        if (d3 != std::string::npos) {
            video.channel_url.assign(line, d2 + 3, d3 - (d2 + 3));
            video.channel_name.assign(line, d3 + 3, std::string::npos);
        } else {
            video.channel_url.assign(line, d2 + 3, std::string::npos);
        }
    } else {
        video.title.assign(line, d1 + 3, std::string::npos);
    }

    list.push_back(std::move(video));
}

} // namespace

std::vector<Video> fetch_videos(const std::string &source, int count) {
    std::vector<Video> videos;
    set_status("Fetching...");

    if (count > MAX_LIST_ITEMS) count = MAX_LIST_ITEMS;
    if (count > 0) videos.reserve(static_cast<size_t>(count));

    Pipe pipe(build_fetch_command(source, count));
    if (!pipe) {
        set_status("Fetch failed");
        return videos;
    }

    std::string line;
    while (read_line(pipe.get(), line)) {
        append_video_from_line(videos, line);
    }

    set_status("Found " + std::to_string(videos.size()) + " videos");
    return videos;
}

int spawn_background(const std::string &cmd) {
    Pipe pipe(cmd + " >/dev/null 2>&1 & echo $!");
    if (!pipe) return -1;

    int pid = -1;
    if (fscanf(pipe.get(), "%d", &pid) != 1) return -1;
    return pid;
}

int download(const Video &v) {
    ensure_video_cache();

    std::ostringstream cmd;
    cmd << "yt-dlp -f \"" << YTDL_FMT
        << "\" --restrict-filenames -o \"" << VIDEO_CACHE
        << "/%(title)s%(id)s.mkv\" \"https://www.youtube.com/watch?v="
        << v.id << "\"";
    return spawn_background(cmd.str());
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
    channel_scroll = 0;

    if(channel_videos.empty()) {
        set_status("No videos found for channel");
        hide_thumbnail();
    } else {
        set_status("Inside a channel");
        show_thumbnail(channel_videos[sel]);
        preload_thumbnails(channel_videos, sel + 1);
    }
}

std::string resolve_channel_url_for_video(const std::string &video_id) {
    Pipe pipe("yt-dlp --no-warnings --print \"%(channel_url)s\" https://www.youtube.com/watch?v=" + video_id + " 2>/dev/null");
    if (!pipe) return std::string();

    std::string out;
    if (!read_line(pipe.get(), out)) return std::string();
    return out;
}
