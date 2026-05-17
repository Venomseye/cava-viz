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
    static constexpr int    GRAD_STEPS_MAX = 256;
    static constexpr int    COLOR_BASE     = 16;
    static constexpr int    HUD_PAIR_BASE  = GRAD_STEPS_MAX + 5;
    static constexpr int    HUD_PAIRS      = 4;
    static constexpr double HUD_HIDE_SECS  = 3.0;
    static constexpr double FEEDBACK_SECS  = 1.5;
    static constexpr int    HUD_ROWS       = 2;

    // Beat flash: average of bars 0–3 must reach this to trigger A_BOLD overlay.
    static constexpr float  BEAT_THRESHOLD = 0.55f;

    // Colour cycle: hue shift per second (degrees in [0,360)).
    static constexpr float  HUE_DEG_PER_SEC = 30.0f;

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
    /// When true, only the top cell of each bar is drawn (lighter, retro look).
    void toggleOutline()         { outline_mode_ = !outline_mode_;
                                   invalidatePrev(); notifyChange();
                                   showFeedback(outline_mode_ ? "Outline" : "Filled"); }
    void setOutlineMode(bool v)  { outline_mode_ = v; invalidatePrev(); }
    bool outlineMode()     const { return outline_mode_; }

    // ── Colour cycle ──────────────────────────────────────────────────────────
    /// When true, the gradient hue rotates slowly over time.
    void toggleColourCycle()        { colour_cycle_ = !colour_cycle_;
                                      showFeedback(colour_cycle_ ? "Colour Cycle On"
                                                                  : "Colour Cycle Off"); }
    void setColourCycle(bool v)     { colour_cycle_ = v; }
    bool colourCycle()        const { return colour_cycle_; }

    // ── Per-bar colour ────────────────────────────────────────────────────────
    /// When true, colour maps to bar index (bass→treble) rather than screen row.
    void togglePerBarColour()       { per_bar_colour_ = !per_bar_colour_;
                                      invalidatePrev(); notifyChange();
                                      showFeedback(per_bar_colour_ ? "Bar Colour"
                                                                    : "Row Colour"); }
    void setPerBarColour(bool v)    { per_bar_colour_ = v; invalidatePrev(); }
    bool perBarColour()       const { return per_bar_colour_; }

private:
    using Clock = std::chrono::steady_clock;
    using TP    = std::chrono::time_point<Clock>;

    bool  initialized_ {false};
    bool  can_rgb_     {false};
    Theme theme_       {Theme::FIRE};
    int   bar_w_       {BAR_W_DEFAULT};
    int   gap_w_       {1};

    // HUD
    TP   last_change_tp_;
    bool hud_visible_  {true};
    bool hud_pinned_   {false};
    bool needs_clear_  {true};

    // Feedback flash
    std::string feedback_msg_;
    TP          feedback_tp_;
    bool        feedback_active_ {false};

    std::string source_name_;

    // Beat flash
    bool beat_flash_ {false};

    // Incremental redraw state
    std::vector<int> prev_l_, prev_r_;
    int              prev_avail_      {0};
    int              grad_steps_      {GRAD_STEPS_MAX};
    int              prev_grad_avail_ {0};

    // Rendering modes
    bool outline_mode_   {false};
    bool colour_cycle_   {false};
    bool per_bar_colour_ {false};

    // Colour cycle state
    float hue_offset_  {0.0f};  // degrees [0, 360)
    TP    last_frame_tp_;

    // Cached bar count for per-bar colour mapping
    int   last_bar_count_ {0};

    void resetPrev(int n, int avail);
    void invalidatePrev();

    void rebuildColors();
    void applyNcursesSettings();
    static bool detectTruecolor() noexcept;  // env-var based, distro/terminal agnostic
    int  gradPair(float frac) const noexcept;
    int  hudPair (int lv)     const noexcept;

    void drawBarColumn(int col, int bar_idx, int total_bars,
                       int height_sub, int prev_sub, int avail);
    void drawMirrorLR (const std::vector<float>& left,
                       const std::vector<float>& right, int avail);
    void drawStatusBar(double fps, const std::string& backend,
                       float sens, bool auto_sens);
};
