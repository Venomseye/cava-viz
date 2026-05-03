#pragma once
/**
 * C++ port of CAVA's cavacore algorithm.
 *  - Dual FFT: bass (2x buffer) + mid/treble (1x buffer)
 *  - Log-distributed cut-off frequencies (CAVA frequency-constant formula)
 *  - Per-bar EQ: (1/2^28) * freq^0.85 / log2(N) / bin_count
 *  - CAVA quadratic-fall gravity model + integral memory
 *  - Auto-sensitivity: overshoot-based feedback
 *  - Monstercat: configurable adjacent-bar bell-curve propagation
 *  - Rise smoothing: configurable attack coefficient
 *
 * Thread safety:
 *   addSamples() — called from the audio capture thread.
 *   execute()    — called from the main (render) thread.
 *   reinit()     — must only be called when the audio thread is stopped.
 */
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <fftw3.h>

class FFTProcessor {
public:
    static constexpr int    LOW_CUT_OFF     = 50;    // Hz
    static constexpr int    HIGH_CUT_OFF    = 20000; // Hz (raised from original 10 kHz)
    static constexpr int    BASS_CUT_OFF    = 100;   // Hz — dual-FFT split point
    static constexpr double NOISE_REDUCTION = 0.77;  // CAVA integral decay factor

    static constexpr float SENS_MIN     = 0.2f;
    static constexpr float SENS_MAX     = 8.0f;
    static constexpr float SENS_STEP    = 0.1f;
    static constexpr float SENS_DEFAULT = 1.5f;

    static constexpr double MONSTERCAT  = 1.5; // default adjacent-bar factor

    FFTProcessor(int sample_rate = 44100, int channels = 2);
    ~FFTProcessor();
    FFTProcessor(const FFTProcessor&)            = delete;
    FFTProcessor& operator=(const FFTProcessor&) = delete;

    /**
     * Reinitialise in-place for a different channel count.
     * The object's address does not change, so [&] captures in audio
     * callbacks remain valid forever.
     * MUST only be called when the audio capture thread is fully stopped.
     */
    void reinit(int new_channels);

    /// Push PCM samples (interleaved) — called from the audio thread.
    void addSamples(const std::vector<float>& samples, int channels);

    /// Compute one display frame. Returns false only if num_bars <= 0.
    bool execute(int num_bars, float fps);

    /// Bar values in [0,1]. barsR() == barsL() when channels == 1.
    const std::vector<float>& barsL() const { return bars_l_; }
    const std::vector<float>& barsR() const { return bars_r_; }

    // ── Sensitivity ───────────────────────────────────────────────────────────
    float sensitivity()         const { return man_sens_.load(); }
    float increaseSensitivity()       { return adjSens(+SENS_STEP); }
    float decreaseSensitivity()       { return adjSens(-SENS_STEP); }
    void  setSensitivity(float v)     { man_sens_.store(std::clamp(v, SENS_MIN, SENS_MAX)); }
    void  setAutoSens(bool v)         { auto_sens_enabled_.store(v); }

    // ── Configurable algorithm parameters ────────────────────────────────────

    /// Fall speed multiplier. 1.0 = CAVA default. Range 0.1–5.0.
    void  setGravity(float g)    { gravity_factor_ = std::clamp(g, 0.1f, 5.0f); }
    float gravity()        const { return gravity_factor_; }

    /// Adjacent-bar propagation. 0 = off, 1.5 = CAVA default. Range 0–5.
    void  setMonstercat(float m) { mcat_factor_ = std::clamp(m, 0.0f, 5.0f); }
    float moncatFactor()   const { return mcat_factor_; }

    /// Upper frequency limit in Hz. Forces plan rebuild on next execute().
    void  setHighCutoff(int hz) {
        const int c = std::clamp(hz, 1000, 24000);
        if (c != high_cutoff_) { high_cutoff_ = c; num_bars_ = 0; }
    }
    int   highCutoff()     const { return high_cutoff_; }

    /// Attack smoothing. 0.0 = instant (original CAVA). Range 0.0–0.95.
    void  setRiseFactor(float r) { rise_factor_ = std::clamp(r, 0.0f, 0.95f); }
    float riseFactor()     const { return rise_factor_; }

    // ── Info ──────────────────────────────────────────────────────────────────
    int sampleRate() const { return rate_; }
    int channels()   const { return channels_; }
    int numBars()    const { return num_bars_; }

private:
    int rate_;
    int channels_;
    int num_bars_ {0};

    // Buffer sizes — set by sample rate in initBufferSizes(); never change.
    int fft_buf_size_   {0};
    int bass_buf_size_  {0};
    // input_buf_size_ = bass_buf_size_ * channels_; changes on reinit.
    int input_buf_size_ {0};

    // FFTW resources
    double*      in_bass_l_  {nullptr};  fftw_complex* out_bass_l_ {nullptr};
    double*      in_bass_r_  {nullptr};  fftw_complex* out_bass_r_ {nullptr};
    double*      in_mid_l_   {nullptr};  fftw_complex* out_mid_l_  {nullptr};
    double*      in_mid_r_   {nullptr};  fftw_complex* out_mid_r_  {nullptr};
    fftw_plan    plan_bass_l_{nullptr},  plan_bass_r_{nullptr};
    fftw_plan    plan_mid_l_ {nullptr},  plan_mid_r_ {nullptr};

    // Hann windows — built once at construction; depend only on buffer sizes.
    std::vector<double> bass_win_;
    std::vector<double> mid_win_;

    std::vector<double> input_buf_;

    // Per-bar frequency plan
    std::vector<int>    cut_lo_;
    std::vector<int>    cut_hi_;
    std::vector<double> eq_;
    int                 bass_cut_bar_ {0};

    // Per-bar smoothing state.
    // Layout: [0..n-1] = left,  [n..2n-1] = right (only used when channels_==2)
    std::vector<double> cava_out_;
    std::vector<double> cava_peak_;
    std::vector<double> cava_fall_;
    std::vector<double> cava_mem_;
    std::vector<double> prev_out_;

    std::vector<float>  bars_l_, bars_r_;

    // Ring buffer (written from the audio thread)
    std::vector<double> ring_;
    size_t              ring_wpos_       {0};
    std::atomic<int>    new_samples_acc_ {0};
    std::mutex          mtx_;

    double framerate_  {60.0};
    int    frame_skip_ {1};

    // Configurable runtime parameters
    float  gravity_factor_ {1.0f};
    float  mcat_factor_    {(float)MONSTERCAT};
    int    high_cutoff_    {HIGH_CUT_OFF};
    float  rise_factor_    {0.3f};

    // Auto-sensitivity state
    double             sens_              {1.0};
    bool               sens_init_         {true};
    std::atomic<float> man_sens_          {SENS_DEFAULT};
    std::atomic<bool>  auto_sens_enabled_ {true};

    // Helpers
    void  initBufferSizes();
    void  buildHannWindows();
    void  initFFTW(bool fast);  // fast=true → FFTW_ESTIMATE (for reinit)
    void  freeFFTW();
    void  buildPlan(int num_bars);
    float adjSens(float d) noexcept;
    void  applyMonstercat(std::vector<double>& bars, double factor) const;
};
