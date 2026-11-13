#ifndef RECORDER_H_
#define RECORDER_H_

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "common/worker.h"

template <typename T> class Recorder {
  public:
    using OnPacketedFunc = std::function<void(AVPacket *pkt)>;

    Recorder()
        : encoder(nullptr),
          st(nullptr) {}
    virtual ~Recorder() = default;

    virtual void OnBuffer(T buffer) = 0;
    virtual void OnStart() {};

    bool AddStream(AVFormatContext *output_fmt_ctx) {
        avcodec_free_context(&encoder);
        InitializeEncoderCtx(encoder);
        st = avformat_new_stream(output_fmt_ctx, encoder->codec);
        if (!st) {
            return false;
        }
        st->time_base = encoder->time_base;
        st->avg_frame_rate = encoder->framerate;
        st->r_frame_rate = encoder->framerate;
        avcodec_parameters_from_context(st->codecpar, encoder);

        return true;
    }

    void OnPacketed(OnPacketedFunc fn) { on_packeted = fn; }

    void Stop() {
        worker.reset();
        avcodec_free_context(&encoder);
    }

    void Start() {
        OnStart();
        worker = std::make_unique<Worker>("Recorder", [this]() {
            ConsumeBuffer();
        });
        worker->Run();
    }

  protected:
    OnPacketedFunc on_packeted;
    std::unique_ptr<Worker> worker;
    AVCodecContext *encoder;
    AVStream *st;

    virtual void InitializeEncoderCtx(AVCodecContext *&encoder) = 0;
    virtual bool ConsumeBuffer() = 0;
    void OnPacketed(AVPacket *pkt) {
        if (on_packeted) {
            on_packeted(pkt);
        }
    }
};

#endif // RECORDER_H_
