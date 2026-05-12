#include "config.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

static std::string xdgBase(const char* var, const char* suffix) {
    const char* x = std::getenv(var);
    if (x && x[0]) return std::string(x);
    const char* h = std::getenv("HOME");
    if (h && h[0]) return std::string(h) + suffix;
    return "/tmp";
}
static void mkdirFor(const std::string& path) {
    std::string dir = path.substr(0, path.rfind('/'));
    mkdir(dir.c_str(), 0755);
}

std::string Config::configPath() {
    return xdgBase("XDG_CONFIG_HOME", "/.config") + "/cava-viz/config";
}
std::string Config::statePath() {
    return xdgBase("XDG_STATE_HOME", "/.local/state") + "/cava-viz/state";
}

static bool parseKV(const char* line, char key[64], char val[256]) {
    if (line[0]=='#'||line[0]=='\n'||line[0]=='\r') return false;
    if (std::sscanf(line," %63[^=] = %255[^\n]",key,val) != 2) return false;
    for (int i=(int)strlen(key)-1; i>=0&&key[i]==' '; --i) key[i]='\0';
    for (int i=(int)strlen(val)-1; i>=0&&(val[i]==' '||val[i]=='\r'); --i) val[i]='\0';
    return key[0] != '\0';
}
static bool asBool(const char* v) {
    return strcmp(v,"1")==0 || strcmp(v,"true")==0;
}

bool Config::load() {
    FILE* f = std::fopen(configPath().c_str(), "r");
    if (!f) return false;
    char line[512], k[64], v[256];
    while (std::fgets(line, sizeof(line), f)) {
        if (!parseKV(line, k, v)) continue;
        // Visual
        if      (!strcmp(k,"theme"))          theme          = std::atoi(v);
        else if (!strcmp(k,"bar_width"))       bar_width      = std::atoi(v);
        else if (!strcmp(k,"gap_width"))       gap_width      = std::atoi(v);
        else if (!strcmp(k,"hud_pinned"))      hud_pinned     = asBool(v);
        // Rendering modes
        else if (!strcmp(k,"outline_mode"))    outline_mode   = asBool(v);
        else if (!strcmp(k,"colour_cycle"))    colour_cycle   = asBool(v);
        else if (!strcmp(k,"per_bar_colour"))  per_bar_colour = asBool(v);
        // Audio
        else if (!strcmp(k,"stereo"))          stereo         = asBool(v);
        else if (!strcmp(k,"high_cutoff"))     high_cutoff    = std::atoi(v);
        // FFT / Smoothing
        else if (!strcmp(k,"gravity"))         gravity        = (float)std::atof(v);
        else if (!strcmp(k,"monstercat"))      monstercat     = (float)std::atof(v);
        else if (!strcmp(k,"rise_factor"))     rise_factor    = (float)std::atof(v);
        else if (!strcmp(k,"bass_smooth"))     bass_smooth    = (float)std::atof(v);
        // Audio processing
        else if (!strcmp(k,"a_weighting"))     a_weighting    = asBool(v);
        else if (!strcmp(k,"noise_gate"))      noise_gate     = (float)std::atof(v);
        else if (!strcmp(k,"auto_mono"))       auto_mono      = asBool(v);
        // Sensitivity
        else if (!strcmp(k,"sensitivity"))     sensitivity    = (float)std::atof(v);
        else if (!strcmp(k,"auto_sens"))       auto_sens      = asBool(v);
        // Performance
        else if (!strcmp(k,"fps"))             fps            = std::atoi(v);
    }
    std::fclose(f);

#define CW(field, lo, hi) \
    { auto _c=std::clamp(field,(lo),(hi)); \
      if(_c!=field) std::fprintf(stderr,"cava-viz: '%s' out of range, clamped.\n",#field); \
      field=_c; }
    CW(theme,        0,      11)
    CW(bar_width,    1,      8)
    CW(gap_width,    0,      2)
    CW(high_cutoff,  1000,   24000)
    CW(gravity,      0.1f,   5.0f)
    CW(monstercat,   0.0f,   5.0f)
    CW(rise_factor,  0.0f,   0.95f)
    CW(bass_smooth,  0.0f,   1.0f)
    CW(noise_gate,   0.0f,   0.2f)
    CW(sensitivity,  0.2f,   8.0f)
    CW(fps,          10,     240)
#undef CW
    return true;
}

void Config::save() const {
    const std::string p = configPath();
    mkdirFor(p);
    FILE* f = std::fopen(p.c_str(), "w");
    if (!f) return;

    fprintf(f, "# cava-viz configuration\n");
    fprintf(f, "# Edit while running — inotify reloads changes instantly.\n\n");

    fprintf(f, "# ── Visual ──────────────────────────────────────────────────────\n");
    fprintf(f, "# 0=Fire 1=Plasma 2=Neon 3=Teal 4=Sunset 5=Candy\n");
    fprintf(f, "# 6=Aurora 7=Inferno 8=White 9=Rose 10=Mermaid 11=Vapor\n");
    fprintf(f, "theme          = %d\n",   theme);
    fprintf(f, "bar_width      = %d\n",   bar_width);
    fprintf(f, "gap_width      = %d\n",   gap_width);
    fprintf(f, "hud_pinned     = %d\n",   hud_pinned     ? 1 : 0);

    fprintf(f, "\n# ── Rendering modes ──────────────────────────────────────────────\n");
    fprintf(f, "# outline_mode: draw only the top cell of each bar (lighter look)\n");
    fprintf(f, "outline_mode   = %d\n",   outline_mode   ? 1 : 0);
    fprintf(f, "# colour_cycle: slowly rotate gradient hue over time\n");
    fprintf(f, "colour_cycle   = %d\n",   colour_cycle   ? 1 : 0);
    fprintf(f, "# per_bar_colour: map colour to bar index (bass=dark, treble=bright)\n");
    fprintf(f, "per_bar_colour = %d\n",   per_bar_colour ? 1 : 0);

    fprintf(f, "\n# ── Audio ────────────────────────────────────────────────────────\n");
    fprintf(f, "stereo         = %d\n",   stereo         ? 1 : 0);
    fprintf(f, "high_cutoff    = %d\n",   high_cutoff);

    fprintf(f, "\n# ── FFT / Smoothing ──────────────────────────────────────────────\n");
    fprintf(f, "# gravity: fall speed (0.1=slow, 1.0=default, 5.0=instant)\n");
    fprintf(f, "gravity        = %.2f\n", (double)gravity);
    fprintf(f, "# monstercat: bar spread (0=off, 1.5=default)\n");
    fprintf(f, "monstercat     = %.2f\n", (double)monstercat);
    fprintf(f, "# rise_factor: attack smoothing (0.0=instant, 0.95=very slow)\n");
    fprintf(f, "rise_factor    = %.2f\n", (double)rise_factor);
    fprintf(f, "# bass_smooth: extra smoothing for bass bars (0.0=off, 0.1-0.3 recommended)\n");
    fprintf(f, "bass_smooth    = %.2f\n", (double)bass_smooth);

    fprintf(f, "\n# ── Audio processing ─────────────────────────────────────────────\n");
    fprintf(f, "# a_weighting: IEC 61672 perceptual frequency weighting\n");
    fprintf(f, "a_weighting    = %d\n",   a_weighting    ? 1 : 0);
    fprintf(f, "# noise_gate: bars below this (post-sens) snap to zero (0.0-0.2)\n");
    fprintf(f, "noise_gate     = %.3f\n", (double)noise_gate);
    fprintf(f, "# auto_mono: collapse stereo to mono when L/R are nearly identical\n");
    fprintf(f, "auto_mono      = %d\n",   auto_mono      ? 1 : 0);

    fprintf(f, "\n# ── Sensitivity ──────────────────────────────────────────────────\n");
    fprintf(f, "sensitivity    = %.2f\n", (double)sensitivity);
    fprintf(f, "auto_sens      = %d\n",   auto_sens      ? 1 : 0);

    fprintf(f, "\n# ── Performance ──────────────────────────────────────────────────\n");
    fprintf(f, "fps            = %d\n",   fps);

    std::fclose(f);
}

bool Config::loadState() {
    FILE* f = std::fopen(statePath().c_str(), "r");
    if (!f) return false;
    char line[512], k[64], v[256];
    while (std::fgets(line, sizeof(line), f)) {
        if (!parseKV(line, k, v)) continue;
        if (!strcmp(k, "last_source")) last_source = v;
    }
    std::fclose(f);
    return true;
}
void Config::saveState() const {
    const std::string p = statePath();
    mkdirFor(p);
    FILE* f = std::fopen(p.c_str(), "w");
    if (!f) return;
    fprintf(f, "# cava-viz internal state — do not edit\n");
    fprintf(f, "last_source = %s\n", last_source.c_str());
    std::fclose(f);
}
