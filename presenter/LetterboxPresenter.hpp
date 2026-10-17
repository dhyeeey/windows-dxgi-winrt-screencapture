#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct LetterboxPresenter {
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoProcessorEnumerator> vpEnum;
    ComPtr<ID3D11VideoProcessor> vp;
    ComPtr<ID3D11VideoProcessorInputView> cachedInView;
    ComPtr<ID3D11VideoProcessorOutputView> cachedOutView;
    ID3D11Texture2D* cachedSrc = nullptr;
    UINT winW = 0, winH = 0;
    UINT srcW = 0, srcH = 0;

    void Init(ID3D11Device* device, ID3D11DeviceContext* ctx, HWND hwnd,
        UINT initialWinW, UINT initialWinH, UINT captureW, UINT captureH);
    void CreateVideoProcessor();
    void CreateRTV(ID3D11Device* device);
    void CreateOutputView();
    void EnsureInputView(ID3D11Texture2D* srcTex);
    void Resize(ID3D11Device* device, ID3D11DeviceContext* ctx, UINT w, UINT h);
    RECT CalcLetterboxRect() const;
    void PresentTexture(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* srcTex);
};
