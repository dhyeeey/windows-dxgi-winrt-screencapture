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

    CaptureStatus AcquireFrame(ID3D11DeviceContext* context, ID3D11Texture2D* targetTexture) override {
        if (!capturer_) return CaptureStatus::Error;

        // Get dimensions for crop box
        UINT w, h;
        capturer_->getSize(w, h);
        D3D11_BOX cropBox = { 0, 0, 0, w, h, 1 };

        // We need the device to call capture
        ComPtr<ID3D11Device> device;
        targetTexture->GetDevice(&device);
        ComPtr<ID3D11RenderTargetView> rtv; // Assuming nullptr is fine if no mouse drawing needed

        auto result = capturer_->capture(device.Get(), targetTexture, rtv.Get(), cropBox, true);

        switch (result) {
        case DxgiCapture::CaptureResult::Success: return CaptureStatus::Success;
        case DxgiCapture::CaptureResult::Timeout: return CaptureStatus::Timeout;
        case DxgiCapture::CaptureResult::DeviceRemoved:
        case DxgiCapture::CaptureResult::SizeChanged: return CaptureStatus::ReinitRequired;
        default: return CaptureStatus::Error;
        }
    }

    void GetSize(UINT& width, UINT& height) override {
        if (capturer_) capturer_->getSize(width, height);
    }

private:
    std::shared_ptr<DxgiCapture::CaptureDevice> capturer_;
};