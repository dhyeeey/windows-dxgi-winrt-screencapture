#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>

struct WindowSize {
    int width = 0;
    int height = 0;
};

/**
 * @brief Library-agnostic window / event-loop host.
 *
 * Implement this for GLFW, raw Win32, SDL, etc. Core app code only needs
 * an HWND (for the D3D swapchain), resize notifications, and a UI event pump.
 */
class IWindowHost {
public:
    virtual ~IWindowHost() = default;

    virtual bool initialize() = 0;
    virtual bool createWindow(int width, int height, const char* title) = 0;
    virtual HWND nativeHandle() const = 0;
    virtual void setResizeCallback(std::function<void(WindowSize)> cb) = 0;
    virtual void pollEvents() = 0;
    virtual bool shouldClose() const = 0;
    virtual void destroy() = 0;
};
