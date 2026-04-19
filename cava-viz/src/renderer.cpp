/**
 * ncurses renderer – CAVA-style incremental redraw + screen-relative gradient.
 *
 * Gradient mapping (from terminal_ncurses.c):
 *   change_colors(line, max_value): colour = line / (max_value / gradient_size)
 *   → colour is determined by which SCREEN ROW the cell is on, not bar height.
 *   → Short bars are dim (low row = low colour index).
 *   → Tall bars reach bright colours (high row = high colour index).
 *
 * Incremental redraw (from terminal_ncurses.c draw_terminal_ncurses):
 *   Only update cells where bars[n] != previous_frame[n].
 *   Uses 8 sub-cell Unicode block characters per terminal row.
 */
#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <locale.h>

// ── Sub-cell block characters (lower-eighth series, like CAVA) ───────────────
// index 0 = empty, 1..8 = ▁▂▃▄▅▆▇█
static const char* kBlk[9] = {
    " ",
    "\xe2\x96\x81","\xe2\x96\x82","\xe2\x96\x83","\xe2\x96\x84",
    "\xe2\x96\x85","\xe2\x96\x86","\xe2\x96\x87","\xe2\x96\x88",
};
static constexpr int SUB = 8;  // sub-cells per terminal row

// ── Theme names ───────────────────────────────────────────────────────────────
static const char* kThemeNames[static_cast<int>(Theme::COUNT)] = {
    "Fire","Plasma","Neon","Teal","Sunset","Candy","Aurora","Inferno",
    "White","Rose","Mermaid","Vapor"
};

// ── RGB gradient stops (ncurses 0-1000, bottom→top) ───────────────────────────
struct RGB  { short r,g,b; };
struct Stop { float t; RGB c; };

static const Stop kGrad[static_cast<int>(Theme::COUNT)][6] = {
    // FIRE: dark-amber → orange → red → electric-pink
    {{{0.00f},{ 100, 22,  0}},{{0.20f},{ 680,140,  0}},
     {{0.42f},{ 980,260,  0}},{{0.62f},{1000, 50, 60}},
     {{0.80f},{ 960,  0,360}},{{1.00f},{1000,180,920}}},
    // PLASMA: deep-crimson → violet → blue → cyan
    {{{0.00f},{ 500,  0, 60}},{{0.22f},{ 680,  0,380}},
     {{0.44f},{ 340,  0,820}},{{0.62f},{  60, 40,980}},
     {{0.82f},{  0, 360,1000}},{{1.00f},{  0, 660,1000}}},
    // NEON: deep-indigo → violet → magenta → pale-pink
    {{{0.00f},{ 110,  0,250}},{{0.22f},{ 360,  0,700}},
     {{0.44f},{ 660,  0,880}},{{0.64f},{ 910, 30,740}},
     {{0.82f},{ 980,150,980}},{{1.00f},{1000,420,1000}}},
    // TEAL: midnight → teal → vivid-cyan
    {{{0.00f},{  0, 140,140}},{{0.22f},{  0, 400,400}},
     {{0.44f},{  0, 640,620}},{{0.64f},{  0, 840,820}},
     {{0.82f},{ 30, 960,920}},{{1.00f},{160,1000,980}}},
    // SUNSET: deep-violet → scarlet → gold
    {{{0.00f},{ 70,  0,300}}, {{0.22f},{ 540,  0,360}},
     {{0.44f},{ 920,180,  0}},{{0.64f},{1000,480,  0}},
     {{0.82f},{1000,780,  0}},{{1.00f},{1000,960,260}}},
    // CANDY: hot-rose → vivid-purple → sky-blue
    {{{0.00f},{ 560,  0,280}},{{0.22f},{ 860, 80,540}},
     {{0.44f},{ 460,  0,920}},{{0.64f},{ 100, 60,980}},
     {{0.82f},{  0, 440,1000}},{{1.00f},{  0, 760,1000}}},
    // AURORA: forest-green → teal → deep-violet
    {{{0.00f},{  0, 240, 70}},{{0.22f},{  0, 560,340}},
     {{0.44f},{  0, 380,760}},{{0.64f},{ 220, 10,820}},
     {{0.82f},{ 520,  0,900}},{{1.00f},{ 740, 20,940}}},
    // INFERNO: near-black → red → orange → pale-gold
    {{{0.00f},{ 20,  0, 20}}, {{0.20f},{ 200,  0, 50}},
     {{0.40f},{ 660,  0,  0}},{{0.60f},{ 950,130,  0}},
     {{0.80f},{1000,550,  0}},{{1.00f},{1000,960,620}}},
    // WHITE: charcoal → white
    {{{0.00f},{ 200,200,200}},{{0.20f},{ 450,450,450}},
     {{0.40f},{ 680,680,680}},{{0.60f},{ 840,840,840}},
     {{0.80f},{ 940,940,940}},{{1.00f},{1000,1000,1000}}},
    // ROSE: deep-wine → rose → blush
    {{{0.00f},{ 250,  0, 60}},{{0.22f},{ 600,  0,120}},
     {{0.44f},{ 860, 60,220}},{{0.62f},{1000,100,380}},
     {{0.80f},{1000,340,600}},{{1.00f},{1000,600,840}}},
    // MERMAID: deep-ocean → aqua-teal → purple-coral
    {{{0.00f},{  0,  60,220}},{{0.22f},{  0, 300,560}},
     {{0.44f},{  0, 620,680}},{{0.62f},{ 120,760,600}},
     {{0.80f},{ 540,200,800}},{{1.00f},{ 900,260,760}}},
    // VAPOR: deep-navy → magenta → hot-pink → cyan
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
    {232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,
     248,249,250,251,252,253,254,255,255,255,255,255,255,255,255,255,
     255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255},
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

static RGB lerpRGB(RGB a, RGB b, float t) noexcept {
    return {(short)(a.r+(b.r-a.r)*t),(short)(a.g+(b.g-a.g)*t),(short)(a.b+(b.b-a.b)*t)};
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

// ─────────────────────────────────────────────────────────────────────────────
Renderer::Renderer()  = default;
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
    can_rgb_ = (COLORS >= 256) && can_change_color();
    rebuildColors();
    last_change_tp_ = Clock::now();
    initialized_    = true;
    return true;
}

void Renderer::handleResize() {
    endwin(); refresh(); clear();
    start_color(); use_default_colors();
    can_rgb_ = (COLORS >= 256) && can_change_color();
    applyNcursesSettings();
    rebuildColors();
    // Invalidate incremental state + gradient (will rebuild at new avail next frame)
    prev_l_.clear(); prev_r_.clear(); prev_avail_ = 0;
    prev_grad_avail_ = 0;
    needs_clear_ = true;
    notifyChange();
}

// ── Colour pairs ──────────────────────────────────────────────────────────────
void Renderer::rebuildColors() {
    const int ti = (int)theme_;
    if (can_rgb_) {
        for (int i = 0; i < grad_steps_; ++i) {
            // Lift floor to 0.08 so bottom row is never pitch-black.
            // Mild gamma 0.85 spreads the dark→mid range across more rows so
            // adjacent bottom colours are perceptually distinct. Top row
            // still hits t=1.0 (unchanged brightness).
            float raw = (float)i / std::max(1, grad_steps_ - 1);
            float t   = 0.08f + std::pow(raw, 0.85f) * 0.92f;
            RGB   rgb = sampleTheme(ti, t);
            short ci  = (short)(COLOR_BASE + i);
            init_color(ci, rgb.r, rgb.g, rgb.b);
            init_pair(i + 1, ci, -1);
        }
    } else if (COLORS >= 256) {
        const short* pal = kFall[ti];
        for (int i = 0; i < grad_steps_; ++i) {
            float raw     = (float)i / std::max(1, grad_steps_ - 1);
            float t       = 0.08f + std::pow(raw, 0.85f) * 0.92f;
            int   pal_idx = std::clamp((int)(t * 47.0f + 0.5f), 0, 47);
            init_pair(i + 1, pal[pal_idx], -1);
        }
    } else {
        for (int i = 0; i < grad_steps_; ++i) {
            float raw = (float)i / std::max(1, grad_steps_ - 1);
            float f   = 0.08f + raw * 0.92f;
            short fg  = f < 0.40f ? COLOR_RED : f < 0.75f ? COLOR_YELLOW : COLOR_WHITE;
            init_pair(i + 1, fg, -1);
        }
    }

    // HUD: dedicated colour slots above the bar range.
    // Sampled at vivid fractions — always bright on every theme,
    // independent of terminal height / grad_steps_.
    //   lv 0 = separator  → 0.55
    //   lv 1 = spare       → 0.72
    //   lv 2 = main text   → 0.88
    //   lv 3 = fps/accent  → 1.00
    static const float kHF[HUD_PAIRS] = {0.55f, 0.72f, 0.88f, 1.00f};
    for (int lv = 0; lv < HUD_PAIRS; ++lv) {
        short ci = (short)(COLOR_BASE + GRAD_STEPS_MAX + lv);
        if (can_rgb_) {
            RGB rgb = sampleTheme(ti, kHF[lv]);
            init_color(ci, rgb.r, rgb.g, rgb.b);
            init_pair(HUD_PAIR_BASE + lv, ci, -1);
        } else if (COLORS >= 256) {
            init_pair(HUD_PAIR_BASE + lv, kFall[ti][(int)(kHF[lv] * 47)], -1);
        } else {
            short fg = kHF[lv] < 0.75f ? COLOR_YELLOW : COLOR_WHITE;
            init_pair(HUD_PAIR_BASE + lv, fg, -1);
        }
    }
}

// Screen-relative gradient: line 0 = bottom → pair 1 (dark)
//                            line avail-1 = top → pair grad_steps_ (bright)
// grad_steps_ == avail: every row has its own unique colour pair — no banding.
int Renderer::gradPair(float screen_frac) const noexcept {
    int idx = (int)(screen_frac * std::max(1, grad_steps_ - 1) + 0.5f);
    return std::clamp(idx, 0, grad_steps_ - 1) + 1;
}
int Renderer::hudPair(int lv) const noexcept {
    return HUD_PAIR_BASE + std::clamp(lv, 0, HUD_PAIRS - 1);
}

// ── Public ────────────────────────────────────────────────────────────────────
int Renderer::cols()  const { return COLS; }
int Renderer::rows()  const { return LINES; }
int Renderer::barCount() const {
    const int unit = std::max(1, bar_w_ + gap_w_);
    // total_w = 2*n*unit - gap_w_ ≤ COLS  →  n = (COLS + gap_w_) / (2*unit)
    // Matches the centering formula in drawMirrorLR exactly.
    return std::max(1, (COLS + gap_w_) / (2 * unit));
}
int Renderer::autoBarWidth() const {
    // 1 extra width unit per 40 columns: 80→2, 120→3, 160→4, 200→5, 320→8
    return std::clamp(COLS / 40, BAR_W_MIN, BAR_W_MAX);
}
void Renderer::notifyChange() { last_change_tp_ = Clock::now(); hud_visible_ = true; }

void Renderer::setTheme(Theme t) {
    theme_ = t;
    if (initialized_) rebuildColors();
    prev_l_.clear(); prev_r_.clear(); needs_clear_ = true;
}
Theme Renderer::nextTheme() {
    theme_ = (Theme)(((int)theme_ + 1) % (int)Theme::COUNT);
    if (initialized_) rebuildColors();
    prev_l_.clear(); prev_r_.clear(); needs_clear_ = true;
    notifyChange(); return theme_;
}
std::string Renderer::themeName() const { return kThemeNames[(int)theme_]; }
int  Renderer::increaseBarWidth() { bar_w_=std::min(bar_w_+1,BAR_W_MAX); prev_l_.clear(); prev_r_.clear(); needs_clear_=true; notifyChange(); return bar_w_; }
int  Renderer::decreaseBarWidth() { bar_w_=std::max(bar_w_-1,BAR_W_MIN); prev_l_.clear(); prev_r_.clear(); needs_clear_=true; notifyChange(); return bar_w_; }
void Renderer::setBarWidth(int w) { bar_w_=std::clamp(w,BAR_W_MIN,BAR_W_MAX); prev_l_.clear(); prev_r_.clear(); needs_clear_=true; }
int  Renderer::cycleGap() { gap_w_=(gap_w_>=2)?0:gap_w_+1; prev_l_.clear(); prev_r_.clear(); needs_clear_=true; notifyChange(); return gap_w_; }

void Renderer::resetPrev(int n, int avail) {
    if ((int)prev_l_.size() != n || prev_avail_ != avail) {
        prev_l_.assign(n, -1);   // -1 forces full redraw on first frame
        prev_r_.assign(n, -1);
        prev_avail_ = avail;
    }
}

// ── Incremental bar column drawing ───────────────────────────────────────────
// height_sub : current bar height in sub-cell units  (0 .. avail*SUB)
// prev_sub   : previous frame's value, OR -1 on forced full-redraw
//
// Line numbering: line 0 = bottom row (screen row LINES-1)
//                 line avail-1 = top row (screen row HUD_ROWS)
void Renderer::drawBarColumn(int col, int height_sub, int prev_sub, int avail) {
    if (col < 0 || col + bar_w_ > COLS || avail <= 0) return;

    const bool force  = (prev_sub < 0);
    if (!force && height_sub == prev_sub) return;

    const int prev_h  = force ? 0 : prev_sub;
    const int cur_lh  = height_sub / SUB;
    const int prev_lh = prev_h / SUB;

    int max_line = (std::max(height_sub, prev_h) + SUB) / SUB;
    max_line = std::min(max_line, avail);

    // Batch bar_w_ copies of the same block char into one mvaddstr call
    // instead of bar_w_ separate calls — reduces ncurses overhead up to 8×.
    char lbuf[BAR_W_MAX * 3 + 1];

    for (int line = 0; line < max_line; ++line) {
        const int   row = HUD_ROWS + avail - 1 - line;
        const float sf  = (avail > 1) ? (float)line / (float)(avail - 1) : 1.f;
        const int   cp  = gradPair(sf);

        if (height_sub >= line * SUB + 1) {
            // ── Cell is within bar ────────────────────────────────────────────
            int bar_step;
            if (cur_lh == line) {
                bar_step = height_sub % SUB;
                if (bar_step == 0) bar_step = SUB - 1;
                else               bar_step--;
            } else if (force || prev_lh <= line) {
                bar_step = SUB - 1;
            } else {
                continue;  // already a full block last frame — skip
            }
            const char* blk = kBlk[std::clamp(bar_step + 1, 1, 8)];
            for (int w = 0; w < bar_w_; ++w) memcpy(lbuf + w * 3, blk, 3);
            lbuf[bar_w_ * 3] = '\0';
            attron(COLOR_PAIR(cp));
            mvaddstr(row, col, lbuf);
            attroff(COLOR_PAIR(cp));

        } else if (!force && prev_lh >= line) {
            // ── Cell vacated — clear with a single hline call ─────────────────
            // (In force mode erase() already cleared it — skip to save work)
            mvhline(row, col, ' ', bar_w_);
        }
    }
}

// ── Mirror LR ─────────────────────────────────────────────────────────────────
// Layout: bass (bar index 0) nearest centre, treble outward.
// RIGHT half of screen → RIGHT audio channel.
// LEFT  half of screen → LEFT  audio channel.
void Renderer::drawMirrorLR(const std::vector<float>& left,
                              const std::vector<float>& right, int avail) {
    if (COLS <= 0 || avail <= 0) return;

    const int n    = (int)left.size();
    const int unit = bar_w_ + gap_w_;

    // Symmetric centering:
    //   total_w   = 2*n*unit - gap_w_   (no trailing gap on outermost edges)
    //   left_edge = floor((COLS - total_w) / 2)
    //   centre    = left_edge + n*unit
    // Left margin == right margin (or differs by 1 on odd COLS).
    // The centre gap is exactly gap_w_ wide.
    const int total_w   = 2 * n * unit - gap_w_;
    const int left_edge = (COLS - total_w) / 2;
    const int centre    = left_edge + n * unit;

    resetPrev(n, avail);

    for (int b = 0; b < n; ++b) {
        const float rv    = (b < (int)right.size()) ? right[b] : 0.f;
        const float lv    = (b < (int)left.size())  ? left[b]  : 0.f;
        const int h_sub_r = std::clamp((int)(rv * avail * SUB), 0, avail * SUB);
        const int h_sub_l = std::clamp((int)(lv * avail * SUB), 0, avail * SUB);

        // RIGHT side: bass (b=0) nearest centre, treble outward
        const int rcol = centre + b * unit;
        if (rcol + bar_w_ <= COLS) {
            drawBarColumn(rcol, h_sub_r, prev_r_[b], avail);
            prev_r_[b] = h_sub_r;
        }

        // LEFT side: bass (b=0) nearest centre, treble outward
        const int lcol = centre - (b + 1) * unit;
        if (lcol >= 0) {
            drawBarColumn(lcol, h_sub_l, prev_l_[b], avail);
            prev_l_[b] = h_sub_l;
        }
    }
}

// ── HUD ───────────────────────────────────────────────────────────────────────
// Row 0: info bar   (theme, settings, fps)
// Row 1: thin separator line
// Rows 2..LINES-1: bars
void Renderer::drawStatusBar(double fps, const std::string& backend,
                               float sens, bool auto_sens) {
    if (LINES < HUD_ROWS + 1 || COLS < 20) return;

    mvhline(0, 0, ' ', COLS);

    char lbuf[280];
    std::snprintf(lbuf, sizeof(lbuf),
        " %s  \xc2\xb7  %s  \xc2\xb7  W:%d G:%d  \xc2\xb7  Sens:%.1f%s",
        backend.empty() ? "?" : backend.c_str(),
        themeName().c_str(),
        bar_w_, gap_w_,
        (double)sens, auto_sens ? " \xe2\x8a\x95" : "");

    char rbuf[32];
    std::snprintf(rbuf, sizeof(rbuf), "%.0f fps ", fps);
    const int rlen = (int)std::strlen(rbuf);

    attron(COLOR_PAIR(hudPair(2)) | A_BOLD);
    mvaddnstr(0, 0, lbuf, std::max(0, COLS - rlen - 1));
    attroff(COLOR_PAIR(hudPair(2)) | A_BOLD);

    attron(COLOR_PAIR(hudPair(3)) | A_BOLD);
    mvaddnstr(0, std::max(0, COLS - rlen), rbuf, rlen);
    attroff(COLOR_PAIR(hudPair(3)) | A_BOLD);

    attron(COLOR_PAIR(hudPair(0)));
    mvhline(1, 0, ACS_HLINE, COLS);
    attroff(COLOR_PAIR(hudPair(0)));
}

// ── Render ────────────────────────────────────────────────────────────────────
// HUD is fixed at the top (rows 0-1).  Bars always occupy rows 2..LINES-1.
void Renderer::render(const std::vector<float>& bars_l,
                       const std::vector<float>& bars_r,
                       double fps, const std::string& backend,
                       float sens, bool auto_sens) {
    if (!initialized_ || bars_l.empty()) return;

    const bool hud_now = (std::chrono::duration<double>(
                              Clock::now() - last_change_tp_).count() < HUD_HIDE_SECS);
    hud_visible_ = hud_now;

    const int avail = std::max(1, LINES - HUD_ROWS);

    // Rebuild gradient whenever terminal height changes.
    // grad_steps_ = avail: every row gets its own precisely sampled colour pair
    // (identical to CAVA's gradient_size = LINES). No two rows share a pair,
    // so the gradient is as smooth as the terminal can physically render.
    if (avail != prev_grad_avail_) {
        grad_steps_      = std::clamp(avail, 1, GRAD_STEPS_MAX);
        prev_grad_avail_ = avail;
        rebuildColors();
        needs_clear_ = true;
    }

    if (needs_clear_) {
        erase();
        needs_clear_ = false;
    }

    drawMirrorLR(bars_l, bars_r, avail);

    if (hud_now) {
        drawStatusBar(fps, backend, sens, auto_sens);
    } else {
        mvhline(0, 0, ' ', COLS);
        mvhline(1, 0, ' ', COLS);
    }

    refresh();
}
