#include "scene_io.h"

#include <cmath>
#include <cstring>
#include <fstream>

#include "error.h"
#include "obj_loader.h"
#include "renderer.h"  // kMaxPointLights

// .ggs 형식 (리틀 엔디언, 문자열 = uint32 바이트 수 + UTF-8):
//   char[4] "GGS1"; uint32 version(=2; 1도 읽는다: 충돌체에 이름이 없고 이름은 빈 문자열이 된다)
//   uint32 textureCount; 각 { uint32 byteLength; bytes[] }                       // 이미지 파일 원본
//   uint32 meshCount; 각 {
//       uint8 skinned; uint32 vertexCount; uint32 indexCount;
//       float[vertexCount*8] vertices; uint32[indexCount] indices;
//       skinned면: uint32[vertexCount*4] joints; float[vertexCount*4] weights;
//                  uint32 boneCount; int32[boneCount] parents; float[boneCount*16] inverseBind;
//                  float[boneCount*3] restTranslation; float[boneCount*4] restRotation; float[boneCount*3] restScale;
//                  uint32 clipCount; 각 { string name; float duration; uint32 channelCount;
//                      각 { int32 bone; uint32 path(0 이동,1 회전,2 크기); uint32 keyCount; float[keyCount] times;
//                           float[keyCount*(path==1?4:3)] values } } }
//   uint32 nodeCount; 각 { string name; int32 parent; int32 mesh; int32 texture; float position[3], rotation[3], scale[3], color[3];
//                          float alpha; uint8 castsShadow; string clip; uint8 playing; float speed }
//   lighting: float ambient[3], sunDirection[3], sunColor[3]; uint32 pointCount; 각 { float position[3], color[3], range };
//             uint8 shadowEnabled; float shadowPlaneY, shadowOpacity
//   uint32 roomCount; 각 { string name; uint32 vertexCount; float[vertexCount*2] polygon; float eye[3], target[3], fov }
//   uint32 colliderCount; 각 { [version 2부터: string name;] float minX, minZ, maxX, maxZ }   // 같은 이름 여럿 가능(스크립트가 한꺼번에 켜고 끈다)
//   uint32 triggerCount; 각 { string name; uint32 vertexCount; float[vertexCount*2] polygon; int32 tag }
//   uint8 hasPlayer; hasPlayer면 { int32 node; uint8 hasSpawn; float spawn[2]; float radius; float speed }
//   uint32 crc32  // 이 앞의 모든 바이트의 CRC-32(IEEE)

namespace gge {

namespace {

constexpr char kMagic[4] = {'G', 'G', 'S', '1'};
constexpr uint32_t kVersion = 2;       // 쓰는 쪽(에디터 SceneBaker)이 만드는 버전
constexpr uint32_t kMinVersion = 1;    // 읽을 수 있는 가장 낮은 버전
constexpr uint64_t kMaxFileSize = 1ull << 31;  // 2 GiB
constexpr uint32_t kMaxTextures = 1u << 16;
constexpr uint32_t kMaxMeshes = 1u << 16;
constexpr uint32_t kMaxNodes = 1u << 20;
constexpr uint32_t kMaxGameplayItems = 1u << 16;
constexpr uint32_t kMaxElements = 64u * 1024 * 1024;  // 배열 하나의 원소 수 한도(GGM과 같다)
constexpr uint32_t kMaxStringBytes = 1u << 20;
constexpr uint32_t kMaxClips = 1u << 12;
constexpr uint32_t kMaxChannels = 1u << 16;
constexpr size_t kTrailerBytes = 4;

uint32_t Crc32(const uint8_t* data, size_t size)
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// 경계를 검사하며 읽는다. 개수를 믿고 먼저 할당하지 않도록, 배열은 남은 바이트로 충분한지 확인한 뒤에만 만든다.
class Reader {
public:
    Reader(const uint8_t* data, size_t size) : _data(data), _size(size) {}

    size_t Remaining() const { return _size - _pos; }

    template <class T>
    T Read(const char* what)
    {
        T value;
        Take(&value, sizeof(T), what);
        return value;
    }

    uint32_t Count(const char* what, uint32_t max, size_t minBytesPerItem)
    {
        uint32_t n = Read<uint32_t>(what);
        if (n > max)
            throw std::runtime_error(std::string("scene: too many ") + what + " (" + std::to_string(n) + ", max " + std::to_string(max) + ")");
        if (minBytesPerItem > 0 && static_cast<uint64_t>(n) * minBytesPerItem > Remaining())
            throw std::runtime_error(std::string("scene file is truncated while reading ") + what);
        return n;
    }

    template <class T>
    void ReadArray(std::vector<T>& out, uint64_t count, const char* what)
    {
        if (count > kMaxElements || count * sizeof(T) > Remaining())
            throw std::runtime_error(std::string("scene file is truncated while reading ") + what);
        out.resize(static_cast<size_t>(count));
        if (count > 0)
            std::memcpy(out.data(), _data + _pos, static_cast<size_t>(count) * sizeof(T));
        _pos += static_cast<size_t>(count) * sizeof(T);
    }

    std::string ReadString(const char* what)
    {
        uint32_t length = Read<uint32_t>(what);
        if (length > kMaxStringBytes || length > Remaining())
            throw std::runtime_error(std::string("scene file is truncated while reading ") + what);
        std::string s(reinterpret_cast<const char*>(_data + _pos), length);
        _pos += length;
        return s;
    }

    DirectX::XMFLOAT3 ReadVec3(const char* what)
    {
        float v[3];
        Take(v, sizeof v, what);
        return {v[0], v[1], v[2]};
    }

    bool ReadBool(const char* what) { return Read<uint8_t>(what) != 0; }

private:
    void Take(void* out, size_t bytes, const char* what)
    {
        if (bytes > Remaining())
            throw std::runtime_error(std::string("scene file is truncated while reading ") + what);
        std::memcpy(out, _data + _pos, bytes);
        _pos += bytes;
    }

    const uint8_t* _data;
    size_t _size;
    size_t _pos = 0;
};

void ReadMesh(Reader& in, SceneMesh& mesh)
{
    mesh.skinned = in.ReadBool("mesh skinned flag");
    uint32_t vertexCount = in.Read<uint32_t>("mesh vertex count");
    uint32_t indexCount = in.Read<uint32_t>("mesh index count");
    if (vertexCount == 0 || indexCount == 0 || vertexCount > kMaxElements / 8 || indexCount > kMaxElements)
        throw std::runtime_error("scene: invalid mesh header");
    in.ReadArray(mesh.vertices, uint64_t(vertexCount) * 8, "mesh vertices");
    in.ReadArray(mesh.indices, indexCount, "mesh indices");
    if (!mesh.skinned)
        return;

    in.ReadArray(mesh.joints, uint64_t(vertexCount) * 4, "mesh joints");
    in.ReadArray(mesh.weights, uint64_t(vertexCount) * 4, "mesh weights");
    uint32_t bones = in.Read<uint32_t>("bone count");
    if (bones == 0 || bones > static_cast<uint32_t>(kMaxBones))
        throw std::runtime_error("scene: invalid bone count " + std::to_string(bones));
    std::vector<float> inverseBind, t, r, s;
    in.ReadArray(mesh.skeleton.parent, bones, "bone parents");
    in.ReadArray(inverseBind, uint64_t(bones) * 16, "inverse bind matrices");
    in.ReadArray(t, uint64_t(bones) * 3, "rest translations");
    in.ReadArray(r, uint64_t(bones) * 4, "rest rotations");
    in.ReadArray(s, uint64_t(bones) * 3, "rest scales");
    mesh.skeleton.inverseBind.resize(bones);
    mesh.skeleton.rest.resize(bones);
    for (uint32_t b = 0; b < bones; ++b) {
        std::memcpy(&mesh.skeleton.inverseBind[b], &inverseBind[b * 16], 16 * sizeof(float));
        mesh.skeleton.rest[b].translation = {t[b * 3], t[b * 3 + 1], t[b * 3 + 2]};
        mesh.skeleton.rest[b].rotation = {r[b * 4], r[b * 4 + 1], r[b * 4 + 2], r[b * 4 + 3]};
        mesh.skeleton.rest[b].scale = {s[b * 3], s[b * 3 + 1], s[b * 3 + 2]};
    }

    uint32_t clips = in.Count("clips", kMaxClips, 12);
    for (uint32_t c = 0; c < clips; ++c) {
        SceneClip clip;
        clip.name = in.ReadString("clip name");
        clip.clip.duration = in.Read<float>("clip duration");
        uint32_t channels = in.Count("clip channels", kMaxChannels, 12);
        for (uint32_t k = 0; k < channels; ++k) {
            ClipChannel channel;
            channel.bone = in.Read<int32_t>("channel bone");
            uint32_t path = in.Read<uint32_t>("channel path");
            if (path > 2)
                throw std::runtime_error("scene: invalid animation channel path " + std::to_string(path));
            channel.path = static_cast<ChannelPath>(path);
            uint32_t keys = in.Read<uint32_t>("channel key count");
            in.ReadArray(channel.times, keys, "channel times");
            in.ReadArray(channel.values, uint64_t(keys) * (path == 1 ? 4 : 3), "channel values");
            clip.clip.channels.push_back(std::move(channel));
        }
        mesh.clips.push_back(std::move(clip));
    }
}

}  // namespace

SceneData ParseScene(const uint8_t* data, size_t size)
{
    if (size < 12 + kTrailerBytes || std::memcmp(data, kMagic, 4) != 0)
        throw std::runtime_error("not a GGS1 scene file");
    uint32_t version;
    std::memcpy(&version, data + 4, 4);
    if (version < kMinVersion || version > kVersion)
        throw std::runtime_error("unsupported scene version " + std::to_string(version) + " (supported " + std::to_string(kMinVersion) + ".." + std::to_string(kVersion) + ")");
    uint32_t storedCrc;
    std::memcpy(&storedCrc, data + size - kTrailerBytes, 4);
    if (Crc32(data, size - kTrailerBytes) != storedCrc)
        throw std::runtime_error("scene file is corrupt (checksum mismatch)");

    Reader in(data + 8, size - 8 - kTrailerBytes);
    SceneData scene;

    uint32_t textures = in.Count("textures", kMaxTextures, 4);
    for (uint32_t i = 0; i < textures; ++i) {
        uint32_t length = in.Read<uint32_t>("texture size");
        std::vector<uint8_t> bytes;
        in.ReadArray(bytes, length, "texture bytes");
        scene.textures.push_back(std::move(bytes));
    }

    uint32_t meshes = in.Count("meshes", kMaxMeshes, 9);
    for (uint32_t i = 0; i < meshes; ++i) {
        scene.meshes.emplace_back();
        ReadMesh(in, scene.meshes.back());
    }

    uint32_t nodes = in.Count("nodes", kMaxNodes, 4);
    for (uint32_t i = 0; i < nodes; ++i) {
        SceneNodeData node;
        node.name = in.ReadString("node name");
        node.parent = in.Read<int32_t>("node parent");
        node.mesh = in.Read<int32_t>("node mesh");
        node.texture = in.Read<int32_t>("node texture");
        node.position = in.ReadVec3("node position");
        node.rotation = in.ReadVec3("node rotation");
        node.scale = in.ReadVec3("node scale");
        node.color = in.ReadVec3("node color");
        node.alpha = in.Read<float>("node alpha");
        node.castsShadow = in.ReadBool("node shadow flag");
        node.clip = in.ReadString("node clip");
        node.playing = in.ReadBool("node playing flag");
        node.speed = in.Read<float>("node speed");
        const std::string who = "node '" + node.name + "'";
        if (node.parent < -1 || node.parent >= static_cast<int>(i))
            throw std::runtime_error("scene: " + who + " has parent " + std::to_string(node.parent) + " (a parent must come before its children)");
        if (node.mesh < -1 || node.mesh >= static_cast<int>(scene.meshes.size()))
            throw std::runtime_error("scene: " + who + " references mesh " + std::to_string(node.mesh) + " which does not exist");
        if (node.texture < -1 || node.texture >= static_cast<int>(scene.textures.size()))
            throw std::runtime_error("scene: " + who + " references texture " + std::to_string(node.texture) + " which does not exist");
        scene.nodes.push_back(std::move(node));
    }

    SceneLightingData& light = scene.lighting;
    light.ambient = in.ReadVec3("ambient");
    light.sunDirection = in.ReadVec3("sun direction");
    light.sunColor = in.ReadVec3("sun color");
    uint32_t points = in.Count("point lights", static_cast<uint32_t>(kMaxPointLights), 28);
    for (uint32_t i = 0; i < points; ++i) {
        ScenePointLight p;
        p.position = in.ReadVec3("point light position");
        p.color = in.ReadVec3("point light color");
        p.range = in.Read<float>("point light range");
        light.points.push_back(p);
    }
    light.shadowEnabled = in.ReadBool("shadow flag");
    light.shadowPlaneY = in.Read<float>("shadow plane");
    light.shadowOpacity = in.Read<float>("shadow opacity");

    uint32_t rooms = in.Count("rooms", kMaxGameplayItems, 12);
    for (uint32_t i = 0; i < rooms; ++i) {
        SceneRoomData room;
        room.name = in.ReadString("room name");
        uint32_t vertices = in.Read<uint32_t>("room vertex count");
        in.ReadArray(room.polygon, uint64_t(vertices) * 2, "room polygon");
        float camera[7];
        for (float& f : camera)
            f = in.Read<float>("room camera");
        room.camera = {{camera[0], camera[1], camera[2]}, {camera[3], camera[4], camera[5]}, camera[6]};
        scene.rooms.push_back(std::move(room));
    }

    uint32_t colliders = in.Count("colliders", kMaxGameplayItems, version >= 2 ? 20 : 16);
    for (uint32_t i = 0; i < colliders; ++i) {
        SceneColliderData c;
        if (version >= 2)
            c.name = in.ReadString("collider name");
        c.minX = in.Read<float>("collider");
        c.minZ = in.Read<float>("collider");
        c.maxX = in.Read<float>("collider");
        c.maxZ = in.Read<float>("collider");
        scene.colliders.push_back(c);
    }

    uint32_t triggers = in.Count("triggers", kMaxGameplayItems, 12);
    for (uint32_t i = 0; i < triggers; ++i) {
        SceneTriggerData t;
        t.name = in.ReadString("trigger name");
        uint32_t vertices = in.Read<uint32_t>("trigger vertex count");
        in.ReadArray(t.polygon, uint64_t(vertices) * 2, "trigger polygon");
        t.tag = in.Read<int32_t>("trigger tag");
        scene.triggers.push_back(std::move(t));
    }

    ScenePlayerData& player = scene.player;
    player.present = in.ReadBool("player flag");
    if (player.present) {
        player.node = in.Read<int32_t>("player node");
        player.hasSpawn = in.ReadBool("player spawn flag");
        player.spawn[0] = in.Read<float>("player spawn");
        player.spawn[1] = in.Read<float>("player spawn");
        player.radius = in.Read<float>("player radius");
        player.speed = in.Read<float>("player speed");
        if (player.node < 0 || player.node >= static_cast<int>(scene.nodes.size()))
            throw std::runtime_error("scene: the player node " + std::to_string(player.node) + " does not exist");
        if (scene.nodes[player.node].parent != -1)
            throw std::runtime_error("scene: the player node '" + scene.nodes[player.node].name + "' must be a root node");
    }

    if (in.Remaining() != 0)
        throw std::runtime_error("scene file has " + std::to_string(in.Remaining()) + " unexpected trailing bytes");
    return scene;
}

SceneData ReadSceneFile(const std::string& pathUtf8)
{
    std::ifstream file(Utf8ToWide(pathUtf8), std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot open file: " + pathUtf8);
    const uint64_t size = static_cast<uint64_t>(file.tellg());
    if (size > kMaxFileSize)
        throw std::runtime_error("scene file is too large");
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("failed reading scene file: " + pathUtf8);
    return ParseScene(bytes.data(), bytes.size());
}

}  // namespace gge
