#include "texture_loader.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>

#include "error.h"
#include "obj_loader.h"

namespace gge {

namespace {

using Microsoft::WRL::ComPtr;

void Check(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        char buf[16];
        snprintf(buf, sizeof buf, "%08X", static_cast<unsigned>(hr));
        throw std::runtime_error(std::string(what) + " failed, hr=0x" + buf);
    }
}

// CoInitializeEx를 한 번 호출하고, 성공했을 때만 소멸자에서 짝을 맞춰 CoUninitialize한다.
// 이미 다른 아파트 모델로 초기화돼 있으면 RPC_E_CHANGED_MODE(실패)가 오지만 WIC는 그대로 쓸 수 있고, 이때는 짝을 맞출 필요가 없다.
// WIC 객체(ComPtr)보다 먼저 만들어 나중에 소멸되게 해야 한다(CoUninitialize 이후에 COM 객체를 놓지 않도록).
class ComScope {
public:
    ComScope() : _initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope()
    {
        if (_initialized) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool _initialized;
};

}  // namespace

ImageData LoadImageFile(const std::string& pathUtf8)
{
    ComScope com;

    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
          "WIC factory");

    ComPtr<IWICBitmapDecoder> decoder;
    std::wstring wide = Utf8ToWide(pathUtf8);
    if (FAILED(factory->CreateDecoderFromFilename(wide.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)))
        throw std::runtime_error("cannot decode image: " + pathUtf8);

    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, &frame), "WIC GetFrame");

    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter), "WIC CreateFormatConverter");
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom),
          "WIC Initialize");

    UINT width = 0, height = 0;
    Check(converter->GetSize(&width, &height), "WIC GetSize");

    // 비정상적으로 큰 이미지는 픽셀 버퍼를 할당하기 전에 거부한다(렌더러도 같은 한도로 거부한다).
    if (width == 0 || height == 0 || width > static_cast<UINT>(kMaxTextureSize) ||
        height > static_cast<UINT>(kMaxTextureSize)) {
        throw std::runtime_error("image too large or empty (" + std::to_string(width) + "x" + std::to_string(height) +
                                 ", max " + std::to_string(kMaxTextureSize) + "): " + pathUtf8);
    }

    ImageData image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba.resize(static_cast<size_t>(width) * height * 4);
    Check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.rgba.size()), image.rgba.data()),
          "WIC CopyPixels");
    return image;
}

}  // namespace gge
