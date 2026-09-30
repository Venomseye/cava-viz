// Regression tests for FFTProcessor: no audio device, no ncurses needed.
// Feeds synthetic PCM straight into addSamples()/execute().
//
// Guards against:
//   * monstercat < ~0.67 amplifying along the spectrum (all bars pinned to 1)
//   * left/right channel swap
//   * silence producing non-zero bars
//   * a pure tone landing in the wrong region of the spectrum

#include "fft_processor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// ── helpers for arbitrary per-sample generators ──────────────────────────────
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  double next() { // uniform [-1, 1)
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<double>(s >> 11) / 4503599627370496.0 - 1.0;
  }
};

// gen(sample_index, left, right)
template <class Gen>
static void feed(FFTProcessor &fft, int frames_n, long &n, Gen gen) {
  const int per_frame = RATE / FPS;
  for (int f = 0; f < frames_n; ++f) {
    std::vector<float> chunk;
    chunk.reserve(static_cast<size_t>(per_frame) * 2);
    for (int i = 0; i < per_frame; ++i, ++n) {
      float l = 0.f, r = 0.f;
      gen(n, l, r);
      chunk.push_back(l);
      chunk.push_back(r);
    }
    fft.addSamples(chunk, 2);
    fft.execute(BARS, static_cast<float>(FPS));
  }
}

static void test_auto_mono_uses_waveform_correlation() {
  {
    // Genuinely mono (L == R) -> must collapse.
    FFTProcessor fft(RATE, 2);
    configure(fft);
    fft.setAutoMono(true);
    Rng rng(1);
    long n = 0;
    feed(fft, 150, n, [&](long, float &l, float &r) {
      l = r = static_cast<float>(0.3 * rng.next());
    });
    CHECK(fft.stereoCorrelation() > 0.97f, "identical channels: corr ~1");
    CHECK(fft.isAutoMonoActive(), "identical channels collapse to mono");
  }
  {
    // Two INDEPENDENT noises: waveforms are unrelated (corr ~0) but their
    // magnitude spectra look alike.  The old magnitude-based correlation
    // scored this ~1.0 and wrongly collapsed real stereo.
    FFTProcessor fft(RATE, 2);
    configure(fft);
    fft.setAutoMono(true);
    Rng rl(11), rr(97);
    long n = 0;
    feed(fft, 300, n, [&](long, float &l, float &r) {
      l = static_cast<float>(0.3 * rl.next());
      r = static_cast<float>(0.3 * rr.next());
    });
    CHECK(fft.stereoCorrelation() < 0.5f, "independent noise: low correlation");
    CHECK(!fft.isAutoMonoActive(), "wide stereo is NOT collapsed");

    // Silence has no defined correlation: state must hold, not drift to mono.
    feed(fft, 300, n, [](long, float &l, float &r) { l = r = 0.f; });
    CHECK(!fft.isAutoMonoActive(), "silence does not flip stereo to mono");
  }
  {
    // Inverted channels are anti-correlated, not mono.
    FFTProcessor fft(RATE, 2);
    configure(fft);
    fft.setAutoMono(true);
    Rng rng(5);
    long n = 0;
    feed(fft, 150, n, [&](long, float &l, float &r) {
      l = static_cast<float>(0.3 * rng.next());
      r = -l;
    });
    CHECK(fft.stereoCorrelation() < -0.5f, "inverted channels: negative corr");
    CHECK(!fft.isAutoMonoActive(), "inverted channels are not collapsed");
  }
}

static void test_auto_sens_ignores_dither() {
  FFTProcessor fft(RATE, 2);
  fft.setAutoSens(true);
  fft.setAutoMono(false);
  fft.setSensitivity(1.0f);
  long n = 0;
  auto music = [](long i, float &l, float &r) {
    l = r = static_cast<float>(0.3 * std::sin(2.0 * PI * 1000.0 * i / RATE));
  };
  feed(fft, 60 * 20, n, music);
  const double g_music = fft.autoGain();

  // A paused player that still emits +-1 LSB dither (not exactly zero).
  Rng rng(3);
  feed(fft, 60 * 60, n, [&](long, float &l, float &r) {
    l = static_cast<float>(std::floor(rng.next() * 1.5) / 32768.0);
    r = static_cast<float>(std::floor(rng.next() * 1.5) / 32768.0);
  });
  const double g_after = fft.autoGain();
  // Old code: +0.1%/frame for 3600 frames => x36.  Must stay put now.
  CHECK(g_after <= g_music * 1.05, "auto-sens does not creep up on dither");

  // ...but a genuinely quiet signal (-60 dBFS) is NOT silence and must still
  // be amplified.
  FFTProcessor quiet(RATE, 2);
  quiet.setAutoSens(true);
  quiet.setAutoMono(false);
  quiet.setSensitivity(1.0f);
  long m = 0;
  feed(quiet, 60 * 20, m, [](long i, float &l, float &r) {
    l = r = static_cast<float>(0.001 * std::sin(2.0 * PI * 1000.0 * i / RATE));
  });
  CHECK(quiet.autoGain() > 5.0, "quiet real signal still gets auto-gain");
}

int main() {
  test_setter_clamp();
  test_silence();
  test_tone_position_and_bounds();
  test_monstercat_low_factor_does_not_saturate();
  test_channel_mapping();
  test_auto_mono_uses_waveform_correlation();
  test_auto_sens_ignores_dither();
  std::printf("fft_processor: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
