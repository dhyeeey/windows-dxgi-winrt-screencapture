#pragma once
/**
 * @file FrameCaptureAccessStrategies.hpp
 * @brief Frame access strategies for window mirroring: GPU texture or CPU pixels.
 */

#include <d3d11.h>
#include <wrl/client.h>
#include <cstring>
#include <optional>
#include <vector>

using Microsoft::WRL::ComPtr;

enum class FrameType {
    CpuAccess,  ///< Raw BGRA pixels in system memory
    GpuDirect   ///< ID3D11Texture2D in VRAM
};

struct FrameData {
    FrameType type = FrameType::GpuDirect;

    // CPU (valid when type == CpuAccess)
    std::vector<uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    UINT stride = 0;

    // GPU (valid when type == GpuDirect)
    ComPtr<ID3D11Texture2D> d3dTexture;
};

class IFrameCaptureAccessStrategy {
public:
    virtual ~IFrameCaptureAccessStrategy() = default;
    virtual void Initialize(ID3D11Device* device, UINT width, UINT height) = 0;
    virtual std::optional<FrameData> ProcessFrame(ID3D11DeviceContext* context, ID3D11Texture2D* capturedTexture, int frameIndex) = 0;
    virtual void Shutdown() = 0;
};

/**
 * Keeps the frame on the GPU (GPU-to-GPU copy). Used by the letterbox presenter.
 */
class GpuDirectStrategy : public IFrameCaptureAccessStrategy {
public:
    void Initialize(ID3D11Device* device, UINT width, UINT height) override {
        device_ = device;
        width_ = width;
        height_ = height;

        textureDesc_.Width = width;
        textureDesc_.Height = height;
        textureDesc_.MipLevels = 1;
        textureDesc_.ArraySize = 1;
        textureDesc_.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDesc_.SampleDesc.Count = 1;
        textureDesc_.Usage = D3D11_USAGE_DEFAULT;
        textureDesc_.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        textureDesc_.CPUAccessFlags = 0;
        textureDesc_.MiscFlags = 0;
    }

    std::optional<FrameData> ProcessFrame(ID3D11DeviceContext* context, ID3D11Texture2D* capturedTexture, int /*frameIndex*/) override {
        ComPtr<ID3D11Texture2D> outputTexture;
        HRESULT hr = device_->CreateTexture2D(&textureDesc_, nullptr, &outputTexture);
        if (FAILED(hr)) return std::nullopt;

        context->CopyResource(outputTexture.Get(), capturedTexture);

        FrameData data;
        data.type = FrameType::GpuDirect;
        data.d3dTexture = outputTexture;
        data.width = width_;
        data.height = height_;
        return data;
    }

    void Shutdown() override {}

private:
    ComPtr<ID3D11Device> device_;
    D3D11_TEXTURE2D_DESC textureDesc_ = {};
    UINT width_ = 0, height_ = 0;
};

/**
 * Maps the frame to system RAM for CPU consumers.
 */
class CpuAccessStrategy : public IFrameCaptureAccessStrategy {
public:
    void Initialize(ID3D11Device* device, UINT width, UINT height) override {
        width_ = width;
        height_ = height;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        device->CreateTexture2D(&desc, nullptr, &stagingTexture_);
    }

    std::optional<FrameData> ProcessFrame(ID3D11DeviceContext* context, ID3D11Texture2D* capturedTexture, int /*frameIndex*/) override {
        context->CopyResource(stagingTexture_.Get(), capturedTexture);

        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(context->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &map))) {
            FrameData data;
            data.type = FrameType::CpuAccess;
            data.width = width_;
            data.height = height_;
            data.stride = map.RowPitch;
            data.pixels.resize(map.RowPitch * height_);
            memcpy(data.pixels.data(), map.pData, map.RowPitch * height_);
            context->Unmap(stagingTexture_.Get(), 0);
            return data;
        }
        return std::nullopt;
    }

    void Shutdown() override {}

private:
    ComPtr<ID3D11Texture2D> stagingTexture_;
    UINT width_ = 0, height_ = 0;
};
