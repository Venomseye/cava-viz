#ifdef HAVE_PULSEAUDIO
#include "pulse_capture.h"

#include <cstdio>
#include <pulse/error.h>
#include <pulse/sample.h>
#include <pulse/simple.h>

// Query default sink monitor via pactl.
// Returns "sinkname.monitor" on success, "" on failure.
static std::string pulseDefaultMonitor() {
    // Method 1: get-default-sink  (fastest, works on PA and pipewire-pulse)
    {
        FILE* fp = popen("pactl get-default-sink 2>/dev/null", "r");
        if (fp) {
            char buf[256] = {};
            bool ok = (std::fgets(buf, sizeof(buf), fp) != nullptr);
            pclose(fp);
            if (ok) {
                std::string s = buf;
                while (!s.empty() &&
                       (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                    s.pop_back();
                if (!s.empty()) return s + ".monitor";
            }
        }
    }

    // Method 2: enumerate sources and pick the first *.monitor
    // FIX: added as fallback for systems where get-default-sink is unavailable
    // or returns nothing (e.g. some minimal installs without pipewire-pulse).
    {
        FILE* fp = popen(
            "pactl list short sources 2>/dev/null"
            " | awk '/\\.monitor/{print $2; exit}'", "r");
        if (fp) {
            char buf[256] = {};
            bool ok = (std::fgets(buf, sizeof(buf), fp) != nullptr);
            pclose(fp);
            if (ok) {
                std::string s = buf;
                while (!s.empty() &&
                       (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                    s.pop_back();
                if (!s.empty()) return s;
            }
        }
    }

    return "";
}

PulseAudioCapture::PulseAudioCapture() = default;
PulseAudioCapture::~PulseAudioCapture() { stop(); }

bool PulseAudioCapture::init(const std::string& src, int sr, int ch) {
    if (src.empty()) {
        // Auto-detect: find the default output monitor so we capture system
        // audio rather than the microphone.
        source_ = pulseDefaultMonitor();
        // NOTE: if detection fails source_ stays empty.  pa_simple_new with
        // a NULL device for PA_STREAM_RECORD uses the server's default SOURCE,
        // which is typically the microphone.  We still proceed and let start()
        // attempt the connection; the caller's fallback chain will try other
        // backends if this ultimately produces no audio.
    } else {
        source_ = src;
    }
    sample_rate_ = sr;
    channels_    = ch;
    failed_.store(false);
    return true;
}

bool PulseAudioCapture::start(AudioCallback cb) {
    if (running_.load()) return false;
    failed_.store(false);

    pa_sample_spec ss{};
    ss.format   = PA_SAMPLE_FLOAT32LE;
    ss.rate     = static_cast<uint32_t>(sample_rate_);
    ss.channels = static_cast<uint8_t>(channels_);

    int err = 0;
    handle_ = pa_simple_new(
        nullptr, "cava-viz", PA_STREAM_RECORD,
        source_.empty() ? nullptr : source_.c_str(),
        "capture", &ss, nullptr, nullptr, &err);

    if (!handle_) {
        failed_.store(true);
        return false;
    }

    running_.store(true);
    thread_ = std::thread(&PulseAudioCapture::captureLoop, this, std::move(cb));
    return true;
}

void PulseAudioCapture::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    if (handle_) { pa_simple_free(handle_); handle_ = nullptr; }
}

void PulseAudioCapture::captureLoop(AudioCallback cb) {
    const int n = kChunkFrames * channels_;
    std::vector<float> buf(n);

    while (running_.load()) {
        int err = 0;
        if (pa_simple_read(handle_, buf.data(),
                           (size_t)n * sizeof(float), &err) < 0) {
            failed_.store(true);
            break;
        }
        cb(buf, channels_);
    }
    if (running_.load()) failed_.store(true);
    running_.store(false);
}

#endif // HAVE_PULSEAUDIO
