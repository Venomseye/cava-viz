#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

// Beat-flash detection, kept free of ncurses so it can be unit-tested.

/// Mean level of the lowest `n` bars (the "kick"/sub-bass region).
inline float lowBandLevel(const std::vector<float> &bars, std::size_t n = 4) {
  const std::size_t lim = std::min(bars.size(), n);
  if (lim == 0)
    return 0.f;
  float sum = 0.f;
  for (std::size_t i = 0; i < lim; ++i)
    sum += bars[i];
  return sum / static_cast<float>(lim);
}

/// Schmitt trigger: switch ON at/above `on`, switch OFF only below `off`
/// (off < on).  With a single threshold a level hovering around it toggled the
/// flash every frame, and every toggle forces a full repaint — visible flicker.
inline bool nextBeatState(bool prev, float level, float on, float off) {
  return prev ? (level >= off) : (level >= on);
}
