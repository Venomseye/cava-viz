#include "audio_capture.h"
#include "audio_utils.h"
#include "config.h"
#include "fft_processor.h"
#include "renderer.h"
#include "user_theme.h"

// Defined by CMakeLists.txt via target_compile_definitions; fall back to
// "unknown" when the binary is built without CMake (e.g. a plain Makefile).
#ifndef VIZ_VERSION
#define VIZ_VERSION "unknown"
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits> // NAME_MAX (Debian/Ubuntu don't expose it via inotify.h)
#include <cmath>   // std::sqrt, std::fmod
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <memory>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_resize{false};
static std::atomic<bool> g_reload{false}; // set by SIGUSR1 for live reload
static void sig_handler(int s) {
  // SIGHUP is what the kernel sends when the controlling terminal goes away
  // (window closed, SSH dropped).  It must terminate us, never reload.
  if (s == SIGINT || s == SIGTERM || s == SIGHUP)
    g_running.store(false);
  else if (s == SIGWINCH)
    g_resize.store(true);
  else if (s == SIGUSR1)
    g_reload.store(true);
}

// ── print_usage
// ───────────────────────────────────────────────────────────────
static void print_usage(const char *p) {
  printf("Usage: %s [OPTIONS]\n\n"
         "Terminal audio visualizer (CAVA algorithm)\n\n"
         "Options:\n"
         "  -b <pulse|pipewire|auto>   Backend (default: auto)\n"
         "  -s <source>                Explicit source device\n"
         "  -M                         Use microphone input\n"
         "  -r <Hz>                    Sample rate (default: 44100)\n"
         "  -t <index>                 Theme index (0-11 built-in, 12+ user)\n"
         "  -f <n>                     Target FPS (default: 60)\n"
         "  -w                         Auto bar width\n"
         "  --list-sources             List available audio sources and exit\n"
         "  --check                    Validate config and exit\n"
         "  -V                         Show version and exit\n"
         "  -h                         Show help\n\n"
         "Keys:\n"
         "  q          Quit\n"
         "  t          Next theme\n"
         "  g          Cycle gap (0-2)\n"
         "  ] / [      Increase / decrease bar width\n"
         "  UP / DOWN  Manual sensitivity\n"
         "  a          Toggle auto-sensitivity\n"
         "  s          Toggle stereo / mono\n"
         "  h          Toggle HUD pin\n"
         "  c          Toggle colour cycle\n"
         "  v          Toggle per-bar colour\n"
         "  w          Toggle A-weighting\n"
         "  n          Toggle auto-mono\n\n"
         "Live reload:\n"
         "  pkill -USR1 -x viz         Reload config + themes without "
         "restarting\n\n"
         "Config: %s\n"
         "  Edit while running — inotify reloads changes instantly.\n\n"
         "Themes: Fire Plasma Neon Teal Sunset Candy Aurora Inferno "
         "White Rose Mermaid Vapor\n",
         p, Config::configPath().c_str());
}

// ── --check
// ───────────────────────────────────────────────────────────────────
static int doCheck(const Config &cfg) {
  const auto themes = loadUserThemes();
  const std::string mon = detectMonitor();
  bool ok = true;

  printf("viz %s — config check\n", VIZ_VERSION);
  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

  printf("Paths:\n");
  printf("  config  = %s\n", Config::configPath().c_str());
  printf("  state   = %s\n", Config::statePath().c_str());
  printf("  themes  = %s  (%zu loaded)\n\n", themesDir().c_str(),
         themes.size());

  printf("Visual:\n");
  const int total_themes =
      static_cast<int>(Theme::COUNT) + static_cast<int>(themes.size());
  if (cfg.theme < 0 || cfg.theme >= total_themes) {
    printf("  theme       = %d  ✗ out of range (0-%d)\n", cfg.theme,
           total_themes - 1);
    ok = false;
  } else if (cfg.theme < static_cast<int>(Theme::COUNT)) {
    printf("  theme       = %d  (%s)\n", cfg.theme,
           builtinThemeName(cfg.theme));
  } else {
    const int ui = cfg.theme - static_cast<int>(Theme::COUNT);
    printf("  theme       = %d  (user: %s)\n", cfg.theme,
           themes[static_cast<std::size_t>(ui)].name.c_str());
  }
  printf("  bar_width   = %d\n", cfg.bar_width);
  printf("  gap_width   = %d\n", cfg.gap_width);
  printf("  hud_pinned  = %s\n\n", cfg.hud_pinned ? "yes" : "no");

  printf("FFT / Smoothing:\n");
  printf("  gravity     = %.2f\n", static_cast<double>(cfg.gravity));
  printf("  monstercat  = %.2f\n", static_cast<double>(cfg.monstercat));
  printf("  rise_factor = %.2f\n", static_cast<double>(cfg.rise_factor));
  printf("  bass_smooth = %.2f\n", static_cast<double>(cfg.bass_smooth));
  printf("  noise_gate  = %.3f\n", static_cast<double>(cfg.noise_gate));
  printf("  sensitivity = %.2f   auto=%s\n\n",
         static_cast<double>(cfg.sensitivity), cfg.auto_sens ? "yes" : "no");

  printf("Audio:\n");
  printf("  stereo      = %s\n", cfg.stereo ? "yes" : "no");
  printf("  high_cutoff = %d Hz\n", cfg.high_cutoff);
  printf("  a_weighting = %s\n", cfg.a_weighting ? "yes" : "no");
  printf("  auto_mono   = %s\n", cfg.auto_mono ? "yes" : "no");
  if (mon.empty()) {
    printf("  monitor     = (not found — is PipeWire/PulseAudio running?)\n");
    ok = false;
  } else {
    printf("  monitor     = %s\n", mon.c_str());
  }
  if (!cfg.last_source.empty())
    printf("  last_source = %s\n", cfg.last_source.c_str());
  printf("\n");

  if (!themes.empty()) {
    printf("User themes:\n");
    for (const auto &t : themes)
      printf("  %s\n", t.name.c_str());
    printf("\n");
  }

  printf("Performance:\n");
  printf("  fps         = %d\n\n", cfg.fps);

  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  printf("%s\n",
         ok ? "All checks passed." : "One or more checks failed — see above.");
  return ok ? 0 : 1;
}

// ── Apply rendering config
// ────────────────────────────────────────────────────

// Safe integer CLI argument parser — prints a helpful error and exits cleanly
// instead of letting std::stoi throw an unhandled exception.
static int argInt(const char *flag, const char *s) {
  try {
    std::size_t pos = 0;
    const int v = std::stoi(s, &pos);
    if (pos != std::strlen(s))
      throw std::invalid_argument("trailing chars");
    return v;
  } catch (...) {
    std::fprintf(stderr, "viz: '%s' expects an integer, got '%s'\n", flag,
                 s);
    std::exit(1);
  }
}

static void applyRendererConfig(Renderer &r, const Config &cfg,
                                bool force_auto_width) {
  r.setThemeIdx(cfg.theme); // handles both built-in and user theme indices
  r.setGapWidth(cfg.gap_width);
  if (!force_auto_width)
    r.setBarWidth(cfg.bar_width);
  r.setHudPinned(cfg.hud_pinned);
  r.setColourCycle(cfg.colour_cycle);
  r.setPerBarColour(cfg.per_bar_colour);
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
  struct sigaction sa {};
  sa.sa_handler = sig_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGWINCH, &sa, nullptr);
  sigaction(SIGHUP, &sa, nullptr);  // terminal closed -> clean shutdown
  sigaction(SIGUSR1, &sa, nullptr); // live reload: pkill -USR1 -x viz

  Config cfg;
  cfg.load();
  cfg.loadState();

  std::string backend = "auto";
  std::string cli_source;
  int sample_rate = 44100;
  bool use_mic = false;
  bool force_auto_width = false;

  // ── Argument parsing ──────────────────────────────────────────────────────
  enum {
    OPT_LIST_SOURCES = 1000,
    OPT_CHECK,
  };

  static const struct option long_opts[] = {
      {"backend", required_argument, nullptr, 'b'},
      {"source", required_argument, nullptr, 's'},
      {"mic", no_argument, nullptr, 'M'},
      {"rate", required_argument, nullptr, 'r'},
      {"theme", required_argument, nullptr, 't'},
      {"fps", required_argument, nullptr, 'f'},
      {"autowidth", no_argument, nullptr, 'w'},
      {"version", no_argument, nullptr, 'V'},
      {"help", no_argument, nullptr, 'h'},
      {"list-sources", no_argument, nullptr, OPT_LIST_SOURCES},
      {"check", no_argument, nullptr, OPT_CHECK},
      {nullptr, 0, nullptr, 0}};
  int o;
  while ((o = getopt_long(argc, argv, "b:s:Mr:t:f:wVh", long_opts, nullptr)) !=
         -1) {
    switch (o) {
    case 'b':
      backend = optarg;
      if (backend != "auto" && backend != "pipewire" && backend != "pulse") {
        std::fprintf(stderr,
                     "viz: unknown backend '%s' (use auto, pipewire or "
                     "pulse)\n",
                     optarg);
        return 1;
      }
      break;
    case 's':
      cli_source = optarg;
      break;
    case 'M':
      use_mic = true;
      break;
    case 'r':
      sample_rate = argInt("-r", optarg);
      if (sample_rate < 8000 || sample_rate > 192000) {
        std::fprintf(stderr,
                     "viz: -r must be between 8000 and 192000 Hz, got "
                     "%d\n",
                     sample_rate);
        return 1;
      }
      break;
    case 't': {
      const int ti = argInt("-t", optarg);
      if (ti >= 0) {
        cfg.theme = ti;
        cfg.cli_theme = true; // session-only: never persisted
      }
    } break;
    case 'f':
      cfg.fps = std::max(1, argInt("-f", optarg));
      cfg.cli_fps = true; // session-only: never persisted
      break;
    case 'w':
      force_auto_width = true;
      break;
    case 'V':
      printf("viz %s\n", VIZ_VERSION);
      return 0;
    case 'h':
      print_usage(argv[0]);
      return 0;
    case OPT_LIST_SOURCES:
      cfg.loadState();
      printSources(cfg);
      return 0;
    case OPT_CHECK:
      cfg.loadState();
      return doCheck(cfg);
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  // ── FFT processor (stack-allocated; never moves) ──────────────────────────
  // The audio callback captures fft by [&]. Stack allocation guarantees the
  // address is stable for the entire duration of main().
  int channels = cfg.stereo ? 2 : 1;
  FFTProcessor fft(sample_rate, channels);
  applyFFTConfig(fft, cfg);

  // ── Audio source ──────────────────────────────────────────────────────────
  std::string active_source;
  std::string bname;
  std::unique_ptr<AudioCapture> audio;

  // Thin wrapper so call sites don't repeat the full parameter list.
  auto startAudio = [&]() {
    doStartAudio(backend, cli_source, use_mic, sample_rate, channels, fft, cfg,
                 audio, active_source, bname);
  };

  // ── Capture liveness / reconnect state ────────────────────────────────────
  // heard_audio : true once a non-silent frame arrived on the CURRENT
  //               connection.  Silence only triggers a reconnect while this
  //               is still false (wrong/dead source); a track that merely
  //               pauses must never tear the stream down.
  // tracked_mon : default-monitor name seen when we last (re)connected;
  //               compared against detectMonitor() to spot a default-sink
  //               change.  Kept separate from active_source so a stale
  //               source name can't cause a reconnect every watchdog tick.
  static constexpr int SILENCE_BASE_SEC = 5;
  static constexpr int SILENCE_MAX_SEC = 60;
  static constexpr int RECONNECT_BACKOFF_BASE_SEC = 2;
  static constexpr int RECONNECT_BACKOFF_MAX_SEC = 10;
  const bool pinned = use_mic || !cli_source.empty();
  const std::string pinned_src =
      use_mic ? std::string(AudioCapture::MIC_SOURCE) : cli_source;
  long next_attempt_frame = 0; // earliest frame for the next retry when down
  int fail_backoff_secs = RECONNECT_BACKOFF_BASE_SEC;
  bool heard_audio = false;
  int silent_frames = 0;
  int silent_retry_secs = SILENCE_BASE_SEC;
  std::string tracked_mon;

  auto resetLiveness = [&]() {
    heard_audio = false;
    silent_frames = 0;
  };
  auto syncTrackedMonitor = [&]() { tracked_mon = detectMonitor(); };
  // Stop capture, rebuild the FFT for the current cfg.stereo, restart.
  // The single place that does this (was copy-pasted 3x).
  auto restartCapture = [&]() {
    if (audio) {
      audio->stop();
      audio.reset();
    }
    channels = cfg.stereo ? 2 : 1;
    fft.reinit(channels);
    applyFFTConfig(fft, cfg);
    resetLiveness();
    startAudio();
    syncTrackedMonitor();
  };

  startAudio();
  syncTrackedMonitor();
  if (!audio) {
#if !defined(HAVE_PULSEAUDIO) && !defined(HAVE_PIPEWIRE)
    fprintf(stderr,
            "viz: no audio backend compiled in.\n"
            "  Install libpulse-dev (PulseAudio) or libpipewire-0.3-dev "
            "(PipeWire)\n"
            "  then rebuild: cd build && cmake .. && cmake --build .\n");
#else
    fprintf(stderr, "viz: no audio backend started.\n");
#endif
    return 1;
  }

  // ── Renderer ──────────────────────────────────────────────────────────────
  Renderer renderer;
  if (!renderer.init()) {
    audio->stop();
    std::fprintf(stderr,
                 "viz: could not initialise the terminal UI.\n"
                 "  Run it in an interactive terminal (stdout must be a tty) "
                 "and make sure TERM is set (e.g. xterm-256color).\n");
    return 1;
  }
  // Load user themes before applyRendererConfig so cfg.theme (which may be
  // a user theme index >= Theme::COUNT) validates and clamps correctly.
  renderer.setUserThemes(loadUserThemes());
  applyRendererConfig(renderer, cfg, force_auto_width);
  if (!renderer.utf8Ok())
    renderer.showFeedback("Locale is not UTF-8 - run with LANG=C.UTF-8");
  if (force_auto_width)
    renderer.setBarWidth(renderer.autoBarWidth());
  renderer.setSourceName(sourceLabel(active_source));
  renderer.notifyChange();

  // ── inotify ───────────────────────────────────────────────────────────────
  const std::string cfg_path = Config::configPath();
  const std::string cfg_dir = cfg_path.substr(0, cfg_path.rfind('/'));
  const std::string cfg_file = cfg_path.substr(cfg_path.rfind('/') + 1);
  const std::string themes_path = themesDir();
  int inotify_fd = -1;
  int wd_cfg = -1;
  int wd_themes = -1;
#ifdef __linux__
  mkdir(cfg_dir.c_str(), 0755);
  mkdir(themes_path.c_str(), 0755); // create themes/ if it doesn't exist
  inotify_fd = inotify_init1(IN_NONBLOCK);
  if (inotify_fd >= 0) {
    wd_cfg = inotify_add_watch(inotify_fd, cfg_dir.c_str(),
                               IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wd_cfg < 0) {
      close(inotify_fd);
      inotify_fd = -1;
    }
    // Watch themes/ dir too — adding, editing, or removing a .theme file
    // triggers an immediate reload without restarting the visualizer.
    // IN_DELETE covers rm; IN_CLOSE_WRITE covers saves; IN_MOVED_TO covers
    // atomic-rename saves (how most editors write files).
    if (inotify_fd >= 0)
      wd_themes = inotify_add_watch(inotify_fd, themes_path.c_str(),
                                    IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);
    // A missing or unreadable themes/ dir is non-fatal; wd_themes stays -1.
  }
#endif

  // ── Main loop ─────────────────────────────────────────────────────────────
  using Clock = std::chrono::steady_clock;
  using Duration = std::chrono::duration<double>;
  using us = std::chrono::microseconds;

  const int target_fps = std::clamp(cfg.fps, 10, 240);
  const us budget(1'000'000 / target_fps);
  double fps = static_cast<double>(target_fps);
  int fcount = 0;
  long frames = 0;
  auto fps_tp = Clock::now();
  const int WATCH = target_fps * 2;


  // Absolute-deadline frame limiter (Linux).  This is the ONLY thing that
  // paces the loop: input is polled without blocking (see below).
#ifdef __linux__
  const int64_t budget_ns = static_cast<int64_t>(budget.count()) * 1000;
  auto monotonicNs = []() -> int64_t {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
  };
  int64_t deadline_ns = monotonicNs();
#endif

  while (g_running.load()) {
    ++frames;
#ifndef __linux__
    const auto frame_start =
        Clock::now(); // used by non-Linux sleep_for limiter
#endif

    // ── Resize ────────────────────────────────────────────────────────────
    if (g_resize.exchange(false))
      renderer.handleResize();

    // ── SIGUSR1 reload ─────────────────────────────────────────────────────
    // Triggered by:  pkill -USR1 -x viz
    // Reloads the config file and user themes exactly like inotify does,
    // but works over SSH and on any POSIX system.
    if (g_reload.exchange(false)) {
      Config nc;
      nc.last_source = cfg.last_source;
      if (nc.load()) {
        nc.inheritCliOverrides(cfg); // -t / -f keep winning this session
        const bool stereo_changed = (nc.stereo != cfg.stereo);
        cfg = nc;
        applyRendererConfig(renderer, cfg, force_auto_width);
        applyFFTConfig(fft, cfg);
        renderer.setUserThemes(loadUserThemes());
        renderer.notifyChange();
        renderer.showFeedback("Reloaded");
        if (stereo_changed) {
          restartCapture();
          if (audio)
            renderer.setSourceName(sourceLabel(active_source));
        }
      }
    }

    // ── inotify reload ────────────────────────────────────────────────────
#ifdef __linux__
    if (inotify_fd >= 0) {
      alignas(struct inotify_event) char
          ibuf[sizeof(struct inotify_event) + NAME_MAX + 1];
      ssize_t ilen;
      bool cfg_reloaded = false;
      while ((ilen = read(inotify_fd, ibuf, sizeof(ibuf))) > 0) {
        for (const char *p = ibuf; p < ibuf + ilen;) {
          const auto *ev = reinterpret_cast<const struct inotify_event *>(p);

          if (ev->wd == wd_cfg && ev->len > 0 && cfg_file == ev->name &&
              !cfg_reloaded) {
            // Our own save() (key press) also fires this event.  If the file
            // on disk is exactly what we last wrote there is nothing to
            // reload — skipping avoids a redundant rebuildColors + full
            // redraw on every key press.
            if (Config::currentFileDigest() != Config::lastSavedDigest()) {
              // ── Config file changed ───────────────────────────────
              Config nc;
              nc.last_source = cfg.last_source;
              if (nc.load()) {
                nc.inheritCliOverrides(cfg); // -t / -f keep winning
                const bool stereo_changed = (nc.stereo != cfg.stereo);
                cfg = nc;
                applyRendererConfig(renderer, cfg, force_auto_width);
                applyFFTConfig(fft, cfg);
                renderer.notifyChange();
                if (stereo_changed) {
                  restartCapture();
                  if (audio) {
                    renderer.setSourceName(sourceLabel(active_source));
                    renderer.showFeedback(cfg.stereo ? "Stereo" : "Mono");
                  }
                }
              }
              // One reload per batch (CLOSE_WRITE + MOVED_TO often arrive
              // together).  The rest of the batch — e.g. theme events — must
              // still be processed, so no `break` here.
              cfg_reloaded = true;
            }

          } else if (ev->wd == wd_themes) {
            // ── A .theme file was added, edited, or removed ───────
            // ev->len is the allocated buffer size (includes NUL byte
            // and alignment padding) — it is NOT the string length.
            // Use strlen so the check reflects the actual filename.
            // Ignore editor temp files like ocean.theme~ or .ocean.theme.swp.
            const std::size_t nlen = (ev->len > 0) ? std::strlen(ev->name) : 0;
            const bool is_theme =
                (nlen >= 7) && // min: "a.theme"
                (std::strcmp(ev->name + nlen - 6, ".theme") == 0) &&
                ev->name[0] != '.'; // skip hidden/swap files
            if (is_theme) {
              renderer.setUserThemes(loadUserThemes());
              renderer.showFeedback("Themes reloaded");
            }
          }

          p += sizeof(struct inotify_event) + ev->len;
        }
      }
    }
#endif

    // ── Watchdog ──────────────────────────────────────────────────────────
    // Auto mode (default): follows the default sink, retries a source that
    // never produced audio, and falls back through several sources.
    // Pinned mode (-s <source> or -M): the user chose the source, so never
    // second-guess it — only RE-OPEN THE SAME ONE if the stream dies (device
    // unplugged, server restarted) and keep retrying until it comes back.
    if (frames % WATCH == 0) {
      bool reconnect = false;
      if (!audio)
        reconnect = (frames >= next_attempt_frame); // last attempt failed
      else if (audio->hasFailed())
        reconnect = true;

      if (!pinned && audio && !reconnect) {
        // Default sink changed since we connected.
        if (!tracked_mon.empty()) {
          const std::string cur = detectMonitor();
          if (!cur.empty() && cur != tracked_mon)
            reconnect = true;
        }
        // Never heard anything on this connection: probably the wrong source.
        // Retry with exponential backoff (5s, 10s, 20s ... 60s).  Once audio
        // has been heard, silence (paused player) never triggers this.
        if (!reconnect && !heard_audio && frames > target_fps &&
            silent_frames >= target_fps * silent_retry_secs) {
          reconnect = true;
          silent_retry_secs = std::min(silent_retry_secs * 2, SILENCE_MAX_SEC);
        }
      }

      if (reconnect) {
        resetLiveness();
        if (audio) {
          audio->stop();
          audio.reset();
        }
        const std::string mon = pinned ? std::string() : detectMonitor();
        AudioCapture::AudioCallback cb = [&fft](const float *s, std::size_t n,
                                                int ch) {
          fft.addSamples(s, n, ch);
        };
        const std::vector<std::string> candidates =
            pinned ? std::vector<std::string>{pinned_src}
                   : std::vector<std::string>{std::string(), mon,
                                              active_source};
        for (const std::string &src : candidates) {
          audio = makeAudio(backend, src, sample_rate, channels, cb);
          if (audio) {
            bname = audio->backendName();
            // Always record what we actually connected to (including "").
            active_source = src;
            if (!pinned && !src.empty()) {
              cfg.last_source = src;
              cfg.saveState();
            }
            renderer.setSourceName(sourceLabel(active_source));
            break;
          }
        }
        if (audio) {
          fail_backoff_secs = RECONNECT_BACKOFF_BASE_SEC;
          next_attempt_frame = 0;
        } else {
          // Still down: wait longer each time (2s, 4s, 8s, then every 10s) instead of
          // hammering the sound server.
          next_attempt_frame =
              frames + static_cast<long>(target_fps) * fail_backoff_secs;
          fail_backoff_secs =
              std::min(fail_backoff_secs * 2, RECONNECT_BACKOFF_MAX_SEC);
        }
        if (!pinned)
          tracked_mon = mon;
      }
    }

    // ── Input ─────────────────────────────────────────────────────────────
    // Non-blocking.  getch() used to block for a whole frame budget AND the
    // deadline sleep followed, so the real period was budget + work (the
    // deadline fell permanently behind and never slept), and a held key
    // skipped the wait entirely, removing the cap.  Now pacing is done in one
    // place only (the deadline sleep at the bottom); up to 16 queued keys
    // (key-repeat, paste) are handled per frame.
    wtimeout(stdscr, 0);
    for (int keys = 0; keys < 16; ++keys) {
      const int ch = getch();
      if (ch == ERR)
        break;

      switch (ch) {

      case 'q':
        g_running.store(false);
        break;

      case 't':
        cfg.theme = renderer.nextTheme(); // int: 0..COUNT-1+user themes
        cfg.cli_theme = false; // the user chose this one: persist it
        cfg.save();
        break;

      case 'g':
        cfg.gap_width = renderer.cycleGap();
        cfg.save();
        break;

      case ']':
        cfg.bar_width = renderer.increaseBarWidth();
        cfg.save();
        break;

      case '[':
        cfg.bar_width = renderer.decreaseBarWidth();
        cfg.save();
        break;

      case KEY_UP: {
        float s = fft.increaseSensitivity();
        cfg.sensitivity = s;
        cfg.auto_sens = false;
        fft.setAutoSens(false);
        renderer.notifyChange();
        cfg.save();
        break;
      }
      case KEY_DOWN: {
        float s = fft.decreaseSensitivity();
        cfg.sensitivity = s;
        cfg.auto_sens = false;
        fft.setAutoSens(false);
        renderer.notifyChange();
        cfg.save();
        break;
      }
      case 'a':
        cfg.auto_sens = !cfg.auto_sens;
        fft.setAutoSens(cfg.auto_sens);
        renderer.notifyChange();
        cfg.save();
        break;

      case 'h':
        renderer.toggleHudPin();
        cfg.hud_pinned = renderer.hudPinned();
        cfg.save();
        break;

      // ── Stereo / Mono hot-toggle ──────────────────────────────────────
      case 's': {
        cfg.stereo = !cfg.stereo;
        restartCapture();
        if (audio) {
          renderer.setSourceName(sourceLabel(active_source));
          renderer.showFeedback(cfg.stereo ? "Stereo" : "Mono");
          cfg.save();
        } else {
          // Revert on failure
          cfg.stereo = !cfg.stereo;
          restartCapture();
          renderer.showFeedback("Toggle failed");
        }
        break;
      }

      // ── Colour cycle ──────────────────────────────────────────────────
      case 'c':
        renderer.toggleColourCycle();
        cfg.colour_cycle = renderer.colourCycle();
        cfg.save();
        break;

      // ── Per-bar colour ────────────────────────────────────────────────
      case 'v':
        renderer.togglePerBarColour();
        cfg.per_bar_colour = renderer.perBarColour();
        cfg.save();
        break;

      // ── A-weighting ───────────────────────────────────────────────────
      case 'w':
        cfg.a_weighting = !cfg.a_weighting;
        fft.setAWeighting(cfg.a_weighting);
        renderer.showFeedback(cfg.a_weighting ? "A-Weight On" : "A-Weight Off");
        renderer.notifyChange();
        cfg.save();
        break;

      // ── Auto-mono ─────────────────────────────────────────────────────
      case 'n':
        cfg.auto_mono = !cfg.auto_mono;
        fft.setAutoMono(cfg.auto_mono);
        renderer.showFeedback(cfg.auto_mono ? "Auto-Mono On" : "Auto-Mono Off");
        renderer.notifyChange();
        cfg.save();
        break;

      case KEY_RESIZE:
        renderer.handleResize();
        break;

      default:
        break;
      }

    } // key drain loop

    // ── Compute + render ──────────────────────────────────────────────────
    fft.execute(renderer.barCount(), static_cast<float>(fps));

    if (frames > target_fps) {
      const auto &bl = fft.barsL();
      float rms = 0.f;
      for (float v : bl)
        rms += v * v;
      rms = std::sqrt(rms / std::max(1, static_cast<int>(bl.size())));
      if (rms < 0.001f) {
        ++silent_frames;
      } else {
        silent_frames = 0;
        heard_audio = true;
        silent_retry_secs = SILENCE_BASE_SEC; // healthy again: reset backoff
      }
    }

    renderer.render(fft.barsL(), fft.barsR(), fps, bname, fft.sensitivity(),
                    cfg.auto_sens);

    // ── FPS tracking ──────────────────────────────────────────────────────
    ++fcount;
    {
      const auto now = Clock::now();
      const double secs = Duration(now - fps_tp).count();
      if (secs >= 1.0) {
        fps = fcount / secs;
        fcount = 0;
        fps_tp = now;
      }
    }

    // ── Frame limiter ─────────────────────────────────────────────────────
    // Sleep until an ABSOLUTE deadline that advances by exactly one frame
    // budget, so per-frame jitter never accumulates and there is no busy-spin
    // remainder (which keeps the CPU out of deep C-states).  A signal
    // (SIGWINCH/SIGUSR1) cuts the sleep short so resize/reload feel instant;
    // that only brings one frame forward, the deadline itself is unchanged.
    // If we are more than two frames late (stall, suspend/resume, huge
    // resize) resync to "now" rather than bursting to catch up.
#ifdef __linux__
    {
      const int64_t now_ns = monotonicNs();
      int64_t next_ns = deadline_ns + budget_ns;
      if (now_ns - next_ns > 2 * budget_ns)
        next_ns = now_ns + budget_ns;
      deadline_ns = next_ns;
      struct timespec ts {};
      ts.tv_sec = static_cast<time_t>(next_ns / 1'000'000'000LL);
      ts.tv_nsec = static_cast<long>(next_ns % 1'000'000'000LL);
      while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) ==
                 EINTR &&
             g_running.load() && !g_resize.load() && !g_reload.load()) {
      }
    }
#else
    const auto elapsed = Clock::now() - frame_start;
    if (elapsed < budget)
      std::this_thread::sleep_for(budget - elapsed);
#endif
  }

  // ── Teardown ──────────────────────────────────────────────────────────────
  if (audio) {
    audio->stop();
    audio.reset();
  }
#ifdef __linux__
  if (inotify_fd >= 0)
    close(inotify_fd);
#endif
  cfg.save();
  cfg.saveState();
  return 0;
}
