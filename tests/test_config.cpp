// tests/test_config.cpp
// Config round-trip and parser correctness tests.
// Compile: g++ -std=c++17 -Isrc -o test_config tests/test_config.cpp src/config.cpp
// Run:     ./test_config

#include "config.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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

static void check_float(const char* label, float got, float expected, float tol = 0.001f) {
    ++tests_run;
    const float diff = got - expected;
    const float abs_diff = diff < 0 ? -diff : diff;
    if (abs_diff <= tol) {
        printf("  \033[32mPASS\033[0m  %s\n", label);
    } else {
        ++tests_failed;
        printf("  \033[31mFAIL\033[0m  %s  expected=%.4f got=%.4f\n",
               label, static_cast<double>(expected), static_cast<double>(got));
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

// RAII temp directory — creates a unique dir under /tmp and sets XDG env vars
// so Config::configPath() / statePath() point into it for the duration of the test.
struct TempDir {
    fs::path path;

    TempDir() {
        char tmpl[] = "/tmp/cava_viz_test_XXXXXX";
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
};

// Write a config file directly (bypassing Config::save) for malformed-input tests.
static void writeRawConfig(const std::string& content) {
    const fs::path p = Config::configPath();
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
}

// ── Test groups ───────────────────────────────────────────────────────────────

static void testRoundTrip() {
    printf("\n[round-trip: save then load]\n");
    TempDir td;

    Config orig;
    orig.theme       = 5;
    orig.bar_width   = 4;
    orig.gap_width   = 2;
    orig.hud_pinned  = true;
    orig.colour_cycle   = true;
    orig.per_bar_colour = true;
    orig.stereo      = false;
    orig.high_cutoff = 12000;
    orig.gravity     = 2.50f;
    orig.monstercat  = 0.75f;
    orig.rise_factor = 0.85f;
    orig.bass_smooth = 0.20f;
    orig.a_weighting = true;
    orig.noise_gate  = 0.015f;
    orig.auto_mono   = true;
    orig.sensitivity = 3.14f;
    orig.auto_sens   = false;
    orig.fps         = 30;

    orig.save();

    Config loaded;
    const bool ok = loaded.load();
    check_bool("load() returns true",        ok,                   true);
    check_int ("theme",                      loaded.theme,         orig.theme);
    check_int ("bar_width",                  loaded.bar_width,     orig.bar_width);
    check_int ("gap_width",                  loaded.gap_width,     orig.gap_width);
    check_bool("hud_pinned",                 loaded.hud_pinned,    orig.hud_pinned);
    check_bool("colour_cycle",               loaded.colour_cycle,  orig.colour_cycle);
    check_bool("per_bar_colour",             loaded.per_bar_colour,orig.per_bar_colour);
    check_bool("stereo",                     loaded.stereo,        orig.stereo);
    check_int ("high_cutoff",                loaded.high_cutoff,   orig.high_cutoff);
    check_float("gravity",                   loaded.gravity,       orig.gravity);
    check_float("monstercat",                loaded.monstercat,    orig.monstercat);
    check_float("rise_factor",               loaded.rise_factor,   orig.rise_factor);
    check_float("bass_smooth",               loaded.bass_smooth,   orig.bass_smooth);
    check_bool("a_weighting",                loaded.a_weighting,   orig.a_weighting);
    check_float("noise_gate",                loaded.noise_gate,    orig.noise_gate);
    check_bool("auto_mono",                  loaded.auto_mono,     orig.auto_mono);
    check_float("sensitivity",               loaded.sensitivity,   orig.sensitivity);
    check_bool("auto_sens",                  loaded.auto_sens,     orig.auto_sens);
    check_int ("fps",                        loaded.fps,           orig.fps);
}

static void testCommentPreservation() {
    printf("\n[comment preservation: save preserves user comments]\n");
    TempDir td;

    // Write a config with a user comment and a custom blank line layout.
    writeRawConfig(
        "# My custom comment\n"
        "theme = 3\n"
        "\n"
        "# Another comment\n"
        "fps = 45\n"
    );

    Config c;
    c.load();
    c.bar_width = 6;   // change one field
    c.save();

    // Read the raw file and check comments survived.
    std::ifstream f(Config::configPath());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());

    ++tests_run;
    const bool has_comment = contents.find("# My custom comment") != std::string::npos;
    if (has_comment) printf("  \033[32mPASS\033[0m  user comment preserved\n");
    else { ++tests_failed; printf("  \033[31mFAIL\033[0m  user comment was lost\n"); }

    ++tests_run;
    const bool has_another = contents.find("# Another comment") != std::string::npos;
    if (has_another) printf("  \033[32mPASS\033[0m  second comment preserved\n");
    else { ++tests_failed; printf("  \033[31mFAIL\033[0m  second comment was lost\n"); }

    // Verify the changed value was actually written.
    Config reload;
    reload.load();
    check_int("modified bar_width saved correctly", reload.bar_width, 6);
    check_int("existing theme value preserved",     reload.theme,     3);
    check_int("existing fps value preserved",       reload.fps,       45);
}

static void testDefaults() {
    printf("\n[defaults: missing file returns defaults]\n");
    TempDir td;

    // Do not write any config — XDG dir is empty.
    Config c;
    const bool loaded = c.load();
    // load() should return false (file absent) but leave fields at defaults.
    check_bool("load() returns false for missing file", loaded, false);
    check_int ("default theme",       c.theme,      0);
    check_int ("default fps",         c.fps,        60);
    check_float("default gravity",    c.gravity,    1.0f);
    check_bool("default auto_sens",   c.auto_sens,  true);
    check_bool("default stereo",      c.stereo,     true);
}

static void testClampValidation() {
    printf("\n[clamping: out-of-range values are clamped]\n");
    TempDir td;

    writeRawConfig(
        "bar_width  = 999\n"   // above max 8
        "gap_width  = -5\n"    // below min 0
        "fps        = 9999\n"  // above max 240
        "gravity    = 99.0\n"  // above max 5.0
        "noise_gate = 0.99\n"  // above max 0.2
        "sensitivity = 0.0\n"  // below min 0.2
    );
    Config c;
    c.load();
    check_int  ("bar_width clamped to 8",    c.bar_width,  8);
    check_int  ("gap_width clamped to 0",    c.gap_width,  0);
    check_int  ("fps clamped to 240",        c.fps,        240);
    check_float("gravity clamped to 5.0",    c.gravity,    5.0f);
    check_float("noise_gate clamped to 0.2", c.noise_gate, 0.2f);
    check_float("sensitivity clamped to 0.2",c.sensitivity,0.2f);
}

static void testMalformedFloat() {
    printf("\n[malformed floats: bad values keep existing defaults]\n");
    TempDir td;

    writeRawConfig(
        "gravity = banana\n"
        "fps = 60\n"
    );
    Config c;
    c.load();
    // gravity should remain at its struct default (1.0f) because "banana" is invalid.
    check_float("malformed gravity keeps default", c.gravity, 1.0f);
    check_int  ("valid fps still parsed",          c.fps,     60);
}

static void testStateRoundTrip() {
    printf("\n[state round-trip: last_source saved and restored]\n");
    TempDir td;

    Config c;
    c.last_source = "alsa_output.pci-0000_00_1f.3.monitor";
    c.saveState();

    Config c2;
    c2.loadState();
    check_str("last_source restored",
              c2.last_source,
              "alsa_output.pci-0000_00_1f.3.monitor");
}

static void testThemeIndexNotClamped() {
    printf("\n[theme index: values above 11 not clamped (user themes)]\n");
    TempDir td;

    writeRawConfig("theme = 15\n");
    Config c;
    c.load();
    // theme >= 0 should pass through; the renderer enforces the real upper bound.
    check_int("theme=15 not clamped by config", c.theme, 15);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("cava-viz config test suite\n");
    printf("===========================\n");

    testRoundTrip();
    testCommentPreservation();
    testDefaults();
    testClampValidation();
    testMalformedFloat();
    testStateRoundTrip();
    testThemeIndexNotClamped();

    printf("\n===========================\n");
    printf("Results: %d/%d passed", tests_run - tests_failed, tests_run);
    if (tests_failed == 0)
        printf("  \033[32m— all good\033[0m\n");
    else
        printf("  \033[31m— %d FAILED\033[0m\n", tests_failed);

    return tests_failed ? 1 : 0;
}
