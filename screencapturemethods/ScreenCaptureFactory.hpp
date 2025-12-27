#pragma once

#include "DxgiCaptureWrapper.hpp"
#include "WinRTCaptureWrapper.hpp"
#include <memory>

class ScreenCaptureFactory {
public:
    static std::unique_ptr<IScreenCapture> Create(CaptureMethod method) {
        switch (method) {
        case CaptureMethod::DXGI:
            std::cout << "Capturing screen using DXGI" << std::endl;
            return std::make_unique<DxgiCaptureWrapper>();
        case CaptureMethod::WinRT:
            std::cout << "Capturing screen using WinRT" << std::endl;
            return std::make_unique<WinRTCaptureWrapper>();
        default:
            return nullptr;
        }
    }
};