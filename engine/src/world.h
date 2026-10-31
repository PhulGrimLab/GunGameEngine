#pragma once
#include <deque>
#include <vector>

#include "collision.h"

namespace gge {

// 트리거 이벤트 종류(gge.h의 GGE_TRIGGER_*와 같은 값).
constexpr int kTriggerEnter = 1;
constexpr int kTriggerExit = 2;

// 트리거 영역에 플레이어가 들어오거나 나갔다는 이벤트. tag는 영역을 만들 때 호출자가 정한 값(문/아이템 연결용)이다.
struct TriggerEvent {
    int triggerId;
    int kind;  // kTriggerEnter / kTriggerExit
    int tag;
};

// 방에 배정된 고정 카메라. 값은 gge_renderer_set_camera에 그대로 넘길 수 있다.
struct RoomCamera {
    float eye[3];
    float target[3];
    float fovY;
};

// 게임플레이 월드. 렌더러와 독립이라 D3D 없이 검증할 수 있다.
// 지금은 고정 카메라 방과 정적 충돌체(AABB)를 가지며, 이후 입력/트리거가 같은 월드에 더해진다.
//
// 방: 바닥(XZ 평면) 위 단순 다각형 영역 하나에 고정 카메라 하나가 대응한다. Y(높이)는 보지 않는다.
// Owner: GgeWorld(capi.cpp)가 소유한다. 스레드 안전하지 않으며 호출자가 한 스레드에서 사용한다.
// Lifetime: gge_world_create부터 gge_world_destroy까지. 방 ID는 1부터 부여되고 재사용되지 않는다.
class World {
public:
    // 방을 추가하고 ID(>0)를 반환한다. xz는 (x,z) 쌍의 꼭짓점 배열로 시계/반시계 모두 허용한다.
    // 꼭짓점이 3개 미만, 유한하지 않은 값, 면적 0, 변이 서로 교차(자기 교차)하면 예외를 던진다.
    // 카메라는 eye != target이고 0 < fovY < pi여야 한다.
    int AddRoom(const float* xz, int vertexCount, const RoomCamera& camera);

    // 점 (x,z)를 포함하는 가장 앞선(ID가 작은) 방의 ID. 없으면 0. 경계 위의 점은 포함으로 본다.
    int FindRoom(float x, float z) const;

    // 플레이어 위치로 활성 방을 갱신하고 갱신된 활성 방 ID를 반환한다(처음 방에 들어가기 전에는 0).
    // 현재 활성 방이 점을 아직 포함하면 유지한다(방이 겹친 경계에서 카메라가 깜빡이는 것을 막는다).
    // 벗어났으면 FindRoom 결과로 바꾼다. 어떤 방에도 속하지 않으면 마지막 활성 방을 유지한다
    // (방 사이 틈이나 바닥 밖에서 카메라가 갑자기 사라지지 않게 하기 위함).
    int UpdateActiveRoom(float x, float z);

    int ActiveRoom() const { return _activeRoom; }

    // 정적 장애물(XZ 평면의 축 정렬 사각형)을 추가하고 ID(>0)를 반환한다.
    // 값이 유한하지 않거나 min >= max이면 예외를 던진다.
    int AddBoxCollider(float minX, float minZ, float maxX, float maxZ);

    // 콜라이더를 켜거나 끈다(처음에는 모두 켜져 있다). 꺼진 콜라이더는 MoveCircle이 없는 것으로 본다(열린 문). 켜는 순간 그 안에 겹쳐 있는
    // 이동체는 별도 처리 없이 기존 규칙대로 다음 MoveCircle에서 가장 가까운 면 바깥으로 밀려난다(이동량 0이어도 밀어낸다).
    // 밀려나는 쪽은 가장 가까운 면 바깥이라, 얇은 문이 방 경계에 걸쳐 있으면 반대편(다른 방)으로 나갈 수 있다. 문은 방 안쪽에 두는 것이 좋다.
    // 같은 상태로 다시 설정해도 변화가 없다. 알 수 없는 ID면 예외.
    void SetColliderEnabled(int colliderId, bool enabled);
    bool IsColliderEnabled(int colliderId) const;

    // 반지름 radius인 원형 이동체를 (x,z)에서 (dx,dz)만큼 움직인 최종 위치를 반환한다(벽 슬라이딩 포함, collision.h 참고).
    // radius <= 0, 유한하지 않은 값, 한 번의 이동량이 radius 대비 너무 크면(kMaxMoveSteps 초과) 예외를 던진다.
    Vec2 MoveCircle(float x, float z, float dx, float dz, float radius) const;

    // 카메라 상대 이동: 입력 축(inputX 오른쪽+, inputY 앞(화면 위쪽)+, 각각 -1..1)을 한 프레임의 월드 이동량(dx,dz)으로
    // 바꾼다. 앞 = 카메라 시선의 수평 성분, 오른쪽 = 앞 x 위(오른손 Y-up). 속도 speed(월드 단위/초) x dt(초).
    // 입력 크기는 1로 제한하고(대각선이 빠르지 않게), 데드존(0.2) 이하는 0, 그 위는 (크기-0.2)/0.8로 0부터 부드럽게 늘어난다.
    //
    // 카메라 컷: 입력이 눌린 채 활성 방이 바뀌면, 컷 직후의 입력 방향에서 60도 넘게 꺾기 전까지는 방향 기준을
    // 입력을 시작한 방 카메라로 유지한다(컷 직후 화면이 뒤집혀 캐릭터가 되돌아가는 것 방지). 입력을 놓거나(데드존 미만)
    // 크게 꺾으면 현재 활성 방 기준으로 갱신한다. 텔레포트/컷신처럼 입력이 끊기지 않는 채 장면이 바뀌면
    // ResetInputFrame으로 기준을 즉시 비운다. 활성 방이 없으면(UpdateActiveRoom 전) 예외를 던진다. 비유한 값, speed/dt < 0도 예외.
    Vec2 MoveInput(float inputX, float inputY, float speed, float dt);

    // 입력 방향 기준을 비운다. 다음 MoveInput은 그 시점의 활성 방 카메라를 기준으로 시작한다.
    void ResetInputFrame();

    // 방 카메라. 알 수 없는 ID면 예외를 던진다.
    const RoomCamera& GetRoomCamera(int roomId) const;

    // ---- 트리거 ----
    // 플레이어 한 명의 위치(점)로 영역 진입/퇴장을 판정한다. 영역은 방과 같은 규칙의 XZ 평면 다각형(경계 포함)이다.

    // 트리거 영역을 추가하고 ID(>0)를 반환한다. 다각형이 잘못되면(AddRoom과 같은 검사) 예외. tag는 이벤트에 그대로 실린다.
    int AddTrigger(const float* xz, int vertexCount, int tag);

    // 꺼진 트리거는 "플레이어가 밖에 있는 것"으로 취급한다: 안에 있는 채로 끄면 다음 UpdateTriggers에서 Exit,
    // 안에 있는 채로 다시 켜면 Enter 이벤트가 생긴다. 알 수 없는 ID면 예외.
    void SetTriggerEnabled(int triggerId, bool enabled);

    // 플레이어 위치를 반영해 상태가 바뀐 트리거마다 이벤트를 큐에 쌓고, 이번 호출에서 생긴 이벤트 수를 반환한다.
    // 한 번의 호출 안에서는 Exit가 모두 먼저, 그다음 Enter가 오고 각각 트리거 ID 순이다(A에서 나와 B로 들어가면 Exit A, Enter B).
    // 처음 호출에서 이미 안에 있으면 Enter가 생긴다. 비유한 값이면 예외.
    // 주의: 위치를 프레임마다 샘플링하므로 한 프레임 이동 거리보다 얇은 영역은 건너뛸 수 있다.
    int UpdateTriggers(float x, float z);

    // 가장 오래된 이벤트 하나를 꺼내 out에 쓰고 true. 없으면 false. 큐는 1024개까지이며 가득 차면 가장 오래된 것을 버린다
    // (호출자가 폴링하지 않는 버그가 있어도 메모리가 무한히 늘지 않게 한다).
    bool PollTriggerEvent(TriggerEvent& out);

    // 마지막 UpdateTriggers 기준으로 플레이어가 영역 안에 있는가. 끈 트리거는 다음 UpdateTriggers부터 false(이벤트와 같은 시점).
    // 알 수 없는 ID면 예외.
    bool IsInsideTrigger(int triggerId) const;

private:
    struct Room {
        std::vector<float> polygon;  // x0,z0,x1,z1,...
        RoomCamera camera;
        float forward[2];  // 시선의 수평 성분(정규화). AddRoom이 한 번 계산한다.
    };

    struct Trigger {
        std::vector<float> polygon;
        int tag = 0;
        bool enabled = true;
        bool inside = false;  // 마지막 UpdateTriggers에서 (켜져 있고) 안에 있었는가
    };

    const Room& RoomById(int roomId) const;
    Trigger& TriggerById(int triggerId);
    const Trigger& TriggerById(int triggerId) const;
    void PushEvent(const Trigger& trigger, int triggerId, int kind);

    std::vector<Box> _boxes;  // 인덱스 + 1 == 콜라이더 ID
    std::vector<char> _boxEnabled;  // _boxes와 같은 인덱스(0/1). vector<bool>의 특수한 참조 동작을 피하려고 char를 쓴다.
    std::vector<Box> _activeBoxes;  // 켜진 콜라이더만(MoveCircle이 쓴다). 켜고 끌 때마다 다시 만든다.
    std::vector<Room> _rooms;  // 인덱스 + 1 == 방 ID
    std::vector<Trigger> _triggers;  // 인덱스 + 1 == 트리거 ID
    std::deque<TriggerEvent> _events;  // 오래된 것이 앞
    int _activeRoom = 0;
    int _inputRoom = 0;  // 입력 방향 기준으로 고정된 방(0 = 아직 없음). MoveInput 참고
    bool _hasCutDirection = false;  // 컷 이후 첫 입력 방향을 기록했는가(_inputRoom != _activeRoom인 동안만 의미 있음)
    float _cutDirection[2] = {0.0f, 0.0f};  // 컷 직후 첫 입력의 단위 방향(입력 공간)
};

}  // namespace gge
