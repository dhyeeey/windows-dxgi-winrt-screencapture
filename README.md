# 🖥️ Windows Screen Capture

A high-performance, modular C++ library for capturing Windows screens.

This project implements a **Dual-Engine Architecture**, allowing you to seamlessly switch between the classic **DXGI Desktop Duplication API** and the modern **Windows.Graphics.Capture (WinRT)** API. It decouples the *Capture Mechanism* from the *Frame Processing Logic*, giving you total control over how frames are handled (Disk, CPU, or GPU).

---

## 🚀 Key Features

### 1. Dual Capture Engines (Factory Pattern)

Switch between capture methods with a single enum.

* **DXGI Engine:** Uses Desktop Duplication API. Best for high-performance full-monitor capture on Windows 7/8/10.
* **WinRT Engine:** Uses `Windows.Graphics.Capture`. Modern API that supports specific window capture, cursor exclusion, and survives GPU driver resets/hybrid GPU switching.

### 2. Flexible Frame Processing (Strategy Pattern)

Decouple *how* you get the frame from *what* you do with it.

* **⚡ GPU Direct:** Zero-copy access to the `ID3D11Texture2D`. Ideal for NVENC/AMF video encoding or Direct3D rendering.
* **🧠 CPU Access:** Efficiently copies data to system RAM (mapped staging textures) for OpenCV/AI analysis.
* **💾 Async Disk Saving:** Multi-threaded pipeline using a thread-safe queue to save frames (JPG/PNG) without blocking the capture loop.

### 3. High Performance

* **Zero-Copy Capture:** Frames remain in VRAM unless explicitly requested.
* **Dirty Rect Optimization:** (DXGI) Only redraws changed regions.
* **Hardware Cursor:** Automatically composites the system mouse cursor (even hardware cursors) onto the frame.

---

## 🏗️ Architecture

| Component | Responsibility | Implementation Class |
| --- | --- | --- |
| **Capture Factory** | Creates the requested capture engine. | `ScreenCaptureFactory` |
| **Capture Engine** | Talks to the OS to get a frame into a Texture. | `DxgiCaptureWrapper`, `WinRTCaptureWrapper` |
| **Access Strategy** | Processes the texture (Copy, Save, or Pass-through). | `GpuDirectStrategy`, `CpuAccessStrategy`, `FrametoImage...Strategy` |

---

## 🛠️ Configuration & Usage

The application is controlled via two simple configuration variables in `main.cpp`.

### 1. Choose Capture Method

```cpp
// Options: CaptureMethod::DXGI or CaptureMethod::WinRT
CaptureMethod CAPTURE_METHOD = CaptureMethod::WinRT; 

```

### 2. Choose Processing Mode

```cpp
// Options:
// FrameType::GpuDirect       -> Fastest. Returns ID3D11Texture2D*.
// FrameType::CpuAccess       -> Returns std::vector<uint8_t> (Raw Pixels).
// FrameType::SaveFrametoImage -> Saves files to disk asynchronously.

FrameType FRAME_TYPE = FrameType::GpuDirect;

```

### Example Output

```text
Initializing Capture Engine...
Capturing screen using WinRT
Capture Started. Resolution: 1920x1080
[Strategy] GPU Direct (Texture Pointer)
Starting loop (30) ...
 [Main] Received GPU Texture: 1920x1080 Addr: 000002A1B3F4D120
 [Main] Received GPU Texture: 1920x1080 Addr: 000002A1B3F4D120
...
Shutting down...

```

---

## 🔧 Building the Project

### Requirements

* Windows 10 (Version 1803 or later for WinRT).
* Visual Studio 2019/2022 (with "Desktop development with C++").
* CMake 3.8+.

### Build Instructions

1. **Clone the repository.**
2. **Open in Visual Studio** (File > Open > Folder).
3. **Build:** CMake will automatically locate the Windows SDK (DirectX and WinRT headers).
* *Note:* You do **not** need to install a separate DirectX SDK.


4. **Run:** Select `WindowsDxgiScreenCapture.exe` as the startup target.

---

## 📂 File Structure

| Directory/File | Description |
| --- | --- |
| `dxgicapture/` | Low-level DXGI Desktop Duplication logic. |
| `winrtcapture/` | Low-level Windows.Graphics.Capture logic (COM/WinRT Interop). |
| `screencapturemethods/` | **Factory** interfaces (`IScreenCapture`) abstracting the engines. |
| `frameaccessmethod/` | **Strategy** classes (`IFrameCaptureAccessStrategy`) for data handling. |
| `util/` | Helpers: `ImageSaver` (WIC), `ThreadSafeQueue`. |
| `main.cpp` | Entry point. Wires the Factory and Strategy together. |

---

## ⚠️ Notes on WinRT

This project uses **C++/WinRT Interop** manually (via `RoInitialize` and `CreateDirect3D11DeviceFromDXGIDevice`) rather than C++/WinRT language projections (`.winmd` files). This ensures the project remains a standard, lightweight C++ console application without needing complex UWP/WinRT build chains.

---

## 📄 License

MIT License. Free for commercial and private use.