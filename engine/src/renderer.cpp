#include "renderer.h"

#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "error.h"

namespace gge {

using namespace DirectX;

namespace {

const char kShader[] = R"(
cbuffer Frame : register(b0) {
    float4x4 viewProj;
    float4 lightDir;
    float4 color;
    float4 retro;   // x: vertex snap, y: color levels (0 = off), z: dither
    float4 screen;  // xy: render target size in pixels
};
struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; };
VSOut vs(VSIn i) {
    VSOut o;
    float4 p = mul(viewProj, float4(i.pos, 1));
    if (retro.x > 0.5 && p.w > 0) {
        float2 grid = screen.xy * 0.5;
        p.xy = round(p.xy / p.w * grid) / grid * p.w;
    }
    o.pos = p;
    o.nrm = i.nrm;
    return o;
}
float4 ps(VSOut i) : SV_Target {
    float d = saturate(dot(normalize(i.nrm), -lightDir.xyz));
    float3 c = color.rgb * (0.25 + 0.75 * d);
    if (retro.y > 0) {
        static const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        uint2 px = (uint2)i.pos.xy & 3;
        float t = (bayer[px.y * 4 + px.x] + 0.5) / 16.0 - 0.5;
        c = saturate(floor(c * retro.y + t * retro.z + 0.5) / retro.y);
    }
    return float4(c, 1);
}
)";

struct Constants {
    XMFLOAT4X4 viewProj;
    XMFLOAT4 lightDir;
    XMFLOAT4 color;
    XMFLOAT4 retro;
    XMFLOAT4 screen;
};

void Check(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        char buf[16];
        snprintf(buf, sizeof buf, "%08X", static_cast<unsigned>(hr));
        throw std::runtime_error(std::string(what) + " failed, hr=0x" + buf);
    }
}

Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char* entry, const char* target)
{
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompile(kShader, sizeof kShader - 1, nullptr, nullptr, nullptr, entry, target, 0, 0,
                            &code, &errors);
    if (FAILED(hr))
        throw std::runtime_error(std::string("shader compile failed: ") +
                                 (errors ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    return code;
}

}  // namespace

Renderer::Renderer(int width, int height, bool useWarp) : width_(width), height_(height)
{
    if (width <= 0 || height <= 0)
        throw std::runtime_error("invalid render size");

    Check(D3D11CreateDevice(nullptr, useWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                            nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_),
          "D3D11CreateDevice");

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Check(device_->CreateTexture2D(&desc, nullptr, &colorTarget_), "CreateTexture2D(color)");
    Check(device_->CreateRenderTargetView(colorTarget_.Get(), nullptr, &rtv_), "CreateRenderTargetView");

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device_->CreateTexture2D(&desc, nullptr, &staging_), "CreateTexture2D(staging)");

    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = 0;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;
    Check(device_->CreateTexture2D(&desc, nullptr, &depth), "CreateTexture2D(depth)");
    Check(device_->CreateDepthStencilView(depth.Get(), nullptr, &dsv_), "CreateDepthStencilView");

    auto vsCode = Compile("vs", "vs_5_0");
    auto psCode = Compile("ps", "ps_5_0");
    Check(device_->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs_),
          "CreateVertexShader");
    Check(device_->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps_),
          "CreatePixelShader");

    D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    Check(device_->CreateInputLayout(elements, 2, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &layout_),
          "CreateInputLayout");

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Check(device_->CreateBuffer(&cb, nullptr, &constants_), "CreateBuffer(constants)");

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_BACK;
    rs.FrontCounterClockwise = TRUE;
    rs.DepthClipEnable = TRUE;
    Check(device_->CreateRasterizerState(&rs, &rasterizer_), "CreateRasterizerState");

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    Check(device_->CreateDepthStencilState(&ds, &depthState_), "CreateDepthStencilState");

    pixels_.assign(static_cast<size_t>(width) * height * 4, 0);
}

void Renderer::SetMesh(const float* vertices, int vertexCount, const uint32_t* indices, int indexCount)
{
    if (!vertices || !indices || vertexCount <= 0 || indexCount <= 0 || indexCount % 3 != 0)
        throw std::runtime_error("invalid mesh arguments");
    for (int i = 0; i < indexCount; ++i)
        if (indices[i] >= static_cast<uint32_t>(vertexCount))
            throw std::runtime_error("mesh index out of range");

    D3D11_BUFFER_DESC bd{};
    D3D11_SUBRESOURCE_DATA init{};

    bd.ByteWidth = static_cast<UINT>(vertexCount) * 6 * sizeof(float);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    init.pSysMem = vertices;
    ComPtr<ID3D11Buffer> vb;
    Check(device_->CreateBuffer(&bd, &init, &vb), "CreateBuffer(vertex)");

    bd.ByteWidth = static_cast<UINT>(indexCount) * sizeof(uint32_t);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    init.pSysMem = indices;
    ComPtr<ID3D11Buffer> ib;
    Check(device_->CreateBuffer(&bd, &init, &ib), "CreateBuffer(index)");

    vertexBuffer_ = vb;
    indexBuffer_ = ib;
    indexCount_ = static_cast<UINT>(indexCount);
}

void Renderer::SetRetro(const RetroSettings& settings)
{
    if (settings.colorBits < 1 || settings.colorBits > 8)
        throw std::runtime_error("color_bits must be in 1..8");
    retro_ = settings;
}

void Renderer::SetCamera(XMFLOAT3 eye, XMFLOAT3 target, float fovY)
{
    eye_ = eye;
    target_ = target;
    fovY_ = fovY;
}

void Renderer::Render()
{
    float clear[4] = {0.05f, 0.05f, 0.08f, 1.0f};
    if (retro_.colorBits < 8) {  // 배경도 셰이더 출력과 같은 색 깊이로 맞춘다
        const float levels = static_cast<float>((1 << retro_.colorBits) - 1);
        for (int i = 0; i < 3; ++i)
            clear[i] = std::floor(clear[i] * levels + 0.5f) / levels;
    }
    context_->ClearRenderTargetView(rtv_.Get(), clear);
    context_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    if (indexCount_ > 0) {
        XMMATRIX view = XMMatrixLookAtRH(XMLoadFloat3(&eye_), XMLoadFloat3(&target_), XMVectorSet(0, 1, 0, 0));
        XMMATRIX proj = XMMatrixPerspectiveFovRH(fovY_, static_cast<float>(width_) / height_, 0.1f, 100.0f);

        Constants c;
        // HLSL은 cbuffer 행렬을 열 우선으로 읽으므로, 행 벡터용 XM 행렬을 그대로 올리면 mul(M, v)가 올바른 전치가 된다.
        XMStoreFloat4x4(&c.viewProj, view * proj);
        XMStoreFloat4(&c.lightDir, XMVector3Normalize(XMVectorSet(-0.4f, -1.0f, -0.6f, 0)));
        c.color = {color_.x, color_.y, color_.z, 1};
        c.retro = {retro_.vertexSnap ? 1.0f : 0.0f,
                   retro_.colorBits >= 8 ? 0.0f : static_cast<float>((1 << retro_.colorBits) - 1),
                   retro_.dither ? 1.0f : 0.0f, 0};
        c.screen = {static_cast<float>(width_), static_cast<float>(height_), 0, 0};
        context_->UpdateSubresource(constants_.Get(), 0, nullptr, &c, 0, 0);

        D3D11_VIEWPORT vp{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
        UINT stride = 6 * sizeof(float), offset = 0;
        ID3D11RenderTargetView* rtv = rtv_.Get();
        context_->OMSetRenderTargets(1, &rtv, dsv_.Get());
        context_->OMSetDepthStencilState(depthState_.Get(), 0);
        context_->RSSetState(rasterizer_.Get());
        context_->RSSetViewports(1, &vp);
        context_->IASetInputLayout(layout_.Get());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
        context_->IASetIndexBuffer(indexBuffer_.Get(), DXGI_FORMAT_R32_UINT, 0);
        context_->VSSetShader(vs_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        context_->PSSetShader(ps_.Get(), nullptr, 0);
        context_->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        context_->DrawIndexed(indexCount_, 0, 0);
    }

    context_->CopyResource(staging_.Get(), colorTarget_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map");
    for (int y = 0; y < height_; ++y)
        std::memcpy(&pixels_[static_cast<size_t>(y) * Stride()],
                    static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                    Stride());
    context_->Unmap(staging_.Get(), 0);
}

}  // namespace gge
