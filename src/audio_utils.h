#pragma once
#include "audio_capture.h"
#include "config.h"
#include "fft_processor.h"
#include <memory>
#include <string>

// ── Shell utility ─────────────────────────────────────────────────────────────

/// Run a shell command and return the first line of stdout (trimmed).
/// Returns "" on failure or empty output.
std::string runCmd(const char* cmd);

// ── Monitor detection ─────────────────────────────────────────────────────────

/// Auto-detect the default PulseAudio/PipeWire monitor source via pactl.
/// Result is cached and re-queried at most once every 5 seconds.
std::string detectMonitor();

// ── Audio backend factory ─────────────────────────────────────────────────────

/// Try to init and start one audio backend.
/// Returns nullptr if the backend isn't compiled in, init fails, or start fails.
std::unique_ptr<AudioCapture> makeAudio(
    const std::string& backend, const std::string& source,
    int sr, int ch, AudioCapture::AudioCallback cb);

// ── Audio startup with fallback chain ────────────────────────────────────────

/// Attempt to open an audio source using the following priority:
///   1. cli_source  (if non-empty)
///   2. use_mic     (empty source → "default")
///   3. cfg.last_source
///   4. detectMonitor()
///   5. empty source (let the backend pick)
///
/// On success, sets audio, active_source, bname, and persists cfg.last_source.
void doStartAudio(
    const std::string& backend, const std::string& cli_source,
    bool use_mic, int sample_rate, int channels,
    FFTProcessor& fft, Config& cfg,
    std::unique_ptr<AudioCapture>& audio,
    std::string& active_source, std::string& bname);

// ── FFT configuration ─────────────────────────────────────────────────────────

/// Apply all FFT-related Config knobs to a live FFTProcessor instance.
void applyFFTConfig(FFTProcessor& fft, const Config& cfg);
