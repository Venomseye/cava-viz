#pragma once
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#include <ncurses.h>

enum class Theme : int {
    FIRE=0, PLASMA=1, NEON=2, TEAL=3, SUNSET=4,
    CANDY=5, AURORA=6, INFERNO=7, WHITE=8, ROSE=9,
    MERMAID=10, VAPOR=11, COUNT=12
};

class Renderer {
public:
    static constexpr int    BAR_W_MIN      = 1;
    static constexpr int    BAR_W_MAX      = 8;
    static constexpr int    BAR_W_DEFAULT  = 2;

    // ── Colour palette sizing ─────────────────────────────────────────────────
    // We request up to GRAD_STEPS_MAX gradient entries, but rebuildColors()
    // clamps the actual count to COLORS − COLOR_BASE − HUD_PAIRS so that
    // every init_color() call uses a valid terminal colour index.
    //
    // On a standard xterm-256color terminal (COLORS=256, COLOR_BASE=16,
    // HUD_PAIRS=4) this gives 236 gradient steps — visually identical to 256
    // while staying within the safe range of colour indices 16..255.
    //
    // On truecolor terminals that expose more than 256 colour slots (e.g.
    // kitty with COLORS=2^24) the full GRAD_STEPS_MAX is used.
    static constexpr int    GRAD_STEPS_MAX = 256;
    static constexpr int    COLOR_BASE     = 16;   // start after the 16 ANSI colours
    static constexpr int    HUD_PAIRS      = 4;

    // Compile-time upper bound for HUD pair index — the actual value is
    // computed at runtime as grad_steps_ + 1 (returned by hudPair()).
    static constexpr int    HUD_PAIR_BASE_MAX = GRAD_STEPS_MAX + 1;

    static constexpr double HUD_HIDE_SECS  = 3.0;
    static constexpr double FEEDBACK_SECS  = 1.5;
    static constexpr int    HUD_ROWS       = 2;

    static constexpr float  BEAT_THRESHOLD   = 0.55f;
    static constexpr float  HUE_DEG_PER_SEC  = 30.0f;

    Renderer();
    ~Renderer();
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init();

    void render(const std::vector<float>& bars_l,
                const std::vector<float>& bars_r,
                double fps          = 60.0,
                const std::string&  backend_name = "",
                float sensitivity   = 1.0f,
                bool  auto_sens     = true);

    void handleResize();
    int  cols()     const;
    int  rows()     const;
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
    void showFeedback(const std::string& msg);

    // ── Source name ───────────────────────────────────────────────────────────
    void setSourceName(const std::string& s) { source_name_ = s; }

    // ── Outline mode ─────────────────────────────────────────────────────────
    // needs_clear_ is mandatory: without it the incremental draw skips erasing
    // old full-bar pixels before switching to outline-only, leaving ghost bars.
    void toggleOutline() {
        outline_mode_ = !outline_mode_;
        needs_clear_  = true;
        invalidatePrev();
        notifyChange();
        showFeedback(outline_mode_ ? "Outline" : "Filled");
    }
    void setOutlineMode(bool v)  { outline_mode_ = v; needs_clear_ = true; invalidatePrev(); }
    bool outlineMode()     const { return outline_mode_; }

    // ── Colour cycle ──────────────────────────────────────────────────────────
    void toggleColourCycle() {
        colour_cycle_ = !colour_cycle_;
        showFeedback(colour_cycle_ ? "Colour Cycle On" : "Colour Cycle Off");
    }
    void setColourCycle(bool v)     { colour_cycle_ = v; }
    bool colourCycle()        const { return colour_cycle_; }

    // ── Per-bar colour ────────────────────────────────────────────────────────
    // Same ghost-bar issue as outline mode — needs_clear_ required.
    void togglePerBarColour() {
        per_bar_colour_ = !per_bar_colour_;
        needs_clear_    = true;
        invalidatePrev();
        notifyChange();
        showFeedback(per_bar_colour_ ? "Bar Colour" : "Row Colour");
    }
    void setPerBarColour(bool v)    { per_bar_colour_ = v; needs_clear_ = true; invalidatePrev(); }
    bool perBarColour()       const { return per_bar_colour_; }

private:
    using Clock = std::chrono::steady_clock;
    using TP    = std::chrono::time_point<Clock>;

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
    bool beat_flash_ {false};

    std::vector<int> prev_l_, prev_r_;
    int              prev_avail_      {0};

    // Actual gradient steps — clamped at runtime to the terminal's COLORS so
    // that all colour indices stay within [COLOR_BASE, COLORS-1].
    // The HUD's 4 colour slots are placed immediately after the gradient
    // (at COLOR_BASE + grad_steps_), so the hard constraint is:
    //   COLOR_BASE + grad_steps_ + HUD_PAIRS <= COLORS
    int              grad_steps_      {GRAD_STEPS_MAX};
    int              prev_grad_avail_ {0};

    bool outline_mode_   {false};
    bool colour_cycle_   {false};
    bool per_bar_colour_ {false};

    float hue_offset_  {0.0f};
    TP    last_frame_tp_;
    int   last_bar_count_ {0};

    void resetPrev(int n, int avail);
    void invalidatePrev();

    void rebuildColors();
    void applyNcursesSettings();
    static bool detectTruecolor() noexcept;

    int  gradPair(float frac) const noexcept;
    // HUD pairs live at grad_steps_+1 .. grad_steps_+HUD_PAIRS.
    // This is always valid because grad_steps_ is clamped so that
    // COLOR_BASE + grad_steps_ + HUD_PAIRS <= COLORS.
    int  hudPair (int lv)     const noexcept;

    void drawBarColumn(int col, int bar_idx, int total_bars,
                       int height_sub, int prev_sub, int avail);
    void drawMirrorLR (const std::vector<float>& left,
                       const std::vector<float>& right, int avail);
    void drawStatusBar(double fps, const std::string& backend,
                       float sens, bool auto_sens);
};
