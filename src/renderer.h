#pragma once
#include "user_theme.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#include <ncurses.h>

enum class Theme : int {
    FIRE    = 0,
    PLASMA  = 1,
    NEON    = 2,
    TEAL    = 3,
    SUNSET  = 4,
    CANDY   = 5,
    AURORA  = 6,
    INFERNO = 7,
    WHITE   = 8,
    ROSE    = 9,
    MERMAID = 10,
    VAPOR   = 11,
    COUNT   = 12,
};

class Renderer {
public:
    static constexpr int BAR_W_MIN     = 1;
    static constexpr int BAR_W_MAX     = 8;
    static constexpr int BAR_W_DEFAULT = 2;

    // Gradient palette: up to 256 steps, runtime-clamped to
    // COLORS - COLOR_BASE - HUD_PAIRS so all init_color() indices are valid.
    static constexpr int GRAD_STEPS_MAX = 256;
    static constexpr int COLOR_BASE     = 16;
    static constexpr int HUD_PAIRS      = 4;

    static constexpr double HUD_HIDE_SECS = 3.0;
    static constexpr double FEEDBACK_SECS = 1.5;
    static constexpr int    HUD_ROWS      = 2;

    // Beat flash: avg of bars 0-3 must reach this to trigger A_BOLD overlay.
    static constexpr float BEAT_THRESHOLD  = 0.55f;
    // Colour cycle: hue rotation speed in degrees per second.
    static constexpr float HUE_DEG_PER_SEC = 30.0f;

    Renderer();
    ~Renderer();
    Renderer(const Renderer &)            = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool init();

    void render(const std::vector<float> &bars_l,
                const std::vector<float> &bars_r,
                double fps             = 60.0,
                const std::string     &backend_name = "",
                float sensitivity      = 1.0f,
                bool  auto_sens        = true);

    void handleResize();
    int  cols() const;
    int  rows() const;
    int  barCount() const;
    void notifyChange();

    // ── Theme ─────────────────────────────────────────────────────────────────
    // theme_abs: 0..COUNT-1 = built-in, COUNT+ = user themes (loaded at runtime)
    void        setTheme(Theme t);          // set by built-in enum (clamped)
    void        setThemeIdx(int idx);       // set by absolute index (built-in or user)
    void        setUserThemes(std::vector<UserTheme> themes);  // replace user theme list
    int         nextTheme();                // cycle to next (built-in → user → wrap)
    int         themeIdx()  const { return theme_abs_; }
    Theme       theme()     const;          // Theme::COUNT when a user theme is active
    std::string themeName() const;          // returns user theme name when applicable

    // ── Bar width ─────────────────────────────────────────────────────────────
    int  increaseBarWidth();
    int  decreaseBarWidth();
    void setBarWidth(int w);
    int  barWidth()     const { return bar_w_; }
    int  autoBarWidth() const;

    // ── Gap ───────────────────────────────────────────────────────────────────
    int  cycleGap();
    void setGapWidth(int g) { gap_w_ = std::clamp(g, 0, 2); }
    int  gapWidth()   const { return gap_w_; }

    // ── HUD pin ───────────────────────────────────────────────────────────────
    void setHudPinned(bool v) { hud_pinned_ = v; }
    void toggleHudPin()       { hud_pinned_ = !hud_pinned_; notifyChange(); }
    bool hudPinned()    const { return hud_pinned_; }

    // ── Feedback flash ────────────────────────────────────────────────────────
    void showFeedback(const std::string &msg);

    // ── Source name ───────────────────────────────────────────────────────────
    void setSourceName(const std::string &s) { source_name_ = s; }

    // ── Colour cycle ──────────────────────────────────────────────────────────
    void toggleColourCycle() {
        colour_cycle_     = !colour_cycle_;
        last_rebuild_hue_ = -360.0f;  // force immediate rebuild on next frame
        showFeedback(colour_cycle_ ? "Colour Cycle On" : "Colour Cycle Off");
    }
    void setColourCycle(bool v) {
        if (v != colour_cycle_) last_rebuild_hue_ = -360.0f;
        colour_cycle_ = v;
    }
    bool colourCycle()    const { return colour_cycle_; }

    // ── Per-bar colour ────────────────────────────────────────────────────────
    void togglePerBarColour() {
        per_bar_colour_ = !per_bar_colour_;
        needs_clear_    = true;
        invalidatePrev();
        notifyChange();
        showFeedback(per_bar_colour_ ? "Bar Colour" : "Row Colour");
    }
    void setPerBarColour(bool v) {
        per_bar_colour_ = v;
        needs_clear_    = true;
        invalidatePrev();
    }
    bool perBarColour() const { return per_bar_colour_; }

private:
    using Clock = std::chrono::steady_clock;
    using TP    = std::chrono::time_point<Clock>;

    bool  initialized_ {false};
    bool  can_rgb_     {false};
    int   theme_abs_   {0};   // 0..COUNT-1 = built-in, COUNT+ = user theme index
    int   bar_w_       {BAR_W_DEFAULT};
    int   gap_w_       {1};

    std::vector<UserTheme> user_themes_;

    TP   last_change_tp_;
    bool hud_visible_  {true};
    bool hud_pinned_   {false};
    bool needs_clear_  {true};

    std::string feedback_msg_;
    TP          feedback_tp_;
    bool        feedback_active_ {false};

    std::string source_name_;
    bool        beat_flash_ {false};

    std::vector<int> prev_l_, prev_r_;
    int              prev_avail_      {0};
    int              grad_steps_      {GRAD_STEPS_MAX};
    int              prev_grad_avail_ {0};

    bool  colour_cycle_      {false};
    bool  per_bar_colour_    {false};
    float hue_offset_        {0.0f};
    // Tracks hue at last rebuildColors() call during colour cycling.
    // Initialised to -360 so the very first cycle frame always rebuilds.
    float last_rebuild_hue_  {-360.0f};
    TP    last_frame_tp_;
    int   last_bar_count_ {0};

    void resetPrev(int n, int avail);
    void invalidatePrev();
    void rebuildColors();
    void applyNcursesSettings();
    void applyTermOverride() noexcept;

    static bool detectTruecolor() noexcept;

    int gradPair(float frac) const noexcept;
    int hudPair(int lv)      const noexcept;

    void drawBarColumn(int col, int bar_idx, int total_bars,
                       int height_sub, int prev_sub, int avail);
    void drawMirrorLR(const std::vector<float> &left,
                      const std::vector<float> &right, int avail);
    void drawStatusBar(double fps, const std::string &backend,
                       float sens, bool auto_sens);
};
