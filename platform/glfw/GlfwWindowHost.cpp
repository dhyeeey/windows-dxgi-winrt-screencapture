#include "GlfwWindowHost.hpp"

#include "Log.hpp"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

GlfwWindowHost::~GlfwWindowHost() {
    destroy();
}

bool GlfwWindowHost::initialize() {
    Log("Main", "Initializing GLFW...");
    if (!glfwInit()) {
        Log("Main", "GLFW Init Failed.");
        return false;
    }
    glfwInitialized_ = true;
    return true;
}

bool GlfwWindowHost::createWindow(int width, int height, const char* title) {
    if (!glfwInitialized_ && !initialize()) {
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    Log("Main", "Creating Window...");
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        Log("Main", "Window Creation Failed.");
        destroy();
        return false;
    }

    hwnd_ = glfwGetWin32Window(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
    return true;
}

HWND GlfwWindowHost::nativeHandle() const {
    return hwnd_;
}

void GlfwWindowHost::setResizeCallback(std::function<void(WindowSize)> cb) {
    resizeCallback_ = std::move(cb);
}

void GlfwWindowHost::pollEvents() {
    glfwWaitEvents();
}

bool GlfwWindowHost::shouldClose() const {
    return window_ == nullptr || glfwWindowShouldClose(window_);
}

void GlfwWindowHost::destroy() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        hwnd_ = nullptr;
    }
    if (glfwInitialized_) {
        glfwTerminate();
        glfwInitialized_ = false;
    }
}

void GlfwWindowHost::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<GlfwWindowHost*>(glfwGetWindowUserPointer(window));
    if (!self || !self->resizeCallback_ || width <= 0 || height <= 0) {
        return;
    }
    self->resizeCallback_({ width, height });
}
