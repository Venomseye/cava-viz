#include "bar_output.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef __linux__
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#endif

// ── Helpers ───────────────────────────────────────────────────────────────────

static int tests_run    = 0;
static int tests_failed = 0;

static void check(const char* label,
                  const std::string& got,
                  const std::string& expected) {
    ++tests_run;
    if (got == expected) {
        printf("  \033[32mPASS\033[0m  %s\n", label);
    } else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  %s\n"
               "        expected: %s\n"
               "        got:      %s\n",
               label, expected.c_str(), got.c_str());
    }
}

// ── Synthetic bar data ────────────────────────────────────────────────────────
//
// 10 bars covering the full 0→1 range with a recognisable pattern:
//
//   idx:   0     1     2     3     4     5     6     7     8     9
//   val: 0.00  0.11  0.25  0.50  0.75  1.00  0.62  0.37  0.12  0.00
//
// Mapped onto 8 block levels (▁▂▃▄▅▆▇█) with round():
//   round(v * 7) → index into "▁▂▃▄▅▆▇█"
//
//   0.00 → 0 → ▁
//   0.11 → 1 → ▂
//   0.25 → 2 → ▃
//   0.50 → 4 → ▅   (round(3.5)=4)
//   0.75 → 5 → ▆   (round(5.25)=5)
//   1.00 → 7 → █
//   0.62 → 4 → ▅   (round(4.34)=4)
//   0.37 → 3 → ▄   (round(2.59)=3)
//   0.12 → 1 → ▂
//   0.00 → 0 → ▁
//
// Expected bar string: ▁▂▃▅▆█▅▄▂▁

static const std::vector<float> kBarsL = {
    0.00f, 0.11f, 0.25f, 0.50f, 0.75f,
    1.00f, 0.62f, 0.37f, 0.12f, 0.00f
};

// Right channel — mirror of left, for stereo split testing
static const std::vector<float> kBarsR = {
    0.00f, 0.12f, 0.37f, 0.62f, 1.00f,
    0.75f, 0.50f, 0.25f, 0.11f, 0.00f
};

// Merge: average of L and R per bar
// (0+0)/2=0 (0.11+0.12)/2≈0.115 (0.25+0.37)/2=0.31 (0.5+0.62)/2=0.56
// (0.75+1.0)/2=0.875 (1+0.75)/2=0.875 (0.62+0.5)/2=0.56 (0.37+0.25)/2=0.31
// (0.12+0.11)/2=0.115 (0+0)/2=0
// → rounds (×7): 0,1,2,4,6,6,4,2,1,0 → ▁▂▃▅▇▇▅▃▂▁

static BarOutputConfig makeCfg(BarFormat fmt,
                                BarStereo stereo = BarStereo::Merge,
                                const std::string& sep = " | ") {
    BarOutputConfig c;
    c.format     = fmt;
    c.count      = 10;
    c.chars      = "▁▂▃▄▅▆▇█";
    c.color      = "#cba6f7";
    c.stereo     = stereo;
    c.stereo_sep = sep;
    return c;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void testPlain() {
    printf("\n[plain]\n");
    const std::string got = formatBars(kBarsL, {}, makeCfg(BarFormat::Plain));
    check("mono",
          got,
          "▁▂▃▅▆█▅▄▂▁");
}

static void testPlainStereoMerge() {
    printf("\n[plain stereo merge]\n");
    const std::string got = formatBars(kBarsL, kBarsR,
                                       makeCfg(BarFormat::Plain, BarStereo::Merge));
    // merged: ▁▂▃▅▇▇▅▃▂▁
    check("merge L+R averaged",
          got,
          "▁▂▃▅▇▇▅▃▂▁");
}

static void testPlainStereoSplit() {
    printf("\n[plain stereo split]\n");
    const std::string got = formatBars(kBarsL, kBarsR,
                                       makeCfg(BarFormat::Plain, BarStereo::Split, " | "));
    // R mirror: ▁▂▄▅█▆▅▃▂▁
    check("split L | R",
          got,
          "▁▂▃▅▆█▅▄▂▁ | ▁▂▄▅█▆▅▃▂▁");
}

static void testWaybar() {
    printf("\n[waybar]\n");
    const std::string got = formatBars(kBarsL, {}, makeCfg(BarFormat::Waybar));
    check("json + pango span",
          got,
          "{\"text\":\"<span color='#cba6f7'>▁▂▃▅▆█▅▄▂▁</span>\",\"class\":\"viz-active\"}");
}

static void testPolybar() {
    printf("\n[polybar]\n");
    const std::string got = formatBars(kBarsL, {}, makeCfg(BarFormat::Polybar));
    check("polybar color tags",
          got,
          "%{F#cba6f7}▁▂▃▅▆█▅▄▂▁%{F-}");
}

static void testEww() {
    printf("\n[eww]\n");
    const std::string got = formatBars(kBarsL, {}, makeCfg(BarFormat::Eww));
    // Eww is plain (same as Plain)
    check("same as plain",
          got,
          "▁▂▃▅▆█▅▄▂▁");
}

static void testDzen2() {
    printf("\n[dzen2]\n");
    const std::string got = formatBars(kBarsL, {}, makeCfg(BarFormat::Dzen2));
    check("dzen2 fg tags",
          got,
          "^fg(#cba6f7)▁▂▃▅▆█▅▄▂▁^fg()");
}

static void testI3bar() {
    printf("\n[i3bar]\n");
    const std::string got = formatBars(kBarsL, {}, makeCfg(BarFormat::I3bar));
    check("i3bar json block",
          got,
          "{\"full_text\":\"▁▂▃▅▆█▅▄▂▁\",\"color\":\"#cba6f7\"}");
}

static void testRaw() {
    printf("\n[raw]\n");
    const std::string got = formatBars(kBarsL, {}, makeCfg(BarFormat::Raw));
    check("space-separated floats",
          got,
          "0.000 0.110 0.250 0.500 0.750 1.000 0.620 0.370 0.120 0.000");
}

static void testRawStereoSplit() {
    printf("\n[raw stereo split]\n");
    const std::string got = formatBars(kBarsL, kBarsR,
                                       makeCfg(BarFormat::Raw, BarStereo::Split, " | "));
    check("raw split",
          got,
          "0.000 0.110 0.250 0.500 0.750 1.000 0.620 0.370 0.120 0.000"
          " | "
          "0.000 0.120 0.370 0.620 1.000 0.750 0.500 0.250 0.110 0.000");
}

static void testEdgeCases() {
    printf("\n[edge cases]\n");

    // All-silent bars → all ▁
    {
        const std::vector<float> silent(10, 0.0f);
        const std::string got = formatBars(silent, {}, makeCfg(BarFormat::Plain));
        check("all zero → all ▁",
              got,
              "▁▁▁▁▁▁▁▁▁▁");
    }

    // All-max bars → all █
    {
        const std::vector<float> full(10, 1.0f);
        const std::string got = formatBars(full, {}, makeCfg(BarFormat::Plain));
        check("all 1.0 → all █",
              got,
              "██████████");
    }

    // Empty input → empty output
    {
        const std::string got = formatBars({}, {}, makeCfg(BarFormat::Plain));
        check("empty bars → empty string",
              got,
              "");
    }

    // Custom chars (3 levels: _, =, ■)
    {
        BarOutputConfig c = makeCfg(BarFormat::Plain);
        c.chars = "_=■";
        const std::vector<float> bars = {0.0f, 0.5f, 1.0f};
        const std::string got = formatBars(bars, {}, c);
        check("custom 3-level chars",
              got,
              "_=■");
    }

    // ASCII chars (not UTF-8 multi-byte)
    {
        BarOutputConfig c = makeCfg(BarFormat::Plain);
        c.chars = "-+#";
        const std::vector<float> bars = {0.0f, 0.5f, 1.0f};
        const std::string got = formatBars(bars, {}, c);
        check("ASCII chars",
              got,
              "-+#");
    }

    // Out-of-range clamping (>1.0 clamped to max char)
    {
        BarOutputConfig c = makeCfg(BarFormat::Plain);
        c.chars = "▁▂▃▄▅▆▇█";
        const std::vector<float> bars = {-0.5f, 1.5f};
        const std::string got = formatBars(bars, {}, c);
        check("clamp <0 → ▁, >1 → █",
              got,
              "▁█");
    }
}

static void testBarWriterStdout() {
    printf("\n[BarWriter stdout]\n");
    ++tests_run;

    BarOutputConfig cfg;
    cfg.sink   = BarSink::Stdout;
    cfg.format = BarFormat::Plain;

    BarWriter w;
    if (!w.open(cfg)) {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  open() returned false\n");
        return;
    }

#ifdef __linux__
    // Flush any buffered printf output before redirecting so it doesn't
    // contaminate what we read back from the pipe.
    fflush(stdout);
    int pipe_fds[2];
    pipe(pipe_fds);
    const int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
#endif

    const bool ok = w.write("▁▂▃▅▆█▅▄▂▁");

#ifdef __linux__
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    char buf[128] = {};
    read(pipe_fds[0], buf, sizeof(buf) - 1);
    close(pipe_fds[0]);

    const std::string captured = buf;
    const bool pass = ok && (captured == "▁▂▃▅▆█▅▄▂▁\n");
    if (pass)
        printf("  \033[32mPASS\033[0m  stdout write + newline appended\n");
    else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  ok=%d captured='%s'\n", (int)ok, captured.c_str());
    }
#else
    if (ok) printf("  \033[32mPASS\033[0m  write returned true\n");
    else { ++tests_failed; printf("  \033[31mFAIL\033[0m  write returned false\n"); }
#endif
    w.close();
}

#ifdef __linux__
static void testBarWriterFifo() {
    printf("\n[BarWriter FIFO]\n");

    const std::string path = "/tmp/cava_viz_test_" + std::to_string(getpid()) + ".pipe";

    // Open the read end in a background thread before calling writer.open()
    // (which blocks until a reader connects)
    int reader_fd = -1;
    std::thread reader_thread([&]() {
        mkfifo(path.c_str(), 0644);
        reader_fd = open(path.c_str(), O_RDONLY);
    });

    BarOutputConfig cfg;
    cfg.sink      = BarSink::Fifo;
    cfg.sink_path = path;

    BarWriter w;
    const bool opened = w.open(cfg);
    reader_thread.join();

    ++tests_run;
    if (!opened || reader_fd < 0) {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  FIFO open failed (opened=%d reader_fd=%d)\n",
               (int)opened, reader_fd);
        if (reader_fd >= 0) close(reader_fd);
        unlink(path.c_str());
        return;
    }
    printf("  \033[32mPASS\033[0m  FIFO created and both ends opened\n");

    // Write a frame and read it back
    ++tests_run;
    w.write("▁▂▃▅▆█");
    w.close();

    char buf[128] = {};
    const ssize_t n = read(reader_fd, buf, sizeof(buf) - 1);
    close(reader_fd);
    unlink(path.c_str());

    const std::string got = (n > 0) ? std::string(buf, (size_t)n) : "";
    if (got == "▁▂▃▅▆█\n") {
        printf("  \033[32mPASS\033[0m  FIFO write+read round-trip: '%s'\n",
               got.substr(0, got.size()-1).c_str());
    } else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  FIFO got: '%s'\n", got.c_str());
    }
}

static void testBarWriterSocket() {
    printf("\n[BarWriter Unix socket]\n");

    const std::string path = "/tmp/cava_viz_test_" + std::to_string(getpid()) + ".sock";

    BarOutputConfig cfg;
    cfg.sink      = BarSink::Socket;
    cfg.sink_path = path;

    BarWriter w;
    ++tests_run;
    if (!w.open(cfg)) {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  socket open failed\n");
        return;
    }
    printf("  \033[32mPASS\033[0m  Unix socket created at %s\n", path.c_str());

    // Connect a client
    int client = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
    connect(client, (struct sockaddr*)&addr, sizeof(addr));

    // Give the server a moment to accept (it accepts on write())
    usleep(10000);

    // Write a frame — this calls acceptClients() internally then broadcasts
    ++tests_run;
    w.write("▁▂▃▅▆█");
    w.close();

    // Read what the client received
    char buf[128] = {};
    const ssize_t n = read(client, buf, sizeof(buf)-1);
    close(client);

    const std::string got = (n > 0) ? std::string(buf, (size_t)n) : "";
    if (got == "▁▂▃▅▆█\n") {
        printf("  \033[32mPASS\033[0m  socket broadcast received: '%s'\n",
               got.substr(0, got.size()-1).c_str());
    } else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  socket got: '%s' (n=%zd)\n", got.c_str(), n);
    }
}
#endif

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("cava-viz bar_output test suite\n");
    printf("================================\n");

    testPlain();
    testPlainStereoMerge();
    testPlainStereoSplit();
    testWaybar();
    testPolybar();
    testEww();
    testDzen2();
    testI3bar();
    testRaw();
    testRawStereoSplit();
    testEdgeCases();
    testBarWriterStdout();
#ifdef __linux__
    testBarWriterFifo();
    testBarWriterSocket();
#endif

    printf("\n================================\n");
    printf("Results: %d/%d passed", tests_run - tests_failed, tests_run);
    if (tests_failed == 0)
        printf("  \033[32m— all good\033[0m\n");
    else
        printf("  \033[31m— %d FAILED\033[0m\n", tests_failed);

    return tests_failed ? 1 : 0;
}
