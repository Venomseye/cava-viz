#pragma once
#include <string>
#include <vector>

// ── User-defined gradient theme
// ───────────────────────────────────────────────
//
// A .theme file lives in ~/.config/cava-viz/themes/ and looks like:
//
//   # My cool theme
//   name   = Ocean
//
//   stop_0 = 0.00  #003366
//   stop_1 = 0.40  #0055aa
//   stop_2 = 0.75  #00aaee
//   stop_3 = 1.00  #00ffcc
//
// Rules:
//   • pos  : 0.0 = bar bottom, 1.0 = bar top  (must be in [0, 1])
//   • color: #RRGGBB hex  (24-bit, uppercase or lowercase)
//   • 2–8 stops required; stops may appear in any order in the file
//   • Lines starting with # are comments
//   • name key is optional — defaults to the filename without .theme
//   • The file suffix must be .theme

struct UserThemeStop {
  float pos;     // 0.0–1.0
  short r, g, b; // ncurses scale 0–1000 (converted from #RRGGBB on load)
};

struct UserTheme {
  std::string name;
  std::vector<UserThemeStop> stops; // always sorted ascending by pos
};

// Returns the path to the user themes directory.
// Derived from the main config path, e.g. ~/.config/cava-viz/themes
std::string themesDir();

// Loads all *.theme files from themesDir().
// • Silently ignores a missing directory, unreadable files, and malformed
// files. • Themes are returned in alphabetical filename order (stable across
// reloads).
std::vector<UserTheme> loadUserThemes();
