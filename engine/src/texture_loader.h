#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gge {

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// WIC로 PNG/BMP/JPEG 등을 읽어 RGBA8로 변환한다. path는 UTF-8.
ImageData LoadImageFile(const std::string& pathUtf8);

}  // namespace gge
