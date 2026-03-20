#include "utils.h"

#include "config.h"
#include "globals.h"
#include "types.h"
#include "youtube.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <jpeglib.h>
#include <ncurses.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

static const std::array<const char *, 2> VIDEO_EXT = {"mkv", "mp4"};
static const size_t YT_ID_LEN = 11;

static bool is_video_ext(const std::string &ext) {
  return std::any_of(VIDEO_EXT.begin(), VIDEO_EXT.end(),
                     [&](const char *e) { return ext == e; });
}

bool file_exists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

void mkdirs() {
  mkdir(CONFIG_DIR.c_str(), 0755);
  mkdir(CACHE_DIR.c_str(), 0755);
  mkdir(VIDEO_CACHE.c_str(), 0755);
  mkdir(THUMBNAIL_CACHE.c_str(), 0755);
}

void ensure_video_cache() {
  mkdir(CACHE_DIR.c_str(), 0755);
  mkdir(VIDEO_CACHE.c_str(), 0755);
}

void set_status(const std::string &msg) {
  status_msg = msg;
  status_time = time(nullptr);
}

std::string find_cached_path_by_id(const std::string &id) {
  DIR *d = opendir(VIDEO_CACHE.c_str());
  if (!d)
    return {};
  struct dirent *ent;
  while ((ent = readdir(d)) != nullptr) {
    if (ent->d_type != DT_REG)
      continue;
    std::string name(ent->d_name);
    size_t dot = name.rfind('.');
    if (dot == std::string::npos)
      continue;
    if (!is_video_ext(name.substr(dot + 1)))
      continue;
    std::string base = name.substr(0, dot);
    if (base.size() >= id.size() &&
        base.compare(base.size() - id.size(), id.size(), id) == 0) {
      closedir(d);
      return VIDEO_CACHE + '/' + name;
    }
  }
  closedir(d);
  return {};
}

std::vector<Video> scan_video_cache() {
  std::vector<Video> out;
  DIR *d = opendir(VIDEO_CACHE.c_str());
  if (!d)
    return out;
  struct dirent *ent;
  while ((ent = readdir(d)) != nullptr) {
    if (ent->d_type != DT_REG)
      continue;
    std::string name(ent->d_name);
    size_t dot = name.rfind('.');
    if (dot == std::string::npos)
      continue;
    if (!is_video_ext(name.substr(dot + 1)))
      continue;
    std::string base = name.substr(0, dot);
    Video v;
    v.path = VIDEO_CACHE + '/' + name;
    if (base.size() >= YT_ID_LEN) {
      v.id = base.substr(base.size() - YT_ID_LEN);
      v.title = base.substr(0, base.size() - YT_ID_LEN);
    } else {
      v.id = v.title = base;
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
  for (const auto &dl : downloads) {
    bool hit = std::any_of(cached.begin(), cached.end(),
                           [&](const Video &c) { return c.id == dl.v.id; });
    if (hit)
      continue;
    Video v = dl.v;
    v.path = VIDEO_CACHE + '/' + v.id + ".mkv";
    out.push_back(std::move(v));
  }
  return out;
}

std::vector<Video> collect_download_items() {
  auto c = scan_video_cache();
  return collect_download_items(c);
}

void update_download_statuses(const std::vector<Video> &cached) {
  for (auto &dl : downloads) {
    bool found = std::any_of(cached.begin(), cached.end(),
                             [&](const Video &c) { return c.id == dl.v.id; });
    dl.done = found;
    if (found)
      dl.pid = 0;
  }
}

std::string esc(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\n')
      out += "\\n";
    else if (c == '|')
      out += "\\p";
    else
      out += c;
  }
  return out;
}

std::string unesc(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      if (n == 'n')
        out += '\n';
      else if (n == 'p')
        out += '|';
      else
        out += n;
    } else {
      out += s[i];
    }
  }
  return out;
}

void load_search_hist() {
  std::ifstream f(SEARCH_HISTORY_FILE);
  std::string line;
  while (std::getline(f, line))
    if (!line.empty())
      search_hist.push_back(line);
}

void save_search_hist() {
  std::ofstream f(SEARCH_HISTORY_FILE);
  for (const auto &s : search_hist)
    f << s << '\n';
}

void add_search_hist(const std::string &s) {
  auto it = std::find(search_hist.begin(), search_hist.end(), s);
  if (it != search_hist.end())
    search_hist.erase(it);
  search_hist.insert(search_hist.begin(), s);
  if (search_hist.size() > 50)
    search_hist.resize(50);
  save_search_hist();
}

void load_history() {
  std::ifstream f(HISTORY_FILE);
  std::string line;
  while (std::getline(f, line)) {
    size_t sep = line.find("|||");
    if (sep == std::string::npos)
      continue;
    Video v;
    v.id = line.substr(0, sep);
    v.title = unesc(line.substr(sep + 3));
    v.path = VIDEO_CACHE + '/' + v.id + ".mkv";
    history.push_back(v);
  }
}

void save_history() {
  std::ofstream f(HISTORY_FILE);
  for (const auto &v : history)
    f << v.id << "|||" << esc(v.title) << '\n';
}

void load_subs() {
  subs.clear();
  std::ifstream f(SUBS_FILE);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    Channel ch;
    size_t sep = line.find('|');
    if (sep != std::string::npos) {
      ch.name = line.substr(0, sep);
      ch.url = line.substr(sep + 1);
      while (!ch.name.empty() && isspace((unsigned char)ch.name.back()))
        ch.name.pop_back();
      while (!ch.url.empty() && isspace((unsigned char)ch.url.front()))
        ch.url.erase(ch.url.begin());
      while (!ch.url.empty() && isspace((unsigned char)ch.url.back()))
        ch.url.pop_back();
    } else {
      ch.name = ch.url = line;
    }
    subs.push_back(ch);
  }
}

void save_subs() {
  std::ofstream f(SUBS_FILE);
  for (const auto &ch : subs)
    f << ch.name << '|' << ch.url << '\n';
}

void toggle_subscription(const Video &v) {
  if (v.channel_url.empty()) {
    set_status("No channel URL available");
    return;
  }

  auto it = std::find_if(subs.begin(), subs.end(), [&](const Channel &ch) {
    return ch.url == v.channel_url;
  });

  if (it != subs.end()) {
    subs.erase(it);
    set_status("Unsubscribed from: " + v.channel_name);
  } else {
    Channel ch;
    ch.name = v.channel_name.empty() ? v.channel_url : v.channel_name;
    ch.url = v.channel_url;
    subs.push_back(ch);
    set_status("Subscribed to: " + ch.name);
  }

  save_subs();
}

void play(const Video &v) {
  std::string local = find_cached_path_by_id(v.id);
  std::string path =
      local.empty() ? "https://www.youtube.com/watch?v=" + v.id : local;
  std::string cmd = "setsid mpv ";
  cmd += MPV_ARGS;
  cmd += " '" + path + "' </dev/null >/dev/null 2>&1 &";
  system(cmd.c_str());
  thumbnail_resume_time = time(nullptr) + 8;
  hide_thumbnail();
  auto it = std::find(history.begin(), history.end(), v);
  if (it != history.end()) {
    Video tmp = *it;
    history.erase(it);
    history.insert(history.begin(), tmp);
  } else {
    history.insert(history.begin(), v);
  }
  save_history();
  set_status("Playing: " + v.title);
}

int enqueue_download(const Video &v) {
  ensure_video_cache();
  Download dl;
  dl.v = v;
  dl.pid = download(v);
  dl.done = false;
  dl.v.path = VIDEO_CACHE + '/' + v.id + ".mkv";
  downloads.insert(downloads.begin(), dl);
  set_status("Downloading: " + v.title);
  return dl.pid;
}

size_t visible_count(size_t rows, size_t items) {
  return rows == 0 ? 0 : std::min(rows, items);
}

/* ==========================================================================
 * Kitty thumbnail renderer
 * ========================================================================== */

static int g_cell_w = 0, g_cell_h = 0;

static void query_cell_size() {
  g_cell_w = 10;
  g_cell_h = 20;
  int fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (fd < 0)
    return;
  if (write(fd, "\033[16t", 5) != 5) {
    close(fd);
    return;
  }
  struct timeval tv = {0, 300000};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) {
    close(fd);
    return;
  }
  char buf[64] = {};
  if (read(fd, buf, sizeof(buf) - 1) <= 0) {
    close(fd);
    return;
  }
  close(fd);
  int ph = 0, pw = 0;
  if (sscanf(buf, "\033[6;%d;%dt", &ph, &pw) == 2 && pw > 0 && ph > 0) {
    g_cell_w = pw;
    g_cell_h = ph;
  }
}

static void ensure_cell_size() {
  if (g_cell_w == 0)
    query_cell_size();
}

static void thumb_geometry(int *col, int *row, int *cols, int *rows, int *px_w,
                           int *px_h) {
  int H, W;
  getmaxyx(stdscr, H, W);
  int c = std::max(8, W * 35 / 100);
  int r = std::max(4, H - 5);
  if (col)
    *col = W - c;
  if (row)
    *row = 3;
  if (cols)
    *cols = c;
  if (rows)
    *rows = r;
  if (px_w || px_h) {
    ensure_cell_size();
    if (px_w)
      *px_w = c * g_cell_w;
    if (px_h)
      *px_h = r * g_cell_h;
  }
}

static void jpeg_noop_error(j_common_ptr) {}
static void jpeg_noop_msg(j_common_ptr, int) {}

static bool jpeg_to_rgba(const std::string &path, std::vector<uint8_t> &out,
                         unsigned &w, unsigned &h) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return false;
  struct jpeg_decompress_struct cinfo = {};
  struct jpeg_error_mgr jerr = {};
  cinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = jpeg_noop_error;
  jerr.emit_message = jpeg_noop_msg;
  jerr.output_message = jpeg_noop_error;
  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, f);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return false;
  }
  cinfo.out_color_space = JCS_EXT_RGBA;
  jpeg_start_decompress(&cinfo);
  w = cinfo.output_width;
  h = cinfo.output_height;
  int comp = cinfo.output_components;
  std::vector<uint8_t> row_buf(w * comp);
  out.resize(w * h * 4);
  uint8_t *dst = out.data();
  while (cinfo.output_scanline < h) {
    uint8_t *rp = row_buf.data();
    jpeg_read_scanlines(&cinfo, &rp, 1);
    if (comp == 4) {
      memcpy(dst, row_buf.data(), w * 4);
    } else {
      for (unsigned x = 0; x < w; ++x) {
        dst[x * 4 + 0] = row_buf[x * 3 + 0];
        dst[x * 4 + 1] = row_buf[x * 3 + 1];
        dst[x * 4 + 2] = row_buf[x * 3 + 2];
        dst[x * 4 + 3] = 255;
      }
    }
    dst += w * 4;
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  fclose(f);
  return true;
}

static void rgba_scale(const uint8_t *src, unsigned sw, unsigned sh,
                       std::vector<uint8_t> &dst, unsigned dw, unsigned dh) {
  dst.resize((size_t)dw * dh * 4);
  float sx = (float)sw / dw, sy = (float)sh / dh;
  for (unsigned y = 0; y < dh; ++y) {
    float fy = (y + 0.5f) * sy - 0.5f;
    int y0 = std::max(0, (int)fy), y1 = std::min((int)sh - 1, y0 + 1);
    float vy = fy - y0;
    for (unsigned x = 0; x < dw; ++x) {
      float fx = (x + 0.5f) * sx - 0.5f;
      int x0 = std::max(0, (int)fx), x1 = std::min((int)sw - 1, x0 + 1);
      float vx = fx - x0;
      const uint8_t *p00 = src + ((size_t)y0 * sw + x0) * 4,
                    *p10 = src + ((size_t)y0 * sw + x1) * 4;
      const uint8_t *p01 = src + ((size_t)y1 * sw + x0) * 4,
                    *p11 = src + ((size_t)y1 * sw + x1) * 4;
      uint8_t *d = dst.data() + ((size_t)y * dw + x) * 4;
      for (int c = 0; c < 4; ++c)
        d[c] = (uint8_t)(p00[c] * (1 - vx) * (1 - vy) + p10[c] * vx * (1 - vy) +
                         p01[c] * (1 - vx) * vy + p11[c] * vx * vy + 0.5f);
    }
  }
}

static void fit_dims(unsigned sw, unsigned sh, unsigned mw, unsigned mh,
                     unsigned &ow, unsigned &oh) {
  if (!sw || !sh) {
    ow = mw;
    oh = mh;
    return;
  }
  float s = std::min((float)mw / sw, (float)mh / sh);
  ow = std::max(1u, (unsigned)(sw * s));
  oh = std::max(1u, (unsigned)(sh * s));
}

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    unsigned b = (unsigned)data[i] << 16;
    if (i + 1 < len)
      b |= (unsigned)data[i + 1] << 8;
    if (i + 2 < len)
      b |= (unsigned)data[i + 2];
    out += B64[(b >> 18) & 63];
    out += B64[(b >> 12) & 63];
    out += (i + 1 < len) ? B64[(b >> 6) & 63] : '=';
    out += (i + 2 < len) ? B64[b & 63] : '=';
  }
  return out;
}

static int g_tty_fd = -1;

static int tty_fd() {
  if (g_tty_fd < 0) {
    g_tty_fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (g_tty_fd < 0)
      g_tty_fd = fileno(stdout);
  }
  return g_tty_fd;
}

static void tty_write(const char *buf, size_t len) {
  int fd = tty_fd();
  fflush(stdout);
  while (len > 0) {
    ssize_t n = write(fd, buf, len);
    if (n <= 0)
      break;
    buf += n;
    len -= (size_t)n;
  }
}
static void tty_write(const std::string &s) { tty_write(s.data(), s.size()); }

static const unsigned KITTY_ID = 1;
static const unsigned KITTY_PID = 1;
static const size_t KITTY_CHUNK = 4096;

static void kitty_upload(const std::vector<uint8_t> &rgba, unsigned w,
                         unsigned h) {
  if (rgba.empty() || !w || !h)
    return;
  std::string enc = b64_encode(rgba.data(), rgba.size());
  std::string out;
  out += "\033_Ga=d,d=I,i=" + std::to_string(KITTY_ID) + ",q=2;\033\\";
  size_t sent = 0;
  bool first = true;
  while (sent < enc.size()) {
    size_t n = std::min(KITTY_CHUNK, enc.size() - sent);
    int more = (sent + n < enc.size()) ? 1 : 0;
    if (first) {
      out += "\033_Ga=t,f=32,i=" + std::to_string(KITTY_ID) +
             ",s=" + std::to_string(w) + ",v=" + std::to_string(h) +
             ",q=2,m=" + std::to_string(more) + ";" + enc.substr(sent, n) +
             "\033\\";
      first = false;
    } else {
      out += "\033_Gm=" + std::to_string(more) + ";" + enc.substr(sent, n) +
             "\033\\";
    }
    sent += n;
  }
  tty_write(out);
}

static void kitty_place(int col, int row, unsigned w, unsigned h) {
  std::string out;
  out += "\0337";
  out +=
      "\033[" + std::to_string(row + 1) + ";" + std::to_string(col + 1) + "H";
  out += "\033_Ga=p,i=" + std::to_string(KITTY_ID) +
         ",p=" + std::to_string(KITTY_PID) + ",s=" + std::to_string(w) +
         ",v=" + std::to_string(h) + ",q=2;\033\\";
  out += "\0338";
  tty_write(out);
}

static void kitty_delete() {
  tty_write("\033_Ga=d,d=I,i=" + std::to_string(KITTY_ID) + ",q=2;\033\\");
}

static const char *THUMB_QUALITIES[] = {"maxresdefault", "hqdefault",
                                        "mqdefault", nullptr};

static std::string fetch_best_thumbnail(const Video &v) {
  mkdir(THUMBNAIL_CACHE.c_str(), 0755);
  std::string dest = THUMBNAIL_CACHE + '/' + v.id + ".jpg";
  if (file_exists(dest))
    return dest;
  for (int i = 0; THUMB_QUALITIES[i]; ++i) {
    std::string url = "https://img.youtube.com/vi/" + v.id + '/' +
                      THUMB_QUALITIES[i] + ".jpg";
    pid_t pid = fork();
    if (pid < 0)
      continue;
    if (pid == 0) {
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
      }
      execlp("curl", "curl", "-sS", "-L", "-f", "-o", dest.c_str(), url.c_str(),
             (char *)nullptr);
      _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
      unlink(dest.c_str());
      continue;
    }
    if (!WIFEXITED(status)) {
      unlink(dest.c_str());
      continue;
    }
    if (WEXITSTATUS(status) == 127)
      return {};
    if (WEXITSTATUS(status) != 0) {
      unlink(dest.c_str());
      continue;
    }
    struct stat st = {};
    if (stat(dest.c_str(), &st) == 0 && st.st_size > 1024)
      return dest;
    unlink(dest.c_str());
  }
  return {};
}

static std::string g_thumb_path;
static std::vector<uint8_t> g_thumb_rgba;
static unsigned g_thumb_w = 0, g_thumb_h = 0;
static std::vector<uint8_t> g_scaled_rgba;
static unsigned g_scaled_w = 0, g_scaled_h = 0;
static unsigned g_kitty_w = 0, g_kitty_h = 0;
static bool g_needs_upload = false;

static void reset_thumb() {
  g_thumb_path.clear();
  g_thumb_rgba.clear();
  g_thumb_w = g_thumb_h = g_scaled_w = g_scaled_h = g_kitty_w = g_kitty_h = 0;
  g_scaled_rgba.clear();
  g_needs_upload = false;
}

void show_thumbnail(const Video &v) {
  if (thumbnail_resume_time > 0) {
    if (time(nullptr) < thumbnail_resume_time)
      return;
    thumbnail_resume_time = 0;
  }
  if (v.id.empty())
    return;
  std::string path = fetch_best_thumbnail(v);
  if (path.empty())
    return;
  if (path != g_thumb_path) {
    std::vector<uint8_t> rgba;
    unsigned w = 0, h = 0;
    if (!jpeg_to_rgba(path, rgba, w, h))
      return;
    reset_thumb();
    g_thumb_path = path;
    g_thumb_rgba = std::move(rgba);
    g_thumb_w = w;
    g_thumb_h = h;
    g_needs_upload = true;
  }
  thumbnail_shown = true;
}

void hide_thumbnail() {
  if (!thumbnail_shown)
    return;
  kitty_delete();
  reset_thumb();
  thumbnail_shown = false;
}

void redraw_thumbnail() {
  if (!thumbnail_shown || g_thumb_rgba.empty())
    return;
  if (thumbnail_resume_time > 0 && time(nullptr) < thumbnail_resume_time)
    return;

  int col, row, px_w, px_h;
  thumb_geometry(&col, &row, nullptr, nullptr, &px_w, &px_h);

  unsigned tw = 0, th = 0;
  fit_dims(g_thumb_w, g_thumb_h, (unsigned)px_w, (unsigned)px_h, tw, th);

  if (tw != g_scaled_w || th != g_scaled_h || g_scaled_rgba.empty()) {
    rgba_scale(g_thumb_rgba.data(), g_thumb_w, g_thumb_h, g_scaled_rgba, tw,
               th);
    g_scaled_w = tw;
    g_scaled_h = th;
    g_needs_upload = true;
  }
  if (g_needs_upload || g_kitty_w != g_scaled_w || g_kitty_h != g_scaled_h) {
    kitty_upload(g_scaled_rgba, g_scaled_w, g_scaled_h);
    g_kitty_w = g_scaled_w;
    g_kitty_h = g_scaled_h;
    g_needs_upload = false;
  }
  kitty_place(col, row, g_scaled_w, g_scaled_h);
}

void preload_thumbnails(const std::vector<Video> &list, size_t start) {
  size_t end = std::min(list.size(), start + 5);
  if (start >= end)
    return;
  std::vector<Video> targets;
  for (size_t i = start; i < end; ++i)
    if (!list[i].id.empty())
      targets.push_back(list[i]);
  if (targets.empty())
    return;
  std::thread([targets = std::move(targets)]() {
    for (const auto &v : targets)
      fetch_best_thumbnail(v);
  }).detach();
}
