#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>

struct RenderContext {
    HWND hwnd = nullptr;
    std::atomic<bool> running{ true };

    std::atomic<bool> resizePending{ false };
    std::atomic<int> targetW{ 1280 };
    std::atomic<int> targetH{ 720 };
};
