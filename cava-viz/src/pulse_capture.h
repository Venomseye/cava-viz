#pragma once
#ifdef HAVE_PULSEAUDIO

#include "audio_capture.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

struct pa_simple;

class PulseAudioCapture final : public AudioCapture {
public:
    PulseAudioCapture();
    ~PulseAudioCapture() override;

    PulseAudioCapture(const PulseAudioCapture&)            = delete;
    PulseAudioCapture& operator=(const PulseAudioCapture&) = delete;

    bool init(const std::string& source      = "",
              int                sample_rate  = 44100,
              int                channels     = 1) override;
    bool start(AudioCallback cb)     override;
    void stop()                      override;
    bool hasFailed() const           override { return failed_.load(); }

    std::string backendName() const  override { return "PulseAudio"; }
    int         sampleRate()  const  override { return sample_rate_; }
    int         channels()    const  override { return channels_;    }

private:
    static constexpr int kChunkFrames = 512;

    pa_simple*         handle_  {nullptr};
    std::thread        thread_;
    std::atomic<bool>  running_ {false};
    std::atomic<bool>  failed_  {false};

    std::string source_;
    int         sample_rate_{44100};
    int         channels_   {1};

    void captureLoop(AudioCallback cb);
};

#endif // HAVE_PULSEAUDIO
