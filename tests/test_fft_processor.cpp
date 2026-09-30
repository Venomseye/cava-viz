// Regression tests for FFTProcessor: no audio device, no ncurses needed.
// Feeds synthetic PCM straight into addSamples()/execute().
//
// Guards against:
//   * monstercat < ~0.67 amplifying along the spectrum (all bars pinned to 1)
//   * left/right channel swap
//   * silence producing non-zero bars
//   * a pure tone landing in the wrong region of the spectrum

#include "fft_processor.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_fail = 0, g_pass = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (cond) {                                                                \
      ++g_pass;                                                                \
    } else {                                                                   \
      ++g_fail;                                                                \
      std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, msg);      \
    }                                                                          \
  } while (0)

static constexpr int RATE = 44100;
static constexpr int FPS = 60;
static constexpr int BARS = 40;
static constexpr double PI = 3.14159265358979323846;

// Push `frames_n` display frames of a sine (interleaved L,R) and compute bars.
static void run(FFTProcessor &fft, int channels, double freq, double amp_l,
                double amp_r, int frames_n) {
  const int per_frame = RATE / FPS; // samples per channel per frame
  long n = 0;
  for (int f = 0; f < frames_n; ++f) {
    std::vector<float> chunk;
    chunk.reserve(static_cast<size_t>(per_frame) * channels);
    for (int i = 0; i < per_frame; ++i, ++n) {
      const double v =
          std::sin(2.0 * PI * freq * static_cast<double>(n) / RATE);
      chunk.push_back(static_cast<float>(amp_l * v));
      if (channels == 2)
        chunk.push_back(static_cast<float>(amp_r * v));
    }
    fft.addSamples(chunk, channels);
    fft.execute(BARS, static_cast<float>(FPS));
  }
}

static int argmax(const std::vector<float> &v) {
  int best = 0;
  for (size_t i = 1; i < v.size(); ++i)
    if (v[i] > v[best])
      best = static_cast<int>(i);
  return best;
}

static void configure(FFTProcessor &fft) {
  fft.setAutoSens(false);
  fft.setSensitivity(1.0f);
  fft.setAutoMono(false);
  fft.setBassSmooth(0.0f);
}

static void test_setter_clamp() {
  FFTProcessor fft(RATE, 2);
  fft.setMonstercat(0.0f);
  CHECK(fft.moncatFactor() == 0.0f, "monstercat 0 stays off");
  fft.setMonstercat(-3.0f);
  CHECK(fft.moncatFactor() == 0.0f, "negative monstercat -> off");
  fft.setMonstercat(0.3f);
  CHECK(fft.moncatFactor() >= 1.0f,
        "0<m<1 is raised to >=1.0 (was amplifying)");
  fft.setMonstercat(1.5f);
  CHECK(fft.moncatFactor() == 1.5f, "1.5 unchanged");
  fft.setMonstercat(99.0f);
  CHECK(fft.moncatFactor() == 5.0f, "upper clamp 5.0");
}

static void test_silence() {
  FFTProcessor fft(RATE, 2);
  configure(fft);
  run(fft, 2, 1000.0, 0.0, 0.0, 60);
  float mx = 0.f;
  for (float b : fft.barsL())
    mx = std::max(mx, b);
  for (float b : fft.barsR())
    mx = std::max(mx, b);
  CHECK(mx == 0.0f, "silence gives all-zero bars");
}

static void test_tone_position_and_bounds() {
  FFTProcessor fft(RATE, 2);
  configure(fft);
  fft.setMonstercat(0.0f);
  run(fft, 2, 1000.0, 0.5, 0.5, 120);
  const auto &b = fft.barsL();
  CHECK(static_cast<int>(b.size()) == BARS, "bar count matches request");
  for (float v : b)
    CHECK(v >= 0.0f && v <= 1.0f, "bar within [0,1]");
  const int pk = argmax(b);
  // 1 kHz on a log axis 50 Hz..20 kHz over 40 bars is ~bar 20.
  CHECK(pk >= 16 && pk <= 24, "1 kHz tone peaks near the middle bars");
  CHECK(b[pk] > 0.05f, "tone produces a visible peak");
  CHECK(b[0] < b[pk] * 0.5f, "lowest bar far below the 1 kHz peak");
  CHECK(b[BARS - 1] < b[pk] * 0.5f, "highest bar far below the 1 kHz peak");
}

static void test_monstercat_low_factor_does_not_saturate() {
  // Before the fix, factor 0.3 pinned EVERY bar to 1.0 for a single tone.
  for (float factor : {0.3f, 0.6f, 1.0f, 1.5f}) {
    FFTProcessor fft(RATE, 2);
    configure(fft);
    fft.setMonstercat(factor);
    run(fft, 2, 1000.0, 0.5, 0.5, 120);
    const auto &b = fft.barsL();
    const int pk = argmax(b);
    CHECK(b[pk] > 0.05f, "peak present");
    CHECK(b[0] < 0.5f, "far-left bar not saturated by monstercat");
    CHECK(b[BARS - 1] < 0.5f, "far-right bar not saturated by monstercat");
  }
}

static void test_channel_mapping() {
  {
    FFTProcessor fft(RATE, 2);
    configure(fft);
    fft.setMonstercat(0.0f);
    run(fft, 2, 1000.0, 0.5, 0.0, 120); // left only
    const float l = fft.barsL()[argmax(fft.barsL())];
    const float r = fft.barsR()[argmax(fft.barsR())];
    CHECK(l > 0.05f, "left-only tone shows on barsL");
    CHECK(r < l * 0.2f, "left-only tone is (nearly) absent on barsR");
  }
  {
    FFTProcessor fft(RATE, 2);
    configure(fft);
    fft.setMonstercat(0.0f);
    run(fft, 2, 1000.0, 0.0, 0.5, 120); // right only
    const float l = fft.barsL()[argmax(fft.barsL())];
    const float r = fft.barsR()[argmax(fft.barsR())];
    CHECK(r > 0.05f, "right-only tone shows on barsR");
    CHECK(l < r * 0.2f, "right-only tone is (nearly) absent on barsL");
  }
}

int main() {
  test_setter_clamp();
  test_silence();
  test_tone_position_and_bounds();
  test_monstercat_low_factor_does_not_saturate();
  test_channel_mapping();
  std::printf("fft_processor: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
