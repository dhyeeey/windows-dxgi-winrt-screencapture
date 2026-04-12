/**
 * @file RtpStreamStrategy.cpp
 * @brief Multi-threaded H.264 RTP streaming strategy using FFmpeg.
 *
 * Architecture:
 *   Main Thread (fast):     GPU->CPU copy → push to queue → return immediately
 *   Encoder Thread (heavy): pop from queue → swscale → encode → RTP send
 *
 * Encoder priority:
 *   1. h264_mf     (Media Foundation - uses GPU HW encoder via Windows)
 *   2. libx264     (Software encoder - fallback if available)
 *   3. mpeg4       (Last resort fallback - always available)
 */

#include "FrameCaptureAcessStrategies.hpp"
#include "RtpStreamStrategy.hpp"

// FFmpeg headers (C library - must use extern "C")
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

#include <iostream>
#include <fstream>
#include <sstream>

// =========================================================
// Constructor / Destructor
// =========================================================

RtpStreamStrategy::RtpStreamStrategy(const RtpStreamConfig& config)
    : config_(config) {
}

RtpStreamStrategy::~RtpStreamStrategy() {
    if (initialized_) {
        Shutdown();
    }
}

// =========================================================
// IFrameCaptureAccessStrategy: Initialize
// =========================================================

void RtpStreamStrategy::Initialize(ID3D11Device* device, UINT width, UINT height) {
    width_ = width;
    height_ = height;

    // 1. Create staging texture (GPU -> CPU copy target)
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &stagingTexture_);
    if (FAILED(hr)) {
        std::cerr << "[RTP] Failed to create staging texture." << std::endl;
        return;
    }

    // 2. Initialize FFmpeg encoder
    if (!initEncoder(width, height)) {
        std::cerr << "[RTP] Failed to initialize encoder." << std::endl;
        return;
    }

    // 3. Initialize RTP output context
    if (!initOutputContext()) {
        std::cerr << "[RTP] Failed to initialize RTP output." << std::endl;
        return;
    }

    // 4. Write SDP file for receivers
    if (!writeSdpFile()) {
        std::cerr << "[RTP] Warning: Failed to write SDP file." << std::endl;
    }

    initialized_ = true;

    // 5. Start the encoder thread (consumer)
    encoderThread_ = std::thread(&RtpStreamStrategy::encoderLoop, this);

    std::cout << "[RTP] Stream initialized (multi-threaded). Streaming to rtp://"
        << config_.destIp << ":" << config_.destPort << std::endl;
    std::cout << "[RTP] SDP file written to: " << config_.sdpFilePath << std::endl;
    std::cout << "[RTP] To view (low-latency):" << std::endl;
    std::cout << "[RTP]   ffplay -protocol_whitelist file,rtp,udp -fflags nobuffer -flags low_delay -i " 
              << config_.sdpFilePath << std::endl;
    std::cout << "[RTP]   vlc --network-caching=0 " << config_.sdpFilePath << std::endl;
}

// =========================================================
// IFrameCaptureAccessStrategy: ProcessFrame (PRODUCER - Main Thread)
// =========================================================
// This runs on the capture thread and must be FAST.
// It only does: GPU->CPU copy + push to queue. No encoding here.

std::optional<FrameData> RtpStreamStrategy::ProcessFrame(
    ID3D11DeviceContext* context,
    ID3D11Texture2D* capturedTexture,
    int frameIndex)
{
    if (!initialized_) return std::nullopt;

    // Drop frames if the encoder can't keep up (prevents latency buildup)
    if (queueDepth_.load() >= config_.queueMaxSize) {
        ++droppedFrames_;
        // Only log occasionally to avoid spamming
        if (droppedFrames_ % 30 == 1) {
            std::cout << "[RTP] Encoder can't keep up, dropped " << droppedFrames_ << " frames total" << std::endl;
        }
        return std::nullopt;
    }

    // 1. Copy GPU texture -> staging texture (CPU-readable)
    context->CopyResource(stagingTexture_.Get(), capturedTexture);

    // 2. Map staging texture to read pixels
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr = context->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    // 3. Deep copy pixels into a queue item (so GPU texture can be released immediately)
    RtpFrameData frameData;
    frameData.width = width_;
    frameData.height = height_;
    frameData.stride = static_cast<UINT>(map.RowPitch);
    frameData.frameIndex = frameIndex;
    frameData.pixels.resize(map.RowPitch * height_);
    memcpy(frameData.pixels.data(), map.pData, map.RowPitch * height_);

    context->Unmap(stagingTexture_.Get(), 0);

    // 4. Push to encoder queue (non-blocking for the capture loop)
    queueDepth_.fetch_add(1);
    encodeQueue_.push(std::move(frameData));

    return std::nullopt; // Frames are sent directly to network, no data returned
}

// =========================================================
// IFrameCaptureAccessStrategy: Shutdown
// =========================================================

void RtpStreamStrategy::Shutdown() {
    if (!initialized_) return;

    std::cout << "[RTP] Shutting down stream... (encoded " << frameCount_ 
              << " frames, dropped " << droppedFrames_ << ")" << std::endl;

    // Send poison pill to stop the encoder thread
    RtpFrameData poison;
    poison.is_poison = true;
    encodeQueue_.push(std::move(poison));

    // Wait for encoder thread to finish
    if (encoderThread_.joinable()) {
        encoderThread_.join();
    }

    // Flush the encoder (send remaining buffered frames)
    if (codecCtx_ && fmtCtx_) {
        avcodec_send_frame(codecCtx_, nullptr); // Signal flush

        while (true) {
            int ret = avcodec_receive_packet(codecCtx_, packet_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            av_packet_rescale_ts(packet_, codecCtx_->time_base, stream_->time_base);
            packet_->stream_index = stream_->index;
            av_interleaved_write_frame(fmtCtx_, packet_);
            av_packet_unref(packet_);
        }

        if (fmtCtx_->pb) {
            av_write_trailer(fmtCtx_);
        }
    }

    // Free FFmpeg resources
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (yuvFrame_) {
        av_frame_free(&yuvFrame_);
    }
    if (packet_) {
        av_packet_free(&packet_);
    }
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
    if (fmtCtx_) {
        if (fmtCtx_->pb && !(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmtCtx_->pb);
        }
        avformat_free_context(fmtCtx_);
        fmtCtx_ = nullptr;
    }

    initialized_ = false;
    std::cout << "[RTP] Stream shut down cleanly." << std::endl;
}

// =========================================================
// Encoder Thread (CONSUMER)
// =========================================================
// This runs on a background thread. Pops frames from the queue,
// converts, encodes, and sends. Never blocks the capture loop.

void RtpStreamStrategy::encoderLoop() {
    std::cout << "[RTP] Encoder thread started." << std::endl;

    while (true) {
        RtpFrameData frame;
        encodeQueue_.pop(frame);  // Blocks until data available
        queueDepth_.fetch_sub(1);

        if (frame.is_poison) {
            std::cout << "[RTP] Encoder thread received shutdown signal." << std::endl;
            break;
        }

        // Encode and send this frame
        encodeAndSend(frame.pixels.data(), frame.stride);
    }

    std::cout << "[RTP] Encoder thread exiting." << std::endl;
}

// =========================================================
// Private: Encoder Initialization
// =========================================================

bool RtpStreamStrategy::initEncoder(UINT width, UINT height) {
    // Try encoders in priority order
    const char* encoderNames[] = {
        "h264_mf",      // Media Foundation (Windows HW encoder - uses NVENC/QSV/AMF)
        "libx264",      // Software x264 (if compiled in)
        "mpeg4",        // Last resort fallback (always available)
    };

    const AVCodec* codec = nullptr;
    const char* chosenEncoder = nullptr;

    for (const char* name : encoderNames) {
        codec = avcodec_find_encoder_by_name(name);
        if (codec) {
            chosenEncoder = name;
            break;
        }
    }

    if (!codec) {
        std::cerr << "[RTP] No suitable encoder found!" << std::endl;
        return false;
    }

    std::cout << "[RTP] Using encoder: " << chosenEncoder << " (" << codec->long_name << ")" << std::endl;

    // Allocate encoder context
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        std::cerr << "[RTP] Failed to allocate codec context." << std::endl;
        return false;
    }

    // =============================================
    // Encoder configuration (low-latency tuned)
    // =============================================
    codecCtx_->width = width;
    codecCtx_->height = height;
    codecCtx_->time_base = { 1, config_.fps };
    codecCtx_->framerate = { config_.fps, 1 };
    codecCtx_->bit_rate = config_.bitrate;
    codecCtx_->rc_max_rate = config_.bitrate;        // Cap max bitrate = target (CBR-like)
    codecCtx_->rc_buffer_size = config_.bitrate / 2;  // Small buffer = lower latency
    codecCtx_->gop_size = config_.fps;                // Keyframe every 1 second (faster seeking/recovery)
    codecCtx_->max_b_frames = 0;                      // NO B-frames (critical for low latency)
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    codecCtx_->thread_count = 1;                       // Encoder-internal threads (for encoder thread only)

    // Encoder-specific low-latency settings
    if (strcmp(chosenEncoder, "h264_mf") == 0) {
        // Media Foundation: request low-latency mode
        av_opt_set(codecCtx_->priv_data, "rate_control", "cbr", 0);
        av_opt_set_int(codecCtx_->priv_data, "quality", 75, 0);
    }
    else if (strcmp(chosenEncoder, "libx264") == 0) {
        av_opt_set(codecCtx_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(codecCtx_->priv_data, "tune", "zerolatency", 0);
    }

    // Open the encoder
    int ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[RTP] Failed to open encoder '" << chosenEncoder << "': " << errbuf << std::endl;

        // If h264_mf failed, try mpeg4 as final fallback
        if (strcmp(chosenEncoder, "h264_mf") == 0) {
            std::cout << "[RTP] Falling back to mpeg4 encoder..." << std::endl;
            avcodec_free_context(&codecCtx_);

            codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
            if (!codec) return false;

            codecCtx_ = avcodec_alloc_context3(codec);
            if (!codecCtx_) return false;

            codecCtx_->width = width;
            codecCtx_->height = height;
            codecCtx_->time_base = { 1, config_.fps };
            codecCtx_->framerate = { config_.fps, 1 };
            codecCtx_->bit_rate = config_.bitrate;
            codecCtx_->gop_size = config_.fps;
            codecCtx_->max_b_frames = 0;
            codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
            codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            ret = avcodec_open2(codecCtx_, codec, nullptr);
            if (ret < 0) {
                av_strerror(ret, errbuf, sizeof(errbuf));
                std::cerr << "[RTP] Fallback encoder also failed: " << errbuf << std::endl;
                return false;
            }
            std::cout << "[RTP] Using fallback encoder: mpeg4" << std::endl;
        }
        else {
            return false;
        }
    }

    // Allocate YUV frame (reused by encoder thread)
    yuvFrame_ = av_frame_alloc();
    if (!yuvFrame_) return false;

    yuvFrame_->format = codecCtx_->pix_fmt;
    yuvFrame_->width = width;
    yuvFrame_->height = height;

    ret = av_frame_get_buffer(yuvFrame_, 32);
    if (ret < 0) {
        std::cerr << "[RTP] Failed to allocate YUV frame buffer." << std::endl;
        return false;
    }

    // Allocate packet (reused by encoder thread)
    packet_ = av_packet_alloc();
    if (!packet_) return false;

    // Create swscale context for BGRA -> YUV420P conversion
    swsCtx_ = sws_getContext(
        width, height, AV_PIX_FMT_BGRA,       // Source: BGRA (from D3D11)
        width, height, AV_PIX_FMT_YUV420P,    // Dest: YUV420P (for H.264)
        SWS_FAST_BILINEAR,                     // Fastest scaling algorithm
        nullptr, nullptr, nullptr
    );

    if (!swsCtx_) {
        std::cerr << "[RTP] Failed to create swscale context." << std::endl;
        return false;
    }

    return true;
}

// =========================================================
// Private: RTP Output Context
// =========================================================

bool RtpStreamStrategy::initOutputContext() {
    std::string url = "rtp://" + config_.destIp + ":" + std::to_string(config_.destPort);

    // Allocate output format context for RTP
    int ret = avformat_alloc_output_context2(&fmtCtx_, nullptr, "rtp", url.c_str());
    if (ret < 0 || !fmtCtx_) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[RTP] Failed to create RTP output context: " << errbuf << std::endl;
        return false;
    }

    // Set low-latency RTP options
    // max_delay: maximum muxing delay in microseconds (0 = send immediately)
    fmtCtx_->max_delay = 0;

    // Add a video stream
    stream_ = avformat_new_stream(fmtCtx_, nullptr);
    if (!stream_) {
        std::cerr << "[RTP] Failed to create output stream." << std::endl;
        return false;
    }

    // Copy codec parameters to the stream
    ret = avcodec_parameters_from_context(stream_->codecpar, codecCtx_);
    if (ret < 0) {
        std::cerr << "[RTP] Failed to copy codec parameters." << std::endl;
        return false;
    }

    stream_->time_base = codecCtx_->time_base;

    // Open the UDP output with low-buffer options
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "pkt_size", "1316", 0);    // Standard RTP packet size (fits in MTU)
    av_dict_set(&opts, "buffer_size", "0", 0);     // Minimize socket buffer

    ret = avio_open2(&fmtCtx_->pb, url.c_str(), AVIO_FLAG_WRITE, nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[RTP] Failed to open UDP output '" << url << "': " << errbuf << std::endl;
        return false;
    }

    // Write the RTP header
    AVDictionary* headerOpts = nullptr;
    ret = avformat_write_header(fmtCtx_, &headerOpts);
    av_dict_free(&headerOpts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[RTP] Failed to write RTP header: " << errbuf << std::endl;
        return false;
    }

    return true;
}

// =========================================================
// Private: SDP File Generation
// =========================================================

bool RtpStreamStrategy::writeSdpFile() {
    if (!fmtCtx_) return false;

    char sdpBuffer[4096] = { 0 };
    int ret = av_sdp_create(&fmtCtx_, 1, sdpBuffer, sizeof(sdpBuffer));
    if (ret < 0) {
        std::cerr << "[RTP] Failed to generate SDP." << std::endl;
        return false;
    }

    std::ofstream sdpFile(config_.sdpFilePath);
    if (!sdpFile.is_open()) {
        std::cerr << "[RTP] Failed to open SDP file for writing: " << config_.sdpFilePath << std::endl;
        return false;
    }

    sdpFile << sdpBuffer;
    sdpFile.close();

    std::cout << "[RTP] SDP content:" << std::endl;
    std::cout << "---" << std::endl;
    std::cout << sdpBuffer;
    std::cout << "---" << std::endl;

    return true;
}

// =========================================================
// Private: Encode & Send (runs on ENCODER THREAD)
// =========================================================

bool RtpStreamStrategy::encodeAndSend(const uint8_t* bgraData, UINT stride) {
    // Make the YUV frame writable
    int ret = av_frame_make_writable(yuvFrame_);
    if (ret < 0) return false;

    // 1. Convert BGRA -> YUV420P
    const uint8_t* srcSlice[1] = { bgraData };
    int srcStride[1] = { static_cast<int>(stride) };

    sws_scale(
        swsCtx_,
        srcSlice, srcStride,
        0, height_,
        yuvFrame_->data, yuvFrame_->linesize
    );

    // 2. Set presentation timestamp
    yuvFrame_->pts = frameCount_++;

    // 3. Send frame to encoder
    ret = avcodec_send_frame(codecCtx_, yuvFrame_);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN)) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "[RTP] Error sending frame to encoder: " << errbuf << std::endl;
            return false;
        }
    }

    // 4. Read all available encoded packets and write to RTP output
    while (true) {
        ret = avcodec_receive_packet(codecCtx_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "[RTP] Error receiving packet from encoder: " << errbuf << std::endl;
            return false;
        }

        // Rescale timestamps from encoder timebase to stream timebase
        av_packet_rescale_ts(packet_, codecCtx_->time_base, stream_->time_base);
        packet_->stream_index = stream_->index;

        // 5. Write RTP packet to UDP
        ret = av_interleaved_write_frame(fmtCtx_, packet_);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            // Don't spam on transient network errors
            av_packet_unref(packet_);
            return false;
        }

        av_packet_unref(packet_);
    }

    return true;
}
