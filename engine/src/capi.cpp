#include <gge/gge.h>

#include "error.h"
#include "obj_loader.h"
#include "renderer.h"

struct GgeRenderer {
    gge::Renderer impl;
    GgeRenderer(int w, int h, bool warp) : impl(w, h, warp) {}
};

namespace {

template <class F>
int Guard(F&& f)
{
    try {
        f();
        return 1;
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return 0;
    }
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

GGE_API int gge_renderer_set_mesh(GgeRenderer* r, const float* vertices, int vertex_count,
                                  const uint32_t* indices, int index_count)
{
    return Guard([&] { r->impl.SetMesh(vertices, vertex_count, indices, index_count); });
}

GGE_API int gge_renderer_load_obj(GgeRenderer* r, const char* path_utf8)
{
    return Guard([&] {
        gge::MeshData mesh = gge::LoadObjFile(path_utf8 ? path_utf8 : "");
        r->impl.SetMesh(mesh.vertices.data(), static_cast<int>(mesh.vertices.size() / 6), mesh.indices.data(),
                        static_cast<int>(mesh.indices.size()));
    });
}

GGE_API void gge_renderer_set_camera(GgeRenderer* r, float ex, float ey, float ez, float tx, float ty,
                                     float tz, float fov_y_radians)
{
    r->impl.SetCamera({ex, ey, ez}, {tx, ty, tz}, fov_y_radians);
}

GGE_API void gge_renderer_set_color(GgeRenderer* r, float red, float green, float blue)
{
    r->impl.SetColor(red, green, blue);
}

GGE_API int gge_renderer_render(GgeRenderer* r)
{
    return Guard([&] { r->impl.Render(); });
}

GGE_API const uint8_t* gge_renderer_get_pixels(GgeRenderer* r, int* width, int* height, int* stride)
{
    if (width) *width = r->impl.Width();
    if (height) *height = r->impl.Height();
    if (stride) *stride = r->impl.Stride();
    return r->impl.Pixels();
}

GGE_API const char* gge_last_error(void) { return gge::LastError().c_str(); }
