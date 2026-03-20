#ifndef CONFIG_H
#define CONFIG_H

#include <cstdlib>
#include <string>

// Paths
inline const std::string CONFIG_DIR =
    std::string(getenv("HOME")) + "/.config/ytui";
inline const std::string CACHE_DIR =
    std::string(getenv("HOME")) + "/.cache/ytui";
inline const std::string VIDEO_CACHE = CACHE_DIR + "/videos";
inline const std::string THUMBNAIL_CACHE = CACHE_DIR + "/thumbs";
inline const std::string HISTORY_FILE = CACHE_DIR + "/history.txt";
inline const std::string SEARCH_HISTORY_FILE =
    CACHE_DIR + "/search_history.txt";
inline const std::string SUBS_FILE = CONFIG_DIR + "/subscriptions.txt";

// MPV & yt-dlp configuration
inline const char *MPV_ARGS =
    "--fs --panscan=1 "
    "--ytdl-raw-options=no-check-certificates=,http-chunk-size=0 "
    "--ytdl-format='bestvideo[height<=1440][height>=720]+bestaudio/"
    "bestvideo[height<=1440]+bestaudio/best[height<=1440]/best'";
inline const char *YTDL_FMT =
    "bestvideo[height<=1440][height>=720]+bestaudio/"
    "bestvideo[height<=1440]+bestaudio/best[height<=1440]/best";

// Key bindings (minimal defaults)
static const int APP_KEY_QUIT = 'Q';
static const int APP_KEY_DOWNLOAD = 'D';
static const int APP_KEY_CHANNEL = 'c';
static const int APP_KEY_SUB_TOGGLE = 'S';
static const int APP_KEY_HOME = 'a';
static const int APP_KEY_SEARCH = 's';
static const int APP_KEY_DOWNLOADS = 'd';
static const int APP_KEY_SUBS = 'w';
static const int APP_KEY_THUMBNAIL = 't';

static const int MAX_LIST_ITEMS = 50;

#endif
