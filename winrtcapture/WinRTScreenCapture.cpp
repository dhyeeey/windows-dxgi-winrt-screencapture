// ============================================================================
// Implementation
// ============================================================================

// WinRTScreenCapture.cpp
#include "WinRTScreenCapture.hpp"

namespace WinRTCapture {

    // ============================================================================
    // WinRTAPILoader Implementation
    // ============================================================================

    WinRTAPILoader& WinRTAPILoader::getInstance() {
        static WinRTAPILoader instance;
        return instance;
    }

    WinRTAPILoader::~WinRTAPILoader() {
        if (d3d11Module_) FreeLibrary(d3d11Module_);
        if (combaseModule_) FreeLibrary(combaseModule_);
        if (user32Module_) FreeLibrary(user32Module_);
    }

    bool WinRTAPILoader::initialize() {
        std::call_once(initFlag_, [this]() {
            loaded_ = loadLibraries() &&
                loadD3D11Functions() &&
                loadCombaseFunctions() &&
                loadUser32Functions();
            });

        return loaded_;
    }

    bool WinRTAPILoader::loadLibraries() {
        d3d11Module_ = LoadLibraryW(L"d3d11.dll");
        if (!d3d11Module_) return false;

        combaseModule_ = LoadLibraryW(L"combase.dll");
        if (!combaseModule_) {
            FreeLibrary(d3d11Module_);
            d3d11Module_ = nullptr;
            return false;
        }

        user32Module_ = LoadLibraryW(L"user32.dll");
        if (!user32Module_) {
            FreeLibrary(combaseModule_);
            FreeLibrary(d3d11Module_);
            combaseModule_ = nullptr;
            d3d11Module_ = nullptr;
            return false;
        }

        return true;
    }

    bool WinRTAPILoader::loadD3D11Functions() {
        pfnCreateDirect3D11Device_ = reinterpret_cast<PFN_CreateDirect3D11DeviceFromDXGIDevice>(
            GetProcAddress(d3d11Module_, "CreateDirect3D11DeviceFromDXGIDevice"));

        return pfnCreateDirect3D11Device_ != nullptr;
    }

    bool WinRTAPILoader::loadCombaseFunctions() {
        pfnRoInitialize_ = reinterpret_cast<PFN_RoInitialize>(
            GetProcAddress(combaseModule_, "RoInitialize"));
        pfnRoUninitialize_ = reinterpret_cast<PFN_RoUninitialize>(
            GetProcAddress(combaseModule_, "RoUninitialize"));
        pfnWindowsCreateString_ = reinterpret_cast<PFN_WindowsCreateString>(
            GetProcAddress(combaseModule_, "WindowsCreateString"));
        pfnWindowsDeleteString_ = reinterpret_cast<PFN_WindowsDeleteString>(
            GetProcAddress(combaseModule_, "WindowsDeleteString"));
        pfnRoGetActivationFactory_ = reinterpret_cast<PFN_RoGetActivationFactory>(
            GetProcAddress(combaseModule_, "RoGetActivationFactory"));

        return pfnRoInitialize_ && pfnRoUninitialize_ &&
            pfnWindowsCreateString_ && pfnWindowsDeleteString_ &&
            pfnRoGetActivationFactory_;
    }

    bool WinRTAPILoader::loadUser32Functions() {
        pfnSetThreadDpiAwarenessContext_ = reinterpret_cast<PFN_SetThreadDpiAwarenessContext>(
            GetProcAddress(user32Module_, "SetThreadDpiAwarenessContext"));

        return pfnSetThreadDpiAwarenessContext_ != nullptr;
    }

    HRESULT WinRTAPILoader::roInitialize(RO_INIT_TYPE initType) {
        return pfnRoInitialize_ ? pfnRoInitialize_(initType) : E_NOTIMPL;
    }

    void WinRTAPILoader::roUninitialize() {
        if (pfnRoUninitialize_) pfnRoUninitialize_();
    }

    HRESULT WinRTAPILoader::windowsCreateString(PCNZWCH sourceString, UINT32 length,
        HSTRING* string) {
        return pfnWindowsCreateString_ ?
            pfnWindowsCreateString_(sourceString, length, string) : E_NOTIMPL;
    }

    HRESULT WinRTAPILoader::windowsDeleteString(HSTRING string) {
        return pfnWindowsDeleteString_ ? pfnWindowsDeleteString_(string) : E_NOTIMPL;
    }

    HRESULT WinRTAPILoader::roGetActivationFactory(HSTRING activatableClassId,
        REFIID iid, void** factory) {
        return pfnRoGetActivationFactory_ ?
            pfnRoGetActivationFactory_(activatableClassId, iid, factory) : E_NOTIMPL;
    }

    HRESULT WinRTAPILoader::createDirect3D11DeviceFromDXGIDevice(
        IDXGIDevice* dxgiDevice, IInspectable** graphicsDevice) {
        return pfnCreateDirect3D11Device_ ?
            pfnCreateDirect3D11Device_(dxgiDevice, graphicsDevice) : E_NOTIMPL;
    }

    DPI_AWARENESS_CONTEXT WinRTAPILoader::setThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT context) {
        return pfnSetThreadDpiAwarenessContext_ ?
            pfnSetThreadDpiAwarenessContext_(context) : nullptr;
    }

    // ============================================================================
    // CaptureSessionInternal Implementation
    // ============================================================================

    CaptureSessionInternal::~CaptureSessionInternal() {
        cleanup();
    }

    void CaptureSessionInternal::cleanup() {
        // Close COM objects properly
        if (captureSession_) {
            ComPtr<IClosable> closable;
            captureSession_.As(&closable);
            if (closable) closable->Close();
            captureSession_.Reset();
        }

        if (framePool_) {
            ComPtr<IClosable> closable;
            framePool_.As(&closable);
            if (closable) closable->Close();
            framePool_.Reset();
        }

        if (captureItem_) {
            ComPtr<IClosable> closable;
            captureItem_.As(&closable);
            if (closable) closable->Close();
            captureItem_.Reset();
        }

        d3dDevice_.Reset();
    }

    HRESULT CaptureSessionInternal::initialize(ID3D11Device* device,
        HMONITOR monitor,
        HWND window,
        const CaptureSize& size) {
        auto& loader = WinRTAPILoader::getInstance();

        // Enable multithreading
        ComPtr<ID3D10Multithread> multithread;
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&multithread));
        if (FAILED(hr)) return hr;

        multithread->SetMultithreadProtected(TRUE);

        // Get GraphicsCaptureItem
        ComPtr<IGraphicsCaptureItemInterop> interop;
        hr = ActivationFactory<IGraphicsCaptureItemInterop>::getFactory(
            RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem, &interop);
        if (FAILED(hr)) return hr;

        if (monitor) {
            hr = interop->CreateForMonitor(monitor, IID_PPV_ARGS(&captureItem_));
        }
        else if (window) {
            hr = interop->CreateForWindow(window, IID_PPV_ARGS(&captureItem_));
        }
        else {
            return E_INVALIDARG;
        }

        if (FAILED(hr)) return hr;

        // Create Direct3DDevice
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        if (FAILED(hr)) return hr;

        ComPtr<IInspectable> inspectable;
        hr = loader.createDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), &inspectable);
        if (FAILED(hr)) return hr;

        hr = inspectable.As(&d3dDevice_);
        if (FAILED(hr)) return hr;

        // Get pool size
        hr = captureItem_->get_Size(&poolSize_);
        if (FAILED(hr)) return hr;

        // Create frame pool
        ComPtr<IDirect3D11CaptureFramePoolStatics> poolStatics;
        hr = ActivationFactory<IDirect3D11CaptureFramePoolStatics>::getFactory(
            RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool,
            &poolStatics);
        if (FAILED(hr)) return hr;

        ComPtr<IDirect3D11CaptureFramePoolStatics2> poolStatics2;
        hr = poolStatics.As(&poolStatics2);
        if (FAILED(hr)) return hr;

        hr = poolStatics2->CreateFreeThreaded(
            d3dDevice_.Get(),
            DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
            1,
            poolSize_,
            &framePool_);
        if (FAILED(hr)) return hr;

        // Create capture session
        hr = framePool_->CreateCaptureSession(captureItem_.Get(), &captureSession_);
        if (FAILED(hr)) return hr;

        return S_OK;
    }

    HRESULT CaptureSessionInternal::startCapture(bool showBorder) {
        if (!captureSession_) return E_FAIL;

        // Configure session
        ComPtr<IGraphicsCaptureSession2> session2;
        captureSession_.As(&session2);
        if (session2) {
            session2->put_IsCursorCaptureEnabled(FALSE);
        }

        ComPtr<IGraphicsCaptureSession3> session3;
        captureSession_.As(&session3);
        if (session3) {
            session3->put_IsBorderRequired(showBorder);
        }

        return captureSession_->StartCapture();
    }

    void CaptureSessionInternal::stopCapture() {
        cleanup();
    }

    HRESULT CaptureSessionInternal::tryGetNextFrame(ComPtr<IDirect3D11CaptureFrame>& frame) {
        if (!framePool_) return E_FAIL;
        return framePool_->TryGetNextFrame(&frame);
    }

    HRESULT CaptureSessionInternal::recreateFramePool(const SizeInt32& size) {
        if (!framePool_ || !d3dDevice_) return E_FAIL;

        poolSize_ = size;
        return framePool_->Recreate(
            d3dDevice_.Get(),
            DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
            1,
            poolSize_);
    }

    void CaptureSessionInternal::setCursorCaptureEnabled(bool enabled) {
        if (!captureSession_) return;

        ComPtr<IGraphicsCaptureSession2> session2;
        captureSession_.As(&session2);
        if (session2) {
            session2->put_IsCursorCaptureEnabled(enabled);
        }
    }

    void CaptureSessionInternal::setBorderRequired(bool required) {
        if (!captureSession_) return;

        ComPtr<IGraphicsCaptureSession3> session3;
        captureSession_.As(&session3);
        if (session3) {
            session3->put_IsBorderRequired(required);
        }
    }

    // ============================================================================
    // WindowEventMonitor Implementation
    // ============================================================================

    WindowEventMonitor& WindowEventMonitor::getInstance() {
        static WindowEventMonitor instance;
        return instance;
    }

    WindowEventMonitor::~WindowEventMonitor() {
        stop();
    }

    void WindowEventMonitor::registerCapture(WinRTCaptureDevice* device, HWND window) {
        std::lock_guard<std::mutex> lock(mutex_);
        windowMap_[window] = device;

        if (!started_) {
            start();
        }
    }

    void WindowEventMonitor::unregisterCapture(WinRTCaptureDevice* device) {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto it = windowMap_.begin(); it != windowMap_.end();) {
            if (it->second == device) {
                it = windowMap_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void WindowEventMonitor::start() {
        if (started_) return;

        eventHook_ = SetWinEventHook(
            EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
            nullptr, eventHookProc, 0, 0,
            WINEVENT_OUTOFCONTEXT);

        started_ = (eventHook_ != nullptr);
    }

    void WindowEventMonitor::stop() {
        if (eventHook_) {
            UnhookWinEvent(eventHook_);
            eventHook_ = nullptr;
        }
        started_ = false;
    }

    void CALLBACK WindowEventMonitor::eventHookProc(HWINEVENTHOOK hook, DWORD event,
        HWND hwnd, LONG idObject,
        LONG idChild, DWORD eventThread,
        DWORD eventTime) {
        if (event != EVENT_OBJECT_DESTROY ||
            idObject != OBJID_WINDOW ||
            idChild != INDEXID_CONTAINER ||
            !hwnd) {
            return;
        }

        getInstance().onWindowDestroyed(hwnd);
    }

    void WindowEventMonitor::onWindowDestroyed(HWND window) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = windowMap_.find(window);
        if (it != windowMap_.end() && it->second) {
            it->second->notifyWindowClosed();
        }
    }

    // ============================================================================
    // WinRTCaptureDevice Implementation
    // ============================================================================

    WinRTCaptureDevice::WinRTCaptureDevice(ID3D11Device* device,
        HMONITOR monitor,
        HWND window,
        bool clientOnly)
        : device_(device)
        , monitorHandle_(monitor)
        , windowHandle_(window)
        , clientOnly_(clientOnly) {

        if (!device) {
            throw std::invalid_argument("Device cannot be null");
        }

        if (!monitor && !window) {
            throw std::invalid_argument("Either monitor or window must be specified");
        }

        if (window && !IsWindow(window)) {
            throw std::invalid_argument("Invalid window handle");
        }

        device->GetImmediateContext(&context_);
        QueryPerformanceFrequency(&performanceFrequency_);

        // Determine capture mode
        if (monitor) {
            captureMode_ = CaptureMode::Monitor;
        }
        else if (clientOnly) {
            captureMode_ = CaptureMode::WindowClientAreaOnly;
        }
        else {
            captureMode_ = CaptureMode::Window;
        }

        // Start worker thread
        workerRunning_ = true;
        workerThread_ = std::thread(&WinRTCaptureDevice::workerThreadFunc, this);

        // Wait for configuration
        std::unique_lock<std::recursive_mutex> lock(mutex_);
        condVar_.wait(lock, [this]() { return session_ != nullptr || !workerRunning_; });
    }

    WinRTCaptureDevice::~WinRTCaptureDevice() {
        shutdown();
    }

    void WinRTCaptureDevice::shutdown() {
        workerRunning_ = false;
        condVar_.notify_all();

        if (workerThread_.joinable()) {
            workerThread_.join();
        }

        if (windowHandle_) {
            WindowEventMonitor::getInstance().unregisterCapture(this);
        }
    }

    void WinRTCaptureDevice::workerThreadFunc() {
        auto& loader = WinRTAPILoader::getInstance();

        // Set DPI awareness
        loader.setThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

        // Initialize WinRT
        HRESULT hr = loader.roInitialize(RO_INIT_MULTITHREADED);
        if (FAILED(hr)) {
            workerRunning_ = false;
            condVar_.notify_all();
            return;
        }

        // Configure capture
        configureCapture();

        // Register for window events if needed
        if (windowHandle_ && session_) {
            WindowEventMonitor::getInstance().registerCapture(this, windowHandle_);
        }

        condVar_.notify_all();

        // Keep thread alive for WinRT context
        while (workerRunning_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Cleanup
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            session_.reset();
        }

        loader.roUninitialize();
    }

    void WinRTCaptureDevice::configureCapture() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        session_ = std::make_unique<CaptureSessionInternal>();

        HRESULT hr = session_->initialize(device_.Get(),
            monitorHandle_,
            windowHandle_,
            captureSize_);

        if (FAILED(hr)) {
            session_.reset();
            return;
        }

        // Get initial sizes
        const auto& poolSize = session_->getPoolSize();
        poolSize_ = CaptureSize(poolSize.Width, poolSize.Height);
        textureSize_ = poolSize_;
        captureSize_ = poolSize_;

        // Adjust for client area if needed
        if (windowHandle_ && clientOnly_) {
            auto clientRect = calculateClientRect();
            if (clientRect) {
                captureSize_ = CaptureSize(clientRect->getWidth(), clientRect->getHeight());
            }
        }

        hr = session_->startCapture(showBorder_);
        if (FAILED(hr)) {
            session_.reset();
            return;
        }

        prepared_ = true;
    }

    CaptureResult WinRTCaptureDevice::prepare() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (prepared_ && session_) {
            return CaptureResult::Success;
        }

        return session_ ? CaptureResult::Success : CaptureResult::Error;
    }

    CaptureResult WinRTCaptureDevice::capture(ID3D11Texture2D* texture,
        const D3D11_BOX& cropBox,
        bool drawMouse) {
        std::unique_lock<std::recursive_mutex> lock(mutex_);

        if (!session_ || !prepared_) {
            return CaptureResult::Error;
        }

        if (session_->isClosed()) {
            return CaptureResult::ItemClosed;
        }

        if (flushing_) {
            return CaptureResult::Flushing;
        }

        // Update cursor capture if needed
        if (drawMouse != showMouse_) {
            showMouse_ = drawMouse;
            session_->setCursorCaptureEnabled(drawMouse);
        }

        // Wait for frame
        ComPtr<IDirect3D11CaptureFrame> frame;
        auto result = waitForFrame(frame, std::chrono::seconds(5));

        if (result != CaptureResult::Success) {
            return result;
        }

        if (!frame) {
            return CaptureResult::Timeout;
        }

        // Process frame
        return processFrame(frame, texture, cropBox);
    }

    CaptureResult WinRTCaptureDevice::waitForFrame(
        ComPtr<IDirect3D11CaptureFrame>& frame,
        std::chrono::milliseconds timeout) {

        auto startTime = std::chrono::steady_clock::now();

        while (true) {
            if (session_->isClosed()) {
                return CaptureResult::ItemClosed;
            }

            if (flushing_) {
                return CaptureResult::Flushing;
            }

            HRESULT hr = session_->tryGetNextFrame(frame);
            if (frame) {
                return CaptureResult::Success;
            }

            if (FAILED(hr)) {
                return CaptureResult::Error;
            }

            // Check timeout
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed >= timeout) {
                return CaptureResult::Timeout;
            }

            // Wait briefly before retry
            condVar_.wait_for(mutex_, std::chrono::milliseconds(1));
        }
    }

    CaptureResult WinRTCaptureDevice::processFrame(
        const ComPtr<IDirect3D11CaptureFrame>& frame,
        ID3D11Texture2D* texture,
        D3D11_BOX cropBox) {

        // Get frame content size
        SizeInt32 contentSize;
        HRESULT hr = frame->get_ContentSize(&contentSize);
        if (FAILED(hr)) {
            return CaptureResult::Error;
        }

        // Check for size change
        if (contentSize.Width != poolSize_.width ||
            contentSize.Height != poolSize_.height) {

            poolSize_ = CaptureSize(contentSize.Width, contentSize.Height);

            hr = session_->recreateFramePool(contentSize);
            if (FAILED(hr)) {
                return CaptureResult::Error;
            }

            return CaptureResult::SizeChanged;
        }

        // Get surface
        ComPtr<IDirect3DSurface> surface;
        hr = frame->get_Surface(&surface);
        if (FAILED(hr)) {
            return CaptureResult::Error;
        }

        // Get texture interface using explicit interface access
        ComPtr<IDirect3DDxgiInterfaceAccess> access;
        hr = surface.As(&access);
        if (FAILED(hr)) {
            return CaptureResult::Error;
        }

        // Get the ID3D11Texture2D interface explicitly
        ID3D11Texture2D* pCapturedTexture = nullptr;
        hr = access->GetInterface(__uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&pCapturedTexture));
        if (FAILED(hr)) {
            return CaptureResult::Error;
        }

        // Wrap in ComPtr for automatic cleanup
        ComPtr<ID3D11Texture2D> capturedTexture;
        capturedTexture.Attach(pCapturedTexture);

        // Get actual texture description
        D3D11_TEXTURE2D_DESC desc;
        capturedTexture->GetDesc(&desc);

        // Check for texture size change
        if (desc.Width != textureSize_.width || desc.Height != textureSize_.height) {
            textureSize_ = CaptureSize(desc.Width, desc.Height);

            if (!windowHandle_ || !clientOnly_) {
                captureSize_ = textureSize_;
            }

            return CaptureResult::SizeChanged;
        }

        // Adjust crop box for client area if needed
        if (windowHandle_ && clientOnly_) {
            auto clientRect = calculateClientRect();
            if (!clientRect) {
                return CaptureResult::Error;
            }

            // Update capture size if changed
            UINT newWidth = clientRect->getWidth();
            UINT newHeight = clientRect->getHeight();

            if (newWidth != captureSize_.width || newHeight != captureSize_.height) {
                captureSize_ = CaptureSize(newWidth, newHeight);
                return CaptureResult::SizeChanged;
            }

            // Adjust crop box
            cropBox.left += clientRect->left;
            cropBox.top += clientRect->top;
            cropBox.right += clientRect->left;
            cropBox.bottom += clientRect->top;

            // Clamp to texture bounds
            cropBox.left = std::min(desc.Width - 1, cropBox.left);
            cropBox.top = std::min(desc.Height - 1, cropBox.top);
            cropBox.right = std::min(desc.Width, cropBox.right);
            cropBox.bottom = std::min(desc.Height, cropBox.bottom);
        }

        // Copy to destination texture
        context_->CopySubresourceRegion(texture, 0, 0, 0, 0,
            capturedTexture.Get(), 0, &cropBox);

        return CaptureResult::Success;
    }

    std::optional<CaptureRect> WinRTCaptureDevice::calculateClientRect() {
        if (!windowHandle_) {
            return std::nullopt;
        }

        auto& loader = WinRTAPILoader::getInstance();

        RECT clientRect, boundRect;
        POINT clientPos = { 0, 0 };

        auto prevContext = loader.setThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

        BOOL success = GetClientRect(windowHandle_, &clientRect) &&
            DwmGetWindowAttribute(windowHandle_,
                DWMWA_EXTENDED_FRAME_BOUNDS,
                &boundRect,
                sizeof(RECT)) == S_OK &&
            ClientToScreen(windowHandle_, &clientPos);

        if (prevContext) {
            loader.setThreadDpiAwarenessContext(prevContext);
        }

        if (!success) {
            return std::nullopt;
        }

        UINT xOffset = 0;
        UINT yOffset = 0;

        if (clientPos.x > boundRect.left) {
            xOffset = clientPos.x - boundRect.left;
        }

        if (clientPos.y > boundRect.top) {
            yOffset = clientPos.y - boundRect.top;
        }

        UINT width = std::max(1U, static_cast<UINT>(clientRect.right - clientRect.left));
        UINT height = std::max(1U, static_cast<UINT>(clientRect.bottom - clientRect.top));

        return CaptureRect(xOffset, yOffset, xOffset + width, yOffset + height);
    }

    void WinRTCaptureDevice::setShowBorder(bool show) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        showBorder_ = show;

        if (session_) {
            session_->setBorderRequired(show);
        }
    }

    void WinRTCaptureDevice::flush() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        flushing_ = true;
        condVar_.notify_all();
    }

    void WinRTCaptureDevice::stopFlush() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        flushing_ = false;
        condVar_.notify_all();
    }

    CaptureSize WinRTCaptureDevice::getCaptureSize() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return captureSize_;
    }

    CaptureMode WinRTCaptureDevice::getCaptureMode() const {
        return captureMode_;
    }

    bool WinRTCaptureDevice::isClosed() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return session_ ? session_->isClosed() : true;
    }

    void WinRTCaptureDevice::notifyWindowClosed() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (session_) {
            session_->setClosed(true);
        }
        condVar_.notify_all();
    }

    // ============================================================================
    // WinRTCaptureManager Implementation
    // ============================================================================

    WinRTCaptureManager& WinRTCaptureManager::getInstance() {
        static WinRTCaptureManager instance;
        return instance;
    }

    std::shared_ptr<WinRTCaptureDevice> WinRTCaptureManager::createMonitorCapture(
        ID3D11Device* device, HMONITOR monitor) {

        if (!WinRTAPILoader::getInstance().initialize()) {
            return nullptr;
        }

        try {
            auto captureDevice = std::make_shared<WinRTCaptureDevice>(
                device, monitor, nullptr, false);

            std::lock_guard<std::mutex> lock(mutex_);
            devices_.push_back(captureDevice);

            return captureDevice;
        }
        catch (const std::exception&) {
            return nullptr;
        }
    }

    std::shared_ptr<WinRTCaptureDevice> WinRTCaptureManager::createWindowCapture(
        ID3D11Device* device, HWND window, bool clientOnly) {

        if (!WinRTAPILoader::getInstance().initialize()) {
            return nullptr;
        }

        if (!isWindowValid(window)) {
            return nullptr;
        }

        try {
            auto captureDevice = std::make_shared<WinRTCaptureDevice>(
                device, nullptr, window, clientOnly);

            std::lock_guard<std::mutex> lock(mutex_);
            devices_.push_back(captureDevice);

            return captureDevice;
        }
        catch (const std::exception&) {
            return nullptr;
        }
    }

    void WinRTCaptureManager::removeDevice(
        const std::shared_ptr<WinRTCaptureDevice>& device) {

        std::lock_guard<std::mutex> lock(mutex_);

        devices_.erase(
            std::remove_if(devices_.begin(), devices_.end(),
                [&device](const std::weak_ptr<WinRTCaptureDevice>& weak) {
                    auto ptr = weak.lock();
                    return !ptr || ptr == device;
                }),
            devices_.end());
    }

    void WinRTCaptureManager::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        devices_.clear();
    }

    // ============================================================================
    // Utility Functions Implementation
    // ============================================================================

    bool isWindowValid(HWND window) {
        return window && IsWindow(window);
    }

    std::vector<HMONITOR> enumerateMonitors() {
        std::vector<HMONITOR> monitors;

        EnumDisplayMonitors(nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
                auto& monitors = *reinterpret_cast<std::vector<HMONITOR>*>(lParam);
                monitors.push_back(monitor);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));

        return monitors;
    }

} // namespace WinRTCapture
