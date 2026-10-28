#include <gge/gge.h>

#include "error.h"
#include "mesh_io.h"
#include "obj_loader.h"
#include "renderer.h"
#include "texture_loader.h"

struct GgeRenderer {
    gge::Renderer impl;
    GgeRenderer(int w, int h, bool warp) : impl(w, h, warp) {}
};

namespace {

// 성공하면 f의 반환값(ID는 >0, 단순 성공은 1)을, 예외가 나면 0을 반환한다.
template <class F>
int Guard(F&& f)
{
    try {
        return f();
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return 0;
    }
}

DirectX::XMFLOAT3 Vec3(const float* v)
{
    if (!v)
        throw std::runtime_error("null vector argument");
    return {v[0], v[1], v[2]};
}

}  // namespace

GGE_API GgeRenderer* gge_renderer_create(int width, int height, int use_warp)
{
    try {
        return new GgeRenderer(width, height, use_warp != 0);
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return nullptr;
    }
}

GGE_API void gge_renderer_destroy(GgeRenderer* r) { delete r; }

GGE_API int gge_mesh_create(GgeRenderer* r, const float* vertices, int vertex_count, const uint32_t* indices,
                            int index_count)
{
    return Guard([&] { return r->impl.CreateMesh(vertices, vertex_count, indices, index_count); });
}

GGE_API int gge_mesh_load_obj(GgeRenderer* r, const char* path_utf8)
{
    return Guard([&] {
        gge::MeshData mesh = gge::LoadObjFile(path_utf8 ? path_utf8 : "");
        return r->impl.CreateMesh(mesh.vertices.data(), static_cast<int>(mesh.vertices.size() / gge::kVertexFloats),
                                  mesh.indices.data(), static_cast<int>(mesh.indices.size()));
    });
}

GGE_API int gge_mesh_load_ggm(GgeRenderer* r, const char* path_utf8)
{
    return Guard([&] {
        gge::MeshData mesh = gge::ReadGgm(path_utf8 ? path_utf8 : "");
        return r->impl.CreateMesh(mesh.vertices.data(), static_cast<int>(mesh.vertices.size() / gge::kVertexFloats),
                                  mesh.indices.data(), static_cast<int>(mesh.indices.size()));
    });
}

GGE_API int gge_convert_obj_to_ggm(const char* obj_path_utf8, const char* ggm_path_utf8)
{
    return Guard([&] {
        gge::WriteGgm(ggm_path_utf8 ? ggm_path_utf8 : "", gge::LoadObjFile(obj_path_utf8 ? obj_path_utf8 : ""));
        return 1;
    });
}

GGE_API int gge_texture_create(GgeRenderer* r, const uint8_t* rgba, int width, int height)
{
    return Guard([&] { return r->impl.CreateTexture(rgba, width, height); });
}

GGE_API int gge_texture_load(GgeRenderer* r, const char* path_utf8)
{
    return Guard([&] {
        gge::ImageData image = gge::LoadImageFile(path_utf8 ? path_utf8 : "");
        return r->impl.CreateTexture(image.rgba.data(), image.width, image.height);
    });
}

GGE_API int gge_object_create(GgeRenderer* r, int mesh_id, int texture_id, int parent_id)
{
    return Guard([&] { return r->impl.CreateObject(mesh_id, texture_id, parent_id); });
}

GGE_API int gge_object_destroy(GgeRenderer* r, int object_id)
{
    return Guard([&] { r->impl.DestroyObject(object_id); return 1; });
}

GGE_API int gge_object_set_parent(GgeRenderer* r, int object_id, int parent_id)
{
    return Guard([&] { r->impl.SetObjectParent(object_id, parent_id); return 1; });
}

GGE_API int gge_object_set_mesh(GgeRenderer* r, int object_id, int mesh_id)
{
    return Guard([&] { r->impl.SetObjectMesh(object_id, mesh_id); return 1; });
}

GGE_API int gge_object_set_texture(GgeRenderer* r, int object_id, int texture_id)
{
    return Guard([&] { r->impl.SetObjectTexture(object_id, texture_id); return 1; });
}

GGE_API int gge_object_set_transform(GgeRenderer* r, int object_id, const float* position, const float* rotation,
                                     const float* scale)
{
    return Guard([&] {
        r->impl.SetObjectTransform(object_id, Vec3(position), Vec3(rotation), Vec3(scale));
        return 1;
    });
}

GGE_API int gge_object_set_color(GgeRenderer* r, int object_id, float red, float green, float blue)
{
    return Guard([&] { r->impl.SetObjectColor(object_id, {red, green, blue}); return 1; });
}

GGE_API void gge_renderer_set_camera(GgeRenderer* r, float ex, float ey, float ez, float tx, float ty,
                                     float tz, float fov_y_radians)
{
    r->impl.SetCamera({ex, ey, ez}, {tx, ty, tz}, fov_y_radians);
}

GGE_API int gge_renderer_set_retro(GgeRenderer* r, int vertex_snap, int color_bits, int dither, int affine_texture)
{
    return Guard([&] {
        r->impl.SetRetro({vertex_snap != 0, color_bits, dither != 0, affine_texture != 0});
        return 1;
    });
}

GGE_API int gge_renderer_render(GgeRenderer* r)
{
    return Guard([&] { r->impl.Render(); return 1; });
}

GGE_API const uint8_t* gge_renderer_get_pixels(GgeRenderer* r, int* width, int* height, int* stride)
{
    if (width) *width = r->impl.Width();
    if (height) *height = r->impl.Height();
    if (stride) *stride = r->impl.Stride();
    return r->impl.Pixels();
}

GGE_API const char* gge_last_error(void) { return gge::LastError().c_str(); }
