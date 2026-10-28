#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gge {

struct MeshData {
    std::vector<float> vertices;  // position(3) + normal(3)
    std::vector<uint32_t> indices;
};

// 폴리곤은 팬으로 삼각화하고, 노멀은 면 단위로 새로 계산한다(정점 공유 없음).
MeshData ParseObj(const std::string& text);
MeshData LoadObjFile(const std::string& pathUtf8);

}  // namespace gge
