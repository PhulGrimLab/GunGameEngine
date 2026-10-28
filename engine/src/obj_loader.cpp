#include "obj_loader.h"

#include <DirectXMath.h>
#include <Windows.h>

#include <fstream>
#include <sstream>

#include "error.h"

namespace gge {

using namespace DirectX;

std::wstring Utf8ToWide(const std::string& utf8)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (length <= 1)
        return std::wstring();
    std::wstring wide(length - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), length);
    return wide;
}

namespace {

int ResolveIndex(int index, size_t count, const char* what)
{
    int n = static_cast<int>(count);
    int resolved = index > 0 ? index - 1 : n + index;
    if (index == 0 || resolved < 0 || resolved >= n)
        throw std::runtime_error(std::string("OBJ ") + what + " index out of range");
    return resolved;
}

struct FaceVertex {
    int position;
    int uv;  // -1이면 없음
};

}  // namespace

MeshData ParseObj(const std::string& text)
{
    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT2> uvs;
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
        } else if (tag == "vt") {
            XMFLOAT2 t{0, 0};
            tokens >> t.x >> t.y;
            uvs.push_back({t.x, 1.0f - t.y});
        } else if (tag == "f") {
            std::vector<FaceVertex> face;
            std::string item;
            while (tokens >> item) {
                size_t firstSlash = item.find('/');
                int positionIndex = std::stoi(item.substr(0, firstSlash));
                FaceVertex fv{ResolveIndex(positionIndex, positions.size(), "face"), -1};
                if (firstSlash != std::string::npos) {
                    size_t secondSlash = item.find('/', firstSlash + 1);
                    std::string uvToken = item.substr(firstSlash + 1, secondSlash == std::string::npos
                                                                          ? std::string::npos
                                                                          : secondSlash - firstSlash - 1);
                    if (!uvToken.empty())
                        fv.uv = ResolveIndex(std::stoi(uvToken), uvs.size(), "texcoord");
                }
                face.push_back(fv);
            }
            if (face.size() < 3)
                throw std::runtime_error("OBJ face has fewer than 3 vertices");

            for (size_t i = 1; i + 1 < face.size(); ++i) {
                const FaceVertex tri[3] = {face[0], face[i], face[i + 1]};
                const XMFLOAT3& a = positions[tri[0].position];
                const XMFLOAT3& b = positions[tri[1].position];
                const XMFLOAT3& c = positions[tri[2].position];
                XMVECTOR n = XMVector3Normalize(
                    XMVector3Cross(XMLoadFloat3(&b) - XMLoadFloat3(&a), XMLoadFloat3(&c) - XMLoadFloat3(&a)));
                XMFLOAT3 normal;
                XMStoreFloat3(&normal, n);
                uint32_t base = static_cast<uint32_t>(mesh.vertices.size() / 8);
                for (const FaceVertex& fv : tri) {
                    const XMFLOAT3& p = positions[fv.position];
                    XMFLOAT2 uv = fv.uv >= 0 ? uvs[fv.uv] : XMFLOAT2{0, 0};
                    mesh.vertices.insert(mesh.vertices.end(), {p.x, p.y, p.z, normal.x, normal.y, normal.z, uv.x, uv.y});
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
    std::ifstream file(Utf8ToWide(pathUtf8), std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open file: " + pathUtf8);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return ParseObj(buffer.str());
}

}  // namespace gge
