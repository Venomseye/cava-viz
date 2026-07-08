// tests/test_user_theme.cpp
// User-defined theme loader and parser tests.
// Compile: g++ -std=c++17 -Isrc -o test_user_theme tests/test_user_theme.cpp src/user_theme.cpp src/config.cpp
// Run:     ./test_user_theme

#include "user_theme.h"
#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

static int tests_run    = 0;
static int tests_failed = 0;

static void check_bool(const char* label, bool got, bool expected) {
    ++tests_run;
    if (got == expected) {
        printf("  \033[32mPASS\033[0m  %s\n", label);
    } else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  %s  expected=%s got=%s\n",
               label, expected ? "true" : "false", got ? "true" : "false");
    }
}

static void check_int(const char* label, int got, int expected) {
    ++tests_run;
    if (got == expected) {
        printf("  \033[32mPASS\033[0m  %s\n", label);
    } else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  %s  expected=%d got=%d\n", label, expected, got);
    }
}

static void check_str(const char* label, const std::string& got, const std::string& expected) {
    ++tests_run;
    if (got == expected) {
        printf("  \033[32mPASS\033[0m  %s\n", label);
    } else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  %s  expected='%s' got='%s'\n",
               label, expected.c_str(), got.c_str());
    }
}

static void check_float(const char* label, float got, float expected, float tol = 0.01f) {
    ++tests_run;
    const float diff = got - expected;
    const float abs_diff = diff < 0.f ? -diff : diff;
    if (abs_diff <= tol) {
        printf("  \033[32mPASS\033[0m  %s\n", label);
    } else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  %s  expected=%.4f got=%.4f\n",
               label, static_cast<double>(expected), static_cast<double>(got));
    }
}

// RAII temp directory pointing XDG vars at a clean location.
struct TempDir {
    fs::path path;
    TempDir() {
        char tmpl[] = "/tmp/cava_viz_theme_test_XXXXXX";
        const char* d = mkdtemp(tmpl);
        if (!d) { perror("mkdtemp"); std::exit(1); }
        path = d;
        setenv("XDG_CONFIG_HOME", (path / "config").c_str(), 1);
        setenv("XDG_STATE_HOME",  (path / "state").c_str(),  1);
    }
    ~TempDir() {
        unsetenv("XDG_CONFIG_HOME");
        unsetenv("XDG_STATE_HOME");
        fs::remove_all(path);
    }

    // Write content to a file under the themes directory.
    void writeTheme(const std::string& filename, const std::string& content) const {
        const fs::path dir = themesDir();
        fs::create_directories(dir);
        std::ofstream f(dir / filename);
        f << content;
    }
};

// ── Test groups ───────────────────────────────────────────────────────────────

static void testValidTheme() {
    printf("\n[valid theme: standard 4-stop gradient]\n");
    TempDir td;
    td.writeTheme("ocean.theme",
        "# Ocean gradient\n"
        "name   = Ocean\n"
        "\n"
        "stop_0 = 0.00  #003366\n"
        "stop_1 = 0.40  #0055aa\n"
        "stop_2 = 0.75  #00aaee\n"
        "stop_3 = 1.00  #00ffcc\n"
    );

    const auto themes = loadUserThemes();
    check_int ("exactly 1 theme loaded",          static_cast<int>(themes.size()), 1);
    if (themes.empty()) return;

    check_str ("name is 'Ocean'",                 themes[0].name,  "Ocean");
    check_int ("4 stops loaded",                  static_cast<int>(themes[0].stops.size()), 4);
    check_float("stop 0 pos = 0.0",               themes[0].stops[0].pos, 0.0f);
    check_float("stop 3 pos = 1.0",               themes[0].stops[3].pos, 1.0f);

    // #003366 → r=0, g=51, b=102 → ncurses: r=0, g=200, b=400 (÷255×1000)
    check_int ("stop_0 r ≈ 0",                    themes[0].stops[0].r, 0);
    // Allow ±2 for integer rounding
    ++tests_run;
    if (themes[0].stops[0].g >= 198 && themes[0].stops[0].g <= 202)
        printf("  \033[32mPASS\033[0m  stop_0 g ≈ 200\n");
    else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  stop_0 g: got %d, expected ~200\n", themes[0].stops[0].g);
    }
}

static void testDefaultName() {
    printf("\n[default name: uses filename when name= absent]\n");
    TempDir td;
    td.writeTheme("synthwave.theme",
        "stop_0 = 0.0  #1a0033\n"
        "stop_1 = 1.0  #ff00aa\n"
    );

    const auto themes = loadUserThemes();
    check_int("1 theme loaded",     static_cast<int>(themes.size()), 1);
    if (!themes.empty())
        check_str("name = filename", themes[0].name, "synthwave");
}

static void testStopSorting() {
    printf("\n[stop sorting: stops out of order in file are sorted]\n");
    TempDir td;
    td.writeTheme("unsorted.theme",
        "name   = Unsorted\n"
        "stop_2 = 1.00  #ffffff\n"
        "stop_0 = 0.00  #000000\n"
        "stop_1 = 0.50  #888888\n"
    );

    const auto themes = loadUserThemes();
    check_int("1 theme loaded", static_cast<int>(themes.size()), 1);
    if (themes.empty()) return;
    check_float("sorted stop[0].pos = 0.0", themes[0].stops[0].pos, 0.0f);
    check_float("sorted stop[1].pos = 0.5", themes[0].stops[1].pos, 0.5f);
    check_float("sorted stop[2].pos = 1.0", themes[0].stops[2].pos, 1.0f);
}

static void testMinStops() {
    printf("\n[stop count: minimum 2 stops accepted]\n");
    TempDir td;
    td.writeTheme("two.theme",
        "name   = Two\n"
        "stop_0 = 0.00  #000000\n"
        "stop_1 = 1.00  #ffffff\n"
    );
    const auto themes = loadUserThemes();
    check_int("2-stop theme accepted", static_cast<int>(themes.size()), 1);
}

static void testMaxStops() {
    printf("\n[stop count: maximum 8 stops accepted]\n");
    TempDir td;
    td.writeTheme("eight.theme",
        "name   = Eight\n"
        "stop_0 = 0.000  #000000\n"
        "stop_1 = 0.143  #111111\n"
        "stop_2 = 0.286  #222222\n"
        "stop_3 = 0.429  #333333\n"
        "stop_4 = 0.571  #444444\n"
        "stop_5 = 0.714  #555555\n"
        "stop_6 = 0.857  #666666\n"
        "stop_7 = 1.000  #777777\n"
    );
    const auto themes = loadUserThemes();
    check_int("8-stop theme accepted", static_cast<int>(themes.size()), 1);
    if (!themes.empty())
        check_int("all 8 stops present", static_cast<int>(themes[0].stops.size()), 8);
}

static void testTooFewStops() {
    printf("\n[stop count: 1 stop rejected]\n");
    TempDir td;
    td.writeTheme("one.theme",
        "name   = One\n"
        "stop_0 = 0.5  #aabbcc\n"
    );
    const auto themes = loadUserThemes();
    check_int("1-stop theme rejected", static_cast<int>(themes.size()), 0);
}

static void testTooManyStops() {
    printf("\n[stop count: 9 stops rejected]\n");
    TempDir td;
    std::string content = "name = Nine\n";
    for (int i = 0; i < 9; ++i) {
        char line[64];
        std::snprintf(line, sizeof(line), "stop_%d = %.3f  #aabbcc\n",
                      i, static_cast<double>(i) / 8.0);
        content += line;
    }
    td.writeTheme("nine.theme", content);
    const auto themes = loadUserThemes();
    check_int("9-stop theme rejected", static_cast<int>(themes.size()), 0);
}

static void testMalformedHex() {
    printf("\n[malformed hex: invalid color stops skip the stop, not the theme]\n");
    TempDir td;
    // 3 stops, one malformed — only 2 valid stops remain → accepted (≥ 2)
    td.writeTheme("partial.theme",
        "name   = Partial\n"
        "stop_0 = 0.0  #000000\n"
        "stop_1 = 0.5  NOTAHEX\n"   // ← bad color, no leading #
        "stop_2 = 1.0  #ffffff\n"
    );
    const auto themes = loadUserThemes();
    check_int("theme with 1 bad stop accepted (2 valid remain)",
              static_cast<int>(themes.size()), 1);
    if (!themes.empty())
        check_int("only 2 valid stops loaded",
                  static_cast<int>(themes[0].stops.size()), 2);
}

static void testOutOfRangePos() {
    printf("\n[out-of-range pos: stops with pos outside 0-1 are skipped]\n");
    TempDir td;
    td.writeTheme("oob.theme",
        "name   = OOB\n"
        "stop_0 = -0.5  #000000\n"  // pos < 0 → skipped
        "stop_1 =  0.5  #888888\n"
        "stop_2 =  1.5  #ffffff\n"  // pos > 1 → skipped
    );
    // Only stop_1 is valid → 1 stop → theme rejected (< 2 stops)
    const auto themes = loadUserThemes();
    check_int("theme with only 1 in-range stop rejected",
              static_cast<int>(themes.size()), 0);
}

static void testAlphabeticalOrder() {
    printf("\n[load order: themes sorted alphabetically by filename]\n");
    TempDir td;
    td.writeTheme("zzz.theme", "name=ZZZ\nstop_0=0.0 #000000\nstop_1=1.0 #ffffff\n");
    td.writeTheme("aaa.theme", "name=AAA\nstop_0=0.0 #000000\nstop_1=1.0 #ffffff\n");
    td.writeTheme("mmm.theme", "name=MMM\nstop_0=0.0 #000000\nstop_1=1.0 #ffffff\n");

    const auto themes = loadUserThemes();
    check_int("3 themes loaded",                 static_cast<int>(themes.size()), 3);
    if (themes.size() == 3) {
        check_str("first = AAA",  themes[0].name, "AAA");
        check_str("second = MMM", themes[1].name, "MMM");
        check_str("third = ZZZ",  themes[2].name, "ZZZ");
    }
}

static void testNameSanitization() {
    printf("\n[name sanitization: escape codes stripped from name]\n");
    TempDir td;
    td.writeTheme("evil.theme",
        "name   = Normal\033[31mRed\033[0m\n"  // ESC codes
        "stop_0 = 0.0  #000000\n"
        "stop_1 = 1.0  #ffffff\n"
    );
    const auto themes = loadUserThemes();
    check_int("theme loads", static_cast<int>(themes.size()), 1);
    if (!themes.empty()) {
        const bool has_esc = themes[0].name.find('\033') != std::string::npos;
        check_bool("escape code removed from name", has_esc, false);
    }
}

static void testEmptyDirectory() {
    printf("\n[empty directory: returns empty vector]\n");
    TempDir td;
    // Don't write any themes — the directory doesn't even exist yet.
    const auto themes = loadUserThemes();
    check_int("no themes for empty/missing dir", static_cast<int>(themes.size()), 0);
}

static void testFirstLastClamped() {
    printf("\n[stop clamping: first stop clamped to 0.0, last to 1.0]\n");
    TempDir td;
    td.writeTheme("clamped.theme",
        "name   = Clamped\n"
        "stop_0 = 0.05  #000000\n"   // not exactly 0
        "stop_1 = 0.95  #ffffff\n"   // not exactly 1
    );
    const auto themes = loadUserThemes();
    check_int("theme loads", static_cast<int>(themes.size()), 1);
    if (!themes.empty()) {
        check_float("first stop pos clamped to 0.0", themes[0].stops.front().pos, 0.0f);
        check_float("last stop pos clamped to 1.0",  themes[0].stops.back().pos,  1.0f);
    }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("cava-viz user_theme test suite\n");
    printf("================================\n");

    testValidTheme();
    testDefaultName();
    testStopSorting();
    testMinStops();
    testMaxStops();
    testTooFewStops();
    testTooManyStops();
    testMalformedHex();
    testOutOfRangePos();
    testAlphabeticalOrder();
    testNameSanitization();
    testEmptyDirectory();
    testFirstLastClamped();

    printf("\n================================\n");
    printf("Results: %d/%d passed", tests_run - tests_failed, tests_run);
    if (tests_failed == 0)
        printf("  \033[32m— all good\033[0m\n");
    else
        printf("  \033[31m— %d FAILED\033[0m\n", tests_failed);

    return tests_failed ? 1 : 0;
}
