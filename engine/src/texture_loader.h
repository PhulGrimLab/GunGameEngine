#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gge {

// 텍스처 한 변의 최대 크기. 렌더러와 로더가 함께 쓴다(로더는 이보다 큰 이미지를 디코딩 전에 거부한다).
constexpr int kMaxTextureSize = 4096;

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// WIC로 PNG/BMP/JPEG 등을 읽어 RGBA8로 변환한다. path는 UTF-8.
ImageData LoadImageFile(const std::string& pathUtf8);

}  // namespace gge
