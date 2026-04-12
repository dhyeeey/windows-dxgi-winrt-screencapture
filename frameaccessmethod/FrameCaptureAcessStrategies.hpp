#pragma once
/**
 * @file FrameCaptureAccessStrategies.hpp
 */

#include <thread>
#include <optional>
#include "ImageSaver.hpp"
#include "ThreadSafeQueue.hpp"

using namespace Microsoft::WRL;

/**
 * @enum FrameType
 * @brief Identifies the type of data contained in a FrameData structure.
 */
enum class FrameType {
    Empty,
    SaveFrametoImage,   ///< Saves frames to Disk in jpg/png/bmp formats (CPU, Async).
    CpuAccess,          ///< Returns raw pixels to Main thread (CPU, Sync).
    GpuDirect,          ///< Returns D3D11 Texture pointer to Main thread (GPU, Zero-Copy).
    RtpStream           ///< Encodes to H.264 and streams via RTP/UDP (Network, Real-time).
};


// =========================================================
// 1. Unified Frame Data Structure
// =========================================================

/**
 * @struct FrameData
 * @brief A container that holds either raw CPU pixels OR a GPU texture pointer.
 */
struct FrameData {
    FrameType type = FrameType::Empty;

    // --- CPU Data (Valid if type == CpuMemory) ---
    std::vector<uint8_t> pixels; ///< Raw BGRA pixel buffer.
    UINT width = 0;              ///< Width of the frame.
    UINT height = 0;             ///< Height of the frame.
    UINT stride = 0;             ///< Bytes per row (Pitch).

    // --- GPU Data (Valid if type == GpuTexture) ---
    ComPtr<ID3D11Texture2D> d3dTexture; ///< Pointer to the texture in VRAM.

    // --- Metadata ---
    std::wstring filename;       ///< Target filename (used by Saver strategy).
    bool is_poison = false;      ///< Internal flag to signal thread termination.
};

// =========================================================
// 2. The Interface
// =========================================================

/**
 * @class IFrameCaptureAccessStrategy
 * @brief Abstract Interface for frame processing logic.
 * * Allows the main application to switch between different processing modes
 * (Disk Saving, CPU access, GPU access) without changing the capture loop.
 */
class IFrameCaptureAccessStrategy {
public:
    virtual ~IFrameCaptureAccessStrategy() = default;

    /**
     * @brief Allocates resources required for the strategy (Textures, Threads, etc.)
     */
    virtual void Initialize(ID3D11Device* device, UINT width, UINT height) = 0;

    /**
     * @brief Processes a captured frame.
     * @return std::optional<FrameData> containing the result (Pixel data, Texture ptr, or nullopt).
     */
    virtual std::optional<FrameData> ProcessFrame(ID3D11DeviceContext* context, ID3D11Texture2D* capturedTexture, int frameIndex) = 0;

    /**
     * @brief Cleans up resources (joins threads, releases memory).
     */
    virtual void Shutdown() = 0;
};

// =========================================================
// 3. Strategy A: GPU Direct (High Performance)
// =========================================================

/**
 * @class GpuDirectStrategy
 * @brief Keeps data on the GPU. Fastest possible performance.
 * * * Use Case: Video Encoding (NVENC/AMF), Rendering into a game engine, or ML inference on GPU.
 * * Mechanism: Performs a GPU-to-GPU copy. No system RAM bandwidth is used.
 */
class GpuDirectStrategy : public IFrameCaptureAccessStrategy {
public:
    void Initialize(ID3D11Device* device, UINT width, UINT height) override {
        device_ = device;
        width_ = width;
        height_ = height;

        // Prepare description for the output texture
        textureDesc_.Width = width;
        textureDesc_.Height = height;
        textureDesc_.MipLevels = 1;
        textureDesc_.ArraySize = 1;
        textureDesc_.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDesc_.SampleDesc.Count = 1;
        textureDesc_.Usage = D3D11_USAGE_DEFAULT;
        textureDesc_.BindFlags = D3D11_BIND_SHADER_RESOURCE; // Allows reading in shaders
        textureDesc_.CPUAccessFlags = 0; // No CPU access!
        textureDesc_.MiscFlags = 0;
    }

    std::optional<FrameData> ProcessFrame(ID3D11DeviceContext* context, ID3D11Texture2D* capturedTexture, int frameIndex) override {
        // We must create a new texture for every frame we return.
        // If we reused a single texture, the Capture Loop would overwrite the data 
        // while the consumer (e.g., Encoder) was still reading it.
        ComPtr<ID3D11Texture2D> outputTexture;

        HRESULT hr = device_->CreateTexture2D(&textureDesc_, nullptr, &outputTexture);
        if (FAILED(hr)) return std::nullopt;

        // Extremely fast GPU-to-GPU copy
        context->CopyResource(outputTexture.Get(), capturedTexture);

        FrameData data;
        data.type = FrameType::GpuDirect;
        data.d3dTexture = outputTexture; // Move ownership to caller
        data.width = width_;
        data.height = height_;

        return data;
    }

    void Shutdown() override {}

private:
    ComPtr<ID3D11Device> device_;
    D3D11_TEXTURE2D_DESC textureDesc_ = {};
    UINT width_ = 0, height_ = 0;
};

// =========================================================
// 4. Strategy B: CPU Access (Standard)
// =========================================================

/**
 * @class CpuAccessStrategy
 * @brief Copies frame data to System RAM for CPU processing.
 * * * Use Case: OpenCV processing, saving to custom file formats, sending via Network Sockets.
 * * Mechanism: Copies GPU Texture -> Staging Texture -> Maps Memory -> Copies to std::vector.
 */
class CpuAccessStrategy : public IFrameCaptureAccessStrategy {
public:
    void Initialize(ID3D11Device* device, UINT width, UINT height) override {
        width_ = width;
        height_ = height;

        // Staging texture allows CPU READ access
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        device->CreateTexture2D(&desc, nullptr, &stagingTexture_);
    }

    std::optional<FrameData> ProcessFrame(ID3D11DeviceContext* context, ID3D11Texture2D* capturedTexture, int frameIndex) override {
        // 1. Copy GPU (Default) -> GPU (Staging)
        context->CopyResource(stagingTexture_.Get(), capturedTexture);

        // 2. Map memory for reading
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(context->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &map))) {
            FrameData data;
            data.type = FrameType::CpuAccess;
            data.width = width_;
            data.height = height_;
            data.stride = map.RowPitch;

            // Deep copy pixels to vector
            data.pixels.resize(map.RowPitch * height_);
            memcpy(data.pixels.data(), map.pData, map.RowPitch * height_);

            context->Unmap(stagingTexture_.Get(), 0);
            return data;
        }
        return std::nullopt;
    }

    void Shutdown() override {}

private:
    ComPtr<ID3D11Texture2D> stagingTexture_;
    UINT width_ = 0, height_ = 0;
};

// =========================================================
// 5. Strategy C: Threaded Saving
// =========================================================

/**
 * @class ThreadedSaveStrategy
 * @brief Saves frames to disk using a background thread (Producer-Consumer).
 * * * Use Case: Creating screenshots or image sequences without blocking the capture loop.
 * * Mechanism: Main thread copies data to RAM and pushes to a Queue. Background thread pops and saves.
 */
class FrametoImageSavingMultiThreadedStrategy : public IFrameCaptureAccessStrategy {
public:
    void Initialize(ID3D11Device* device, UINT width, UINT height) override {
        width_ = width;
        height_ = height;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        device->CreateTexture2D(&desc, nullptr, &stagingTexture_);

        // Start background saver thread
        saverThread_ = std::thread(&FrametoImageSavingMultiThreadedStrategy::SaverLoop, this);
    }

    std::optional<FrameData> ProcessFrame(ID3D11DeviceContext* context, ID3D11Texture2D* capturedTexture, int frameIndex) override {
        // Optimization: Only save every 2nd frame
        if (frameIndex % 2 != 0) return std::nullopt;

        context->CopyResource(stagingTexture_.Get(), capturedTexture);

        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(context->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &map))) {
            FrameData data;
            data.type = FrameType::CpuAccess;
            data.width = width_;
            data.height = height_;
            data.stride = map.RowPitch;
            data.filename = L"capture_" + std::to_wstring(frameIndex) + L".jpg";

            data.pixels.resize(map.RowPitch * height_);
            memcpy(data.pixels.data(), map.pData, map.RowPitch * height_);

            context->Unmap(stagingTexture_.Get(), 0);

            // Push to queue and return immediately (Non-blocking)
            queue_.push(data);
        }
        return std::nullopt;
    }

    void Shutdown() override {
        // Send poison pill to stop the thread
        FrameData poison; poison.is_poison = true;
        queue_.push(poison);

        if (saverThread_.joinable()) saverThread_.join();
    }

private:
    void SaverLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ImageSaver saver;
        while (true) {
            FrameData frame;
            queue_.pop(frame); // Blocks until data exists

            if (frame.is_poison) break;

            if (frame.type == FrameType::CpuAccess) {
                saver.saveRaw(frame.pixels, frame.width, frame.height, frame.stride, frame.filename);
            }
        }
        CoUninitialize();
    }

    ComPtr<ID3D11Texture2D> stagingTexture_;
    std::thread saverThread_;
    ThreadSafeQueue<FrameData> queue_;
    UINT width_ = 0, height_ = 0;
};