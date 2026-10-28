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

}  // namespace

ImageData LoadImageFile(const std::string& pathUtf8)
{
    // 이미 다른 아파트 모델로 초기화돼 있으면 RPC_E_CHANGED_MODE가 반환되지만 WIC는 그대로 사용할 수 있다.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

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

    ImageData image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba.resize(static_cast<size_t>(width) * height * 4);
    Check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.rgba.size()), image.rgba.data()),
          "WIC CopyPixels");
    return image;
}

}  // namespace gge
