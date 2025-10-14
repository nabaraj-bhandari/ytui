#include "youtube.h"
#include "config.h"
#include "utils.h"
#include <sys/stat.h>

std::vector<Video> fetch_videos(const std::string &source, int count) {
    std::vector<Video> r;
    set_status("Fetching...");
    std::string cmd = "yt-dlp --no-warnings --flat-playlist --print \"%(id)s|||%(title)s|||%(channel_url)s\" ";
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
        r.push_back({line.substr(0, d1), line.substr(d1 + 3, d2 != std::string::npos ? d2 - d1 - 3 : std::string::npos), d2 != std::string::npos ? line.substr(d2 + 3) : "", ""});
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
    mkdir(VIDEO_CACHE.c_str(), 0755);
    std::string cmd = "yt-dlp -f \"" + std::string(YTDL_FMT) + "\" --restrict-filenames -o \"" + VIDEO_CACHE + "/%(title)s%(id)s.mkv\" \"https://www.youtube.com/watch?v=" + v.id + "\"";
    return spawn_background(cmd);
}

void copy_url() {
    if(res.empty() || sel >= res.size()) return;
    std::string url = "https://www.youtube.com/watch?v=" + res[sel].id;
    int ok = system(("echo '" + url + "' | xclip -selection clipboard 2>/dev/null").c_str());
    if(ok == 0) set_status("Copied to clipboard (xclip)");
    else if(system(("echo '" + url + "' | wl-copy 2>/dev/null").c_str()) == 0) set_status("Copied to clipboard (wl-copy)");
    else if(system(("echo '" + url + "' | xsel -b 2>/dev/null").c_str()) == 0) set_status("Copied to clipboard (xsel)");
    else set_status("No clipboard tool found");
}

void show_related() {
    if(history.empty()) return;
    set_status("Finding related videos...");
    res = fetch_videos("https://www.youtube.com/watch?v=" + history[0].id, 20);
    if(!res.empty()) { focus = RESULTS; sel = 0; }
}

void show_channel() {
    if(res.empty() || sel >= res.size() || res[sel].channel_url.empty()) { set_status("No channel URL available"); return; }
    set_status("Loading channel...");
    res = fetch_videos(res[sel].channel_url, 20);
    sel = 0;
}

