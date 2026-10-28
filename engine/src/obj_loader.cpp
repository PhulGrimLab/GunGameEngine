#include "obj_loader.h"

#include <DirectXMath.h>
#include <Windows.h>

#include <fstream>
#include <sstream>

#include "error.h"

namespace gge {

using namespace DirectX;

MeshData ParseObj(const std::string& text)
{
    std::vector<XMFLOAT3> positions;
    MeshData mesh;

    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream tokens(line);
        std::string tag;
        tokens >> tag;
        if (tag == "v") {
            XMFLOAT3 p;
            tokens >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (tag == "f") {
            std::vector<int> face;
            std::string item;
            while (tokens >> item) {
                int index = std::stoi(item.substr(0, item.find('/')));
                int count = static_cast<int>(positions.size());
                int resolved = index > 0 ? index - 1 : count + index;
                if (resolved < 0 || resolved >= count)
                    throw std::runtime_error("OBJ face index out of range");
                face.push_back(resolved);
            }
            if (face.size() < 3)
                throw std::runtime_error("OBJ face has fewer than 3 vertices");

            for (size_t i = 1; i + 1 < face.size(); ++i) {
                const XMFLOAT3& a = positions[face[0]];
                const XMFLOAT3& b = positions[face[i]];
                const XMFLOAT3& c = positions[face[i + 1]];
                XMVECTOR n = XMVector3Normalize(
                    XMVector3Cross(XMLoadFloat3(&b) - XMLoadFloat3(&a), XMLoadFloat3(&c) - XMLoadFloat3(&a)));
                XMFLOAT3 normal;
                XMStoreFloat3(&normal, n);
                uint32_t base = static_cast<uint32_t>(mesh.vertices.size() / 6);
                for (const XMFLOAT3* p : {&a, &b, &c}) {
                    mesh.vertices.insert(mesh.vertices.end(), {p->x, p->y, p->z, normal.x, normal.y, normal.z});
                }
                mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
            }
        }
    }
    if (mesh.indices.empty())
        throw std::runtime_error("OBJ contains no faces");
    return mesh;
}

MeshData LoadObjFile(const std::string& pathUtf8)
{
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, nullptr, 0);
    std::wstring wide(wideLength > 0 ? wideLength - 1 : 0, L'\0');
    if (wideLength > 1)
        MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, wide.data(), wideLength);

    std::ifstream file(wide, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open file: " + pathUtf8);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return ParseObj(buffer.str());
}

}  // namespace gge
