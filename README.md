Here is a clean, professional `README.md` file formatted for GitHub. You can copy-paste this directly into your repository.

---

# 🖥️ DXGI High-Performance Screen Capture

A modular, high-performance C++ library for capturing Windows screens using the **DXGI Desktop Duplication API**.

This project demonstrates how to robustly capture desktop frames, composite the mouse cursor, and process the data using a **Strategy Pattern**—allowing seamless switching between **Zero-Copy GPU access**, **CPU analysis**, and **Multi-threaded Disk Recording**.

---

## 🚀 Features

* **⚡ Zero-Copy GPU Capture:** Access frames directly in VRAM (`ID3D11Texture2D`) for maximum performance (ideal for NVENC/Rendering).
* **🖱️ Hardware Cursor Composition:** Automatically renders the mouse cursor (color or monochrome) onto the captured frame using custom shaders.
* **📦 Dirty Rect Optimization:** Processes only the regions of the screen that changed, saving bandwidth.
* **🔄 Modular Strategy Pattern:** Switch processing modes (Disk Save vs. CPU Access vs. GPU Direct) with a single line of code.
* **🧵 Multi-Threaded Saving:** Asynchronous Producer-Consumer architecture for saving images (JPG/PNG/BMP) without blocking the capture loop.
* **🛡️ Robust Error Handling:** Automatically detects and reports lost devices (TDR) or monitor disconnects.

---

## 🏗️ Architecture Overview

The project is structured into three main layers:

### 1. Core Capture Engine (`DxgiScreenCapture`)

This module handles the low-level heavy lifting with the Windows API.

* **`DesktopDuplicator`**: The worker class. Manages the `IDXGIOutputDuplication` interface, processes metadata (moved regions), and runs the HLSL shaders to draw the mouse cursor.
* **`CaptureDevice`**: Represents a single monitor session. Manages the D3D11 device lifecycle.
* **`CaptureManager`**: A thread-safe singleton factory that manages capture sessions.

### 2. Processing Strategies (`CaptureStrategies`)

Defines *how* a frame is handled once captured. Implements the **Strategy Pattern**.

* **`GpuDirectStrategy`**: **(Fastest)** Returns a D3D11 Texture pointer. Data never leaves the GPU.
* **`CpuAccessStrategy`**: Copies the frame from VRAM to System RAM (Staging Texture) and returns raw BGRA pixels.
* **`ThreadedSaveStrategy`**: Copies frame to RAM and pushes it to a `ThreadSafeQueue`. A background thread pops the frame and saves it to disk.

### 3. Utilities

* **`ImageSaver`**: A wrapper around **WIC (Windows Imaging Component)** for saving textures/pixels to disk efficiently.
* **`ThreadSafeQueue`**: A generic queue enabling the Producer-Consumer pattern.

---

## 🛠️ Getting Started

### Prerequisites

* Windows 10 or later.
* Visual Studio 2019/2022.
* DirectX SDK (Included in Windows SDK).

### Installation

1. Clone the repository.
2. Open the folder in Visual Studio (or generate project files).
3. Ensure you link against `d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`, and `windowscodec.lib`.

### Configuration

In `ScreenCaptureNew.cpp` (Main), simply change the `CURRENT_MODE` variable to switch the application's behavior:

```cpp
// Options: 
// AppMode::ThreadedSaver  -> Save images to disk
// AppMode::CpuAccess      -> Get raw pixels in Main
// AppMode::GpuDirect      -> Get Texture pointer in Main

AppMode CURRENT_MODE = AppMode::ThreadedSaver;

```

---

## 📚 Code Walkthrough

### 1. The Capture Loop (Main)

The `main()` function is clean and logic-free. It initializes D3D11, sets up the `CaptureDevice`, and runs a loop. It delegates the actual work to the active **Strategy**.

```cpp
// 1. Capture the frame from Windows
auto result = capturer->capture(device, targetTexture, ...);

// 2. Delegate processing to the chosen Strategy
std::optional<FrameData> data = strategy->ProcessFrame(context, targetTexture, frameIndex);

// 3. (Optional) Use the returned data
if (data.has_value()) {
    // We have pixels or a texture!
}

```

### 2. The Strategy Interface

All strategies inherit from `ICaptureStrategy`. This ensures `main` never needs to know the details of threading or memory mapping.

```cpp
class ICaptureStrategy {
    virtual void Initialize(...) = 0;
    virtual std::optional<FrameData> ProcessFrame(...) = 0;
    virtual void Shutdown() = 0;
};

```

---

## 📂 File Structure

| File | Component | Description |
| --- | --- | --- |
| `ScreenCaptureNew.cpp` | **Main** | Entry point. Initializes D3D and runs the capture loop. |
| `DxgiScreenCapture.hpp` | **Core** | Low-level DXGI logic, Mouse Drawing, Dirty Rects. |
| `CaptureStrategies.hpp` | **Logic** | Implementation of GPU/CPU/Disk strategies. |
| `ImageSaver.hpp` | **Utility** | WIC Wrapper for saving JPG/PNG/BMP. |
| `ThreadSafeQueue.hpp` | **Utility** | Thread synchronization helper. |

---

## ⚠️ Known Limitations

* **Fullscreen Exclusive:** DXGI cannot capture applications running in Fullscreen Exclusive mode (a limitation of the Windows API).
* **HDR:** The current implementation captures in `B8G8R8A8_UNORM` (SDR). HDR content may appear washed out without tone mapping.

## 📄 License

This project is open-source and available under the MIT License.