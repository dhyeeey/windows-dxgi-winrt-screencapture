#pragma once
/**
 * @file ImageSaver.hpp
 * @brief Helper class for saving Direct3D textures to disk using WIC (Windows Imaging Component).
 * * This class wraps the complexity of the COM-based WIC API. It provides two main ways to save images:
 * 1. save(): Saves directly from a D3D11 Texture (handles GPU->CPU copy internally).
 * 2. saveRaw(): Saves from a raw CPU buffer (std::vector), used by background threads.
 */

#include <d3d11.h>
#include <wrl/client.h>
#include <wincodec.h> // WIC headers
#include <string>
#include <iostream>
#include <vector>

 // Link WIC library (Windows Imaging Component)
//#pragma comment(lib, "windowscodec.lib") 

using namespace Microsoft::WRL;

/**
 * @class ImageSaver
 * @brief Handles image encoding (JPG/PNG/BMP) and file writing.
 * * @note This class assumes COM (Component Object Model) has been initialized
 * on the thread creating/using it via `CoInitializeEx`.
 */
class ImageSaver {
public:
    /**
     * @brief Constructor. Initializes the WIC Imaging Factory.
     * * The factory is the central entry point for WIC, used to create
     * encoders, streams, and bitmaps. It is created once and reused
     * for performance.
     */
    ImageSaver() {
        // We rely on CoInitialize() being called in main() or the thread entry point.

        // Create WIC Factory once
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory_)
        );

        if (FAILED(hr)) {
            std::cerr << "Failed to create WIC Imaging Factory. Ensure CoInitialize() was called." << std::endl;
        }
    }

    ~ImageSaver() = default;

    /**
     * @brief Saves a D3D11 Texture directly to disk (Synchronous).
     * * This method handles the full pipeline:
     * 1. Creates a "Staging" texture (CPU readable).
     * 2. Copies GPU VRAM -> Staging RAM.
     * 3. Maps memory and feeds it to WIC.
     * * @warning This is a blocking operation. Do not use in a high-performance loop.
     * * @param dev D3D11 Device (for creating staging resource).
     * @param ctx D3D11 Context (for copy operations).
     * @param tex The source GPU texture.
     * @param filename Output filename (extension determines format: .jpg, .png, .bmp).
     */
    void save(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, const std::wstring& filename) {
        if (!factory_) return;

        HRESULT hr;

        // 1. Determine Image Format based on extension
        GUID containerFormat = GUID_ContainerFormatPng; // Default
        if (filename.find(L".jpg") != std::string::npos || filename.find(L".jpeg") != std::string::npos)
            containerFormat = GUID_ContainerFormatJpeg;
        else if (filename.find(L".bmp") != std::string::npos)
            containerFormat = GUID_ContainerFormatBmp;

        // 2. Get Texture Description (Width, Height, Format)
        D3D11_TEXTURE2D_DESC desc;
        tex->GetDesc(&desc);

        // 3. Create Staging Texture (GPU -> CPU Bridge)
        // A "Staging" resource is required to read GPU data on the CPU.
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0; // Staging textures cannot be bound as shaders
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; // We want to read it
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> stagingTex;
        hr = dev->CreateTexture2D(&stagingDesc, nullptr, &stagingTex);
        if (FAILED(hr)) return;

        // Execute Copy (VRAM -> System RAM)
        ctx->CopyResource(stagingTex.Get(), tex);

        // 4. Initialize WIC Stream & Encoder
        ComPtr<IWICStream> stream;
        hr = factory_->CreateStream(&stream);
        if (FAILED(hr)) return;

        hr = stream->InitializeFromFilename(filename.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            std::wcerr << L"Failed to create file: " << filename << std::endl;
            return;
        }

        ComPtr<IWICBitmapEncoder> encoder;
        hr = factory_->CreateEncoder(containerFormat, nullptr, &encoder);
        if (FAILED(hr)) return;

        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) return;

        // Create a specific Frame (Images can technically have multiple frames, we just use one)
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;
        hr = encoder->CreateNewFrame(&frame, &props);
        if (FAILED(hr)) return;

        // 5. Configure JPEG Quality (Optional)
        // Default quality is often low (0.7), so we bump it to 0.9 (90%)
        if (containerFormat == GUID_ContainerFormatJpeg) {
            PROPBAG2 option = { 0 };
            option.pstrName = (LPOLESTR)L"ImageQuality";
            VARIANT varValue;
            VariantInit(&varValue);
            varValue.vt = VT_R4;
            varValue.fltVal = 0.9f;
            props->Write(1, &option, &varValue);
        }

        hr = frame->Initialize(props.Get());
        if (FAILED(hr)) return;

        hr = frame->SetSize(desc.Width, desc.Height);
        if (FAILED(hr)) return;

        // 6. Set Pixel Format
        // We tell WIC our source data is BGRA (DirectX Standard).
        // WIC might change this guid to BGR if the format (like JPEG) doesn't support Alpha.
        WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&pixelFormat);
        if (FAILED(hr)) return;

        // 7. Map Memory and Write
        D3D11_MAPPED_SUBRESOURCE map;
        hr = ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &map);
        if (SUCCEEDED(hr)) {

            // Create a WIC Bitmap Wrapper around the raw memory
            // This is safer than raw pointer arithmetic because it handles Stride/Padding correctly.
            ComPtr<IWICBitmap> wicBitmap;
            hr = factory_->CreateBitmapFromMemory(
                desc.Width,
                desc.Height,
                GUID_WICPixelFormat32bppBGRA, // Input Format
                map.RowPitch,                 // Stride (Bytes per row)
                map.RowPitch * desc.Height,   // Total Buffer Size
                (BYTE*)map.pData,             // Pointer to data
                &wicBitmap
            );

            if (SUCCEEDED(hr)) {
                // WriteSource performs the magic: Format Conversion (32bpp -> 24bpp)
                frame->WriteSource(wicBitmap.Get(), nullptr);
            }

            ctx->Unmap(stagingTex.Get(), 0);
        }

        frame->Commit();
        encoder->Commit();

        std::wcout << L"Saved: " << filename << std::endl;
    }

    /**
     * @brief Saves raw pixel data from RAM to disk.
     * * This method is Thread-Safe and D3D-Agnostic.
     * It is designed to be called from a background worker thread.
     * * @param pixels Vector containing raw BGRA pixel data.
     * @param width Image width.
     * @param height Image height.
     * @param stride Bytes per row (RowPitch).
     * @param filename Output filename.
     */
    void saveRaw(const std::vector<uint8_t>& pixels, UINT width, UINT height, UINT stride, const std::wstring& filename) {
        if (!factory_) return;

        HRESULT hr;

        // 1. Create Output Stream
        ComPtr<IWICStream> stream;
        hr = factory_->CreateStream(&stream);
        if (FAILED(hr)) return;

        hr = stream->InitializeFromFilename(filename.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            std::wcerr << L"Failed to create file: " << filename << std::endl;
            return;
        }

        // 2. Determine Format (JPG/PNG/BMP)
        GUID containerFormat = GUID_ContainerFormatPng;
        if (filename.find(L".jpg") != std::string::npos || filename.find(L".jpeg") != std::string::npos)
            containerFormat = GUID_ContainerFormatJpeg;
        else if (filename.find(L".bmp") != std::string::npos)
            containerFormat = GUID_ContainerFormatBmp;

        ComPtr<IWICBitmapEncoder> encoder;
        hr = factory_->CreateEncoder(containerFormat, nullptr, &encoder);
        if (FAILED(hr)) return;

        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) return;

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;
        hr = encoder->CreateNewFrame(&frame, &props);
        if (FAILED(hr)) return;

        // 3. Configure JPEG Quality
        if (containerFormat == GUID_ContainerFormatJpeg) {
            PROPBAG2 option = { 0 };
            wchar_t optName[] = L"ImageQuality";
            option.pstrName = optName;
            VARIANT varValue;
            VariantInit(&varValue);
            varValue.vt = VT_R4;
            varValue.fltVal = 0.9f;
            props->Write(1, &option, &varValue);
        }

        hr = frame->Initialize(props.Get());
        if (FAILED(hr)) return;
        hr = frame->SetSize(width, height);
        if (FAILED(hr)) return;

        // 4. Negotiate Pixel Format
        // We request 32bpp BGRA. WIC will likely change this to 24bpp BGR for JPEGs.
        WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&pixelFormat);
        if (FAILED(hr)) return;

        // 5. Create a WIC Bitmap Wrapper (Crucial Step)
        // We wrap our raw std::vector data in an IWICBitmap object.
        // This explicitly tells WIC: "This buffer is 32-bit BGRA".
        ComPtr<IWICBitmap> sourceBitmap;
        hr = factory_->CreateBitmapFromMemory(
            width,
            height,
            GUID_WICPixelFormat32bppBGRA, // Input Format (Must match our vector data)
            stride,
            static_cast<UINT>(pixels.size()),
            (BYTE*)pixels.data(),
            &sourceBitmap
        );

        if (SUCCEEDED(hr)) {
            // 6. WriteSource (Format Conversion)
            // WriteSource sees that Input is 32bpp and Output is 24bpp (for JPG).
            // It automatically discards the Alpha channel and handles the packing.
            hr = frame->WriteSource(sourceBitmap.Get(), nullptr);
        }

        if (SUCCEEDED(hr)) {
            frame->Commit();
            encoder->Commit();
            std::wcout << L"Saved: " << filename << std::endl;
        }
    }

private:
    ComPtr<IWICImagingFactory> factory_; ///< Central WIC Factory object
};