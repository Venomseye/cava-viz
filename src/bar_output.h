#pragma once
#include <string>
#include <vector>

// ── Output format
// ─────────────────────────────────────────────────────────────

enum class BarFormat {
  Plain, // ▁▃▇█▅▂           (plain UTF-8, one line)
  Waybar, // {"text":"<span color='#hex'>▁▃▇█</span>","class":"viz-active"}
  Polybar, // %{F#hex}▁▃▇█%{F-}
  Eww,     // ▁▃▇█             (same as plain; eww styles via CSS)
  Raw,     // 0.05 0.74 0.98   (space-separated floats for custom consumers)
  Dzen2,   // ^fg(#hex)▁▃▇█^fg()
  I3bar,   // {"full_text":"▁▃▇█","color":"#hex"}
};

// ── Output sink
// ───────────────────────────────────────────────────────────────

enum class BarSink {
  Stdout, // write to stdout (default)
  Fifo,   // write to a named pipe (FIFO) at sink_path
  Socket, // Unix domain socket server at sink_path; broadcasts to all clients
};

// ── Stereo handling
// ───────────────────────────────────────────────────────────

enum class BarStereo {
  Merge, // average L+R into one bar string (default)
  Split, // emit L_bars <stereo_sep> R_bars
};

// ── Configuration
// ─────────────────────────────────────────────────────────────

struct BarOutputConfig {
  BarFormat format{BarFormat::Plain};
  BarSink sink{BarSink::Stdout};
  std::string sink_path{}; // path for FIFO or socket

  int count{10};                 // bars per channel
  std::string chars{"▁▂▃▄▅▆▇█"}; // UTF-8 block characters (any count ≥ 2)
  std::string color{"#00ffcc"};  // accent colour (#RRGGBB) for tagged formats

  int fps{15}; // output frame rate

  BarStereo stereo{BarStereo::Merge};
  std::string stereo_sep{" | "}; // separator when stereo == Split
};

// ── Formatting
// ────────────────────────────────────────────────────────────────

/// Format normalised bar vectors into the target output string.
///   bars_l  — left channel (or mono), values in [0, 1], length == cfg.count
///   bars_r  — right channel; ignored when cfg.stereo == Merge or empty
///
/// The returned string does NOT include a trailing newline.
std::string formatBars(const std::vector<float> &bars_l,
                       const std::vector<float> &bars_r,
                       const BarOutputConfig &cfg);

// ── Sink writer
// ───────────────────────────────────────────────────────────────

/// Manages the lifecycle of one output sink.
/// Open → write frames in a loop → close.
///
/// FIFO behaviour:
///   open() blocks until the first reader connects, then returns.
///   write() is non-blocking; a full pipe (slow reader) skips the frame.
///   On EPIPE (reader disconnected), write() returns false; call reopen().
///
/// Socket behaviour:
///   open() binds and listens immediately (non-blocking).
///   write() accepts any pending clients, then broadcasts to all.
///   Dead clients are pruned automatically on each write.
class BarWriter {
public:
  BarWriter() = default;
  ~BarWriter() { close(); }

  BarWriter(const BarWriter &) = delete;
  BarWriter &operator=(const BarWriter &) = delete;

  /// Open the sink. Returns false on unrecoverable error.
  bool open(const BarOutputConfig &cfg);

  /// Reopen after a reader disconnect (FIFO only).
  /// Blocks until a new reader connects.
  bool reopen();

  /// Write one formatted frame (without a trailing newline — added internally).
  /// Returns false when the sink is permanently broken (use reopen() for FIFO).
  bool write(const std::string &frame);

  /// Close the sink and release all resources.
  void close();

  bool isOpen() const { return open_; }
  BarSink sinkType() const { return sink_type_; }

private:
  BarSink sink_type_{BarSink::Stdout};
  std::string path_;
  bool open_{false};

  // FIFO
  int fifo_fd_{-1};

  // Unix socket
  int server_fd_{-1};
  std::vector<int> clients_;

  bool openFifo();      // create + open write end (blocks for reader)
  bool openSocket();    // create + bind + listen
  void acceptClients(); // accept all pending connections (non-blocking)
};
