#pragma once

/**
 * @file DxgiScreenCapture.hpp
 * @brief Core implementation of high-performance screen capture using DXGI Desktop Duplication API.
 * * This file contains the low-level logic for interacting with Windows DXGI API.
 * It handles the complex tasks of:
 * - Managing the Desktop Duplication session.
 * - Processing "Dirty Rects" (optimizing updates by only redrawing changed pixels).
 * - Drawing the Mouse Cursor (hardware cursors need manual rendering).
 * - Handling device loss/reset scenarios.
 */

#include <iostream>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <map>
#include <array>
#include <d3dcompiler.h>
#include <algorithm>
#include <fstream>
#include <thread>
#include <chrono>

 // Library requirement for shader compilation (HLSL)
 // #pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace DxgiCapture {

    // ============================================================================
    // Exception Classes
    // ============================================================================

    /**
     * @brief Base exception for all capture-related errors.
     */
    class CaptureException : public std::runtime_error {
    public:
        explicit CaptureException(const std::string& message)
            : std::runtime_error(message) {}
    };

    /**
     * @brief Thrown when the D3D11 device is lost (e.g., GPU driver update or TDR).
     * The application should destroy the current device and re-initialize.
     */
    class DeviceRemovedException : public CaptureException {
    public:
        DeviceRemovedException()
            : CaptureException("D3D11 device was removed") {
        }
    };

    /**
     * @brief Thrown when the OS does not support Desktop Duplication (e.g., some fullscreen games).
     */
    class UnsupportedException : public CaptureException {
    public:
        UnsupportedException()
            : CaptureException("Desktop duplication not supported") {
        }
    };

    // ============================================================================
    // Enumerations
    // ============================================================================

    /**
     * @enum CaptureResult
     * @brief Result codes for capture operations.
     */
    enum class CaptureResult {
        Success,       ///< Frame captured successfully.
        Timeout,       ///< No new frame arrived within the timeout period (Screen is static).
        ExpectedError, ///< A recoverable error occurred (e.g., mode change).
        SizeChanged,   ///< Desktop resolution changed; reconfiguration needed.
        DeviceRemoved, ///< GPU device lost; re-initialization needed.
        Error          ///< Fatal or unknown error.
    };

    // ============================================================================
    // Structures
    // ============================================================================

    /**
     * @struct Vertex
     * @brief Vertex definition for drawing quads (used for Dirty Rects and Mouse Cursor).
     */
    struct Vertex {
        float position[3];  // x, y, z in Normalized Device Coordinates (NDC)
        float texCoord[2];  // u, v texture coordinates

        Vertex() : position{ 0, 0, 0 }, texCoord{ 0, 0 } {}
        Vertex(float x, float y, float z, float u, float v)
            : position{ x, y, z }, texCoord{ u, v } {
        }
    };

    /**
     * @struct DesktopBounds
     * @brief Stores physical monitor coordinates and resolution.
     */
    struct DesktopBounds {
        RECT coordinates; ///< Virtual screen coordinates (e.g., left can be negative for secondary monitors).
        UINT width;       ///< Physical width in pixels.
        UINT height;      ///< Physical height in pixels.

        DesktopBounds() : coordinates{ 0, 0, 0, 0 }, width(0), height(0) {}
    };

    // ============================================================================
    // PointerInfo Class
    // ============================================================================

    /**
     * @class PointerInfo
     * @brief Manages the state of the Mouse Cursor (Position, Shape, Visibility).
     * * Desktop Duplication returns the mouse cursor as a separate image (or monochrome mask)
     * which must be manually composited onto the frame.
     */
    class PointerInfo {
    public:
        PointerInfo();
        ~PointerInfo() = default;

        void reallocBuffer(UINT size);
        void reset();

        // Accessors for raw shape buffer
        const BYTE* getShapeBuffer() const { return shapeBuffer_.data(); }
        BYTE* getShapeBuffer() { return shapeBuffer_.data(); }
        UINT getBufferSize() const { return static_cast<UINT>(shapeBuffer_.size()); }

        // Accessors for DXGI shape info (width, height, type)
        const DXGI_OUTDUPL_POINTER_SHAPE_INFO& getShapeInfo() const { return shapeInfo_; }
        DXGI_OUTDUPL_POINTER_SHAPE_INFO& getShapeInfo() { return shapeInfo_; }

        // Position relative to the top-left of the monitor
        const POINT& getPosition() const { return position_; }
        POINT& getPosition() { return position_; }

        bool isVisible() const { return visible_; }
        void setVisible(bool visible) { visible_ = visible; }

        const LARGE_INTEGER& getLastTimestamp() const { return lastTimestamp_; }
        LARGE_INTEGER& getLastTimestamp() { return lastTimestamp_; }

    private:
        std::vector<BYTE> shapeBuffer_; ///< Raw pixel data for the cursor icon.
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo_;
        POINT position_;
        bool visible_;
        LARGE_INTEGER lastTimestamp_;
    };

    // ============================================================================
    // ShaderResources Class
    // ============================================================================

    /**
     * @class ShaderResources
     * @brief Wrapper for D3D11 rendering state (Shaders, Samplers, Blend State).
     * * Used to draw the Dirty Rects (optimizing frame updates) and the Mouse Cursor.
     */
    class ShaderResources {
    public:
        ShaderResources() = default;
        ~ShaderResources() = default;

        /**
         * @brief Compiles shaders and creates D3D state objects.
         */
        bool initialize(ID3D11Device* device);

        ID3D11VertexShader* getVertexShader() const { return vertexShader_.Get(); }
        ID3D11PixelShader* getPixelShader() const { return pixelShader_.Get(); }
        ID3D11InputLayout* getInputLayout() const { return inputLayout_.Get(); }
        ID3D11SamplerState* getSampler() const { return sampler_.Get(); }
        ID3D11RasterizerState* getRasterizer() const { return rasterizer_.Get(); }
        ID3D11BlendState* getBlendState() const { return blendState_.Get(); }

    private:
        // Internal helper methods for creating specific resources
        bool createVertexShader(ID3D11Device* device);
        bool createPixelShader(ID3D11Device* device);
        bool createSampler(ID3D11Device* device);
        bool createRasterizer(ID3D11Device* device);
        bool createBlendState(ID3D11Device* device); // Essential for alpha-blending cursor

        ComPtr<ID3D11VertexShader> vertexShader_;
        ComPtr<ID3D11PixelShader> pixelShader_;
        ComPtr<ID3D11InputLayout> inputLayout_;
        ComPtr<ID3D11SamplerState> sampler_;
        ComPtr<ID3D11RasterizerState> rasterizer_;
        ComPtr<ID3D11BlendState> blendState_;
    };

    // ============================================================================
    // FrameMetadata Class
    // ============================================================================

    /**
     * @class FrameMetadata
     * @brief Buffer for DXGI metadata (Dirty Rects and Move Rects).
     * * DXGI provides lists of rectangles that changed or moved.
     * We use this to avoid copying the entire screen every frame.
     */
    class FrameMetadata {
    public:
        FrameMetadata() = default;
        ~FrameMetadata() = default;

        void reallocBuffer(UINT size);
        BYTE* getBuffer() { return buffer_.data(); }
        UINT getBufferSize() const { return static_cast<UINT>(buffer_.size()); }

    private:
        std::vector<BYTE> buffer_;
    };

    // ============================================================================
    // DesktopDuplicator Class
    // ============================================================================

    /**
     * @class DesktopDuplicator
     * @brief Low-level worker class that interacts directly with IDXGIOutputDuplication.
     * * This class handles the actual "AcquireFrame" calls, processing move/dirty rects,
     * and drawing the cursor.
     */
    class DesktopDuplicator {
    public:
        DesktopDuplicator();
        ~DesktopDuplicator();

        // ---------------- Initialization ----------------

        /**
         * @brief Connects to the DXGI Output Duplication interface.
         */
        CaptureResult initialize(ID3D11Device* device, HMONITOR monitor);

        // ---------------- Capture ----------------

        /**
         * @brief Acquires the next frame from the OS.
         * @return Success if new frame, Timeout if no update, or Error.
         */
        CaptureResult capture();

        /**
         * @brief Copies the internal shared texture to the user's target texture.
         * Handles cropping if a cropBox is provided.
         */
        CaptureResult copyToTexture(ID3D11Device* targetDevice,
            ID3D11Texture2D* texture,
            const D3D11_BOX& cropBox);

        /**
         * @brief Renders the hardware mouse cursor onto the target frame.
         */
        bool drawMouse(ID3D11Device* device,
            ID3D11RenderTargetView* rtv,
            const D3D11_BOX& cropBox);

        // ---------------- Information ----------------
        void getSize(UINT& width, UINT& height) const;
        const DXGI_OUTDUPL_DESC& getOutputDesc() const { return outputDesc_; }

    private:
        // Internal Helpers
        CaptureResult initializeDuplication(HMONITOR monitor);
        bool createSharedTexture();

        // Frame Logic
        CaptureResult acquireFrame(ComPtr<ID3D11Texture2D>& texture,
            UINT& moveCount,
            UINT& dirtyCount,
            DXGI_OUTDUPL_FRAME_INFO& frameInfo,
            bool& timeout);

        CaptureResult getMouseInfo(const DXGI_OUTDUPL_FRAME_INFO& frameInfo);

        CaptureResult processFrame(ID3D11Texture2D* acquiredTexture,
            ID3D11Texture2D* sharedTexture,
            UINT moveCount,
            UINT dirtyCount,
            const DXGI_OUTDUPL_FRAME_INFO& frameInfo);

        // Optimization: Copy only moved or dirty regions
        CaptureResult copyMoveRects(ID3D11Texture2D* surface,
            DXGI_OUTDUPL_MOVE_RECT* moveBuffer,
            UINT moveCount);

        CaptureResult copyDirtyRects(ID3D11Texture2D* srcSurface,
            ID3D11Texture2D* dstSurface,
            RECT* dirtyBuffer,
            UINT dirtyCount);

        // Mouse Logic: Converts monochrome masks to RGBA textures
        bool processMonoMask(bool isMono,
            INT& ptrWidth, INT& ptrHeight,
            INT& ptrLeft, INT& ptrTop,
            std::vector<BYTE>& initBuffer,
            D3D11_BOX& box);

        bool updateMouseTexture();

        // Utility: Coordinate conversion helpers
        void setMoveRect(RECT& srcRect, RECT& dstRect,
            const DXGI_OUTDUPL_MOVE_RECT& moveRect,
            INT texWidth, INT texHeight);

        void setDirtyVert(std::array<Vertex, 6>& vertices,
            const RECT& dirty,
            const D3D11_TEXTURE2D_DESC& fullDesc,
            const D3D11_TEXTURE2D_DESC& thisDesc);

        // Error Handling
        static CaptureResult checkHResult(ID3D11Device* device,
            HRESULT hr,
            const std::vector<HRESULT>& expectedErrors = {});

        // ---------------- Resources ----------------
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;

        // This texture holds the accumulated desktop image (persists between frames)
        ComPtr<ID3D11Texture2D> sharedTexture_;
        ComPtr<IDXGIKeyedMutex> keyedMutex_; // Synchronization for shared resource

        ComPtr<ID3D11RenderTargetView> renderTargetView_;
        ComPtr<ID3D11Texture2D> moveTexture_;

        // Mouse Rendering Resources
        ComPtr<ID3D11Texture2D> mouseTexture_;
        ComPtr<ID3D11ShaderResourceView> mouseSRV_;
        D3D11_TEXTURE2D_DESC mouseTexDesc_;

        // DXGI Interface
        ComPtr<IDXGIOutputDuplication> duplication_;
        DXGI_OUTDUPL_DESC outputDesc_;

        // Sub-systems
        std::unique_ptr<ShaderResources> shaderResources_;
        std::unique_ptr<FrameMetadata> metadata_;
        std::vector<BYTE> vertexBuffer_;
        PointerInfo pointerInfo_;
    };

    // ============================================================================
    // CaptureDevice Class
    // ============================================================================

    /**
     * @class CaptureDevice
     * @brief High-level wrapper for a single Monitor Capture instance.
     * * This class manages the lifecycle of the capture session for a specific monitor.
     * It handles initialization, error recovery, and exposes the simple `capture()` API.
     */
    class CaptureDevice {
    public:
        CaptureDevice(ID3D11Device* device, HMONITOR monitor);
        ~CaptureDevice();

        // Disable copy (Resource heavy)
        CaptureDevice(const CaptureDevice&) = delete;
        CaptureDevice& operator=(const CaptureDevice&) = delete;

        // Enable move
        CaptureDevice(CaptureDevice&&) noexcept = default;
        CaptureDevice& operator=(CaptureDevice&&) noexcept = default;

        /**
         * @brief Initializes the underlying Duplicator. Call this before capturing.
         */
        CaptureResult prepare();

        /**
         * @brief Captures a single frame.
         * @param targetDevice The D3D device that owns the destination texture.
         * @param texture The destination texture to copy the screen into.
         * @param rtv (Optional) Render Target View used to draw the mouse cursor.
         * @param cropBox Region of the screen to capture.
         * @param drawMouse If true, composites the hardware cursor onto the image.
         */
        CaptureResult capture(ID3D11Device* targetDevice,
            ID3D11Texture2D* texture,
            ID3D11RenderTargetView* rtv,
            const D3D11_BOX& cropBox,
            bool drawMouse = true);

        // Information Getters
        bool getSize(UINT& width, UINT& height);
        const DesktopBounds& getDesktopBounds() const { return bounds_; }
        HMONITOR getMonitorHandle() const { return monitorHandle_; }
        int64_t getAdapterLuid() const { return adapterLuid_; }
        bool isPrepared() const { return prepared_; }

    private:
        bool initializeMonitorInfo();
        static int64_t luidToInt64(const LUID& luid);

        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<IDXGIOutput> output_;

        HMONITOR monitorHandle_;
        DesktopBounds bounds_;
        int64_t adapterLuid_;
        bool prepared_;

        std::unique_ptr<DesktopDuplicator> duplicator_;
        mutable std::recursive_mutex mutex_;
    };

    // ============================================================================
    // CaptureManager Class
    // ============================================================================

    /**
     * @class CaptureManager
     * @brief Singleton factory for creating and managing CaptureDevice instances.
     * * Ensures we don't accidentally create multiple capture sessions for the same monitor
     * on the same thread context.
     */
    class CaptureManager {
    public:
        static CaptureManager& getInstance();

        /**
         * @brief Creates or retrieves an existing CaptureDevice for the given monitor.
         */
        std::shared_ptr<CaptureDevice> createCaptureDevice(ID3D11Device* device,
            HMONITOR monitor);

        std::shared_ptr<CaptureDevice> getCaptureDevice(HMONITOR monitor);
        void removeCaptureDevice(HMONITOR monitor);
        void clear();

    private:
        CaptureManager() = default;
        ~CaptureManager() = default;

        CaptureManager(const CaptureManager&) = delete;
        CaptureManager& operator=(const CaptureManager&) = delete;

        std::mutex mutex_;
        std::map<HMONITOR, std::weak_ptr<CaptureDevice>> devices_;
    };

    // ============================================================================
    // Utility Functions
    // ============================================================================

    /**
     * @brief helper to find the IDXGIOutput corresponding to an HMONITOR handle.
     */
    HRESULT findOutputForMonitor(HMONITOR monitor,
        ComPtr<IDXGIAdapter1>& adapter,
        ComPtr<IDXGIOutput>& output);

    /**
     * @brief Enumerates all active monitors on the system.
     */
    std::vector<HMONITOR> enumerateMonitors();

} // namespace DxgiCapture