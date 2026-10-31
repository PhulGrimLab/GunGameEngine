#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "animation.h"
#include "font.h"

namespace gge {

constexpr int kVertexFloats = 8;  // position(3) + normal(3) + uv(2)
constexpr int kMaxPointLights = 8;
constexpr int kMaxUiItems = 64;   // 화면 위 글자/사각형 항목 수 상한

// 화면 위 항목(HUD/대화창). 3D가 모두 그려지고 GPU 후처리(PS1 효과)가 끝난 뒤 렌더러의 읽기용 CPU 픽셀에 덮어 그려진다(그래서 PS1 효과를 받지 않고
// 장치와 무관하게 바이트 단위로 같다). 그리는 순서는 만든 순서(ID 오름차순)다. 종류를 enum으로 열어 두어 나중에 텍스처 쿼드(아이콘) 등을 더할 수 있다.
enum class UiKind { Text, Rect };

struct UiItem {
    UiKind kind = UiKind::Text;
    int x = 0, y = 0;            // 렌더 타깃의 정수 픽셀 좌표(왼쪽 위). 화면 밖/음수도 되며 잘려서 그려진다
    uint32_t rgb = 0xFFFFFF;     // 0xRRGGBB
    bool visible = true;
    // Text
    std::shared_ptr<const Font> font;   // 항목이 폰트를 공유하므로 핸들을 destroy해도 항목이 지워질 때까지 폰트가 산다
    std::string text;
    int maxWidth = 0;                   // 0 이하 = 줄바꿈은 '\n'뿐
    // Rect
    int width = 0, height = 0, alpha = 255;
};

struct RetroSettings {
    bool vertexSnap = false;  // 클립 공간 정점을 렌더 타깃 픽셀 격자에 스냅
    int colorBits = 8;        // 채널당 비트 수. 8이면 양자화 없음
    bool dither = false;      // 4x4 오더드 디더 (colorBits < 8일 때만 의미 있음)
    bool affineTexture = false;  // 원근 보정 없이 화면 공간에서 UV 보간 (PS1 텍스처 왜곡)
};

// 조명 규약: 최종 밝기 = 앰비언트 + 방향광색 * max(0, N.-L) + 점광원 합. 점광원 i는 색 * max(0, N.l) * a^2,
// a = saturate(1 - 거리/범위)이다. 기본값(앰비언트 0.25, 방향광색 0.75)은 조명 API가 생기기 전의 고정 조명 0.25 + 0.75 * N.-L과 같다.
struct LightingSettings {
    DirectX::XMFLOAT3 ambient{0.25f, 0.25f, 0.25f};
    DirectX::XMFLOAT3 direction{-0.4f, -1.0f, -0.6f};  // 빛이 나아가는 방향(정규화해서 보관)
    DirectX::XMFLOAT3 color{0.75f, 0.75f, 0.75f};
};

struct PointLight {
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT3 color{1, 1, 1};
    float range = 1;
};

// 평면 투영 그림자: 방향광을 y = planeY 평면에 투영한 모양을 opacity만큼 어둡게 칠한다(겹쳐도 한 번만).
struct ShadowSettings {
    bool enabled = false;
    float planeY = 0;
    float opacity = 0.5f;
};

struct Object {
    int mesh = 0;     // 0이면 그리지 않는 그룹 노드
    int texture = 0;  // 0이면 흰색 텍스처
    int parent = 0;   // 0이면 루트
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT3 rotation{0, 0, 0};  // (pitch X, yaw Y, roll Z) 라디안
    DirectX::XMFLOAT3 scale{1, 1, 1};
    DirectX::XMFLOAT3 color{0.8f, 0.8f, 0.8f};
    float alpha = 1;            // 1 = 불투명, 0 = 그리지 않음, 그 사이 = 알파 블렌딩(뒤에서 앞으로 정렬해 그린다)
    bool castsShadow = false;   // ShadowSettings가 켜져 있을 때 바닥 평면에 그림자를 드리운다
    bool visible = true;        // false면 이 오브젝트와 모든 자손을 그리지 않는다(그림자 포함). 알파/그림자 설정과는 별개 상태다.

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
    // 같은 방식의 "자기와 모든 조상이 보이는가" 캐시(Renderer::EffectivelyVisible 전용). 부모 체인을 오브젝트마다 거슬러 오르면 깊은 계층에서 O(깊이^2)다.
    mutable uint64_t visibleStamp = 0;
    mutable bool effectivelyVisible = true;
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

    // 리소스 해제. 알 수 없는 ID이거나 어떤 오브젝트가 아직 쓰고 있으면 예외(쓰는 오브젝트를 먼저 destroy/교체해야 한다).
    // ID는 해제 후에도 재사용되지 않는다. GPU 메모리는 해제 즉시 놓는다(오브젝트가 참조하지 않으므로 그릴 일이 없다).
    void DestroyMesh(int id);
    void DestroyTexture(int id);
    void DestroySkeleton(int id);
    void DestroyClip(int id);

    // 조명. 색 성분은 유한하고 0 이상이어야 한다(1을 넘는 과노출 허용). 잘못된 값은 예외.
    void SetAmbient(DirectX::XMFLOAT3 color);
    void SetDirectionalLight(DirectX::XMFLOAT3 direction, DirectX::XMFLOAT3 color);  // direction은 0이 아닌 유한 벡터
    // 점광원은 최대 kMaxPointLights개. 범위는 0보다 커야 한다. ID는 1부터, 해제 후 재사용하지 않는다.
    int CreatePointLight(DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 color, float range);
    void SetPointLight(int id, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 color, float range);
    void DestroyPointLight(int id);
    // planeY 유한, opacity 0..1. 그림자는 방향광이 아래를 향할 때(y 성분 < 0)만 그려진다.
    void SetShadow(bool enabled, float planeY, float opacity);

    int CreateObject(int mesh, int texture, int parent);
    void DestroyObject(int id);  // 자손까지 함께 제거
    int ObjectCount() const { return static_cast<int>(objects_.size()); }  // 살아 있는 오브젝트 수(진단/누수 검사용)
    void SetObjectParent(int id, int parent);
    void SetObjectMesh(int id, int mesh);
    void SetObjectTexture(int id, int texture);
    void SetObjectTransform(int id, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 rotation, DirectX::XMFLOAT3 scale);
    void SetObjectColor(int id, DirectX::XMFLOAT3 color);
    void SetObjectAlpha(int id, float alpha);  // 유한한 0..1
    void SetObjectCastsShadow(int id, bool casts);
    // 표시 여부(처음엔 true). false면 이 오브젝트와 모든 자손이 그려지지 않고 그림자도 드리우지 않으며 반투명 정렬에서도 빠진다. 알파와 별개라
    // 다시 켜면 이전 알파/그림자 설정 그대로 돌아온다. 점광원은 오브젝트가 아니라서 영향받지 않는다.
    void SetObjectVisible(int id, bool visible);
    bool IsObjectVisible(int id);  // 이 오브젝트 자신의 플래그(부모가 숨겨졌는지는 보지 않는다)

    // 스키닝된 메시의 최대 조인트 인덱스가 스켈레톤 본 수보다 작아야 한다. skeleton 0이면 해제.
    void SetObjectSkeleton(int id, int skeleton);
    // 시간은 호출자가 구동한다(엔진은 상태가 없다). clipA 0 = 레스트 포즈, clipB 0 = 블렌딩 없음.
    void SetObjectPose(int id, int clipA, float timeA, int clipB, float timeB, float blend);
    // 본별 모델 공간 전역 행렬(행 벡터 규약, 행 우선 16 float).
    std::vector<DirectX::XMFLOAT4X4> GetObjectPose(int id);

    // ---- 화면 위 항목(글자/사각형) ----
    // ID는 1부터, 해제 후 재사용하지 않고, 최대 kMaxUiItems개. 오류는 모두 사유가 담긴 예외이고 상태는 그대로다.
    // 글자: 폰트는 null이 아니어야 하고 text는 kMaxTextBytes 이하. 사각형: 크기 > 0, alpha 0..255(0 = 안 보임, 255 = 불투명).
    int CreateUiText(std::shared_ptr<const Font> font, std::string text, int x, int y, int maxWidth, uint32_t rgb);
    int CreateUiRect(int x, int y, int width, int height, uint32_t rgb, int alpha);
    void SetUiText(int id, std::string text);           // 글자 항목만
    void SetUiRect(int id, int width, int height, int alpha);   // 사각형 항목만(체력 바 길이 같은 용도)
    void SetUiPosition(int id, int x, int y);
    void SetUiColor(int id, uint32_t rgb);
    void SetUiVisible(int id, bool visible);
    void DestroyUi(int id);
    void ClearUi();
    int UiCount() const { return static_cast<int>(ui_.size()); }

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
    // 부모 체인을 곱한 월드 행렬. Render 중 오브젝트마다 한 번만 계산하도록 캐시한다(깊은 계층에서 O(깊이^2) -> O(깊이)).
    DirectX::XMMATRIX CachedWorldMatrix(const Object& o) const;
    bool EffectivelyVisible(const Object& o) const;  // 자기와 모든 조상의 visible이 true인가
    ComPtr<ID3D11ShaderResourceView> MakeTexture(const uint8_t* rgba, int width, int height);

    // Render 보조: 오브젝트 하나를 그린다. shadowPass면 그림자 투영 셰이더 경로를 쓴다.
    void DrawObject(const Object& object, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, bool shadowPass);

    int width_;
    int height_;
    LightingSettings lighting_;
    ShadowSettings shadow_;
    std::map<int, PointLight> pointLights_;
    int nextPointLight_ = 1;
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
    std::map<int, UiItem> ui_;
    int nextUi_ = 1;
    UiItem& GetUi(int id);
    void DrawUi();   // Render 끝에서 읽기용 픽셀 위에 항목을 만든 순서대로 덮어 그린다

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
    ComPtr<ID3D11RasterizerState> shadowRasterizer_;  // 평면으로 눌린 도형은 감김 방향이 제각각이라 컬링하지 않는다
    ComPtr<ID3D11DepthStencilState> depthState_;
    ComPtr<ID3D11DepthStencilState> blendDepthState_;   // 깊이 검사만, 쓰기 없음(반투명)
    ComPtr<ID3D11DepthStencilState> shadowDepthState_;  // 깊이 검사 + 스텐실로 픽셀당 한 번만 칠한다
    ComPtr<ID3D11BlendState> alphaBlend_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11ShaderResourceView> whiteTexture_;
};

}  // namespace gge
