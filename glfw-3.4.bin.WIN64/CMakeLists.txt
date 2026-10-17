cmake_minimum_required(VERSION 3.8)

# Prebuilt GLFW 3.4 (MSVC static /MT). Swap lib-vc* here if the toolchain changes.
set(_GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-vc2022")

add_library(glfw STATIC IMPORTED GLOBAL)
set_target_properties(glfw PROPERTIES
  IMPORTED_LOCATION "${_GLFW_LIB_DIR}/glfw3_mt.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${_GLFW_ROOT}/include"
)
