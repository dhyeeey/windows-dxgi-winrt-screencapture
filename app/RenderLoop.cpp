#include "RenderLoop.hpp"

#include "D3dUtil.hpp"
#include "Log.hpp"
#include "LetterboxPresenter.hpp"
#include "ScreenCaptureFactory.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>
#include <sstream>

using Microsoft::WRL::ComPtr;

void RunRenderLoop(RenderContext& threadCtx, CaptureMethod captureMethod, FrameType frameType) {
    Log("Render", "Thread Started.");

    // Match DXGI desktop coordinates / GetCursorInfo physical pixels.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &device, &featureLevel, &context);

    if (FAILED(hr)) {
        Log("Render", "CRITICAL: D3D11CreateDevice Failed.");
        return;
    }
    Log("Render", "D3D11 Device Created.");

    Log("Render", "Initializing Capture Session...");
    POINT pt = { 0, 0 };
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    auto captureSession = ScreenCaptureFactory::Create(captureMethod);

    if (!captureSession || !captureSession->Initialize(device.Get(), hMonitor)) {
        Log("Render", "CRITICAL: Capture Session Initialize Failed.");
        return;
    }

    UINT width, height;
    captureSession->GetSize(width, height);

    std::stringstream ss;
    ss << "Capture Session Ready. Resolution: " << width << "x" << height;
    Log("Render", ss.str());

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
    Log("Render", "Target Texture Created.");

    ComPtr<ID3D11RenderTargetView> targetRTV;
    ThrowIfFailed(device->CreateRenderTargetView(targetTexture.Get(), nullptr, &targetRTV),
        "CreateRenderTargetView(targetTexture)");
    Log("Render", "Target RTV Created (for cursor compositing).");

    std::unique_ptr<IFrameCaptureAccessStrategy> strategy;
    if (frameType == FrameType::GpuDirect) {
        strategy = std::make_unique<GpuDirectStrategy>();
        Log("Render", "Strategy Selected: GPU Direct");
    }
    else if (frameType == FrameType::CpuAccess) {
        strategy = std::make_unique<CpuAccessStrategy>();
        Log("Render", "Strategy Selected: CPU Access");
    }
    else {
        strategy = std::make_unique<FrametoImageSavingMultiThreadedStrategy>();
        Log("Render", "Strategy Selected: Save to Image");
    }

    strategy->Initialize(device.Get(), width, height);
    Log("Render", "Strategy Initialized.");

    LetterboxPresenter presenter;
    int currentW = threadCtx.targetW;
    int currentH = threadCtx.targetH;

    presenter.Init(device.Get(), context.Get(), threadCtx.hwnd,
        (UINT)currentW, (UINT)currentH, width, height);

    Log("Render", "Entering Render Loop...");

    while (threadCtx.running) {
        if (threadCtx.resizePending) {
            int w = threadCtx.targetW;
            int h = threadCtx.targetH;
            if (w > 0 && h > 0) {
                presenter.Resize(device.Get(), context.Get(), (UINT)w, (UINT)h);
            }
            threadCtx.resizePending = false;
        }

        CaptureStatus status = captureSession->AcquireFrame(context.Get(), targetTexture.Get(), targetRTV.Get());

        // Only Present on Success. Presenting on Timeout with Present(1,0) burns a full
        // vsync wait while no new desktop frame is acquired, which quickly adds mirror lag.
        if (status == CaptureStatus::Success) {
            strategy->ProcessFrame(context.Get(), targetTexture.Get(), 0);
            presenter.PresentTexture(device.Get(), context.Get(), targetTexture.Get());
        }
        else if (status == CaptureStatus::ReinitRequired) {
            Log("Render", "WARNING: Capture Reinit Required (Not Implemented).");
        }
    }

    Log("Render", "Loop Finished. Shutting down...");
    strategy->Shutdown();
    Log("Render", "Strategy Shutdown Complete.");
    Log("Render", "Thread Exiting.");
}
