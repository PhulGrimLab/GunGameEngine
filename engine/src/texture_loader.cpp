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

namespace {

ComPtr<IWICImagingFactory> CreateFactory()
{
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
          "WIC factory");
    return factory;
}

// 디코더의 첫 프레임을 RGBA8로 변환한다. what은 오류 메시지용 이름(파일 경로 등).
ImageData DecodeFirstFrame(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, const std::string& pathUtf8)
{
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

}  // namespace

ImageData LoadImageFile(const std::string& pathUtf8)
{
    ComScope com;
    ComPtr<IWICImagingFactory> factory = CreateFactory();

    ComPtr<IWICBitmapDecoder> decoder;
    std::wstring wide = Utf8ToWide(pathUtf8);
    if (FAILED(factory->CreateDecoderFromFilename(wide.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)))
        throw std::runtime_error("cannot decode image: " + pathUtf8);
    return DecodeFirstFrame(factory.Get(), decoder.Get(), pathUtf8);
}

ImageData LoadImageMemory(const uint8_t* data, size_t size, const std::string& what)
{
    if (!data || size == 0 || size > 0xFFFFFFFFull)
        throw std::runtime_error("cannot decode image: " + what + " (empty or too large)");
    ComScope com;
    ComPtr<IWICImagingFactory> factory = CreateFactory();

    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream), "WIC CreateStream");
    // 스트림은 이 버퍼를 복사하지 않고 가리키므로 디코딩이 끝날 때까지 data가 살아 있어야 한다(이 함수 안에서 끝난다).
    Check(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size)), "WIC InitializeFromMemory");
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)))
        throw std::runtime_error("cannot decode image: " + what);
    return DecodeFirstFrame(factory.Get(), decoder.Get(), what);
}

}  // namespace gge
