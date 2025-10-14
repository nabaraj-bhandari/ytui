#include "youtube.h"
#include "config.h"
#include "utils.h"
#include <ncurses.h>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>

std::vector<Video> fetch_videos(const std::string &source, int count) {
    std::vector<Video> r;
    set_status("Fetching...");
    std::string cmd = "yt-dlp --no-warnings --flat-playlist --print \"%(id)s|||%(title)s|||%(channel_url)s\" ";

    if(source.find("youtube.com") != std::string::npos || source.find("youtu.be") != std::string::npos) {
        cmd += "-I 0:" + std::to_string(count) + " \"" + source + "\" 2>/dev/null";
    } else {
        cmd += "\"ytsearch" + std::to_string(count) + ":" + source + "\" 2>/dev/null";
    }

    FILE *p = popen(cmd.c_str(), "r");
    if(!p) {
        set_status("Fetch failed");
        return r;
    }
    char buf[4096];
    while(fgets(buf, sizeof(buf), p)) {
        std::string line(buf);
        if(!line.empty() && line.back() == '\n') line.pop_back();

        size_t d1 = line.find("|||");
        if(d1 != std::string::npos) {
            size_t d2 = line.find("|||", d1 + 3);
            Video v;
            v.id = line.substr(0, d1);
            v.title = line.substr(d1 + 3, d2 != std::string::npos ? d2 - d1 - 3 : std::string::npos);
            if(d2 != std::string::npos) v.channel_url = line.substr(d2 + 3);
            v.path = VIDEO_CACHE + "/" + v.id + ".mkv";
            r.push_back(v);
        }
    }
    pclose(p);
    set_status("Found " + std::to_string(r.size()) + " videos");
    return r;
}

int spawn_background(const std::string &cmd) {
    // returns PID of backgrounded shell command or -1
    std::string full = cmd + " >/dev/null 2>&1 & echo $!";
    FILE *p = popen(full.c_str(), "r");
    if(!p) return -1;
    int pid = -1;
    if(fscanf(p, "%d", &pid) != 1) pid = -1;
    pclose(p);
    return pid;
}

int download(const Video &v) {
    // Full download via yt-dlp into VIDEO_CACHE
    std::string out = VIDEO_CACHE + "/" + v.id + ".mkv";
    // ensure dir exists
    mkdir(VIDEO_CACHE.c_str(), 0755);
    std::string cmd = "yt-dlp -f \"" + std::string(YTDL_FMT) + "\" --write-info-json -o \"" + out + "\" \"https://www.youtube.com/watch?v=" + v.id + "\"";
    return spawn_background(cmd);
}

void show_description() {
    if(res.empty() || sel >= res.size()) return;
    const Video &v = res[sel];
    set_status("Fetching description...");
    std::string tmp = "/tmp/ytui_desc_" + v.id + ".txt";
    system(("yt-dlp --skip-download --get-description 'https://www.youtube.com/watch?v=" + 
            v.id + "' > " + tmp + " 2>/dev/null").c_str());

    def_prog_mode();
    endwin();
    system(("less " + tmp).c_str());
    reset_prog_mode();
    refresh();
    set_status("Back to ytui");
}

void copy_url() {
    if(res.empty() || sel >= res.size()) return;
    const Video &v = res[sel];
    std::string url = "https://www.youtube.com/watch?v=" + v.id;

    if(system(("echo '" + url + "' | xclip -selection clipboard 2>/dev/null").c_str()) == 0) {
        set_status("Copied to clipboard (xclip)");
    } else if(system(("echo '" + url + "' | wl-copy 2>/dev/null").c_str()) == 0) {
        set_status("Copied to clipboard (wl-copy)");
    } else if(system(("echo '" + url + "' | xsel -b 2>/dev/null").c_str()) == 0) {
        set_status("Copied to clipboard (xsel)");
    } else {
        set_status("No clipboard tool found");
    }
}

void show_related() {
    if(history.empty()) return;
    const Video &v = history[0];
    set_status("Finding related videos...");
    res = fetch_videos("https://www.youtube.com/watch?v=" + v.id, 20);
    if(!res.empty()) {
        focus = RESULTS;
        sel = 0;
    }
}

void show_channel() {
    if(res.empty() || sel >= res.size()) return;
    const Video &v = res[sel];
    if(v.channel_url.empty()) {
        set_status("No channel URL available");
        return;
    }
    set_status("Loading channel...");
    res = fetch_videos(v.channel_url, 20);
    sel = 0;
}

void show_trending() {
    set_status("Loading trending...");
    res = fetch_videos("https://www.youtube.com/feed/trending", 30);
    if(!res.empty()) {
        focus = RESULTS;
        sel = 0;
    }
}
