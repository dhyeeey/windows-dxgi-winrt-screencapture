#pragma once
/**
 * @file RtpStreamStrategy.hpp
 * @brief Frame access strategy that encodes captured frames to H.264 and streams via RTP/UDP.
 *
 * Architecture (Multi-threaded Producer-Consumer):
 *   Main Thread (Producer):  GPU->CPU copy + push raw pixels to queue  [FAST - doesn't block capture]
 *   Encoder Thread (Consumer): swscale + H.264 encode + RTP mux + UDP send  [HEAVY - runs independently]
 *
 * This decouples capture from encoding so the capture loop runs at full speed.
 *
 * Receiver can view the stream using:
 *   ffplay -protocol_whitelist file,rtp,udp -fflags nobuffer -flags low_delay -i stream.sdp
 *   vlc --network-caching=0 stream.sdp
 */

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <d3d11.h>
#include <wrl/client.h>
#include <optional>

#include "ThreadSafeQueue.hpp"

// Forward declarations for FFmpeg types
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct SwsContext;

using namespace Microsoft::WRL;

// =========================================================
// Configuration
// =========================================================

/**
 * @struct RtpStreamConfig
 * @brief Configuration for the RTP streaming strategy.
 */
struct RtpStreamConfig {
    std::string destIp      = "127.0.0.1";      ///< Destination IP address.
    int         destPort    = 5004;               ///< Destination port (should be even per RTP spec).
    int         fps         = 30;                 ///< Target framerate.
    int         bitrate     = 4'000'000;          ///< Target bitrate in bits/sec (4 Mbps default).
    std::string sdpFilePath = "stream.sdp";       ///< Path to write the SDP file for receivers.
    int         queueMaxSize = 10;                 ///< Max frames in the encode queue (drop if full to avoid latency buildup).
};

// Forward-declare FrameType and FrameData from the main strategies header
enum class FrameType;
struct FrameData;
class IFrameCaptureAccessStrategy;

// =========================================================
// Internal: Raw frame data for the encode queue
// =========================================================

/**
 * @struct RtpFrameData
 * @brief Lightweight container for raw pixel data pushed through the encode queue.
 *        Separate from FrameData to avoid carrying unnecessary fields.
 */
struct RtpFrameData {
    std::vector<uint8_t> pixels;    ///< Raw BGRA pixel buffer (deep copy from staging texture).
    UINT width   = 0;
    UINT height  = 0;
    UINT stride  = 0;               ///< Row pitch in bytes.
    int  frameIndex = 0;
    bool is_poison = false;          ///< Signals the encoder thread to exit.
};

// =========================================================
// RTP Stream Strategy (Multi-threaded)
// =========================================================

/**
 * @class RtpStreamStrategy
 * @brief Encodes frames to H.264 and streams over RTP/UDP in real-time.
 *
 * Producer-Consumer pattern (same as FrametoImageSavingMultiThreadedStrategy):
 *   - Main thread: copies pixels from GPU staging texture → pushes to ThreadSafeQueue (non-blocking)
 *   - Encoder thread: pops from queue → swscale → encode → RTP send
 *
 * This ensures the capture loop is never blocked by encoding latency.
 */
class RtpStreamStrategy : public IFrameCaptureAccessStrategy {
public:
    explicit RtpStreamStrategy(const RtpStreamConfig& config = {});
    ~RtpStreamStrategy();

    // IFrameCaptureAccessStrategy interface
    void Initialize(ID3D11Device* device, UINT width, UINT height) override;
    std::optional<FrameData> ProcessFrame(ID3D11DeviceContext* context, ID3D11Texture2D* capturedTexture, int frameIndex) override;
    void Shutdown() override;

private:
    // Encoder thread entry point
    void encoderLoop();

    // Internal initialization helpers
    bool initEncoder(UINT width, UINT height);
    bool initOutputContext();
    bool writeSdpFile();

    // Encoding pipeline (called from encoder thread)
    bool encodeAndSend(const uint8_t* bgraData, UINT stride);

    // Configuration
    RtpStreamConfig config_;

    // D3D11 staging texture for GPU → CPU copy
    ComPtr<ID3D11Texture2D> stagingTexture_;
    UINT width_  = 0;
    UINT height_ = 0;

    // Producer-Consumer queue + encoder thread
    ThreadSafeQueue<RtpFrameData> encodeQueue_;
    std::thread encoderThread_;
    std::atomic<int> queueDepth_{ 0 };   ///< Tracks queue size for drop-when-full logic.

    // FFmpeg encoding (owned by encoder thread)
    AVCodecContext*  codecCtx_    = nullptr;
    AVFormatContext* fmtCtx_     = nullptr;
    AVStream*        stream_     = nullptr;
    AVFrame*         yuvFrame_   = nullptr;
    AVPacket*        packet_     = nullptr;
    SwsContext*      swsCtx_     = nullptr;

    // State
    bool initialized_ = false;
    int64_t frameCount_ = 0;
    int64_t droppedFrames_ = 0;
};
