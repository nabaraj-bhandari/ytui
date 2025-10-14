#include <algorithm>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sys/stat.h>
#include <string>

#include "utils.h"
#include "config.h"
#include "types.h"
#include "youtube.h"

static std::map<std::string, std::pair<std::string,std::string>> video_meta;

std::string find_cached_path_by_id(const std::string &id) {
    auto cached = scan_video_cache();
    for(const auto &v : cached) {
        if(v.id == id) return v.path;
    }
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
        if (ext != "mkv" && ext != "mp4") continue;

        std::string base = name.substr(0, dot);
        Video v;
        v.path = VIDEO_CACHE + "/" + name;

        // Expect filename format: <title><id> where id is 11 chars at the end
        if (base.size() >= 11) {
            v.id = base.substr(base.size() - 11);
            v.title = base.substr(0, base.size() - 11);
        } else {
            // fallback: use whole base as id
            v.id = base;
            v.title = base;
        }

        out.push_back(v);
    }

    closedir(d);

    // augment with persisted metadata when available
    for (auto &vv : out) {
        auto it = video_meta.find(vv.id);
        if (it != video_meta.end()) {
            vv.channel_url = it->second.first;
            vv.channel_name = it->second.second;
        }
    }
    return out;
}

bool is_video_downloaded(const Video &v) {
    auto cached = scan_video_cache();
    for(const auto &c : cached) {
        if(c.id == v.id) return true;
    }
    return false;
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
    // Persist video metadata so cached file can be associated with its channel immediately
    if(!v.channel_url.empty() || !v.channel_name.empty()) {
        video_meta_store(v.id, v.channel_url, v.channel_name);
        save_video_meta();
    }
    return pid;
}

void load_channel_cache() {
    channel_cache.clear();
    std::ifstream f(CHANNEL_CACHE_FILE);
    std::string line;
    while(std::getline(f, line)) {
        if(line.empty()) continue;
        size_t sep = line.find('|');
        if(sep == std::string::npos) continue;
        std::string key = line.substr(0, sep);
        std::string url = line.substr(sep + 1);
        // trim spaces
        while(!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
        while(!url.empty() && isspace((unsigned char)url.front())) url.erase(url.begin());
        while(!url.empty() && isspace((unsigned char)url.back())) url.pop_back();
        if(!key.empty() && !url.empty()) channel_cache[key] = url;
    }
}

void save_channel_cache() {
    std::ofstream f(CHANNEL_CACHE_FILE);
    for(const auto &p : channel_cache) f << p.first << " | " << p.second << "\n";
}

std::string channel_cache_lookup(const std::string &key) {
    auto it = channel_cache.find(key);
    return it == channel_cache.end() ? std::string() : it->second;
}

void channel_cache_store(const std::string &key, const std::string &url) {
    if(key.empty() || url.empty()) return;
    channel_cache[key] = url;
}

void load_video_meta() {
    video_meta.clear();
    std::ifstream f(VIDEO_META_FILE);
    std::string line;
    while(std::getline(f, line)) {
        if(line.empty()) continue;
        if(line[0] == '#') continue;
        size_t p1 = line.find('|');
        size_t p2 = line.find('|', p1 == std::string::npos ? std::string::npos : p1 + 1);
        if(p1 == std::string::npos || p2 == std::string::npos) continue;
        std::string id = line.substr(0, p1);
        std::string url = line.substr(p1 + 1, p2 - (p1 + 1));
        std::string name = line.substr(p2 + 1);
        // trim
        while(!id.empty() && isspace((unsigned char)id.back())) id.pop_back();
        while(!url.empty() && isspace((unsigned char)url.front())) url.erase(url.begin());
        while(!url.empty() && isspace((unsigned char)url.back())) url.pop_back();
        while(!name.empty() && isspace((unsigned char)name.front())) name.erase(name.begin());
        while(!name.empty() && isspace((unsigned char)name.back())) name.pop_back();
        if(!id.empty()) video_meta[id] = std::make_pair(url, name);
    }
}

void save_video_meta() {
    std::ofstream f(VIDEO_META_FILE);
    f << "# Format: id|channel_url|channel_name\n";
    for(const auto &p : video_meta) {
        f << p.first << "|" << p.second.first << "|" << p.second.second << "\n";
    }
}

std::string video_meta_lookup_channel_url(const std::string &id) {
    auto it = video_meta.find(id);
    return it == video_meta.end() ? std::string() : it->second.first;
}

std::string video_meta_lookup_channel_name(const std::string &id) {
    auto it = video_meta.find(id);
    return it == video_meta.end() ? std::string() : it->second.second;
}

void video_meta_store(const std::string &id, const std::string &channel_url, const std::string &channel_name) {
    if(id.empty()) return;
    video_meta[id] = std::make_pair(channel_url, channel_name);
}

size_t visible_count(size_t available_rows, size_t total_items) {
    if (available_rows == 0) return 0;
    size_t cap = (size_t)MAX_LIST_ITEMS;
    size_t rows = std::min(available_rows, cap);
    return std::min(rows, total_items);
}

bool is_subscribed(const std::string &url, const std::string &name) {
    return std::any_of(subs.begin(), subs.end(), [&](const Channel &c){ return c.url == url || c.name == name; });
}

void subscribe_channel(const std::string &name, const std::string &url) {
    Channel chn; chn.name = name; chn.url = url.empty() ? name : url;
    if(!is_subscribed(chn.url, chn.name)) {
        subs.insert(subs.begin(), chn);
        save_subs();
    }
}

void unsubscribe_channel(const std::string &url, const std::string &name) {
    auto it = std::find_if(subs.begin(), subs.end(), [&](const Channel &c){ return c.url == url || c.name == name; });
    if(it != subs.end()) { subs.erase(it); save_subs(); }
}

bool toggle_subscription(const std::string &name, const std::string &url) {
    if(is_subscribed(url, name)) { unsubscribe_channel(url, name); return false; }
    subscribe_channel(name, url); return true;
}
