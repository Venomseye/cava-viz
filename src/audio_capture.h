#pragma once
#include <functional>
#include <string>
#include <vector>

/// Abstract audio capture backend.
/// Callbacks are invoked from a dedicated capture thread.
class AudioCapture {
public:
    using AudioCallback =
        std::function<void(const std::vector<float>& samples, int channels)>;

    virtual ~AudioCapture() = default;

    /// Store configuration (does not open device).
    virtual bool init(const std::string& source      = "",
                      int                sample_rate  = 44100,
                      int                channels     = 1) = 0;

    /// Open device and start capture thread.
    virtual bool start(AudioCallback cb) = 0;

    /// Stop capture and release device.  Safe to call multiple times.
    virtual void stop() = 0;

    /// Returns true if the capture thread exited due to an error
    /// (device disconnected, server died, etc.).  Checked by the main
    /// loop to trigger a reconnect.
    virtual bool hasFailed() const = 0;

    virtual std::string backendName() const = 0;
    virtual int         sampleRate()  const = 0;
    virtual int         channels()    const = 0;
};
