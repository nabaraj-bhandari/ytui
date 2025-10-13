#pragma once

// === Paths ===
// All paths are relative to the user's HOME directory
constexpr const char* CACHE_DIR         = "/.cache/ytui";
constexpr const char* VIDEO_DIR         = "/.cache/ytui/videos";
constexpr const char* HISTORY_FILE      = "/.cache/ytui/history";
constexpr const char* QUEUE_FILE        = "/.cache/ytui/queue";
constexpr const char* SUBSCRIPTIONS_FILE = "/.config/ytui/subscriptions";
constexpr const char* MPV_SOCK_PATH     = "/tmp/mpvsocket";

// === Commands & Formats ===
// MPV_ARGS is a format string; the socket path will be inserted into the %s.
constexpr const char* MPV_ARGS = "--idle --input-ipc-server=%s --really-quiet "
                                 "--ytdl-format='bestvideo[height<=?1080]+bestaudio/best' "
                                 "--hwdec=auto --fullscreen=yes";

constexpr const char* YTDLP_ARGS = "-f 'bestvideo[height<=?1080]+bestaudio/best' --no-playlist "
                                   "--restrict-filenames --merge-output-format mkv";

// === Keybindings ===
constexpr int KEY_QUIT            = 'q';
constexpr int KEY_TOGGLE_VIEW     = '\t'; // Tab
constexpr int KEY_MOVE_DOWN       = 'j';
constexpr int KEY_MOVE_UP         = 'k';
constexpr int KEY_PLAY_ITEM       = '\n'; // Enter
constexpr int KEY_APPEND_TO_QUEUE = 'a';
constexpr int KEY_REMOVE_ITEM     = 'x';

// Playback Controls
constexpr int KEY_TOGGLE_PAUSE    = ' ';
constexpr int KEY_SEEK_FORWARD    = 'l';
constexpr int KEY_SEEK_BACKWARD   = 'h';
constexpr int KEY_PLAYLIST_NEXT   = 'L';
constexpr int KEY_PLAYLIST_PREV   = 'H';
constexpr int KEY_TOGGLE_MUTE     = 'm';

// Video Context & Discovery
constexpr int KEY_SHOW_DESCRIPTION = 'd';
constexpr int KEY_YANK_URL         = 'y';
constexpr int KEY_FETCH_CHANNEL    = 'c';
constexpr int KEY_FETCH_RELATED    = 'r';

// === External Tools ===
constexpr const char* PAGER_CMD   = "less";
constexpr const char* CLIPBOARD_CMD = "xclip -selection clipboard"; // or "wl-copy" for Wayland
