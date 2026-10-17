cmake_minimum_required (VERSION 3.8)

project ("WindowsDxgiScreenCapture")

add_subdirectory(glfw-3.4.bin.WIN64)

set(WIN_LIBS

    # --- DirectX / Graphics Libraries ---
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
    windowscodecs.lib
    windowsapp.lib

    winmm.lib
    opengl32.lib

    # --- Windows System Libraries ---
    Ws2_32.lib
    Bcrypt.lib
    Secur32.lib
    Shlwapi.lib
    Crypt32.lib
    Userenv.lib
    Advapi32.lib
    Shell32.lib
    Ole32.lib
    OleAut32.lib
    user32.lib

    strmiids.lib   # For IID_ICodecAPI
    mfuuid.lib     # For IID_IMFMediaEventGenerator, IID_IMFTransform
    mfplat.lib     # Often required alongside mfuuid
)

# 3. Use static runtime: /MT for Release, /MTd for Debug
if (MSVC)
    add_compile_options(
        $<$<CONFIG:Release>:/MT>
        $<$<CONFIG:Debug>:/MTd>
    )
endif()

# Add source to this project's executable.
add_executable (WindowsDxgiScreenCapture
    "main.cpp"
    "app/CaptureApp.cpp"
    "app/CaptureApp.hpp"
    "app/RenderLoop.cpp"
    "app/RenderLoop.hpp"
    "app/RenderContext.hpp"
    "app/Log.hpp"
    "app/D3dUtil.hpp"
    "presenter/LetterboxPresenter.cpp"
    "presenter/LetterboxPresenter.hpp"
    "platform/IWindowHost.hpp"
    "platform/glfw/GlfwWindowHost.cpp"
    "platform/glfw/GlfwWindowHost.hpp"
    "dxgicapture/DxgiScreenCapture.hpp"
    "dxgicapture/DxgiScreenCapture.cpp"
    "util/ImageSaver.hpp"
    "util/ThreadSafeQueue.hpp"
    "frameaccessmethod/FrameCaptureAcessStrategies.hpp"
    "winrtcapture/WinRTScreenCapture.hpp"
    "winrtcapture/WinRTScreenCapture.cpp"
    "screencapturemethods/IScreenCapture.hpp"
    "dxgicapture/DxgiCaptureWrapper.hpp"
    "winrtcapture/WinRTCaptureWrapper.hpp"
    "screencapturemethods/ScreenCaptureFactory.hpp"
)

# =========================================================================
# 4. ADD INCLUDE DIRECTORIES HERE
# =========================================================================
target_include_directories(WindowsDxgiScreenCapture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/presenter"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/glfw"
    "${CMAKE_CURRENT_SOURCE_DIR}/dxgicapture"
    "${CMAKE_CURRENT_SOURCE_DIR}/winrtcapture"
    "${CMAKE_CURRENT_SOURCE_DIR}/screencapturemethods"
    "${CMAKE_CURRENT_SOURCE_DIR}/util"
    "${CMAKE_CURRENT_SOURCE_DIR}/frameaccessmethod"
)

target_link_libraries(WindowsDxgiScreenCapture
  PRIVATE
    ${WIN_LIBS}
    glfw
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET WindowsDxgiScreenCapture PROPERTY CXX_STANDARD 20)
endif()
