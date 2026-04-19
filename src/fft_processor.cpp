/**
 * C++ port of CAVA's cavacore.c algorithm.
 * See: https://github.com/karlstav/cava/blob/master/cavacore.c
 *
 * Key CAVA design decisions preserved:
 *  1. Dual FFT: bass (2x buffer) + mid/treble (1x buffer)
 *  2. Frequency-constant log distribution of bars (not simple log10 mapping)
 *  3. Per-bar EQ: (1/2^28) * freq^0.85 / log2(N) / bin_count
 *  4. Smoothing: peak * (1 - fall² * gravity_mod)  +  integral memory
 *  5. Auto-sensitivity: overshoot-based ±
 *  6. Monstercat: adjacent bars get max(peak / distance^falloff, self)
 */
#include "fft_processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
FFTProcessor::FFTProcessor(int sr, int ch)
    : rate_(sr), channels_(ch)
{
    // Choose base FFT buffer size from CAVA's table (cavacore.c lines 34-46)
    int base = 512;
    if      (sr >   8125 && sr <=  16250) base *= 2;
    else if (sr >  16250 && sr <=  32500) base *= 4;
    else if (sr >  32500 && sr <=  75000) base *= 8;   // 44100 Hz → 4096
    else if (sr >  75000 && sr <= 150000) base *= 16;
    else if (sr > 150000 && sr <= 300000) base *= 32;
    else if (sr > 300000)                 base *= 64;

    fft_buf_size_   = base;
    bass_buf_size_  = base * 2;
    input_buf_size_ = bass_buf_size_ * channels_;

    input_buf_.assign(input_buf_size_, 0.0);

    // Ring buffer: hold enough for bass window * channels
    ring_.assign(input_buf_size_, 0.0);

    buildHannWindows();
    initFFTW();
}

FFTProcessor::~FFTProcessor() { freeFFTW(); }

// ── Hann windows ──────────────────────────────────────────────────────────────
void FFTProcessor::buildHannWindows() {
    bass_win_.resize(bass_buf_size_);
    mid_win_.resize(fft_buf_size_);
    for (int i = 0; i < bass_buf_size_; ++i)
        bass_win_[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (bass_buf_size_ - 1)));
    for (int i = 0; i < fft_buf_size_; ++i)
        mid_win_[i]  = 0.5 * (1.0 - cos(2.0 * M_PI * i / (fft_buf_size_  - 1)));
}

// ── FFTW allocation ───────────────────────────────────────────────────────────
void FFTProcessor::initFFTW() {
    in_bass_l_ = fftw_alloc_real(bass_buf_size_);
    in_mid_l_  = fftw_alloc_real(fft_buf_size_);
    out_bass_l_ = fftw_alloc_complex(bass_buf_size_ / 2 + 1);
    out_mid_l_  = fftw_alloc_complex(fft_buf_size_  / 2 + 1);
    if (!in_bass_l_ || !in_mid_l_ || !out_bass_l_ || !out_mid_l_)
        throw std::runtime_error("FFTProcessor: FFTW alloc failed (L)");

    plan_bass_l_ = fftw_plan_dft_r2c_1d(bass_buf_size_, in_bass_l_, out_bass_l_, FFTW_MEASURE);
    plan_mid_l_  = fftw_plan_dft_r2c_1d(fft_buf_size_,  in_mid_l_,  out_mid_l_,  FFTW_MEASURE);

    if (channels_ == 2) {
        in_bass_r_  = fftw_alloc_real(bass_buf_size_);
        in_mid_r_   = fftw_alloc_real(fft_buf_size_);
        out_bass_r_ = fftw_alloc_complex(bass_buf_size_ / 2 + 1);
        out_mid_r_  = fftw_alloc_complex(fft_buf_size_  / 2 + 1);
        if (!in_bass_r_ || !in_mid_r_ || !out_bass_r_ || !out_mid_r_)
            throw std::runtime_error("FFTProcessor: FFTW alloc failed (R)");
        plan_bass_r_ = fftw_plan_dft_r2c_1d(bass_buf_size_, in_bass_r_, out_bass_r_, FFTW_MEASURE);
        plan_mid_r_  = fftw_plan_dft_r2c_1d(fft_buf_size_,  in_mid_r_,  out_mid_r_,  FFTW_MEASURE);
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

// ── Plan: compute cut-offs, EQ, allocate smoothing arrays ────────────────────
// Port of cava_init() frequency distribution (cavacore.c lines 147-235)
void FFTProcessor::buildPlan(int num_bars) {
    num_bars_ = num_bars;

    cut_lo_.resize(num_bars + 1);
    cut_hi_.resize(num_bars + 1);
    eq_.resize(num_bars);

    cava_out_.assign(num_bars * channels_, 0.0);
    cava_peak_.assign(num_bars * channels_, 0.0);
    cava_fall_.assign(num_bars * channels_, 0.0);
    cava_mem_.assign(num_bars * channels_, 0.0);
    prev_out_.assign(num_bars * channels_, 0.0);

    bars_l_.assign(num_bars, 0.f);
    bars_r_.assign(num_bars, 0.f);

    int lo = LOW_CUT_OFF, hi = HIGH_CUT_OFF;

    // Frequency constant (CAVA formula, cavacore.c line 164)
    double fc = log10((double)lo / hi) / (1.0 / (num_bars + 1) - 1.0);

    float min_bw = (float)rate_ / bass_buf_size_;

    std::vector<float> cut_freq(num_bars + 1);
    bass_cut_bar_ = 0;
    bool first_bar = true;

    for (int n = 0; n <= num_bars; ++n) {
        double coeff = fc * (-1.0) + (double)(n + 1) / (num_bars + 1) * fc;
        cut_freq[n] = (float)(hi * pow(10.0, coeff));

        if (n > 0 && cut_freq[n - 1] >= cut_freq[n])
            cut_freq[n] = cut_freq[n - 1] + min_bw;

        float rel = cut_freq[n] / ((float)rate_ / 2.0f);

        if (cut_freq[n] < BASS_CUT_OFF) {
            // Bass FFT bins
            cut_lo_[n] = (int)(rel * (bass_buf_size_ / 2));
            cut_lo_[n] = std::min(cut_lo_[n], bass_buf_size_ / 2);
            bass_cut_bar_++;
            if (bass_cut_bar_ > 1) first_bar = false;
        } else {
            // Mid/treble FFT bins
            cut_lo_[n] = (int)ceil(rel * (float)(fft_buf_size_ / 2));
            if (n == bass_cut_bar_) {
                first_bar = true;
                if (n > 0)
                    cut_hi_[n - 1] = (int)(rel * (bass_buf_size_ / 2)) - 1;
            } else {
                first_bar = false;
            }
            cut_lo_[n] = std::min(cut_lo_[n], fft_buf_size_ / 2);
        }

        if (n > 0) {
            if (!first_bar) {
                cut_hi_[n - 1] = cut_lo_[n] - 1;
                // Push up if clumped
                if (cut_lo_[n] <= cut_lo_[n - 1]) {
                    int maxbin = (n < bass_cut_bar_) ? bass_buf_size_ / 2 : fft_buf_size_ / 2;
                    if (cut_lo_[n - 1] + 1 < maxbin + 1) {
                        cut_lo_[n]     = cut_lo_[n - 1] + 1;
                        cut_hi_[n - 1] = cut_lo_[n] - 1;
                    }
                }
            } else {
                if (cut_hi_[n - 1] < cut_lo_[n - 1])
                    cut_hi_[n - 1] = cut_lo_[n - 1] + 1;
            }
        }

        // Update cut_freq from actual bins
        if (n < bass_cut_bar_)
            cut_freq[n] = (float)cut_lo_[n] / (float)(bass_buf_size_ / 2) * ((float)rate_ / 2.0f);
        else
            cut_freq[n] = (float)cut_lo_[n] / (float)(fft_buf_size_  / 2) * ((float)rate_ / 2.0f);
    }

    // EQ per bar (cavacore.c lines 238-256)
    for (int n = 0; n < num_bars; ++n) {
        eq_[n] = 1.0 / pow(2.0, 28.0);
        eq_[n] *= pow((double)cut_freq[n + 1], 0.85);
        if (n < bass_cut_bar_)
            eq_[n] /= log2((double)bass_buf_size_);
        else
            eq_[n] /= log2((double)fft_buf_size_);
        int span = cut_hi_[n] - cut_lo_[n] + 1;
        if (span < 1) span = 1;
        eq_[n] /= span;
    }
}

// ── Audio input ───────────────────────────────────────────────────────────────
// Stores interleaved samples verbatim into the ring buffer, preserving the
// L/R interleave order expected by CAVA's channel split in execute().
// Normalises float [-1,1] → int16 range (×32768) to match CAVA's EQ scaling.
void FFTProcessor::addSamples(const std::vector<float>& s, int /*ch*/) {
    // try_lock: if execute() holds the mutex, drop this batch rather than
    // blocking the audio callback (avoids priority inversion / xruns).
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock()) return;
    const size_t rsize = ring_.size();
    for (float v : s) {
        ring_[ring_wpos_] = (double)v * 32768.0;
        ring_wpos_ = (ring_wpos_ + 1) % rsize;
    }
    new_samples_acc_.fetch_add((int)s.size(), std::memory_order_relaxed);
}

// ── Execute one frame ─────────────────────────────────────────────────────────
// Port of cava_execute() from cavacore.c
bool FFTProcessor::execute(int num_bars, float fps) {
    if (num_bars <= 0) return false;

    // Rebuild plan if bar count changed
    if (num_bars != num_bars_)
        buildPlan(num_bars);

    int new_samples = new_samples_acc_.exchange(0, std::memory_order_relaxed);

    bool silence = true;

    if (new_samples > 0) {
        // Framerate estimation (EMA) – from cavacore.c
        // new_samples is total floats pushed (both channels); divide by channels_
        // to get the number of audio frames for the framerate calculation.
        framerate_ -= framerate_ / 64.0;
        framerate_ += (double)(rate_ * frame_skip_) /
                      ((double)new_samples / channels_) / 64.0;
        frame_skip_ = 1;

        // Shift input_buf right and fill the front with the newest samples.
        // CAVA layout: input_buf[0] = most-recent sample, input_buf[1] = one before, ...
        // For stereo interleaved input the ring holds [..., L_old, R_old, L_new, R_new]
        // with ring_wpos_ pointing to the slot AFTER the newest written sample.
        // CAVA's stereo split:  in_r[n] = input_buf[n*2],  in_l[n] = input_buf[n*2+1]
        // which means input_buf[0]=R_newest, input_buf[1]=L_newest, [2]=R_prev, [3]=L_prev...
        // Our ring stores them in arrival order so newest is at (ring_wpos_-1).
        // Reversing the ring into input_buf recreates CAVA's expected layout exactly.
        {
            std::lock_guard<std::mutex> lk(mtx_);

            int fill = std::min(new_samples, input_buf_size_);

            // Shift existing samples right to make room at the front
            for (int n = input_buf_size_ - 1; n >= fill; --n)
                input_buf_[n] = input_buf_[n - fill];

            // Fill front: input_buf_[0] = newest, input_buf_[fill-1] = oldest of batch
            const size_t rsize = ring_.size();
            for (int n = 0; n < fill; ++n) {
                // n=0 → newest (ring_wpos_-1), n=1 → second newest, etc.
                const size_t idx = (ring_wpos_ + rsize - 1 - n) % rsize;
                input_buf_[n] = ring_[idx];
                if (ring_[idx] != 0.0) silence = false;
            }
        }
    } else {
        frame_skip_++;
    }

    // ── Fill bass and mid/treble raw buffers ──────────────────────────────────
    // (cavacore.c lines 332-348)
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

    // ── Apply Hann window ─────────────────────────────────────────────────────
    for (int i = 0; i < bass_buf_size_; ++i) {
        in_bass_l_[i] *= bass_win_[i];
        if (channels_ == 2) in_bass_r_[i] *= bass_win_[i];
    }
    for (int i = 0; i < fft_buf_size_; ++i) {
        in_mid_l_[i] *= mid_win_[i];
        if (channels_ == 2) in_mid_r_[i] *= mid_win_[i];
    }

    // ── Execute FFTs ──────────────────────────────────────────────────────────
    fftw_execute(plan_bass_l_);
    fftw_execute(plan_mid_l_);
    if (channels_ == 2) {
        fftw_execute(plan_bass_r_);
        fftw_execute(plan_mid_r_);
    }

    // ── Bin → bar magnitude (CAVA exact: sum hypot, then multiply EQ) ─────────
    // (cavacore.c lines 371-402)
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

        cava_out_[n]               = temp_l * eq_[n];
        cava_out_[n + num_bars]    = (channels_ == 2) ? temp_r * eq_[n] : cava_out_[n];
    }

    // ── Apply combined manual+auto sensitivity ────────────────────────────────
    double combined_sens = sens_ * (double)man_sens_.load();
    for (int n = 0; n < num_bars * channels_; ++n)
        cava_out_[n] *= combined_sens;

    // ── Smoothing (cavacore.c lines 413-448) ──────────────────────────────────
    double fr_mod      = 66.0 / std::max(framerate_, 1.0);
    double gravity_mod = pow(fr_mod, 2.5) * 2.0 / NOISE_REDUCTION;
    double integ_mod   = pow(fr_mod, 0.1);

    int overshoot = 0;

    for (int n = 0; n < num_bars * channels_; ++n) {
        // Fall
        if (cava_out_[n] < prev_out_[n] && NOISE_REDUCTION > 0.1) {
            cava_out_[n] = cava_peak_[n] * (1.0 - (cava_fall_[n] * cava_fall_[n] * gravity_mod));
            if (cava_out_[n] < 0.0) cava_out_[n] = 0.0;
            cava_fall_[n] += 0.028;
        } else {
            cava_peak_[n] = cava_out_[n];
            cava_fall_[n] = 0.0;
        }
        prev_out_[n] = cava_out_[n];

        // Integral smoothing
        cava_out_[n] = cava_mem_[n] * NOISE_REDUCTION / integ_mod + cava_out_[n];
        cava_mem_[n] = cava_out_[n];

        if (cava_out_[n] > 1.0) {
            overshoot    = 1;
            cava_out_[n] = 1.0;
        }
        if (cava_out_[n] < 0.0) cava_out_[n] = 0.0;
    }

    // ── Auto-sensitivity (cavacore.c lines 451-462) ───────────────────────────
    if (auto_sens_enabled_.load()) {
        if (overshoot) {
            sens_ *= (1.0 - 0.02 * fr_mod);
            sens_init_ = false;
        } else if (!silence) {
            sens_ *= (1.0 + 0.001 * fr_mod);
            if (sens_init_) sens_ *= (1.0 + 0.1 * fr_mod);
        }
        sens_ = std::max(0.01, std::min(sens_, 1000.0));
    } else {
        // Auto-sens off: reset multiplier to 1 so man_sens_ is applied as-is.
        sens_      = 1.0;
        sens_init_ = true;
    }

    // ── Monstercat: bell-curve neighbour propagation ──────────────────────────
    // Applied to L and R separately
    {
        std::vector<double> tmp_l(cava_out_.begin(), cava_out_.begin() + num_bars);
        monstercat(tmp_l, MONSTERCAT);
        for (int n = 0; n < num_bars; ++n) cava_out_[n] = tmp_l[n];

        if (channels_ == 2) {
            std::vector<double> tmp_r(cava_out_.begin() + num_bars, cava_out_.begin() + num_bars * 2);
            monstercat(tmp_r, MONSTERCAT);
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

// ── Monstercat bell-curve filter (cava.c lines 270-310) ──────────────────────
void FFTProcessor::monstercat(std::vector<double>& bars, double factor) const {
    const int n = (int)bars.size();
    for (int z = 0; z < n; ++z) {
        for (int m = z - 1; m >= 0; --m) {
            int de = z - m;
            bars[m] = std::max(bars[z] / pow(factor * 1.5, de), bars[m]);
        }
        for (int m = z + 1; m < n; ++m) {
            int de = m - z;
            bars[m] = std::max(bars[z] / pow(factor * 1.5, de), bars[m]);
        }
    }
}

float FFTProcessor::adjSens(float d) noexcept {
    float s = std::clamp(man_sens_.load() + d, SENS_MIN, SENS_MAX);
    man_sens_.store(s);
    return s;
}
