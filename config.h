#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <cstdlib>

// Paths
inline const std::string CONFIG_DIR = std::string(getenv("HOME")) + "/.config/ytui";
inline const std::string CACHE_DIR = std::string(getenv("HOME")) + "/.cache/ytui";
inline const std::string VIDEO_CACHE = CACHE_DIR + "/videos";
inline const std::string HISTORY_FILE = CACHE_DIR + "/history.txt";
inline const std::string SEARCH_HISTORY_FILE = CACHE_DIR + "/search_history.txt";
inline const std::string MPV_SOCK = "/tmp/mpvsocket";
inline const std::string SUBS_FILE = CONFIG_DIR + "/subscriptions";

// MPV & yt-dlp configuration
inline const char* MPV_ARGS = "--fs --ytdl-format='137+234/136+234/399+234/398+234/232+234/270+234/609+234/614+234/248+234/247+234/135+233/134+233/best'";
inline const char* YTDL_FMT = "137+234/136+234/399+234/398+234/232+234/270+234/609+234/614+234/248+234/247+234/135+233/134+233/best";

// Key bindings (minimal defaults)
static const int APP_KEY_QUIT = 'Q';
static const int APP_KEY_RELATED = 'r';
static const int APP_KEY_DOWNLOAD = 'D';
static const int APP_KEY_COPY = 'y';
static const int APP_KEY_CHANNEL = 'c';
static const int APP_KEY_HOME = 'a';
static const int APP_KEY_SEARCH = 's';
static const int APP_KEY_DOWNLOADS = 'd';
static const int APP_KEY_SUBS = 'w';
static const int APP_KEY_PROFILE = 'p';
static const int APP_KEY_DOWN = 'j';
static const int APP_KEY_UP = 'k';

#endif

