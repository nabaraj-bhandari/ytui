#pragma once

// === Paths ===
constexpr const char* CACHE_DIR         = "/.cache/ytui";
constexpr const char* VIDEO_DIR         = "/.cache/ytui/videos";
constexpr const char* HISTORY_FILE      = "/.cache/ytui/history";
constexpr const char* QUEUE_FILE        = "/.cache/ytui/queue";
constexpr const char* SUBSCRIPTIONS_FILE = "/.config/ytui/subscriptions";
constexpr const char* MPV_SOCK_PATH     = "/tmp/mpvsocket";

// === Formats & Commands ===
constexpr const char* YTDL_FORMAT = "bestvideo[height<=?1080]+bestaudio/best";
constexpr const char* MPV_EXECUTABLE = "mpv";
constexpr const char* YTDLP_EXECUTABLE = "yt-dlp";

// === Keybindings ===
constexpr int KEY_QUIT            = 'q';
constexpr int KEY_TOGGLE_VIEW     = '\t';
constexpr int KEY_MOVE_DOWN       = 'j';
constexpr int KEY_MOVE_UP         = 'k';
constexpr int KEY_PLAY_ITEM       = '\n';
constexpr int KEY_APPEND_TO_QUEUE = 'a';
constexpr int KEY_REMOVE_ITEM     = 'x';
constexpr int KEY_MOVE_ITEM_DOWN  = 'J'; // Shift+J
constexpr int KEY_MOVE_ITEM_UP    = 'K'; // Shift+K
constexpr int KEY_TOGGLE_PAUSE    = ' ';
constexpr int KEY_SEEK_FORWARD    = 'l';
constexpr int KEY_SEEK_BACKWARD   = 'h';
constexpr int KEY_TOGGLE_HELP     = '?';
constexpr int KEY_SHOW_DESCRIPTION = 'd';
constexpr int KEY_YANK_URL         = 'y';
constexpr int KEY_FETCH_CHANNEL    = 'c';
constexpr int KEY_FETCH_RELATED    = 'r';

// === External Tools ===
constexpr const char* PAGER_CMD   = "less";
constexpr const char* CLIPBOARD_CMD = "xclip"; // Command for clipboard, e.g., "xclip -selection clipboard"
