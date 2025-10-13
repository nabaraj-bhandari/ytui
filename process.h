#pragma once
#include "structs.h"
#include <string>
#include <vector>

namespace proc {
    std::string exec(const std::vector<std::string>& args);
    pid_t launch_daemon(const std::vector<std::string>& args);
    void spawn_pager(const std::string& input);
    void spawn_clipboard(const std::string& input);

    class DownloadManager {
    public:
        void add(const std::string& video_id, pid_t pid);
        bool is_downloading(const std::string& video_id);
        void check_finished();
    private:
        std::vector<Download> downloads;
    };
}
