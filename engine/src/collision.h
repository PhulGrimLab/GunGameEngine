#pragma once
#include <vector>

namespace gge {

// 바닥(XZ 평면) 위 축 정렬 사각형 장애물. 높이는 보지 않는다. min < max.
struct Box {
    float minX, minZ, maxX, maxZ;
};

struct Vec2 {
    float x, z;
};

// 반지름 radius인 원형 이동체를 pos에서 delta만큼 움직이고, 장애물에 막히면 벽을 따라 미끄러진 최종 위치를 반환한다.
//
// 처리 흐름: 이동량을 radius/2 이하의 작은 걸음으로 나눠(빠른 이동이 얇은 벽을 통과하는 것을 막는다) 걸음마다
// 이동한 뒤 모든 장애물에서 밀어낸다. 밀어내기는 "원 중심에서 가장 가까운 상자 점"의 반대 방향으로 겹친 만큼
// 되돌리는 것이라, 벽 쪽 성분은 사라지고 벽과 나란한 성분은 남아 슬라이딩이 된다. 모서리 같은 곳은 한 상자를
// 밀어내면 다른 상자와 겹칠 수 있어 걸음마다 몇 번 반복한다.
//
// 시작 위치가 이미 장애물과 겹쳐 있으면 첫 걸음에서 밖으로 밀려난다. 중심이 상자 안에 있으면 침투가 가장 작은
// 축 방향으로 나간다. 두 상자 사이 틈이 지름보다 좁아 모두 만족할 수 없으면 반복 횟수 한도 안에서 가장 나은 위치를
// 반환한다(통과하지는 못한다).
Vec2 MoveCircle(const std::vector<Box>& boxes, Vec2 pos, Vec2 delta, float radius);

}  // namespace gge
