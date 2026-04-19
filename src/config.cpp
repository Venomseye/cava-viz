#include "config.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

std::string Config::configPath() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const char* h = std::getenv("HOME");
    std::string b;
    if (x && x[0]) b = x;
    else if (h && h[0]) b = std::string(h) + "/.config";
    else b = "/tmp";
    return b + "/cava-viz/config";
}

bool Config::load() {
    FILE* f = std::fopen(configPath().c_str(), "r");
    if (!f) return false;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char k[64] = {}, v[256] = {};
        if (std::sscanf(line, " %63[^=] = %255[^\n]", k, v) != 2) continue;
        for (int i=(int)strlen(k)-1; i>=0 && k[i]==' '; --i) k[i]='\0';
        for (int i=(int)strlen(v)-1; i>=0 && (v[i]==' '||v[i]=='\r'); --i) v[i]='\0';
        auto ib = [](const char* s){ return strcmp(s,"1")==0 || strcmp(s,"true")==0; };
        if      (!strcmp(k,"theme"))       theme       = std::atoi(v);
        else if (!strcmp(k,"bar_width"))   bar_width   = std::atoi(v);
        else if (!strcmp(k,"gap_width"))   gap_width   = std::atoi(v);
        else if (!strcmp(k,"sensitivity")) sensitivity = (float)std::atof(v);
        else if (!strcmp(k,"auto_sens"))   auto_sens   = ib(v);
        else if (!strcmp(k,"stereo"))      stereo      = ib(v);
        else if (!strcmp(k,"last_source")) last_source = v;
    }
    std::fclose(f);
    theme       = std::max(0,    std::min(theme,       11));
    bar_width   = std::max(1,    std::min(bar_width,    8));
    gap_width   = std::max(0,    std::min(gap_width,    2));
    sensitivity = std::max(0.2f, std::min(sensitivity,  8.0f));
    return true;
}

void Config::save() const {
    const std::string p = configPath();
    mkdir(p.substr(0, p.rfind('/')).c_str(), 0755);
    FILE* f = std::fopen(p.c_str(), "w");
    if (!f) return;
    fprintf(f, "# cava-viz\n");
    fprintf(f, "theme       = %d\n",   theme);
    fprintf(f, "bar_width   = %d\n",   bar_width);
    fprintf(f, "gap_width   = %d\n",   gap_width);
    fprintf(f, "sensitivity = %.2f\n", (double)sensitivity);
    fprintf(f, "auto_sens   = %d\n",   auto_sens ? 1 : 0);
    fprintf(f, "stereo      = %d\n",   stereo    ? 1 : 0);
    fprintf(f, "last_source = %s\n",   last_source.c_str());
    std::fclose(f);
}
