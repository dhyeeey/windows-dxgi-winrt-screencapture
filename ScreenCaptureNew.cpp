// ScreenCaptureNew.cpp : Defines the entry point for the application.

/**
 * @file ScreenCaptureNew.cpp
 * @brief Main entry point for the DXGI Screen Capture application.
 * * This application demonstrates high-performance screen capture on Windows using DirectX 11 and DXGI Desktop Duplication API.
 * It implements a Strategy Pattern to allow switching between different data handling modes:
 * 1. Disk Saving (Multi-threaded)
 * 2. CPU Access (Raw Pixels)
 * 3. GPU Direct (Texture Sharing)
 * * @author Dhyey
 */

#include "DxgiScreenCapture.hpp"
#include "CaptureStrategies.hpp"

 // Link required Windows libraries
 // we already linked all these libs in cmake file so no need to link them again here
 // 
//#pragma comment(lib, "d3d11.lib")
//#pragma comment(lib, "dxgi.lib")
//#pragma comment(lib, "user32.lib")
//#pragma comment(lib, "ole32.lib")

/**
 * @enum AppMode
 * @brief Defines the operational mode of the application.
 */
enum class AppMode {
    SaveFrametoImage,   ///< Saves frames to Disk in jpg/png/bmp formats (CPU, Async).
    CpuAccess,          ///< Returns raw pixels to Main thread (CPU, Sync).
    GpuDirect           ///< Returns D3D11 Texture pointer to Main thread (GPU, Zero-Copy).
};

// ==========================================
// CONFIGURATION: Switch Modes Here
// ==========================================
AppMode CURRENT_MODE = AppMode::CpuAccess;

int main() {
    // 1. Initialize COM
    // Required for WIC (Image Saving) and other Windows components.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return -1;

    // 2. Initialize Direct3D 11
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel;

    // Create hardware-accelerated device
    hr = D3D11CreateDevice(
            nullptr, 
            D3D_DRIVER_TYPE_HARDWARE, 
            nullptr, 
            0, 
            nullptr, 
            0, 
            D3D11_SDK_VERSION, 
            &device, 
            &featureLevel, 
            &context
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 Device." << std::endl;
        CoUninitialize();
        return -1;
    }

    // 3. Initialize Capture System
    POINT pt = { 0, 0 };
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

    auto& manager = DxgiCapture::CaptureManager::getInstance();
    auto capturer = manager.createCaptureDevice(device.Get(), hMonitor);

    if (capturer->prepare() != DxgiCapture::CaptureResult::Success) {
        std::cerr << "Failed to initialize capture device." << std::endl;
        CoUninitialize();
        return -1;
    }

    UINT width, height;
    capturer->getSize(width, height);
    std::cout << "Monitor Resolution: " << width << "x" << height << std::endl;

    // 4. Create Target Texture
    // This is the intermediate texture where the Desktop Duplication API copies the screen.
    // It must be a Render Target to allow drawing the Mouse Cursor on top.
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> targetTexture;
    device->CreateTexture2D(&texDesc, nullptr, &targetTexture);

    ComPtr<ID3D11RenderTargetView> rtv;
    device->CreateRenderTargetView(targetTexture.Get(), nullptr, &rtv);

    D3D11_BOX cropBox = { 0, 0, 0, width, height, 1 };

    // =========================================================
    // 5. Strategy Factory (Polymorphism)
    // =========================================================
    std::unique_ptr<ICaptureStrategy> strategy;

    switch (CURRENT_MODE) {
    case AppMode::SaveFrametoImage:
        std::cout << "[Mode] Threaded Disk Saver (Async)" << std::endl;
        strategy = std::make_unique<ThreadedSaveStrategy>();
        break;
    case AppMode::CpuAccess:
        std::cout << "[Mode] CPU Access (Raw Pixels)" << std::endl;
        strategy = std::make_unique<CpuAccessStrategy>();
        break;
    case AppMode::GpuDirect:
        std::cout << "[Mode] GPU Direct (Texture Pointer)" << std::endl;
        strategy = std::make_unique<GpuDirectStrategy>();
        break;
    }

    // Initialize the chosen strategy
    strategy->Initialize(device.Get(), width, height);

    // =========================================================
    // 6. Main Capture Loop
    // =========================================================
    std::cout << "Starting capture loop (20 frames)..." << std::endl;

    for (int i = 0; i < 20; ++i) {
        // A. Capture Frame from Windows
        auto result = capturer->capture(device.Get(), targetTexture.Get(), rtv.Get(), cropBox, true);

        if (result == DxgiCapture::CaptureResult::Success) {

            // B. Process Frame using selected Strategy
            // The strategy determines if we get data back or if it handles it internally.
            std::optional<FrameData> output = strategy->ProcessFrame(context.Get(), targetTexture.Get(), i);

            // C. Handle Return Data (if any)
            if (output.has_value()) {
                FrameData& frame = output.value();

                if (frame.type == FrameType::GpuTexture) {
                    // --- GPU PATH ---
                    // We have a valid D3D11 Texture pointer.
                    // This is ideal for passing to Video Encoders (NVENC) or Rendering.
                    D3D11_TEXTURE2D_DESC desc;
                    frame.d3dTexture->GetDesc(&desc);
                    std::cout << " [Main] Received GPU Texture: " << desc.Width << "x" << desc.Height
                        << " Addr: " << frame.d3dTexture.Get() << std::endl;
                }
                else if (frame.type == FrameType::CpuMemory) {
                    // --- CPU PATH ---
                    // We have a vector of raw pixels.
                    // This is ideal for OpenCV, socket transmission, or custom algorithms.
                    std::cout << " [Main] Received CPU Pixels: " << frame.pixels.size() << " bytes" << std::endl;
                }
            }
        }
        else if (result == DxgiCapture::CaptureResult::Timeout) {
            // Timeout is normal behavior for Desktop Duplication if nothing on screen changes.
        }
        else {
            std::cerr << "Capture Error: " << (int)result << std::endl;
            break;
        }

        // Cap framerate to ~30 FPS for this demo
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    // 7. Cleanup
    strategy->Shutdown(); // Stop threads and release resources
    CoUninitialize();
    return 0;
}