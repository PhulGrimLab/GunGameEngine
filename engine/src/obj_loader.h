#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gge {

struct MeshData {
    std::vector<float> vertices;  // position(3) + normal(3) + uv(2)
    std::vector<uint32_t> indices;
};

// 폴리곤은 팬으로 삼각화하고, 노멀은 면 단위로 새로 계산한다(정점 공유 없음).
// vt가 없는 정점의 UV는 (0,0)이다. OBJ의 v는 아래가 0이므로 읽을 때 1-v로 뒤집어 이미지 좌상단 원점으로 맞춘다.
MeshData ParseObj(const std::string& text);
MeshData LoadObjFile(const std::string& pathUtf8);

std::wstring Utf8ToWide(const std::string& utf8);

}  // namespace gge
