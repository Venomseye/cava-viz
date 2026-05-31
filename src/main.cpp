#include "audio_capture.h"
#include "audio_utils.h"
#include "bar_mode.h"
#include "bar_output.h"
#include "config.h"
#include "fft_processor.h"
#include "renderer.h"
#include "user_theme.h"

// Defined by CMakeLists.txt via target_compile_definitions; fall back to
// "unknown" when the binary is built without CMake (e.g. a plain Makefile).
#ifndef CAVA_VIZ_VERSION
#  define CAVA_VIZ_VERSION "unknown"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>   // NAME_MAX (Debian/Ubuntu don't expose it via inotify.h)
#include <cmath>     // std::sqrt, std::fmod
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <memory>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#  include <fcntl.h>
#  include <sys/inotify.h>
#  include <sys/resource.h>
#  include <sys/stat.h>
#  include <time.h>
#  include <unistd.h>
#endif

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_resize {false};
static void sig_handler(int s) {
    if (s==SIGINT||s==SIGTERM) g_running.store(false);
    else if (s==SIGWINCH)      g_resize.store(true);
}

static void print_usage(const char* p) {
    printf(
        "Usage: %s [OPTIONS]\n\n"
        "Terminal audio visualizer (CAVA algorithm)\n\n"
        "Options:\n"
        "  -b <pulse|pipewire|auto>   Backend (default: auto)\n"
        "  -s <source>                Explicit source device\n"
        "  -M                         Use microphone input\n"
        "  -r <Hz>                    Sample rate (default: 44100)\n"
        "  -t <0-11>                  Theme index\n"
        "  -f <n>                     Target FPS (default: 60)\n"
        "  -w                         Auto bar width\n"
        "  -V                         Show version and exit\n"
        "  -h                         Show help\n\n"
        "Bar mode (headless — write to stdout, FIFO, or Unix socket):\n"
        "  --bar                      Enable bar mode\n"
        "  --bar-format <fmt>         plain|waybar|polybar|eww|raw|dzen2|i3bar  (default: plain)\n"
        "  --bar-count  <n>           Bars per channel  (default: 10)\n"
        "  --bar-chars  <chars>       UTF-8 level characters  (default: ▁▂▃▄▅▆▇█)\n"
        "  --bar-color  <#hex>        Accent color for tagged formats  (default: #00ffcc)\n"
        "  --bar-fps    <n>           Output frame rate  (default: 15)\n"
        "  --bar-stereo <merge|split> Stereo handling  (default: merge)\n"
        "  --bar-sep    <str>         Separator for split stereo  (default: ' | ')\n"
        "  --bar-sink   <type>        stdout|fifo|socket  (default: stdout)\n"
        "  --bar-out    <path>        Output path for fifo or socket sink\n\n"
        "Keys (terminal mode only):\n"
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
        "Config: %s\n"
        "  Edit while running — inotify reloads changes instantly.\n\n"
        "Themes: Fire Plasma Neon Teal Sunset Candy Aurora Inferno "
               "White Rose Mermaid Vapor\n",
        p, Config::configPath().c_str());
}

// ── Apply rendering config ────────────────────────────────────────────────────
static void applyRendererConfig(Renderer& r, const Config& cfg,
                                bool force_auto_width) {
    r.setThemeIdx(cfg.theme);   // handles both built-in and user theme indices
    r.setGapWidth(cfg.gap_width);
    if (!force_auto_width) r.setBarWidth(cfg.bar_width);
    r.setHudPinned(cfg.hud_pinned);
    r.setColourCycle(cfg.colour_cycle);
    r.setPerBarColour(cfg.per_bar_colour);
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    struct sigaction sa{};
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGWINCH,&sa, nullptr);

    Config cfg;
    cfg.load();
    cfg.loadState();

#ifdef __linux__
    // Lower process priority so the kernel parks this core into deeper C-states
    // when nothing else is running, allowing the fan controller to reduce speed.
    // nice +5: still responsive but yields immediately to any competing work.
    setpriority(PRIO_PROCESS, 0, 5);
#endif

    std::string backend   = "auto";
    std::string cli_source;
    int  sample_rate      = 44100;
    bool use_mic          = false;
    bool force_auto_width = false;

    // ── Bar mode config ───────────────────────────────────────────────────────
    bool          bar_mode {false};
    BarOutputConfig bar_cfg;

    // ── Argument parsing ──────────────────────────────────────────────────────
    // Long-only bar options get indices above the printable ASCII range.
    enum {
        OPT_BAR        = 1000,
        OPT_BAR_FORMAT,
        OPT_BAR_COUNT,
        OPT_BAR_CHARS,
        OPT_BAR_COLOR,
        OPT_BAR_FPS,
        OPT_BAR_STEREO,
        OPT_BAR_SEP,
        OPT_BAR_SINK,
        OPT_BAR_OUT,
    };

    static const struct option long_opts[] = {
        {"backend",    required_argument, nullptr, 'b'},
        {"source",     required_argument, nullptr, 's'},
        {"mic",        no_argument,       nullptr, 'M'},
        {"rate",       required_argument, nullptr, 'r'},
        {"theme",      required_argument, nullptr, 't'},
        {"fps",        required_argument, nullptr, 'f'},
        {"autowidth",  no_argument,       nullptr, 'w'},
        {"version",    no_argument,       nullptr, 'V'},
        {"help",       no_argument,       nullptr, 'h'},
        // Bar mode (long-only)
        {"bar",        no_argument,       nullptr, OPT_BAR},
        {"bar-format", required_argument, nullptr, OPT_BAR_FORMAT},
        {"bar-count",  required_argument, nullptr, OPT_BAR_COUNT},
        {"bar-chars",  required_argument, nullptr, OPT_BAR_CHARS},
        {"bar-color",  required_argument, nullptr, OPT_BAR_COLOR},
        {"bar-fps",    required_argument, nullptr, OPT_BAR_FPS},
        {"bar-stereo", required_argument, nullptr, OPT_BAR_STEREO},
        {"bar-sep",    required_argument, nullptr, OPT_BAR_SEP},
        {"bar-sink",   required_argument, nullptr, OPT_BAR_SINK},
        {"bar-out",    required_argument, nullptr, OPT_BAR_OUT},
        {nullptr,      0,                 nullptr,  0 }
    };
    int o;
    while ((o = getopt_long(argc, argv, "b:s:Mr:t:f:wVh", long_opts, nullptr)) != -1) {
        switch (o) {
            case 'b': backend          = optarg;                              break;
            case 's': cli_source       = optarg;                              break;
            case 'M': use_mic          = true;                                break;
            case 'r': sample_rate      = std::stoi(optarg);                   break;
            case 't': { int ti=std::stoi(optarg);
                        if (ti>=0&&ti<(int)Theme::COUNT) cfg.theme=ti; }     break;
            case 'f': cfg.fps          = std::max(1, std::stoi(optarg));      break;
            case 'w': force_auto_width = true;                                break;
            case 'V': printf("cava-viz %s\n", CAVA_VIZ_VERSION); return 0;
            case 'h': print_usage(argv[0]); return 0;
            // ── Bar mode flags ────────────────────────────────────────────────
            case OPT_BAR:        bar_mode = true;                            break;
            case OPT_BAR_FORMAT: {
                std::string f = optarg;
                if      (f=="plain")   bar_cfg.format = BarFormat::Plain;
                else if (f=="waybar")  bar_cfg.format = BarFormat::Waybar;
                else if (f=="polybar") bar_cfg.format = BarFormat::Polybar;
                else if (f=="eww")     bar_cfg.format = BarFormat::Eww;
                else if (f=="raw")     bar_cfg.format = BarFormat::Raw;
                else if (f=="dzen2")   bar_cfg.format = BarFormat::Dzen2;
                else if (f=="i3bar")   bar_cfg.format = BarFormat::I3bar;
                else { fprintf(stderr,"cava-viz: unknown --bar-format '%s'\n",optarg);
                       return 1; }
                break;
            }
            case OPT_BAR_COUNT:  bar_cfg.count  = std::max(1, std::stoi(optarg)); break;
            case OPT_BAR_CHARS:  bar_cfg.chars  = optarg;                    break;
            case OPT_BAR_COLOR:  bar_cfg.color  = optarg;                    break;
            case OPT_BAR_FPS:    bar_cfg.fps    = std::max(1, std::stoi(optarg)); break;
            case OPT_BAR_STEREO: {
                std::string s = optarg;
                if      (s=="merge") bar_cfg.stereo = BarStereo::Merge;
                else if (s=="split") bar_cfg.stereo = BarStereo::Split;
                else { fprintf(stderr,"cava-viz: unknown --bar-stereo '%s'\n",optarg);
                       return 1; }
                break;
            }
            case OPT_BAR_SEP:    bar_cfg.stereo_sep = optarg;               break;
            case OPT_BAR_SINK: {
                std::string s = optarg;
                if      (s=="stdout") bar_cfg.sink = BarSink::Stdout;
                else if (s=="fifo")   bar_cfg.sink = BarSink::Fifo;
                else if (s=="socket") bar_cfg.sink = BarSink::Socket;
                else { fprintf(stderr,"cava-viz: unknown --bar-sink '%s'\n",optarg);
                       return 1; }
                break;
            }
            case OPT_BAR_OUT:    bar_cfg.sink_path = optarg;                break;
            default: print_usage(argv[0]); return 1;
        }
    }

    // ── Bar mode branch (before ncurses) ──────────────────────────────────────
    if (bar_mode) {
        // Inherit stereo setting from config unless --bar-stereo was given.
        // (bar_cfg.stereo defaults to Merge regardless of cfg.stereo — keeping
        // them independent lets users choose the stereo output behaviour.)
        return runBarMode(backend, cli_source, use_mic, sample_rate,
                          cfg, bar_cfg, g_running);
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
        doStartAudio(backend, cli_source, use_mic, sample_rate, channels,
                     fft, cfg, audio, active_source, bname);
    };

    startAudio();
    if (!audio) {
#if !defined(HAVE_PULSEAUDIO) && !defined(HAVE_PIPEWIRE)
        fprintf(stderr,
            "cava-viz: no audio backend compiled in.\n"
            "  Install libpulse-dev (PulseAudio) or libpipewire-0.3-dev (PipeWire)\n"
            "  then rebuild: cd build && cmake .. && cmake --build .\n");
#else
        fprintf(stderr, "cava-viz: no audio backend started.\n");
#endif
        return 1;
    }

    // ── Renderer ──────────────────────────────────────────────────────────────
    Renderer renderer;
    if (!renderer.init()) { audio->stop(); return 1; }
    // Load user themes before applyRendererConfig so cfg.theme (which may be
    // a user theme index >= Theme::COUNT) validates and clamps correctly.
    renderer.setUserThemes(loadUserThemes());
    applyRendererConfig(renderer, cfg, force_auto_width);
    if (force_auto_width) renderer.setBarWidth(renderer.autoBarWidth());
    renderer.setSourceName(active_source);
    renderer.notifyChange();

    // ── inotify ───────────────────────────────────────────────────────────────
    const std::string cfg_path    = Config::configPath();
    const std::string cfg_dir     = cfg_path.substr(0, cfg_path.rfind('/'));
    const std::string cfg_file    = cfg_path.substr(cfg_path.rfind('/') + 1);
    const std::string themes_path = themesDir();
    int inotify_fd = -1;
    int wd_cfg     = -1;
    int wd_themes  = -1;
#ifdef __linux__
    mkdir(cfg_dir.c_str(), 0755);
    mkdir(themes_path.c_str(), 0755);   // create themes/ if it doesn't exist
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd >= 0) {
        wd_cfg = inotify_add_watch(inotify_fd, cfg_dir.c_str(),
                                   IN_CLOSE_WRITE|IN_MOVED_TO);
        if (wd_cfg < 0) { close(inotify_fd); inotify_fd = -1; }
        // Watch themes/ dir too — adding, editing, or removing a .theme file
        // triggers an immediate reload without restarting the visualizer.
        // IN_DELETE covers rm; IN_CLOSE_WRITE covers saves; IN_MOVED_TO covers
        // atomic-rename saves (how most editors write files).
        if (inotify_fd >= 0)
            wd_themes = inotify_add_watch(inotify_fd, themes_path.c_str(),
                                          IN_CLOSE_WRITE|IN_MOVED_TO|IN_DELETE);
        // A missing or unreadable themes/ dir is non-fatal; wd_themes stays -1.
    }
#endif

    // ── Main loop ─────────────────────────────────────────────────────────────
    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;
    using us       = std::chrono::microseconds;

    const int target_fps = std::clamp(cfg.fps, 10, 240);
    const us  budget(1'000'000 / target_fps);
    double fps   = (double)target_fps;
    int fcount   = 0, frames = 0;
    auto fps_tp  = Clock::now();
    const int WATCH = target_fps * 2;

    static constexpr int SILENCE_SEC = 5;
    int silent_frames     = 0;
    const int silence_lim = target_fps * SILENCE_SEC;

    // Absolute-deadline frame limiter (Linux): initialise the first deadline
    // to now so the first iteration sleeps for exactly one budget period.
#ifdef __linux__
    struct timespec t_deadline{};
    clock_gettime(CLOCK_MONOTONIC, &t_deadline);
#endif

    while (g_running.load()) {
        ++frames;
#ifndef __linux__
        const auto frame_start = Clock::now();  // used by non-Linux sleep_for limiter
#endif

        // ── Resize ────────────────────────────────────────────────────────────
        if (g_resize.exchange(false))
            renderer.handleResize();

        // ── inotify reload ────────────────────────────────────────────────────
#ifdef __linux__
        if (inotify_fd >= 0) {
            alignas(struct inotify_event)
            char ibuf[sizeof(struct inotify_event) + NAME_MAX + 1];
            ssize_t ilen;
            while ((ilen = read(inotify_fd, ibuf, sizeof(ibuf))) > 0) {
                for (const char* p = ibuf; p < ibuf + ilen; ) {
                    const auto* ev = reinterpret_cast<const struct inotify_event*>(p);

                    if (ev->wd == wd_cfg && ev->len > 0 && cfg_file == ev->name) {
                        // ── Config file changed ───────────────────────────────
                        Config nc;
                        nc.last_source = cfg.last_source;
                        if (nc.load()) {
                            const bool stereo_changed = (nc.stereo != cfg.stereo);
                            cfg = nc;
                            applyRendererConfig(renderer, cfg, force_auto_width);
                            applyFFTConfig(fft, cfg);
                            renderer.notifyChange();
                            if (stereo_changed) {
                                if (audio) { audio->stop(); audio.reset(); }
                                channels = cfg.stereo ? 2 : 1;
                                fft.reinit(channels);
                                applyFFTConfig(fft, cfg);
                                silent_frames = 0;
                                startAudio();
                                if (audio) {
                                    renderer.setSourceName(active_source);
                                    renderer.showFeedback(
                                        cfg.stereo ? "Stereo" : "Mono");
                                }
                            }
                        }
                        break;

                    } else if (ev->wd == wd_themes) {
                        // ── A .theme file was added, edited, or removed ───────
                        // Only react to .theme files; ignore other files (e.g.
                        // editor swap files like ocean.theme~).
                        const bool is_theme = (ev->len > 6) &&
                            std::string(ev->name).rfind(".theme") ==
                                std::string(ev->name).size() - 6;
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
        if (!use_mic && cli_source.empty() && (frames % WATCH == 0)) {
            bool reconnect = audio && audio->hasFailed();
            if (!reconnect && !active_source.empty()) {
                const std::string cur = detectMonitor();
                if (!cur.empty() && cur != active_source) reconnect = true;
            }
            if (!reconnect && frames > target_fps && silent_frames >= silence_lim)
                reconnect = true;

            if (reconnect) {
                silent_frames = 0;
                if (audio) { audio->stop(); audio.reset(); }
                const std::string mon = detectMonitor();
                AudioCapture::AudioCallback cb =
                    [&fft](const std::vector<float>& s, int ch) {
                        fft.addSamples(s, ch);
                    };
                for (const std::string& src : {std::string(""), mon, active_source}) {
                    audio = makeAudio(backend, src, sample_rate, channels, cb);
                    if (audio) {
                        bname = audio->backendName();
                        if (!src.empty()) {
                            active_source   = src;
                            cfg.last_source = src;
                            cfg.saveState();
                            renderer.setSourceName(active_source);
                        }
                        break;
                    }
                }
            }
        }

        // ── Input ─────────────────────────────────────────────────────────────
        // Set timeout equal to the full frame budget so ncurses blocks in the
        // kernel rather than polling repeatedly — reduces CPU wakeups.
        wtimeout(stdscr, 1000 / target_fps);
        const int ch = getch();
        if (ch != ERR) {

        switch (ch) {

            case 'q': g_running.store(false); break;

            case 't':
                cfg.theme = renderer.nextTheme();  // int: 0..COUNT-1+user themes
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
                cfg.sensitivity = s; cfg.auto_sens = false;
                fft.setAutoSens(false);
                renderer.notifyChange(); cfg.save();
                break;
            }
            case KEY_DOWN: {
                float s = fft.decreaseSensitivity();
                cfg.sensitivity = s; cfg.auto_sens = false;
                fft.setAutoSens(false);
                renderer.notifyChange(); cfg.save();
                break;
            }
            case 'a':
                cfg.auto_sens = !cfg.auto_sens;
                fft.setAutoSens(cfg.auto_sens);
                renderer.notifyChange(); cfg.save();
                break;

            case 'h':
                renderer.toggleHudPin();
                cfg.hud_pinned = renderer.hudPinned();
                cfg.save();
                break;

            // ── Stereo / Mono hot-toggle ──────────────────────────────────────
            case 's': {
                if (audio) { audio->stop(); audio.reset(); }
                cfg.stereo = !cfg.stereo;
                channels   = cfg.stereo ? 2 : 1;
                fft.reinit(channels);
                applyFFTConfig(fft, cfg);
                silent_frames = 0;
                startAudio();
                if (audio) {
                    renderer.setSourceName(active_source);
                    renderer.showFeedback(cfg.stereo ? "Stereo" : "Mono");
                    cfg.save();
                } else {
                    // Revert on failure
                    cfg.stereo = !cfg.stereo;
                    channels   = cfg.stereo ? 2 : 1;
                    fft.reinit(channels);
                    applyFFTConfig(fft, cfg);
                    startAudio();
                    renderer.showFeedback("Toggle failed");
                }
                break;
            }            // ── Colour cycle ──────────────────────────────────────────────────
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
                renderer.notifyChange(); cfg.save();
                break;

            // ── Auto-mono ─────────────────────────────────────────────────────
            case 'n':
                cfg.auto_mono = !cfg.auto_mono;
                fft.setAutoMono(cfg.auto_mono);
                renderer.showFeedback(cfg.auto_mono ? "Auto-Mono On" : "Auto-Mono Off");
                renderer.notifyChange(); cfg.save();
                break;

            case KEY_RESIZE:
                renderer.handleResize();
                break;

            default: break;
        }

        } // if (ch != ERR)

        // ── Compute + render ──────────────────────────────────────────────────
        fft.execute(renderer.barCount(), (float)fps);

        if (frames > target_fps) {
            const auto& bl = fft.barsL();
            float rms = 0.f;
            for (float v : bl) rms += v * v;
            rms = std::sqrt(rms / std::max(1, (int)bl.size()));
            silent_frames = (rms < 0.001f) ? silent_frames + 1 : 0;
        }

        renderer.render(fft.barsL(), fft.barsR(), fps, bname,
                        fft.sensitivity(), cfg.auto_sens);

        // ── FPS tracking ──────────────────────────────────────────────────────
        ++fcount;
        {
            const auto   now  = Clock::now();
            const double secs = Duration(now - fps_tp).count();
            if (secs >= 1.0) { fps = fcount / secs; fcount = 0; fps_tp = now; }
        }

        // ── Frame limiter ─────────────────────────────────────────────────────
        // Use clock_nanosleep with an absolute deadline rather than
        // sleep_for(budget - elapsed).  sleep_for measures elapsed AFTER the
        // frame work finishes and then adds a relative sleep — on Linux the
        // kernel rounds up to the next timer tick (~1 ms), so the loop can
        // busy-spin for up to 1 ms at the end of every frame, preventing the
        // CPU from entering C-states and keeping the fan running.
        // An absolute deadline lets the kernel wake us at exactly the right
        // tick without any busy-spin remainder.
#ifdef __linux__
        t_deadline.tv_nsec += budget.count() * 1000LL;
        while (t_deadline.tv_nsec >= 1'000'000'000LL) {
            t_deadline.tv_nsec -= 1'000'000'000LL;
            t_deadline.tv_sec  += 1;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_deadline, nullptr);
#else
        const auto elapsed = Clock::now() - frame_start;
        if (elapsed < budget) std::this_thread::sleep_for(budget - elapsed);
#endif
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    if (audio) { audio->stop(); audio.reset(); }
#ifdef __linux__
    if (inotify_fd >= 0) close(inotify_fd);
#endif
    cfg.save();
    cfg.saveState();
    return 0;
}
