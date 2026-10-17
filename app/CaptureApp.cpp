#include "CaptureApp.hpp"

#include "Log.hpp"
#include "RenderContext.hpp"
#include "RenderLoop.hpp"

#include <thread>

bool CaptureApp::run(IWindowHost& window, CaptureMethod captureMethod, FrameType frameType) {
    if (!window.initialize()) {
        return false;
    }

    if (!window.createWindow(1280, 720, "Capture (Multithreaded + Log)")) {
        return false;
    }

    RenderContext ctx;
    ctx.hwnd = window.nativeHandle();
    ctx.targetW = 1280;
    ctx.targetH = 720;

    window.setResizeCallback([&ctx](WindowSize size) {
        ctx.targetW = size.width;
        ctx.targetH = size.height;
        ctx.resizePending = true;
    });

    Log("Main", "Starting Render Thread...");
    std::thread renderThread(RunRenderLoop, std::ref(ctx), captureMethod, frameType);

    Log("Main", "Entering Event Loop.");
    while (!window.shouldClose()) {
        window.pollEvents();
    }

    Log("Main", "Window Close Requested.");
    Log("Main", "Signaling Render Thread to Stop...");
    ctx.running = false;

    if (renderThread.joinable()) {
        Log("Main", "Waiting for Render Thread to Join...");
        renderThread.join();
        Log("Main", "Render Thread Joined.");
    }

    window.destroy();
    Log("Main", "Application Terminated.");
    return true;
}
