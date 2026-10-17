cmake_minimum_required (VERSION 3.8)

project ("WindowsScreenCapture")

# Link MSVC CRT statically (/MT, /MTd) when ON; dynamically (/MD, /MDd) when OFF.
# Also selects the matching prebuilt GLFW library (glfw3_mt.lib vs glfw3.lib).
option(USE_STATIC_RUNTIME "Link the MSVC C/C++ runtime statically (/MT)" ON)

# Must come after the option so glfw-3.4.bin.WIN64 can read USE_STATIC_RUNTIME.
add_subdirectory(glfw-3.4.bin.WIN64)

set(WIN_LIBS

    # --- DirectX / Graphics Libraries ---
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
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

if (MSVC)
    if (USE_STATIC_RUNTIME)
        message(STATUS "MSVC runtime: static (/MT, /MTd)")
        add_compile_options(
            $<$<CONFIG:Release>:/MT>
            $<$<CONFIG:Debug>:/MTd>
            $<$<CONFIG:RelWithDebInfo>:/MT>
            $<$<CONFIG:MinSizeRel>:/MT>
        )
    else()
        message(STATUS "MSVC runtime: dynamic (/MD, /MDd)")
        add_compile_options(
            $<$<CONFIG:Release>:/MD>
            $<$<CONFIG:Debug>:/MDd>
            $<$<CONFIG:RelWithDebInfo>:/MD>
            $<$<CONFIG:MinSizeRel>:/MD>
        )
    endif()
endif()

# Add source to this project's executable.
add_executable (WindowsScreenCapture
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
target_include_directories(WindowsScreenCapture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/presenter"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/glfw"
    "${CMAKE_CURRENT_SOURCE_DIR}/dxgicapture"
    "${CMAKE_CURRENT_SOURCE_DIR}/winrtcapture"
    "${CMAKE_CURRENT_SOURCE_DIR}/screencapturemethods"
    "${CMAKE_CURRENT_SOURCE_DIR}/frameaccessmethod"
)

target_link_libraries(WindowsScreenCapture
  PRIVATE
    ${WIN_LIBS}
    glfw
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET WindowsScreenCapture PROPERTY CXX_STANDARD 20)
endif()
