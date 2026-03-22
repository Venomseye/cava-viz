/*
 * Terminal Audio Visualizer
 *
 * Build:
 *   g++ visualizer.cpp -o visualizer \
 *       -lncursesw -lpulse-simple -lpulse -lfftw3 -lm -O2 -std=c++17
 *
 * Settings auto-saved to ~/.config/visualizer.conf
 *
 * ╭──────────────────────────────────────────────────────╮
 * │  Controls  (press any key to show, hides after 3 s)  │
 * │  1-4   Filled · Mirror · Bounce · Classic            │
 * │  m     Mono / Stereo                                 │
 * │  t/T   Next / Prev theme                             │
 * │  +/-   Sensitivity                                   │
 * │  [/]   Peak fall speed                               │
 * │  s     Peak dots on/off                              │
 * │  b     Blur/trail on/off                             │
 * │  r     Reset   q  Quit                               │
 * ╰──────────────────────────────────────────────────────╯
 */

#include <ncurses.h>
#include <locale.h>
#include <wchar.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <fftw3.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <chrono>
#include <signal.h>

using Clock = std::chrono::steady_clock;

// ─── Audio ────────────────────────────────────────────────────────────────────
static const int BUFFER_SIZE = 2048;
static const int HOP         = 256;
static const int SAMPLE_RATE = 48000;

// ─── Bar geometry ────────────────────────────────────────────────────────────
static const int BAR_W   = 2;   // character columns per bar
static const int BAR_GAP = 1;   // gap between bars
static const int CELL_W  = BAR_W + BAR_GAP;

// ─── Dynamics ────────────────────────────────────────────────────────────────
static const float ATTACK         = 0.50f;
static const float DECAY          = 0.78f;
static const int   PEAK_HOLD_DEF  = 22;
static const float PEAK_FALL_MIN  = 0.0006f;
static const float PEAK_FALL_MAX  = 0.014f;
static const float PEAK_FALL_DEF  = 0.003f;
static const float PEAK_FALL_STEP = 0.001f;
static const float GRAV           = 0.035f;  // bounce gravity

// ─── Afterglow / trail ────────────────────────────────────────────────────────
// Each bar keeps a "glow" value that snaps to smooth[] on rise,
// then decays slowly — leaving a visible fading ghost above the bar.
// GLOW_DECAY closer to 1.0 = longer, more visible trail.
static const float GLOW_DECAY = 0.92f;  // trail lasts ~12 frames = ~200ms visible

// ─── Frequency range ─────────────────────────────────────────────────────────
static const double FREQ_LO = 20.0;
static const double FREQ_HI = 20000.0;

// ─── Sensitivity ─────────────────────────────────────────────────────────────
static const float SENS_DEFAULT = 1.4f;
static const float SENS_MIN     = 0.2f;
static const float SENS_MAX     = 8.0f;
static const float SENS_STEP    = 0.2f;

// ─── HUD ──────────────────────────────────────────────────────────────────────
static const int HUD_MS = 3500;

// ─── Color pair layout ───────────────────────────────────────────────────────
// 1..32   bar gradient  (32 steps for ultra-smooth blend)
// 33      peak marker
// 34      blur/ghost layer (dim version of gradient base)
// 35      HUD background
// 36      HUD key (accent)
// 37      HUD value
// 38      HUD separator
static const int C_GRAD_BASE = 1;
static const int C_GRAD_N    = 32;
static const int C_PEAK      = C_GRAD_BASE + C_GRAD_N;  // 33
static const int C_BLUR      = C_PEAK  + 1;              // 34
static const int C_HUD_BG    = C_BLUR  + 1;              // 35
static const int C_HUD_KEY   = C_HUD_BG  + 1;            // 36
static const int C_HUD_VAL   = C_HUD_KEY + 1;            // 37
static const int C_HUD_SEP   = C_HUD_VAL + 1;            // 38

// ─── Styles ───────────────────────────────────────────────────────────────────
enum Style { FILLED=0, MIRROR, BOUNCE, TRADITIONAL, CLASSIC_FILLED, N_STYLES };
static const char* STYLE_NAMES[] = { "Filled", "Mirror", "Bounce", "Classic", "ClassicFill" };

// ─── Themes ───────────────────────────────────────────────────────────────────
struct Theme {
    const char* name;
    int lo, hi;      // xterm-256 bar gradient endpoints
    int peak;        // peak marker colour
    int accent;      // HUD accent colour
    int blurCol;     // blur/ghost dim colour
};

static const Theme THEMES[] = {
    // name        lo   hi   peak  accent  blur
    { "Spectrum",  57, 196,  196,   213,   54  }, // violet→red
    { "Fire",      52, 220,  220,   214,   88  }, // deep red→yellow
    { "Ice",       17,  51,   51,    45,   18  }, // navy→cyan
    { "Neon",     201, 226,  220,   201,  128  }, // pink→yellow
    { "Matrix",    22, 154,  154,    82,   22  }, // dark→bright green
    { "Sunset",    54, 214,  214,   204,   53  }, // purple→gold
    { "Ocean",     17,  45,   45,    38,   17  }, // abyss→aqua
    { "Lava",      88, 202,  202,   160,   52  }, // dark red→orange
    { "Aurora",    54,  86,   86,   105,   17  }, // indigo→teal
    { "Candy",    200, 229,  229,   219,  163  }, // pink→yellow
    { "Toxic",     58, 118,  118,    82,   22  }, // olive→lime
    { "Mono",     234, 255,  255,   250,  238  }, // near-black→white
};
static const int N_THEMES = (int)(sizeof(THEMES)/sizeof(THEMES[0]));

// ─── RGB helpers ─────────────────────────────────────────────────────────────
static void xterm256ToRGB(int c, int& r, int& g, int& b) {
    if (c >= 232) { int v=8+(c-232)*10; r=g=b=v; return; }
    if (c >= 16)  { int i=c-16; b=(i%6)*51; i/=6; g=(i%6)*51; i/=6; r=i*51; return; }
    static const int T[16][3]={
        {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},
        {0,128,128},{192,192,192},{128,128,128},{255,0,0},{0,255,0},
        {255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}
    };
    r=T[c][0]; g=T[c][1]; b=T[c][2];
}

static int rgbToXterm256(int r, int g, int b) {
    r=std::clamp(r,0,255); g=std::clamp(g,0,255); b=std::clamp(b,0,255);
    if (std::abs(r-g)<14 && std::abs(g-b)<14 && std::abs(r-b)<14) {
        int v=(r+g+b)/3;
        if (v<8) return 16; if (v>238) return 231;
        return 232+(v-8)/10;
    }
    return 16 + 36*std::clamp((r+25)/51,0,5)
              +  6*std::clamp((g+25)/51,0,5)
              +    std::clamp((b+25)/51,0,5);
}

static int lerpColor(int c0, int c1, float t) {
    int r0,g0,b0,r1,g1,b1;
    xterm256ToRGB(c0,r0,g0,b0);
    xterm256ToRGB(c1,r1,g1,b1);
    return rgbToXterm256(
        r0+(int)((r1-r0)*t),
        g0+(int)((g1-g0)*t),
        b0+(int)((b1-b0)*t));
}

static int gradTable[C_GRAD_N];  // precomputed pair indices

static void applyTheme(int t) {
    const Theme& th = THEMES[t % N_THEMES];
    for (int i=0; i<C_GRAD_N; i++) {
        float frac = (float)i / (C_GRAD_N-1);
        init_pair(C_GRAD_BASE+i, lerpColor(th.lo, th.hi, frac), -1);
        gradTable[i] = C_GRAD_BASE+i;
    }
    init_pair(C_PEAK,    th.peak,    -1 );
    init_pair(C_BLUR,    th.blurCol, -1 );
    init_pair(C_HUD_BG,  236,        235);
    init_pair(C_HUD_KEY, th.accent,  235);
    init_pair(C_HUD_VAL, 253,        235);
    init_pair(C_HUD_SEP, 239,        235);
}

static inline int gradPair(float level) {
    int i = std::clamp((int)(level*(C_GRAD_N-1)), 0, C_GRAD_N-1);
    return gradTable[i];
}

// dim version for blur — steps back 8 positions in gradient table
static inline int blurPair(float level) {
    int i = std::clamp((int)(level*(C_GRAD_N-1)) - 8, 0, C_GRAD_N-1);
    return gradTable[i];
}

// ─── Config ───────────────────────────────────────────────────────────────────
static std::string g_configPath;

static void initConfigPath() {
    const char* h = getenv("HOME");
    std::string dir = std::string(h ? h : "/tmp") + "/.config";
    system(("mkdir -p '" + dir + "' 2>/dev/null").c_str());
    g_configPath = dir + "/visualizer.conf";
}

struct Settings {
    int   style     = FILLED;
    int   theme     = 0;
    float sens      = SENS_DEFAULT;
    int   stereo    = 0;
    int   showPeaks = 1;
    int   blur      = 0;
    float peakFall  = PEAK_FALL_DEF;
};

static void saveSettings(const Settings& s) {
    std::ofstream f(g_configPath); if (!f) return;
    f << "style="     << s.style     << "\n"
      << "theme="     << s.theme     << "\n"
      << "sens="      << s.sens      << "\n"
      << "stereo="    << s.stereo    << "\n"
      << "showPeaks=" << s.showPeaks << "\n"
      << "blur="      << s.blur      << "\n"
      << "peakFall="  << s.peakFall  << "\n";
}

static Settings loadSettings() {
    Settings s;
    std::ifstream f(g_configPath); if (!f) return s;
    std::string line;
    while (std::getline(f, line)) {
        auto p=line.find('='); if(p==std::string::npos) continue;
        std::string k=line.substr(0,p), v=line.substr(p+1);
        try {
            if(k=="style")     s.style     = std::clamp(std::stoi(v),0,N_STYLES-1);
            if(k=="theme")     s.theme     = std::clamp(std::stoi(v),0,N_THEMES-1);
            if(k=="sens")      s.sens      = std::clamp(std::stof(v),SENS_MIN,SENS_MAX);
            if(k=="stereo")    s.stereo    = std::clamp(std::stoi(v),0,1);
            if(k=="showPeaks") s.showPeaks = std::clamp(std::stoi(v),0,1);
            if(k=="blur")      s.blur      = std::clamp(std::stoi(v),0,1);
            if(k=="peakFall")  s.peakFall  = std::clamp(std::stof(v),PEAK_FALL_MIN,PEAK_FALL_MAX);
        } catch(...) {}
    }
    return s;
}

// ─── PulseAudio monitor detection ────────────────────────────────────────────
// Returns RUNNING monitor first, falls back to any monitor.
// Does NOT require audio to be playing — visualizer works at any time.
static std::string getActiveMonitor() {
    FILE* fp=popen("pactl list sources short","r");
    if(!fp) return "";
    char line[512]; std::string running, fallback;
    while (fgets(line,sizeof(line),fp)) {
        std::string s(line);
        if(s.find(".monitor")==std::string::npos) continue;
        char name[256]=""; sscanf(line,"%*d %255s",name);
        if(!name[0]) continue;
        if(fallback.empty()) fallback=name;
        if(s.find("RUNNING")!=std::string::npos && running.empty()) running=name;
    }
    pclose(fp);
    return running.empty() ? fallback : running;
}

// ─── HUD ─────────────────────────────────────────────────────────────────────
static void drawHUD(const Settings& cfg, int cols) {
    // Fill background
    attron(COLOR_PAIR(C_HUD_BG));
    for(int x=0;x<cols;x++) mvaddch(0,x,' ');
    attroff(COLOR_PAIR(C_HUD_BG));

    char sensBuf[8], fallBuf[8];
    snprintf(sensBuf,sizeof(sensBuf),"%.1f",cfg.sens);
    snprintf(fallBuf,sizeof(fallBuf),"%.0f%%",
        (cfg.peakFall-PEAK_FALL_MIN)/(PEAK_FALL_MAX-PEAK_FALL_MIN)*100.f);

    struct Tok { const char* key; const char* val; };
    Tok toks[] = {
        { "1-5",  STYLE_NAMES[cfg.style]                  },
        { "m",    cfg.stereo    ? "Stereo"   : "Mono"     },
        { "t/T",  THEMES[cfg.theme%N_THEMES].name         },
        { "+/-",  sensBuf                                  },
        { "s",    cfg.showPeaks ? "Peaks:on" : "Peaks:off" },
        { "b",    cfg.blur      ? "Blur:on"  : "Blur:off"  },
        { "[/]",  fallBuf                                  },
        { "r",    "Reset"                                  },
        { "q",    "Quit"                                   },
    };
    const int N = (int)(sizeof(toks)/sizeof(toks[0]));

    int x = 1;
    for (int ti=0; ti<N; ti++) {
        int klen=(int)strlen(toks[ti].key);
        int vlen=(int)strlen(toks[ti].val);
        int need = klen+1+vlen + (ti>0 ? 3 : 0); // sep=" │ "
        if (x+need >= cols) break;

        if (ti>0) {
            attron(COLOR_PAIR(C_HUD_SEP));
            mvaddstr(0,x," \xe2\x94\x82 "); x+=3;  // " │ "
            attroff(COLOR_PAIR(C_HUD_SEP));
        }
        attron(COLOR_PAIR(C_HUD_KEY)|A_BOLD);
        mvaddstr(0,x,toks[ti].key); x+=klen;
        attroff(COLOR_PAIR(C_HUD_KEY)|A_BOLD);

        attron(COLOR_PAIR(C_HUD_SEP));
        mvaddch(0,x,':'); x++;
        attroff(COLOR_PAIR(C_HUD_SEP));

        attron(COLOR_PAIR(C_HUD_VAL));
        mvaddstr(0,x,toks[ti].val); x+=vlen;
        attroff(COLOR_PAIR(C_HUD_VAL));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {

    initConfigPath();
    setlocale(LC_ALL, "");   // must be before initscr for wide chars to work

    initscr(); start_color(); use_default_colors();
    noecho(); curs_set(0); timeout(0); keypad(stdscr,TRUE);

    Settings cfg = loadSettings();
    applyTheme(cfg.theme);

    // ── PulseAudio ────────────────────────────────────────────────────────────
    std::string src = getActiveMonitor();
    if (src.empty()) { endwin(); fputs("No monitor source.\n",stderr); return 1; }

    pa_sample_spec ss{};
    ss.format=PA_SAMPLE_S32LE; ss.channels=2; ss.rate=SAMPLE_RATE;
    pa_buffer_attr ba{};
    ba.maxlength = (uint32_t)-1;
    ba.tlength   = (uint32_t)-1;
    ba.prebuf    = (uint32_t)-1;
    ba.minreq    = (uint32_t)-1;
    // Small fragsize: tells PA to deliver data in tiny chunks.
    // This prevents the monitor source from being auto-suspended when idle,
    // so the visualizer reacts immediately when audio starts playing.
    ba.fragsize  = 32;
    int error=0;
    pa_simple* pa=pa_simple_new(NULL,"Visualizer",PA_STREAM_RECORD,
        src.c_str(),"capture",&ss,NULL,&ba,&error);
    if(!pa){endwin();fprintf(stderr,"PA: %s\n",pa_strerror(error));return 1;}

    // ── FFTW ─────────────────────────────────────────────────────────────────
    static double       inL[BUFFER_SIZE], inR[BUFFER_SIZE];
    static fftw_complex outL[BUFFER_SIZE/2+1], outR[BUFFER_SIZE/2+1];
    memset(inL,0,sizeof(inL)); memset(inR,0,sizeof(inR));
    fftw_plan planL=fftw_plan_dft_r2c_1d(BUFFER_SIZE,inL,outL,FFTW_ESTIMATE);
    fftw_plan planR=fftw_plan_dft_r2c_1d(BUFFER_SIZE,inR,outR,FFTW_ESTIMATE);

    // Hann window with amplitude normalisation baked in
    static double win[BUFFER_SIZE];
    {
        double ws=0;
        for(int i=0;i<BUFFER_SIZE;i++){win[i]=0.5*(1-cos(2*M_PI*i/(BUFFER_SIZE-1)));ws+=win[i];}
        double norm=2.0/ws;
        for(int i=0;i<BUFFER_SIZE;i++) win[i]*=norm;
    }
    const double LOG81=log(81.0);

    // ── Ring + hop buffers ────────────────────────────────────────────────────
    std::vector<double>  ringL(BUFFER_SIZE,0.0), ringR(BUFFER_SIZE,0.0);
    std::vector<int32_t> hop(HOP*2);

    // ── Per-bar state ─────────────────────────────────────────────────────────
    int rows=0, cols=0, numBars=0;
    int g_cellW=3, g_barW=2, g_barGap=0, g_rem=0;
    struct Band{int lo,hi;};
    std::vector<Band>  bands;
    std::vector<int>   xpos;   // precomputed left-edge column of each bar
    std::vector<int>   bwidths; // precomputed width of each bar
    std::vector<float> smooth, mag, peakVal, ballPos, ballVel;
    std::vector<float> blurBuf;   // temporal ghost buffer for blur mode
    std::vector<int>   peakTimer;

    auto rebuild=[&](){
        getmaxyx(stdscr, rows, cols);
        if(rows < 4 || cols < 6) return;

        // ── Centered layout ───────────────────────────────────────────────────
        // Every bar  = exactly 2 cols
        // Every gap  = exactly 1 col
        // Both edges = equal margin (0 or 1 col) — centered, symmetric
        //
        // n    = (cols+1)/3     → max bars fitting with gap=1
        // used = 3n-1           → total bar+gap area
        // leftover = cols-used  → 0,1,2 cols remaining
        // margin = leftover/2   → split equally left and right
        // xpos[i] = margin + i*3  → constant stride, centered
        {
            int n        = std::max(2, (cols + 1) / 3);
            int used     = 3 * n - 1;
            int leftover = cols - used;
            if(leftover < 0){ n--; used = 3*n-1; leftover = cols-used; }
            int margin   = leftover / 2;  // left AND right (right may get +1 if odd)

            numBars  = n;
            g_barW   = 2;
            g_barGap = 1;
            g_rem    = 0;
            g_cellW  = 3;

            smooth.assign(numBars,0);   mag.assign(numBars,0);
            peakVal.assign(numBars,0);  peakTimer.assign(numBars,0);
            ballPos.assign(numBars,0);  ballVel.assign(numBars,0);
            blurBuf.assign(numBars,0);
            xpos.resize(numBars);
            bwidths.resize(numBars);

            for(int i = 0; i < numBars; i++){
                xpos[i]    = margin + i * 3;
                bwidths[i] = 2;
            }
        }

        bands.resize(numBars);
        const int    maxBin=BUFFER_SIZE/2;
        const double nyq=SAMPLE_RATE/2.0;
        const double lLo=log10(FREQ_LO), lHi=log10(std::min(FREQ_HI,nyq));
        for(int i=0;i<numBars;i++){
            double fLo=pow(10.0,lLo+(double)i    /numBars*(lHi-lLo));
            double fHi=pow(10.0,lLo+(double)(i+1)/numBars*(lHi-lLo));
            int bLo=std::clamp((int)round(fLo/nyq*maxBin),1,maxBin);
            int bHi=std::clamp((int)round(fHi/nyq*maxBin),1,maxBin);
            if(bHi<bLo)bHi=bLo;
            bands[i]={bLo,bHi};
        }
        clear();
    };
    rebuild();

    bool hudVisible=true;
    auto lastKey=Clock::now();

    // ─────────────────────────────────────────────────────────────────────────
    while(true) {

        // Detect terminal resize by comparing getmaxyx each frame
        { int r,c; getmaxyx(stdscr,r,c); if(r!=rows||c!=cols) rebuild(); }

        // ── Read audio ────────────────────────────────────────────────────────
        if(pa_simple_read(pa,hop.data(),(int)hop.size()*sizeof(int32_t),&error)<0) break;

        const double scale=1.0/2147483648.0;
        memmove(ringL.data(),ringL.data()+HOP,(BUFFER_SIZE-HOP)*sizeof(double));
        memmove(ringR.data(),ringR.data()+HOP,(BUFFER_SIZE-HOP)*sizeof(double));
        for(int i=0;i<HOP;i++){
            ringL[BUFFER_SIZE-HOP+i]=hop[i*2  ]*scale;
            ringR[BUFFER_SIZE-HOP+i]=hop[i*2+1]*scale;
        }

        // ── Window + FFT ──────────────────────────────────────────────────────
        for(int i=0;i<BUFFER_SIZE;i++){inL[i]=ringL[i]*win[i];inR[i]=ringR[i]*win[i];}
        fftw_execute(planL); fftw_execute(planR);

        // ── FFT → magnitudes ──────────────────────────────────────────────────
        const bool isStereo=cfg.stereo;
        for(int i=0;i<numBars;i++){
            int lo=bands[i].lo, hi=bands[i].hi;
            double pk=0.0;
            if(!isStereo){
                for(int b=lo;b<=hi;b++){
                    double mL=sqrt(outL[b][0]*outL[b][0]+outL[b][1]*outL[b][1]);
                    double mR=sqrt(outR[b][0]*outR[b][0]+outR[b][1]*outR[b][1]);
                    double m=(mL+mR)*0.5; if(m>pk)pk=m;
                }
            } else {
                const fftw_complex* out=(i<numBars/2)?outL:outR;
                for(int b=lo;b<=hi;b++){
                    double m=sqrt(out[b][0]*out[b][0]+out[b][1]*out[b][1]);
                    if(m>pk)pk=m;
                }
            }
            float val=(float)(log(pk*80.0*cfg.sens+1.0)/LOG81);
            mag[i]=std::clamp(val,0.f,1.f);
        }

        // ── EMA + peak + blur + bounce ────────────────────────────────────────
        const int halfH=std::max(1,rows/2);
        const int fullH=std::max(1,rows-2);

        for(int i=0;i<numBars;i++){
            float m=mag[i];
            float& s=smooth[i];
            s=(m>s)?s*ATTACK+m*(1.f-ATTACK):s*DECAY+m*(1.f-DECAY);

            // afterglow: snaps up with bar, decays slowly leaving a ghost trail
            float& bl=blurBuf[i];
            if(s>bl) bl=s;          // snap up instantly with bar
            else      bl*=GLOW_DECAY; // decay slowly toward zero (not toward s)

            // peak
            if(s>peakVal[i]){peakVal[i]=s;peakTimer[i]=PEAK_HOLD_DEF;}
            else if(peakTimer[i]>0) peakTimer[i]--;
            else peakVal[i]=std::max(0.f,peakVal[i]-cfg.peakFall);

            // bounce ball
            if(s>ballPos[i]+0.02f){ballVel[i]=s*0.18f;ballPos[i]=s;}
            ballPos[i]+=ballVel[i]; ballVel[i]-=GRAV;
            if(ballPos[i]<0.f){ballPos[i]=0.f;ballVel[i]=0.f;}
            if(ballPos[i]>1.f) ballPos[i]=1.f;
        }

        // ── DRAW ──────────────────────────────────────────────────────────────
        const Style style=(Style)cfg.style;
        const int   center=rows/2;
        clear();

        // ── helper lambdas — take bar width bw as parameter ──────────────────
        auto drawBarMirrored=[&](int x, int bw, int h, int blurH, bool showBlur){
            // Draw afterglow first (full height of glow, dim colour)
            // then draw solid bar on top — bar overwrites lower glow cells
            if(showBlur && blurH>0){
                // glow uses the theme's lo colour — dim, ghostly
                int glowCp = gradTable[0]; // dimmest gradient step
                attron(COLOR_PAIR(glowCp));
                for(int y=0; y<blurH; y++)
                    for(int w=0; w<bw; w++){
                        mvaddch(center-y, x+w, ACS_BLOCK);
                        mvaddch(center+y, x+w, ACS_BLOCK);
                    }
                attroff(COLOR_PAIR(glowCp));
            }
            // solid bar drawn on top (overwrites glow in bar region)
            int prevCp=-1;
            for(int y=0;y<h;y++){
                float lv=(halfH>1)?(float)y/(halfH-1):1.f;
                int   cp=gradPair(lv);
                if(cp!=prevCp){
                    if(prevCp>=0) attroff(COLOR_PAIR(prevCp));
                    attron(COLOR_PAIR(cp)); prevCp=cp;
                }
                for(int w=0;w<bw;w++){
                    mvaddch(center-y,x+w,ACS_BLOCK);
                    mvaddch(center+y,x+w,ACS_BLOCK);
                }
            }
            if(prevCp>=0) attroff(COLOR_PAIR(prevCp));
        };

        auto drawBarUp=[&](int x, int bw, int h, int blurH, bool showBlur){
            int base=rows-2;
            if(showBlur && blurH>0){
                int glowCp=gradTable[0];
                attron(COLOR_PAIR(glowCp));
                for(int y=0;y<blurH;y++)
                    for(int w=0;w<bw;w++) mvaddch(base-y,x+w,ACS_BLOCK);
                attroff(COLOR_PAIR(glowCp));
            }
            int prevCp=-1;
            for(int y=0;y<h;y++){
                float lv=(fullH>1)?(float)y/(fullH-1):1.f;
                int   cp=gradPair(lv);
                if(cp!=prevCp){
                    if(prevCp>=0) attroff(COLOR_PAIR(prevCp));
                    attron(COLOR_PAIR(cp)); prevCp=cp;
                }
                for(int w=0;w<bw;w++) mvaddch(base-y,x+w,ACS_BLOCK);
            }
            if(prevCp>=0) attroff(COLOR_PAIR(prevCp));
        };

        // ── Style: TRADITIONAL ────────────────────────────────────────────────
        if(style==TRADITIONAL || style==CLASSIC_FILLED){
            // shared shade data for CLASSIC_FILLED
            static const wchar_t SHADES[4]={L'\u2591',L'\u2592',L'\u2593',L'\u2588'};
            auto drawShadeUp=[&](int row,int col,wchar_t wc,int cpair){
                if(row<0||row>=rows||col<0||col>=cols) return;
                cchar_t cc; wchar_t ws[2]={wc,L'\0'};
                setcchar(&cc,ws,A_NORMAL,(short)cpair,nullptr);
                mvadd_wch(row,col,&cc);
            };

            for(int i=0;i<numBars;i++){
                int x=xpos[i], bw=bwidths[i];
                int h =std::clamp((int)(smooth[i]*fullH),0,fullH-1);
                int bh=std::clamp((int)(blurBuf[i]*fullH),0,fullH-1);
                int p =std::clamp((int)(peakVal[i]*fullH),0,fullH-1);
                int base=rows-2;

                if(style==TRADITIONAL){
                    // solid blocks, glow underneath
                    drawBarUp(x,bw,h,bh,cfg.blur);
                } else {
                    // CLASSIC_FILLED: ░▒▓█ texture growing from bottom
                    // glow first
                    if(cfg.blur && bh>0){
                        attron(COLOR_PAIR(gradTable[0]));
                        for(int y=0;y<bh;y++)
                            for(int w=0;w<bw;w++) mvaddch(base-y,x+w,ACS_BLOCK);
                        attroff(COLOR_PAIR(gradTable[0]));
                    }
                    // shaded bar: bottom=░ → top=█
                    for(int y=0;y<h;y++){
                        float lv=(fullH>1)?(float)y/(fullH-1):1.f;
                        int   cp=gradPair(lv);
                        // distance from TOP of bar (y=h-1 is top)
                        int distFromTop=h-1-y;
                        wchar_t wc; int dim;
                        if(distFromTop==0)        { wc=SHADES[3]; dim=0; } // █ top
                        else if(distFromTop<=h/4) { wc=SHADES[2]; dim=2; } // ▓
                        else if(distFromTop<=h/2) { wc=SHADES[1]; dim=5; } // ▒
                        else                      { wc=SHADES[0]; dim=8; } // ░ bottom
                        int cp2=std::max(C_GRAD_BASE,cp-dim);
                        for(int w=0;w<bw;w++)
                            drawShadeUp(base-y,x+w,wc,cp2);
                    }
                }

                if(cfg.showPeaks&&p>h&&p>0){
                    attron(COLOR_PAIR(C_PEAK)|A_BOLD);
                    for(int w=0;w<bw;w++) mvaddch(base-p,x+w,'-');
                    attroff(COLOR_PAIR(C_PEAK)|A_BOLD);
                }
            }

        // ── Style: BOUNCE ─────────────────────────────────────────────────────
        } else if(style==BOUNCE){
            for(int i=0;i<numBars;i++){
                int x=xpos[i], bw=bwidths[i];
                int h   =std::clamp((int)(smooth[i]*halfH),0,halfH-1);
                int bh  =std::clamp((int)(blurBuf[i]*halfH),0,halfH-1);
                int ball=std::clamp((int)(ballPos[i]*halfH),0,halfH-1);
                drawBarMirrored(x,bw,h,bh,cfg.blur);
                if(ball>h){
                    float lv=(halfH>1)?(float)ball/(halfH-1):1.f;
                    attron(COLOR_PAIR(gradPair(lv))|A_BOLD);
                    for(int w=0;w<bw;w++){
                        mvaddch(center-ball,x+w,ACS_DIAMOND);
                        mvaddch(center+ball,x+w,ACS_DIAMOND);
                    }
                    attroff(COLOR_PAIR(gradPair(lv))|A_BOLD);
                }
            }

        // ── Style: MIRROR ─────────────────────────────────────────────────────
        } else if(style==MIRROR){
            for(int i=0;i<numBars;i++){
                int x=xpos[i], bw=bwidths[i];
                int h =std::clamp((int)(smooth[i]*halfH),0,halfH-1);
                int bh=std::clamp((int)(blurBuf[i]*halfH),0,halfH-1);
                int p =std::clamp((int)(peakVal[i]*halfH),0,halfH-1);
                drawBarMirrored(x,bw,h,bh,cfg.blur);
                if(cfg.showPeaks&&p>h&&p>0){
                    attron(COLOR_PAIR(C_PEAK)|A_BOLD);
                    for(int w=0;w<bw;w++){
                        mvaddch(center-p,x+w,'-');
                        mvaddch(center+p,x+w,'-');
                    }
                    attroff(COLOR_PAIR(C_PEAK)|A_BOLD);
                }
            }

        // ── Style: FILLED ─────────────────────────────────────────────────────
        } else {
            static const wchar_t SHADES[4] = {
                L'\u2591', L'\u2592', L'\u2593', L'\u2588'
            };
            auto drawShade=[&](int row, int col, wchar_t wc, int cpair){
                if(row<0||row>=rows||col<0||col>=cols) return;
                cchar_t cc; wchar_t ws[2]={wc,L'\0'};
                setcchar(&cc,ws,A_NORMAL,(short)cpair,nullptr);
                mvadd_wch(row,col,&cc);
            };
            for(int i=0;i<numBars;i++){
                int x=xpos[i], bw=bwidths[i];
                int h =std::clamp((int)(smooth[i]*halfH),0,halfH-1);
                int bh=std::clamp((int)(blurBuf[i]*halfH),0,halfH-1);
                int p =std::clamp((int)(peakVal[i]*halfH),0,halfH-1);
                // glow: draw full glow height as ░ in dim colour first
                if(cfg.blur && bh>0){
                    for(int y=0;y<bh;y++)
                        for(int w=0;w<bw;w++){
                            drawShade(center-y,x+w,SHADES[0],gradTable[0]);
                            drawShade(center+y,x+w,SHADES[0],gradTable[0]);
                        }
                }
                // filled bar drawn on top (overwrites glow below bar height)
                for(int y=0;y<h;y++){
                    float lv=(halfH>1)?(float)y/(halfH-1):1.f;
                    int   cp=gradPair(lv);
                    int   dist=h-1-y;
                    wchar_t wc; int dim;
                    if(dist==0)           { wc=SHADES[3]; dim=0; }
                    else if(dist<=h/4)    { wc=SHADES[2]; dim=2; }
                    else if(dist<=h/2)    { wc=SHADES[1]; dim=5; }
                    else                  { wc=SHADES[0]; dim=8; }
                    int cp2=std::max(C_GRAD_BASE,cp-dim);
                    for(int w=0;w<bw;w++){
                        drawShade(center-y,x+w,wc,cp2);
                        drawShade(center+y,x+w,wc,cp2);
                    }
                }
                if(cfg.showPeaks&&p>h&&p>0){
                    attron(COLOR_PAIR(C_PEAK)|A_BOLD);
                    for(int w=0;w<bw;w++){
                        mvaddch(center-p,x+w,'-');
                        mvaddch(center+p,x+w,'-');
                    }
                    attroff(COLOR_PAIR(C_PEAK)|A_BOLD);
                }
            }
        }

        // ── HUD ───────────────────────────────────────────────────────────────
        auto now=Clock::now();
        bool hudNow=hudVisible &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now-lastKey).count()<HUD_MS;
        if(hudNow) drawHUD(cfg,cols);

        refresh();

        // ── Input ─────────────────────────────────────────────────────────────
        int ch=getch();
        if(ch!=ERR){
            hudVisible=true; lastKey=Clock::now();
            bool changed=true;
            switch(ch){
                case 'q': saveSettings(cfg); goto done;
                case 'm': cfg.stereo    =!cfg.stereo; break;
                case 's': cfg.showPeaks =!cfg.showPeaks; break;
                case 'b': cfg.blur      =!cfg.blur; break;
                case 't': cfg.theme=(cfg.theme+1)%N_THEMES;
                          applyTheme(cfg.theme); break;
                case 'T': cfg.theme=(cfg.theme+N_THEMES-1)%N_THEMES;
                          applyTheme(cfg.theme); break;
                case '+': case '=':
                          cfg.sens=std::min(cfg.sens+SENS_STEP,SENS_MAX); break;
                case '-': cfg.sens=std::max(cfg.sens-SENS_STEP,SENS_MIN); break;
                case '[': cfg.peakFall=std::max(cfg.peakFall-PEAK_FALL_STEP,PEAK_FALL_MIN); break;
                case ']': cfg.peakFall=std::min(cfg.peakFall+PEAK_FALL_STEP,PEAK_FALL_MAX); break;
                case 'r': cfg=Settings{}; applyTheme(cfg.theme); break;
                case KEY_RESIZE: rebuild(); changed=false; break;
                default:
                    if(ch>='1'&&ch<='5') cfg.style=ch-'1';
                    else changed=false;
                    break;
            }
            if(changed) saveSettings(cfg);
        }
        napms(1);
    }
    done:
    saveSettings(cfg);
    fftw_destroy_plan(planL); fftw_destroy_plan(planR);
    pa_simple_free(pa); endwin();
    return 0;
}
