#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <vector>

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
};

class Renderer {
public:
    Renderer(int width, int height, bool useWarp);

    // colorBits가 1..8 범위가 아니면 예외.
    void SetRetro(const RetroSettings& settings);
    void SetCamera(DirectX::XMFLOAT3 eye, DirectX::XMFLOAT3 target, float fovY);

    // 리소스와 오브젝트 ID는 1부터 시작하며 종류별로 독립이다.
    int CreateMesh(const float* vertices, int vertexCount, const uint32_t* indices, int indexCount);
    int CreateTexture(const uint8_t* rgba, int width, int height);

    int CreateObject(int mesh, int texture, int parent);
    void DestroyObject(int id);  // 자손까지 함께 제거
    void SetObjectParent(int id, int parent);
    void SetObjectMesh(int id, int mesh);
    void SetObjectTexture(int id, int texture);
    void SetObjectTransform(int id, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 rotation, DirectX::XMFLOAT3 scale);
    void SetObjectColor(int id, DirectX::XMFLOAT3 color);

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
    };

    Object& Get(int id);
    void RequireMesh(int id) const;
    void RequireTexture(int id) const;
    DirectX::XMMATRIX LocalMatrix(const Object& o) const;
    DirectX::XMMATRIX WorldMatrix(const Object& o) const;
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
    int nextMesh_ = 1, nextTexture_ = 1, nextObject_ = 1;

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
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11DepthStencilState> depthState_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11ShaderResourceView> whiteTexture_;
};

}  // namespace gge
