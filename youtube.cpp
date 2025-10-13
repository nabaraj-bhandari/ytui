#include "youtube.h"
#include "process.h"
#include "config.h"
#include "files.h"
#include <string_view>

namespace youtube {
    static std::vector<Video> parse_output(std::string_view output) {
        std::vector<Video> videos;
        size_t start = 0;
        while ((start = output.find_first_not_of("\n", start)) != std::string_view::npos) {
            size_t end = output.find('\n', start);
            std::string_view line = output.substr(start, end - start);
            
            auto next_part = [&](std::string_view& l) -> std::string_view {
                size_t d = l.find("|||");
                std::string_view part = l.substr(0, d);
                l = (d == std::string_view::npos) ? "" : l.substr(d + 3);
                return part;
            };

            std::string_view id = next_part(line);
            std::string_view title = next_part(line);
            std::string_view channel = next_part(line);
            std::string_view duration = next_part(line);
            std::string_view views = line;

            if (!id.empty() && !title.empty()) {
                videos.push_back({std::string(id), std::string(title), std::string(channel),
                                  std::string(duration), std::string(views)});
            }
            if (end == std::string_view::npos) break;
            start = end;
        }
        return videos;
    }
    
    std::vector<Video> search(const std::string& q) {
        return parse_output(proc::exec({YTDLP_EXECUTABLE, "--no-warnings", "--flat-playlist",
            "--print", "%(id)s|||%(title)s|||%(channel)s|||%(duration_string)s|||%(view_count)s", "ytsearch30:" + q}));
    }

    std::vector<Video> fetch_channel_videos(const std::string& url) {
        return parse_output(proc::exec({YTDLP_EXECUTABLE, "--no-warnings", "--flat-playlist", "-I", "0:30",
            "--print", "%(id)s|||%(title)s|||%(channel)s|||%(duration_string)s|||%(view_count)s", url}));
    }
    
    void get_video_context(Video& v) {
        if (v.channel.empty()) {
            v.channel = proc::exec({YTDLP_EXECUTABLE, "--print", "%(channel)s", "https://www.youtube.com/watch?v=" + v.id});
            if(!v.channel.empty()) v.channel.pop_back();
        }
    }

    pid_t download(const Video& v) {
        return proc::launch_daemon({YTDLP_EXECUTABLE, "-f", YTDL_FORMAT, "--no-playlist", "--restrict-filenames",
            "--merge-output-format", "mkv", "-o", files::get_home_path(VIDEO_DIR) + "/%(id)s.%(ext)s",
            "https://www.youtube.com/watch?v=" + v.id});
    }
     std::string fetch_description(const std::string& video_id) {
        return proc::exec({YTDLP_EXECUTABLE, "--get-description", "https://www.youtube.com/watch?v=" + video_id});
    }
}
