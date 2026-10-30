#pragma once
#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

#include "animation.h"
#include "world.h"

namespace gge {

// 구운(baked) 씬 파일 .ggs: 에디터(C# SceneBaker)가 씬 JSON과 거기서 참조하는 메시/텍스처/glTF를 하나의 파일로 합친 것.
// 독립 플레이어는 이 파일만 있으면 실행된다(JSON/OBJ/glTF 파서가 필요 없다). 리틀 엔디언, 형식은 scene_io.cpp 맨 위 주석이 기준이다.
// 이 헤더의 구조체는 "읽은 그대로의 순수 데이터"이며 GPU/D3D에 의존하지 않는다.

struct SceneClip {
    std::string name;
    Clip clip;
};

struct SceneMesh {
    std::vector<float> vertices;  // 정점당 8 float: position(3) normal(3) uv(2)
    std::vector<uint32_t> indices;
    bool skinned = false;
    std::vector<uint32_t> joints;  // skinned일 때 정점당 4개
    std::vector<float> weights;    // skinned일 때 정점당 4개
    Skeleton skeleton;             // skinned일 때만 의미 있음
    std::vector<SceneClip> clips;
};

struct SceneNodeData {
    std::string name;
    int parent = -1;   // 노드 목록에서의 부모 번호. 부모가 항상 앞에 온다. -1 = 루트
    int mesh = -1;     // 메시 번호(-1 = 그룹 노드)
    int texture = -1;  // 텍스처 번호(-1 = 없음)
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT3 rotation{0, 0, 0};
    DirectX::XMFLOAT3 scale{1, 1, 1};
    DirectX::XMFLOAT3 color{0.8f, 0.8f, 0.8f};
    float alpha = 1;
    bool castsShadow = false;
    std::string clip;  // 재생할 클립 이름(빈 문자열 = 레스트 포즈)
    bool playing = true;
    float speed = 1;
};

struct ScenePointLight {
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT3 color{1, 1, 1};
    float range = 1;
};

struct SceneLightingData {
    DirectX::XMFLOAT3 ambient{0.25f, 0.25f, 0.25f};
    DirectX::XMFLOAT3 sunDirection{-0.4f, -1.0f, -0.6f};
    DirectX::XMFLOAT3 sunColor{0.75f, 0.75f, 0.75f};
    std::vector<ScenePointLight> points;
    bool shadowEnabled = false;
    float shadowPlaneY = 0;
    float shadowOpacity = 0.5f;
};

struct SceneRoomData {
    std::string name;
    std::vector<float> polygon;  // x,z 쌍
    RoomCamera camera{};
};

struct SceneColliderData {
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
};

struct SceneTriggerData {
    std::string name;
    std::vector<float> polygon;
    int tag = 0;
};

struct ScenePlayerData {
    bool present = false;
    int node = -1;  // 노드 목록에서의 번호(루트 노드여야 한다)
    bool hasSpawn = false;
    float spawn[2] = {0, 0};
    float radius = 0.4f;
    float speed = 3;
};

struct SceneData {
    std::vector<std::vector<uint8_t>> textures;  // 이미지 파일(PNG/BMP/JPEG) 원본 바이트. 로드할 때 WIC로 디코딩한다
    std::vector<SceneMesh> meshes;
    std::vector<SceneNodeData> nodes;
    SceneLightingData lighting;
    std::vector<SceneRoomData> rooms;
    std::vector<SceneColliderData> colliders;
    std::vector<SceneTriggerData> triggers;
    ScenePlayerData player;
};

// .ggs 바이트를 읽고 구조(개수 한도, 번호 범위, 부모 순서, CRC)를 검증한다. 값 자체(메시 인덱스, 다각형 모양 등)의 검증은
// 렌더러/월드가 객체를 만들 때 한다. 잘못된 파일은 사유가 담긴 std::runtime_error.
SceneData ParseScene(const uint8_t* data, size_t size);
SceneData ReadSceneFile(const std::string& pathUtf8);

}  // namespace gge
