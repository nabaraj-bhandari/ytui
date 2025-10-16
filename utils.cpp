#include "utils.h"

#include "config.h"
#include "globals.h"
#include "types.h"
#include "youtube.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <utility>

namespace {

constexpr std::array<const char *, 2> VIDEO_EXTENSIONS = {"mkv", "mp4"};
constexpr size_t YT_ID_LENGTH = 11;

bool is_supported_extension(const std::string &ext) {
    return std::any_of(VIDEO_EXTENSIONS.begin(), VIDEO_EXTENSIONS.end(),
                       [&](const char *candidate) { return ext == candidate; });
}

} // namespace

std::string find_cached_path_by_id(const std::string &id) {
    DIR *d = opendir(VIDEO_CACHE.c_str());
    if (!d) return std::string();

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_type != DT_REG) continue;

        std::string name(ent->d_name);
        size_t dot = name.rfind('.');
        if (dot == std::string::npos) continue;

        std::string ext = name.substr(dot + 1);
        if (!is_supported_extension(ext)) continue;

        std::string base = name.substr(0, dot);
        if (base.size() >= id.size() &&
            base.compare(base.size() - id.size(), id.size(), id) == 0) {
            std::string path = VIDEO_CACHE;
            path += '/';
            path += name;
            closedir(d);
            return path;
        }
    }

    closedir(d);
    return std::string();
}

void mkdirs() {
    mkdir(CONFIG_DIR.c_str(), 0755);
    mkdir(CACHE_DIR.c_str(), 0755);
    mkdir(VIDEO_CACHE.c_str(), 0755);
}

bool file_exists(const std::string &path) {
    struct stat buf;
    return stat(path.c_str(), &buf) == 0;
}

void ensure_video_cache() {
    mkdir(CACHE_DIR.c_str(), 0755);
    mkdir(VIDEO_CACHE.c_str(), 0755);
}

void set_status(const std::string &msg) {
    status_msg = msg;
    status_time = time(nullptr);
}

void play(const Video &v) {
    std::string local_path = find_cached_path_by_id(v.id);
    std::string path = !local_path.empty() ? local_path : "https://www.youtube.com/watch?v=" + v.id;
    std::string cmd = "setsid mpv ";
    cmd += std::string(MPV_ARGS) + " '" + path + "' </dev/null >/dev/null 2>&1 &";
    system(cmd.c_str());
    auto it = std::find(history.begin(), history.end(), v);
    if(it != history.end()) {
        Video ex = *it;
        history.erase(it);
        history.insert(history.begin(), ex);
    } else {
        history.insert(history.begin(), v);
    }
    save_history();
    set_status("Playing: " + v.title);
}

void load_search_hist() {
    std::ifstream f(SEARCH_HISTORY_FILE);
    std::string line;
    while(std::getline(f, line)) if(!line.empty()) search_hist.push_back(line);
}

void save_search_hist() {
    std::ofstream f(SEARCH_HISTORY_FILE);
    for(const auto &s : search_hist) f << s << "\n";
}

void add_search_hist(const std::string &s) {
    auto it = std::find(search_hist.begin(), search_hist.end(), s);
    if(it != search_hist.end()) search_hist.erase(it);
    search_hist.insert(search_hist.begin(), s);
    if(search_hist.size() > 50) search_hist.resize(50);
    save_search_hist();
}

void load_history() {
    std::ifstream f(HISTORY_FILE);
    std::string line;
    while(std::getline(f, line)) {
        size_t d = line.find("|||");
        if(d != std::string::npos) {
            Video v;
            v.id = line.substr(0, d);
            v.title = unesc(line.substr(d + 3));
            v.path = VIDEO_CACHE + "/" + v.id + ".mkv";
            history.push_back(v);
        }
    }
}

void save_history() {
    std::ofstream f(HISTORY_FILE);
    for(const auto &v : history) f << v.id << "|||" << esc(v.title) << "\n";
}

void load_subs() {
    subs.clear();
    std::ifstream f(SUBS_FILE);
    std::string line;
    while(std::getline(f, line)) {
        if(line.empty()) continue;
        if(line[0] == '#') continue;
        size_t sep = line.find('|');
        Channel ch;
        if(sep != std::string::npos) {
            ch.name = line.substr(0, sep);
            ch.url = line.substr(sep + 1);
            // trim
            while(!ch.name.empty() && isspace((unsigned char)ch.name.back())) ch.name.pop_back();
            while(!ch.url.empty() && isspace((unsigned char)ch.url.front())) ch.url.erase(ch.url.begin());
            while(!ch.url.empty() && isspace((unsigned char)ch.url.back())) ch.url.pop_back();
        } else {
            ch.name = line;
            ch.url = line;
        }
        subs.push_back(ch);
    }
}

void save_subs() {
    std::ofstream f(SUBS_FILE);
    f << "# Format: Name | URL\n";
    for(const auto &ch : subs) f << ch.name << " | " << ch.url << "\n";
}

std::vector<Video> scan_video_cache() {
    std::vector<Video> out;
    DIR *d = opendir(VIDEO_CACHE.c_str());
    if (!d) return out;

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_type != DT_REG) continue;

        std::string name(ent->d_name);
        size_t dot = name.rfind('.');
        if (dot == std::string::npos) continue;

        std::string ext = name.substr(dot + 1);
        if (!is_supported_extension(ext)) continue;

        std::string base = name.substr(0, dot);
        Video v;
        std::string path = VIDEO_CACHE;
        path += '/';
        path += name;
        v.path = std::move(path);

        if (base.size() >= YT_ID_LENGTH) {
            v.id = base.substr(base.size() - YT_ID_LENGTH);
            v.title = base.substr(0, base.size() - YT_ID_LENGTH);
        } else {
            v.id = base;
            v.title = base;
        }

        out.push_back(std::move(v));
    }

    closedir(d);

    return out;
}

bool is_video_downloaded(const Video &v) {
    return !find_cached_path_by_id(v.id).empty();
}

std::vector<Video> collect_download_items(const std::vector<Video> &cached) {
    std::vector<Video> out;
    out.reserve(cached.size() + downloads.size());
    out.insert(out.end(), cached.begin(), cached.end());

    for (const auto &d : downloads) {
        const bool already_cached = std::any_of(
            cached.begin(), cached.end(),
            [&](const Video &c) { return c.id == d.v.id; });
        if (already_cached) continue;

        Video candidate = d.v;
        candidate.path = VIDEO_CACHE + "/" + candidate.id + ".mkv";
        out.push_back(std::move(candidate));
    }

    return out;
}

std::vector<Video> collect_download_items() {
    auto cached = scan_video_cache();
    return collect_download_items(cached);
}

void update_download_statuses(const std::vector<Video> &cached) {
    for (auto &d : downloads) {
        const bool found = std::any_of(
            cached.begin(), cached.end(),
            [&](const Video &c) { return c.id == d.v.id; });
        d.done = found;
        if (found) d.pid = 0;
    }
}

std::string esc(const std::string &s) {
    std::string out;
    for(char c : s) {
        if(c == '\n') out += "\\n";
        else if(c == '|') out += "\\p";
        else out += c;
    }
    return out;
}

std::string unesc(const std::string &s) {
    std::string out;
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i+1];
            if(n == 'n') { out += '\n'; i++; }
            else if(n == 'p') { out += '|'; i++; }
            else out += n;
        } else out += s[i];
    }
    return out;
}

int enqueue_download(const Video &v) {
    ensure_video_cache();
    int pid = download(v);
    Download dl2; dl2.v = v; dl2.pid = pid; dl2.done = false; dl2.v.path = VIDEO_CACHE + "/" + v.id + ".mkv";
    downloads.insert(downloads.begin(), dl2);
    set_status("Downloading: " + v.title);
    return pid;
}

size_t visible_count(size_t available_rows, size_t total_items) {
    if (available_rows == 0) return 0;
    return std::min(available_rows, total_items);
}

bool is_subscribed(const std::string &url, const std::string &name) {
    return std::any_of(subs.begin(), subs.end(), [&](const Channel &c){ return c.url == url || c.name == name; });
}
