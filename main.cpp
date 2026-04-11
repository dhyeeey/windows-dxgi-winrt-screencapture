#define WIN32_LEAN_AND_MEAN
// --- GLFW ---
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>


#include "ScreenCaptureFactory.hpp"
#include "FrameCaptureAcessStrategies.hpp"  

// --- Logging Helper ---
// Prevents text from different threads getting jumbled on the console
std::mutex g_LogMutex;
void Log(const char* threadName, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    std::cout << "[" << threadName << "] " << msg << std::endl;
}


// --- Shared State ---
struct RenderContext {
    HWND hwnd = nullptr;
    std::atomic<bool> running{ true };

    // Resize signal
    std::atomic<bool> resizePending{ false };
    std::atomic<int> targetW{ 1280 };
    std::atomic<int> targetH{ 720 };
};

static void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << what << " failed (hr=0x" << std::hex << (unsigned)hr << std::dec << ")";
        Log("ERROR", ss.str());
        throw std::runtime_error(ss.str());
    }
}


struct LetterboxPresenter {
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoProcessorEnumerator> vpEnum;
    ComPtr<ID3D11VideoProcessor> vp;
    ComPtr<ID3D11VideoProcessorInputView>  cachedInView;
    ComPtr<ID3D11VideoProcessorOutputView> cachedOutView;
    ID3D11Texture2D* cachedSrc = nullptr;
    UINT winW = 0, winH = 0;
    UINT srcW = 0, srcH = 0;

    void Init(ID3D11Device* device, ID3D11DeviceContext* ctx, HWND hwnd, UINT initialWinW, UINT initialWinH, UINT captureW, UINT captureH) {
        srcW = captureW; srcH = captureH;
        winW = initialWinW; winH = initialWinH;

        Log("Render", "Initializing Presenter Swapchain...");

        ComPtr<IDXGIDevice> dxgiDevice;
        ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)), "QI IDXGIDevice");
        ComPtr<IDXGIAdapter> adapter;
        ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
        ComPtr<IDXGIFactory2> factory;
        ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&factory)), "GetParent IDXGIFactory2");

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = winW; desc.Height = winH;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ThrowIfFailed(factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, &swapchain), "CreateSwapChainForHwnd");
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&videoDevice)), "QI ID3D11VideoDevice");
        ThrowIfFailed(ctx->QueryInterface(IID_PPV_ARGS(&videoContext)), "QI ID3D11VideoContext");

        CreateVideoProcessor();
        CreateRTV(device);
        CreateOutputView();
        Log("Render", "Presenter Initialized Successfully.");
    }

    void CreateVideoProcessor() {
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputWidth = srcW; content.InputHeight = srcH;
        content.OutputWidth = srcW; content.OutputHeight = srcH;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        ThrowIfFailed(videoDevice->CreateVideoProcessorEnumerator(&content, &vpEnum), "CreateVPEnum");
        ThrowIfFailed(videoDevice->CreateVideoProcessor(vpEnum.Get(), 0, &vp), "CreateVP");
    }
    void CreateRTV(ID3D11Device* device) {
        rtv.Reset();
        ComPtr<ID3D11Texture2D> backBuffer;
        ThrowIfFailed(swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer");
        ThrowIfFailed(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv), "CreateRTV");
    }
    void CreateOutputView() {
        cachedOutView.Reset();
        ComPtr<ID3D11Texture2D> backBuffer;
        ThrowIfFailed(swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer");
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outDesc{};
        outDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ThrowIfFailed(videoDevice->CreateVideoProcessorOutputView(backBuffer.Get(), vpEnum.Get(), &outDesc, &cachedOutView), "CreateVPOView");
    }
    void EnsureInputView(ID3D11Texture2D* srcTex) {
        if (!srcTex || (cachedSrc == srcTex && cachedInView)) return;
        cachedSrc = srcTex; cachedInView.Reset();
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inDesc{};
        inDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        ThrowIfFailed(videoDevice->CreateVideoProcessorInputView(srcTex, vpEnum.Get(), &inDesc, &cachedInView), "CreateVPIView");
    }
    void Resize(ID3D11Device* device, ID3D11DeviceContext* /*ctx*/, UINT w, UINT h) {
        if (!swapchain || w == 0 || h == 0 || (w == winW && h == winH)) return;

        std::stringstream ss; ss << "Resizing SwapChain to " << w << "x" << h;
        Log("Render", ss.str());

        winW = w; winH = h;
        rtv.Reset(); cachedOutView.Reset();
        ThrowIfFailed(swapchain->ResizeBuffers(0, winW, winH, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
        CreateRTV(device); CreateOutputView();
    }
    RECT CalcLetterboxRect() const {
        double srcAspect = (double)srcW / srcH;
        double winAspect = (double)winW / winH;
        UINT dstW, dstH;
        if (winAspect > srcAspect) { dstH = winH; dstW = (UINT)(dstH * srcAspect + 0.5); }
        else { dstW = winW; dstH = (UINT)(dstW / srcAspect + 0.5); }
        UINT left = (winW - dstW) / 2; UINT top = (winH - dstH) / 2;
        return { (LONG)left, (LONG)top, (LONG)(left + dstW), (LONG)(top + dstH) };
    }
    void PresentTexture(ID3D11Device* /*device*/, ID3D11DeviceContext* ctx, ID3D11Texture2D* srcTex) {
        if (!swapchain || !srcTex || !rtv) return;
        EnsureInputView(srcTex);
        const float black[4] = { 0,0,0,1 };
        ctx->ClearRenderTargetView(rtv.Get(), black);
        RECT srcRect{ 0, 0, (LONG)srcW, (LONG)srcH };
        RECT dstRect = CalcLetterboxRect();
        videoContext->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &srcRect);
        videoContext->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &dstRect);
        videoContext->VideoProcessorSetOutputTargetRect(vp.Get(), TRUE, &dstRect);
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE; stream.pInputSurface = cachedInView.Get();
        videoContext->VideoProcessorBlt(vp.Get(), cachedOutView.Get(), 0, 1, &stream);
        swapchain->Present(1, 0);
    }
};

// ==========================================
// RENDER THREAD LOGIC
// ==========================================
void RenderThreadMain(RenderContext* threadCtx, CaptureMethod CAPTURE_METHOD, FrameType FRAME_TYPE) {
    Log("Render", "Thread Started.");

    // Initialize D3D11
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &device, &featureLevel, &context);

    if (FAILED(hr)) { Log("Render", "CRITICAL: D3D11CreateDevice Failed."); return; }
    Log("Render", "D3D11 Device Created.");

    // Init Capture Session
    Log("Render", "Initializing Capture Session...");
    POINT pt = { 0, 0 };
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    auto captureSession = ScreenCaptureFactory::Create(CAPTURE_METHOD);

    if (!captureSession || !captureSession->Initialize(device.Get(), hMonitor)) {
        Log("Render", "CRITICAL: Capture Session Initialize Failed."); return;
    }

    UINT width, height;
    captureSession->GetSize(width, height);

    std::stringstream ss; ss << "Capture Session Ready. Resolution: " << width << "x" << height;
    Log("Render", ss.str());

    // Setup Target Texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width; texDesc.Height = height; texDesc.MipLevels = 1; texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> targetTexture;
    device->CreateTexture2D(&texDesc, nullptr, &targetTexture);
    Log("Render", "Target Texture Created.");

    ComPtr<ID3D11RenderTargetView> targetRTV;
    ThrowIfFailed(device->CreateRenderTargetView(targetTexture.Get(), nullptr, &targetRTV),
        "CreateRenderTargetView(targetTexture)");
    Log("Render", "Target RTV Created (for cursor compositing).");

    // Setup Strategy
    std::unique_ptr<IFrameCaptureAccessStrategy> strategy;
    if (FRAME_TYPE == FrameType::GpuDirect) {
        strategy = std::make_unique<GpuDirectStrategy>();
        Log("Render", "Strategy Selected: GPU Direct");
    }
    else if (FRAME_TYPE == FrameType::CpuAccess) {
        strategy = std::make_unique<CpuAccessStrategy>();
        Log("Render", "Strategy Selected: CPU Access");
    }
    else {
        strategy = std::make_unique<FrametoImageSavingMultiThreadedStrategy>();
        Log("Render", "Strategy Selected: Save to Image");
    }

    strategy->Initialize(device.Get(), width, height);
    Log("Render", "Strategy Initialized.");

    // Setup Presenter
    LetterboxPresenter presenter;
    int currentW = threadCtx->targetW;
    int currentH = threadCtx->targetH;

    presenter.Init(device.Get(), context.Get(), threadCtx->hwnd,
        (UINT)currentW, (UINT)currentH, width, height);

    Log("Render", "Entering Render Loop...");

    // --- RENDER LOOP ---
    while (threadCtx->running) {

        if (threadCtx->resizePending) {
            int w = threadCtx->targetW;
            int h = threadCtx->targetH;
            if (w > 0 && h > 0) {
                presenter.Resize(device.Get(), context.Get(), (UINT)w, (UINT)h);
            }
            threadCtx->resizePending = false;
        }

        CaptureStatus status = captureSession->AcquireFrame(context.Get(), targetTexture.Get(), targetRTV.Get());


        if (status == CaptureStatus::Success) {
            strategy->ProcessFrame(context.Get(), targetTexture.Get(), 0);
            presenter.PresentTexture(device.Get(), context.Get(), targetTexture.Get());
        }
        else if (status == CaptureStatus::ReinitRequired) {
            Log("Render", "WARNING: Capture Reinit Required (Not Implemented).");
        }
    }

    Log("Render", "Loop Finished. Shutting down...");
    strategy->Shutdown();
    Log("Render", "Strategy Shutdown Complete.");
    Log("Render", "Thread Exiting.");
}


// ==========================================
// MAIN (UI THREAD)
// ==========================================

static RenderContext* g_RenderCtx = nullptr;

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    if (g_RenderCtx && width > 0 && height > 0) {
        g_RenderCtx->targetW = width;
        g_RenderCtx->targetH = height;
        g_RenderCtx->resizePending = true;
    }
}

int main() {
    RenderContext ctx;
    g_RenderCtx = &ctx;

    Log("Main", "Initializing GLFW...");
    if (!glfwInit()) {
        Log("Main", "GLFW Init Failed.");
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    Log("Main", "Creating Window...");
    GLFWwindow* win = glfwCreateWindow(1280, 720, "Capture (Multithreaded + Log)", nullptr, nullptr);
    if (!win) {
        Log("Main", "Window Creation Failed.");
        glfwTerminate();
        return -1;
    }

    ctx.hwnd = glfwGetWin32Window(win);

    glfwSetFramebufferSizeCallback(win, framebuffer_size_callback);

    Log("Main", "Starting Render Thread...");
    std::thread renderThread(RenderThreadMain, &ctx, CaptureMethod::DXGI, FrameType::GpuDirect);

    Log("Main", "Entering Event Loop.");

    while (!glfwWindowShouldClose(win)) {
        glfwWaitEvents();
    }

    Log("Main", "Window Close Requested.");

    // Cleanup
    Log("Main", "Signaling Render Thread to Stop...");
    ctx.running = false;

    if (renderThread.joinable()) {
        Log("Main", "Waiting for Render Thread to Join...");
        renderThread.join();
        Log("Main", "Render Thread Joined.");
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    Log("Main", "Application Terminated.");

    return 0;
}