#pragma once
#include <string>
struct Config {
    int   theme       = 0;
    int   bar_width   = 2;
    int   gap_width   = 1;
    float sensitivity = 1.5f;
    bool  auto_sens   = true;
    bool  stereo      = true;
    std::string last_source;
    static std::string configPath();
    bool load();
    void save() const;
};
