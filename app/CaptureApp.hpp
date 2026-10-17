#pragma once

#include "IWindowHost.hpp"
#include "FrameCaptureAcessStrategies.hpp"
#include "IScreenCapture.hpp"

class CaptureApp {
public:
    bool run(IWindowHost& window, CaptureMethod captureMethod, FrameType frameType);
};
