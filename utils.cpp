#include "utils.h"
#include "config.h"
#include "types.h"
#include "youtube.h"

#include <sys/stat.h>

#include <ctime>
#include <fstream>
#include <algorithm>
#include <dirent.h>
#include <cstring>
#include <cstdlib>


void mkdirs() { 
    mkdir(CONFIG_DIR.c_str(), 0755);
    mkdir(CACHE_DIR.c_str(), 0755); 
    mkdir(VIDEO_CACHE.c_str(), 0755); 
}

bool file_exists(const std::string &path) { 
    struct stat buf; 
    return stat(path.c_str(), &buf) == 0; 
}

void set_status(const std::string &msg) {
    status_msg = msg;
    status_time = time(nullptr);
}

void play(const Video &v) {
    std::string path = file_exists(v.path) ? v.path : "https://www.youtube.com/watch?v=" + v.id;
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

// Search history
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

// Video history
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

// Subscriptions
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
        } else {
            ch.name = line;
            ch.url = line;
        }
        subs.push_back(ch);
    }
}

void save_subs() {
    std::ofstream f(SUBS_FILE);
    f << "# Format: Name|URL\n";
    for(const auto &ch : subs) f << ch.name << "|" << ch.url << "\n";
}



// Scan VIDEO_CACHE for downloaded files and return Video entries
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

        std::string id = name.substr(0, dot);
        Video v;
        v.id = id;
        v.path = VIDEO_CACHE + "/" + name;

        // Load title from history if available
        auto it = std::find_if(history.begin(), history.end(),
            [&](const Video &h){ return h.id == id; });
        if (it != history.end()) v.title = it->title;
        else v.title = id; // fallback

        // Try to read metadata JSON file (<id>.info.json)
        std::string info_path = VIDEO_CACHE + "/" + id + ".info.json";
        FILE *f = fopen(info_path.c_str(), "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::string buf(size, '\0');
            fread(&buf[0], 1, size, f);
            fclose(f);

            // crude string extraction for small footprint
            auto find_field = [&](const char *key) -> std::string {
                std::string k = "\"" + std::string(key) + "\":";
                size_t pos = buf.find(k);
                if (pos == std::string::npos) return "";
                pos = buf.find('"', pos + k.size());
                if (pos == std::string::npos) return "";
                size_t end = buf.find('"', pos + 1);
                if (end == std::string::npos) return "";
                return buf.substr(pos + 1, end - pos - 1);
            };

            std::string title = find_field("title");
            std::string channel = find_field("channel_url");

            if (!title.empty()) v.title = title;
            if (!channel.empty()) v.channel_url = channel;
        }

        out.push_back(v);
    }

    closedir(d);
    return out;
}


// background/threaded operations removed - use synchronous fetches instead

// Simple escaping to preserve newlines and pipes in titles
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
