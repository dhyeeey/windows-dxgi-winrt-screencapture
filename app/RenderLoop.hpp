#pragma once

#include "RenderContext.hpp"
#include "FrameCaptureAcessStrategies.hpp"
#include "IScreenCapture.hpp"

void RunRenderLoop(RenderContext& threadCtx, CaptureMethod captureMethod, FrameType frameType);
