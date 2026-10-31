#pragma once
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "renderer.h"
#include "scene_io.h"
#include "world.h"

namespace gge {

// 동적 오브젝트(SpawnObject)의 한도. 프로토타입 값이며 P8(전투)에서 다시 정한다.
constexpr int kMaxDynamicNodes = 256;            // 살아 있는 복제 노드 합계(자손 포함)
constexpr int kMaxSpawnsPerStep = 64;            // Step 한 번(= 한 프레임) 사이의 SpawnObject 호출 수
constexpr int kMaxCloneDepth = 16;               // 복제하는 서브트리의 깊이(템플릿 자신 = 1)
constexpr size_t kMaxInstanceNameBytes = 128;    // 인스턴스 이름의 UTF-8 바이트 수

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
// 클립 재생 상태 기계. C# AnimationPlayer(tools/GunGame.Interop/Animation)와 같은 규칙이며 교차 검증 테스트가 프레임마다 본 행렬을 비교한다.
// 시간은 호출자가 Update(dt)로 진행시키고 CurrentPose를 렌더러 SetObjectPose에 그대로 넘긴다.
class ClipPlayer {
public:
    struct Ref {
        int id = 0;            // 렌더러 클립 ID(0 = 없음)
        float duration = 0;
        bool operator==(const Ref&) const = default;
    };
    struct Pose {
        int clipA = 0;  // 0 = 레스트 포즈
        float timeA = 0;
        int clipB = 0;  // 0 = 블렌딩 없음
        float timeB = 0;
        float blend = 0;
    };

    bool playing = true;  // 일시정지 스위치(false면 시간과 페이드 진행이 모두 멈춘다)
    float speed = 1;      // 음수면 거꾸로(유한해야 한다: 호출자가 검사)

    // 같은 클립이면(loop가 달라도) restart가 아닌 한 무시한다. 다르면 0초부터 시작하고, 레스트가 아니며 fade > 0이면 이전 클립에서 크로스페이드한다.
    // restart가 true면 같은 클립도 0초부터 다시 시작한다(페이드 없이 즉시, loop 값은 새로 적용).
    void Play(const Ref& clip, float fadeSeconds, bool restart, bool loop);
    void Clear();  // 레스트 포즈로 복귀
    void Update(float dt);
    Pose CurrentPose() const;

    bool HasClip() const { return _hasCurrent; }
    const Ref& Current() const { return _current; }
    float Time() const { return _currentTime; }
    bool Loop() const { return _currentLoop; }
    bool Fading() const { return _hasFrom; }
    // 한 번 재생(loop=false)이 재생 방향의 끝에 닿아 마지막 포즈로 멈춰 있다: speed >= 0이면 끝 시간, 음수면 0초. 반복/레스트는 false.
    bool Finished() const;

private:
    bool _hasCurrent = false, _currentLoop = true;
    Ref _current;
    float _currentTime = 0;
    bool _hasFrom = false, _fromLoop = true;
    Ref _from;
    float _fromTime = 0, _fadeElapsed = 0, _fadeDuration = 0;
};

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

    // ---- 클립 재생(스킨 메시 노드, 플레이어 노드 포함) ----
    // 대상은 이름이 같은 첫 노드. 오류(예외): 없는 노드, 스킨이 아닌 노드("has no skeleton animation"), fadeSeconds가 유한한 0..60이 아님,
    // 없는 클립 이름(대소문자 구분, 쓸 수 있는 이름 목록 포함: 20개를 넘으면 앞 20개 + 총 개수).
    void PlayClip(const std::string& nodeName, const std::string& clipName, float fadeSeconds, bool restart, bool loop);
    void StopClip(const std::string& nodeName);                           // 레스트 포즈로 복귀
    void SetClipSpeed(const std::string& nodeName, float speed);          // 유한(음수 허용)
    void SetClipPlaying(const std::string& nodeName, bool playing);       // false = 일시정지(페이드 진행도 멈춘다)
    struct NodeAnimation {
        std::string clip;  // 재생 중인 클립 이름, 레스트 포즈면 빈 문자열
        float time = 0;
        bool playing = true, fading = false, loop = true, finished = false;
        float speed = 1;
    };
    NodeAnimation GetNodeAnimation(const std::string& nodeName) const;    // 없는 노드/스킨 아닌 노드는 예외

    // ---- 동적 오브젝트(생성/삭제) ----
    // 씬 파일의 노드(템플릿)와 그 모든 자손을 복제한다. 메시/텍스처/스켈레톤/클립은 공유하고 오브젝트만 새로 만든다.
    //  - 복제 루트 이름 = instanceName, 자손 이름 = "instanceName/원래이름". 이후 모든 이름 기반 API(위치/표시/클립 ...)가 이 이름으로 동작한다.
    //    이름은 비어 있거나 '/'를 포함하거나 kMaxInstanceNameBytes를 넘거나 이미 있는 어떤 노드 이름(구운/동적, 자손 이름 포함)과 같으면 예외.
    //  - 시작 상태는 템플릿의 **구운 값**(스크립트가 템플릿을 옮기거나 알파를 바꿔도 영향 없음), 복제 루트는 항상 보임.
    //    position만 인자로 덮어쓰며 **템플릿과 같은 부모 기준의 로컬 좌표**다(부모가 루트가 아니면 월드 좌표와 다르다). 회전/크기는 템플릿의 구운 값이라
    //    방향은 SpawnObject 직후 SetNodeRotation으로 정한다(그 사이 렌더/Step이 끼지 않으므로 한 프레임도 잘못된 방향으로 그려지지 않는다).
    //  - 템플릿은 씬 파일의 노드여야 한다(복제본을 다시 복제할 수 없다). 플레이어 노드, 플레이어를 자손으로 가진 노드는 불가.
    //    조명/충돌체/트리거는 복제하지 않는다(동적 충돌체는 P8). 스킨 메시는 복제마다 독립된 재생 상태를 가진다(초기 클립은 구운 값).
    //  - 모두 성공하거나 모두 실패한다: 이름/한도(kMaxDynamicNodes, kMaxSpawnsPerStep, kMaxCloneDepth)는 렌더러 오브젝트를 만들기 전에 검사하고,
    //    만드는 도중 실패해도 만든 것을 되돌린다(상태 불변, 누수 없음). 없는 템플릿도 예외.
    void SpawnObject(const std::string& templateName, const std::string& instanceName, DirectX::XMFLOAT3 position);
    // SpawnObject로 만든 인스턴스(루트 이름)를 자손까지 지운다. 그 이름은 다시 쓸 수 있다. 씬 파일의 노드/복제 자손 단독/없는 이름은 예외(숨기려면 SetNodeVisible).
    void DestroyObject(const std::string& instanceName);
    // 그 이름의 노드가 있는가(구운/동적 구분 없음).
    bool ObjectExists(const std::string& name) const { return _nodeByName.count(name) != 0; }
    // 살아 있는 복제 노드 수(자손 포함)와 인스턴스 수.
    int DynamicNodeCount() const { return _liveDynamic; }
    int InstanceCount() const { return static_cast<int>(_instances.size()); }
    // 노드 슬롯 수(구운 노드 + 복제가 쓴 슬롯의 최대치). 지운 슬롯을 재사용하므로 복제 노드 한도(kMaxDynamicNodes)를 넘어 늘지 않는다.
    int NodeSlotCount() const { return static_cast<int>(_data.nodes.size()); }
    // 테스트 전용: SpawnObject가 n번째 렌더러 오브젝트를 만든 직후 실패하게 한다(-1 = 끔). 되돌림 검증용.
    void FailSpawnAfterNodesForTests(int n) { _failSpawnAfter = n; }

    // 마지막 Step에서 충돌/슬라이딩을 반영한 뒤 플레이어가 실제로 이동한 거리 / dt(월드 단위/초). 벽을 정면으로 밀면 0, 벽을 따라 미끄러지면 0보다 크다.
    // 텔레포트 직후(다음 Step 전)와 dt가 0인 Step, 플레이어가 없는 씬은 0이다. 켜진 충돌체가 겹친 플레이어를 밀어내는 이동도 이동으로 센다.
    float PlayerSpeed() const { return _playerSpeed; }

    // 플레이어를 바닥 위치 (x,z)로 즉시 옮긴다(문/방 이동용). 입력 방향 기준을 비우고(카메라 컷 보정 해제), 활성 방과
    // 트리거를 새 위치로 갱신하며 렌더러 카메라/플레이어 변환에 바로 반영한다. 방향(yaw)은 그대로다.
    // 플레이어가 없거나, 값이 유한하지 않거나, 어떤 방에도 속하지 않으면 예외(상태는 바뀌지 않는다).
    // 충돌체 안으로 옮기는지는 검사하지 않는다(호출자가 문 앞 같은 열린 자리를 골라야 한다).
    void TeleportPlayer(float x, float z);

private:
    // 이름이 같은 첫 노드의 번호. 없거나 플레이어 노드면 예외(위치/회전/크기 쓰기 전용: 플레이어는 이동 규칙이 정한다).
    size_t EditableNodeIndex(const std::string& nodeName) const;

    // 스킨 메시를 쓰는 노드마다 하나(초기 clip이 없어도): 스크립트가 나중에 클립을 시작할 수 있다.
    struct NodeAnim {
        size_t node = 0;
        int object = 0;  // 렌더러 오브젝트 ID
        int mesh = 0;    // 씬의 메시 번호
        ClipPlayer player;
    };
    // 이름이 같은 첫 노드의 _anims 번호. 없는 노드/스킨이 아닌 노드는 예외.
    size_t AnimIndex(const std::string& nodeName) const;
    void ApplyPose(const NodeAnim& anim);  // 렌더러에 현재 포즈를 올린다
    // 이름이 같은 첫 노드의 번호(구운 노드가 먼저, 같은 이름이면 가장 낮은 번호). 없으면 -1.
    int FindNode(const std::string& name) const;
    // 노드 데이터대로 렌더러 오브젝트를 만들고(스킨이면 anim도 채운다) ID를 돌려준다. 실패하면 만든 오브젝트를 지우고 예외.
    int CreateNodeObject(const SceneNodeData& node, size_t slot, int parentObject, std::optional<NodeAnim>& anim);

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
    std::vector<NodeAnim> _anims;
    std::vector<int> _animOfNode;  // 노드 번호 -> _anims 번호(스킨 노드가 아니면 -1)
    std::deque<SceneEvent> _events;

    // 동적 오브젝트. 노드 슬롯(_data.nodes/_objectIds/_animOfNode의 번호)은 [0, _bakedCount)가 씬 파일 노드, 그 뒤가 복제 노드(빈 슬롯은 재사용)다.
    struct Instance {
        std::vector<size_t> slots;  // 루트가 첫 번째
        int rootObject = 0;
    };
    std::vector<SceneNodeData> _bakedNodes;                 // 로드 시점 스냅샷(복제의 시작 값). _data.nodes는 스크립트가 바꾸는 현재 값
    size_t _bakedCount = 0;
    std::unordered_map<std::string, size_t> _nodeByName;    // 이름 -> 슬롯(같은 이름이면 가장 낮은 번호)
    std::unordered_map<std::string, Instance> _instances;   // 루트 이름 -> 인스턴스
    std::vector<size_t> _freeSlots;
    int _liveDynamic = 0;
    int _spawnsThisStep = 0;
    int _failSpawnAfter = -1;

    ScenePlayerData _player;
    Vec2 _position{0, 0};
    float _playerSpeed = 0;  // PlayerSpeed() 참고
    float _yaw = 0;
    float _baseHeight = 0;
    DirectX::XMFLOAT3 _baseRotation{0, 0, 0};
};

}  // namespace gge
