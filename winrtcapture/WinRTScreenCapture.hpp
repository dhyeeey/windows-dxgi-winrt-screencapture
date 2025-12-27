// WinRTScreenCapture.hpp
#pragma once

#ifndef WINAPI_PARTITION_DESKTOP
#define WINAPI_PARTITION_DESKTOP 1
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // Windows 10
#endif

// Standard Windows headers
#include <windows.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

// D3D11 headers
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

// WRL
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/wrappers/corewrappers.h>

// DWM
#include <dwmapi.h>

// Windows Runtime Base
#include <roapi.h>
#include <winstring.h>

// Try to include WinRT headers, fallback to manual definitions if not available
#if __has_include(<windows.graphics.capture.interop.h>)
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#define HAS_WINRT_HEADERS 1
#else
#define HAS_WINRT_HEADERS 0
// Forward declarations for manual definitions
typedef interface IGraphicsCaptureItem IGraphicsCaptureItem;
typedef interface IDirect3D11CaptureFramePool IDirect3D11CaptureFramePool;
typedef interface IGraphicsCaptureSession IGraphicsCaptureSession;
typedef interface IDirect3DDevice IDirect3DDevice;
typedef interface IDirect3DSurface IDirect3DSurface;
typedef interface IDirect3D11CaptureFrame IDirect3D11CaptureFrame;
#endif

#include <iostream>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <chrono>
#include <optional>
#include <map>
#include <algorithm>
#include <stdexcept>

//#pragma comment(lib, "d3d11.lib")
//#pragma comment(lib, "dxgi.lib")
//#pragma comment(lib, "dwmapi.lib")
//#pragma comment(lib, "windowsapp.lib")
//
//#pragma comment(lib, "d3d11.lib")
//#pragma comment(lib, "dxgi.lib")
//#pragma comment(lib, "dwmapi.lib")
//#pragma comment(lib, "windowsapp.lib")

using Microsoft::WRL::ComPtr;

// Namespace declarations - use conditionally based on header availability
#if HAS_WINRT_HEADERS
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Graphics;
using namespace ABI::Windows::Graphics::Capture;
using namespace ABI::Windows::Graphics::DirectX;
using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
#else
// Manual forward declarations when headers are not available
namespace ABI {
    namespace Windows {
        namespace Foundation {
            struct Size { FLOAT Width; FLOAT Height; };
            struct SizeInt32 { INT32 Width; INT32 Height; };
        }
        namespace Graphics {
            namespace DirectX {
                enum DirectXPixelFormat { DirectXPixelFormat_B8G8R8A8UIntNormalized = 87 };
                namespace Direct3D11 {
                    typedef interface IDirect3DDevice IDirect3DDevice;
                    typedef interface IDirect3DSurface IDirect3DSurface;
                }
            }
        }
        namespace Capture {
            typedef interface IGraphicsCaptureItem IGraphicsCaptureItem;
        }
    }
}

using ABI::Windows::Foundation::SizeInt32;
#endif

// Define IDirect3DDxgiInterfaceAccess if not available
#ifndef __IDirect3DDxgiInterfaceAccess_INTERFACE_DEFINED__
#define __IDirect3DDxgiInterfaceAccess_INTERFACE_DEFINED__

MIDL_INTERFACE("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")
IDirect3DDxgiInterfaceAccess : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetInterface(
        REFIID iid,
        void** p) = 0;
};

#endif // __IDirect3DDxgiInterfaceAccess_INTERFACE_DEFINED__

// Runtime class names
//static const WCHAR RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem[] =
//L"Windows.Graphics.Capture.GraphicsCaptureItem";
//static const WCHAR RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool[] =
//L"Windows.Graphics.Capture.Direct3D11CaptureFramePool";

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace WinRTCapture {

    // ============================================================================
    // Forward Declarations
    // ============================================================================

    class CaptureSession;
    class WinRTCaptureDevice;
    class WinRTCaptureManager;

    // ============================================================================
    // Enumerations
    // ============================================================================

    enum class CaptureResult {
        Success,
        Timeout,
        SizeChanged,
        ItemClosed,
        Flushing,
        Error
    };

    enum class CaptureMode {
        Monitor,
        Window,
        WindowClientAreaOnly
    };

    // ============================================================================
    // Structures
    // ============================================================================

    struct CaptureSize {
        UINT width;
        UINT height;

        CaptureSize() : width(0), height(0) {}
        CaptureSize(UINT w, UINT h) : width(w), height(h) {}

        bool operator==(const CaptureSize& other) const {
            return width == other.width && height == other.height;
        }

        bool operator!=(const CaptureSize& other) const {
            return !(*this == other);
        }
    };

    struct CaptureRect {
        UINT left;
        UINT top;
        UINT right;
        UINT bottom;

        CaptureRect() : left(0), top(0), right(0), bottom(0) {}
        CaptureRect(UINT l, UINT t, UINT r, UINT b)
            : left(l), top(t), right(r), bottom(b) {
        }

        UINT getWidth() const { return right - left; }
        UINT getHeight() const { return bottom - top; }
    };

    // ============================================================================
    // WinRT API Loader
    // ============================================================================

    class WinRTAPILoader {
    public:
        static WinRTAPILoader& getInstance();

        bool initialize();
        bool isLoaded() const { return loaded_; }

        // WinRT Functions
        HRESULT roInitialize(RO_INIT_TYPE initType);
        void roUninitialize();
        HRESULT windowsCreateString(PCNZWCH sourceString, UINT32 length, HSTRING* string);
        HRESULT windowsDeleteString(HSTRING string);
        HRESULT roGetActivationFactory(HSTRING activatableClassId, REFIID iid, void** factory);

        // D3D11 Functions
        HRESULT createDirect3D11DeviceFromDXGIDevice(IDXGIDevice* dxgiDevice,
            IInspectable** graphicsDevice);

        // User32 Functions
        DPI_AWARENESS_CONTEXT setThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context);

    private:
        WinRTAPILoader() = default;
        ~WinRTAPILoader();

        WinRTAPILoader(const WinRTAPILoader&) = delete;
        WinRTAPILoader& operator=(const WinRTAPILoader&) = delete;

        bool loadLibraries();
        bool loadD3D11Functions();
        bool loadCombaseFunctions();
        bool loadUser32Functions();

        HMODULE d3d11Module_ = nullptr;
        HMODULE combaseModule_ = nullptr;
        HMODULE user32Module_ = nullptr;

        bool loaded_ = false;
        std::once_flag initFlag_;

        // Function pointers
        using PFN_CreateDirect3D11DeviceFromDXGIDevice = HRESULT(WINAPI*)(
            IDXGIDevice*, IInspectable**);
        using PFN_RoInitialize = HRESULT(WINAPI*)(RO_INIT_TYPE);
        using PFN_RoUninitialize = void(WINAPI*)();
        using PFN_WindowsCreateString = HRESULT(WINAPI*)(PCNZWCH, UINT32, HSTRING*);
        using PFN_WindowsDeleteString = HRESULT(WINAPI*)(HSTRING);
        using PFN_RoGetActivationFactory = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
        using PFN_SetThreadDpiAwarenessContext = DPI_AWARENESS_CONTEXT(WINAPI*)(
            DPI_AWARENESS_CONTEXT);

        PFN_CreateDirect3D11DeviceFromDXGIDevice pfnCreateDirect3D11Device_ = nullptr;
        PFN_RoInitialize pfnRoInitialize_ = nullptr;
        PFN_RoUninitialize pfnRoUninitialize_ = nullptr;
        PFN_WindowsCreateString pfnWindowsCreateString_ = nullptr;
        PFN_WindowsDeleteString pfnWindowsDeleteString_ = nullptr;
        PFN_RoGetActivationFactory pfnRoGetActivationFactory_ = nullptr;
        PFN_SetThreadDpiAwarenessContext pfnSetThreadDpiAwarenessContext_ = nullptr;
    };

    // ============================================================================
    // Activation Factory Helper
    // ============================================================================

    template<typename InterfaceType>
    class ActivationFactory {
    public:
        static HRESULT getFactory(PCNZWCH runtimeClassId, InterfaceType** factory) {
            auto& loader = WinRTAPILoader::getInstance();
            if (!loader.isLoaded()) {
                return E_NOINTERFACE;
            }

            HSTRING classIdHstring;
            HRESULT hr = loader.windowsCreateString(runtimeClassId,
                static_cast<UINT32>(wcslen(runtimeClassId)),
                &classIdHstring);
            if (FAILED(hr)) return hr;

            hr = loader.roGetActivationFactory(classIdHstring, __uuidof(InterfaceType),
                reinterpret_cast<void**>(factory));

            loader.windowsDeleteString(classIdHstring);
            return hr;
        }
    };

    // ============================================================================
    // Capture Session Internal
    // ============================================================================

    class CaptureSessionInternal {
    public:
        CaptureSessionInternal() = default;
        ~CaptureSessionInternal();

        HRESULT initialize(ID3D11Device* device,
            HMONITOR monitor,
            HWND window,
            const CaptureSize& size);

        HRESULT startCapture(bool showBorder);
        void stopCapture();

        bool isClosed() const { return closed_; }
        void setClosed(bool closed) { closed_ = closed; }

        HRESULT tryGetNextFrame(ComPtr<IDirect3D11CaptureFrame>& frame);
        HRESULT recreateFramePool(const SizeInt32& size);

        void setCursorCaptureEnabled(bool enabled);
        void setBorderRequired(bool required);

        const SizeInt32& getPoolSize() const { return poolSize_; }
        void setPoolSize(const SizeInt32& size) { poolSize_ = size; }

    private:
        void cleanup();

        ComPtr<IDirect3DDevice> d3dDevice_;
        ComPtr<IGraphicsCaptureItem> captureItem_;
        ComPtr<IDirect3D11CaptureFramePool> framePool_;
        ComPtr<IGraphicsCaptureSession> captureSession_;

        SizeInt32 poolSize_{};
        std::atomic<bool> closed_{ false };
    };

    // ============================================================================
    // Window Event Monitor
    // ============================================================================

    class WindowEventMonitor {
    public:
        static WindowEventMonitor& getInstance();

        void registerCapture(WinRTCaptureDevice* device, HWND window);
        void unregisterCapture(WinRTCaptureDevice* device);

        void start();
        void stop();

    private:
        WindowEventMonitor() = default;
        ~WindowEventMonitor();

        static void CALLBACK eventHookProc(HWINEVENTHOOK hook, DWORD event,
            HWND hwnd, LONG idObject,
            LONG idChild, DWORD eventThread,
            DWORD eventTime);

        void onWindowDestroyed(HWND window);

        std::mutex mutex_;
        std::map<HWND, WinRTCaptureDevice*> windowMap_;
        HWINEVENTHOOK eventHook_ = nullptr;
        std::atomic<bool> started_{ false };
    };

    // ============================================================================
    // WinRT Capture Device
    // ============================================================================

    class WinRTCaptureDevice : public std::enable_shared_from_this<WinRTCaptureDevice> {
    public:
        WinRTCaptureDevice(ID3D11Device* device,
            HMONITOR monitor = nullptr,
            HWND window = nullptr,
            bool clientOnly = false);
        ~WinRTCaptureDevice();

        // Disable copy
        WinRTCaptureDevice(const WinRTCaptureDevice&) = delete;
        WinRTCaptureDevice& operator=(const WinRTCaptureDevice&) = delete;

        // Initialization
        CaptureResult prepare();
        void shutdown();

        // Capture operations
        CaptureResult capture(ID3D11Texture2D* texture,
            const D3D11_BOX& cropBox,
            bool drawMouse = true);

        // Configuration
        void setShowBorder(bool show);
        void flush();
        void stopFlush();

        // Information
        CaptureSize getCaptureSize() const;
        CaptureMode getCaptureMode() const;
        bool isPrepared() const { return prepared_; }
        bool isClosed() const;

        // Internal notification
        void notifyWindowClosed();

    private:
        void workerThreadFunc();
        void configureCapture();

        CaptureResult waitForFrame(ComPtr<IDirect3D11CaptureFrame>& frame,
            std::chrono::milliseconds timeout);

        CaptureResult processFrame(const ComPtr<IDirect3D11CaptureFrame>& frame,
            ID3D11Texture2D* texture,
            D3D11_BOX cropBox);

        std::optional<CaptureRect> calculateClientRect();

        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;

        HMONITOR monitorHandle_;
        HWND windowHandle_;
        bool clientOnly_;
        CaptureMode captureMode_;

        // Session
        std::unique_ptr<CaptureSessionInternal> session_;

        // Size tracking
        CaptureSize poolSize_;
        CaptureSize textureSize_;
        CaptureSize captureSize_;

        // Worker thread
        std::thread workerThread_;
        std::atomic<bool> workerRunning_{ false };

        // Synchronization
        mutable std::recursive_mutex mutex_;
        std::condition_variable_any condVar_;
        std::atomic<bool> flushing_{ false };
        std::atomic<bool> prepared_{ false };
        std::atomic<bool> showBorder_{ false };
        std::atomic<bool> showMouse_{ false };

        // Timing
        LARGE_INTEGER performanceFrequency_{};
    };

    // ============================================================================
    // WinRT Capture Manager
    // ============================================================================

    class WinRTCaptureManager {
    public:
        static WinRTCaptureManager& getInstance();

        std::shared_ptr<WinRTCaptureDevice> createMonitorCapture(
            ID3D11Device* device, HMONITOR monitor);

        std::shared_ptr<WinRTCaptureDevice> createWindowCapture(
            ID3D11Device* device, HWND window, bool clientOnly = false);

        void removeDevice(const std::shared_ptr<WinRTCaptureDevice>& device);
        void clear();

    private:
        WinRTCaptureManager() = default;
        ~WinRTCaptureManager() = default;

        WinRTCaptureManager(const WinRTCaptureManager&) = delete;
        WinRTCaptureManager& operator=(const WinRTCaptureManager&) = delete;

        std::mutex mutex_;
        std::vector<std::weak_ptr<WinRTCaptureDevice>> devices_;
    };

    // ============================================================================
    // Utility Functions
    // ============================================================================

    bool isWindowValid(HWND window);
    std::vector<HMONITOR> enumerateMonitors();

} // namespace WinRTCapture
