#include "mesh_io.h"

#include <cstring>
#include <fstream>

#include "error.h"

namespace gge {

namespace {

constexpr char kMagic[4] = {'G', 'G', 'M', '1'};
constexpr uint32_t kMaxCount = 64u * 1024 * 1024;

}  // namespace

void WriteGgm(const std::string& pathUtf8, const MeshData& mesh)
{
    std::ofstream file(Utf8ToWide(pathUtf8), std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot write file: " + pathUtf8);
    uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size() / 8);
    uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
    file.write(kMagic, 4);
    file.write(reinterpret_cast<const char*>(&vertexCount), 4);
    file.write(reinterpret_cast<const char*>(&indexCount), 4);
    file.write(reinterpret_cast<const char*>(mesh.vertices.data()),
               static_cast<std::streamsize>(mesh.vertices.size() * sizeof(float)));
    file.write(reinterpret_cast<const char*>(mesh.indices.data()),
               static_cast<std::streamsize>(mesh.indices.size() * sizeof(uint32_t)));
    if (!file)
        throw std::runtime_error("failed writing file: " + pathUtf8);
}

MeshData ReadGgm(const std::string& pathUtf8)
{
    std::ifstream file(Utf8ToWide(pathUtf8), std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot open file: " + pathUtf8);
    const uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    file.seekg(0);

    char magic[4];
    uint32_t vertexCount = 0, indexCount = 0;
    if (fileSize < 12 || !file.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0)
        throw std::runtime_error("not a GGM1 mesh file");
    file.read(reinterpret_cast<char*>(&vertexCount), 4);
    file.read(reinterpret_cast<char*>(&indexCount), 4);
    if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0 || vertexCount > kMaxCount || indexCount > kMaxCount)
        throw std::runtime_error("invalid GGM1 header");

    const uint64_t expected = 12 + uint64_t(vertexCount) * 8 * 4 + uint64_t(indexCount) * 4;
    if (fileSize != expected)
        throw std::runtime_error("GGM1 file size does not match header");

    MeshData mesh;
    mesh.vertices.resize(static_cast<size_t>(vertexCount) * 8);
    mesh.indices.resize(indexCount);
    file.read(reinterpret_cast<char*>(mesh.vertices.data()), static_cast<std::streamsize>(mesh.vertices.size() * sizeof(float)));
    file.read(reinterpret_cast<char*>(mesh.indices.data()), static_cast<std::streamsize>(mesh.indices.size() * sizeof(uint32_t)));
    if (!file)
        throw std::runtime_error("failed reading GGM1 file");
    for (uint32_t index : mesh.indices)
        if (index >= vertexCount)
            throw std::runtime_error("GGM1 index out of range");
    return mesh;
}

}  // namespace gge
