#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "CaptureApp.hpp"
#include "GlfwWindowHost.hpp"
#include "FrameCaptureAcessStrategies.hpp"
#include "IScreenCapture.hpp"

static void SetProcessDpiAware() {
    // Prefer Per-Monitor V2 so Win32 cursor coords match DXGI physical pixels.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setCtx = reinterpret_cast<SetDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx && setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }
    SetProcessDPIAware();
}

int main() {
    SetProcessDpiAware();

    GlfwWindowHost window;
    CaptureApp app;
    const bool ok = app.run(window, CaptureMethod::DXGI, FrameType::GpuDirect);
    return ok ? 0 : -1;
}
