#pragma once
/**
 * Faithful C++ port of CAVA's cavacore algorithm.
 *
 * Key design points (from cavacore.c):
 *  - Dual FFT: bass uses 2x buffer for sub-100Hz resolution
 *  - Log-distributed cut-off frequencies (not simple log mapping)
 *  - Per-bar EQ: (1/2^28) * freq^0.85 / log2(fftsize) / bin_count
 *  - Smoothing: quadratic fall (CAVA gravity model) + integral
 *  - Auto-sensitivity: overshoot detection
 *  - Monstercat: adjacent-bar bell-curve propagation
 */
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <fftw3.h>

class FFTProcessor {
public:
    // CAVA default tuning
    static constexpr int   LOW_CUT_OFF      = 50;    // Hz
    static constexpr int   HIGH_CUT_OFF     = 10000; // Hz
    static constexpr int   BASS_CUT_OFF     = 100;   // Hz – split point
    static constexpr double NOISE_REDUCTION = 0.77;  // CAVA default

    // Manual sensitivity controls (on top of autosens)
    static constexpr float SENS_MIN     = 0.2f;
    static constexpr float SENS_MAX     = 8.0f;
    static constexpr float SENS_STEP    = 0.1f;
    static constexpr float SENS_DEFAULT = 1.5f;

    // Monstercat factor (0 = off, 1.5 = CAVA default)
    static constexpr double MONSTERCAT  = 1.5;

    FFTProcessor(int sample_rate = 44100, int channels = 2);
    ~FFTProcessor();

    FFTProcessor(const FFTProcessor&)            = delete;
    FFTProcessor& operator=(const FFTProcessor&) = delete;

    /// Push PCM samples (interleaved stereo or mono) from capture thread.
    void addSamples(const std::vector<float>& samples, int channels);

    /// Compute one display frame.  num_bars may change (plan rebuilds automatically).
    /// Returns false only if num_bars <= 0.
    bool execute(int num_bars, float fps);

    /// Results (valid after execute()).  For mono audio barsR() == barsL().
    const std::vector<float>& barsL() const { return bars_l_; }
    const std::vector<float>& barsR() const { return bars_r_; }

    // Sensitivity
    float sensitivity()         const { return man_sens_.load(); }
    float increaseSensitivity()       { return adjSens(+SENS_STEP); }
    float decreaseSensitivity()       { return adjSens(-SENS_STEP); }
    void  setSensitivity(float v)     { man_sens_.store(std::clamp(v,SENS_MIN,SENS_MAX)); }
    // When disabled, auto-sens multiplier (sens_) is frozen at its current
    // value so the saved manual sensitivity is applied exactly as set.
    void  setAutoSens(bool v)         { auto_sens_enabled_.store(v); }

    int sampleRate() const { return rate_; }
    int channels()   const { return channels_; }
    int numBars()    const { return num_bars_; }

private:
    int  rate_;
    int  channels_;
    int  num_bars_ {0};

    // ── FFT resources (dual: bass + mid/treble) ───────────────────────────────
    int    fft_buf_size_   {0};  // base size (rate-dependent)
    int    bass_buf_size_  {0};  // 2 * fft_buf_size_
    int    input_buf_size_ {0};  // bass_buf_size_ * channels_

    double* in_bass_l_    {nullptr};  fftw_complex* out_bass_l_ {nullptr};
    double* in_bass_r_    {nullptr};  fftw_complex* out_bass_r_ {nullptr};
    double* in_mid_l_     {nullptr};  fftw_complex* out_mid_l_  {nullptr};
    double* in_mid_r_     {nullptr};  fftw_complex* out_mid_r_  {nullptr};

    fftw_plan plan_bass_l_ {nullptr}, plan_bass_r_ {nullptr};
    fftw_plan plan_mid_l_  {nullptr}, plan_mid_r_  {nullptr};

    std::vector<double> bass_win_;   // Hann window for bass buffer
    std::vector<double> mid_win_;    // Hann window for mid buffer
    std::vector<double> input_buf_;  // sliding window (input_buf_size_)

    // ── Per-bar data (rebuilt when num_bars changes) ──────────────────────────
    std::vector<int>    cut_lo_;   // lower FFT bin for each bar
    std::vector<int>    cut_hi_;   // upper FFT bin for each bar
    std::vector<double> eq_;       // EQ multiplier per bar
    int                 bass_cut_bar_ {0};  // first bar >= 100 Hz

    // ── Per-bar smoothing state ───────────────────────────────────────────────
    // Index layout: [0..n-1] = left, [n..2n-1] = right
    std::vector<double> cava_out_;
    std::vector<double> cava_peak_;
    std::vector<double> cava_fall_;
    std::vector<double> cava_mem_;
    std::vector<double> prev_out_;

    // ── Output ────────────────────────────────────────────────────────────────
    std::vector<float> bars_l_, bars_r_;

    // ── Ring buffer (audio thread) ────────────────────────────────────────────
    std::vector<double> ring_;
    size_t              ring_wpos_       {0};
    std::atomic<int>    new_samples_acc_ {0};  // samples accumulated since last execute
    std::mutex          mtx_;

    // ── Framerate tracking ────────────────────────────────────────────────────
    double framerate_  {60.0};
    int    frame_skip_ {1};

    // ── Auto-sensitivity state ────────────────────────────────────────────────
    double              sens_              {1.0};
    bool                sens_init_         {true};
    std::atomic<float>  man_sens_          {SENS_DEFAULT};
    std::atomic<bool>   auto_sens_enabled_ {true};

    // ── Helpers ───────────────────────────────────────────────────────────────
    void  initFFTW();
    void  freeFFTW();
    void  buildPlan(int num_bars);   // compute cut-offs, eq, alloc smooth state
    void  buildHannWindows();

    float adjSens(float d) noexcept;
    void  monstercat(std::vector<double>& bars, double factor) const;
};
