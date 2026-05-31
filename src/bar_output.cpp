#include "bar_output.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

// ── UTF-8 helpers
// ─────────────────────────────────────────────────────────────

// Split a UTF-8 string into individual codepoint byte-strings.
// e.g. "▁▂▃" (9 bytes) → {"▁","▂","▃"} (3 elements, 3 bytes each)
static std::vector<std::string> splitUtf8(const std::string &s) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = (unsigned char)s[i];
    std::size_t len = 1;
    if ((c & 0x80) == 0x00)
      len = 1;
    else if ((c & 0xE0) == 0xC0)
      len = 2;
    else if ((c & 0xF0) == 0xE0)
      len = 3;
    else if ((c & 0xF8) == 0xF0)
      len = 4;
    if (i + len <= s.size())
      out.push_back(s.substr(i, len));
    i += len;
  }
  return out;
}

// Map a single [0,1] value to a block character from the split char table.
static const std::string &mapChar(float v,
                                  const std::vector<std::string> &chars) {
  const int levels = (int)chars.size();
  const int idx = std::clamp((int)std::round(v * (levels - 1)), 0, levels - 1);
  return chars[idx];
}

// Build the bar string (just the codepoints, no colour tags) for one channel.
static std::string buildBarStr(const std::vector<float> &bars,
                               const std::vector<std::string> &chars) {
  std::string s;
  s.reserve(bars.size() * 3); // 3 bytes each for block chars
  for (float v : bars)
    s += mapChar(v, chars);
  return s;
}

// Merge L and R channels into one bar vector by averaging.
static std::vector<float> mergeStereo(const std::vector<float> &L,
                                      const std::vector<float> &R) {
  std::vector<float> out(L.size(), 0.f);
  for (std::size_t i = 0; i < L.size(); ++i)
    out[i] = (R.size() > i) ? (L[i] + R[i]) * 0.5f : L[i];
  return out;
}

// ── formatBars
// ────────────────────────────────────────────────────────────────

std::string formatBars(const std::vector<float> &bars_l,
                       const std::vector<float> &bars_r,
                       const BarOutputConfig &cfg) {
  if (bars_l.empty())
    return "";

  const std::vector<std::string> chars = splitUtf8(cfg.chars);
  if (chars.size() < 2)
    return ""; // need at least 2 levels

  const bool split = (cfg.stereo == BarStereo::Split && !bars_r.empty());

  // ── Build the visible text ────────────────────────────────────────────────
  std::string text;
  if (cfg.format == BarFormat::Raw) {
    // Raw: space-separated floats
    const auto emit = [&](const std::vector<float> &bars) {
      for (std::size_t i = 0; i < bars.size(); ++i) {
        if (i)
          text += ' ';
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.3f", (double)bars[i]);
        text += buf;
      }
    };
    if (split) {
      emit(bars_l);
      text += cfg.stereo_sep;
      emit(bars_r);
    } else {
      emit(cfg.stereo == BarStereo::Merge && !bars_r.empty()
               ? mergeStereo(bars_l, bars_r)
               : bars_l);
    }
    return text; // raw never gets colour tags
  }

  // All other formats use block chars.
  if (split) {
    text = buildBarStr(bars_l, chars);
    text += cfg.stereo_sep;
    text += buildBarStr(bars_r, chars);
  } else {
    const std::vector<float> &src =
        (cfg.stereo == BarStereo::Merge && !bars_r.empty())
            ? mergeStereo(bars_l, bars_r)
            : bars_l;
    text = buildBarStr(src, chars);
  }

  // ── Wrap in format-specific colour tags ───────────────────────────────────
  const std::string &col = cfg.color; // #RRGGBB

  switch (cfg.format) {
  case BarFormat::Plain:
  case BarFormat::Eww:
    return text;

  case BarFormat::Waybar:
    return "{\"text\":\"<span color='" + col + "'>" + text +
           "</span>\",\"class\":\"viz-active\"}";

  case BarFormat::Polybar:
    return "%{F" + col + "}" + text + "%{F-}";

  case BarFormat::Dzen2:
    return "^fg(" + col + ")" + text + "^fg()";

  case BarFormat::I3bar:
    return "{\"full_text\":\"" + text + "\",\"color\":\"" + col + "\"}";

  case BarFormat::Raw:
    break; // handled above — unreachable here
  }
  return text;
}

// ── BarWriter
// ─────────────────────────────────────────────────────────────────

bool BarWriter::open(const BarOutputConfig &cfg) {
  close();
  sink_type_ = cfg.sink;
  path_ = cfg.sink_path;

  switch (sink_type_) {
  case BarSink::Stdout:
    open_ = true;
    return true;

#ifdef __linux__
  case BarSink::Fifo:
    return openFifo();

  case BarSink::Socket:
    return openSocket();
#else
  case BarSink::Fifo:
  case BarSink::Socket:
    std::fprintf(stderr,
                 "cava-viz: FIFO and socket sinks are only supported on Linux. "
                 "Falling back to stdout.\n");
    sink_type_ = BarSink::Stdout;
    open_ = true;
    return true;
#endif
  }
  return false;
}

bool BarWriter::write(const std::string &frame) {
  const std::string line = frame + "\n";

  switch (sink_type_) {

  case BarSink::Stdout:
    if (std::fwrite(line.data(), 1, line.size(), stdout) != line.size())
      return false;
    std::fflush(stdout);
    return true;

#ifdef __linux__
  case BarSink::Fifo: {
    if (fifo_fd_ < 0)
      return false;
    const ssize_t n = ::write(fifo_fd_, line.data(), (ssize_t)line.size());
    if (n >= 0)
      return true;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return true; // reader slow, skip frame
    if (errno == EPIPE || errno == EBADF) {
      ::close(fifo_fd_);
      fifo_fd_ = -1;
      return false; // caller should call reopen()
    }
    return false;
  }

  case BarSink::Socket: {
    acceptClients();
    if (clients_.empty())
      return true; // no readers yet — not an error
    std::vector<int> alive;
    for (int fd : clients_) {
      const ssize_t n = ::write(fd, line.data(), (ssize_t)line.size());
      if (n >= 0 || errno == EAGAIN || errno == EWOULDBLOCK)
        alive.push_back(fd);
      else
        ::close(fd);
    }
    clients_ = std::move(alive);
    return true;
  }
#else
  case BarSink::Fifo:
  case BarSink::Socket:
    return false;
#endif
  }
  return false;
}

bool BarWriter::reopen() {
#ifdef __linux__
  if (sink_type_ == BarSink::Fifo) {
    if (fifo_fd_ >= 0) {
      ::close(fifo_fd_);
      fifo_fd_ = -1;
    }
    return openFifo();
  }
#endif
  return false;
}

void BarWriter::close() {
#ifdef __linux__
  if (fifo_fd_ >= 0) {
    ::close(fifo_fd_);
    fifo_fd_ = -1;
  }
  if (server_fd_ >= 0) {
    for (int fd : clients_)
      ::close(fd);
    clients_.clear();
    ::close(server_fd_);
    server_fd_ = -1;
    if (!path_.empty())
      ::unlink(path_.c_str());
  }
#endif
  open_ = false;
}

// ── Private sink helpers
// ──────────────────────────────────────────────────────

#ifdef __linux__

bool BarWriter::openFifo() {
  if (path_.empty()) {
    std::fprintf(stderr, "cava-viz: --bar-out path required for fifo sink.\n");
    return false;
  }
  // Create FIFO (ignore error if it already exists).
  if (mkfifo(path_.c_str(), 0644) < 0 && errno != EEXIST) {
    std::fprintf(stderr, "cava-viz: mkfifo(%s): %s\n", path_.c_str(),
                 std::strerror(errno));
    return false;
  }
  // Open the write end — BLOCKS until the first reader opens the read end.
  // This is intentional: the program waits for a status bar / script to
  // connect.
  std::fprintf(stderr, "cava-viz: waiting for reader on %s ...\n",
               path_.c_str());
  fifo_fd_ = ::open(path_.c_str(), O_WRONLY);
  if (fifo_fd_ < 0) {
    std::fprintf(stderr, "cava-viz: open(%s): %s\n", path_.c_str(),
                 std::strerror(errno));
    return false;
  }
  // Switch to non-blocking after open so slow readers just lose frames.
  fcntl(fifo_fd_, F_SETFL, O_NONBLOCK);
  open_ = true;
  return true;
}

bool BarWriter::openSocket() {
  if (path_.empty()) {
    std::fprintf(stderr,
                 "cava-viz: --bar-out path required for socket sink.\n");
    return false;
  }
  server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    std::fprintf(stderr, "cava-viz: socket(): %s\n", std::strerror(errno));
    return false;
  }
  // Non-blocking so accept() in write() doesn't stall the render loop.
  fcntl(server_fd_, F_SETFL, O_NONBLOCK);

  ::unlink(path_.c_str()); // remove stale socket from previous run
  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

  if (::bind(server_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    std::fprintf(stderr, "cava-viz: bind(%s): %s\n", path_.c_str(),
                 std::strerror(errno));
    ::close(server_fd_);
    server_fd_ = -1;
    return false;
  }
  ::listen(server_fd_, 16);
  std::fprintf(stderr, "cava-viz: listening on %s\n", path_.c_str());
  open_ = true;
  return true;
}

void BarWriter::acceptClients() {
  while (true) {
    const int fd = ::accept(server_fd_, nullptr, nullptr);
    if (fd < 0)
      break; // EAGAIN — no more pending connections
    fcntl(fd, F_SETFL, O_NONBLOCK);
    clients_.push_back(fd);
  }
}

#endif // __linux__
