#include "collision.h"

#include <algorithm>
#include <cmath>

namespace gge {

namespace {

// 한 걸음의 최대 길이를 radius의 이 비율로 제한한다. 걸음이 radius보다 작으면 원이 얇은 상자를 건너뛸 수 없다.
constexpr float kMaxStepFraction = 0.5f;
// 한 걸음 안에서 모든 상자에서 밀어내기를 반복하는 최대 횟수(모서리/좁은 틈 수렴용).
constexpr int kMaxResolvePasses = 4;
// 밀어낸 뒤 표면에 정확히 닿은 상태가 다음 판정에서 다시 겹침으로 보이지 않도록 더하는 여유.
constexpr float kSkin = 1e-4f;

// 원(center, radius)이 box와 겹치면 밀어낸 중심을 center에 쓰고 true. 겹치지 않으면 false.
bool PushOut(const Box& box, Vec2& center, float radius)
{
    float nearestX = std::clamp(center.x, box.minX, box.maxX);
    float nearestZ = std::clamp(center.z, box.minZ, box.maxZ);
    float dx = center.x - nearestX, dz = center.z - nearestZ;
    float distSq = dx * dx + dz * dz;
    if (distSq >= radius * radius) {
        return false;
    }

    if (distSq > 0.0f) {
        // 중심이 상자 밖: 가장 가까운 점에서 멀어지는 방향으로 반지름까지 되돌린다.
        float dist = std::sqrt(distSq);
        float push = radius + kSkin - dist;
        center.x += dx / dist * push;
        center.z += dz / dist * push;
        return true;
    }

    // 중심이 상자 안(또는 경계 위): 네 면 중 가장 가까운 면 밖으로 내보낸다.
    float toMinX = center.x - box.minX, toMaxX = box.maxX - center.x;
    float toMinZ = center.z - box.minZ, toMaxZ = box.maxZ - center.z;
    float best = std::min(std::min(toMinX, toMaxX), std::min(toMinZ, toMaxZ));
    float exit = best + radius + kSkin;
    if (best == toMinX) {
        center.x -= exit;
    } else if (best == toMaxX) {
        center.x += exit;
    } else if (best == toMinZ) {
        center.z -= exit;
    } else {
        center.z += exit;
    }
    return true;
}

}  // namespace

Vec2 MoveCircle(const std::vector<Box>& boxes, Vec2 pos, Vec2 delta, float radius)
{
    float length = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    int steps = std::max(1, static_cast<int>(std::ceil(length / (radius * kMaxStepFraction))));
    Vec2 step = {delta.x / steps, delta.z / steps};

    for (int s = 0; s < steps; ++s) {
        pos.x += step.x;
        pos.z += step.z;
        for (int pass = 0; pass < kMaxResolvePasses; ++pass) {
            bool moved = false;
            for (const Box& box : boxes) {
                moved |= PushOut(box, pos, radius);
            }
            if (!moved) {
                break;  // 모든 상자와 겹치지 않음: 수렴
            }
            // moved == true: 밀어내기로 다른 상자와 새로 겹쳤을 수 있어 한 번 더 검사한다.
        }
    }
    return pos;
}

}  // namespace gge
