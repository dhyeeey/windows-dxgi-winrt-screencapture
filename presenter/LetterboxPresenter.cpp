#include "LetterboxPresenter.hpp"

#include "D3dUtil.hpp"
#include "Log.hpp"

#include <sstream>

void LetterboxPresenter::Init(ID3D11Device* device, ID3D11DeviceContext* ctx, HWND hwnd,
    UINT initialWinW, UINT initialWinH, UINT captureW, UINT captureH) {
    srcW = captureW;
    srcH = captureH;
    winW = initialWinW;
    winH = initialWinH;

    Log("Render", "Initializing Presenter Swapchain...");

    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)), "QI IDXGIDevice");
    ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
    ComPtr<IDXGIFactory2> factory;
    ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&factory)), "GetParent IDXGIFactory2");

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = winW;
    desc.Height = winH;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ThrowIfFailed(factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, &swapchain),
        "CreateSwapChainForHwnd");
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&videoDevice)), "QI ID3D11VideoDevice");
    ThrowIfFailed(ctx->QueryInterface(IID_PPV_ARGS(&videoContext)), "QI ID3D11VideoContext");

    CreateVideoProcessor();
    CreateRTV(device);
    CreateOutputView();
    Log("Render", "Presenter Initialized Successfully.");
}

void LetterboxPresenter::CreateVideoProcessor() {
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = srcW;
    content.InputHeight = srcH;
    content.OutputWidth = srcW;
    content.OutputHeight = srcH;
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    ThrowIfFailed(videoDevice->CreateVideoProcessorEnumerator(&content, &vpEnum), "CreateVPEnum");
    ThrowIfFailed(videoDevice->CreateVideoProcessor(vpEnum.Get(), 0, &vp), "CreateVP");
}

void LetterboxPresenter::CreateRTV(ID3D11Device* device) {
    rtv.Reset();
    ComPtr<ID3D11Texture2D> backBuffer;
    ThrowIfFailed(swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer");
    ThrowIfFailed(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv), "CreateRTV");
}

void LetterboxPresenter::CreateOutputView() {
    cachedOutView.Reset();
    ComPtr<ID3D11Texture2D> backBuffer;
    ThrowIfFailed(swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer");
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outDesc{};
    outDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ThrowIfFailed(videoDevice->CreateVideoProcessorOutputView(backBuffer.Get(), vpEnum.Get(), &outDesc, &cachedOutView),
        "CreateVPOView");
}

void LetterboxPresenter::EnsureInputView(ID3D11Texture2D* srcTex) {
    if (!srcTex || (cachedSrc == srcTex && cachedInView)) return;
    cachedSrc = srcTex;
    cachedInView.Reset();
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inDesc{};
    inDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ThrowIfFailed(videoDevice->CreateVideoProcessorInputView(srcTex, vpEnum.Get(), &inDesc, &cachedInView),
        "CreateVPIView");
}

void LetterboxPresenter::Resize(ID3D11Device* device, ID3D11DeviceContext* /*ctx*/, UINT w, UINT h) {
    if (!swapchain || w == 0 || h == 0 || (w == winW && h == winH)) return;

    std::stringstream ss;
    ss << "Resizing SwapChain to " << w << "x" << h;
    Log("Render", ss.str());

    winW = w;
    winH = h;
    rtv.Reset();
    cachedOutView.Reset();
    ThrowIfFailed(swapchain->ResizeBuffers(0, winW, winH, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
    CreateRTV(device);
    CreateOutputView();
}

RECT LetterboxPresenter::CalcLetterboxRect() const {
    double srcAspect = (double)srcW / srcH;
    double winAspect = (double)winW / winH;
    UINT dstW, dstH;
    if (winAspect > srcAspect) {
        dstH = winH;
        dstW = (UINT)(dstH * srcAspect + 0.5);
    }
    else {
        dstW = winW;
        dstH = (UINT)(dstW / srcAspect + 0.5);
    }
    UINT left = (winW - dstW) / 2;
    UINT top = (winH - dstH) / 2;
    return { (LONG)left, (LONG)top, (LONG)(left + dstW), (LONG)(top + dstH) };
}

void LetterboxPresenter::PresentTexture(ID3D11Device* /*device*/, ID3D11DeviceContext* ctx, ID3D11Texture2D* srcTex) {
    if (!swapchain || !srcTex || !rtv) return;
    EnsureInputView(srcTex);
    const float black[4] = { 0, 0, 0, 1 };
    ctx->ClearRenderTargetView(rtv.Get(), black);
    RECT srcRect{ 0, 0, (LONG)srcW, (LONG)srcH };
    RECT dstRect = CalcLetterboxRect();
    videoContext->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &srcRect);
    videoContext->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &dstRect);
    videoContext->VideoProcessorSetOutputTargetRect(vp.Get(), TRUE, &dstRect);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = cachedInView.Get();
    videoContext->VideoProcessorBlt(vp.Get(), cachedOutView.Get(), 0, 1, &stream);
    swapchain->Present(1, 0);
}
