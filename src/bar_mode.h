#pragma once
#include "bar_output.h"
#include "config.h"
#include <atomic>
#include <string>

/// Run the headless bar-mode loop: audio capture → FFT → format → sink.
///
/// Returns 0 on clean exit (running set to false via SIGINT/SIGTERM).
/// Returns 1 if the audio backend fails to initialise.
///
/// The loop runs at bar_cfg.fps and writes one formatted line per frame.
/// FIFO sink: blocks in open() until a reader connects, then streams.
/// Socket sink: broadcasts to all connected Unix socket clients each frame.
///
/// Parameters mirror main()'s audio startup:
///   backend     — "auto", "pipewire", or "pulse"
///   cli_source  — explicit capture device, or "" for auto-detection
///   use_mic     — capture from microphone rather than a monitor source
///   sample_rate — PCM sample rate in Hz
///   cfg         — full config (FFT knobs read from here; last_source updated)
///   bar_cfg     — bar count, format, sink, colours, fps, stereo mode
///   running     — shared stop flag (set false by the signal handler in main)
int runBarMode(
    const std::string& backend,
    const std::string& cli_source,
    bool               use_mic,
    int                sample_rate,
    Config&            cfg,
    const BarOutputConfig& bar_cfg,
    std::atomic<bool>& running);
