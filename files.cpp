#include "files.h"
#include "config.h"
#include <fstream>
#include <sys/stat.h>

namespace files {
    std::string get_home_path(const char* subpath) {
        const char* home = getenv("HOME");
        return home ? std::string(home) + subpath : "";
    }

    void setup_dirs() {
        mkdir(get_home_path(CACHE_DIR).c_str(), 0755);
        mkdir(get_home_path(VIDEO_DIR).c_str(), 0755);
    }

    bool is_cached(const Video& v) {
        struct stat buffer;
        return stat((get_home_path(VIDEO_DIR) + "/" + v.id + ".mkv").c_str(), &buffer) == 0;
    }

    void save_queue(const std::vector<Video>& queue) {
        std::ofstream f(get_home_path(QUEUE_FILE));
        for (const auto& v : queue) f << v.id << "|||" << v.title << "\n";
    }

    void save_history(const std::vector<std::string>& history) {
        std::ofstream f(get_home_path(HISTORY_FILE));
        for (const auto& s : history) f << s << "\n";
    }

    std::vector<Video> load_queue() {
        std::vector<Video> q;
        std::ifstream f(get_home_path(QUEUE_FILE));
        std::string line;
        while (std::getline(f, line)) {
            size_t d = line.find("|||");
            if (d != std::string::npos) {
                // **FIX**: Explicitly initialize all members of the struct to silence the warning.
                q.push_back({
                    line.substr(0, d),      // id
                    line.substr(d + 3),     // title
                    "",                     // channel
                    "",                     // duration
                    ""                      // views
                });
            }
        }
        return q;
    }

    std::vector<std::string> load_history() {
        std::vector<std::string> hist;
        std::ifstream f(get_home_path(HISTORY_FILE));
        std::string line;
        while (std::getline(f, line)) if (!line.empty()) hist.push_back(line);
        return hist;
    }

    std::vector<Subscription> load_subscriptions() {
        std::vector<Subscription> subs;
        std::ifstream f(get_home_path(SUBSCRIPTIONS_FILE));
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t d = line.find(' ');
            if (d != std::string::npos) subs.push_back({line.substr(0, d), line.substr(d + 1)});
        }
        return subs;
    }
}
