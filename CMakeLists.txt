# CMakeList.txt : CMake project for ScreenCaptureNew, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("ScreenCaptureNew")

# 1. Define ALL Required Libraries here (Just filenames)
set(WIN_LIBS
    # --- FFmpeg Libraries ---
    # avformat.lib
    # avcodec.lib
    # swscale.lib
    # avutil.lib
    # swresample.lib

    # --- DirectX / Graphics Libraries ---
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    windowscodecs.lib

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

    # --- NEWLY ADDED: Media Foundation Libs (Required for avcodec static linking) ---
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
add_executable (ScreenCaptureNew "ScreenCaptureNew.cpp" 
                                  
                                 "DxgiScreenCapture.hpp" 
                                 "DxgiScreenCapture.cpp" 
                                 "ImageSaver.hpp" "ThreadSafeQueue.hpp" "CaptureStrategies.hpp")

target_link_libraries(ScreenCaptureNew
  PRIVATE
    ${WIN_LIBS}
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ScreenCaptureNew PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
