#include "app.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits.h>   // NAME_MAX
#include <thread>

#include <signal.h>
#include <sys/inotify.h>
#include <unistd.h>

#ifdef HAVE_PIPEWIRE
#  include "pipewire_capture.h"
#endif
#ifdef HAVE_PULSEAUDIO
#  include "pulse_capture.h"
#endif

// ── Global signal flags ───────────────────────────────────────────────────────
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_resize{false};

static void sigHandler(int sig) {
    if (sig == SIGWINCH)
        g_resize.store(true, std::memory_order_relaxed);
    else
        g_running.store(false, std::memory_order_relaxed);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
App::App() = default;

App::~App() {
    if (audio_) audio_->stop();
    if (inotify_fd_ >= 0) close(inotify_fd_);
    cfg_.saveState();
}

// ── CLI ───────────────────────────────────────────────────────────────────────
void App::printUsage(const char *prog) const {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -b <backend>   Audio backend: auto | pipewire | pulseaudio\n"
        "  -s <source>    Audio source name (device/monitor)\n"
        "  -m             Capture microphone instead of loopback\n"
        "  -w             Auto bar width based on terminal columns\n"
        "  -h             Show this help\n"
        "\n"
        "Keys (while running):\n"
        "  t / T          Next / previous theme\n"
        "  + / -          Increase / decrease sensitivity\n"
        "  a              Toggle auto-sensitivity\n"
        "  [ / ]          Decrease / increase bar width\n"
        "  g              Cycle gap width (0 → 1 → 2 → 0)\n"
        "  c              Toggle colour cycle\n"
        "  b              Toggle per-bar colour mode\n"
        "  h              Toggle HUD pin (always visible)\n"
        "  q / ESC        Quit\n"
        "\n"
        "Config: %s\n"
        "  Edit while running — reloaded instantly via inotify.\n\n",
        prog, Config::configPath().c_str());
}

bool App::parseArgs(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printUsage(argv[0]);
            return false;
        } else if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            backend_arg_ = argv[++i];
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            cli_source_ = argv[++i];
        } else if (!strcmp(argv[i], "-m")) {
            use_mic_ = true;
        } else if (!strcmp(argv[i], "-w")) {
            force_auto_width_ = true;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}

// ── Audio ─────────────────────────────────────────────────────────────────────
std::string App::detectMonitor() {
    static std::string cached;
    if (!cached.empty()) return cached;
#ifdef HAVE_PULSEAUDIO
    FILE *f = popen(
        "pactl list short sources 2>/dev/null | awk '/monitor/{print $2; exit}'", "r");
    if (f) {
        char buf[256] = {};
        if (std::fgets(buf, sizeof(buf), f))
            cached = std::string(buf, strcspn(buf, "\n"));
        pclose(f);
    }
#endif
    return cached;
}

bool App::startAudio() {
    if (audio_) audio_->stop();
    audio_.reset();

    std::string src = cli_source_;
    if (src.empty() && !use_mic_) {
        src = cfg_.last_source;
        if (src.empty()) src = detectMonitor();
    }
    active_source_ = src;
    channels_      = cfg_.stereo ? 2 : 1;

    // Capture callback: push PCM samples into the FFT processor.
    // Called from the audio capture thread — FFTProcessor::addSamples is mutex-safe.
    auto cb = [this](const std::vector<float> &samples, int ch) {
        if (fft_) fft_->addSamples(samples, ch);
    };

    auto tryBackend = [&](std::unique_ptr<AudioCapture> cap) -> bool {
        if (!cap->init(src, sample_rate_, channels_)) return false;
        if (!cap->start(cb))                          return false;
        backend_name_ = cap->backendName();
        audio_        = std::move(cap);
        return true;
    };

    bool ok = false;
#ifdef HAVE_PIPEWIRE
    if (!ok && (backend_arg_ == "auto" || backend_arg_ == "pipewire"))
        ok = tryBackend(std::make_unique<PipeWireCapture>());
#endif
#ifdef HAVE_PULSEAUDIO
    if (!ok && (backend_arg_ == "auto" || backend_arg_ == "pulseaudio"))
        ok = tryBackend(std::make_unique<PulseAudioCapture>());
#endif
    if (!ok) {
        std::fprintf(stderr,
            "cava-viz: no audio backend could open source \"%s\".\n"
            "  Try:  viz -b pipewire   or   viz -b pulseaudio\n",
            src.c_str());
        return false;
    }
    renderer_.setSourceName(active_source_);
    cfg_.last_source = active_source_;
    return true;
}

// ── inotify config reload ─────────────────────────────────────────────────────
void App::initInotify() {
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) return;

    const std::string dir  = Config::configPath();
    const std::string parent = dir.substr(0, dir.rfind('/'));
    inotify_add_watch(inotify_fd_, parent.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);
}

void App::checkReload() {
    if (inotify_fd_ < 0) return;

    alignas(struct inotify_event)
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

    bool changed = false;
    while (true) {
        ssize_t n = read(inotify_fd_, buf, sizeof(buf));
        if (n <= 0) break;
        const auto *ev = reinterpret_cast<const struct inotify_event *>(buf);
        const std::string path = Config::configPath();
        const std::string base = path.substr(path.rfind('/') + 1);
        if (ev->len > 0 && base == ev->name)
            changed = true;
    }
    if (!changed) return;

    Config fresh;
    if (!fresh.load()) return;

    cfg_.theme          = fresh.theme;
    cfg_.bar_width      = fresh.bar_width;
    cfg_.gap_width      = fresh.gap_width;
    cfg_.hud_pinned     = fresh.hud_pinned;
    cfg_.colour_cycle   = fresh.colour_cycle;
    cfg_.per_bar_colour = fresh.per_bar_colour;
    cfg_.gravity        = fresh.gravity;
    cfg_.monstercat     = fresh.monstercat;
    cfg_.rise_factor    = fresh.rise_factor;
    cfg_.bass_smooth    = fresh.bass_smooth;
    cfg_.noise_gate     = fresh.noise_gate;
    cfg_.a_weighting    = fresh.a_weighting;
    cfg_.sensitivity    = fresh.sensitivity;
    cfg_.auto_sens      = fresh.auto_sens;
    cfg_.fps            = fresh.fps;

    applyRendererConfig();
    applyFFTConfig();
    renderer_.showFeedback("Config reloaded");
}

// ── Apply config ──────────────────────────────────────────────────────────────
void App::applyRendererConfig() {
    renderer_.setTheme(static_cast<Theme>(cfg_.theme));
    renderer_.setBarWidth(cfg_.bar_width);
    renderer_.setGapWidth(cfg_.gap_width);
    renderer_.setHudPinned(cfg_.hud_pinned);
    renderer_.setColourCycle(cfg_.colour_cycle);
    renderer_.setPerBarColour(cfg_.per_bar_colour);
}

void App::applyFFTConfig() {
    if (!fft_) return;
    fft_->setGravity(cfg_.gravity);
    fft_->setMonstercat(cfg_.monstercat);
    fft_->setRiseFactor(cfg_.rise_factor);
    fft_->setBassSmooth(cfg_.bass_smooth);
    fft_->setNoiseGate(cfg_.noise_gate);
    fft_->setAWeighting(cfg_.a_weighting);
    fft_->setHighCutoff(cfg_.high_cutoff);
    fft_->setSensitivity(cfg_.sensitivity);
    fft_->setAutoSens(cfg_.auto_sens);
}

// ── Watchdog ──────────────────────────────────────────────────────────────────
void App::checkWatchdog(int target_fps) {
    static int tick = 0;
    if (++tick < target_fps) return; // run once per second
    tick = 0;

    if (audio_ && audio_->hasFailed()) {
        renderer_.showFeedback("Audio disconnected — reconnecting…");
        startAudio();
        return;
    }
    if (silent_frames_ >= target_fps * 5) {
        silent_frames_ = 0;
        renderer_.showFeedback("No audio — retrying source…");
        startAudio();
    }
}

// ── Input ─────────────────────────────────────────────────────────────────────
bool App::handleInput(int ch, int /*target_fps*/) {
    switch (ch) {
    case 'q': case 'Q': case 27: return false;

    case 't':
        renderer_.nextTheme();
        cfg_.theme = (int)renderer_.theme();
        cfg_.save();
        break;
    case 'T': {
        int prev = ((int)renderer_.theme() - 1 + (int)Theme::COUNT) % (int)Theme::COUNT;
        renderer_.setTheme(static_cast<Theme>(prev));
        cfg_.theme = prev;
        cfg_.save();
        break;
    }
    case '+': case '=':
        cfg_.auto_sens = false;
        if (fft_) cfg_.sensitivity = fft_->increaseSensitivity();
        renderer_.showFeedback("Sens: " + std::to_string(cfg_.sensitivity).substr(0, 4));
        cfg_.save();
        break;
    case '-':
        cfg_.auto_sens = false;
        if (fft_) cfg_.sensitivity = fft_->decreaseSensitivity();
        renderer_.showFeedback("Sens: " + std::to_string(cfg_.sensitivity).substr(0, 4));
        cfg_.save();
        break;
    case 'a':
        cfg_.auto_sens = !cfg_.auto_sens;
        if (fft_) fft_->setAutoSens(cfg_.auto_sens);
        renderer_.showFeedback(cfg_.auto_sens ? "Auto-sens On" : "Auto-sens Off");
        cfg_.save();
        break;
    case '[':
        renderer_.decreaseBarWidth();
        cfg_.bar_width = renderer_.barWidth();
        cfg_.save();
        break;
    case ']':
        renderer_.increaseBarWidth();
        cfg_.bar_width = renderer_.barWidth();
        cfg_.save();
        break;
    case 'g':
        renderer_.cycleGap();
        cfg_.gap_width = renderer_.gapWidth();
        cfg_.save();
        break;
    case 'c':
        renderer_.toggleColourCycle();
        cfg_.colour_cycle = renderer_.colourCycle();
        cfg_.save();
        break;
    case 'b':
        renderer_.togglePerBarColour();
        cfg_.per_bar_colour = renderer_.perBarColour();
        cfg_.save();
        break;
    case 'h':
        renderer_.toggleHudPin();
        cfg_.hud_pinned = renderer_.hudPinned();
        cfg_.save();
        break;
    default: break;
    }
    return true;
}

// ── run ───────────────────────────────────────────────────────────────────────
int App::run(int argc, char *argv[]) {
    if (!parseArgs(argc, argv)) return 0;

    cfg_.load();
    cfg_.loadState();

    if (!renderer_.init()) {
        std::fprintf(stderr, "cava-viz: failed to initialise terminal.\n");
        return 1;
    }

    if (force_auto_width_)
        cfg_.bar_width = renderer_.autoBarWidth();
    applyRendererConfig();

    // Signals
    struct sigaction sa{};
    sa.sa_handler = sigHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM,  &sa, nullptr);
    sigaction(SIGINT,   &sa, nullptr);
    sigaction(SIGWINCH, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    if (!startAudio()) { endwin(); return 1; }

    // Build FFT processor — num_bars is passed each frame to execute(),
    // so there is no separate resize step.
    fft_ = std::make_unique<FFTProcessor>(sample_rate_, channels_);
    applyFFTConfig();

    initInotify();

    auto fps_ref  = std::chrono::steady_clock::now();
    auto fps_tick = fps_ref;
    int  fcount   = 0;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (g_running.load(std::memory_order_relaxed)) {

        // Resize
        if (g_resize.exchange(false, std::memory_order_relaxed)) {
            renderer_.handleResize();
            if (force_auto_width_) {
                cfg_.bar_width = renderer_.autoBarWidth();
                renderer_.setBarWidth(cfg_.bar_width);
            }
        }

        checkReload();

        const int target_fps = std::clamp(cfg_.fps, 10, 240);
        checkWatchdog(target_fps);

        int ch;
        while ((ch = getch()) != ERR)
            if (!handleInput(ch, target_fps))
                g_running.store(false, std::memory_order_relaxed);

        // ── FFT execute ───────────────────────────────────────────────────────
        // execute() is non-blocking: it processes whatever samples addSamples()
        // has deposited in the ring buffer and returns the smoothed bars.
        // Passing num_bars each frame means bar-count changes (resize, width
        // toggle) are handled automatically with no separate resize call.
        const int num_bars = renderer_.barCount();
        std::vector<float> bars_l, bars_r;

        if (fft_ && fft_->execute(num_bars, (float)fps_)) {
            bars_l = fft_->barsL();
            bars_r = fft_->barsR();

            // Track silence for watchdog
            float peak = 0.f;
            for (float v : bars_l) peak = std::max(peak, v);
            for (float v : bars_r) peak = std::max(peak, v);
            if (peak < 0.005f) ++silent_frames_;
            else               silent_frames_ = 0;

            // Keep cfg_ sensitivity in sync with FFT's auto-adjusted value
            // so the HUD and config file reflect the live operating point.
            cfg_.sensitivity = fft_->sensitivity();
        }

        renderer_.render(bars_l, bars_r, fps_,
                         backend_name_, cfg_.sensitivity, cfg_.auto_sens);

        // ── FPS limiter ───────────────────────────────────────────────────────
        const auto frame_dur = std::chrono::microseconds(1'000'000 / target_fps);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - fps_tick;
        if (elapsed < frame_dur)
            std::this_thread::sleep_for(frame_dur - elapsed);
        fps_tick = std::chrono::steady_clock::now();

        // Update displayed FPS once per second
        ++fcount;
        if (fps_tick - fps_ref >= std::chrono::seconds(1)) {
            fps_    = fcount /
                      std::chrono::duration<double>(fps_tick - fps_ref).count();
            fcount  = 0;
            fps_ref = fps_tick;
        }
    }

    cfg_.save();
    return 0;
}
