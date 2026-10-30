#pragma once
#include <vector>

namespace gge {

// 바닥(XZ) 평면 위 단순 다각형 도구. 고정 카메라 방과 트리거 영역이 함께 쓴다.

// xz는 (x,z) 쌍의 꼭짓점 배열이다(시계/반시계 모두 허용). 꼭짓점이 3개 미만, 유한하지 않은 값, 길이 0인 변, 면적 0,
// 변끼리 교차/겹침(자기 교차)이면 사유와 함께 예외를 던진다.
void ValidatePolygon(const float* xz, int vertexCount);

// (x,z)가 다각형 안이거나 경계 위인가. 경계는 포함으로 봐서 맞닿은 영역 사이에 틈이 생기지 않게 한다. 오목 다각형도 된다.
bool PolygonContains(const std::vector<float>& polygon, double x, double z);

}  // namespace gge
