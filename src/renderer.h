#pragma once
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#include <ncurses.h>

enum class Theme : int {
    FIRE = 0,
    PLASMA = 1,
    NEON = 2,
    TEAL = 3,
    SUNSET = 4,
    CANDY = 5,
    AURORA = 6,
    INFERNO = 7,
    WHITE = 8,
    ROSE = 9,
    MERMAID = 10,
    VAPOR = 11,
    COUNT = 12,
};

class Renderer {
public:
    static constexpr int BAR_W_MIN = 1;
    static constexpr int BAR_W_MAX = 8;
    static constexpr int BAR_W_DEFAULT = 2;

    // ── Colour palette ────────────────────────────────────────────────────────
    // We request up to GRAD_STEPS_MAX gradient entries; rebuildColors() clamps
    // the actual count to  COLORS - COLOR_BASE - HUD_PAIRS  so every
    // init_color() call uses a valid terminal colour index.
    //
    // On xterm-256color (COLORS=256, COLOR_BASE=16, HUD_PAIRS=4) this gives
    // 236 steps — visually identical to 256 while staying within 0..255.
    // On truecolor terminals with COLORS > 256 the full 256 steps are used.
    static constexpr int GRAD_STEPS_MAX = 256;
    static constexpr int COLOR_BASE = 16;   // first colour index we allocate
    static constexpr int HUD_PAIRS = 4;    // accent colours for the HUD

    static constexpr double HUD_HIDE_SECS = 3.0;
    static constexpr double FEEDBACK_SECS = 1.5;
    static constexpr int    HUD_ROWS = 2;

    // Beat flash: average of bars 0-3 must hit this to trigger A_BOLD overlay.
    static constexpr float BEAT_THRESHOLD = 0.55f;
    // Colour cycle: hue rotation speed in degrees per second.
    static constexpr float HUE_DEG_PER_SEC = 30.0f;

    Renderer();
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator = (const Renderer &) = delete;

    bool init();

    void render(const std::vector<float> &bars_l,
                const std::vector<float> &bars_r,
                double fps = 60.0,
                const std::string      &backend_name = "",
                float sensitivity = 1.0f,
                bool  auto_sens = true);

    void handleResize();
    int  cols() const;
    int  rows() const;
    int  barCount() const;
    void notifyChange();

    // ── Theme ─────────────────────────────────────────────────────────────────
    void        setTheme(Theme t);
    Theme       nextTheme();
    Theme       theme()     const { return theme_; }
    std::string themeName() const;

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

    // ── Outline mode ─────────────────────────────────────────────────────────
    // BUG FIX: needs_clear_ = true is mandatory.
    // Without it, invalidatePrev() forces a full redraw (prev = -1) but the
    // old filled-bar pixels are still on screen, leaving permanent ghost bars.
    void toggleOutline() {
        outline_mode_ = !outline_mode_;
        needs_clear_ = true;
        invalidatePrev();
        notifyChange();
        showFeedback(outline_mode_ ? "Outline" : "Filled");
    }
    void setOutlineMode(bool v) {
        outline_mode_ = v;
        needs_clear_ = true;
        invalidatePrev();
    }
    bool outlineMode() const { return outline_mode_; }

    // ── Colour cycle ──────────────────────────────────────────────────────────
    void toggleColourCycle() {
        colour_cycle_ = !colour_cycle_;
        showFeedback(colour_cycle_ ? "Colour Cycle On" : "Colour Cycle Off");
    }
    void setColourCycle(bool v) { colour_cycle_ = v; }
    bool colourCycle()    const { return colour_cycle_; }

    // ── Per-bar colour ────────────────────────────────────────────────────────
    // BUG FIX: same ghost-bar issue as outline mode — needs_clear_ required.
    void togglePerBarColour() {
        per_bar_colour_ = !per_bar_colour_;
        needs_clear_ = true;
        invalidatePrev();
        notifyChange();
        showFeedback(per_bar_colour_ ? "Bar Colour" : "Row Colour");
    }
    void setPerBarColour(bool v) {
        per_bar_colour_ = v;
        needs_clear_ = true;
        invalidatePrev();
    }
    bool perBarColour() const { return per_bar_colour_; }

private:
    using Clock = std::chrono::steady_clock;
    using TP = std::chrono::time_point<Clock>;

    bool  initialized_ {false};
    bool  can_rgb_     {false};
    Theme theme_       {Theme::FIRE};
    int   bar_w_       {BAR_W_DEFAULT};
    int   gap_w_       {1};

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

    // Actual gradient step count — clamped in rebuildColors() so that all
    // colour indices stay within [COLOR_BASE, COLORS - 1].
    // HUD colours occupy the 4 slots immediately after the gradient:
    //   colour index  COLOR_BASE + grad_steps_ + [0..3]
    //   ncurses pair  grad_steps_ + 1          + [0..3]
    int grad_steps_      {GRAD_STEPS_MAX};
    int prev_grad_avail_ {0};

    bool outline_mode_   {false};
    bool colour_cycle_   {false};
    bool per_bar_colour_ {false};

    float hue_offset_     {0.0f};
    TP    last_frame_tp_;
    int   last_bar_count_ {0};

    void resetPrev(int n, int avail);
    void invalidatePrev();

    void rebuildColors();
    void applyNcursesSettings();
    void applyTermOverride() noexcept;

    static bool detectTruecolor() noexcept;

    int gradPair(float frac) const noexcept;
    // HUD pairs live at grad_steps_+1 .. grad_steps_+HUD_PAIRS (always valid).
    int hudPair(int lv) const noexcept;

    void drawBarColumn(int col, int bar_idx, int total_bars,
                       int height_sub, int prev_sub, int avail);
    void drawMirrorLR(const std::vector<float> &left,
                      const std::vector<float> &right, int avail);
    void drawStatusBar(double fps, const std::string &backend,
                       float sens, bool auto_sens);
};
