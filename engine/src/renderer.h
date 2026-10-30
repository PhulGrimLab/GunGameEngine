#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <vector>

#include "animation.h"

namespace gge {

constexpr int kVertexFloats = 8;  // position(3) + normal(3) + uv(2)

struct RetroSettings {
    bool vertexSnap = false;  // 클립 공간 정점을 렌더 타깃 픽셀 격자에 스냅
    int colorBits = 8;        // 채널당 비트 수. 8이면 양자화 없음
    bool dither = false;      // 4x4 오더드 디더 (colorBits < 8일 때만 의미 있음)
    bool affineTexture = false;  // 원근 보정 없이 화면 공간에서 UV 보간 (PS1 텍스처 왜곡)
};

struct Object {
    int mesh = 0;     // 0이면 그리지 않는 그룹 노드
    int texture = 0;  // 0이면 흰색 텍스처
    int parent = 0;   // 0이면 루트
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT3 rotation{0, 0, 0};  // (pitch X, yaw Y, roll Z) 라디안
    DirectX::XMFLOAT3 scale{1, 1, 1};
    DirectX::XMFLOAT3 color{0.8f, 0.8f, 0.8f};

    // 스켈레탈 애니메이션 (skeleton이 0이면 정점을 바인드 포즈 그대로 그린다)
    int skeleton = 0;
    int clipA = 0;  // 0이면 레스트 포즈
    float timeA = 0;
    int clipB = 0;  // 0이면 블렌딩 없음
    float timeB = 0;
    float blend = 0;  // 0이면 A, 1이면 B

    // Render 한 번 안에서만 유효한 월드 행렬 캐시(Renderer::CachedWorldMatrix 전용). worldStamp가 현재 렌더 번호와
    // 같을 때만 world를 믿는다. 오브젝트 변경 API는 이 값을 건드리지 않으며, 렌더 번호가 매번 올라가므로 낡은 값이 쓰이지 않는다.
    mutable DirectX::XMFLOAT4X4 world{};
    mutable uint64_t worldStamp = 0;
};

class Renderer {
public:
    Renderer(int width, int height, bool useWarp);

    // colorBits가 1..8 범위가 아니면 예외.
    void SetRetro(const RetroSettings& settings);
    void SetCamera(DirectX::XMFLOAT3 eye, DirectX::XMFLOAT3 target, float fovY);

    // 리소스와 오브젝트 ID는 1부터 시작하며 종류별로 독립이다.
    // joints/weights가 둘 다 null이면 정적 메시. 아니면 정점당 4개씩(joints는 본 인덱스, weights는 합이 1로 정규화됨).
    int CreateMesh(const float* vertices, int vertexCount, const uint32_t* indices, int indexCount,
                   const uint32_t* joints = nullptr, const float* weights = nullptr);
    int CreateTexture(const uint8_t* rgba, int width, int height);
    int CreateSkeleton(Skeleton skeleton);
    int CreateClip(Clip clip);

    int CreateObject(int mesh, int texture, int parent);
    void DestroyObject(int id);  // 자손까지 함께 제거
    void SetObjectParent(int id, int parent);
    void SetObjectMesh(int id, int mesh);
    void SetObjectTexture(int id, int texture);
    void SetObjectTransform(int id, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 rotation, DirectX::XMFLOAT3 scale);
    void SetObjectColor(int id, DirectX::XMFLOAT3 color);

    // 스키닝된 메시의 최대 조인트 인덱스가 스켈레톤 본 수보다 작아야 한다. skeleton 0이면 해제.
    void SetObjectSkeleton(int id, int skeleton);
    // 시간은 호출자가 구동한다(엔진은 상태가 없다). clipA 0 = 레스트 포즈, clipB 0 = 블렌딩 없음.
    void SetObjectPose(int id, int clipA, float timeA, int clipB, float timeB, float blend);
    // 본별 모델 공간 전역 행렬(행 벡터 규약, 행 우선 16 float).
    std::vector<DirectX::XMFLOAT4X4> GetObjectPose(int id);

    void Render();

    const uint8_t* Pixels() const { return pixels_.data(); }
    int Width() const { return width_; }
    int Height() const { return height_; }
    int Stride() const { return width_ * 4; }

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct GpuMesh {
        ComPtr<ID3D11Buffer> vertices;
        ComPtr<ID3D11Buffer> indices;
        UINT indexCount = 0;
        bool skinned = false;
        uint32_t maxJoint = 0;  // 스키닝 메시에서 실제로 영향을 주는 최대 본 인덱스
    };

    Object& Get(int id);
    void RequireMesh(int id) const;
    void RequireTexture(int id) const;
    void RequireSkeletonFits(int meshId, int skeletonId) const;
    void RequireClipFits(int clipId, int skeletonId) const;
    std::vector<DirectX::XMFLOAT4X4> Pose(const Object& o) const;
    DirectX::XMMATRIX LocalMatrix(const Object& o) const;
    DirectX::XMMATRIX WorldMatrix(const Object& o) const;
    // WorldMatrix와 같은 결과를 Render 중 한 번만 계산하도록 부모 체인을 캐시한다(깊은 계층에서 O(깊이^2) -> O(깊이)).
    DirectX::XMMATRIX CachedWorldMatrix(const Object& o) const;
    ComPtr<ID3D11ShaderResourceView> MakeTexture(const uint8_t* rgba, int width, int height);

    int width_;
    int height_;
    DirectX::XMFLOAT3 eye_{0, 0, 5};
    DirectX::XMFLOAT3 target_{0, 0, 0};
    float fovY_ = 1.0f;
    RetroSettings retro_;
    std::vector<uint8_t> pixels_;

    std::map<int, GpuMesh> meshes_;
    std::map<int, ComPtr<ID3D11ShaderResourceView>> textures_;
    std::map<int, Object> objects_;
    std::map<int, Skeleton> skeletons_;
    std::map<int, Clip> clips_;
    uint64_t renderStamp_ = 0;  // Render 호출마다 증가. Object::worldStamp와 비교한다.
    int nextMesh_ = 1, nextTexture_ = 1, nextObject_ = 1, nextSkeleton_ = 1, nextClip_ = 1;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> colorTarget_;
    ComPtr<ID3D11Texture2D> staging_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11Buffer> bones_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11DepthStencilState> depthState_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11ShaderResourceView> whiteTexture_;
};

}  // namespace gge
