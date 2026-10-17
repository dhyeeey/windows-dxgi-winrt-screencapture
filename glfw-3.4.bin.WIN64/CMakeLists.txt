cmake_minimum_required(VERSION 3.8)

# Prebuilt GLFW 3.4 for MSVC. Lib choice must match the app CRT:
#   USE_STATIC_RUNTIME=ON  -> glfw3_mt.lib  (/MT)
#   USE_STATIC_RUNTIME=OFF -> glfw3.lib     (/MD)
if (NOT DEFINED USE_STATIC_RUNTIME)
    set(USE_STATIC_RUNTIME ON)
endif()

set(_GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-vc2022")

if (USE_STATIC_RUNTIME)
    set(_GLFW_LIB "${_GLFW_LIB_DIR}/glfw3_mt.lib")
else()
    set(_GLFW_LIB "${_GLFW_LIB_DIR}/glfw3.lib")
endif()

if (NOT EXISTS "${_GLFW_LIB}")
    message(FATAL_ERROR "GLFW library not found: ${_GLFW_LIB}")
endif()

message(STATUS "GLFW library: ${_GLFW_LIB}")

add_library(glfw STATIC IMPORTED GLOBAL)
set_target_properties(glfw PROPERTIES
  IMPORTED_LOCATION "${_GLFW_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${_GLFW_ROOT}/include"
)
