#pragma once
#include <string>

#include "obj_loader.h"

namespace gge {

// .ggm: 엔진 전용 바이너리 메시 포맷 (리틀 엔디언).
//   char[4] magic "GGM1"; uint32 vertexCount; uint32 indexCount;
//   float[vertexCount * 8] vertices; uint32[indexCount] indices
void WriteGgm(const std::string& pathUtf8, const MeshData& mesh);
MeshData ReadGgm(const std::string& pathUtf8);

}  // namespace gge
