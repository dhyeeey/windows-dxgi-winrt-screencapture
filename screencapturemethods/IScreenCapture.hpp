#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <optional>
#include <iostream>

using namespace Microsoft::WRL;

// Unified enum for Capture Method
enum class CaptureMethod {
    DXGI,   // Desktop Duplication API 
    WinRT   // Windows.Graphics.Capture 
};

// Return status
enum class CaptureStatus {
    Success,
    Timeout,
    ReinitRequired, // Device lost or Size changed
    Error
};

/**
 * @class IScreenCapture
 * @brief Abstract interface unifying DXGI and WinRT capture methods.
 */
class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    // Initialize the capture session
    virtual bool Initialize(ID3D11Device* device, HMONITOR monitor) = 0;

    // Capture a frame into the target texture
    //virtual CaptureStatus AcquireFrame(ID3D11DeviceContext* context, ID3D11Texture2D* targetTexture) = 0;
    // Capture a frame into the target texture
    virtual CaptureStatus AcquireFrame(
        ID3D11DeviceContext* context,
        ID3D11Texture2D* targetTexture,
        ID3D11RenderTargetView* targetRTV = nullptr
    ) = 0;


    // Get width/height of the captured area
    virtual void GetSize(UINT& width, UINT& height) = 0;
};