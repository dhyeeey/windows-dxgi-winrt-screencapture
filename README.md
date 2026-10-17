# Windows DXGI / WinRT Screen Capture

High-performance C++ screen capture for Windows with a dual-engine design: classic **DXGI Desktop Duplication** and modern **Windows.Graphics.Capture (WinRT)**. Capture is decoupled from frame handling (GPU texture, CPU pixels, or async image save), and the UI shell is decoupled from the window library so GLFW can be swapped for another host.

---

## Features

### Dual capture engines

Switch engines with `CaptureMethod` in [`main.cpp`](main.cpp) (passed into `CaptureApp::run`).

| Engine | API | Notes |
| --- | --- | --- |
| **DXGI** | Desktop Duplication | Full-monitor capture; dirty/move rects; manual hardware-cursor compositing |
| **WinRT** | `Windows.Graphics.Capture` | Monitor (and library support for window) capture; OS cursor via `IsCursorCaptureEnabled` |

The demo app currently initializes capture on the **primary monitor** (`MonitorFromPoint` in `RunRenderLoop`).

### Frame access strategies

Choose with `FrameType` (mirroring always presents; strategy selects how the frame is exposed):

| Mode | Enum | Behavior |
| --- | --- | --- |
| GPU Direct | `FrameType::GpuDirect` | Keep `ID3D11Texture2D` in VRAM (default preview path) |
| CPU Access | `FrameType::CpuAccess` | Map staging texture → CPU BGRA buffer |

### Preview window

- Resizable window via `IWindowHost` (default: GLFW).
- Letterboxed D3D11 presentation (`LetterboxPresenter`).
- DXGI path composites the hardware cursor onto the capture texture (hotspot-correct Win32 timeout fallback; Per-Monitor DPI aware).

---

## Architecture

```text
main.cpp
  └─ GlfwWindowHost (platform/glfw) ── implements IWindowHost
  └─ CaptureApp::run
        ├─ UI thread: window events, resize → RenderContext
        └─ Render thread: RunRenderLoop
              ├─ ScreenCaptureFactory → DxgiCaptureWrapper | WinRTCaptureWrapper
              ├─ IFrameCaptureAccessStrategy (Gpu / CPU / Save)
              └─ LetterboxPresenter → swapchain on HWND
```

| Layer | Path | Role |
| --- | --- | --- |
| Entry | `main.cpp` | DPI setup; pick window host, capture method, frame type |
| App | `app/` | `CaptureApp`, `RenderLoop`, `RenderContext`, logging |
| Platform | `platform/` | `IWindowHost`; `glfw/GlfwWindowHost` |
| Presenter | `presenter/` | Letterboxed D3D11 present |
| Capture | `screencapturemethods/`, `dxgicapture/`, `winrtcapture/` | Factory + engines |
| Frames | `frameaccessmethod/` | GPU Direct / CPU Access strategies |

To use another window library, implement `IWindowHost` and pass it to `CaptureApp::run` instead of `GlfwWindowHost`. On the CMake side, vendor that library similarly to `glfw-3.4.bin.WIN64/` and link its imported target.

---

## Configuration (CLI)

Run the built executable with optional flags. Defaults are **DXGI** + **GPU** access when no args are given.

```bat
WindowsScreenCapture.exe
WindowsScreenCapture.exe --capture winrt --access cpu
WindowsScreenCapture.exe -c dxgi -a gpu
WindowsScreenCapture.exe --help
```

| Flag | Values | Default |
| --- | --- | --- |
| `-c`, `--capture` | `dxgi`, `winrt` | `dxgi` |
| `-a`, `--access` | `gpu` (or `gpudirect`), `cpu` (or `cpuaccess`) | `gpu` |
| `-h`, `--help` | | print usage |

You can still call `CaptureApp::run(...)` from code with the same enums if you embed the library-style entry path.

---

## Building

### Requirements

- Windows 10 (1803+ recommended for WinRT Graphics Capture)
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.8+
- Vendored prebuilt **GLFW 3.4** under `glfw-3.4.bin.WIN64/` (MSVC `/MT` static lib; no separate GLFW install)

Windows SDK provides Direct3D 11 and WinRT headers; no legacy DirectX SDK is required.

### Build (Visual Studio)

1. Clone the repository.
2. **File → Open → Folder** on the repo root.
3. Select a CMake preset (e.g. `x64-debug` from [`CMakePresets.json`](CMakePresets.json); output under `out/build/...`).
4. Build and run target `WindowsScreenCapture`.

### Build (CLI)

```bat
cmake -S . -B out/build/x64-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/x64-debug
```

#### Static vs dynamic MSVC runtime

| Option | Default | Effect |
| --- | --- | --- |
| `USE_STATIC_RUNTIME=ON` | yes | `/MT` / `/MTd` + `glfw3_mt.lib` |
| `USE_STATIC_RUNTIME=OFF` | | `/MD` / `/MDd` + `glfw3.lib` |

```bat
rem Dynamic CRT (requires matching VC++ redistributable on the target machine)
cmake -S . -B out/build/x64-debug-md -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUSE_STATIC_RUNTIME=OFF
cmake --build out/build/x64-debug-md
```

Reconfigure (or use a fresh binary dir) after changing this option so the CRT flags and GLFW lib stay in sync.

Root [`CMakeLists.txt`](CMakeLists.txt) pulls in GLFW with:

```cmake
add_subdirectory(glfw-3.4.bin.WIN64)
target_link_libraries(WindowsScreenCapture PRIVATE ... glfw)
```

---

## Notes on WinRT

WinRT capture uses manual COM / WinRT interop (`RoInitialize`, `CreateDirect3D11DeviceFromDXGIDevice`, etc.) rather than C++/WinRT language projections, so the app stays a standard native C++ executable without a UWP packaging chain.

---

## License

This project is licensed under the **MIT License**. See [`LICENSE.txt`](LICENSE.txt).

---

## Third-party acknowledgements

### GLFW

This project vendors and links **[GLFW](https://www.glfw.org/)** 3.4 (prebuilt binaries in [`glfw-3.4.bin.WIN64/`](glfw-3.4.bin.WIN64/)) for the default window/event host used by the preview UI.

GLFW is Copyright (c) 2002-2006 Marcus Geelnard and Copyright (c) 2006-2019 Camilla Löwy. It is provided under the zlib/libpng license. The full license text is in [`glfw-3.4.bin.WIN64/LICENSE.md`](glfw-3.4.bin.WIN64/LICENSE.md).

We gratefully acknowledge the GLFW authors and contributors. Use of GLFW here does not imply endorsement of this project by the GLFW project.
