#include "renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <locale.h>

// Unicode eighth-block characters ▁–█ for sub-cell bar tips.
static const char *kBlk[9] = {
    " ",
    "\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84",
    "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88",
};
static constexpr int SUB = 8; // sub-cell divisions per row

static const char *kThemeNames[static_cast<int>(Theme::COUNT)] = {
    "Fire", "Plasma", "Neon",    "Teal",    "Sunset", "Candy",
    "Aurora", "Inferno", "White", "Rose", "Mermaid", "Vapor",
};

struct RGB  { short r, g, b; };
struct Stop { float t; RGB c; };

// ── Gradient palette ──────────────────────────────────────────────────────────
//
// Design rules
//   1.  t = 0 (bar bottom): vivid, saturated colour — never near-black.
//       Short bars look rich, not like dark smudges.
//   2.  Each theme follows a clean HSV arc so adjacent rows have similar hues.
//       Measured max per-row Δ < 80 ncurses units (imperceptible banding).
//   3.  smoothstep() inside sampleTheme makes each stop interval C1-continuous,
//       eliminating the slope-kink that plain linear lerp leaves at boundaries.
//   4.  GAMMA = 1.0 (linear palette mapping) distributes colour variation evenly
//       across all rows — the smallest possible worst-case per-row delta.
//
// All values are in ncurses 0-1000 scale (r, g, b).
//
static const Stop kGrad[static_cast<int>(Theme::COUNT)][7] = {
    // FIRE  orange → orange-red → red → rose-red → hot-pink
    {{{0.00f}, {1000, 467,   0}}, {{0.17f}, {1000, 267,   0}},
     {{0.33f}, {1000,  67,   0}}, {{0.50f}, { 980,   0, 163}},
     {{0.67f}, { 940,   0, 345}}, {{0.83f}, { 900,   0, 525}},
     {{1.00f}, { 860,   0, 659}}},

    // PLASMA  blue-violet → electric-blue → neon-cyan
    {{{0.00f}, { 301,   0, 820}}, {{0.17f}, { 117,   0, 880}},
     {{0.33f}, {   0,  93, 930}}, {{0.50f}, {   0, 356, 970}},
     {{0.67f}, {  30, 612, 1000}}, {{0.83f}, {  70, 814, 1000}},
     {{1.00f}, { 100, 1000, 1000}}},

    // NEON  electric-magenta → violet → electric-blue
    {{{0.00f}, { 920,   0, 767}}, {{0.17f}, { 918,   0, 950}},
     {{0.33f}, { 744,   0, 970}}, {{0.50f}, { 567,   0, 1000}},
     {{0.67f}, { 333,   0, 1000}}, {{0.83f}, {  67,   0, 1000}},
     {{1.00f}, {   0, 200, 1000}}},

    // TEAL  dark-teal → teal → bright-aqua
    {{{0.00f}, {  84, 700, 618}}, {{0.17f}, {  77, 770, 712}},
     {{0.33f}, {  66, 830, 805}}, {{0.50f}, {  45, 876, 890}},
     {{0.67f}, {  28, 879, 940}}, {{0.83f}, {  10, 867, 980}},
     {{1.00f}, {   0, 800, 1000}}},

    // SUNSET  orange-amber → orange-red → crimson → deep-violet-pink
    {{{0.00f}, {1000, 583,   0}}, {{0.17f}, {1000, 367,   0}},
     {{0.33f}, {1000, 133,   0}}, {{0.50f}, { 980,   0,  98}},
     {{0.67f}, { 950,   0, 317}}, {{0.83f}, { 920,   0, 552}},
     {{1.00f}, { 900,   0, 750}}},

    // CANDY  hot-rose → magenta → violet
    {{{0.00f}, {1000,   0, 333}}, {{0.17f}, {1000,   0, 533}},
     {{0.33f}, {1000,   0, 700}}, {{0.50f}, {1000,   0, 867}},
     {{0.67f}, { 933,   0, 1000}}, {{0.83f}, { 733,   0, 1000}},
     {{1.00f}, { 533,   0, 1000}}},

    // AURORA  sea-green → teal → sky-blue → soft-violet
    {{{0.00f}, {   0, 780, 364}}, {{0.17f}, {   0, 840, 588}},
     {{0.33f}, {  36, 900, 842}}, {{0.50f}, {  76, 775, 950}},
     {{0.67f}, {  98, 568, 980}}, {{0.83f}, { 130, 362, 1000}},
     {{1.00f}, { 216, 160, 1000}}},

    // INFERNO  blood-red → red-orange → amber → bright-gold
    {{{0.00f}, { 760,  51,   0}}, {{0.17f}, { 820, 164,   0}},
     {{0.33f}, { 880, 323,   0}}, {{0.50f}, { 930, 496,   0}},
     {{0.67f}, { 970, 679,   0}}, {{0.83f}, {1000, 867,   0}},
     {{1.00f}, {1000, 1000,   0}}},

    // WHITE  cool-slate → silver → pearl → pure-white
    {{{0.00f}, { 370, 391, 420}}, {{0.17f}, { 468, 490, 520}},
     {{0.33f}, { 580, 601, 630}}, {{0.50f}, { 686, 704, 730}},
     {{0.67f}, { 797, 811, 830}}, {{0.83f}, { 902, 909, 920}},
     {{1.00f}, {1000, 1000, 1000}}},

    // ROSE  crimson-rose → coral → salmon → orchid-pink
    {{{0.00f}, { 840,  84, 109}}, {{0.17f}, { 880, 123, 262}},
     {{0.33f}, { 920, 166, 417}}, {{0.50f}, { 950, 228, 577}},
     {{0.67f}, { 980, 294, 728}}, {{0.83f}, {1000, 360, 893}},
     {{1.00f}, { 962, 430, 1000}}},

    // MERMAID  ocean-blue → teal → periwinkle → orchid
    {{{0.00f}, {  94, 528, 780}}, {{0.17f}, { 126, 388, 840}},
     {{0.33f}, { 160, 258, 890}}, {{0.50f}, { 285, 186, 930}},
     {{0.67f}, { 516, 213, 970}}, {{0.83f}, { 747, 240, 1000}},
     {{1.00f}, { 976, 270, 1000}}},

    // VAPOR (synthwave)  deep-neon-purple → violet-magenta → hot-pink
    {{{0.00f}, { 352,   0, 660}}, {{0.17f}, { 555,   0, 740}},
     {{0.33f}, { 793,   0, 820}}, {{0.50f}, { 880,   0, 733}},
     {{0.67f}, { 940,   0, 595}}, {{0.83f}, { 980,   0, 425}},
     {{1.00f}, {1000,   0, 267}}},
};

// ── 256-colour fallback palettes ──────────────────────────────────────────────
static const short kFall[static_cast<int>(Theme::COUNT)][48] = {
    {202, 208, 208, 214, 214, 220, 196, 196, 160, 160, 161, 125, 125,  89, // FIRE
      89,  53,  89, 125, 161, 196, 202, 208, 214, 220, 226, 220, 214, 208,
     202, 196, 161, 125,  89,  53,  89, 125, 161, 196, 202, 208, 214, 220,
     220, 226, 231, 231, 231, 231},
    { 93,  57,  57,  21,  27,  33,  39,  45,  51,  87, 123, 159, 195, 231, // PLASMA
     195, 159, 123,  87,  51,  45,  39,  33,  27,  21,  57,  93, 129, 165,
     201, 207, 213, 219, 225, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231, 231, 231},
    {127, 163, 163, 199, 205, 205, 211, 211, 217, 183, 183, 189, 189, 225, // NEON
     225, 231, 225, 219, 213, 207, 201, 207, 213, 219, 225, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    { 30,  36,  37,  43,  44,  45,  51,  87, 123, 159, 195, 231, 231, 231, // TEAL
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    {214, 208, 202, 196, 160, 124, 125,  89,  53,  54,  55,  91,  92, 128, // SUNSET
     129, 165, 201, 207, 213, 219, 225, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    {163, 127,  91,  57,  21,  27,  33,  39,  45,  51,  87, 123, 159, 195, // CANDY
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    { 35,  41,  47,  83, 119, 155, 191, 227, 225, 219, 213, 207, 201, 165, // AURORA
     129,  93,  57,  93, 129, 165, 201, 207, 213, 219, 225, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    {124, 160, 196, 202, 208, 214, 220, 226, 227, 228, 229, 230, 231, 231, // INFERNO
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    {240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, // WHITE
     254, 255, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    {161, 162, 168, 204, 210, 211, 217, 218, 224, 225, 231, 231, 231, 231, // ROSE
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    { 27,  33,  38,  44,  43,  79, 115, 151, 152, 153, 189, 225, 219, 213, // MERMAID
     207, 201, 165, 129,  93,  57,  93, 129, 165, 201, 207, 213, 219, 225,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
    { 93,  99, 163, 199, 205, 211, 217, 183, 189, 225, 231, 231, 231, 231, // VAPOR
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231,
     231, 231, 231, 231},
};

// ── Colour helpers ────────────────────────────────────────────────────────────
static RGB lerpRGB(RGB a, RGB b, float t) noexcept {
    return {
        (short)(a.r + (b.r - a.r) * t),
        (short)(a.g + (b.g - a.g) * t),
        (short)(a.b + (b.b - a.b) * t),
    };
}

// Cubic smoothstep — C1-continuous at stop boundaries.
// Prevents the slope discontinuity (faint band) that linear lerp leaves where
// two gradient segments meet.
static float smoothstep(float t) noexcept {
    return t * t * (3.0f - 2.0f * t);
}

static RGB sampleTheme(int ti, float t) noexcept {
    const Stop *s = kGrad[ti];
    for (int i = 1; i < 7; ++i) {
        if (t <= s[i].t) {
            float lt = (t - s[i - 1].t) / (s[i].t - s[i - 1].t);
            lt = smoothstep(std::clamp(lt, 0.f, 1.f));
            return lerpRGB(s[i - 1].c, s[i].c, lt);
        }
    }
    return s[6].c;
}

static RGB hsvToRgb(float h, float s, float v) noexcept {
    const float hh = std::fmod(h, 360.f) / 60.f;
    const int i = (int)hh;
    const float ff = hh - i;
    const float p = v * (1 - s);
    const float q = v * (1 - s * ff);
    const float t_ = v * (1 - s * (1 - ff));
    float r = 0, g = 0, b = 0;
    switch (i) {
    case 0: r = v;  g = t_; b = p;  break;
    case 1: r = q;  g = v;  b = p;  break;
    case 2: r = p;  g = v;  b = t_; break;
    case 3: r = p;  g = q;  b = v;  break;
    case 4: r = t_; g = p;  b = v;  break;
    default: r = v; g = p;  b = q;  break;
    }
    return {(short)(r * 1000), (short)(g * 1000), (short)(b * 1000)};
}

static RGB blendRGB(RGB a, RGB b, float t) noexcept {
    return {
        (short)(a.r + (b.r - a.r) * t),
        (short)(a.g + (b.g - a.g) * t),
        (short)(a.b + (b.b - a.b) * t),
    };
}

// ── Truecolor detection ───────────────────────────────────────────────────────
// Most modern terminals support init_color() but their TERM=xterm-256color
// terminfo entry omits the  LIT0  capability, so can_change_color() returns
// false. Detect via the environment variables terminals reliably set instead.
bool Renderer::detectTruecolor() noexcept {
    const char *ct = std::getenv("COLORTERM");
    if (ct && (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0))
        return true;
    if (std::getenv("KONSOLE_VERSION"))  return true; // Konsole
    if (std::getenv("KITTY_WINDOW_ID"))  return true; // kitty
    if (std::getenv("VTE_VERSION"))      return true; // GNOME / Xfce / Terminator
    if (std::getenv("WT_SESSION"))       return true; // Windows Terminal
    if (std::getenv("ITERM_SESSION_ID")) return true; // iTerm2
    return false;
}

Renderer::Renderer() { last_frame_tp_ = Clock::now(); }
Renderer::~Renderer() {
    if (initialized_)
        endwin();
}

void Renderer::applyNcursesSettings() {
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
}

bool Renderer::init() {
    setlocale(LC_ALL, "");
    set_escdelay(20);
    initscr();
    applyNcursesSettings();
    if (!has_colors()) {
        endwin();
        return false;
    }
    start_color();
    use_default_colors();
    can_rgb_ = (COLORS >= 256) && (can_change_color() || detectTruecolor());
    grad_steps_ = GRAD_STEPS_MAX;
    rebuildColors();
    last_change_tp_ = Clock::now();
    last_frame_tp_ = Clock::now();
    initialized_ = true;
    return true;
}

void Renderer::handleResize() {
    endwin();
    refresh();
    clear();
    start_color();
    use_default_colors();
    can_rgb_ = (COLORS >= 256) && (can_change_color() || detectTruecolor());
    grad_steps_ = GRAD_STEPS_MAX;
    applyNcursesSettings();
    rebuildColors();
    invalidatePrev();
    prev_grad_avail_ = 0;
    needs_clear_ = true;
    notifyChange();
}

// ── rebuildColors ─────────────────────────────────────────────────────────────
//
// Colour index safety
//   A standard xterm-256color terminal has COLORS = 256 (indices 0..255).
//   We start at COLOR_BASE = 16, so at most 240 slots are available.
//   Reserving HUD_PAIRS = 4 slots at the end leaves 236 gradient entries.
//   The safe cap is:  grad_steps_ <= COLORS - COLOR_BASE - HUD_PAIRS
//   Without this clamp init_color() silently fails for out-of-range indices
//   and those palette entries stay at the terminal's default foreground
//   (usually white), which appeared as white ghost bars near the HUD.
//
// Gradient mapping
//   t = pow(raw, GAMMA)  where raw = palette_index / (steps - 1)
//   GAMMA = 1.0 → linear mapping; distributes colour variation evenly
//           across all rows, minimising the worst-case per-row delta.
//
void Renderer::rebuildColors() {
    static constexpr float T_MIN = 0.00f;
    static constexpr float T_SPAN = 1.00f;
    static constexpr float GAMMA = 1.00f;

    // Clamp to the number of colour indices we can safely use.
    const int max_safe = std::max(4, COLORS - (int)COLOR_BASE - HUD_PAIRS);
    grad_steps_ = std::min(GRAD_STEPS_MAX, max_safe);

    const int ti = (int)theme_;
    const float hue_a = colour_cycle_ ? 0.38f : 0.0f;

    if (can_rgb_) {
        for (int i = 0; i < grad_steps_; ++i) {
            const float raw = (float)i / std::max(1, grad_steps_ - 1);
            const float t = T_MIN + std::pow(raw, GAMMA) * T_SPAN;
            RGB rgb = sampleTheme(ti, t);
            if (hue_a > 0.f) {
                RGB hue = hsvToRgb(hue_offset_ + raw * 60.f, 0.82f, 0.92f);
                rgb = blendRGB(rgb, hue, hue_a);
            }
            const short ci = (short)(COLOR_BASE + i);
            init_color(ci, rgb.r, rgb.g, rgb.b);
            init_pair(i + 1, ci, -1);
        }
    } else if (COLORS >= 256) {
        const short *pal = kFall[ti];
        for (int i = 0; i < grad_steps_; ++i) {
            const float raw = (float)i / std::max(1, grad_steps_ - 1);
            const float t = T_MIN + std::pow(raw, GAMMA) * T_SPAN;
            const int pi = std::clamp((int)(t * 47.f + 0.5f), 0, 47);
            init_pair(i + 1, pal[pi], -1);
        }
    } else {
        for (int i = 0; i < grad_steps_; ++i) {
            const float f = (float)i / std::max(1, grad_steps_ - 1);
            const short fg = f < 0.40f ? COLOR_YELLOW
                           : f < 0.72f ? COLOR_RED
                           : COLOR_WHITE;
            init_pair(i + 1, fg, -1);
        }
    }

    // HUD accent colour pairs — placed immediately after the gradient pairs.
    // Colour indices: COLOR_BASE + grad_steps_ .. COLOR_BASE + grad_steps_ + HUD_PAIRS - 1
    // Ncurses pairs:  grad_steps_ + 1          .. grad_steps_ + HUD_PAIRS
    // Both ranges are always within bounds given the safe clamp above.
    static const float kHF[HUD_PAIRS] = {0.55f, 0.72f, 0.88f, 1.00f};
    for (int lv = 0; lv < HUD_PAIRS; ++lv) {
        const short hud_ci = (short)(COLOR_BASE + grad_steps_ + lv);
        if (can_rgb_) {
            RGB rgb = sampleTheme(ti, kHF[lv]);
            init_color(hud_ci, rgb.r, rgb.g, rgb.b);
            init_pair(grad_steps_ + 1 + lv, hud_ci, -1);
        } else if (COLORS >= 256) {
            init_pair(grad_steps_ + 1 + lv,
                      kFall[ti][(int)(kHF[lv] * 47)], -1);
        } else {
            const short fg = kHF[lv] < 0.75f ? COLOR_YELLOW : COLOR_WHITE;
            init_pair(grad_steps_ + 1 + lv, fg, -1);
        }
    }
}

int Renderer::gradPair(float frac) const noexcept {
    int idx = (int)(frac * std::max(1, grad_steps_ - 1) + 0.5f);
    return std::clamp(idx, 0, grad_steps_ - 1) + 1;
}

int Renderer::hudPair(int lv) const noexcept {
    return grad_steps_ + 1 + std::clamp(lv, 0, HUD_PAIRS - 1);
}

// ── Public helpers ────────────────────────────────────────────────────────────
int Renderer::cols() const { return COLS; }
int Renderer::rows() const { return LINES; }

int Renderer::barCount() const {
    const int unit = std::max(1, bar_w_ + gap_w_);
    return std::max(1, (COLS + gap_w_) / (2 * unit));
}

int Renderer::autoBarWidth() const {
    return std::clamp(COLS / 40, BAR_W_MIN, BAR_W_MAX);
}

void Renderer::notifyChange() {
    last_change_tp_ = Clock::now();
    hud_visible_ = true;
}

void Renderer::invalidatePrev() {
    prev_l_.assign(prev_l_.size(), -1);
    prev_r_.assign(prev_r_.size(), -1);
}

void Renderer::showFeedback(const std::string &msg) {
    feedback_msg_ = msg;
    feedback_tp_ = Clock::now();
    feedback_active_ = true;
    notifyChange();
}

void Renderer::setTheme(Theme t) {
    theme_ = t;
    if (initialized_)
        rebuildColors();
    invalidatePrev();
    needs_clear_ = true;
}

Theme Renderer::nextTheme() {
    theme_ = (Theme)(((int)theme_ + 1) % (int)Theme::COUNT);
    if (initialized_)
        rebuildColors();
    invalidatePrev();
    needs_clear_ = true;
    notifyChange();
    return theme_;
}

std::string Renderer::themeName() const { return kThemeNames[(int)theme_]; }

int Renderer::increaseBarWidth() {
    bar_w_ = std::min(bar_w_ + 1, BAR_W_MAX);
    invalidatePrev();
    needs_clear_ = true;
    showFeedback("Width: " + std::to_string(bar_w_));
    return bar_w_;
}

int Renderer::decreaseBarWidth() {
    bar_w_ = std::max(bar_w_ - 1, BAR_W_MIN);
    invalidatePrev();
    needs_clear_ = true;
    showFeedback("Width: " + std::to_string(bar_w_));
    return bar_w_;
}

void Renderer::setBarWidth(int w) {
    bar_w_ = std::clamp(w, BAR_W_MIN, BAR_W_MAX);
    invalidatePrev();
    needs_clear_ = true;
}

int Renderer::cycleGap() {
    gap_w_ = (gap_w_ >= 2) ? 0 : gap_w_ + 1;
    invalidatePrev();
    needs_clear_ = true;
    showFeedback("Gap: " + std::to_string(gap_w_));
    return gap_w_;
}

void Renderer::resetPrev(int n, int avail) {
    if ((int)prev_l_.size() != n || prev_avail_ != avail) {
        prev_l_.assign(n, -1);
        prev_r_.assign(n, -1);
        prev_avail_ = avail;
    }
}

// ── drawBarColumn ─────────────────────────────────────────────────────────────
void Renderer::drawBarColumn(int col, int bar_idx, int total_bars,
                              int height_sub, int prev_sub, int avail) {
    if (col < 0 || col + bar_w_ > COLS || avail <= 0)
        return;

    const bool force = (prev_sub < 0);
    if (!force && height_sub == prev_sub)
        return;

    const int prev_h = force ? 0 : prev_sub;
    const int cur_lh = height_sub / SUB;
    const int prev_lh = prev_h / SUB;

    int max_line = (std::max(height_sub, prev_h) + SUB) / SUB;
    max_line = std::min(max_line, avail);

    char lbuf[BAR_W_MAX * 3 + 1];

    for (int line = 0; line < max_line; ++line) {
        const int row = HUD_ROWS + avail - 1 - line;
        if (row < HUD_ROWS || row >= LINES)
            continue;

        // frac = 0 → bottom of bar (vivid base colour)
        // frac = 1 → tip of bar   (theme's tip colour)
        float frac;
        if (per_bar_colour_ && total_bars > 1)
            frac = (float)bar_idx / (float)(total_bars - 1);
        else
            frac = (avail > 1) ? (float)line / (float)(avail - 1) : 1.f;

        const int cp = gradPair(frac);
        const int attr = beat_flash_ ? (COLOR_PAIR(cp) | A_BOLD) : COLOR_PAIR(cp);

        // ── Outline mode ──────────────────────────────────────────────────────
        if (outline_mode_) {
            if (line == cur_lh && height_sub > 0) {
                int bar_step = height_sub % SUB;
                if (bar_step == 0)
                    bar_step = SUB - 1;
                else
                    bar_step--;
                const char *blk = kBlk[std::clamp(bar_step + 1, 1, 8)];
                for (int w = 0; w < bar_w_; ++w)
                    memcpy(lbuf + w * 3, blk, 3);
                lbuf[bar_w_ * 3] = '\0';
                attron(attr);
                mvaddstr(row, col, lbuf);
                attroff(attr);
            } else if (!force && line <= prev_lh && line != cur_lh) {
                mvhline(row, col, ' ', bar_w_);
            }
            continue;
        }

        // ── Filled bar ────────────────────────────────────────────────────────
        if (height_sub >= line * SUB + 1) {
            int bar_step;
            if (cur_lh == line) {
                bar_step = height_sub % SUB;
                if (bar_step == 0)
                    bar_step = SUB - 1;
                else
                    bar_step--;
            } else if (force || prev_lh <= line) {
                bar_step = SUB - 1;
            } else {
                continue;
            }
            const char *blk = kBlk[std::clamp(bar_step + 1, 1, 8)];
            for (int w = 0; w < bar_w_; ++w)
                memcpy(lbuf + w * 3, blk, 3);
            lbuf[bar_w_ * 3] = '\0';
            attron(attr);
            mvaddstr(row, col, lbuf);
            attroff(attr);
        } else if (!force && prev_lh >= line) {
            mvhline(row, col, ' ', bar_w_);
        }
    }
}

// ── drawMirrorLR ──────────────────────────────────────────────────────────────
void Renderer::drawMirrorLR(const std::vector<float> &left,
                              const std::vector<float> &right, int avail) {
    if (COLS <= 0 || avail <= 0)
        return;

    const int n = (int)left.size();
    const int unit = bar_w_ + gap_w_;

    const int total_w = 2 * n * unit - gap_w_;
    const int left_edge = (COLS - total_w) / 2;
    const int centre = left_edge + n * unit;

    resetPrev(n, avail);

    for (int b = 0; b < n; ++b) {
        const float rv = (b < (int)right.size()) ? right[b] : 0.f;
        const float lv = (b < (int)left.size())  ? left[b]  : 0.f;
        const int h_sub_r = std::clamp((int)(rv * avail * SUB), 0, avail * SUB);
        const int h_sub_l = std::clamp((int)(lv * avail * SUB), 0, avail * SUB);

        const int rcol = centre + b * unit;
        if (rcol + bar_w_ <= COLS) {
            drawBarColumn(rcol, b, n, h_sub_r, prev_r_[b], avail);
            prev_r_[b] = h_sub_r;
        }
        const int lcol = centre - (b + 1) * unit;
        if (lcol >= 0) {
            drawBarColumn(lcol, b, n, h_sub_l, prev_l_[b], avail);
            prev_l_[b] = h_sub_l;
        }
    }
}

// ── drawStatusBar ─────────────────────────────────────────────────────────────
void Renderer::drawStatusBar(double fps, const std::string &backend,
                              float sens, bool auto_sens) {
    if (LINES < HUD_ROWS + 1 || COLS < 10)
        return;

    mvhline(0, 0, ' ', COLS); // clear row 0 first

    const bool show_fb = feedback_active_ &&
        (std::chrono::duration<double>(Clock::now() - feedback_tp_).count()
         < FEEDBACK_SECS);
    if (!show_fb)
        feedback_active_ = false;

    char lbuf[512];
    if (show_fb) {
        std::snprintf(lbuf, sizeof(lbuf), "  %s", feedback_msg_.c_str());
    } else {
        char flags[16] = {};
        int fi = 0;
        if (outline_mode_)   flags[fi++] = 'O';
        if (colour_cycle_)   flags[fi++] = 'C';
        if (per_bar_colour_) flags[fi++] = 'B';
        flags[fi] = '\0';

        char src_buf[80] = {};
        if (!source_name_.empty())
            std::snprintf(src_buf, sizeof(src_buf), "  %.38s", source_name_.c_str());

        std::snprintf(lbuf, sizeof(lbuf),
                      " %s  %s  W:%d G:%d  Sens:%.1f%s%s%s",
                      backend.empty() ? "?" : backend.c_str(),
                      themeName().c_str(),
                      bar_w_, gap_w_,
                      (double)sens,
                      auto_sens   ? " A"   : "",
                      hud_pinned_ ? " PIN" : "",
                      fi > 0 ? (std::string(" [") + flags + "]").c_str() : "");

        if (src_buf[0])
            std::strncat(lbuf, src_buf, sizeof(lbuf) - strlen(lbuf) - 1);
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

    // Row 1: thin separator in the theme's mid-gradient accent colour.
    attron(COLOR_PAIR(hudPair(0)));
    mvhline(1, 0, ACS_HLINE, COLS);
    attroff(COLOR_PAIR(hudPair(0)));
}

// ── render ────────────────────────────────────────────────────────────────────
void Renderer::render(const std::vector<float> &bars_l,
                      const std::vector<float> &bars_r,
                      double fps, const std::string &backend,
                      float sens, bool auto_sens) {
    if (!initialized_ || bars_l.empty())
        return;

    // Colour cycle: advance hue offset by elapsed wall time.
    const auto now_tp = Clock::now();
    if (colour_cycle_) {
        const float dt =
            std::chrono::duration<float>(now_tp - last_frame_tp_).count();
        hue_offset_ = std::fmod(hue_offset_ + HUE_DEG_PER_SEC * dt, 360.f);
        rebuildColors();
    }
    last_frame_tp_ = now_tp;

    // HUD visibility window.
    const double elapsed =
        std::chrono::duration<double>(now_tp - last_change_tp_).count();
    hud_visible_ = hud_pinned_ || (elapsed < HUD_HIDE_SECS) || feedback_active_;

    // Rebuild palette only when the terminal is resized.
    const int avail = std::max(1, LINES - HUD_ROWS);
    if (avail != prev_grad_avail_) {
        grad_steps_ = GRAD_STEPS_MAX; // rebuildColors clamps to safe value
        prev_grad_avail_ = avail;
        rebuildColors();
        needs_clear_ = true;
    }

    // Beat flash: average of the lowest 4 bars.
    bool do_clear = needs_clear_;
    {
        const int nb = (int)bars_l.size();
        const int lim = std::min(nb, 4);
        float lfe = 0.f;
        for (int i = 0; i < lim; ++i)
            lfe += bars_l[i];
        const bool beat_now = (lim > 0) && (lfe / lim >= BEAT_THRESHOLD);
        if (beat_now != beat_flash_) {
            do_clear = true;
            invalidatePrev();
        }
        beat_flash_ = beat_now;
    }

    if (do_clear) {
        erase();
        needs_clear_ = false;
    }

    last_bar_count_ = (int)bars_l.size();

    drawMirrorLR(bars_l, bars_r, avail);

    if (hud_visible_) {
        drawStatusBar(fps, backend, sens, auto_sens);
    } else {
        mvhline(0, 0, ' ', COLS);
        mvhline(1, 0, ' ', COLS);
    }

    refresh();
}
