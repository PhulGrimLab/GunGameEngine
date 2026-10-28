#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace gge {

class Renderer {
public:
    Renderer(int width, int height, bool useWarp);

    void SetMesh(const float* vertices, int vertexCount, const uint32_t* indices, int indexCount);
    void SetCamera(DirectX::XMFLOAT3 eye, DirectX::XMFLOAT3 target, float fovY);
    void SetColor(float r, float g, float b) { color_ = {r, g, b}; }
    void Render();

    const uint8_t* Pixels() const { return pixels_.data(); }
    int Width() const { return width_; }
    int Height() const { return height_; }
    int Stride() const { return width_ * 4; }

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    int width_;
    int height_;
    DirectX::XMFLOAT3 eye_{0, 0, 5};
    DirectX::XMFLOAT3 target_{0, 0, 0};
    float fovY_ = 1.0f;
    DirectX::XMFLOAT3 color_{0.8f, 0.8f, 0.8f};
    UINT indexCount_ = 0;
    std::vector<uint8_t> pixels_;

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
    ComPtr<ID3D11Buffer> vertexBuffer_;
    ComPtr<ID3D11Buffer> indexBuffer_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11DepthStencilState> depthState_;
};

}  // namespace gge
