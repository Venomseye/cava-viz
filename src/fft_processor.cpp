/**
 * C++ port of CAVA's cavacore.c algorithm.
 * See: https://github.com/karlstav/cava/blob/master/cavacore.c
 *
 * Changes vs original:
 *  - HIGH_CUT_OFF raised to 20 kHz; runtime-configurable via setHighCutoff()
 *  - gravity_factor_: runtime fall-speed multiplier (1.0 = CAVA default)
 *  - mcat_factor_: runtime monstercat strength (1.5 = CAVA default, 0 = off)
 *  - rise_factor_: attack smoothing coefficient (0.0 = original CAVA behaviour)
 *  - reinit(ch): reinitialise in-place without moving the object
 *  - Fixed OOB: cava_out_[n+num_bars] OOB write in mono mode
 *  - Hann windows built once at construction; never recomputed
 *  - FFTW_ESTIMATE used in reinit() so the stereo toggle is instant
 *
 * Algorithm improvements:
 *  - EMA framerate window 64→20: tracks FPS changes ~3x faster
 *  - Rise smoothing applied after sensitivity: prevents auto-sens spikes
 *    from triggering false "rising" detection on unrelated bars
 *  - FPS-normalised gravity: fall_step = 0.028 * fr_mod so visual fall
 *    duration is the same at 30, 60, or 120 fps
 *  - FPS-normalised integral: decay = NOISE_REDUCTION^(fps/66) per frame
 *    so the smoothing time constant is constant at any frame rate
 *  - O(n) monstercat: two-pass rolling-max replaces the O(n²) nested loop;
 *    identical output, negligible cost at any bar count
 *  - Auto-sens initial blast fix: sens_ initialised to 1/man_sens_ so
 *    combined_sens starts at exactly 1.0; init ramp reduced 0.1→0.04/frame;
 *    removed broken pre-clamp that prevented sens_init_=false from firing
 */
#include "fft_processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
void FFTProcessor::initBufferSizes() {
    // CAVA lookup table (cavacore.c lines 34-46)
    int base = 512;
    if      (rate_ >   8125 && rate_ <=  16250) base *= 2;
    else if (rate_ >  16250 && rate_ <=  32500) base *= 4;
    else if (rate_ >  32500 && rate_ <=  75000) base *= 8;   // 44100 → 4096
    else if (rate_ >  75000 && rate_ <= 150000) base *= 16;
    else if (rate_ > 150000 && rate_ <= 300000) base *= 32;
    else if (rate_ > 300000)                    base *= 64;
    fft_buf_size_  = base;
    bass_buf_size_ = base * 2;
}

FFTProcessor::FFTProcessor(int sr, int ch)
    : rate_(sr), channels_(ch)
{
    initBufferSizes();
    input_buf_size_ = bass_buf_size_ * channels_;
    input_buf_.assign(input_buf_size_, 0.0);
    ring_.assign(input_buf_size_, 0.0);
    buildHannWindows();
    // Start auto-sens at 1/man_sens so combined_sens = sens_ * man_sens_ = 1.0.
    // Without this, combined_sens starts at 1.0 * 1.5 = 1.5, giving bars a 50%
    // initial boost before auto-sens can react, which causes the initial blast.
    sens_ = 1.0 / (double)man_sens_.load();
    initFFTW(false);   // FFTW_MEASURE at startup
}

FFTProcessor::~FFTProcessor() { freeFFTW(); }

// ── reinit: change channel count without moving the object ───────────────────
// Must only be called when the audio capture thread is stopped.
void FFTProcessor::reinit(int new_channels) {
    // ── 1. Reset ring buffer under the mutex ──────────────────────────────────
    // The audio thread is stopped, but a final addSamples() call may still be
    // completing. The mutex ensures we don't corrupt the ring mid-reset.
    {
        std::lock_guard<std::mutex> lk(mtx_);
        channels_       = new_channels;
        input_buf_size_ = bass_buf_size_ * channels_;
        input_buf_.assign(input_buf_size_, 0.0);
        ring_.assign(input_buf_size_, 0.0);
        ring_wpos_ = 0;
        new_samples_acc_.store(0, std::memory_order_relaxed);
    }

    // ── 2. Rebuild FFTW plans outside the mutex ───────────────────────────────
    // FFTW plan creation doesn't touch the ring buffer so no lock needed.
    // Use FFTW_ESTIMATE here — fast (~µs vs ~100ms for MEASURE). The slight
    // performance difference is imperceptible at 60 fps on a 4096-sample FFT.
    freeFFTW();
    initFFTW(true);   // FFTW_ESTIMATE — instant

    // ── 3. Reset per-bar state (plan rebuilt on next execute()) ───────────────
    num_bars_ = 0;
    cava_out_.clear();  cava_peak_.clear();
    cava_fall_.clear(); cava_mem_.clear();
    prev_out_.clear();
    bars_l_.clear();    bars_r_.clear();

    // Reset auto-sensitivity so the new channel count ramps up cleanly.
    // Start at 1/man_sens_ so combined_sens = 1.0 from the first frame.
    sens_      = 1.0 / (double)man_sens_.load();
    sens_init_ = true;
    framerate_ = 60.0;
    frame_skip_= 1;

    // Hann windows are unchanged — they depend only on buffer sizes, not channels.
}

// ── Hann windows (built once in constructor; never recomputed) ────────────────
void FFTProcessor::buildHannWindows() {
    bass_win_.resize(bass_buf_size_);
    mid_win_.resize(fft_buf_size_);
    for (int i = 0; i < bass_buf_size_; ++i)
        bass_win_[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (bass_buf_size_ - 1)));
    for (int i = 0; i < fft_buf_size_; ++i)
        mid_win_[i]  = 0.5 * (1.0 - cos(2.0 * M_PI * i / (fft_buf_size_  - 1)));
}

// ── FFTW allocation ───────────────────────────────────────────────────────────
// fast=false → FFTW_MEASURE (best runtime perf, slow to plan)
// fast=true  → FFTW_ESTIMATE (instant, slightly lower runtime perf — used in reinit)
void FFTProcessor::initFFTW(bool fast) {
    const unsigned flags = fast ? FFTW_ESTIMATE : FFTW_MEASURE;

    in_bass_l_  = fftw_alloc_real(bass_buf_size_);
    in_mid_l_   = fftw_alloc_real(fft_buf_size_);
    out_bass_l_ = fftw_alloc_complex(bass_buf_size_ / 2 + 1);
    out_mid_l_  = fftw_alloc_complex(fft_buf_size_  / 2 + 1);
    if (!in_bass_l_ || !in_mid_l_ || !out_bass_l_ || !out_mid_l_)
        throw std::runtime_error("FFTProcessor: FFTW alloc failed (L)");

    plan_bass_l_ = fftw_plan_dft_r2c_1d(bass_buf_size_, in_bass_l_, out_bass_l_, flags);
    plan_mid_l_  = fftw_plan_dft_r2c_1d(fft_buf_size_,  in_mid_l_,  out_mid_l_,  flags);

    if (channels_ == 2) {
        in_bass_r_  = fftw_alloc_real(bass_buf_size_);
        in_mid_r_   = fftw_alloc_real(fft_buf_size_);
        out_bass_r_ = fftw_alloc_complex(bass_buf_size_ / 2 + 1);
        out_mid_r_  = fftw_alloc_complex(fft_buf_size_  / 2 + 1);
        if (!in_bass_r_ || !in_mid_r_ || !out_bass_r_ || !out_mid_r_)
            throw std::runtime_error("FFTProcessor: FFTW alloc failed (R)");
        plan_bass_r_ = fftw_plan_dft_r2c_1d(bass_buf_size_, in_bass_r_, out_bass_r_, flags);
        plan_mid_r_  = fftw_plan_dft_r2c_1d(fft_buf_size_,  in_mid_r_,  out_mid_r_,  flags);
    }

    memset(in_bass_l_, 0, sizeof(double) * bass_buf_size_);
    memset(in_mid_l_,  0, sizeof(double) * fft_buf_size_);
}

void FFTProcessor::freeFFTW() {
    if (plan_bass_l_) { fftw_destroy_plan(plan_bass_l_); plan_bass_l_ = nullptr; }
    if (plan_mid_l_)  { fftw_destroy_plan(plan_mid_l_);  plan_mid_l_  = nullptr; }
    if (plan_bass_r_) { fftw_destroy_plan(plan_bass_r_); plan_bass_r_ = nullptr; }
    if (plan_mid_r_)  { fftw_destroy_plan(plan_mid_r_);  plan_mid_r_  = nullptr; }
    if (out_bass_l_)  { fftw_free(out_bass_l_); out_bass_l_ = nullptr; }
    if (out_mid_l_)   { fftw_free(out_mid_l_);  out_mid_l_  = nullptr; }
    if (out_bass_r_)  { fftw_free(out_bass_r_); out_bass_r_ = nullptr; }
    if (out_mid_r_)   { fftw_free(out_mid_r_);  out_mid_r_  = nullptr; }
    if (in_bass_l_)   { fftw_free(in_bass_l_);  in_bass_l_  = nullptr; }
    if (in_mid_l_)    { fftw_free(in_mid_l_);   in_mid_l_   = nullptr; }
    if (in_bass_r_)   { fftw_free(in_bass_r_);  in_bass_r_  = nullptr; }
    if (in_mid_r_)    { fftw_free(in_mid_r_);   in_mid_r_   = nullptr; }
}

// ── Frequency plan (cut-offs + EQ) ───────────────────────────────────────────
// Faithful port of cava_init() (cavacore.c lines 147-256).
// Uses high_cutoff_ so runtime changes via setHighCutoff() are honoured.
void FFTProcessor::buildPlan(int num_bars) {
    num_bars_ = num_bars;

    cut_lo_.resize(num_bars + 1);
    cut_hi_.resize(num_bars + 1);
    eq_.resize(num_bars);

    // Smoothing arrays: layout [0..n-1]=left, [n..2n-1]=right
    // Always allocate 2*num_bars so the second half is valid for stereo.
    // The mono path never writes or reads the second half.
    const int total = num_bars * 2;
    cava_out_.assign(total, 0.0);
    cava_peak_.assign(total, 0.0);
    cava_fall_.assign(total, 0.0);
    cava_mem_.assign(total, 0.0);
    prev_out_.assign(total, 0.0);

    bars_l_.assign(num_bars, 0.f);
    bars_r_.assign(num_bars, 0.f);

    const int lo = LOW_CUT_OFF;
    const int hi = high_cutoff_;

    // Frequency constant (cavacore.c line 164)
    double fc = log10((double)lo / hi) / (1.0 / (num_bars + 1) - 1.0);
    const float min_bw = (float)rate_ / bass_buf_size_;

    std::vector<float> cut_freq(num_bars + 1);
    bass_cut_bar_ = 0;
    bool first_bar = true;

    for (int n = 0; n <= num_bars; ++n) {
        double coeff = fc * (-1.0) + (double)(n + 1) / (num_bars + 1) * fc;
        cut_freq[n] = (float)(hi * pow(10.0, coeff));

        if (n > 0 && cut_freq[n-1] >= cut_freq[n])
            cut_freq[n] = cut_freq[n-1] + min_bw;

        float rel = cut_freq[n] / ((float)rate_ / 2.0f);

        if (cut_freq[n] < BASS_CUT_OFF) {
            cut_lo_[n] = (int)(rel * (bass_buf_size_ / 2));
            cut_lo_[n] = std::min(cut_lo_[n], bass_buf_size_ / 2);
            bass_cut_bar_++;
            if (bass_cut_bar_ > 1) first_bar = false;
        } else {
            cut_lo_[n] = (int)ceil(rel * (float)(fft_buf_size_ / 2));
            if (n == bass_cut_bar_) {
                first_bar = true;
                if (n > 0)
                    cut_hi_[n-1] = (int)(rel * (bass_buf_size_ / 2)) - 1;
            } else {
                first_bar = false;
            }
            cut_lo_[n] = std::min(cut_lo_[n], fft_buf_size_ / 2);
        }

        if (n > 0) {
            if (!first_bar) {
                cut_hi_[n-1] = cut_lo_[n] - 1;
                if (cut_lo_[n] <= cut_lo_[n-1]) {
                    int maxbin = (n < bass_cut_bar_) ? bass_buf_size_/2 : fft_buf_size_/2;
                    if (cut_lo_[n-1] + 1 < maxbin + 1) {
                        cut_lo_[n]   = cut_lo_[n-1] + 1;
                        cut_hi_[n-1] = cut_lo_[n] - 1;
                    }
                }
            } else {
                if (cut_hi_[n-1] < cut_lo_[n-1])
                    cut_hi_[n-1] = cut_lo_[n-1] + 1;
            }
        }

        if (n < bass_cut_bar_)
            cut_freq[n] = (float)cut_lo_[n] / (float)(bass_buf_size_/2) * ((float)rate_/2.0f);
        else
            cut_freq[n] = (float)cut_lo_[n] / (float)(fft_buf_size_/2)  * ((float)rate_/2.0f);
    }

    // EQ per bar (cavacore.c lines 238-256)
    for (int n = 0; n < num_bars; ++n) {
        eq_[n]  = 1.0 / pow(2.0, 28.0);
        eq_[n] *= pow((double)cut_freq[n+1], 0.85);
        eq_[n] /= (n < bass_cut_bar_) ? log2((double)bass_buf_size_)
                                       : log2((double)fft_buf_size_);
        int span = cut_hi_[n] - cut_lo_[n] + 1;
        if (span < 1) span = 1;
        eq_[n] /= span;
    }
}

// ── addSamples (audio thread) ─────────────────────────────────────────────────
void FFTProcessor::addSamples(const std::vector<float>& s, int /*ch*/) {
    // try_to_lock: drop this batch rather than blocking the audio callback.
    // Dropped batches are rare (only when execute() holds the lock briefly).
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock()) return;
    const size_t rsize = ring_.size();
    for (float v : s) {
        ring_[ring_wpos_] = (double)v * 32768.0;  // scale to CAVA's EQ range
        ring_wpos_ = (ring_wpos_ + 1) % rsize;
    }
    new_samples_acc_.fetch_add((int)s.size(), std::memory_order_relaxed);
}

// ── Monstercat O(n) rolling-max ───────────────────────────────────────────────
// Replaces the original O(n²) nested loop with two linear passes that produce
// identical output.
//
// Each bar propagates its value to neighbours as:  neighbour = max(neighbour, self / decay^dist)
//
// Left→right pass: bars[i] = max(bars[i], bars[i-1] / decay)
//   → each bar inherits the attenuated maximum from everything to its left.
// Right→left pass: bars[i] = max(bars[i], bars[i+1] / decay)
//   → each bar inherits the attenuated maximum from everything to its right.
//
// Two passes give the same result as the O(n²) because the rolling max carries
// the peak value across any distance with the correct per-step attenuation.
void FFTProcessor::applyMonstercat(std::vector<double>& bars, double factor) const {
    const int    n     = (int)bars.size();
    if (n < 2 || factor <= 0.0) return;
    const double decay = factor * 1.5;   // matches original: pow(factor*1.5, dist)

    // Left → right
    for (int i = 1; i < n; ++i)
        bars[i] = std::max(bars[i], bars[i-1] / decay);

    // Right → left
    for (int i = n-2; i >= 0; --i)
        bars[i] = std::max(bars[i], bars[i+1] / decay);
}

// ── execute ───────────────────────────────────────────────────────────────────
bool FFTProcessor::execute(int num_bars, float /*fps*/) {
    if (num_bars <= 0) return false;

    // Rebuild plan if bar count changed or setHighCutoff() was called.
    if (num_bars != num_bars_)
        buildPlan(num_bars);

    int new_samples = new_samples_acc_.exchange(0, std::memory_order_relaxed);
    bool silence = true;

    if (new_samples > 0) {
        // EMA framerate from actual sample count.
        // Window of 20 (was CAVA's 64) tracks FPS changes ~3x faster while
        // still smoothing out frame-time noise.
        framerate_ -= framerate_ / 20.0;
        framerate_ += (double)(rate_ * frame_skip_) /
                      ((double)new_samples / channels_) / 20.0;
        frame_skip_ = 1;

        {
            std::lock_guard<std::mutex> lk(mtx_);
            int fill = std::min(new_samples, input_buf_size_);
            // Shift older samples right to make room at front
            for (int n = input_buf_size_-1; n >= fill; --n)
                input_buf_[n] = input_buf_[n - fill];
            // Fill front: newest sample at index 0 (CAVA ring reversal)
            const size_t rsize = ring_.size();
            for (int n = 0; n < fill; ++n) {
                const size_t idx = (ring_wpos_ + rsize - 1 - n) % rsize;
                input_buf_[n] = ring_[idx];
                if (ring_[idx] != 0.0) silence = false;
            }
        }
    } else {
        frame_skip_++;
    }

    // ── Deinterleave into bass and mid FFT input buffers ──────────────────────
    // CAVA stereo layout: in_r[n] = input_buf[n*2], in_l[n] = input_buf[n*2+1]
    if (channels_ == 2) {
        for (int n = 0; n < bass_buf_size_; ++n) {
            in_bass_r_[n] = input_buf_[n * 2];
            in_bass_l_[n] = input_buf_[n * 2 + 1];
        }
        for (int n = 0; n < fft_buf_size_; ++n) {
            in_mid_r_[n] = input_buf_[n * 2];
            in_mid_l_[n] = input_buf_[n * 2 + 1];
        }
    } else {
        for (int n = 0; n < bass_buf_size_; ++n) in_bass_l_[n] = input_buf_[n];
        for (int n = 0; n < fft_buf_size_;  ++n) in_mid_l_[n]  = input_buf_[n];
    }

    // ── Hann window ───────────────────────────────────────────────────────────
    for (int i = 0; i < bass_buf_size_; ++i) {
        in_bass_l_[i] *= bass_win_[i];
        if (channels_ == 2) in_bass_r_[i] *= bass_win_[i];
    }
    for (int i = 0; i < fft_buf_size_; ++i) {
        in_mid_l_[i] *= mid_win_[i];
        if (channels_ == 2) in_mid_r_[i] *= mid_win_[i];
    }

    // ── FFTs ──────────────────────────────────────────────────────────────────
    fftw_execute(plan_bass_l_);
    fftw_execute(plan_mid_l_);
    if (channels_ == 2) {
        fftw_execute(plan_bass_r_);
        fftw_execute(plan_mid_r_);
    }

    // ── Bin → bar magnitude ───────────────────────────────────────────────────
    // FIX: Only write cava_out_[n + num_bars] when channels_==2.
    //      The original code wrote it unconditionally, which was OOB for mono
    //      (cava_out_ had size num_bars, not 2*num_bars, when channels_==1).
    //      buildPlan() now always allocates 2*num_bars so both paths are safe,
    //      but we still skip the right-channel write for mono to stay clean.
    for (int n = 0; n < num_bars; ++n) {
        double temp_l = 0.0, temp_r = 0.0;
        for (int i = cut_lo_[n]; i <= cut_hi_[n]; ++i) {
            if (n < bass_cut_bar_) {
                temp_l += hypot(out_bass_l_[i][0], out_bass_l_[i][1]);
                if (channels_ == 2)
                    temp_r += hypot(out_bass_r_[i][0], out_bass_r_[i][1]);
            } else {
                temp_l += hypot(out_mid_l_[i][0], out_mid_l_[i][1]);
                if (channels_ == 2)
                    temp_r += hypot(out_mid_r_[i][0], out_mid_r_[i][1]);
            }
        }
        cava_out_[n] = temp_l * eq_[n];
        if (channels_ == 2)
            cava_out_[n + num_bars] = temp_r * eq_[n];
    }

    // ── Sensitivity ───────────────────────────────────────────────────────────
    const double combined_sens = sens_ * (double)man_sens_.load();
    // Apply to left half always; right half only when stereo.
    for (int n = 0; n < num_bars; ++n)
        cava_out_[n] *= combined_sens;
    if (channels_ == 2)
        for (int n = num_bars; n < num_bars * 2; ++n)
            cava_out_[n] *= combined_sens;

    // ── Smoothing ─────────────────────────────────────────────────────────────
    //
    // FPS normalisation rationale:
    //
    // fr_mod = reference_fps / actual_fps  (= 1.0 at 66 fps)
    //
    // Gravity fall: the bar falls as  peak * (1 - fall² * gravity_mod).
    //   `fall` increments by fall_step each frame.  For the visual duration to
    //   be FPS-independent, fall_step must scale inversely with FPS:
    //     fall_step = 0.028 * fr_mod
    //   `gravity_mod` is then the reference-fps value with no extra fr_mod power
    //   (since the step itself is already normalised).
    //
    // Integral smoothing: proper per-frame decay is NOISE_REDUCTION^(fps/66).
    //   At 66 fps: pow(0.77, 1) = 0.77  (matches original)
    //   At 30 fps: pow(0.77, 0.45) ≈ 0.88  (less decay per frame → same per second)
    //   At 120fps: pow(0.77, 1.82) ≈ 0.62  (more decay per frame → same per second)
    //   The old `pow(fr_mod, 0.1)` correction changed integ by only ±8% at 30/120 fps,
    //   leaving most of the FPS dependence uncorrected.

    const double fps_d        = std::max(framerate_, 1.0);
    const double fr_mod       = 66.0 / fps_d;
    // Gravity: base constant at reference fps; fall_step provides FPS scaling.
    const double gravity_mod  = 2.0 / NOISE_REDUCTION * (double)gravity_factor_;
    const double fall_step    = 0.028 * fr_mod;   // FPS-normalised fall increment
    // Integral: per-frame decay that gives constant per-second behaviour at any fps.
    const double decay_frame  = pow(NOISE_REDUCTION, fps_d / 66.0);

    int overshoot = 0;

    // Process left always, right only when stereo.
    const int smooth_count = num_bars * (channels_ == 2 ? 2 : 1);

    for (int n = 0; n < smooth_count; ++n) {

        // ── Rise smoothing ────────────────────────────────────────────────────
        // Applied AFTER sensitivity so a temporary auto-sens spike doesn't
        // cause false "rising" detection and slow down unrelated bars.
        // rise_factor_=0.0 gives original CAVA instant-rise behaviour.
        if (rise_factor_ > 0.0f && cava_out_[n] > prev_out_[n]) {
            cava_out_[n] = prev_out_[n]
                         + (cava_out_[n] - prev_out_[n]) * (1.0 - (double)rise_factor_);
        }

        // ── Gravity fall (CAVA model, FPS-normalised) ─────────────────────────
        if (cava_out_[n] < prev_out_[n] && NOISE_REDUCTION > 0.1) {
            cava_out_[n] = cava_peak_[n]
                         * (1.0 - (cava_fall_[n] * cava_fall_[n] * gravity_mod));
            if (cava_out_[n] < 0.0) cava_out_[n] = 0.0;
            cava_fall_[n] += fall_step;   // FPS-normalised (was hardcoded 0.028)
        } else {
            cava_peak_[n] = cava_out_[n];
            cava_fall_[n] = 0.0;
        }
        prev_out_[n] = cava_out_[n];

        // ── Integral smoothing (FPS-normalised) ───────────────────────────────
        cava_out_[n] = cava_mem_[n] * decay_frame + cava_out_[n];
        cava_mem_[n] = cava_out_[n];

        if (cava_out_[n] > 1.0) { overshoot = 1; cava_out_[n] = 1.0; }
        if (cava_out_[n] < 0.0) cava_out_[n] = 0.0;
    }

    // ── Auto-sensitivity ──────────────────────────────────────────────────────
    if (auto_sens_enabled_.load()) {
        // Overshoot path: reduce sens_ and exit init phase.
        // IMPORTANT: must always set sens_init_=false on overshoot so the faster
        // init ramp stops firing and the system settles at the correct level.
        if (overshoot) {
            sens_ *= (1.0 - 0.02 * fr_mod);
            sens_init_ = false;
        } else if (!silence) {
            // Steady-state ramp: very slow (+0.1% per frame at 66fps).
            sens_ *= (1.0 + 0.001 * fr_mod);
            // Init ramp: faster rise so bars reach a good level quickly.
            // 0.04/frame at 66fps doubles sens_ in ~17 frames (~0.26s).
            // Original CAVA used 0.1/frame (doubled in 7 frames = 0.1s) which
            // caused the initial blast because overshoot detection only fires
            // AFTER bars are already drawn at full height.
            // 0.04 is fast enough to feel responsive but slow enough that the
            // overshoot path fires before bars visually blast to the ceiling.
            if (sens_init_) sens_ *= (1.0 + 0.04 * fr_mod);
        }
        sens_ = std::max(0.01, std::min(sens_, 1000.0));
    } else {
        sens_      = 1.0;
        sens_init_ = true;
    }

    // ── Monstercat bell-curve propagation ─────────────────────────────────────
    if (mcat_factor_ > 0.0f) {
        std::vector<double> tmp_l(cava_out_.begin(), cava_out_.begin() + num_bars);
        applyMonstercat(tmp_l, (double)mcat_factor_);
        for (int n = 0; n < num_bars; ++n) cava_out_[n] = tmp_l[n];

        if (channels_ == 2) {
            std::vector<double> tmp_r(cava_out_.begin() + num_bars,
                                      cava_out_.begin() + num_bars * 2);
            applyMonstercat(tmp_r, (double)mcat_factor_);
            for (int n = 0; n < num_bars; ++n) cava_out_[n + num_bars] = tmp_r[n];
        }
    }

    // ── Copy to output ────────────────────────────────────────────────────────
    for (int n = 0; n < num_bars; ++n) {
        bars_l_[n] = (float)std::clamp(cava_out_[n], 0.0, 1.0);
        bars_r_[n] = (channels_ == 2)
                   ? (float)std::clamp(cava_out_[n + num_bars], 0.0, 1.0)
                   : bars_l_[n];
    }
    return true;
}

float FFTProcessor::adjSens(float d) noexcept {
    float s = std::clamp(man_sens_.load() + d, SENS_MIN, SENS_MAX);
    man_sens_.store(s);
    return s;
}
