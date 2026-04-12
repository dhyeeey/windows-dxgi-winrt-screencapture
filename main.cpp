#include "ScreenCaptureFactory.hpp"
#include "FrameCaptureAcessStrategies.hpp"   
#include "RtpStreamStrategy.hpp"

#include <csignal>

 // ==========================================
 // CONFIGURATION: Switch Modes Here
 // ==========================================

 // CaptureMethod::DXGI 
 // CaptureMethod::WinRT
CaptureMethod CAPTURE_METHOD = CaptureMethod::WinRT;

// FrameType::GpuDirect
// FrameType::CpuAccess
// FrameType::SaveFrametoImage
// FrameType::RtpStream          <-- NEW: Real-time H.264 streaming over RTP
FrameType FRAME_TYPE = FrameType::RtpStream;

using ull = unsigned long long;

// ==========================================
// Ctrl+C Signal Handler for clean shutdown
// ==========================================
static volatile bool g_running = true;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        std::cout << "\n[Main] Shutdown signal received. Stopping capture..." << std::endl;
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

int main() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM." << std::endl;
        return -1;
    }

    // Register Ctrl+C handler
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel;

    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    hr = D3D11CreateDevice(
        nullptr,                  
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        nullptr, 0,
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

    std::cout << "Initializing Capture Engine..." << std::endl;

    POINT pt = { 0, 0 };
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

    auto captureSession = ScreenCaptureFactory::Create(CAPTURE_METHOD);

    if (!captureSession || !captureSession->Initialize(device.Get(), hMonitor)) {
        std::cerr << "Failed to initialize capture session." << std::endl;
        CoUninitialize();
        return -1;
    }

    UINT width, height;
    captureSession->GetSize(width, height);
    std::cout << "Capture Started. Resolution: " << width << "x" << height << std::endl;

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
    hr = device->CreateTexture2D(&texDesc, nullptr, &targetTexture);
    if (FAILED(hr)) return -1;

    std::unique_ptr<IFrameCaptureAccessStrategy> strategy;

    switch (FRAME_TYPE) {
    case FrameType::SaveFrametoImage:
        std::cout << "[Mode] This mode will save captured frame to image (Multithreaded)" << std::endl;
        strategy = std::make_unique<FrametoImageSavingMultiThreadedStrategy>();
        break;
    case FrameType::CpuAccess:
        std::cout << "[Strategy] CPU Access (Raw Pixels)" << std::endl;
        strategy = std::make_unique<CpuAccessStrategy>();
        break;
    case FrameType::GpuDirect:
        std::cout << "[Strategy] GPU Direct (Texture Pointer)" << std::endl;
        strategy = std::make_unique<GpuDirectStrategy>();
        break;
    case FrameType::RtpStream: {
        std::cout << "[Strategy] RTP Stream (H.264 over UDP)" << std::endl;
        RtpStreamConfig rtpConfig;
        rtpConfig.destIp = "127.0.0.1";
        rtpConfig.destPort = 5004;
        rtpConfig.fps = 30;
        rtpConfig.bitrate = 4'000'000;   // 4 Mbps
        rtpConfig.sdpFilePath = "stream.sdp";
        strategy = std::make_unique<RtpStreamStrategy>(rtpConfig);
        break;
    }
    }

    strategy->Initialize(device.Get(), width, height);

    // =========================================================
    // Main Loop
    // =========================================================
    // For RTP streaming, run continuously until Ctrl+C.
    // For other modes, run for a fixed number of frames.
    bool continuousMode = (FRAME_TYPE == FrameType::RtpStream);
    ull n_frames = continuousMode ? 0ULL : 30ULL;
    ull frameIndex = 0;

    if (continuousMode) {
        std::cout << "Starting continuous capture loop (Ctrl+C to stop)..." << std::endl;
    }
    else {
        std::cout << "Starting loop (" << n_frames << ") ..." << std::endl;
    }

    while (g_running && (continuousMode || frameIndex < n_frames)) {

        CaptureStatus status = captureSession->AcquireFrame(context.Get(), targetTexture.Get());

        if (status == CaptureStatus::Success) {

            std::optional<FrameData> output = strategy->ProcessFrame(context.Get(), targetTexture.Get(), static_cast<int>(frameIndex));

            // C. Handle Returned Data (Optional)
            if (output.has_value()) {
                FrameData& frame = output.value();

                if (frame.type == FrameType::GpuDirect) {
                    D3D11_TEXTURE2D_DESC desc;
                    frame.d3dTexture->GetDesc(&desc);
                    std::cout << " [Main] Received GPU Texture: " << desc.Width << "x" << desc.Height
                        << " Addr: " << frame.d3dTexture.Get() << std::endl;
                }
                else if (frame.type == FrameType::CpuAccess) {
                    std::cout << " [Main] Received CPU Pixels: " << frame.pixels.size() << " bytes" << std::endl;
                }
                else if (frame.type == FrameType::SaveFrametoImage) {

                }
            }
            // For RTP mode, no output is returned (frames sent directly to network)
        }
        else if (status == CaptureStatus::Timeout) {

        }
        else if (status == CaptureStatus::ReinitRequired) {
            std::cerr << "Re-initialization required!" << std::endl;
            break;
        }
        else {
            std::cerr << "Fatal Capture Error!" << std::endl;
            break;
        }

        ++frameIndex;

        // Cap framerate (~30 FPS)
        //std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    // Cleanup
    std::cout << "Shutting down..." << std::endl;
    strategy->Shutdown();
    CoUninitialize();

    return 0;
}