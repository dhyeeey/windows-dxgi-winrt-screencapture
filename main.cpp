#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "CaptureApp.hpp"
#include "GlfwWindowHost.hpp"
#include "FrameCaptureAcessStrategies.hpp"
#include "IScreenCapture.hpp"
#include "Log.hpp"

#include <cctype>
#include <iostream>
#include <string>

struct CliOptions {
    CaptureMethod capture = CaptureMethod::DXGI;
    FrameType access = FrameType::GpuDirect;
    bool showHelp = false;
    bool ok = true;
    std::string error;
};

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

static void PrintUsage(const char* exeName) {
    std::cout
        << "Usage: " << exeName << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  -c, --capture <dxgi|winrt>   Capture engine (default: dxgi)\n"
        << "  -a, --access  <gpu|cpu>      Frame access mode (default: gpu)\n"
        << "  -h, --help                   Show this help\n"
        << "\n"
        << "Examples:\n"
        << "  " << exeName << "\n"
        << "  " << exeName << " --capture winrt --access cpu\n"
        << "  " << exeName << " -c dxgi -a gpu\n";
}

static bool EqualsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static CliOptions ParseArgs(int argc, char** argv) {
    CliOptions opts;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto needValue = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                opts.ok = false;
                opts.error = std::string("Missing value for ") + flag;
                return nullptr;
            }
            return argv[++i];
        };

        if (EqualsIgnoreCase(arg, "-h") || EqualsIgnoreCase(arg, "--help")) {
            opts.showHelp = true;
            return opts;
        }

        if (EqualsIgnoreCase(arg, "-c") || EqualsIgnoreCase(arg, "--capture")) {
            const char* value = needValue(arg);
            if (!value) return opts;
            if (EqualsIgnoreCase(value, "dxgi")) {
                opts.capture = CaptureMethod::DXGI;
            }
            else if (EqualsIgnoreCase(value, "winrt")) {
                opts.capture = CaptureMethod::WinRT;
            }
            else {
                opts.ok = false;
                opts.error = std::string("Unknown capture method: ") + value + " (use dxgi or winrt)";
                return opts;
            }
            continue;
        }

        if (EqualsIgnoreCase(arg, "-a") || EqualsIgnoreCase(arg, "--access")) {
            const char* value = needValue(arg);
            if (!value) return opts;
            if (EqualsIgnoreCase(value, "gpu") || EqualsIgnoreCase(value, "gpudirect")) {
                opts.access = FrameType::GpuDirect;
            }
            else if (EqualsIgnoreCase(value, "cpu") || EqualsIgnoreCase(value, "cpuaccess")) {
                opts.access = FrameType::CpuAccess;
            }
            else {
                opts.ok = false;
                opts.error = std::string("Unknown access mode: ") + value + " (use gpu or cpu)";
                return opts;
            }
            continue;
        }

        opts.ok = false;
        opts.error = std::string("Unknown argument: ") + arg;
        return opts;
    }

    return opts;
}

static const char* CaptureName(CaptureMethod method) {
    return method == CaptureMethod::WinRT ? "WinRT" : "DXGI";
}

static const char* AccessName(FrameType access) {
    return access == FrameType::CpuAccess ? "CPU" : "GPU";
}

int main(int argc, char** argv) {
    const char* exeName = (argc > 0 && argv[0]) ? argv[0] : "WindowsScreenCapture.exe";
    CliOptions opts = ParseArgs(argc, argv);

    if (opts.showHelp) {
        PrintUsage(exeName);
        return 0;
    }
    if (!opts.ok) {
        std::cerr << "Error: " << opts.error << "\n\n";
        PrintUsage(exeName);
        return 1;
    }

    SetProcessDpiAware();

    Log("Main", std::string("Capture=") + CaptureName(opts.capture)
        + ", Access=" + AccessName(opts.access));

    GlfwWindowHost window;
    CaptureApp app;
    const bool ok = app.run(window, opts.capture, opts.access);
    return ok ? 0 : -1;
}
