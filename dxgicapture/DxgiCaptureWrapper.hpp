#pragma once

#include "IScreenCapture.hpp"
#include "DxgiScreenCapture.hpp"

class DxgiCaptureWrapper : public IScreenCapture {
public:
    bool Initialize(ID3D11Device* device, HMONITOR monitor) override {
        try {
            auto& manager = DxgiCapture::CaptureManager::getInstance();
            capturer_ = manager.createCaptureDevice(device, monitor);
            return capturer_->prepare() == DxgiCapture::CaptureResult::Success;
        }
        catch (...) { return false; }
    }

    //CaptureStatus AcquireFrame(ID3D11DeviceContext* context, ID3D11Texture2D* targetTexture) override {
    //    if (!capturer_) return CaptureStatus::Error;

    //    // Get dimensions for crop box
    //    UINT w, h;
    //    capturer_->getSize(w, h);
    //    D3D11_BOX cropBox = { 0, 0, 0, w, h, 1 };

    //    // We need the device to call capture
    //    ComPtr<ID3D11Device> device;
    //    targetTexture->GetDevice(&device);
    //    ComPtr<ID3D11RenderTargetView> rtv; // Assuming nullptr is fine if no mouse drawing needed

    //    auto result = capturer_->capture(device.Get(), targetTexture, rtv.Get(), cropBox, true);

    //    switch (result) {
    //    case DxgiCapture::CaptureResult::Success: return CaptureStatus::Success;
    //    case DxgiCapture::CaptureResult::Timeout: return CaptureStatus::Timeout;
    //    case DxgiCapture::CaptureResult::DeviceRemoved:
    //    case DxgiCapture::CaptureResult::SizeChanged: return CaptureStatus::ReinitRequired;
    //    default: return CaptureStatus::Error;
    //    }
    //}

    CaptureStatus AcquireFrame(
        ID3D11DeviceContext* context,
        ID3D11Texture2D* targetTexture,
        ID3D11RenderTargetView* targetRTV
    ) override {

        if (!capturer_ || !targetTexture) {
            std::cerr << "[DxgiCaptureWrapper] ERROR: capturer_ or targetTexture is null\n";
            return CaptureStatus::Error;
        }

        static bool lastWasNull = false;
        bool nowNull = (targetRTV == nullptr);
        if (nowNull && !lastWasNull) {
            std::cerr << "[DxgiCaptureWrapper] Cursor compositing OFF (RTV is null)\n";
        }
        else if (!nowNull && lastWasNull) {
            std::cerr << "[DxgiCaptureWrapper] Cursor compositing ON\n";
        }
        lastWasNull = nowNull;

        // Crop box (full frame)
        UINT w = 0, h = 0;
        capturer_->getSize(w, h);
        D3D11_BOX cropBox{};
        cropBox.left = 0;
        cropBox.top = 0;
        cropBox.front = 0;
        cropBox.right = w;
        cropBox.bottom = h;
        cropBox.back = 1;

        // Device that owns the destination texture
        ComPtr<ID3D11Device> targetDevice;
        targetTexture->GetDevice(&targetDevice);

        /*std::cerr << "[DxgiCaptureWrapper] AcquireFrame: ctx=" << context
            << " tex=" << targetTexture
            << " rtv=" << targetRTV
            << " size=" << w << "x" << h
            << " targetDevice=" << targetDevice.Get()
            << "\n";*/

        if (!targetRTV) {
            //std::cerr << "[DxgiCaptureWrapper] WARNING: targetRTV is NULL => cursor compositing will be skipped!\n";
        }

        // IMPORTANT: pass targetRTV instead of nullptr
        auto result = capturer_->capture(targetDevice.Get(),
            targetTexture,
            targetRTV,
            cropBox,
            true);

        //std::cerr << "[DxgiCaptureWrapper] CaptureDevice::capture result=" << (int)result << "\n";

        switch (result) {
        case DxgiCapture::CaptureResult::Success:
            return CaptureStatus::Success;
        case DxgiCapture::CaptureResult::Timeout:
            return CaptureStatus::Timeout;
        case DxgiCapture::CaptureResult::DeviceRemoved:
        case DxgiCapture::CaptureResult::SizeChanged:
            return CaptureStatus::ReinitRequired;
        default:
            return CaptureStatus::Error;
        }
    }


    void GetSize(UINT& width, UINT& height) override {
        if (capturer_) capturer_->getSize(width, height);
    }

private:
    std::shared_ptr<DxgiCapture::CaptureDevice> capturer_;
};