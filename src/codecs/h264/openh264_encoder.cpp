#include "codecs/h264/openh264_encoder.h"

#include <algorithm>

#include "common/logging.h"

std::unique_ptr<Openh264Encoder> Openh264Encoder::Create(EncoderConfig config) {
    auto ptr = std::make_unique<Openh264Encoder>(config);
    if (!ptr->Init()) {
        return nullptr;
    }
    return ptr;
}

Openh264Encoder::Openh264Encoder(EncoderConfig config)
    : config_(config),
      max_bitrate_(config.max_bitrate > 0 ? config.max_bitrate : UNSPECIFIED_BIT_RATE),
      encoder_(nullptr) {}

Openh264Encoder::~Openh264Encoder() {
    encoder_->Uninitialize();
    WelsDestroySVCEncoder(encoder_);
    DEBUG_PRINT("sw h264 encode was released!\n");
}

bool Openh264Encoder::Init() {
    int rv = WelsCreateSVCEncoder(&encoder_);
    if (rv != 0) {
        ERROR_PRINT("Failed to create OpenH264 encoder.");
        return false;
    }

    SEncParamExt encoder_param;
    encoder_->GetDefaultParams(&encoder_param);
    encoder_param.iUsageType = CAMERA_VIDEO_REAL_TIME;
    encoder_param.iTemporalLayerNum = config_.temporal_layers;
    if (config_.temporal_layers > 1) {
        // One reference buffer per lower temporal layer.
        encoder_param.iNumRefFrame = config_.temporal_layers - 1;
    }
    encoder_param.uiIntraPeriod = config_.keyframe_interval;
    encoder_param.uiMaxNalSize = 0;
    encoder_param.iRCMode =
        (config_.rc_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) ? RC_QUALITY_MODE : RC_BITRATE_MODE;
    encoder_param.bEnableFrameSkip = config_.frame_dropping;
    encoder_param.fMaxFrameRate = config_.fps;
    encoder_param.iTargetBitrate = config_.bitrate;
    encoder_param.iMaxBitrate = max_bitrate_;

    encoder_param.iMultipleThreadIdc = config_.thread_count;
    encoder_param.iComplexityMode = LOW_COMPLEXITY;
    encoder_param.iEntropyCodingModeFlag = 0;

    encoder_param.iSpatialLayerNum = 1;
    SSpatialLayerConfig *spartialLayerConfiguration = &encoder_param.sSpatialLayers[0];
    spartialLayerConfiguration->sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;
    spartialLayerConfiguration->sSliceArgument.uiSliceNum = config_.thread_count;
    spartialLayerConfiguration->uiProfileIdc = PRO_BASELINE;
    encoder_param.iPicWidth = spartialLayerConfiguration->iVideoWidth = config_.width;
    encoder_param.iPicHeight = spartialLayerConfiguration->iVideoHeight = config_.height;
    encoder_param.fMaxFrameRate = spartialLayerConfiguration->fFrameRate = config_.fps;
    encoder_param.iTargetBitrate = spartialLayerConfiguration->iSpatialBitrate = config_.bitrate;
    encoder_param.iMaxBitrate = spartialLayerConfiguration->iMaxSpatialBitrate = max_bitrate_;

    rv = encoder_->InitializeExt(&encoder_param);
    if (rv != 0) {
        ERROR_PRINT("Failed to initialize OpenH264 encoder.");
        return false;
    }
    return true;
}

void Openh264Encoder::ForceIntraFrame() {
    if (encoder_) {
        encoder_->ForceIntraFrame(true);
    }
}

void Openh264Encoder::SetRates(int bitrate_bps, float fps) {
    if (max_bitrate_ != UNSPECIFIED_BIT_RATE) {
        bitrate_bps = std::min(bitrate_bps, max_bitrate_);
    }
    config_.bitrate = bitrate_bps;
    config_.fps = fps;

    if (!encoder_) {
        return;
    }

    SBitrateInfo target = {};
    target.iLayer = SPATIAL_LAYER_ALL;
    target.iBitrate = bitrate_bps;
    encoder_->SetOption(ENCODER_OPTION_BITRATE, &target);

    float frame_rate = fps;
    encoder_->SetOption(ENCODER_OPTION_FRAME_RATE, &frame_rate);
}

bool Openh264Encoder::Encode(webrtc::scoped_refptr<webrtc::I420BufferInterface> frame_buffer,
                             SFrameBSInfo *info) {
    src_pic_ = {0};
    src_pic_.iPicWidth = config_.width;
    src_pic_.iPicHeight = config_.height;
    src_pic_.iColorFormat = videoFormatI420;
    src_pic_.iStride[0] = frame_buffer->StrideY();
    src_pic_.iStride[1] = frame_buffer->StrideU();
    src_pic_.iStride[2] = frame_buffer->StrideV();
    src_pic_.pData[0] = const_cast<uint8_t *>(frame_buffer->DataY());
    src_pic_.pData[1] = const_cast<uint8_t *>(frame_buffer->DataU());
    src_pic_.pData[2] = const_cast<uint8_t *>(frame_buffer->DataV());

    memset(info, 0, sizeof(SFrameBSInfo));
    int rv = encoder_->EncodeFrame(&src_pic_, info);

    return rv == cmResultSuccess && info->eFrameType != videoFrameTypeSkip;
}

bool Openh264Encoder::Encode(webrtc::scoped_refptr<webrtc::I420BufferInterface> frame_buffer,
                             std::function<void(uint8_t *, int, bool)> on_capture) {
    SFrameBSInfo info;
    if (!Encode(frame_buffer, &info)) {
        return false;
    }

    encoded_buf_.resize(BitstreamSize(info));
    CopyBitstream(info, encoded_buf_.data());

    bool is_keyframe = (info.eFrameType == videoFrameTypeIDR);
    on_capture(encoded_buf_.data(), encoded_buf_.size(), is_keyframe);

    return true;
}

int Openh264Encoder::BitstreamSize(const SFrameBSInfo &info) {
    int size = 0;
    for (int i = 0; i < info.iLayerNum; i++) {
        const SLayerBSInfo *layer = &info.sLayerInfo[i];
        for (int nal = 0; nal < layer->iNalCount; ++nal) {
            size += layer->pNalLengthInByte[nal];
        }
    }
    return size;
}

void Openh264Encoder::CopyBitstream(const SFrameBSInfo &info, uint8_t *dst) {
    for (int i = 0; i < info.iLayerNum; i++) {
        const SLayerBSInfo *layer = &info.sLayerInfo[i];
        int layer_len = 0;
        for (int nal = 0; nal < layer->iNalCount; ++nal) {
            layer_len += layer->pNalLengthInByte[nal];
        }

        memcpy(dst, layer->pBsBuf, layer_len);
        dst += layer_len;
    }
}
