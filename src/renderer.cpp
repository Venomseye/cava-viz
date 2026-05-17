#include "renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <locale.h>

static const char* kBlk[9] = {
    " ",
    "\xe2\x96\x81","\xe2\x96\x82","\xe2\x96\x83","\xe2\x96\x84",
    "\xe2\x96\x85","\xe2\x96\x86","\xe2\x96\x87","\xe2\x96\x88",
};
static constexpr int SUB = 8;

static const char* kThemeNames[static_cast<int>(Theme::COUNT)] = {
    "Fire","Plasma","Neon","Teal","Sunset","Candy",
    "Aurora","Inferno","White","Rose","Mermaid","Vapor"
};

struct RGB  { short r,g,b; };
struct Stop { float t; RGB c; };

static const Stop kGrad[static_cast<int>(Theme::COUNT)][6] = {
    // FIRE
    {{{0.00f},{ 100, 22,  0}},{{0.20f},{ 680,140,  0}},
     {{0.42f},{ 980,260,  0}},{{0.62f},{1000, 50, 60}},
     {{0.80f},{ 960,  0,360}},{{1.00f},{1000,180,920}}},
    // PLASMA
    {{{0.00f},{ 500,  0, 60}},{{0.22f},{ 680,  0,380}},
     {{0.44f},{ 340,  0,820}},{{0.62f},{  60, 40,980}},
     {{0.82f},{  0, 360,1000}},{{1.00f},{  0, 660,1000}}},
    // NEON
    {{{0.00f},{ 110,  0,250}},{{0.22f},{ 360,  0,700}},
     {{0.44f},{ 660,  0,880}},{{0.64f},{ 910, 30,740}},
     {{0.82f},{ 980,150,980}},{{1.00f},{1000,420,1000}}},
    // TEAL
    {{{0.00f},{  0, 140,140}},{{0.22f},{  0, 400,400}},
     {{0.44f},{  0, 640,620}},{{0.64f},{  0, 840,820}},
     {{0.82f},{ 30, 960,920}},{{1.00f},{160,1000,980}}},
    // SUNSET
    {{{0.00f},{ 70,  0,300}}, {{0.22f},{ 540,  0,360}},
     {{0.44f},{ 920,180,  0}},{{0.64f},{1000,480,  0}},
     {{0.82f},{1000,780,  0}},{{1.00f},{1000,960,260}}},
    // CANDY
    {{{0.00f},{ 560,  0,280}},{{0.22f},{ 860, 80,540}},
     {{0.44f},{ 460,  0,920}},{{0.64f},{ 100, 60,980}},
     {{0.82f},{  0, 440,1000}},{{1.00f},{  0, 760,1000}}},
    // AURORA
    {{{0.00f},{  0, 240, 70}},{{0.22f},{  0, 560,340}},
     {{0.44f},{  0, 380,760}},{{0.64f},{ 220, 10,820}},
     {{0.82f},{ 520,  0,900}},{{1.00f},{ 740, 20,940}}},
    // INFERNO
    {{{0.00f},{ 20,  0, 20}}, {{0.20f},{ 200,  0, 50}},
     {{0.40f},{ 660,  0,  0}},{{0.60f},{ 950,130,  0}},
     {{0.80f},{1000,550,  0}},{{1.00f},{1000,960,620}}},
    // WHITE
    {{{0.00f},{ 180,200,260}},{{0.20f},{ 360,400,500}},
     {{0.40f},{ 560,600,700}},{{0.60f},{ 740,780,880}},
     {{0.80f},{ 880,900,960}},{{1.00f},{1000,1000,1000}}},
    // ROSE
    {{{0.00f},{ 250,  0, 60}},{{0.22f},{ 600,  0,120}},
     {{0.44f},{ 860, 60,220}},{{0.62f},{1000,100,380}},
     {{0.80f},{1000,340,600}},{{1.00f},{1000,600,840}}},
    // MERMAID
    {{{0.00f},{  0,  60,220}},{{0.22f},{  0, 300,560}},
     {{0.44f},{  0, 620,680}},{{0.62f},{ 120,760,600}},
     {{0.80f},{ 540,200,800}},{{1.00f},{ 900,260,760}}},
    // VAPOR
    {{{0.00f},{ 20,  0,160}}, {{0.22f},{ 200,  0,440}},
     {{0.44f},{ 760,  0,600}},{{0.62f},{1000, 40,480}},
     {{0.80f},{1000,200,800}},{{1.00f},{  0, 840,1000}}},
};

static const short kFall[static_cast<int>(Theme::COUNT)][48] = {
    {52,52,88,88,124,124,160,160,196,196,202,202,208,208,214,214,
     220,220,226,226,220,214,208,202,196,202,208,214,220,226,213,207,
     201,207,213,219,225,231,231,231,231,231,231,231,231,231,231,231},
    {52,88,124,129,93,57,21,27,33,39,45,51,87,123,159,195,
     231,231,39,45,87,123,57,93,129,165,201,207,213,219,27,33,
     45,87,123,159,195,231,231,231,231,231,231,231,231,231,231,231},
    {53,54,55,56,93,99,135,129,165,201,207,213,219,225,231,231,
     201,165,129,99,93,57,93,129,165,201,207,213,219,225,53,99,
     165,207,213,219,225,231,231,231,231,231,231,231,231,231,231,231},
    {23,23,29,29,35,36,37,43,44,45,51,87,123,159,195,231,
     45,51,87,123,159,195,231,231,23,29,35,43,45,51,87,123,
     159,195,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
    {53,89,125,161,197,196,202,208,214,220,226,227,228,229,230,231,
     196,202,208,220,226,231,231,231,53,89,161,202,208,214,220,226,
     231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
    {89,125,161,197,201,165,129,93,57,21,27,33,39,45,51,87,
     123,159,195,231,165,129,93,57,89,161,201,165,93,27,33,45,
     87,159,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
    {22,28,34,40,36,43,37,31,25,19,57,93,129,165,201,207,
     93,57,93,129,165,207,201,207,22,28,36,43,37,31,57,93,
     165,207,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
    {52,52,88,88,124,124,160,196,196,202,208,214,220,220,226,226,
     227,228,229,230,231,231,231,231,52,88,124,196,202,208,214,220,
     226,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
    {17,18,19,20,21,27,33,39,45,51,87,117,153,189,195,231,
     195,189,153,117,87,51,45,39,33,27,21,20,195,231,231,231,
     231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
    {52,88,89,125,125,161,161,197,197,196,204,211,218,225,231,231,
     211,204,211,218,225,231,231,231,52,89,161,197,204,211,218,225,
     231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
    {17,18,19,20,21,27,32,37,43,45,51,87,123,159,195,231,
     87,123,129,165,201,207,159,195,21,27,37,45,87,159,165,201,
     207,159,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
    {17,18,19,54,55,91,92,128,129,165,201,207,213,219,225,231,
     201,165,129,91,55,54,91,129,165,201,207,213,219,225,231,231,
     231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231},
};

// ── Colour helpers ────────────────────────────────────────────────────────────
static RGB lerpRGB(RGB a, RGB b, float t) noexcept {
    return {(short)(a.r+(b.r-a.r)*t),
            (short)(a.g+(b.g-a.g)*t),
            (short)(a.b+(b.b-a.b)*t)};
}
static RGB sampleTheme(int ti, float t) noexcept {
    const Stop* s = kGrad[ti];
    for (int i = 1; i < 6; ++i)
        if (t <= s[i].t) {
            float lt = (t-s[i-1].t)/(s[i].t-s[i-1].t);
            return lerpRGB(s[i-1].c, s[i].c, std::clamp(lt,0.f,1.f));
        }
    return s[5].c;
}

// HSV → RGB (H in [0,360), S and V in [0,1]).
// Returns ncurses 0-1000 scaled RGB.
static RGB hsvToRgb(float h, float s, float v) noexcept {
    float r=0,g=0,b=0;
    const float hh = std::fmod(h, 360.f) / 60.f;
    const int   i  = (int)hh;
    const float ff = hh - i;
    const float p  = v*(1-s), q=v*(1-s*ff), t_=v*(1-s*(1-ff));
    switch (i) {
        case 0: r=v;  g=t_; b=p;  break;
        case 1: r=q;  g=v;  b=p;  break;
        case 2: r=p;  g=v;  b=t_; break;
        case 3: r=p;  g=q;  b=v;  break;
        case 4: r=t_; g=p;  b=v;  break;
        default:r=v;  g=p;  b=q;  break;
    }
    return {(short)(r*1000),(short)(g*1000),(short)(b*1000)};
}

// Blend two ncurses-scaled RGB values.
static RGB blendRGB(RGB base, RGB hue, float amt) noexcept {
    return {(short)(base.r + (hue.r-base.r)*amt),
            (short)(base.g + (hue.g-base.g)*amt),
            (short)(base.b + (hue.b-base.b)*amt)};
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Truecolor terminal detection ──────────────────────────────────────────────
// ncurses' can_change_color() consults the terminfo database. Most truecolor
// terminals (Konsole, GNOME Terminal, Xfce Terminal, kitty, Alacritty, …) set
// TERM=xterm-256color whose terminfo entry deliberately omits the ccc (can
// change color) capability, so can_change_color() returns false even though
// init_color() works perfectly — the terminal processes xterm OSC 4 sequences
// regardless of what terminfo says.
//
// We therefore detect truecolor support via well-known environment variables
// that terminal emulators set independently of TERM/terminfo:
//   COLORTERM=truecolor|24bit  — the standard signal; most modern terminals
//   KONSOLE_VERSION            — Konsole specifically (always truecolor)
//   KITTY_WINDOW_ID            — kitty
//   VTE_VERSION                — GNOME Terminal, Terminator, Xfce Terminal
//   WT_SESSION                 — Windows Terminal (WSL)
//   ITERM_SESSION_ID           — iTerm2 (macOS)
//
// When any of these is set we force can_rgb_=true so the full RGB gradient
// path is used instead of the 256-colour fallback palette.
bool Renderer::detectTruecolor() noexcept {
    const char* ct = std::getenv("COLORTERM");
    if (ct && (strcmp(ct,"truecolor")==0 || strcmp(ct,"24bit")==0)) return true;
    if (std::getenv("KONSOLE_VERSION"))  return true;   // Konsole
    if (std::getenv("KITTY_WINDOW_ID"))  return true;   // kitty
    if (std::getenv("VTE_VERSION"))      return true;   // GNOME/Xfce/Terminator
    if (std::getenv("WT_SESSION"))       return true;   // Windows Terminal
    if (std::getenv("ITERM_SESSION_ID")) return true;   // iTerm2
    return false;
}

Renderer::Renderer()  { last_frame_tp_ = Clock::now(); }
Renderer::~Renderer() { if (initialized_) endwin(); }

void Renderer::applyNcursesSettings() {
    cbreak(); noecho(); curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
}

bool Renderer::init() {
    setlocale(LC_ALL, "");
    set_escdelay(20);
    initscr();
    applyNcursesSettings();
    if (!has_colors()) { endwin(); return false; }
    start_color();
    use_default_colors();
    // Force RGB colour path on terminals that support init_color() but whose
    // terminfo entry (typically xterm-256color) omits the ccc capability.
    can_rgb_ = (COLORS >= 256) && (can_change_color() || detectTruecolor());
    rebuildColors();
    last_change_tp_ = Clock::now();
    last_frame_tp_  = Clock::now();
    initialized_    = true;
    return true;
}

void Renderer::handleResize() {
    endwin(); refresh(); clear();
    start_color(); use_default_colors();
    can_rgb_ = (COLORS >= 256) && (can_change_color() || detectTruecolor());
    applyNcursesSettings();
    rebuildColors();
    invalidatePrev();
    prev_grad_avail_ = 0;
    needs_clear_     = true;
    notifyChange();
}

// ── Colour rebuild ─────────────────────────────────────────────────────────────
void Renderer::rebuildColors() {
    const int ti = (int)theme_;

    // Colour cycle: compute how much to rotate the hue of each theme colour.
    // We apply a gentle hue blend (up to 40%) rather than a full HSV override
    // so the theme's character is preserved while still visibly shifting.
    const float hue_amt = colour_cycle_ ? 0.40f : 0.0f;

    static constexpr float T_MIN  = 0.08f;
    static constexpr float T_SPAN = 0.92f;
    static constexpr float GAMMA  = 0.85f;

    if (can_rgb_) {
        for (int i = 0; i < grad_steps_; ++i) {
            float raw = (float)i / std::max(1, grad_steps_-1);
            float t   = T_MIN + std::pow(raw, GAMMA) * T_SPAN;
            RGB   rgb = sampleTheme(ti, t);
            if (colour_cycle_ && hue_amt > 0.f) {
                RGB hue = hsvToRgb(hue_offset_ + raw * 60.f, 0.8f, 0.9f);
                rgb     = blendRGB(rgb, hue, hue_amt);
            }
            short ci = (short)(COLOR_BASE + i);
            init_color(ci, rgb.r, rgb.g, rgb.b);
            init_pair(i+1, ci, -1);
        }
    } else if (COLORS >= 256) {
        const short* pal = kFall[ti];
        for (int i = 0; i < grad_steps_; ++i) {
            float raw = (float)i / std::max(1, grad_steps_-1);
            float t   = T_MIN + std::pow(raw, GAMMA) * T_SPAN;
            int   pi  = std::clamp((int)(t * 47.f + 0.5f), 0, 47);
            init_pair(i+1, pal[pi], -1);
        }
    } else {
        for (int i = 0; i < grad_steps_; ++i) {
            float raw = (float)i / std::max(1, grad_steps_-1);
            float f   = T_MIN + raw * T_SPAN;
            short fg  = f < 0.40f ? COLOR_RED : f < 0.75f ? COLOR_YELLOW : COLOR_WHITE;
            init_pair(i+1, fg, -1);
        }
    }

    // HUD colour pairs
    static const float kHF[HUD_PAIRS] = {0.55f, 0.72f, 0.88f, 1.00f};
    for (int lv = 0; lv < HUD_PAIRS; ++lv) {
        short ci = (short)(COLOR_BASE + GRAD_STEPS_MAX + lv);
        if (can_rgb_) {
            RGB rgb = sampleTheme(ti, kHF[lv]);
            init_color(ci, rgb.r, rgb.g, rgb.b);
            init_pair(HUD_PAIR_BASE+lv, ci, -1);
        } else if (COLORS >= 256) {
            init_pair(HUD_PAIR_BASE+lv, kFall[ti][(int)(kHF[lv]*47)], -1);
        } else {
            init_pair(HUD_PAIR_BASE+lv, kHF[lv]<0.75f ? COLOR_YELLOW : COLOR_WHITE, -1);
        }
    }
}

int Renderer::gradPair(float frac) const noexcept {
    int idx = (int)(frac * std::max(1, grad_steps_-1) + 0.5f);
    return std::clamp(idx, 0, grad_steps_-1) + 1;
}
int Renderer::hudPair(int lv) const noexcept {
    return HUD_PAIR_BASE + std::clamp(lv, 0, HUD_PAIRS-1);
}

// ── Public helpers ────────────────────────────────────────────────────────────
int Renderer::cols()  const { return COLS; }
int Renderer::rows()  const { return LINES; }
int Renderer::barCount() const {
    const int unit = std::max(1, bar_w_ + gap_w_);
    return std::max(1, (COLS + gap_w_) / (2 * unit));
}
int Renderer::autoBarWidth() const {
    return std::clamp(COLS / 40, BAR_W_MIN, BAR_W_MAX);
}
void Renderer::notifyChange() { last_change_tp_ = Clock::now(); hud_visible_ = true; }
void Renderer::invalidatePrev() {
    prev_l_.assign(prev_l_.size(), -1);
    prev_r_.assign(prev_r_.size(), -1);
}
void Renderer::showFeedback(const std::string& msg) {
    feedback_msg_    = msg;
    feedback_tp_     = Clock::now();
    feedback_active_ = true;
    notifyChange();
}

void Renderer::setTheme(Theme t) {
    theme_ = t;
    if (initialized_) rebuildColors();
    invalidatePrev(); needs_clear_ = true;
}
Theme Renderer::nextTheme() {
    theme_ = (Theme)(((int)theme_+1) % (int)Theme::COUNT);
    if (initialized_) rebuildColors();
    invalidatePrev(); needs_clear_ = true;
    notifyChange(); return theme_;
}
std::string Renderer::themeName() const { return kThemeNames[(int)theme_]; }

int Renderer::increaseBarWidth() {
    bar_w_ = std::min(bar_w_+1, BAR_W_MAX);
    invalidatePrev(); needs_clear_ = true;
    showFeedback("Width: " + std::to_string(bar_w_));
    return bar_w_;
}
int Renderer::decreaseBarWidth() {
    bar_w_ = std::max(bar_w_-1, BAR_W_MIN);
    invalidatePrev(); needs_clear_ = true;
    showFeedback("Width: " + std::to_string(bar_w_));
    return bar_w_;
}
void Renderer::setBarWidth(int w) {
    bar_w_ = std::clamp(w, BAR_W_MIN, BAR_W_MAX);
    invalidatePrev(); needs_clear_ = true;
}
int Renderer::cycleGap() {
    gap_w_ = (gap_w_ >= 2) ? 0 : gap_w_+1;
    invalidatePrev(); needs_clear_ = true;
    showFeedback("Gap: " + std::to_string(gap_w_));
    return gap_w_;
}

void Renderer::resetPrev(int n, int avail) {
    const bool resize = ((int)prev_l_.size() != n || prev_avail_ != avail);
    if (resize) {
        prev_l_.assign(n, -1);   prev_r_.assign(n, -1);
        prev_avail_ = avail;
    }
}

// ── drawBarColumn ─────────────────────────────────────────────────────────────
void Renderer::drawBarColumn(int col, int bar_idx, int total_bars,
                              int height_sub, int prev_sub, int avail) {
    if (col < 0 || col + bar_w_ > COLS || avail <= 0) return;

    const bool force  = (prev_sub < 0);
    if (!force && height_sub == prev_sub) return;

    const int prev_h  = force ? 0 : prev_sub;
    const int cur_lh  = height_sub / SUB;
    const int prev_lh = prev_h / SUB;

    int max_line = (std::max(height_sub, prev_h) + SUB) / SUB;
    max_line = std::min(max_line, avail);

    char lbuf[BAR_W_MAX * 3 + 1];

    for (int line = 0; line < max_line; ++line) {
        const int row = HUD_ROWS + avail - 1 - line;
        if (row < HUD_ROWS || row >= LINES) continue;

        // ── Colour pair selection ─────────────────────────────────────────────
        float frac;
        if (per_bar_colour_ && total_bars > 1) {
            frac = (float)bar_idx / (float)(total_bars - 1);
        } else {
            frac = (avail > 1) ? (float)line / (float)(avail - 1) : 1.f;
        }
        const int cp   = gradPair(frac);
        const int attr = beat_flash_ ? (COLOR_PAIR(cp) | A_BOLD) : COLOR_PAIR(cp);

        // ── Outline mode: only draw the topmost cell of the bar ───────────────
        if (outline_mode_) {
            if (line == cur_lh && height_sub > 0) {
                int bar_step = height_sub % SUB;
                if (bar_step == 0) bar_step = SUB - 1; else bar_step--;
                const char* blk = kBlk[std::clamp(bar_step+1, 1, 8)];
                for (int w = 0; w < bar_w_; ++w) memcpy(lbuf + w*3, blk, 3);
                lbuf[bar_w_*3] = '\0';
                attron(attr);
                mvaddstr(row, col, lbuf);
                attroff(attr);
            } else if (!force && line <= prev_lh && line != cur_lh) {
                mvhline(row, col, ' ', bar_w_);
            }
            continue;
        }

        // ── Filled bar drawing ────────────────────────────────────────────────
        if (height_sub >= line * SUB + 1) {
            int bar_step;
            if (cur_lh == line) {
                bar_step = height_sub % SUB;
                if (bar_step == 0) bar_step = SUB - 1; else bar_step--;
            } else if (force || prev_lh <= line) {
                bar_step = SUB - 1;
            } else {
                continue;
            }
            const char* blk = kBlk[std::clamp(bar_step+1, 1, 8)];
            for (int w = 0; w < bar_w_; ++w) memcpy(lbuf + w*3, blk, 3);
            lbuf[bar_w_*3] = '\0';
            attron(attr);
            mvaddstr(row, col, lbuf);
            attroff(attr);
        } else if (!force && prev_lh >= line) {
            mvhline(row, col, ' ', bar_w_);
        }
    }
}

// ── drawMirrorLR ──────────────────────────────────────────────────────────────
void Renderer::drawMirrorLR(const std::vector<float>& left,
                              const std::vector<float>& right, int avail) {
    if (COLS <= 0 || avail <= 0) return;

    const int n    = (int)left.size();
    const int unit = bar_w_ + gap_w_;

    // Centre the mirror layout
    const int total_w   = 2 * n * unit - gap_w_;
    const int left_edge = (COLS - total_w) / 2;
    const int centre    = left_edge + n * unit;

    resetPrev(n, avail);

    // total_bars for per-bar colour: full mirror width = 2*n bars.
    // Bar index: right side = bar_idx 0..n-1 (bass→treble outward),
    //            left side  = bar_idx n..2n-1 (bass←treble inward).
    // We map both to [0,n-1] symmetrically so colour is symmetric.
    const int total_bars = n;

    for (int b = 0; b < n; ++b) {
        const float rv    = (b < (int)right.size()) ? right[b] : 0.f;
        const float lv    = (b < (int)left.size())  ? left[b]  : 0.f;
        const int h_sub_r = std::clamp((int)(rv * avail * SUB), 0, avail * SUB);
        const int h_sub_l = std::clamp((int)(lv * avail * SUB), 0, avail * SUB);

        const int rcol = centre + b * unit;
        if (rcol + bar_w_ <= COLS) {
            drawBarColumn(rcol, b, total_bars, h_sub_r, prev_r_[b], avail);
            prev_r_[b] = h_sub_r;
        }

        const int lcol = centre - (b+1) * unit;
        if (lcol >= 0) {
            drawBarColumn(lcol, b, total_bars, h_sub_l, prev_l_[b], avail);
            prev_l_[b] = h_sub_l;
        }
    }
}

// ── drawStatusBar ─────────────────────────────────────────────────────────────
void Renderer::drawStatusBar(double fps, const std::string& backend,
                               float sens, bool auto_sens) {
    if (LINES < HUD_ROWS + 1 || COLS < 10) return;
    mvhline(0, 0, ' ', COLS);

    const bool show_fb = feedback_active_ &&
        (std::chrono::duration<double>(Clock::now() - feedback_tp_).count() < FEEDBACK_SECS);
    if (!show_fb) feedback_active_ = false;

    char lbuf[512];
    if (show_fb) {
        std::snprintf(lbuf, sizeof(lbuf), "  %s", feedback_msg_.c_str());
    } else {
        // Mode flags shown compactly in the HUD
        char flags[32] = {};
        int  fi = 0;
        if (outline_mode_)   flags[fi++] = 'O';
        if (colour_cycle_)   flags[fi++] = 'C';
        if (per_bar_colour_) flags[fi++] = 'B';
        flags[fi] = '\0';

        char src_buf[80] = {};
        if (!source_name_.empty())
            std::snprintf(src_buf, sizeof(src_buf), "  |  %.40s", source_name_.c_str());

        std::snprintf(lbuf, sizeof(lbuf),
            " %s  |  %s  |  Bars:%d%s  |  W:%d G:%d  |  Sens:%.1f%s%s%s",
            backend.empty() ? "?" : backend.c_str(),
            themeName().c_str(),
            barCount(),
            src_buf,
            bar_w_, gap_w_,
            (double)sens,
            auto_sens    ? " A"   : "",
            hud_pinned_  ? " PIN" : "",
            fi > 0       ? (std::string("  [") + flags + "]").c_str() : "");
    }

    char rbuf[20];
    std::snprintf(rbuf, sizeof(rbuf), "%.0f fps ", fps);
    const int rlen = (int)strlen(rbuf);
    const int rcol = std::max(0, COLS - rlen);

    attron(COLOR_PAIR(hudPair(2)) | A_BOLD);
    mvaddnstr(0, 0, lbuf, std::max(0, rcol - 1));
    attroff(COLOR_PAIR(hudPair(2)) | A_BOLD);

    attron(COLOR_PAIR(hudPair(3)) | A_BOLD);
    mvaddnstr(0, rcol, rbuf, std::min(rlen, COLS - rcol));
    attroff(COLOR_PAIR(hudPair(3)) | A_BOLD);

    attron(COLOR_PAIR(hudPair(0)));
    mvhline(1, 0, ACS_HLINE, COLS);
    attroff(COLOR_PAIR(hudPair(0)));
}

// ── render ────────────────────────────────────────────────────────────────────
void Renderer::render(const std::vector<float>& bars_l,
                       const std::vector<float>& bars_r,
                       double fps, const std::string& backend,
                       float sens, bool auto_sens) {
    if (!initialized_ || bars_l.empty()) return;

    // ── Colour cycle: advance hue offset by elapsed time ─────────────────────
    const auto now_tp = Clock::now();
    if (colour_cycle_) {
        const float dt = std::chrono::duration<float>(now_tp - last_frame_tp_).count();
        hue_offset_ = std::fmod(hue_offset_ + HUE_DEG_PER_SEC * dt, 360.f);
        rebuildColors();   // cheap: only updates colour pair definitions
    }
    last_frame_tp_ = now_tp;

    // ── HUD visibility ────────────────────────────────────────────────────────
    const double elapsed = std::chrono::duration<double>(
                               now_tp - last_change_tp_).count();
    const bool hud_now = hud_pinned_ || (elapsed < HUD_HIDE_SECS) || feedback_active_;
    hud_visible_ = hud_now;

    // ── Gradient step count ───────────────────────────────────────────────────
    const int avail = std::max(1, LINES - HUD_ROWS);
    if (avail != prev_grad_avail_) {
        grad_steps_      = std::clamp(avail, 1, GRAD_STEPS_MAX);
        prev_grad_avail_ = avail;
        rebuildColors();
        needs_clear_ = true;
    }

    // ── Beat flash detection ──────────────────────────────────────────────────
    bool do_clear = needs_clear_;
    {
        const int  nb  = (int)bars_l.size();
        const int  lim = std::min(nb, 4);
        float      lfe = 0.f;
        for (int i = 0; i < lim; ++i) lfe += bars_l[i];
        const bool beat_now = (lim > 0) && (lfe / lim >= BEAT_THRESHOLD);
        if (beat_now != beat_flash_) {
            do_clear = true;
            invalidatePrev();
        }
        beat_flash_ = beat_now;
    }

    if (do_clear) { erase(); needs_clear_ = false; }

    // ── Cache bar count for per-bar colour ────────────────────────────────────
    last_bar_count_ = (int)bars_l.size();

    drawMirrorLR(bars_l, bars_r, avail);

    if (hud_now)
        drawStatusBar(fps, backend, sens, auto_sens);
    else {
        mvhline(0, 0, ' ', COLS);
        mvhline(1, 0, ' ', COLS);
    }

    refresh();
}
