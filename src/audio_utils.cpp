#include "audio_utils.h"

#ifdef HAVE_PULSEAUDIO
#  include "pulse_capture.h"
#endif
#ifdef HAVE_PIPEWIRE
#  include "pipewire_capture.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

// ── Shell utility ─────────────────────────────────────────────────────────────

std::string runCmd(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return "";
    char buf[256] = {};
    const bool ok = (std::fgets(buf, sizeof(buf), fp) != nullptr);
    pclose(fp);
    if (!ok) return "";
    std::string s = buf;
    while (!s.empty() && (s.back()=='\n'||s.back()=='\r'||s.back()==' '))
        s.pop_back();
    return s;
}

// ── Monitor detection ─────────────────────────────────────────────────────────

static constexpr double MONITOR_POLL_SECS = 5.0;

std::string detectMonitor() {
    using Clock = std::chrono::steady_clock;
    static std::string       cached;
    static Clock::time_point last_call = Clock::time_point{};
    const auto now = Clock::now();
    if (std::chrono::duration<double>(now - last_call).count() < MONITOR_POLL_SECS)
        return cached;
    last_call = now;
    const std::string sink = runCmd("pactl get-default-sink 2>/dev/null");
    cached = sink.empty()
           ? runCmd("pactl list short sources 2>/dev/null | awk '/\\.monitor/{print $2;exit}'")
           : (sink + ".monitor");
    return cached;
}

// ── Audio backend factory ─────────────────────────────────────────────────────

std::unique_ptr<AudioCapture> makeAudio(
        const std::string& backend, const std::string& source,
        int sr, int ch, AudioCapture::AudioCallback cb)
{
#if defined(HAVE_PIPEWIRE) || defined(HAVE_PULSEAUDIO)
    auto try1 = [&](std::unique_ptr<AudioCapture> cap) -> std::unique_ptr<AudioCapture> {
        if (!cap->init(source, sr, ch)) return nullptr;
        if (!cap->start(cb))            return nullptr;
        return cap;
    };
#endif
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
    // Suppress "unused parameter" warnings when both backends are disabled.
    (void)backend; (void)source; (void)sr; (void)ch; (void)cb;
    return a;
}

// ── Audio startup with fallback chain ────────────────────────────────────────

void doStartAudio(
    const std::string& backend, const std::string& cli_source,
    bool use_mic, int sample_rate, int channels,
    FFTProcessor& fft, Config& cfg,
    std::unique_ptr<AudioCapture>& audio,
    std::string& active_source, std::string& bname)
{
    AudioCapture::AudioCallback cb =
        [&fft](const std::vector<float>& s, int ch) {
            fft.addSamples(s, ch);
        };

    if (!cli_source.empty()) {
        active_source = cli_source;
        audio = makeAudio(backend, active_source, sample_rate, channels, cb);
    } else if (use_mic) {
        active_source = "";
        audio = makeAudio(backend, active_source, sample_rate, channels, cb);
        if (!audio) {
            active_source = "default";
            audio = makeAudio(backend, active_source, sample_rate, channels, cb);
        }
    } else {
        active_source = "";
        audio = makeAudio(backend, active_source, sample_rate, channels, cb);
        if (!audio && !cfg.last_source.empty()) {
            active_source = cfg.last_source;
            audio = makeAudio(backend, active_source, sample_rate, channels, cb);
        }
        if (!audio) {
            active_source = detectMonitor();
            if (!active_source.empty())
                audio = makeAudio(backend, active_source, sample_rate, channels, cb);
        }
        if (!audio) {
            active_source = "";
            audio = makeAudio(backend, active_source, sample_rate, channels, cb);
        }
    }

    if (audio) {
        bname = audio->backendName();
        if (!active_source.empty() && !use_mic) {
            cfg.last_source = active_source;
            cfg.saveState();
        }
    }
}

// ── FFT configuration ─────────────────────────────────────────────────────────

void applyFFTConfig(FFTProcessor& fft, const Config& cfg) {
    fft.setSensitivity(cfg.sensitivity);
    fft.setAutoSens(cfg.auto_sens);
    fft.setGravity(cfg.gravity);
    fft.setMonstercat(cfg.monstercat);
    fft.setHighCutoff(cfg.high_cutoff);
    fft.setRiseFactor(cfg.rise_factor);
    fft.setBassSmooth(cfg.bass_smooth);
    fft.setAWeighting(cfg.a_weighting);
    fft.setNoiseGate(cfg.noise_gate);
    fft.setAutoMono(cfg.auto_mono);
}
