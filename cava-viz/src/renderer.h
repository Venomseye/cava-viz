#pragma once
/**
 * ncurses renderer.
 * Gradient: SCREEN-relative (row determines colour, not bar height).
 * Incremental redraw: only cells that changed from last frame.
 * Dynamic gradient: one colour pair per terminal row — no banding.
 * No A_BOLD on bar cells (washes out RGB colours on most terminals).
 */
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

    // CAVA: gradient_size = LINES — one colour pair per terminal row.
    // grad_steps_ is the live count (= avail, rebuilt on resize/init).
    static constexpr int    GRAD_STEPS_MAX = 256;
    static constexpr int    COLOR_BASE     = 16;
    // HUD_PAIR_BASE sits above bar slots AND the 4 dedicated HUD colour slots.
    static constexpr int    HUD_PAIR_BASE  = GRAD_STEPS_MAX + 5;
    static constexpr int    HUD_PAIRS      = 4;
    static constexpr double HUD_HIDE_SECS  = 3.0;
    static constexpr int    HUD_ROWS       = 2;

    Renderer();
    ~Renderer();
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init();

    /// bars_l / bars_r: values in [0,1] (left and right channel).
    void render(const std::vector<float>& bars_l,
                const std::vector<float>& bars_r,
                double fps          = 60.0,
                const std::string&  backend_name = "",
                float sensitivity   = 1.0f,
                bool  auto_sens     = true);

    void handleResize();
    int  cols() const;
    int  rows() const;

    /// Number of bars that fit per side at current geometry.
    int  barCount() const;

    void notifyChange();

    // Theme
    void        setTheme(Theme t);
    Theme       nextTheme();
    Theme       theme()    const { return theme_; }
    std::string themeName() const;

    // Bar width
    int  increaseBarWidth();
    int  decreaseBarWidth();
    void setBarWidth(int w);
    int  barWidth()     const { return bar_w_; }
    /// Bar width that proportionally fills the terminal (1 unit per 40 cols).
    int  autoBarWidth() const;

    // Gap
    int  cycleGap();
    void setGapWidth(int g) { gap_w_ = std::clamp(g, 0, 2); }
    int  gapWidth()  const  { return gap_w_; }

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
    bool needs_clear_  {true};

    // Incremental redraw state
    std::vector<int> prev_l_, prev_r_;
    int              prev_avail_      {0};
    // Live gradient step count = avail.  One colour pair per row: no banding.
    int              grad_steps_      {GRAD_STEPS_MAX};
    int              prev_grad_avail_ {0};
    void resetPrev(int n, int avail);

    // Colour
    void rebuildColors();
    void applyNcursesSettings();
    int  gradPair(float screen_frac) const noexcept;
    int  hudPair (int lv)            const noexcept;

    // Drawing
    void drawBarColumn(int col, int height_sub, int prev_sub, int avail);
    void drawMirrorLR (const std::vector<float>& left,
                       const std::vector<float>& right, int avail);
    void drawStatusBar(double fps, const std::string& backend,
                       float sens, bool auto_sens);
};
