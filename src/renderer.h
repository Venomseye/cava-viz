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
    // Beat flash: sum of bars 0-3 must exceed this to trigger a one-frame bold overlay.
    // Range [0,1] per bar; 4 bars summed → effective range [0,4].
    // 0.55 * 4 = 2.2 means each of the 4 bass bars averages 55% height.
    static constexpr float  BEAT_THRESHOLD = 0.55f;

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
    int  gapWidth()  const  { return gap_w_; }

    // ── HUD pin ───────────────────────────────────────────────────────────────
    void setHudPinned(bool v)  { hud_pinned_ = v; }
    void toggleHudPin()        { hud_pinned_ = !hud_pinned_; notifyChange(); }
    bool hudPinned()     const { return hud_pinned_; }

    // ── Feedback flash ────────────────────────────────────────────────────────
    void showFeedback(const std::string& msg);

    // ── Source name for HUD ───────────────────────────────────────────────────
    void setSourceName(const std::string& s) { source_name_ = s; }

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

    // Beat flash state
    bool beat_flash_ {false};   // true for exactly one frame when beat detected

    std::vector<int> prev_l_, prev_r_;
    int              prev_avail_      {0};
    int              grad_steps_      {GRAD_STEPS_MAX};
    int              prev_grad_avail_ {0};
    void resetPrev(int n, int avail);

    void rebuildColors();
    void applyNcursesSettings();
    int  gradPair(float screen_frac) const noexcept;
    int  hudPair (int lv)            const noexcept;

    void drawBarColumn(int col, int height_sub, int prev_sub, int avail);
    void drawMirrorLR (const std::vector<float>& left,
                       const std::vector<float>& right, int avail);
    void drawStatusBar(double fps, const std::string& backend,
                       float sens, bool auto_sens);
};
