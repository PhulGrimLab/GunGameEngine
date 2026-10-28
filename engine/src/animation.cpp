#include "animation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace gge {

using namespace DirectX;

int Clip::MaxBone() const
{
    int maxBone = -1;
    for (const ClipChannel& c : channels)
        maxBone = std::max(maxBone, c.bone);
    return maxBone;
}

namespace {

bool Finite(const float* v, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (!std::isfinite(v[i]))
            return false;
    return true;
}

// 두 키 사이 보간 구간을 찾는다. 반환: 왼쪽 키 인덱스와 [0,1] 비율.
void FindSegment(const std::vector<float>& times, float t, size_t& left, float& ratio)
{
    if (t <= times.front()) { left = 0; ratio = 0; return; }
    if (t >= times.back()) { left = times.size() - 1; ratio = 0; return; }
    auto it = std::upper_bound(times.begin(), times.end(), t);
    left = static_cast<size_t>(it - times.begin()) - 1;
    ratio = (t - times[left]) / (times[left + 1] - times[left]);
}

XMVECTOR SampleVec3(const ClipChannel& c, float t)
{
    size_t i; float r;
    FindSegment(c.times, t, i, r);
    XMVECTOR a = XMVectorSet(c.values[i * 3], c.values[i * 3 + 1], c.values[i * 3 + 2], 0);
    if (r == 0) return a;
    XMVECTOR b = XMVectorSet(c.values[i * 3 + 3], c.values[i * 3 + 4], c.values[i * 3 + 5], 0);
    return XMVectorLerp(a, b, r);
}

XMVECTOR SampleQuat(const ClipChannel& c, float t)
{
    size_t i; float r;
    FindSegment(c.times, t, i, r);
    XMVECTOR a = XMVectorSet(c.values[i * 4], c.values[i * 4 + 1], c.values[i * 4 + 2], c.values[i * 4 + 3]);
    if (r == 0) return a;
    XMVECTOR b = XMVectorSet(c.values[i * 4 + 4], c.values[i * 4 + 5], c.values[i * 4 + 6], c.values[i * 4 + 7]);
    return XMQuaternionSlerp(a, b, r);
}

struct Pose {
    std::vector<XMVECTOR> t, r, s;
};

Pose SamplePose(const Skeleton& skeleton, const Clip* clip, float time)
{
    const int n = skeleton.BoneCount();
    Pose pose;
    pose.t.resize(n); pose.r.resize(n); pose.s.resize(n);
    for (int i = 0; i < n; ++i) {
        const BoneTransform& rest = skeleton.rest[i];
        pose.t[i] = XMLoadFloat3(&rest.translation);
        pose.r[i] = XMLoadFloat4(&rest.rotation);
        pose.s[i] = XMLoadFloat3(&rest.scale);
    }
    if (!clip)
        return pose;

    float clamped = std::clamp(time, 0.0f, clip->duration);
    for (const ClipChannel& c : clip->channels) {
        if (c.bone >= n)
            throw std::runtime_error("clip targets bone " + std::to_string(c.bone) + " but skeleton has " +
                                     std::to_string(n) + " bones");
        switch (c.path) {
            case ChannelPath::Translation: pose.t[c.bone] = SampleVec3(c, clamped); break;
            case ChannelPath::Rotation: pose.r[c.bone] = SampleQuat(c, clamped); break;
            case ChannelPath::Scale: pose.s[c.bone] = SampleVec3(c, clamped); break;
        }
    }
    return pose;
}

}  // namespace

void ValidateSkeleton(Skeleton& skeleton)
{
    const int n = skeleton.BoneCount();
    if (n < 1 || n > kMaxBones)
        throw std::runtime_error("bone count must be in 1.." + std::to_string(kMaxBones));
    if (skeleton.inverseBind.size() != static_cast<size_t>(n) || skeleton.rest.size() != static_cast<size_t>(n))
        throw std::runtime_error("skeleton arrays have inconsistent sizes");

    for (int i = 0; i < n; ++i) {
        if (skeleton.parent[i] < -1 || skeleton.parent[i] >= i)
            throw std::runtime_error("bone " + std::to_string(i) + " parent must be -1 or an earlier bone");
        if (!Finite(&skeleton.inverseBind[i].m[0][0], 16))
            throw std::runtime_error("inverse bind matrix is not finite");
        BoneTransform& rest = skeleton.rest[i];
        if (!Finite(&rest.translation.x, 3) || !Finite(&rest.rotation.x, 4) || !Finite(&rest.scale.x, 3))
            throw std::runtime_error("rest transform is not finite");
        XMVECTOR q = XMLoadFloat4(&rest.rotation);
        if (XMVectorGetX(XMVector4LengthSq(q)) < 1e-12f)
            throw std::runtime_error("rest rotation quaternion is zero");
        XMStoreFloat4(&rest.rotation, XMQuaternionNormalize(q));
    }
}

void ValidateClip(Clip& clip)
{
    if (!std::isfinite(clip.duration) || clip.duration < 0)
        throw std::runtime_error("clip duration must be finite and non-negative");
    if (clip.channels.empty())
        throw std::runtime_error("clip has no channels");

    for (ClipChannel& c : clip.channels) {
        if (c.bone < 0)
            throw std::runtime_error("clip channel bone must be non-negative");
        const size_t width = c.path == ChannelPath::Rotation ? 4 : 3;
        if (c.times.empty() || c.values.size() != c.times.size() * width)
            throw std::runtime_error("clip channel keys and values do not match");
        if (!Finite(c.times.data(), c.times.size()) || !Finite(c.values.data(), c.values.size()))
            throw std::runtime_error("clip channel contains non-finite values");
        for (size_t i = 1; i < c.times.size(); ++i)
            if (c.times[i] <= c.times[i - 1])
                throw std::runtime_error("clip channel times must be strictly increasing");
        if (c.times.front() < 0 || c.times.back() > clip.duration)
            throw std::runtime_error("clip channel times must lie within [0, duration]");
        if (c.path == ChannelPath::Rotation) {
            for (size_t k = 0; k < c.times.size(); ++k) {
                XMVECTOR q = XMVectorSet(c.values[k * 4], c.values[k * 4 + 1], c.values[k * 4 + 2], c.values[k * 4 + 3]);
                if (XMVectorGetX(XMVector4LengthSq(q)) < 1e-12f)
                    throw std::runtime_error("clip rotation key is a zero quaternion");
                XMVECTOR n = XMQuaternionNormalize(q);
                c.values[k * 4] = XMVectorGetX(n); c.values[k * 4 + 1] = XMVectorGetY(n);
                c.values[k * 4 + 2] = XMVectorGetZ(n); c.values[k * 4 + 3] = XMVectorGetW(n);
            }
        }
    }
}

std::vector<XMFLOAT4X4> ComputeBoneGlobals(const Skeleton& skeleton, const Clip* a, float timeA, const Clip* b,
                                           float timeB, float blend)
{
    const int n = skeleton.BoneCount();
    Pose pa = SamplePose(skeleton, a, timeA);
    Pose pb;
    const bool useBlend = b != nullptr;
    if (useBlend)
        pb = SamplePose(skeleton, b, timeB);

    std::vector<XMFLOAT4X4> globals(n);
    std::vector<XMMATRIX> matrices(n);
    for (int i = 0; i < n; ++i) {
        XMVECTOR t = pa.t[i], r = pa.r[i], s = pa.s[i];
        if (useBlend) {
            t = XMVectorLerp(t, pb.t[i], blend);
            s = XMVectorLerp(s, pb.s[i], blend);
            r = XMQuaternionSlerp(r, pb.r[i], blend);
        }
        XMMATRIX local = XMMatrixScalingFromVector(s) * XMMatrixRotationQuaternion(XMQuaternionNormalize(r)) *
                         XMMatrixTranslationFromVector(t);
        matrices[i] = skeleton.parent[i] < 0 ? local : local * matrices[skeleton.parent[i]];
        XMStoreFloat4x4(&globals[i], matrices[i]);
    }
    return globals;
}

std::vector<XMFLOAT4X4> ComputeSkinMatrices(const Skeleton& skeleton, const std::vector<XMFLOAT4X4>& globals)
{
    std::vector<XMFLOAT4X4> skin(globals.size());
    for (size_t i = 0; i < globals.size(); ++i)
        XMStoreFloat4x4(&skin[i], XMLoadFloat4x4(&skeleton.inverseBind[i]) * XMLoadFloat4x4(&globals[i]));
    return skin;
}

}  // namespace gge
