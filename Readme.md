# ytui - Terminal YouTube Client (Prototype 2)

A minimal, keyboard-driven terminal YouTube client with mpv integration and YouTube search.

## Features (Prototype 2)

- ✅ Full TUI with multiple windows (header, main, MPV logs, footer)
- ✅ **YouTube search integration** via yt-dlp
- ✅ Three UI modes: Playlist, Search Input, Search Results
- ✅ Dynamic playlist management
- ✅ Vim-style navigation (j/k)
- ✅ MPV IPC integration with dedicated log section
- ✅ Add individual or all search results to playlist
- ✅ Delete tracks from playlist
- ✅ Visual feedback for current playing track

## Dependencies

```bash
# Arch Linux
sudo pacman -S mpv yt-dlp ncurses

# Ubuntu/Debian
sudo apt install mpv yt-dlp libncurses-dev

# Fedora
sudo dnf install mpv yt-dlp ncurses-devel
```

## Build & Run

```bash
# Compile
make

# Run
./build/ytui

# Install system-wide (optional)
sudo make install
```

## Usage

### Basic Workflow

1. Press `/` to enter search mode
2. Type your search query (e.g., "lofi hip hop")
3. Press `ENTER` to search YouTube
4. Navigate results with `j/k`
5. Press `ENTER` to add selected video to playlist
6. Or press `a` to add all results
7. Press `ESC` to return to playlist
8. Navigate playlist with `j/k`
9. Press `ENTER` to play selected track

## Keybindings

### Playlist Mode
| Key     | Action                      |
|---------|-----------------------------|
| `/`     | Enter search mode           |
| `j`     | Move down in list           |
| `k`     | Move up in list             |
| `ENTER` | Play selected video         |
| `p`     | Toggle pause/play           |
| `h`     | Previous track              |
| `l`     | Next track                  |
| `d`     | Delete selected from playlist|
| `q`     | Quit                        |

### Search Input Mode
| Key        | Action                   |
|------------|--------------------------|
| Type       | Enter search query       |
| `ENTER`    | Execute search           |
| `BACKSPACE`| Delete character         |
| `ESC`      | Cancel and return        |

### Search Results Mode
| Key     | Action                      |
|---------|-----------------------------|
| `j/k`   | Navigate results            |
| `ENTER` | Add selected to playlist    |
| `a`     | Add all results to playlist |
| `ESC`   | Return to playlist          |
| `q`     | Quit                        |

## UI Layout

```
┌─────────────────────────────────────┐
│ ▶ Now Playing: Lofi Hip Hop Radio  │  ← Header
├─────────────────────────────────────┤
│                                     │
│  Playlist (3 tracks)                │
│    ▶ Lofi Hip Hop Radio             │  ← Main Window
│      Synthwave Mix                  │    (Playlist/Search)
│      Jazz for Work                  │
│                                     │
├─────────────────────────────────────┤
│ [ MPV Output ]                      │  ← MPV Logs
│  AO: [pulse] 48000Hz stereo...      │    (5 lines)
│  A: 00:01:23 / 02:45:00             │
├─────────────────────────────────────┤
│ /: search | j/k: navigate | ...     │  ← Footer
└─────────────────────────────────────┘
```

## Architecture

```
┌─────────────────────────┐
│  ncurses TUI (4 windows)│
│  - Header (status)      │
│  - Main (playlist/search)│
│  - MPV logs             │
│  - Footer (keybinds)    │
└───────────┬─────────────┘
            │
            ├──────────────────┐
            ▼                  ▼
┌─────────────────────┐  ┌─────────────────────┐
│  MPV IPC Client     │  │  yt-dlp Search      │
│  - Socket comm      │  │  - YouTube API      │
│  - Log redirection  │  │  - Parse results    │
└─────────────────────┘  └─────────────────────┘
```

## New in Prototype 2

### YouTube Search
- Type `/` to activate search mode
- Uses `yt-dlp` to fetch top 10 results
- Shows video titles in real-time
- Add individual or bulk videos to playlist

### Dedicated MPV Section
- MPV output no longer interferes with TUI
- Redirected to `/tmp/ytui-mpv.log`
- Last 5 lines displayed in dedicated window
- Shows buffering, audio info, playback position

### Dynamic Playlist
- Start with empty playlist
- Add videos from search
- Delete with `d` key
- No hardcoded URLs needed

## Troubleshooting

**MPV connection failed:**
```bash
# Check if mpv socket exists
ls -l /tmp/mpv-socket

# Check MPV logs
tail -f /tmp/ytui-mpv.log
```

**Search not working:**
```bash
# Test yt-dlp manually
yt-dlp --flat-playlist "ytsearch10:lofi hip hop"

# Ensure yt-dlp is installed and updated
sudo pip install -U yt-dlp
```

**No results found:**
- Check internet connection
- Try simpler search terms
- Verify yt-dlp works: `yt-dlp --version`

## Next Steps (Prototype 3)

- [ ] Full playlist management (reorder, save/load)
- [ ] Real-time status updates from MPV
- [ ] Playlist persistence
- [ ] Better error handling
- [ ] Progress bar
- [ ] Volume indicator

## License

Suckless philosophy - do whatever you want with it.
