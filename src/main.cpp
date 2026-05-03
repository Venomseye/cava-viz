#include "audio_capture.h"
#include "config.h"
#include "fft_processor.h"
#include "renderer.h"

#ifdef HAVE_PULSEAUDIO
#  include "pulse_capture.h"
#endif
#ifdef HAVE_PIPEWIRE
#  include "pipewire_capture.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
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
#  include <sys/stat.h>
#  include <unistd.h>
#endif

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_resize {false};
static void sig_handler(int s) {
    if (s == SIGINT || s == SIGTERM) g_running.store(false);
    else if (s == SIGWINCH)          g_resize.store(true);
}

// ── Monitor detection (throttled to once per POLL_SECS) ───────────────────────
static constexpr double MONITOR_POLL_SECS = 5.0;

static std::string runCmd(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return "";
    char buf[256] = {};
    bool ok = (std::fgets(buf, sizeof(buf), fp) != nullptr);
    pclose(fp);
    if (!ok) return "";
    std::string s = buf;
    while (!s.empty() && (s.back()=='\n'||s.back()=='\r'||s.back()==' '))
        s.pop_back();
    return s;
}

static std::string detectMonitor() {
    using Clock = std::chrono::steady_clock;
    static std::string      cached;
    static Clock::time_point last_call = Clock::time_point{};
    const auto now = Clock::now();
    if (std::chrono::duration<double>(now - last_call).count() < MONITOR_POLL_SECS)
        return cached;
    last_call = now;
    std::string sink = runCmd("pactl get-default-sink 2>/dev/null");
    cached = sink.empty()
           ? runCmd("pactl list short sources 2>/dev/null | awk '/\\.monitor/{print $2;exit}'")
           : (sink + ".monitor");
    return cached;
}

// ── Audio factory ─────────────────────────────────────────────────────────────
static std::unique_ptr<AudioCapture> makeAudio(
        const std::string& backend, const std::string& source,
        int sr, int ch, AudioCapture::AudioCallback cb)
{
    auto try1 = [&](std::unique_ptr<AudioCapture> cap) -> std::unique_ptr<AudioCapture> {
        if (!cap->init(source, sr, ch)) return nullptr;
        if (!cap->start(cb))            return nullptr;
        return cap;
    };
    std::unique_ptr<AudioCapture> a;
#ifdef HAVE_PIPEWIRE
    if (backend == "auto" || backend == "pipewire") {
        a = try1(std::make_unique<PipeWireCapture>());
        if (a || backend == "pipewire") return a;
    }
#endif
#ifdef HAVE_PULSEAUDIO
    if (!a && (backend == "auto" || backend == "pulse"))
        a = try1(std::make_unique<PulseAudioCapture>());
#endif
    return a;
}

static void print_usage(const char* p) {
    printf(
        "Usage: %s [OPTIONS]\n\n"
        "Terminal audio visualizer (CAVA algorithm)\n\n"
        "Options:\n"
        "  -b <pulse|pipewire|auto>  Backend (default: auto)\n"
        "  -s <source>               Explicit source name\n"
        "  -M                        Use microphone\n"
        "  -r <Hz>                   Sample rate (default: 44100)\n"
        "  -t <0-11>                 Initial theme\n"
        "  -f <n>                    Target FPS (default: 60)\n"
        "  -w                        Force auto bar width\n"
        "  -h                        Show help\n\n"
        "Keys:\n"
        "  q          Quit\n"
        "  t          Next theme\n"
        "  g          Cycle gap (0-2)\n"
        "  ] / [      Increase / decrease bar width\n"
        "  UP / DOWN  Manual sensitivity\n"
        "  a          Toggle auto-sensitivity\n"
        "  h          Toggle HUD pin (always visible)\n"
        "  s          Toggle stereo / mono\n\n"
        "Config: %s\n"
        "  Edit while running — inotify reloads changes instantly.\n\n"
        "Themes: Fire Plasma Neon Teal Sunset Candy Aurora Inferno "
               "White Rose Mermaid Vapor\n",
        p, Config::configPath().c_str());
}

// ── Apply all FFT config fields ───────────────────────────────────────────────
static void applyFFTConfig(FFTProcessor& fft, const Config& cfg) {
    fft.setSensitivity(cfg.sensitivity);
    fft.setAutoSens(cfg.auto_sens);
    fft.setGravity(cfg.gravity);
    fft.setMonstercat(cfg.monstercat);
    fft.setHighCutoff(cfg.high_cutoff);
    fft.setRiseFactor(cfg.rise_factor);
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    struct sigaction sa{};
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGWINCH,&sa, nullptr);

    // ── Config ────────────────────────────────────────────────────────────────
    Config cfg;
    cfg.load();
    cfg.loadState();  // loads last_source from the separate state file

    // ── CLI ───────────────────────────────────────────────────────────────────
    std::string backend    = "auto";
    std::string cli_source;
    int  sample_rate       = 44100;
    bool use_mic           = false;
    bool force_auto_width  = false;

    static const struct option long_opts[] = {
        {"backend",   required_argument, nullptr, 'b'},
        {"source",    required_argument, nullptr, 's'},
        {"mic",       no_argument,       nullptr, 'M'},
        {"rate",      required_argument, nullptr, 'r'},
        {"theme",     required_argument, nullptr, 't'},
        {"fps",       required_argument, nullptr, 'f'},
        {"autowidth", no_argument,       nullptr, 'w'},
        {"help",      no_argument,       nullptr, 'h'},
        {nullptr,     0,                 nullptr,  0 }
    };
    int o;
    while ((o = getopt_long(argc, argv, "b:s:Mr:t:f:wh", long_opts, nullptr)) != -1) {
        switch (o) {
            case 'b': backend          = optarg;                              break;
            case 's': cli_source       = optarg;                              break;
            case 'M': use_mic          = true;                                break;
            case 'r': sample_rate      = std::stoi(optarg);                   break;
            case 't': { int ti = std::stoi(optarg);
                        if (ti>=0 && ti<(int)Theme::COUNT) cfg.theme=ti; }   break;
            case 'f': cfg.fps          = std::max(1, std::stoi(optarg));      break;
            case 'w': force_auto_width = true;                                break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    // ── FFT processor (stack-allocated; lives for the entire duration of main) ─
    // The audio callback captures it by reference via [&] — safe because the
    // object never moves. Stereo toggle calls fft.reinit() in-place after
    // stopping the audio thread, so no pointer ever becomes invalid.
    int channels = cfg.stereo ? 2 : 1;
    FFTProcessor fft(sample_rate, channels);
    applyFFTConfig(fft, cfg);

    // ── Callback builder ──────────────────────────────────────────────────────
    // Always produces a fresh closure bound to the current `fft` by reference.
    // Safe because `fft` is on the stack and outlives all AudioCapture objects.
    auto make_cb = [&]() -> AudioCapture::AudioCallback {
        return [&](const std::vector<float>& s, int ch) { fft.addSamples(s, ch); };
    };

    // ── Audio source resolution ───────────────────────────────────────────────
    std::string active_source;
    std::unique_ptr<AudioCapture> audio;
    std::string bname;  // backend display name; updated on every (re)start

    // startAudio(): resolves source, starts audio, updates active_source/bname.
    // Assumes audio is already stopped (or was never started).
    auto startAudio = [&]() {
        if (!cli_source.empty()) {
            active_source = cli_source;
            audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
        } else if (use_mic) {
            // Try empty source first, then "default"
            active_source = "";
            audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
            if (!audio) {
                active_source = "default";
                audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
            }
        } else {
            // 1. PipeWire loopback (empty source)
            active_source = "";
            audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
            // 2. Saved source from last session
            if (!audio && !cfg.last_source.empty()) {
                active_source = cfg.last_source;
                audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
            }
            // 3. Shell-detected monitor (throttled)
            if (!audio) {
                active_source = detectMonitor();
                if (!active_source.empty())
                    audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
            }
            // 4. Final fallback
            if (!audio) {
                active_source = "";
                audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
            }
        }

        if (audio) {
            bname = audio->backendName();
            if (!active_source.empty() && !use_mic) {
                cfg.last_source = active_source;
                cfg.saveState();
            }
        }
    };

    startAudio();
    if (!audio) { fprintf(stderr, "cava-viz: no audio backend started.\n"); return 1; }

    // ── Renderer ──────────────────────────────────────────────────────────────
    Renderer renderer;
    if (!renderer.init()) { audio->stop(); return 1; }
    renderer.setTheme(static_cast<Theme>(cfg.theme));
    renderer.setGapWidth(cfg.gap_width);
    renderer.setBarWidth(force_auto_width ? renderer.autoBarWidth() : cfg.bar_width);
    renderer.setHudPinned(cfg.hud_pinned);
    renderer.setSourceName(active_source);
    renderer.notifyChange();

    // ── inotify: live config reload ───────────────────────────────────────────
    const std::string cfg_path = Config::configPath();
    const std::string cfg_dir  = cfg_path.substr(0, cfg_path.rfind('/'));
    const std::string cfg_file = cfg_path.substr(cfg_path.rfind('/') + 1);

    int inotify_fd = -1;
#ifdef __linux__
    mkdir(cfg_dir.c_str(), 0755);
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd >= 0) {
        if (inotify_add_watch(inotify_fd, cfg_dir.c_str(),
                              IN_CLOSE_WRITE | IN_MOVED_TO) < 0) {
            close(inotify_fd);
            inotify_fd = -1;
        }
    }
#endif

    // ── Main loop ─────────────────────────────────────────────────────────────
    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;
    using us       = std::chrono::microseconds;

    const int target_fps = std::clamp(cfg.fps, 10, 240);
    const us  budget(1'000'000 / target_fps);
    double fps   = (double)target_fps;
    int fcount   = 0;
    int frames   = 0;
    auto fps_tp  = Clock::now();
    const int WATCH = target_fps * 2;

    // RMS silence tracking — only start counting after a warm-up period
    static constexpr int SILENCE_SEC = 5;
    int silent_frames     = 0;
    const int silence_lim = target_fps * SILENCE_SEC;

    while (g_running.load()) {
        const auto t0 = Clock::now();
        ++frames;

        // ── Resize ────────────────────────────────────────────────────────────
        if (g_resize.exchange(false))
            renderer.handleResize();

        // ── Live config reload ────────────────────────────────────────────────
#ifdef __linux__
        if (inotify_fd >= 0) {
            alignas(struct inotify_event)
            char ibuf[sizeof(struct inotify_event) + NAME_MAX + 1];
            ssize_t ilen;
            while ((ilen = read(inotify_fd, ibuf, sizeof(ibuf))) > 0) {
                for (const char* p = ibuf; p < ibuf + ilen; ) {
                    const auto* ev = reinterpret_cast<const struct inotify_event*>(p);
                    if (ev->len > 0 && cfg_file == ev->name) {
                        Config nc;
                        nc.last_source = cfg.last_source;  // preserve state
                        if (nc.load()) {
                            cfg = nc;
                            renderer.setTheme(static_cast<Theme>(cfg.theme));
                            renderer.setGapWidth(cfg.gap_width);
                            if (!force_auto_width)
                                renderer.setBarWidth(cfg.bar_width);
                            renderer.setHudPinned(cfg.hud_pinned);
                            applyFFTConfig(fft, cfg);
                            renderer.notifyChange();
                        }
                        break;
                    }
                    p += sizeof(struct inotify_event) + ev->len;
                }
            }
        }
#endif

        // ── Watchdog: reconnect on backend failure, source change, or silence ─
        if (!use_mic && cli_source.empty() && (frames % WATCH == 0)) {
            bool reconnect = audio && audio->hasFailed();

            if (!reconnect && !active_source.empty()) {
                const std::string cur = detectMonitor();  // throttled
                if (!cur.empty() && cur != active_source)
                    reconnect = true;
            }

            // Reconnect if output has been silent for SILENCE_SEC seconds.
            // Skip the first warm-up period to avoid false triggers on startup.
            if (!reconnect && frames > target_fps && silent_frames >= silence_lim)
                reconnect = true;

            if (reconnect) {
                silent_frames = 0;
                if (audio) { audio->stop(); audio.reset(); }
                const std::string mon = detectMonitor();
                for (const std::string& src : {std::string(""), mon, active_source}) {
                    audio = makeAudio(backend, src, sample_rate, channels, make_cb());
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
        wtimeout(stdscr, 4);
        const int ch = getch();
        if (ch == ERR) goto next_frame;

        switch (ch) {
            case 'q':
                g_running.store(false);
                break;

            case 't':
                cfg.theme = (int)renderer.nextTheme();
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
                cfg.auto_sens   = false;
                fft.setAutoSens(false);
                renderer.notifyChange();
                cfg.save();
                break;
            }

            case KEY_DOWN: {
                float s = fft.decreaseSensitivity();
                cfg.sensitivity = s;
                cfg.auto_sens   = false;
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

            // ── HUD pin ───────────────────────────────────────────────────────
            case 'h':
                renderer.toggleHudPin();
                cfg.hud_pinned = renderer.hudPinned();
                cfg.save();
                break;

            // ── Stereo / Mono hot-toggle ──────────────────────────────────────
            // Safe sequence:
            //   1. Stop the audio capture thread (no more callbacks after this)
            //   2. Call fft.reinit() — object stays at the same address, so the
            //      [&] reference in the callback closure remains valid forever
            //   3. Restart audio — new callback produced by make_cb() captures
            //      the same `fft` by reference
            case 's': {
                if (audio) { audio->stop(); audio.reset(); }

                cfg.stereo = !cfg.stereo;
                channels   = cfg.stereo ? 2 : 1;

                // reinit() takes the internal mutex and resets all FFT state.
                // No audio thread is running at this point, so it is safe.
                fft.reinit(channels);
                applyFFTConfig(fft, cfg);
                silent_frames = 0;

                startAudio();  // make_cb() produces a fresh closure for the new channel count

                if (audio) {
                    renderer.setSourceName(active_source);
                    renderer.showFeedback(cfg.stereo ? "Stereo" : "Mono");
                    cfg.save();
                } else {
                    // Restart failed — revert
                    cfg.stereo = !cfg.stereo;
                    channels   = cfg.stereo ? 2 : 1;
                    fft.reinit(channels);
                    applyFFTConfig(fft, cfg);
                    startAudio();
                    renderer.showFeedback("Toggle failed");
                }
                break;
            }

            case KEY_RESIZE:
                renderer.handleResize();
                break;

            default:
                break;
        }

        next_frame:;

        // ── Compute + render ──────────────────────────────────────────────────
        const int   n    = renderer.barCount();
        const float fpsc = (float)fps;
        fft.execute(n, fpsc);

        // RMS silence detection (skip warm-up)
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
        const auto elapsed = Clock::now() - t0;
        if (elapsed < budget)
            std::this_thread::sleep_for(budget - elapsed);
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    // Stop audio BEFORE fft goes out of scope so the callback never fires on
    // a destroyed object. (fft is on the stack; audio is destroyed first here.)
    if (audio) { audio->stop(); audio.reset(); }

#ifdef __linux__
    if (inotify_fd >= 0) close(inotify_fd);
#endif

    cfg.save();
    cfg.saveState();
    return 0;
}
