#pragma once
#include "audio_capture.h"
#include "config.h"
#include "fft_processor.h"
#include <memory>
#include <string>
#include <vector>

// ── Subprocess helper
// ─────────────────────────────────────────────────────────────

/// Run a program (no shell) and return its complete stdout.
/// stdin/stderr are /dev/null.  Returns "" if the program can't be started,
/// exits non-zero, or does not finish within timeout_ms (it is SIGKILLed and
/// reaped in that case).  Output is capped at 64 KiB.
std::string runProcess(const std::vector<std::string> &args,
                       int timeout_ms = 1500);

// ── Source enumeration
// ────────────────────────────────────────────────────────

/// Return all PulseAudio/PipeWire source names visible to pactl.
/// Returns an empty vector when pactl is not on PATH or produces no output.
std::vector<std::string> listSources();

/// Print a formatted source table to stdout (for --list-sources).
/// Marks the default monitor and the last-used source from cfg.
void printSources(const Config &cfg);

// ── Monitor detection
// ─────────────────────────────────────────────────────────

/// Query the default monitor source RIGHT NOW via pactl (bounded by timeouts,
/// may block up to ~2 s in the worst case).  Returns "" if unavailable.
std::string queryDefaultMonitor();

/// Default PulseAudio/PipeWire monitor source.  The first call queries
/// synchronously; afterwards a background thread refreshes it every 5 s and
/// this returns the cached value immediately, so it is safe to call from the
/// render loop even if the sound server hangs.
std::string detectMonitor();

// ── Audio backend factory
// ─────────────────────────────────────────────────────

/// Try to init and start one audio backend.
/// Returns nullptr if the backend isn't compiled in, init fails, or start
/// fails.
std::unique_ptr<AudioCapture> makeAudio(const std::string &backend,
                                        const std::string &source, int sr,
                                        int ch, AudioCapture::AudioCallback cb);

// ── Audio startup with fallback chain ────────────────────────────────────────

/// Attempt to open an audio source using the following priority:
///   1. cli_source  (if non-empty)
///   2. use_mic     (empty source → "default")
///   3. cfg.last_source
///   4. detectMonitor()
///   5. empty source (let the backend pick)
///
/// On success, sets audio, active_source, bname, and persists cfg.last_source.
void doStartAudio(const std::string &backend, const std::string &cli_source,
                  bool use_mic, int sample_rate, int channels,
                  FFTProcessor &fft, Config &cfg,
                  std::unique_ptr<AudioCapture> &audio,
                  std::string &active_source, std::string &bname);

// ── FFT configuration
// ─────────────────────────────────────────────────────────

/// Apply all FFT-related Config knobs to a live FFTProcessor instance.
void applyFFTConfig(FFTProcessor &fft, const Config &cfg);
