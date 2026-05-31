#include "bar_mode.h"
#include "audio_utils.h"
#include "fft_processor.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <memory>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#  include <time.h>
#endif

// ── Frame timing ──────────────────────────────────────────────────────────────
// Use clock_nanosleep (absolute deadline) on Linux for low drift.
// Fall back to std::this_thread::sleep_for on other platforms.

#ifdef __linux__
static void sleepUntil(struct timespec& deadline, long frame_ns) {
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
    deadline.tv_nsec += frame_ns;
    if (deadline.tv_nsec >= 1'000'000'000L) {
        deadline.tv_nsec -= 1'000'000'000L;
        deadline.tv_sec  += 1;
    }
    // If we're more than one frame behind, snap forward to avoid spiral lag.
    struct timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    const long long now_ns  = (long long)now.tv_sec * 1'000'000'000LL + now.tv_nsec;
    const long long dead_ns = (long long)deadline.tv_sec * 1'000'000'000LL + deadline.tv_nsec;
    if (dead_ns < now_ns) {
        deadline.tv_sec  = now.tv_sec;
        deadline.tv_nsec = now.tv_nsec + frame_ns;
        if (deadline.tv_nsec >= 1'000'000'000L) {
            deadline.tv_nsec -= 1'000'000'000L;
            deadline.tv_sec  += 1;
        }
    }
}
#else
#  include <chrono>
static void sleepUntil(std::chrono::steady_clock::time_point& deadline,
                       long frame_ns) {
    std::this_thread::sleep_until(deadline);
    deadline += std::chrono::nanoseconds(frame_ns);
    const auto now = std::chrono::steady_clock::now();
    if (deadline < now)
        deadline = now + std::chrono::nanoseconds(frame_ns);
}
#endif

// ── Bar extraction ────────────────────────────────────────────────────────────

// Resize/pad an FFT output vector to exactly n bars.
static std::vector<float> trimPad(const std::vector<float>& src, int n) {
    std::vector<float> out(n, 0.f);
    const int copy = std::min((int)src.size(), n);
    std::copy(src.begin(), src.begin() + copy, out.begin());
    return out;
}

// ── runBarMode ────────────────────────────────────────────────────────────────

int runBarMode(
    const std::string& backend,
    const std::string& cli_source,
    bool               use_mic,
    int                sample_rate,
    Config&            cfg,
    const BarOutputConfig& bar_cfg,
    std::atomic<bool>& running)
{
    // Ignore SIGPIPE so broken FIFO/socket writes return EPIPE instead of
    // killing the process — we handle reconnection ourselves.
    signal(SIGPIPE, SIG_IGN);

    // ── FFT ───────────────────────────────────────────────────────────────────
    const int channels = cfg.stereo ? 2 : 1;
    FFTProcessor fft(sample_rate, channels);
    applyFFTConfig(fft, cfg);

    // ── Audio ─────────────────────────────────────────────────────────────────
    std::unique_ptr<AudioCapture> audio;
    std::string active_source;
    std::string bname;

    doStartAudio(backend, cli_source, use_mic, sample_rate, channels,
                 fft, cfg, audio, active_source, bname);

    if (!audio) {
        std::fprintf(stderr,
            "cava-viz: bar mode: failed to open any audio source.\n");
        return 1;
    }
    std::fprintf(stderr, "cava-viz: bar mode  source=%s  backend=%s  "
                 "format=%d  fps=%d  count=%d\n",
                 active_source.empty() ? "(default)" : active_source.c_str(),
                 bname.c_str(), (int)bar_cfg.format,
                 bar_cfg.fps, bar_cfg.count);

    // ── Sink ──────────────────────────────────────────────────────────────────
    BarWriter writer;
    if (!writer.open(bar_cfg)) {
        std::fprintf(stderr, "cava-viz: bar mode: failed to open output sink.\n");
        audio->stop();
        return 1;
    }

    // ── Timing ───────────────────────────────────────────────────────────────
    const int  fps      = std::max(1, bar_cfg.fps);
    const long frame_ns = 1'000'000'000L / fps;

#ifdef __linux__
    struct timespec deadline{};
    clock_gettime(CLOCK_MONOTONIC, &deadline);
#else
    auto deadline = std::chrono::steady_clock::now();
#endif

    // ── Render loop ───────────────────────────────────────────────────────────
    int silent_frames = 0;

    while (running.load()) {
        // Compute one FFT frame.
        fft.execute(bar_cfg.count, (float)fps);

        const std::vector<float> bl = trimPad(fft.barsL(), bar_cfg.count);
        const std::vector<float> br = (channels == 2)
                                    ? trimPad(fft.barsR(), bar_cfg.count)
                                    : std::vector<float>{};

        // Simple silence watchdog: if every bar has been zero for 10 seconds,
        // try to reconnect to the audio source.
        const bool all_silent = std::all_of(bl.begin(), bl.end(),
                                            [](float v){ return v < 1e-4f; });
        silent_frames = all_silent ? silent_frames + 1 : 0;

        const int reconnect_frames = fps * 10;
        if (silent_frames > reconnect_frames) {
            silent_frames = 0;
            audio->stop();
            audio.reset();
            doStartAudio(backend, cli_source, use_mic, sample_rate, channels,
                         fft, cfg, audio, active_source, bname);
            if (!audio) {
                std::fprintf(stderr, "cava-viz: bar mode: reconnect failed.\n");
            }
        }

        // Format and write the frame.
        const std::string frame = formatBars(bl, br, bar_cfg);
        if (!writer.write(frame)) {
            // FIFO reader disconnected — wait for a new one.
            if (writer.sinkType() == BarSink::Fifo) {
                std::fprintf(stderr,
                    "cava-viz: FIFO reader disconnected, waiting for reconnect...\n");
                if (!writer.reopen()) {
                    std::fprintf(stderr,
                        "cava-viz: failed to reopen FIFO, exiting.\n");
                    break;
                }
            } else {
                break;  // stdout/socket failure is fatal
            }
        }

        sleepUntil(deadline, frame_ns);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    writer.close();
    if (audio) audio->stop();
    return 0;
}
