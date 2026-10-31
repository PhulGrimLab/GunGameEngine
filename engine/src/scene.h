#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "renderer.h"
#include "scene_io.h"
#include "world.h"

namespace gge {

// Scene이 내보내는 트리거 이벤트(영역 이름 포함). 같은 프레임 안에서는 Exit가 먼저, 그다음 Enter이며 각각 트리거 순서다.
struct SceneEvent {
    int kind = 0;  // kTriggerEnter / kTriggerExit
    int tag = 0;
    std::string triggerName;
};

// 구운 씬(SceneData)을 렌더러/월드에 올려 실행하는 런타임. 에디터의 플레이 모드(C# PlaySession + SceneSync)와 같은 규칙이다:
//  - 프레임 순서(Step): 입력 -> 활성 방 카메라 기준 이동 -> 충돌 이동 -> 활성 방 갱신(카메라 컷) -> 트리거 갱신
//    -> 애니메이션 진행 -> 플레이어 변환 적용 -> 렌더러 카메라를 활성 방 카메라로.
//  - 플레이어 노드는 루트 노드여야 하고, 모델 앞쪽이 +Z라고 가정해 이동 방향으로 Y축 회전(Yaw)을 돌린다.
//  - 애니메이션: 클립이 지정된 스킨 노드는 시간 0에서 시작해 speed 배율로 계속 반복한다(playing이 false면 시간 0 자세로 정지).
// Owner: 호출자(GgeScene)가 소유하며 렌더러보다 먼저 파괴해야 한다. 만든 오브젝트/조명/리소스는 소멸자에서 렌더러에서 제거한다.
// 스레드 안전하지 않다(렌더러와 같은 스레드에서만 사용).
class Scene {
public:
    // 실패하면 사유가 담긴 예외를 던지고, 그때까지 만든 것은 모두 정리한다. 렌더러의 조명/그림자 설정은 씬의 값으로 바뀐다.
    Scene(Renderer& renderer, SceneData data);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // dt는 유한하고 0 이상이어야 한다. 입력 축은 -1..1(오른쪽+, 화면 위쪽+).
    void Step(float inputX, float inputY, float dt);

    // 가장 오래된 이벤트를 꺼낸다. 없으면 false. 시작할 때 이미 영역 안이면 그 Enter가 첫 이벤트다.
    bool PollEvent(SceneEvent& out);

    bool HasPlayer() const { return _player.present; }
    Vec2 PlayerPosition() const { return _position; }
    float PlayerYaw() const { return _yaw; }
    // 활성 방 이름(방이 없으면 빈 문자열).
    const std::string& ActiveRoomName() const;
    // 이름이 같은 첫 노드의 렌더러 오브젝트 ID. 없으면 0.
    int FindObject(const std::string& nodeName) const;

    // 이름이 같은 트리거를 모두 켜거나 끄고 바뀐 개수(찾은 개수)를 반환한다. 없으면 0.
    // 이벤트는 World::SetTriggerEnabled 규칙대로 다음 Step에서 나온다(안에 있는 채로 끄면 Exit, 다시 켜면 Enter).
    int SetTriggerEnabled(const std::string& name, bool enabled);

    // 이름이 같은 충돌체를 모두 켜거나 끄고 바뀐(찾은) 개수를 반환한다. 없으면 0. 빈 이름은 "이름 없음"이라 지정할 수 없다(호출자가 거부한다).
    int SetColliderEnabled(const std::string& name, bool enabled);

    // 지금 플레이어가 이름이 같은 트리거 중 하나 안에 있는가(마지막 갱신 기준: 끄거나 켠 결과는 다음 Step부터). 그런 이름이 없으면 예외.
    bool IsInsideTrigger(const std::string& name) const;

    // 이름이 같은 첫 노드의 부모 기준(로컬) 위치. 마지막으로 설정한 값(처음에는 구운 값)이다. 없으면 false.
    bool GetNodePosition(const std::string& nodeName, DirectX::XMFLOAT3& out) const;
    // 이름이 같은 첫 노드의 로컬 위치를 바꾼다(회전/크기는 그대로). 그런 노드가 없거나, 값이 유한하지 않거나,
    // 플레이어 노드면 예외(플레이어는 TeleportPlayer/이동으로만 움직인다). 상태는 바뀌지 않는다.
    void SetNodePosition(const std::string& nodeName, DirectX::XMFLOAT3 position);

    // 로컬 회전(오일러 pitch X, yaw Y, roll Z, 라디안: 렌더러/씬 파일과 같은 규약)과 로컬 크기. 읽기는 없으면 false.
    // 쓰기는 위치와 같은 규칙: 없는 노드/비유한 값/플레이어 노드는 예외(상태 그대로).
    bool GetNodeRotation(const std::string& nodeName, DirectX::XMFLOAT3& out) const;
    void SetNodeRotation(const std::string& nodeName, DirectX::XMFLOAT3 rotation);
    bool GetNodeScale(const std::string& nodeName, DirectX::XMFLOAT3& out) const;
    void SetNodeScale(const std::string& nodeName, DirectX::XMFLOAT3 scale);

    // 이름이 같은 첫 노드의 표시 여부(처음엔 true). 숨기면 그 노드와 모든 자손이 그려지지 않고 그림자도 없다. 알파와 별개 상태다(다시 켜면
    // 알파는 그대로). 읽기는 없으면 false, 쓰기는 없는 노드면 예외. 플레이어 노드도 숨길 수 있다(위치/회전/크기와 달리 플레이어 규칙과 충돌하지 않는다).
    bool GetNodeVisible(const std::string& nodeName, bool& out) const;
    void SetNodeVisible(const std::string& nodeName, bool visible);

    // 플레이어를 바닥 위치 (x,z)로 즉시 옮긴다(문/방 이동용). 입력 방향 기준을 비우고(카메라 컷 보정 해제), 활성 방과
    // 트리거를 새 위치로 갱신하며 렌더러 카메라/플레이어 변환에 바로 반영한다. 방향(yaw)은 그대로다.
    // 플레이어가 없거나, 값이 유한하지 않거나, 어떤 방에도 속하지 않으면 예외(상태는 바뀌지 않는다).
    // 충돌체 안으로 옮기는지는 검사하지 않는다(호출자가 문 앞 같은 열린 자리를 골라야 한다).
    void TeleportPlayer(float x, float z);

private:
    // 이름이 같은 첫 노드의 번호. 없거나 플레이어 노드면 예외(위치/회전/크기 쓰기 전용: 플레이어는 이동 규칙이 정한다).
    size_t EditableNodeIndex(const std::string& nodeName) const;

    struct Animated {
        int object = 0;
        int clip = 0;  // 렌더러 클립 ID
        float duration = 0;
        float time = 0;
        float speed = 1;
        bool playing = true;
    };

    void Build();
    void Destroy();
    void ApplyPlayer();
    void UpdateTriggers();

    Renderer& _renderer;
    SceneData _data;
    World _world;
    std::vector<int> _textureIds, _meshIds, _skeletonIds;  // 메시 번호 -> 렌더러 ID(스켈레톤 없으면 0)
    std::vector<std::vector<int>> _clipIds;                 // 메시 번호 -> 클립 번호 -> 렌더러 클립 ID
    std::vector<int> _objectIds;                            // 노드 번호 -> 렌더러 오브젝트 ID
    std::vector<int> _rootObjects;
    std::vector<int> _lightIds;
    std::vector<Animated> _animated;
    std::deque<SceneEvent> _events;

    ScenePlayerData _player;
    Vec2 _position{0, 0};
    float _yaw = 0;
    float _baseHeight = 0;
    DirectX::XMFLOAT3 _baseRotation{0, 0, 0};
};

}  // namespace gge
