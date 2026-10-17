#pragma once

#include "Log.hpp"

#include <d3d11.h>
#include <sstream>
#include <stdexcept>
#include <string>

inline void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << what << " failed (hr=0x" << std::hex << (unsigned)hr << std::dec << ")";
        Log("ERROR", ss.str());
        throw std::runtime_error(ss.str());
    }
}
