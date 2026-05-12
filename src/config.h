#pragma once
#include <string>

struct Config {
    // ── Visual ────────────────────────────────────────────────────────────────
    int   theme          = 0;       // 0-11
    int   bar_width      = 2;       // 1-8
    int   gap_width      = 1;       // 0-2
    bool  hud_pinned     = false;

    // ── Rendering modes ───────────────────────────────────────────────────────
    bool  outline_mode   = false;   // only draw top cell of each bar
    bool  colour_cycle   = false;   // slowly rotate gradient hue over time
    bool  per_bar_colour = false;   // map colour to bar index, not screen row

    // ── Audio ─────────────────────────────────────────────────────────────────
    bool  stereo         = true;
    int   high_cutoff    = 20000;   // Hz, 1000-24000

    // ── FFT / Smoothing ───────────────────────────────────────────────────────
    float gravity        = 1.0f;    // fall speed multiplier (0.1-5.0)
    float monstercat     = 1.5f;    // adjacent-bar propagation (0.0-5.0)
    float rise_factor    = 0.3f;    // attack smoothing (0.0=instant, 0.95=slow)
    float bass_smooth    = 0.0f;    // extra bass smoothing (0.0=off, 0.1-0.3 recommended)

    // ── Audio processing ──────────────────────────────────────────────────────
    bool  a_weighting    = false;   // IEC 61672 perceptual frequency weighting
    float noise_gate     = 0.02f;   // bars below this snapped to zero (0.0-0.2)
    bool  auto_mono      = false;   // auto-collapse to mono on high L/R correlation

    // ── Sensitivity ───────────────────────────────────────────────────────────
    float sensitivity    = 1.5f;
    bool  auto_sens      = true;

    // ── Performance ───────────────────────────────────────────────────────────
    int   fps            = 60;      // 10-240

    // ── Paths ─────────────────────────────────────────────────────────────────
    static std::string configPath();   // ~/.config/cava-viz/config
    static std::string statePath();    // ~/.local/state/cava-viz/state

    bool load();
    void save() const;

    std::string last_source;   // internal state — kept in statePath()
    bool  loadState();
    void  saveState() const;
};
