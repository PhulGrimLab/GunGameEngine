#include "renderer.h"

#include <d3dcompiler.h>

#include <algorithm>
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
    float4x4 world;
    float4x4 normalMatrix;
    float4 lightDir;
    float4 color;
    float4 retro;   // x: vertex snap, y: color levels (0 = off), z: dither, w: affine texture
    float4 screen;  // xy: render target size in pixels
    float4 skin;    // x: skinning enabled
};
cbuffer Bones : register(b1) {
    float4x4 bones[64];
};
Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct VSIn {
    float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD;
    uint4 joints : BLENDINDICES; float4 weights : BLENDWEIGHT;
};
struct VSOut {
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
    float2 uvPerspective : TEXCOORD0;
    noperspective float2 uvAffine : TEXCOORD1;
};
VSOut vs(VSIn i) {
    VSOut o;
    float3 position = i.pos;
    float3 normal = i.nrm;
    if (skin.x > 0.5) {
        float4x4 m = i.weights.x * bones[i.joints.x] + i.weights.y * bones[i.joints.y] +
                     i.weights.z * bones[i.joints.z] + i.weights.w * bones[i.joints.w];
        position = mul(m, float4(i.pos, 1)).xyz;
        normal = mul(m, float4(i.nrm, 0)).xyz;
    }
    float4 p = mul(viewProj, mul(world, float4(position, 1)));
    if (retro.x > 0.5 && p.w > 0) {
        float2 grid = screen.xy * 0.5;
        p.xy = round(p.xy / p.w * grid) / grid * p.w;
    }
    o.pos = p;
    o.nrm = mul(normalMatrix, float4(normal, 0)).xyz;
    o.uvPerspective = i.uv;
    o.uvAffine = i.uv;
    return o;
}
float4 ps(VSOut i) : SV_Target {
    float d = saturate(dot(normalize(i.nrm), -lightDir.xyz));
    float2 uv = retro.w > 0.5 ? i.uvAffine : i.uvPerspective;
    float3 c = color.rgb * tex.Sample(samp, uv).rgb * (0.25 + 0.75 * d);
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
    XMFLOAT4X4 world;
    XMFLOAT4X4 normalMatrix;
    XMFLOAT4 lightDir;
    XMFLOAT4 color;
    XMFLOAT4 retro;
    XMFLOAT4 screen;
    XMFLOAT4 skin;
};

// GPU 정점: pos(3) nrm(3) uv(2) joints(4, uint) weights(4). 정적 메시도 같은 레이아웃(joint 0, weight 1)을 쓴다.
struct GpuVertex {
    float position[3];
    float normal[3];
    float uv[2];
    uint32_t joints[4];
    float weights[4];
};
static_assert(sizeof(GpuVertex) == 64);

constexpr int kMaxTextureSize = 4096;

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
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    Check(device_->CreateInputLayout(elements, 5, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &layout_),
          "CreateInputLayout");

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Check(device_->CreateBuffer(&cb, nullptr, &constants_), "CreateBuffer(constants)");

    cb.ByteWidth = kMaxBones * sizeof(XMFLOAT4X4);
    Check(device_->CreateBuffer(&cb, nullptr, &bones_), "CreateBuffer(bones)");

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

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;  // PS1은 텍스처 필터링이 없다
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    Check(device_->CreateSamplerState(&sd, &sampler_), "CreateSamplerState");

    const uint8_t white[4] = {255, 255, 255, 255};
    whiteTexture_ = MakeTexture(white, 1, 1);

    pixels_.assign(static_cast<size_t>(width) * height * 4, 0);
}

Renderer::ComPtr<ID3D11ShaderResourceView> Renderer::MakeTexture(const uint8_t* rgba, int width, int height)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rgba, static_cast<UINT>(width) * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    Check(device_->CreateTexture2D(&desc, &init, &texture), "CreateTexture2D(texture)");
    ComPtr<ID3D11ShaderResourceView> srv;
    Check(device_->CreateShaderResourceView(texture.Get(), nullptr, &srv), "CreateShaderResourceView");
    return srv;
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

int Renderer::CreateMesh(const float* vertices, int vertexCount, const uint32_t* indices, int indexCount,
                         const uint32_t* joints, const float* weights)
{
    if (!vertices || !indices || vertexCount <= 0 || indexCount <= 0 || indexCount % 3 != 0)
        throw std::runtime_error("invalid mesh arguments");
    if ((joints == nullptr) != (weights == nullptr))
        throw std::runtime_error("joints and weights must be given together");
    for (int i = 0; i < indexCount; ++i)
        if (indices[i] >= static_cast<uint32_t>(vertexCount))
            throw std::runtime_error("mesh index out of range");

    GpuMesh mesh;
    mesh.skinned = joints != nullptr;
    std::vector<GpuVertex> gpuVertices(vertexCount);
    for (int v = 0; v < vertexCount; ++v) {
        GpuVertex& g = gpuVertices[v];
        std::memcpy(&g, vertices + static_cast<size_t>(v) * kVertexFloats, kVertexFloats * sizeof(float));
        g.joints[0] = g.joints[1] = g.joints[2] = g.joints[3] = 0;
        g.weights[0] = 1; g.weights[1] = g.weights[2] = g.weights[3] = 0;
        if (!mesh.skinned)
            continue;

        float sum = 0;
        for (int k = 0; k < 4; ++k) {
            float w = weights[v * 4 + k];
            if (!std::isfinite(w) || w < 0)
                throw std::runtime_error("skin weight must be finite and non-negative");
            if (w > 0 && joints[v * 4 + k] >= static_cast<uint32_t>(kMaxBones))
                throw std::runtime_error("joint index exceeds the maximum of " + std::to_string(kMaxBones) + " bones");
            sum += w;
        }
        if (sum <= 0)
            continue;  // 영향이 없는 정점은 본 0에 고정
        for (int k = 0; k < 4; ++k) {
            float w = weights[v * 4 + k] / sum;
            g.weights[k] = w;
            g.joints[k] = w > 0 ? joints[v * 4 + k] : 0;
            if (w > 0)
                mesh.maxJoint = std::max(mesh.maxJoint, g.joints[k]);
        }
    }

    D3D11_BUFFER_DESC bd{};
    D3D11_SUBRESOURCE_DATA init{};
    bd.ByteWidth = static_cast<UINT>(vertexCount) * sizeof(GpuVertex);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    init.pSysMem = gpuVertices.data();
    Check(device_->CreateBuffer(&bd, &init, &mesh.vertices), "CreateBuffer(vertex)");

    bd.ByteWidth = static_cast<UINT>(indexCount) * sizeof(uint32_t);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    init.pSysMem = indices;
    Check(device_->CreateBuffer(&bd, &init, &mesh.indices), "CreateBuffer(index)");
    mesh.indexCount = static_cast<UINT>(indexCount);

    int id = nextMesh_++;
    meshes_[id] = std::move(mesh);
    return id;
}

int Renderer::CreateSkeleton(Skeleton skeleton)
{
    ValidateSkeleton(skeleton);
    int id = nextSkeleton_++;
    skeletons_[id] = std::move(skeleton);
    return id;
}

int Renderer::CreateClip(Clip clip)
{
    ValidateClip(clip);
    int id = nextClip_++;
    clips_[id] = std::move(clip);
    return id;
}

int Renderer::CreateTexture(const uint8_t* rgba, int width, int height)
{
    if (!rgba || width <= 0 || height <= 0 || width > kMaxTextureSize || height > kMaxTextureSize)
        throw std::runtime_error("invalid texture arguments");
    int id = nextTexture_++;
    textures_[id] = MakeTexture(rgba, width, height);
    return id;
}

void Renderer::RequireMesh(int id) const
{
    if (id != 0 && !meshes_.count(id))
        throw std::runtime_error("unknown mesh id " + std::to_string(id));
}

void Renderer::RequireTexture(int id) const
{
    if (id != 0 && !textures_.count(id))
        throw std::runtime_error("unknown texture id " + std::to_string(id));
}

Object& Renderer::Get(int id)
{
    auto it = objects_.find(id);
    if (it == objects_.end())
        throw std::runtime_error("unknown object id " + std::to_string(id));
    return it->second;
}

int Renderer::CreateObject(int mesh, int texture, int parent)
{
    RequireMesh(mesh);
    RequireTexture(texture);
    if (parent != 0)
        Get(parent);
    Object o;
    o.mesh = mesh;
    o.texture = texture;
    o.parent = parent;
    int id = nextObject_++;
    objects_[id] = o;
    return id;
}

void Renderer::DestroyObject(int id)
{
    Get(id);
    std::vector<int> pending{id};
    while (!pending.empty()) {
        int current = pending.back();
        pending.pop_back();
        for (const auto& [childId, child] : objects_)
            if (child.parent == current)
                pending.push_back(childId);
        objects_.erase(current);
    }
}

void Renderer::SetObjectParent(int id, int parent)
{
    Object& o = Get(id);
    if (parent != 0) {
        for (int p = parent; p != 0; p = Get(p).parent)
            if (p == id)
                throw std::runtime_error("parent would create a cycle");
    }
    o.parent = parent;
}

void Renderer::SetObjectMesh(int id, int mesh)
{
    RequireMesh(mesh);
    Object& o = Get(id);
    if (o.skeleton != 0)
        RequireSkeletonFits(mesh, o.skeleton);
    o.mesh = mesh;
}

void Renderer::RequireSkeletonFits(int meshId, int skeletonId) const
{
    if (skeletonId == 0)
        return;
    auto skeleton = skeletons_.find(skeletonId);
    if (skeleton == skeletons_.end())
        throw std::runtime_error("unknown skeleton id " + std::to_string(skeletonId));
    if (meshId == 0)
        return;
    const GpuMesh& mesh = meshes_.at(meshId);
    if (!mesh.skinned)
        throw std::runtime_error("mesh is not skinned");
    if (mesh.maxJoint >= static_cast<uint32_t>(skeleton->second.BoneCount()))
        throw std::runtime_error("mesh uses bone " + std::to_string(mesh.maxJoint) + " but skeleton has " +
                                 std::to_string(skeleton->second.BoneCount()) + " bones");
}

void Renderer::SetObjectSkeleton(int id, int skeleton)
{
    Object& o = Get(id);
    RequireSkeletonFits(o.mesh, skeleton);
    if (skeleton != 0)
        for (int clip : {o.clipA, o.clipB})
            RequireClipFits(clip, skeleton);
    o.skeleton = skeleton;
}

void Renderer::RequireClipFits(int clipId, int skeletonId) const
{
    if (clipId == 0 || skeletonId == 0)
        return;
    const Clip& clip = clips_.at(clipId);
    int bones = skeletons_.at(skeletonId).BoneCount();
    if (clip.MaxBone() >= bones)
        throw std::runtime_error("clip targets bone " + std::to_string(clip.MaxBone()) + " but skeleton has " +
                                 std::to_string(bones) + " bones");
}

void Renderer::SetObjectPose(int id, int clipA, float timeA, int clipB, float timeB, float blend)
{
    Object& o = Get(id);
    for (int clip : {clipA, clipB})
        if (clip != 0 && !clips_.count(clip))
            throw std::runtime_error("unknown clip id " + std::to_string(clip));
    if (!std::isfinite(timeA) || !std::isfinite(timeB) || !std::isfinite(blend) || blend < 0 || blend > 1)
        throw std::runtime_error("pose time must be finite and blend must be in [0, 1]");
    RequireClipFits(clipA, o.skeleton);
    RequireClipFits(clipB, o.skeleton);
    o.clipA = clipA; o.timeA = timeA;
    o.clipB = clipB; o.timeB = timeB;
    o.blend = blend;
}

std::vector<XMFLOAT4X4> Renderer::Pose(const Object& o) const
{
    const Skeleton& skeleton = skeletons_.at(o.skeleton);
    const Clip* a = o.clipA ? &clips_.at(o.clipA) : nullptr;
    const Clip* b = o.clipB ? &clips_.at(o.clipB) : nullptr;
    return ComputeBoneGlobals(skeleton, a, o.timeA, b, o.timeB, o.blend);
}

std::vector<XMFLOAT4X4> Renderer::GetObjectPose(int id)
{
    const Object& o = Get(id);
    if (o.skeleton == 0)
        throw std::runtime_error("object has no skeleton");
    return Pose(o);
}

void Renderer::SetObjectTexture(int id, int texture)
{
    RequireTexture(texture);
    Get(id).texture = texture;
}

void Renderer::SetObjectTransform(int id, XMFLOAT3 position, XMFLOAT3 rotation, XMFLOAT3 scale)
{
    Object& o = Get(id);
    o.position = position;
    o.rotation = rotation;
    o.scale = scale;
}

void Renderer::SetObjectColor(int id, XMFLOAT3 color) { Get(id).color = color; }

XMMATRIX Renderer::LocalMatrix(const Object& o) const
{
    return XMMatrixScaling(o.scale.x, o.scale.y, o.scale.z) *
           XMMatrixRotationRollPitchYaw(o.rotation.x, o.rotation.y, o.rotation.z) *
           XMMatrixTranslation(o.position.x, o.position.y, o.position.z);
}

XMMATRIX Renderer::WorldMatrix(const Object& o) const
{
    XMMATRIX world = LocalMatrix(o);
    for (int p = o.parent; p != 0;) {
        const Object& parent = objects_.at(p);
        world = world * LocalMatrix(parent);
        p = parent.parent;
    }
    return world;
}

XMMATRIX Renderer::CachedWorldMatrix(const Object& o) const
{
    if (o.worldStamp == renderStamp_) {
        return XMLoadFloat4x4(&o.world);
    }
    XMMATRIX world = LocalMatrix(o);
    if (o.parent != 0) {
        world = world * CachedWorldMatrix(objects_.at(o.parent));  // 부모는 이번 렌더에서 한 번만 계산된다
    }
    XMStoreFloat4x4(&o.world, world);
    o.worldStamp = renderStamp_;
    return world;
}

void Renderer::Render()
{
    ++renderStamp_;  // 이전 렌더의 월드 행렬 캐시를 모두 무효화한다
    float clear[4] = {0.05f, 0.05f, 0.08f, 1.0f};
    if (retro_.colorBits < 8) {  // 배경도 셰이더 출력과 같은 색 깊이로 맞춘다
        const float levels = static_cast<float>((1 << retro_.colorBits) - 1);
        for (int i = 0; i < 3; ++i)
            clear[i] = std::floor(clear[i] * levels + 0.5f) / levels;
    }
    context_->ClearRenderTargetView(rtv_.Get(), clear);
    context_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    XMMATRIX view = XMMatrixLookAtRH(XMLoadFloat3(&eye_), XMLoadFloat3(&target_), XMVectorSet(0, 1, 0, 0));
    XMMATRIX proj = XMMatrixPerspectiveFovRH(fovY_, static_cast<float>(width_) / height_, 0.1f, 100.0f);

    D3D11_VIEWPORT vp{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    ID3D11RenderTargetView* rtv = rtv_.Get();
    context_->OMSetRenderTargets(1, &rtv, dsv_.Get());
    context_->OMSetDepthStencilState(depthState_.Get(), 0);
    context_->RSSetState(rasterizer_.Get());
    context_->RSSetViewports(1, &vp);
    context_->IASetInputLayout(layout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    ID3D11Buffer* vsBuffers[2] = {constants_.Get(), bones_.Get()};
    context_->VSSetConstantBuffers(0, 2, vsBuffers);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());

    for (const auto& [id, object] : objects_) {
        if (object.mesh == 0)
            continue;
        const GpuMesh& mesh = meshes_.at(object.mesh);
        XMMATRIX world = CachedWorldMatrix(object);

        Constants c;
        // HLSL은 cbuffer 행렬을 열 우선으로 읽으므로, 행 벡터용 XM 행렬을 그대로 올리면 mul(M, v)가 올바른 전치가 된다.
        XMStoreFloat4x4(&c.viewProj, view * proj);
        XMStoreFloat4x4(&c.world, world);
        XMStoreFloat4x4(&c.normalMatrix, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
        XMStoreFloat4(&c.lightDir, XMVector3Normalize(XMVectorSet(-0.4f, -1.0f, -0.6f, 0)));
        c.color = {object.color.x, object.color.y, object.color.z, 1};
        c.retro = {retro_.vertexSnap ? 1.0f : 0.0f,
                   retro_.colorBits >= 8 ? 0.0f : static_cast<float>((1 << retro_.colorBits) - 1),
                   retro_.dither ? 1.0f : 0.0f, retro_.affineTexture ? 1.0f : 0.0f};
        c.screen = {static_cast<float>(width_), static_cast<float>(height_), 0, 0};

        const bool skinning = object.skeleton != 0 && mesh.skinned;
        c.skin = {skinning ? 1.0f : 0.0f, 0, 0, 0};
        if (skinning) {
            const Skeleton& skeleton = skeletons_.at(object.skeleton);
            std::vector<XMFLOAT4X4> skin = ComputeSkinMatrices(skeleton, Pose(object));
            skin.resize(kMaxBones);
            context_->UpdateSubresource(bones_.Get(), 0, nullptr, skin.data(), 0, 0);
        }
        context_->UpdateSubresource(constants_.Get(), 0, nullptr, &c, 0, 0);

        ID3D11ShaderResourceView* srv =
            object.texture ? textures_.at(object.texture).Get() : whiteTexture_.Get();
        context_->PSSetShaderResources(0, 1, &srv);

        UINT stride = sizeof(GpuVertex), offset = 0;
        context_->IASetVertexBuffers(0, 1, mesh.vertices.GetAddressOf(), &stride, &offset);
        context_->IASetIndexBuffer(mesh.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
        context_->DrawIndexed(mesh.indexCount, 0, 0);
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
