#pragma once
#include <DirectXMath.h>

#include <vector>

namespace gge {

constexpr int kMaxBones = 64;

struct BoneTransform {
    DirectX::XMFLOAT3 translation{0, 0, 0};
    DirectX::XMFLOAT4 rotation{0, 0, 0, 1};  // 단위 쿼터니언 (x, y, z, w)
    DirectX::XMFLOAT3 scale{1, 1, 1};
};

// 본은 부모가 항상 자식보다 앞에 오도록 정렬된다(parent[i] < i, 루트는 -1).
struct Skeleton {
    std::vector<int> parent;
    std::vector<DirectX::XMFLOAT4X4> inverseBind;  // 행 벡터 규약(이동이 마지막 행). glTF 열 우선 배열과 메모리 배치가 같다
    std::vector<BoneTransform> rest;
    int BoneCount() const { return static_cast<int>(parent.size()); }
};

enum class ChannelPath { Translation = 0, Rotation = 1, Scale = 2 };

struct ClipChannel {
    int bone = 0;
    ChannelPath path = ChannelPath::Translation;
    std::vector<float> times;   // 엄격히 증가
    std::vector<float> values;  // 키당 3개(이동/스케일) 또는 4개(회전 xyzw)
};

struct Clip {
    float duration = 0;
    std::vector<ClipChannel> channels;
    int MaxBone() const;
};

// 입력 검증(실패 시 예외). 회전 쿼터니언은 정규화해 저장한다.
void ValidateSkeleton(Skeleton& skeleton);
void ValidateClip(Clip& clip);

// 클립이 없으면(nullptr) 레스트 포즈. 시간은 [0, duration]로 클램프한다.
// blend는 0이면 A, 1이면 B(둘 다 있을 때만 의미). 반환: 본별 모델 공간 전역 행렬.
std::vector<DirectX::XMFLOAT4X4> ComputeBoneGlobals(const Skeleton& skeleton, const Clip* a, float timeA,
                                                    const Clip* b, float timeB, float blend);

// 셰이더용 스키닝 행렬: inverseBind * global
std::vector<DirectX::XMFLOAT4X4> ComputeSkinMatrices(const Skeleton& skeleton,
                                                     const std::vector<DirectX::XMFLOAT4X4>& globals);

}  // namespace gge
