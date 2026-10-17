#pragma once

#include "IWindowHost.hpp"

struct GLFWwindow;

class GlfwWindowHost : public IWindowHost {
public:
    GlfwWindowHost() = default;
    ~GlfwWindowHost() override;

    bool initialize() override;
    bool createWindow(int width, int height, const char* title) override;
    HWND nativeHandle() const override;
    void setResizeCallback(std::function<void(WindowSize)> cb) override;
    void pollEvents() override;
    bool shouldClose() const override;
    void destroy() override;

private:
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    HWND hwnd_ = nullptr;
    bool glfwInitialized_ = false;
    std::function<void(WindowSize)> resizeCallback_;
};
