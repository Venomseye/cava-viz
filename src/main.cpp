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
    if (s==SIGINT||s==SIGTERM) g_running.store(false);
    else if (s==SIGWINCH)      g_resize.store(true);
}

// ── Silent monitor detection ──────────────────────────────────────────────────
static std::string runCmd(const char* cmd) {
    FILE* fp = popen(cmd, "r"); if (!fp) return "";
    char buf[256] = {};
    bool ok = (std::fgets(buf, sizeof(buf), fp) != nullptr);
    pclose(fp); if (!ok) return "";
    std::string s = buf;
    while (!s.empty() && (s.back()=='\n'||s.back()=='\r'||s.back()==' ')) s.pop_back();
    return s;
}
static std::string detectMonitor() {
    std::string sink = runCmd("pactl get-default-sink 2>/dev/null");
    if (!sink.empty()) return sink + ".monitor";
    return runCmd("pactl list short sources 2>/dev/null | awk '/\\.monitor/{print $2;exit}'");
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
    if (backend=="auto"||backend=="pipewire") {
        a = try1(std::make_unique<PipeWireCapture>());
        if (a || backend=="pipewire") return a;
    }
#endif
#ifdef HAVE_PULSEAUDIO
    if (!a && (backend=="auto"||backend=="pulse"))
        a = try1(std::make_unique<PulseAudioCapture>());
#endif
    return a;
}

static void print_usage(const char* p) {
    printf("Usage: %s [OPTIONS]\n\n"
        "Terminal audio visualizer (CAVA algorithm)\n\n"
        "Options:\n"
        "  -b <pulse|pipewire|auto>  Backend (default: auto)\n"
        "  -s <source>               Explicit source name\n"
        "  -M                        Use microphone\n"
        "  -r <Hz>                   Sample rate (default: 44100)\n"
        "  -t <0-11>                 Initial theme\n"
        "  -f <n>                    Target FPS (default: 60)\n"
        "  -w                        Force auto bar width (ignores saved width)\n"
        "  -h                        Show help\n\n"
        "Keys:\n"
        "  q        Quit\n"
        "  t        Next theme\n"
        "  g        Cycle gap (0-2)\n"
        "  ] / [    Increase / decrease bar width (1-8)\n"
        "  UP / DOWN  Sensitivity\n"
        "  a        Toggle auto-sensitivity\n\n"
        "Live reload: edit %s while running — changes apply instantly.\n\n"
        "Themes: Fire Plasma Neon Teal Sunset Candy Aurora Inferno "
               "White Rose Mermaid Vapor\n",
        p, Config::configPath().c_str());
}

int main(int argc, char* argv[]) {
    struct sigaction sa{}; sa.sa_handler = sig_handler; sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr); sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGWINCH, &sa, nullptr);

    Config cfg; cfg.load();
    std::string backend = "auto", cli_source;
    int  sample_rate     = 44100;
    int  target_fps      = 60;
    bool use_mic         = false;
    bool force_auto_width= false;

    static const struct option opts[] = {
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
    while ((o = getopt_long(argc, argv, "b:s:Mr:t:f:wh", opts, nullptr)) != -1) {
        switch (o) {
            case 'b': backend          = optarg;                           break;
            case 's': cli_source       = optarg;                           break;
            case 'M': use_mic          = true;                             break;
            case 'r': sample_rate      = std::stoi(optarg);                break;
            case 't': { int ti = std::stoi(optarg);
                        if (ti>=0 && ti<(int)Theme::COUNT) cfg.theme=ti; } break;
            case 'f': target_fps       = std::max(1, std::stoi(optarg));   break;
            case 'w': force_auto_width = true;                             break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    const int channels = cfg.stereo ? 2 : 1;

    FFTProcessor fft(sample_rate, channels);
    fft.setSensitivity(cfg.sensitivity);

    auto make_cb = [&]() -> AudioCapture::AudioCallback {
        return [&](const std::vector<float>& s, int ch) { fft.addSamples(s, ch); };
    };

    // ── Audio source selection ────────────────────────────────────────────────
    std::string active_source;
    std::unique_ptr<AudioCapture> audio;

    if (!cli_source.empty()) {
        active_source = cli_source;
        audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
    } else if (use_mic) {
        active_source = "";
        audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
    } else {
        // 1. Empty source — PipeWire's stream.capture.sink (system loopback)
        active_source = "";
        audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
        // 2. Saved source (PulseAudio or explicit preference)
        if (!audio && !cfg.last_source.empty()) {
            active_source = cfg.last_source;
            audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
        }
        // 3. Shell-detected monitor (PulseAudio fallback)
        if (!audio) {
            active_source = detectMonitor();
            if (!active_source.empty())
                audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
        }
        // 4. Last resort: empty again
        if (!audio) {
            active_source = "";
            audio = makeAudio(backend, active_source, sample_rate, channels, make_cb());
        }
    }

    if (!audio) { fprintf(stderr, "cava-viz: no audio backend started.\n"); return 1; }
    if (!active_source.empty()) cfg.last_source = active_source;

    // ── Renderer ──────────────────────────────────────────────────────────────
    Renderer renderer;
    if (!renderer.init()) { audio->stop(); return 1; }
    renderer.setTheme(static_cast<Theme>(cfg.theme));
    renderer.setGapWidth(cfg.gap_width);
    // Always restore the saved bar width. -w flag overrides with auto-computed width.
    renderer.setBarWidth(force_auto_width ? renderer.autoBarWidth() : cfg.bar_width);
    fft.setAutoSens(cfg.auto_sens);
    renderer.notifyChange();

    const std::string bname = audio->backendName();

    // ── inotify: live config reload (Linux only) ──────────────────────────────
    const std::string cfg_path = Config::configPath();
    const std::string cfg_dir  = cfg_path.substr(0, cfg_path.rfind('/'));
    const std::string cfg_name = cfg_path.substr(cfg_path.rfind('/') + 1);

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

    const us budget(1'000'000 / target_fps);
    double fps  = (double)target_fps;
    int fcount  = 0, frames = 0;
    auto fps_tp = Clock::now();
    const int WATCH = target_fps * 2;

    while (g_running.load()) {
        const auto t0 = Clock::now();
        ++frames;

        // ── Resize ────────────────────────────────────────────────────────────
        if (g_resize.exchange(false)) {
            renderer.handleResize();
        }

        // ── Live config reload ────────────────────────────────────────────────
#ifdef __linux__
        if (inotify_fd >= 0) {
            alignas(struct inotify_event)
            char ibuf[sizeof(struct inotify_event) + NAME_MAX + 1];
            ssize_t ilen;
            while ((ilen = read(inotify_fd, ibuf, sizeof(ibuf))) > 0) {
                for (const char* ptr = ibuf; ptr < ibuf + ilen; ) {
                    const auto* ev =
                        reinterpret_cast<const struct inotify_event*>(ptr);
                    if (ev->len > 0 && cfg_name == ev->name) {
                        Config nc;
                        if (nc.load()) {
                            cfg = nc;
                            renderer.setTheme(static_cast<Theme>(cfg.theme));
                            renderer.setGapWidth(cfg.gap_width);
                            if (!force_auto_width)
                                renderer.setBarWidth(cfg.bar_width);
                            fft.setSensitivity(cfg.sensitivity);
                            fft.setAutoSens(cfg.auto_sens);
                            renderer.notifyChange();
                        }
                        break;
                    }
                    ptr += sizeof(struct inotify_event) + ev->len;
                }
            }
        }
#endif

        // ── Silent watchdog ───────────────────────────────────────────────────
        if (!use_mic && cli_source.empty() && frames % WATCH == 0) {
            bool reconnect = (audio && audio->hasFailed());
            if (!reconnect && !active_source.empty()) {
                std::string cur = detectMonitor();
                if (!cur.empty() && cur != active_source) reconnect = true;
            }
            if (reconnect) {
                if (audio) { audio->stop(); audio.reset(); }
                for (const std::string& src : {std::string(""), detectMonitor(), active_source}) {
                    audio = makeAudio(backend, src, sample_rate, channels, make_cb());
                    if (audio) {
                        if (!src.empty()) { active_source = src; cfg.last_source = src; }
                        break;
                    }
                }
            }
        }

        // ── Input ─────────────────────────────────────────────────────────────
        wtimeout(stdscr, 4);
        const int ch = getch();

        // Only process if ncurses returned a real key (not ERR or a stray
        // byte from a partial escape sequence).  KEY_* constants are > 255;
        // printable ASCII we care about is < 128.  Anything else is noise.
        if (ch == ERR) goto next_frame;

        switch (ch) {
            case 'q':
                g_running.store(false);
                break;
            case 't':
                cfg.theme = (int)renderer.nextTheme();
                break;
            case 'g':
                cfg.gap_width = renderer.cycleGap();
                break;
            case ']':
                cfg.bar_width = renderer.increaseBarWidth();
                break;
            case '[':
                cfg.bar_width = renderer.decreaseBarWidth();
                break;
            case KEY_UP: {
                float s = fft.increaseSensitivity();
                cfg.sensitivity = s; cfg.auto_sens = false;
                fft.setAutoSens(false);
                renderer.notifyChange();
                break;
            }
            case KEY_DOWN: {
                float s = fft.decreaseSensitivity();
                cfg.sensitivity = s; cfg.auto_sens = false;
                fft.setAutoSens(false);
                renderer.notifyChange();
                break;
            }
            case 'a':
                cfg.auto_sens = !cfg.auto_sens;
                fft.setAutoSens(cfg.auto_sens);
                renderer.notifyChange();
                break;
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
        renderer.render(fft.barsL(), fft.barsR(), fps, bname,
                        fft.sensitivity(), cfg.auto_sens);

        // ── FPS tracking ──────────────────────────────────────────────────────
        ++fcount;
        const auto   now  = Clock::now();
        const double secs = Duration(now - fps_tp).count();
        if (secs >= 1.0) { fps = fcount / secs; fcount = 0; fps_tp = now; }

        // ── Frame limiter ─────────────────────────────────────────────────────
        const auto elapsed = Clock::now() - t0;
        if (elapsed < budget) std::this_thread::sleep_for(budget - elapsed);
    }

    if (audio) audio->stop();
#ifdef __linux__
    if (inotify_fd >= 0) close(inotify_fd);
#endif
    cfg.save();
    return 0;
}
