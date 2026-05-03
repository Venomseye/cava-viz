#pragma once
#include <string>

struct Config {
    // ── Visual ────────────────────────────────────────────────────────────────
    int   theme       = 0;        // 0-11
    int   bar_width   = 2;        // 1-8
    int   gap_width   = 1;        // 0-2
    bool  hud_pinned  = false;    // HUD always visible

    // ── Audio ─────────────────────────────────────────────────────────────────
    bool  stereo      = true;
    int   high_cutoff = 20000;    // Hz upper limit (1000-24000)

    // ── FFT / Smoothing ───────────────────────────────────────────────────────
    float gravity     = 1.0f;     // bar fall speed multiplier (0.1-5.0)
    float monstercat  = 1.5f;     // adjacent-bar propagation (0.0-5.0)
    float rise_factor = 0.3f;     // attack smoothing (0.0=instant, 0.95=slow)

    // ── Sensitivity ───────────────────────────────────────────────────────────
    float sensitivity = 1.5f;
    bool  auto_sens   = true;

    // ── Performance ───────────────────────────────────────────────────────────
    int   fps         = 60;       // target FPS (10-240)

    // ── Paths ─────────────────────────────────────────────────────────────────
    static std::string configPath();   // ~/.config/cava-viz/config
    static std::string statePath();    // ~/.local/state/cava-viz/state

    bool load();        // load user config; returns false if missing
    void save() const;  // save user config (no last_source)

    // last_source is internal state, stored separately
    std::string last_source;
    bool  loadState();
    void  saveState() const;
};
