#include "user_theme.h"
#include "config.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Strip leading and trailing whitespace in-place.
static void trim(char* s) {
    // leading
    char* p = s;
    while (*p && std::isspace((unsigned char)*p)) ++p;
    if (p != s) std::memmove(s, p, std::strlen(p) + 1);
    // trailing
    int i = (int)std::strlen(s) - 1;
    while (i >= 0 && std::isspace((unsigned char)s[i])) s[i--] = '\0';
}

// Parse #RRGGBB → ncurses 0-1000 scale.
// Returns true on success.
static bool parseHex(const char* s, short& r, short& g, short& b) {
    if (!s || s[0] != '#' || std::strlen(s) != 7) return false;
    unsigned int rv = 0, gv = 0, bv = 0;
    if (std::sscanf(s + 1, "%2x%2x%2x", &rv, &gv, &bv) != 3) return false;
    r = (short)(rv * 1000 / 255);
    g = (short)(gv * 1000 / 255);
    b = (short)(bv * 1000 / 255);
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

std::string themesDir() {
    const std::string cp = Config::configPath();
    const std::size_t slash = cp.rfind('/');
    return (slash != std::string::npos ? cp.substr(0, slash) : cp) + "/themes";
}

std::vector<UserTheme> loadUserThemes() {
    std::vector<UserTheme> result;

    const std::string dir = themesDir();
    DIR* d = opendir(dir.c_str());
    if (!d) return result;

    // Collect .theme filenames, then sort alphabetically for consistency.
    std::vector<std::string> paths;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        const char*  n   = ent->d_name;
        const size_t len = std::strlen(n);
        if (len > 6 && std::strcmp(n + len - 6, ".theme") == 0)
            paths.push_back(dir + "/" + n);
    }
    closedir(d);
    std::sort(paths.begin(), paths.end());

    for (const auto& path : paths) {
        FILE* f = std::fopen(path.c_str(), "r");
        if (!f) continue;

        UserTheme theme;

        // Default name: filename without .theme extension.
        const std::size_t slash = path.rfind('/');
        std::string fname = (slash != std::string::npos)
                          ? path.substr(slash + 1) : path;
        if (fname.size() > 6)
            theme.name = fname.substr(0, fname.size() - 6);
        else
            theme.name = fname;

        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            // Strip CR + LF.
            if (char* p = std::strchr(line, '\r')) *p = '\0';
            if (char* p = std::strchr(line, '\n')) *p = '\0';
            if (line[0] == '#' || line[0] == '\0') continue;

            // Split on the first '='
            char* eq = std::strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            char* key = line;
            char* val = eq + 1;
            trim(key);
            trim(val);
            if (key[0] == '\0' || val[0] == '\0') continue;

            if (std::strcmp(key, "name") == 0) {
                theme.name = val;

            } else if (std::strncmp(key, "stop_", 5) == 0) {
                // Format:  0.75  #RRGGBB
                float pos = 0.f;
                char  hex[16] = {};
                if (std::sscanf(val, "%f %15s", &pos, hex) != 2) continue;
                if (pos < 0.0f || pos > 1.0f) continue;
                short r = 0, g = 0, b = 0;
                if (!parseHex(hex, r, g, b)) continue;
                theme.stops.push_back({pos, r, g, b});
            }
        }
        std::fclose(f);

        // Need at least 2 stops, at most 8.
        if (theme.stops.size() < 2 || theme.stops.size() > 8) continue;

        // Sort by position.
        std::sort(theme.stops.begin(), theme.stops.end(),
                  [](const UserThemeStop& a, const UserThemeStop& b) {
                      return a.pos < b.pos;
                  });

        // Clamp first/last stop to exactly 0 and 1 so sampleUserTheme never
        // has to extrapolate beyond the gradient.
        theme.stops.front().pos = 0.0f;
        theme.stops.back().pos  = 1.0f;

        result.push_back(std::move(theme));
    }

    return result;
}
