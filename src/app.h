#pragma once
#include "audio_capture.h"
#include "config.h"
#include "fft_processor.h"
#include "renderer.h"

#include <memory>
#include <string>

// ── App ───────────────────────────────────────────────────────────────────────
//
// Owns every runtime resource (Config, Renderer, FFTProcessor, AudioCapture)
// and runs the main event loop.  main() creates one App and calls run().
//
// Design goals
//   • main() is trivial — no logic, just run().
//   • Each concern lives in its own method; the loop body is a clean sequence
//     of named steps (resize, reload, watchdog, input, compute, render, sleep).
//   • Signal atomics (g_running, g_resize) remain global so signal handlers
//     can write them from any signal context; App reads them each frame.
//
class App {
public:
    App();
    ~App();

    // Parse argv, initialise all subsystems, run the loop until quit.
    // Returns an exit code suitable for main().
    int run(int argc, char *argv[]);

private:
    // ── Owned resources ───────────────────────────────────────────────────────
    Config                        cfg_;
    Renderer                      renderer_;
    std::unique_ptr<FFTProcessor> fft_;
    std::unique_ptr<AudioCapture> audio_;

    // ── CLI / session state ───────────────────────────────────────────────────
    std::string backend_arg_      {"auto"};
    std::string cli_source_;
    std::string active_source_;
    std::string backend_name_;
    int         sample_rate_      {44100};
    int         channels_         {2};
    bool        use_mic_          {false};
    bool        force_auto_width_ {false};

    // ── inotify config-watch ──────────────────────────────────────────────────
    int  inotify_fd_ {-1};
    void initInotify();
    void checkReload();  // drain inotify events; apply config if file changed

    // ── Frame accounting ──────────────────────────────────────────────────────
    double fps_          {60.0};
    int    fcount_       {0};
    int    frames_       {0};
    int    silent_frames_{0};

    // ── Helpers ───────────────────────────────────────────────────────────────
    bool parseArgs(int argc, char *argv[]);
    void printUsage(const char *prog) const;

    // Start (or restart) the audio capture pipeline.
    // Returns true if a capture object is live after the call.
    bool startAudio();

    // Push current cfg_ values into the FFT processor.
    void applyFFTConfig();

    // Push current cfg_ values into the renderer (theme, gap, width, …).
    void applyRendererConfig();

    // Reconnect audio if the active device disappeared or went silent.
    void checkWatchdog(int target_fps);

    // Handle one ncurses key.  Returns false to request quit.
    bool handleInput(int ch, int target_fps);

    // Monitor-source detection (throttled, cached).
    static std::string detectMonitor();
};
