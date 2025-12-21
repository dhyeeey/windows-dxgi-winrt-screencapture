#include "DxgiScreenCapture.hpp"

namespace DxgiCapture {

    static constexpr UINT NUM_VERTICES = 6;
    static constexpr UINT BYTES_PER_PIXEL = 4;

    // Expected error arrays
    static const std::vector<HRESULT> SYSTEM_TRANSITIONS_EXPECTED_ERRORS = {
        DXGI_ERROR_DEVICE_REMOVED,
        DXGI_ERROR_ACCESS_LOST,
        static_cast<HRESULT>(WAIT_ABANDONED)
    };

    static const std::vector<HRESULT> CREATE_DUPLICATION_EXPECTED_ERRORS = {
        DXGI_ERROR_DEVICE_REMOVED,
        static_cast<HRESULT>(E_ACCESSDENIED),
        DXGI_ERROR_SESSION_DISCONNECTED
    };

    static const std::vector<HRESULT> FRAME_INFO_EXPECTED_ERRORS = {
        DXGI_ERROR_DEVICE_REMOVED,
        DXGI_ERROR_ACCESS_LOST
    };

    // ============================================================================
    // Shader Source Code
    // ============================================================================

    const char* VS_SOURCE = R"(
    struct VS_INPUT {
        float3 Pos : POSITION;
        float2 Tex : TEXCOORD0;
    };

    struct PS_INPUT {
        float4 Pos : SV_POSITION;
        float2 Tex : TEXCOORD0;
    };

    PS_INPUT VS(VS_INPUT input) {
        PS_INPUT output;
        output.Pos = float4(input.Pos, 1.0f);
        output.Tex = input.Tex;
        return output;
    }
    )";

    const char* PS_SOURCE = R"(
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);

    struct PS_INPUT {
        float4 Pos : SV_POSITION;
        float2 Tex : TEXCOORD0;
    };

    float4 PS(PS_INPUT input) : SV_Target {
        return tex.Sample(samp, input.Tex);
    }
    )";

    // Helper for compilation
    HRESULT CompileShader(const char* source, const char* entryPoint, const char* profile, ID3DBlob** blob) {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG;
#endif
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
            entryPoint, profile, flags, 0, blob, &errorBlob);
        if (FAILED(hr) && errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return hr;
    }

    // ============================================================================
    // PointerInfo Implementation
    // ============================================================================

    PointerInfo::PointerInfo()
        : shapeInfo_{}, position_{ 0, 0 }, visible_(false) {
        lastTimestamp_.QuadPart = 0;
    }

    void PointerInfo::reallocBuffer(UINT size) {
        if (size <= shapeBuffer_.size()) return;
        shapeBuffer_.resize(size);
    }

    void PointerInfo::reset() {
        shapeBuffer_.clear();
        shapeInfo_ = {};
        position_ = { 0, 0 };
        visible_ = false;
        lastTimestamp_.QuadPart = 0;
    }

    // ============================================================================
    // ShaderResources Implementation
    // ============================================================================

    bool ShaderResources::initialize(ID3D11Device* device) {
        if (!device) return false;

        if (!createVertexShader(device)) return false;
        if (!createPixelShader(device)) return false;
        if (!createSampler(device)) return false;
        if (!createRasterizer(device)) return false;
        if (!createBlendState(device)) return false;

        return true;
    }

    bool ShaderResources::createVertexShader(ID3D11Device* device) {
        ComPtr<ID3DBlob> vsBlob;
        if (FAILED(CompileShader(VS_SOURCE, "VS", "vs_5_0", &vsBlob))) {
            return false;
        }

        if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr, &vertexShader_))) {
            return false;
        }

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        if (FAILED(device->CreateInputLayout(layout, ARRAYSIZE(layout),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            &inputLayout_))) {
            return false;
        }

        return true;
    }

    bool ShaderResources::createPixelShader(ID3D11Device* device) {
        ComPtr<ID3DBlob> psBlob;
        if (FAILED(CompileShader(PS_SOURCE, "PS", "ps_5_0", &psBlob))) {
            return false;
        }

        if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr, &pixelShader_))) {
            return false;
        }

        return true;
    }

    bool ShaderResources::createSampler(ID3D11Device* device) {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        return SUCCEEDED(device->CreateSamplerState(&samplerDesc, &sampler_));
    }

    bool ShaderResources::createRasterizer(ID3D11Device* device) {
        D3D11_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.FrontCounterClockwise = FALSE;
        rasterizerDesc.DepthClipEnable = TRUE;

        return SUCCEEDED(device->CreateRasterizerState(&rasterizerDesc, &rasterizer_));
    }

    bool ShaderResources::createBlendState(ID3D11Device* device) {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        return SUCCEEDED(device->CreateBlendState(&blendDesc, &blendState_));
    }

    // ============================================================================
    // FrameMetadata Implementation
    // ============================================================================

    void FrameMetadata::reallocBuffer(UINT size) {
        if (size <= buffer_.size()) return;
        buffer_.resize(size);
    }

    // ============================================================================
    // DesktopDuplicator Implementation
    // ============================================================================

    DesktopDuplicator::DesktopDuplicator()
        : outputDesc_{}, mouseTexDesc_{} {
    }

    DesktopDuplicator::~DesktopDuplicator() {
        if (keyedMutex_) {
            keyedMutex_->ReleaseSync(0);
        }
    }

    CaptureResult DesktopDuplicator::initialize(ID3D11Device* device, HMONITOR monitor) {
        if (!device) return CaptureResult::Error;

        device_ = device;
        device->GetImmediateContext(&context_);

        // Initialize shader resources
        shaderResources_ = std::make_unique<ShaderResources>();
        if (!shaderResources_->initialize(device)) {
            return CaptureResult::Error;
        }

        // Initialize metadata
        metadata_ = std::make_unique<FrameMetadata>();

        // Initialize duplication
        auto result = initializeDuplication(monitor);
        if (result != CaptureResult::Success) {
            return result;
        }

        // Create shared texture
        if (!createSharedTexture()) {
            return CaptureResult::Error;
        }

        return CaptureResult::Success;
    }

    CaptureResult DesktopDuplicator::initializeDuplication(HMONITOR monitor) {
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIOutput1> output1;

        HRESULT hr = findOutputForMonitor(monitor, adapter, output);
        if (FAILED(hr)) {
            return CaptureResult::Error;
        }

        hr = output.As(&output1);
        if (FAILED(hr)) {
            return CaptureResult::Error;
        }

        // Set thread desktop for proper capture
        HDESK hdesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (hdesk) {
            SetThreadDesktop(hdesk);
            CloseDesktop(hdesk);
        }

        // Create output duplication
        hr = output1->DuplicateOutput(device_.Get(), &duplication_);
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
                return CaptureResult::Error;
            }
            if (hr == DXGI_ERROR_UNSUPPORTED) {
                return CaptureResult::Error;
            }
            return checkHResult(device_.Get(), hr, CREATE_DUPLICATION_EXPECTED_ERRORS);
        }

        duplication_->GetDesc(&outputDesc_);
        return CaptureResult::Success;
    }

    bool DesktopDuplicator::createSharedTexture() {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = outputDesc_.ModeDesc.Width;
        texDesc.Height = outputDesc_.ModeDesc.Height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &sharedTexture_);
        if (FAILED(hr)) return false;

        hr = sharedTexture_.As(&keyedMutex_);
        if (FAILED(hr)) return false;

        hr = keyedMutex_->AcquireSync(0, INFINITE);
        if (FAILED(hr)) return false;

        return true;
    }

    CaptureResult DesktopDuplicator::capture() {
        ComPtr<ID3D11Texture2D> texture;
        UINT moveCount = 0, dirtyCount = 0;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        bool timeout = false;

        auto result = acquireFrame(texture, moveCount, dirtyCount, frameInfo, timeout);
        if (result != CaptureResult::Success) {
            return result;
        }

        if (timeout) {
            return CaptureResult::Timeout;
        }

        // Get mouse pointer info
        result = getMouseInfo(frameInfo);
        if (result != CaptureResult::Success) {
            duplication_->ReleaseFrame();
            return result;
        }

        // Process frame
        result = processFrame(texture.Get(), sharedTexture_.Get(),
            moveCount, dirtyCount, frameInfo);

        HRESULT hr = duplication_->ReleaseFrame();
        if (FAILED(hr)) {
            return checkHResult(device_.Get(), hr, FRAME_INFO_EXPECTED_ERRORS);
        }

        return result;
    }

    CaptureResult DesktopDuplicator::acquireFrame(ComPtr<ID3D11Texture2D>& texture,
        UINT& moveCount,
        UINT& dirtyCount,
        DXGI_OUTDUPL_FRAME_INFO& frameInfo,
        bool& timeout) {
        ComPtr<IDXGIResource> resource;

        HRESULT hr = duplication_->AcquireNextFrame(0, &frameInfo, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            timeout = true;
            return CaptureResult::Success;
        }

        timeout = false;
        moveCount = 0;
        dirtyCount = 0;

        if (FAILED(hr)) {
            return checkHResult(device_.Get(), hr, FRAME_INFO_EXPECTED_ERRORS);
        }

        hr = resource.As(&texture);
        if (FAILED(hr)) {
            return CaptureResult::Error;
        }

        // Get metadata
        if (frameInfo.TotalMetadataBufferSize > 0) {
            UINT bufSize = frameInfo.TotalMetadataBufferSize;
            metadata_->reallocBuffer(bufSize);

            // Get move rectangles
            hr = duplication_->GetFrameMoveRects(bufSize,
                reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(metadata_->getBuffer()),
                &bufSize);
            if (FAILED(hr)) {
                return checkHResult(nullptr, hr, FRAME_INFO_EXPECTED_ERRORS);
            }

            moveCount = bufSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);

            // Get dirty rectangles
            BYTE* dirtyRects = metadata_->getBuffer() + bufSize;
            bufSize = frameInfo.TotalMetadataBufferSize - bufSize;

            hr = duplication_->GetFrameDirtyRects(bufSize,
                reinterpret_cast<RECT*>(dirtyRects), &bufSize);
            if (FAILED(hr)) {
                moveCount = 0;
                return checkHResult(nullptr, hr, FRAME_INFO_EXPECTED_ERRORS);
            }

            dirtyCount = bufSize / sizeof(RECT);
        }

        return CaptureResult::Success;
    }

    CaptureResult DesktopDuplicator::getMouseInfo(const DXGI_OUTDUPL_FRAME_INFO& frameInfo) {
        if (frameInfo.LastMouseUpdateTime.QuadPart == 0) {
            return CaptureResult::Success;
        }

        pointerInfo_.getPosition().x = frameInfo.PointerPosition.Position.x;
        pointerInfo_.getPosition().y = frameInfo.PointerPosition.Position.y;
        pointerInfo_.getLastTimestamp() = frameInfo.LastMouseUpdateTime;
        pointerInfo_.setVisible(frameInfo.PointerPosition.Visible != 0);

        if (frameInfo.PointerShapeBufferSize == 0) {
            return CaptureResult::Success;
        }

        pointerInfo_.reallocBuffer(frameInfo.PointerShapeBufferSize);

        UINT dummy;
        HRESULT hr = duplication_->GetFramePointerShape(
            frameInfo.PointerShapeBufferSize,
            pointerInfo_.getShapeBuffer(),
            &dummy,
            &pointerInfo_.getShapeInfo());

        if (FAILED(hr)) {
            return checkHResult(device_.Get(), hr, FRAME_INFO_EXPECTED_ERRORS);
        }

        return CaptureResult::Success;
    }

    CaptureResult DesktopDuplicator::processFrame(ID3D11Texture2D* acquiredTexture,
        ID3D11Texture2D* sharedTexture,
        UINT moveCount,
        UINT dirtyCount,
        const DXGI_OUTDUPL_FRAME_INFO& frameInfo) {
        if (frameInfo.TotalMetadataBufferSize == 0) {
            return CaptureResult::Success;
        }

        if (moveCount > 0) {
            auto result = copyMoveRects(sharedTexture,
                reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(metadata_->getBuffer()),
                moveCount);
            if (result != CaptureResult::Success) {
                return result;
            }
        }

        if (dirtyCount > 0) {
            RECT* dirtyRects = reinterpret_cast<RECT*>(
                metadata_->getBuffer() + (moveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)));

            auto result = copyDirtyRects(acquiredTexture, sharedTexture,
                dirtyRects, dirtyCount);
            if (result != CaptureResult::Success) {
                return result;
            }
        }

        return CaptureResult::Success;
    }

    CaptureResult DesktopDuplicator::copyToTexture(ID3D11Device* targetDevice,
        ID3D11Texture2D* texture,
        const D3D11_BOX& cropBox) {
        ComPtr<ID3D11Texture2D> tex = sharedTexture_;
        ComPtr<IDXGIKeyedMutex> otherKeyedMutex;
        ComPtr<ID3D11DeviceContext> targetContext;

        targetDevice->GetImmediateContext(&targetContext);

        // If different device, need to open shared resource
        if (targetDevice != device_.Get()) {
            ComPtr<IDXGIResource> dxgiResource;
            HANDLE sharedHandle;

            HRESULT hr = sharedTexture_.As(&dxgiResource);
            if (FAILED(hr)) return CaptureResult::Error;

            hr = dxgiResource->GetSharedHandle(&sharedHandle);
            if (FAILED(hr)) return CaptureResult::Error;

            hr = targetDevice->OpenSharedResource(sharedHandle,
                IID_PPV_ARGS(&tex));
            if (FAILED(hr)) return CaptureResult::Error;

            hr = tex.As(&otherKeyedMutex);
            if (FAILED(hr)) return CaptureResult::Error;

            keyedMutex_->ReleaseSync(0);
            otherKeyedMutex->AcquireSync(0, INFINITE);
        }

        targetContext->CopySubresourceRegion(texture, 0, 0, 0, 0,
            tex.Get(), 0, &cropBox);

        if (otherKeyedMutex) {
            otherKeyedMutex->ReleaseSync(0);
            keyedMutex_->AcquireSync(0, INFINITE);
        }

        return CaptureResult::Success;
    }

    void DesktopDuplicator::getSize(UINT& width, UINT& height) const {
        width = outputDesc_.ModeDesc.Width;
        height = outputDesc_.ModeDesc.Height;
    }

    CaptureResult DesktopDuplicator::copyMoveRects(ID3D11Texture2D* surface,
        DXGI_OUTDUPL_MOVE_RECT* moveBuffer,
        UINT moveCount) {
        D3D11_TEXTURE2D_DESC fullDesc;
        surface->GetDesc(&fullDesc);

        // Create intermediate texture if needed
        if (!moveTexture_) {
            D3D11_TEXTURE2D_DESC moveDesc = fullDesc;
            moveDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
            moveDesc.MiscFlags = 0;

            HRESULT hr = device_->CreateTexture2D(&moveDesc, nullptr, &moveTexture_);
            if (FAILED(hr)) return CaptureResult::Error;
        }

        for (UINT i = 0; i < moveCount; i++) {
            RECT srcRect, dstRect;
            setMoveRect(srcRect, dstRect, moveBuffer[i],
                fullDesc.Width, fullDesc.Height);

            D3D11_BOX box;
            box.left = srcRect.left;
            box.top = srcRect.top;
            box.front = 0;
            box.right = srcRect.right;
            box.bottom = srcRect.bottom;
            box.back = 1;

            context_->CopySubresourceRegion(moveTexture_.Get(), 0,
                srcRect.left, srcRect.top, 0,
                surface, 0, &box);

            context_->CopySubresourceRegion(surface, 0,
                dstRect.left, dstRect.top, 0,
                moveTexture_.Get(), 0, &box);
        }

        return CaptureResult::Success;
    }

    CaptureResult DesktopDuplicator::copyDirtyRects(ID3D11Texture2D* srcSurface,
        ID3D11Texture2D* dstSurface,
        RECT* dirtyBuffer,
        UINT dirtyCount) {
        D3D11_TEXTURE2D_DESC fullDesc, thisDesc;
        dstSurface->GetDesc(&fullDesc);
        srcSurface->GetDesc(&thisDesc);

        // Create render target view if needed
        if (!renderTargetView_) {
            HRESULT hr = device_->CreateRenderTargetView(dstSurface,
                nullptr,
                &renderTargetView_);
            if (FAILED(hr)) return CaptureResult::Error;
        }

        // Create shader resource view
        ComPtr<ID3D11ShaderResourceView> shaderResource;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = thisDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = thisDesc.MipLevels - 1;
        srvDesc.Texture2D.MipLevels = thisDesc.MipLevels;

        HRESULT hr = device_->CreateShaderResourceView(srcSurface,
            &srvDesc,
            &shaderResource);
        if (FAILED(hr)) {
            return checkHResult(device_.Get(), hr,
                SYSTEM_TRANSITIONS_EXPECTED_ERRORS);
        }

        // Prepare vertex buffer
        UINT bytesNeeded = sizeof(Vertex) * NUM_VERTICES * dirtyCount;
        vertexBuffer_.resize(bytesNeeded);

        Vertex* dirtyVertex = reinterpret_cast<Vertex*>(vertexBuffer_.data());
        for (UINT i = 0; i < dirtyCount; i++) {
            std::array<Vertex, NUM_VERTICES> vertices;
            setDirtyVert(vertices, dirtyBuffer[i], fullDesc, thisDesc);
            std::copy(vertices.begin(), vertices.end(), dirtyVertex);
            dirtyVertex += NUM_VERTICES;
        }

        // Create vertex buffer
        ComPtr<ID3D11Buffer> vertBuf;
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = bytesNeeded;
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = vertexBuffer_.data();

        hr = device_->CreateBuffer(&bufferDesc, &initData, &vertBuf);
        if (FAILED(hr)) return CaptureResult::Error;

        // Set up pipeline state
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ID3D11Buffer* buffers[] = { vertBuf.Get() };
        ID3D11SamplerState* samplers[] = { shaderResources_->getSampler() };
        ID3D11ShaderResourceView* srvs[] = { shaderResource.Get() };
        ID3D11RenderTargetView* rtvs[] = { renderTargetView_.Get() };

        context_->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
        context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        context_->OMSetRenderTargets(1, rtvs, nullptr);
        context_->VSSetShader(shaderResources_->getVertexShader(), nullptr, 0);
        context_->PSSetShader(shaderResources_->getPixelShader(), nullptr, 0);
        context_->PSSetShaderResources(0, 1, srvs);
        context_->PSSetSamplers(0, 1, samplers);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->IASetInputLayout(shaderResources_->getInputLayout());

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<FLOAT>(fullDesc.Width);
        viewport.Height = static_cast<FLOAT>(fullDesc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        context_->RSSetViewports(1, &viewport);
        context_->RSSetState(shaderResources_->getRasterizer());
        context_->Draw(NUM_VERTICES * dirtyCount, 0);

        // Unbind resources
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context_->PSSetShaderResources(0, 1, &nullSRV);
        context_->OMSetRenderTargets(0, nullptr, nullptr);

        return CaptureResult::Success;
    }

    bool DesktopDuplicator::processMonoMask(bool isMono,
        INT& ptrWidth, INT& ptrHeight,
        INT& ptrLeft, INT& ptrTop,
        std::vector<BYTE>& initBuffer,
        D3D11_BOX& box) {
        // Current shape info
        const auto& shapeInfo = pointerInfo_.getShapeInfo();

        // Copy coordinates
        ptrWidth = shapeInfo.Width;
        ptrHeight = shapeInfo.Height;
        ptrLeft = pointerInfo_.getPosition().x;
        ptrTop = pointerInfo_.getPosition().y;

        if (!isMono) {
            // Color cursor (already 32-bit ARGB)
            initBuffer.assign(pointerInfo_.getShapeBuffer(),
                pointerInfo_.getShapeBuffer() + pointerInfo_.getBufferSize());
        }
        else {
            // Monochrome cursor (1 bit AND mask, 1 bit XOR mask)
            ptrHeight = shapeInfo.Height / 2; // Mono masks are double height
            UINT maskPitch = shapeInfo.Pitch;
            const BYTE* shapeBuffer = pointerInfo_.getShapeBuffer();

            initBuffer.resize(ptrWidth * ptrHeight * 4);

            const BYTE* andMask = shapeBuffer;
            const BYTE* xorMask = shapeBuffer + (ptrHeight * maskPitch);

            for (INT row = 0; row < ptrHeight; ++row) {
                for (INT col = 0; col < ptrWidth; ++col) {
                    UINT maskBit = 0x80 >> (col & 7);
                    UINT maskByte = col / 8;

                    bool andBit = (andMask[row * maskPitch + maskByte] & maskBit) != 0;
                    bool xorBit = (xorMask[row * maskPitch + maskByte] & maskBit) != 0;

                    UINT index = (row * ptrWidth + col) * 4;

                    // Cursor Logic:
                    // AND 1, XOR 0 -> Transparent
                    // AND 0, XOR 1 -> White
                    // AND 0, XOR 0 -> Black
                    // AND 1, XOR 1 -> Invert (Treat as Black for simplicity)

                    if (andBit && !xorBit) {
                        // Transparent
                        initBuffer[index] = 0;
                        initBuffer[index + 1] = 0;
                        initBuffer[index + 2] = 0;
                        initBuffer[index + 3] = 0;
                    }
                    else if (!andBit && xorBit) {
                        // White
                        initBuffer[index] = 0xFF;
                        initBuffer[index + 1] = 0xFF;
                        initBuffer[index + 2] = 0xFF;
                        initBuffer[index + 3] = 0xFF;
                    }
                    else {
                        // Black
                        initBuffer[index] = 0;
                        initBuffer[index + 1] = 0;
                        initBuffer[index + 2] = 0;
                        initBuffer[index + 3] = 0xFF; // Opaque
                    }
                }
            }
        }

        // Set Update Box
        box.left = 0;
        box.top = 0;
        box.front = 0;
        box.right = ptrWidth;
        box.bottom = ptrHeight;
        box.back = 1;

        return true;
    }

    bool DesktopDuplicator::updateMouseTexture() {
        const auto& shapeInfo = pointerInfo_.getShapeInfo();

        // 1. Extract and process cursor pixels
        std::vector<BYTE> textureData;
        INT width, height, left, top;
        D3D11_BOX box;

        bool isMono = (shapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME);

        processMonoMask(isMono, width, height, left, top, textureData, box);

        // 2. Check if we need to recreate the texture (size changed)
        bool recreate = !mouseTexture_ ||
            mouseTexDesc_.Width != static_cast<UINT>(width) ||
            mouseTexDesc_.Height != static_cast<UINT>(height);

        if (recreate) {
            mouseTexture_.Reset();
            mouseSRV_.Reset();

            mouseTexDesc_ = {};
            mouseTexDesc_.Width = width;
            mouseTexDesc_.Height = height;
            mouseTexDesc_.MipLevels = 1;
            mouseTexDesc_.ArraySize = 1;
            mouseTexDesc_.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            mouseTexDesc_.SampleDesc.Count = 1;
            mouseTexDesc_.Usage = D3D11_USAGE_DEFAULT;
            mouseTexDesc_.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            mouseTexDesc_.CPUAccessFlags = 0;

            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = textureData.data();
            initData.SysMemPitch = width * 4;

            HRESULT hr = device_->CreateTexture2D(&mouseTexDesc_, &initData, &mouseTexture_);
            if (FAILED(hr)) return false;

            hr = device_->CreateShaderResourceView(mouseTexture_.Get(), nullptr, &mouseSRV_);
            if (FAILED(hr)) return false;
        }
        else {
            // Just update existing texture
            context_->UpdateSubresource(mouseTexture_.Get(), 0, &box,
                textureData.data(), width * 4, 0);
        }

        return true;
    }

    bool DesktopDuplicator::drawMouse(ID3D11Device* device,
        ID3D11RenderTargetView* rtv,
        const D3D11_BOX& cropBox) {
        if (!pointerInfo_.isVisible() || pointerInfo_.getBufferSize() == 0) {
            return true;
        }

        if (!updateMouseTexture()) {
            return false;
        }

        // Screen dimensions (of the output texture)
        float screenW = static_cast<float>(cropBox.right - cropBox.left);
        float screenH = static_cast<float>(cropBox.bottom - cropBox.top);

        // Mouse position relative to the crop box
        const auto& shapeInfo = pointerInfo_.getShapeInfo();
        float mouseX = static_cast<float>(pointerInfo_.getPosition().x) - cropBox.left;
        float mouseY = static_cast<float>(pointerInfo_.getPosition().y) - cropBox.top;

        float ptrW = static_cast<float>(mouseTexDesc_.Width);
        float ptrH = static_cast<float>(mouseTexDesc_.Height);

        // Calculate vertices in NDC (-1 to 1)
        float left = (mouseX / screenW) * 2.0f - 1.0f;
        float right = ((mouseX + ptrW) / screenW) * 2.0f - 1.0f;
        float top = -((mouseY / screenH) * 2.0f - 1.0f);
        float bottom = -(((mouseY + ptrH) / screenH) * 2.0f - 1.0f);

        Vertex vertices[6];

        // Quad - Triangle 1
        vertices[0] = Vertex(left, bottom, 0.0f, 0.0f, 1.0f);  // Bottom-Left
        vertices[1] = Vertex(left, top, 0.0f, 0.0f, 0.0f);     // Top-Left
        vertices[2] = Vertex(right, bottom, 0.0f, 1.0f, 1.0f); // Bottom-Right

        // Quad - Triangle 2
        vertices[3] = vertices[2];                             // Bottom-Right
        vertices[4] = vertices[1];                             // Top-Left
        vertices[5] = Vertex(right, top, 0.0f, 1.0f, 0.0f);    // Top-Right

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(Vertex) * 6;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = vertices;

        ComPtr<ID3D11Buffer> vBuffer;
        if (FAILED(device_->CreateBuffer(&bd, &initData, &vBuffer))) {
            return false;
        }

        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ID3D11Buffer* vbs[] = { vBuffer.Get() };

        context_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->IASetInputLayout(shaderResources_->getInputLayout());

        context_->VSSetShader(shaderResources_->getVertexShader(), nullptr, 0);
        context_->PSSetShader(shaderResources_->getPixelShader(), nullptr, 0);

        ID3D11ShaderResourceView* srvs[] = { mouseSRV_.Get() };
        context_->PSSetShaderResources(0, 1, srvs);

        ID3D11SamplerState* samplers[] = { shaderResources_->getSampler() };
        context_->PSSetSamplers(0, 1, samplers);

        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        context_->OMSetBlendState(shaderResources_->getBlendState(), blendFactor, 0xFFFFFFFF);
        context_->OMSetRenderTargets(1, &rtv, nullptr);

        D3D11_VIEWPORT vp;
        vp.Width = screenW;
        vp.Height = screenH;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        context_->RSSetViewports(1, &vp);

        context_->Draw(6, 0);

        // Cleanup
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context_->PSSetShaderResources(0, 1, &nullSRV);

        return true;
    }

    void DesktopDuplicator::setMoveRect(RECT& srcRect, RECT& dstRect,
        const DXGI_OUTDUPL_MOVE_RECT& moveRect,
        INT texWidth, INT texHeight) {
        switch (outputDesc_.Rotation) {
        case DXGI_MODE_ROTATION_IDENTITY:
        case DXGI_MODE_ROTATION_UNSPECIFIED:
            srcRect.left = moveRect.SourcePoint.x;
            srcRect.top = moveRect.SourcePoint.y;
            srcRect.right = moveRect.SourcePoint.x +
                moveRect.DestinationRect.right -
                moveRect.DestinationRect.left;
            srcRect.bottom = moveRect.SourcePoint.y +
                moveRect.DestinationRect.bottom -
                moveRect.DestinationRect.top;
            dstRect = moveRect.DestinationRect;
            break;
            // Additional rotations (90, 180, 270) should be handled here for robustness
        default:
            srcRect = dstRect = { 0, 0, 0, 0 };
            break;
        }
    }

    void DesktopDuplicator::setDirtyVert(std::array<Vertex, 6>& vertices,
        const RECT& dirty,
        const D3D11_TEXTURE2D_DESC& fullDesc,
        const D3D11_TEXTURE2D_DESC& thisDesc) {
        INT centerX = fullDesc.Width / 2;
        INT centerY = fullDesc.Height / 2;

        RECT destDirty = dirty;

        // Set texture coordinates
        vertices[0].texCoord[0] = dirty.left / static_cast<float>(thisDesc.Width);
        vertices[0].texCoord[1] = dirty.bottom / static_cast<float>(thisDesc.Height);

        vertices[1].texCoord[0] = dirty.left / static_cast<float>(thisDesc.Width);
        vertices[1].texCoord[1] = dirty.top / static_cast<float>(thisDesc.Height);

        vertices[2].texCoord[0] = dirty.right / static_cast<float>(thisDesc.Width);
        vertices[2].texCoord[1] = dirty.bottom / static_cast<float>(thisDesc.Height);

        vertices[5].texCoord[0] = dirty.right / static_cast<float>(thisDesc.Width);
        vertices[5].texCoord[1] = dirty.top / static_cast<float>(thisDesc.Height);

        // Set positions
        vertices[0].position[0] = (destDirty.left - centerX) / static_cast<float>(centerX);
        vertices[0].position[1] = -1.0f * (destDirty.bottom - centerY) / static_cast<float>(centerY);
        vertices[0].position[2] = 0.0f;

        vertices[1].position[0] = (destDirty.left - centerX) / static_cast<float>(centerX);
        vertices[1].position[1] = -1.0f * (destDirty.top - centerY) / static_cast<float>(centerY);
        vertices[1].position[2] = 0.0f;

        vertices[2].position[0] = (destDirty.right - centerX) / static_cast<float>(centerX);
        vertices[2].position[1] = -1.0f * (destDirty.bottom - centerY) / static_cast<float>(centerY);
        vertices[2].position[2] = 0.0f;

        vertices[5].position[0] = (destDirty.right - centerX) / static_cast<float>(centerX);
        vertices[5].position[1] = -1.0f * (destDirty.top - centerY) / static_cast<float>(centerY);
        vertices[5].position[2] = 0.0f;

        vertices[3] = vertices[2];
        vertices[4] = vertices[1];
    }

    CaptureResult DesktopDuplicator::checkHResult(ID3D11Device* device,
        HRESULT hr,
        const std::vector<HRESULT>& expectedErrors) {
        HRESULT translatedHr = hr;

        if (device) {
            HRESULT removeReason = device->GetDeviceRemovedReason();

            switch (removeReason) {
            case DXGI_ERROR_DEVICE_REMOVED:
            case DXGI_ERROR_DEVICE_RESET:
            case E_OUTOFMEMORY:
                translatedHr = DXGI_ERROR_DEVICE_REMOVED;
                break;
            case S_OK:
                break;
            default:
                translatedHr = removeReason;
                break;
            }
        }

        for (HRESULT expected : expectedErrors) {
            if (expected == translatedHr) {
                return CaptureResult::ExpectedError;
            }
        }

        return CaptureResult::Error;
    }

    // ============================================================================
    // CaptureDevice Implementation
    // ============================================================================

    CaptureDevice::CaptureDevice(ID3D11Device* device, HMONITOR monitor)
        : device_(device)
        , monitorHandle_(monitor)
        , adapterLuid_(0)
        , prepared_(false) {

        if (!device || !monitor) {
            throw CaptureException("Invalid device or monitor handle");
        }

        device->GetImmediateContext(&context_);

        if (!initializeMonitorInfo()) {
            throw CaptureException("Failed to initialize monitor information");
        }
    }

    CaptureDevice::~CaptureDevice() = default;

    bool CaptureDevice::initializeMonitorInfo() {
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput> output;

        HRESULT hr = findOutputForMonitor(monitorHandle_, adapter, output);
        if (FAILED(hr)) return false;

        DXGI_ADAPTER_DESC adapterDesc;
        hr = adapter->GetDesc(&adapterDesc);
        if (FAILED(hr)) return false;

        DXGI_OUTPUT_DESC outputDesc;
        hr = output->GetDesc(&outputDesc);
        if (FAILED(hr)) return false;

        adapterLuid_ = luidToInt64(adapterDesc.AdapterLuid);

        // Get monitor info
        MONITORINFOEXW monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFOEXW);
        if (!GetMonitorInfoW(outputDesc.Monitor, &monitorInfo)) {
            return false;
        }

        // Get display settings
        DEVMODEW devMode = {};
        devMode.dmSize = sizeof(DEVMODEW);
        devMode.dmDriverExtra = sizeof(POINTL);
        devMode.dmFields = DM_POSITION;

        if (!EnumDisplaySettingsW(monitorInfo.szDevice,
            ENUM_CURRENT_SETTINGS,
            &devMode)) {
            return false;
        }

        bounds_.coordinates.left = devMode.dmPosition.x;
        bounds_.coordinates.top = devMode.dmPosition.y;
        bounds_.coordinates.right = devMode.dmPosition.x + devMode.dmPelsWidth;
        bounds_.coordinates.bottom = devMode.dmPosition.y + devMode.dmPelsHeight;
        bounds_.width = devMode.dmPelsWidth;
        bounds_.height = devMode.dmPelsHeight;

        output_ = output;
        return true;
    }

    CaptureResult CaptureDevice::prepare() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (prepared_) {
            return CaptureResult::Success;
        }

        duplicator_ = std::make_unique<DesktopDuplicator>();
        auto result = duplicator_->initialize(device_.Get(), monitorHandle_);

        if (result != CaptureResult::Success) {
            duplicator_.reset();
            return result;
        }

        prepared_ = true;
        return CaptureResult::Success;
    }

    bool CaptureDevice::getSize(UINT& width, UINT& height) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (duplicator_) {
            duplicator_->getSize(width, height);
            bounds_.width = width;
            bounds_.height = height;
        }

        width = bounds_.width;
        height = bounds_.height;
        return true;
    }

    CaptureResult CaptureDevice::capture(ID3D11Device* targetDevice,
        ID3D11Texture2D* texture,
        ID3D11RenderTargetView* rtv,
        const D3D11_BOX& cropBox,
        bool drawMouse) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (!prepared_) {
            auto result = prepare();
            if (result != CaptureResult::Success) {
                return result;
            }
        }

        UINT width, height;
        getSize(width, height);

        // Validate crop box
        if (cropBox.left >= width || cropBox.right > width ||
            cropBox.top >= height || cropBox.bottom > height) {
            return CaptureResult::SizeChanged;
        }

        // Capture frame
        auto result = duplicator_->capture();
        if (result != CaptureResult::Success && result != CaptureResult::Timeout) {
            duplicator_.reset();
            prepared_ = false;
            return result;
        }

        if (result == CaptureResult::Timeout) {
            return result;
        }

        // Copy to texture
        result = duplicator_->copyToTexture(targetDevice, texture, cropBox);
        if (result != CaptureResult::Success) {
            return result;
        }

        // Draw mouse if requested
        if (drawMouse && rtv) {
            duplicator_->drawMouse(targetDevice, rtv, cropBox);
        }

        return CaptureResult::Success;
    }

    int64_t CaptureDevice::luidToInt64(const LUID& luid) {
        LARGE_INTEGER li;
        li.LowPart = luid.LowPart;
        li.HighPart = luid.HighPart;
        return li.QuadPart;
    }

    // ============================================================================
    // CaptureManager Implementation
    // ============================================================================

    CaptureManager& CaptureManager::getInstance() {
        static CaptureManager instance;
        return instance;
    }

    std::shared_ptr<CaptureDevice> CaptureManager::createCaptureDevice(
        ID3D11Device* device, HMONITOR monitor) {

        std::lock_guard<std::mutex> lock(mutex_);

        // Check if device already exists
        auto it = devices_.find(monitor);
        if (it != devices_.end()) {
            if (auto existing = it->second.lock()) {
                return existing;
            }
        }

        // Create new device
        auto captureDevice = std::make_shared<CaptureDevice>(device, monitor);
        devices_[monitor] = captureDevice;

        return captureDevice;
    }

    std::shared_ptr<CaptureDevice> CaptureManager::getCaptureDevice(HMONITOR monitor) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = devices_.find(monitor);
        if (it != devices_.end()) {
            return it->second.lock();
        }

        return nullptr;
    }

    void CaptureManager::removeCaptureDevice(HMONITOR monitor) {
        std::lock_guard<std::mutex> lock(mutex_);
        devices_.erase(monitor);
    }

    void CaptureManager::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        devices_.clear();
    }

    // ============================================================================
    // Utility Functions Implementation
    // ============================================================================

    HRESULT findOutputForMonitor(HMONITOR monitor,
        ComPtr<IDXGIAdapter1>& adapter,
        ComPtr<IDXGIOutput>& output) {
        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return hr;

        for (UINT i = 0; ; i++) {
            ComPtr<IDXGIAdapter1> tempAdapter;
            hr = factory->EnumAdapters1(i, &tempAdapter);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) continue;

            for (UINT j = 0; ; j++) {
                ComPtr<IDXGIOutput> tempOutput;
                hr = tempAdapter->EnumOutputs(j, &tempOutput);
                if (hr == DXGI_ERROR_NOT_FOUND) break;
                if (FAILED(hr)) continue;

                DXGI_OUTPUT_DESC desc;
                hr = tempOutput->GetDesc(&desc);
                if (SUCCEEDED(hr) && desc.Monitor == monitor) {
                    adapter = tempAdapter;
                    output = tempOutput;
                    return S_OK;
                }
            }
        }

        return E_FAIL;
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

} // namespace DxgiCapture