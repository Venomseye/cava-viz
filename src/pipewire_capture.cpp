#ifdef HAVE_PIPEWIRE
#include "pipewire_capture.h"

#include <cstdio>
#include <cstring>

#include <spa/param/audio/format.h>
#include <pipewire/keys.h>

PipeWireCapture::PipeWireCapture() { pw_init(nullptr, nullptr); }
PipeWireCapture::~PipeWireCapture() { stop(); pw_deinit(); }

bool PipeWireCapture::init(const std::string& src, int sr, int ch) {
    source_      = src;
    sample_rate_ = sr;
    channels_    = ch;
    failed_.store(false);
    return true;
}

bool PipeWireCapture::start(AudioCallback cb) {
    if (running_.load()) return false;
    failed_.store(false);
    callback_ = std::move(cb);

    loop_ = pw_main_loop_new(nullptr);
    if (!loop_) { failed_.store(true); return false; }

    ctx_ = pw_context_new(pw_main_loop_get_loop(loop_), nullptr, 0);
    if (!ctx_) {
        pw_main_loop_destroy(loop_); loop_=nullptr;
        failed_.store(true); return false;
    }

    core_ = pw_context_connect(ctx_, nullptr, 0);
    if (!core_) {
        pw_context_destroy(ctx_); ctx_=nullptr;
        pw_main_loop_destroy(loop_); loop_=nullptr;
        failed_.store(true); return false;
    }

    struct pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,     "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE,     "Music",
        PW_KEY_APP_NAME,       "cava-viz",
        nullptr);

    if (!source_.empty()) {
        // Connect to a specific named node / monitor source
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, source_.c_str());
    } else {
        // stream.capture.sink=true attaches this record stream to the DEFAULT
        // OUTPUT SINK (i.e. system audio loopback), not the microphone.
        // This is the PipeWire-native way to capture "what's playing".
        pw_properties_set(props, "stream.capture.sink", "true");
    }

    // Request low latency so capture chunks are small and responsive
    pw_properties_set(props, PW_KEY_NODE_LATENCY, "512/44100");

    stream_ = pw_stream_new(core_, "cava-viz-capture", props);
    if (!stream_) {
        pw_core_disconnect(core_);   core_=nullptr;
        pw_context_destroy(ctx_);    ctx_=nullptr;
        pw_main_loop_destroy(loop_); loop_=nullptr;
        failed_.store(true); return false;
    }

    std::memset(&stream_events_, 0, sizeof(stream_events_));
    stream_events_.version       = PW_VERSION_STREAM_EVENTS;
    stream_events_.process       = PipeWireCapture::onProcess;
    stream_events_.param_changed = PipeWireCapture::onParamChanged;
    stream_events_.state_changed = PipeWireCapture::onStreamState;

    std::memset(&stream_listener_, 0, sizeof(stream_listener_));
    pw_stream_add_listener(stream_, &stream_listener_, &stream_events_, this);

    uint8_t buf[1024];
    struct spa_pod_builder b;
    spa_pod_builder_init(&b, buf, sizeof(buf));

    struct spa_audio_info_raw info{};
    info.format   = SPA_AUDIO_FORMAT_F32;
    info.rate     = static_cast<uint32_t>(sample_rate_);
    info.channels = static_cast<uint32_t>(channels_);

    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    int rc = pw_stream_connect(
        stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
        static_cast<enum pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    if (rc < 0) {
        pw_stream_destroy(stream_);  stream_=nullptr;
        pw_core_disconnect(core_);   core_=nullptr;
        pw_context_destroy(ctx_);    ctx_=nullptr;
        pw_main_loop_destroy(loop_); loop_=nullptr;
        failed_.store(true); return false;
    }

    running_.store(true);
    thread_ = std::thread([this]{ pw_main_loop_run(loop_); });
    return true;
}

void PipeWireCapture::stop() {
    if (!running_.load() && !loop_) return;
    running_.store(false);
    if (loop_) pw_main_loop_quit(loop_);
    if (thread_.joinable()) thread_.join();
    if (stream_) { pw_stream_destroy(stream_);  stream_=nullptr; }
    if (core_)   { pw_core_disconnect(core_);   core_=nullptr;   }
    if (ctx_)    { pw_context_destroy(ctx_);    ctx_=nullptr;    }
    if (loop_)   { pw_main_loop_destroy(loop_); loop_=nullptr;   }
}

void PipeWireCapture::onProcess(void* ud) {
    auto* self = static_cast<PipeWireCapture*>(ud);
    if (!self->running_.load()) return;

    struct pw_buffer* pwbuf = pw_stream_dequeue_buffer(self->stream_);
    if (!pwbuf) return;

    struct spa_buffer* sbuf = pwbuf->buffer;

    // FIX: guard against empty buffer (n_datas==0 or null data pointer)
    if (sbuf->n_datas == 0 || !sbuf->datas[0].data ||
        !sbuf->datas[0].chunk) {
        pw_stream_queue_buffer(self->stream_, pwbuf);
        return;
    }

    // FIX: chunk->offset was ignored.  The SPA buffer spec requires that the
    // actual audio data starts at  data_ptr + chunk->offset, not at data_ptr.
    // Skipping offset can cause garbled or silent audio on drivers that use it
    // (e.g. some USB audio and Bluetooth paths).
    const uint32_t offset    = std::min(sbuf->datas[0].chunk->offset,
                                        sbuf->datas[0].maxsize);
    const uint32_t byte_size = sbuf->datas[0].chunk->size;
    if (byte_size == 0 || offset + byte_size > sbuf->datas[0].maxsize) {
        pw_stream_queue_buffer(self->stream_, pwbuf);
        return;
    }

    const auto* raw  = static_cast<const uint8_t*>(sbuf->datas[0].data);
    const auto* data = reinterpret_cast<const float*>(raw + offset);
    uint32_t n_samples = byte_size / sizeof(float);

    if (n_samples > 0) {
        std::vector<float> audio(data, data + n_samples);
        self->callback_(audio, self->channels_);
    }
    pw_stream_queue_buffer(self->stream_, pwbuf);
}

void PipeWireCapture::onParamChanged(void* ud, uint32_t id,
                                      const struct spa_pod* param) {
    (void)ud; (void)id; (void)param;
}

void PipeWireCapture::onStreamState(void* ud,
                                     enum pw_stream_state /*old*/,
                                     enum pw_stream_state ns,
                                     const char* error) {
    auto* self = static_cast<PipeWireCapture*>(ud);
    if (ns == PW_STREAM_STATE_ERROR ||
        (ns == PW_STREAM_STATE_UNCONNECTED && self->running_.load())) {
        (void)error;
        self->failed_.store(true);
        if (self->loop_) pw_main_loop_quit(self->loop_);
    }
}

#endif // HAVE_PIPEWIRE
