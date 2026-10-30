#pragma once
#include <vector>

namespace gge {

// 방에 배정된 고정 카메라. 값은 gge_renderer_set_camera에 그대로 넘길 수 있다.
struct RoomCamera {
    float eye[3];
    float target[3];
    float fovY;
};

// 게임플레이 월드. 렌더러와 독립이라 D3D 없이 검증할 수 있다.
// 지금은 고정 카메라 방만 가지며, 이후 충돌과 트리거가 같은 월드에 더해진다.
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

    // 방 카메라. 알 수 없는 ID면 예외를 던진다.
    const RoomCamera& GetRoomCamera(int roomId) const;

private:
    struct Room {
        std::vector<float> polygon;  // x0,z0,x1,z1,...
        RoomCamera camera;
    };

    const Room& RoomById(int roomId) const;

    std::vector<Room> _rooms;  // 인덱스 + 1 == 방 ID
    int _activeRoom = 0;
};

}  // namespace gge
