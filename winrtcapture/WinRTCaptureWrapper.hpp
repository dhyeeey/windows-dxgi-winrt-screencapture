#pragma once

#include "IScreenCapture.hpp"
#include "WinRTScreenCapture.hpp"

class WinRTCaptureWrapper : public IScreenCapture {
public:
    bool Initialize(ID3D11Device* device, HMONITOR monitor) override {
        try {
            auto& manager = WinRTCapture::WinRTCaptureManager::getInstance();
            capturer_ = manager.createMonitorCapture(device, monitor);
            return capturer_ && capturer_->prepare() == WinRTCapture::CaptureResult::Success;
        }
        catch (...) { return false; }
    }

    CaptureStatus AcquireFrame(ID3D11DeviceContext* context, ID3D11Texture2D* targetTexture) override {
        if (!capturer_) return CaptureStatus::Error;

        UINT w, h;
        GetSize(w, h);
        D3D11_BOX cropBox = { 0, 0, 0, w, h, 1 };

        auto result = capturer_->capture(targetTexture, cropBox, true);

        switch (result) {
        case WinRTCapture::CaptureResult::Success: return CaptureStatus::Success;
        case WinRTCapture::CaptureResult::Timeout: return CaptureStatus::Timeout;
        case WinRTCapture::CaptureResult::SizeChanged: return CaptureStatus::ReinitRequired;
        default: return CaptureStatus::Error;
        }
    }

    void GetSize(UINT& width, UINT& height) override {
        if (capturer_) {
            auto size = capturer_->getCaptureSize();
            width = size.width;
            height = size.height;
        }
    }

private:
    std::shared_ptr<WinRTCapture::WinRTCaptureDevice> capturer_;
};