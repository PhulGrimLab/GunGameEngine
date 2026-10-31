#include <gge/gge.h>

#include <cstring>
#include <string>

#include "error.h"
#include "mesh_io.h"
#include "obj_loader.h"
#include "renderer.h"
#include "scene.h"
#include "scene_io.h"
#include "texture_loader.h"
#include "world.h"

struct GgeRenderer {
    gge::Renderer impl;
    GgeRenderer(int w, int h, bool warp) : impl(w, h, warp) {}
};

struct GgeWorld {
    gge::World impl;
};

struct GgeScene {
    gge::Scene impl;
    std::string eventName;  // gge_scene_poll_event가 돌려준 이름의 저장소
    std::string clipName;   // gge_scene_get_node_animation이 돌려준 클립 이름의 저장소
    GgeScene(gge::Renderer& r, gge::SceneData data) : impl(r, std::move(data)) {}
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

GGE_API int gge_mesh_create_skinned(GgeRenderer* r, const float* vertices, int vertex_count,
                                    const uint32_t* joints, const float* weights, const uint32_t* indices,
                                    int index_count)
{
    return Guard([&] {
        if (!joints || !weights)
            throw std::runtime_error("skinned mesh requires joints and weights");
        return r->impl.CreateMesh(vertices, vertex_count, indices, index_count, joints, weights);
    });
}

GGE_API int gge_skeleton_create(GgeRenderer* r, int bone_count, const int* parents, const float* inverse_bind,
                                const float* rest_translation, const float* rest_rotation, const float* rest_scale)
{
    return Guard([&] {
        if (bone_count < 1 || bone_count > gge::kMaxBones)
            throw std::runtime_error("bone count must be in 1.." + std::to_string(gge::kMaxBones));
        if (!parents || !inverse_bind || !rest_translation || !rest_rotation || !rest_scale)
            throw std::runtime_error("null skeleton array");
        gge::Skeleton skeleton;
        skeleton.parent.assign(parents, parents + bone_count);
        skeleton.inverseBind.resize(bone_count);
        skeleton.rest.resize(bone_count);
        for (int i = 0; i < bone_count; ++i) {
            std::memcpy(&skeleton.inverseBind[i], inverse_bind + i * 16, 16 * sizeof(float));
            skeleton.rest[i].translation = {rest_translation[i * 3], rest_translation[i * 3 + 1], rest_translation[i * 3 + 2]};
            skeleton.rest[i].rotation = {rest_rotation[i * 4], rest_rotation[i * 4 + 1], rest_rotation[i * 4 + 2], rest_rotation[i * 4 + 3]};
            skeleton.rest[i].scale = {rest_scale[i * 3], rest_scale[i * 3 + 1], rest_scale[i * 3 + 2]};
        }
        return r->impl.CreateSkeleton(std::move(skeleton));
    });
}

GGE_API int gge_clip_create(GgeRenderer* r, float duration, int channel_count, const GgeClipChannel* channels)
{
    return Guard([&] {
        if (channel_count < 1 || !channels)
            throw std::runtime_error("clip needs at least one channel");
        gge::Clip clip;
        clip.duration = duration;
        for (int i = 0; i < channel_count; ++i) {
            const GgeClipChannel& in = channels[i];
            if (in.path < 0 || in.path > 2)
                throw std::runtime_error("clip channel path must be 0, 1 or 2");
            if (in.key_count < 1 || !in.times || !in.values)
                throw std::runtime_error("clip channel needs keys");
            gge::ClipChannel c;
            c.bone = in.bone;
            c.path = static_cast<gge::ChannelPath>(in.path);
            c.times.assign(in.times, in.times + in.key_count);
            size_t width = in.path == 1 ? 4 : 3;
            c.values.assign(in.values, in.values + in.key_count * width);
            clip.channels.push_back(std::move(c));
        }
        return r->impl.CreateClip(std::move(clip));
    });
}

GGE_API int gge_mesh_destroy(GgeRenderer* r, int mesh_id)
{
    return Guard([&] { r->impl.DestroyMesh(mesh_id); return 1; });
}

GGE_API int gge_texture_destroy(GgeRenderer* r, int texture_id)
{
    return Guard([&] { r->impl.DestroyTexture(texture_id); return 1; });
}

GGE_API int gge_skeleton_destroy(GgeRenderer* r, int skeleton_id)
{
    return Guard([&] { r->impl.DestroySkeleton(skeleton_id); return 1; });
}

GGE_API int gge_clip_destroy(GgeRenderer* r, int clip_id)
{
    return Guard([&] { r->impl.DestroyClip(clip_id); return 1; });
}

GGE_API int gge_object_set_skeleton(GgeRenderer* r, int object_id, int skeleton_id)
{
    return Guard([&] { r->impl.SetObjectSkeleton(object_id, skeleton_id); return 1; });
}

GGE_API int gge_object_set_pose(GgeRenderer* r, int object_id, int clip_a, float time_a, int clip_b,
                                float time_b, float blend)
{
    return Guard([&] { r->impl.SetObjectPose(object_id, clip_a, time_a, clip_b, time_b, blend); return 1; });
}

GGE_API int gge_object_get_pose(GgeRenderer* r, int object_id, float* out_matrices, int bone_capacity)
{
    return Guard([&] {
        std::vector<DirectX::XMFLOAT4X4> pose = r->impl.GetObjectPose(object_id);
        if (!out_matrices || bone_capacity < static_cast<int>(pose.size()))
            throw std::runtime_error("output buffer too small for " + std::to_string(pose.size()) + " bones");
        std::memcpy(out_matrices, pose.data(), pose.size() * sizeof(DirectX::XMFLOAT4X4));
        return static_cast<int>(pose.size());
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

GGE_API int gge_object_set_alpha(GgeRenderer* r, int object_id, float alpha)
{
    return Guard([&] { r->impl.SetObjectAlpha(object_id, alpha); return 1; });
}

GGE_API int gge_object_set_casts_shadow(GgeRenderer* r, int object_id, int casts)
{
    return Guard([&] { r->impl.SetObjectCastsShadow(object_id, casts != 0); return 1; });
}

GGE_API int gge_object_set_visible(GgeRenderer* r, int object_id, int visible)
{
    return Guard([&] { r->impl.SetObjectVisible(object_id, visible != 0); return 1; });
}

GGE_API int gge_object_is_visible(GgeRenderer* r, int object_id)
{
    try {
        return r->impl.IsObjectVisible(object_id) ? 1 : 0;
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return -1;
    }
}

GGE_API int gge_renderer_set_ambient(GgeRenderer* r, float red, float green, float blue)
{
    return Guard([&] { r->impl.SetAmbient({red, green, blue}); return 1; });
}

GGE_API int gge_renderer_set_directional_light(GgeRenderer* r, const float* direction, const float* color)
{
    return Guard([&] { r->impl.SetDirectionalLight(Vec3(direction), Vec3(color)); return 1; });
}

GGE_API int gge_light_create_point(GgeRenderer* r, const float* position, const float* color, float range)
{
    return Guard([&] { return r->impl.CreatePointLight(Vec3(position), Vec3(color), range); });
}

GGE_API int gge_light_set_point(GgeRenderer* r, int light_id, const float* position, const float* color, float range)
{
    return Guard([&] { r->impl.SetPointLight(light_id, Vec3(position), Vec3(color), range); return 1; });
}

GGE_API int gge_light_destroy(GgeRenderer* r, int light_id)
{
    return Guard([&] { r->impl.DestroyPointLight(light_id); return 1; });
}

GGE_API int gge_renderer_set_shadow(GgeRenderer* r, int enabled, float plane_y, float opacity)
{
    return Guard([&] { r->impl.SetShadow(enabled != 0, plane_y, opacity); return 1; });
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

GGE_API GgeWorld* gge_world_create(void) { return new GgeWorld(); }

GGE_API void gge_world_destroy(GgeWorld* w) { delete w; }

GGE_API int gge_world_add_room(GgeWorld* w, const float* xz, int vertex_count, const float* eye, const float* target,
                               float fov_y_radians)
{
    return Guard([&] {
        if (!eye || !target) {
            throw std::runtime_error("null camera vector argument");
        }
        gge::RoomCamera camera = {{eye[0], eye[1], eye[2]}, {target[0], target[1], target[2]}, fov_y_radians};
        return w->impl.AddRoom(xz, vertex_count, camera);
    });
}

GGE_API int gge_world_find_room(const GgeWorld* w, float x, float z) { return w->impl.FindRoom(x, z); }

GGE_API int gge_world_update_active_room(GgeWorld* w, float x, float z) { return w->impl.UpdateActiveRoom(x, z); }

GGE_API int gge_world_get_active_room(const GgeWorld* w) { return w->impl.ActiveRoom(); }

GGE_API int gge_world_get_room_camera(const GgeWorld* w, int room_id, float* eye, float* target,
                                      float* fov_y_radians)
{
    return Guard([&] {
        if (!eye || !target || !fov_y_radians) {
            throw std::runtime_error("null output argument");
        }
        const gge::RoomCamera& camera = w->impl.GetRoomCamera(room_id);
        std::memcpy(eye, camera.eye, sizeof(camera.eye));
        std::memcpy(target, camera.target, sizeof(camera.target));
        *fov_y_radians = camera.fovY;
        return 1;
    });
}

GGE_API int gge_world_add_box_collider(GgeWorld* w, float min_x, float min_z, float max_x, float max_z)
{
    return Guard([&] { return w->impl.AddBoxCollider(min_x, min_z, max_x, max_z); });
}

GGE_API int gge_world_set_collider_enabled(GgeWorld* w, int collider_id, int enabled)
{
    return Guard([&] { w->impl.SetColliderEnabled(collider_id, enabled != 0); return 1; });
}

GGE_API int gge_world_is_collider_enabled(const GgeWorld* w, int collider_id)
{
    try {
        return w->impl.IsColliderEnabled(collider_id) ? 1 : 0;
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return -1;
    }
}

GGE_API int gge_world_move_circle(const GgeWorld* w, float x, float z, float dx, float dz, float radius,
                                  float* out_xz)
{
    return Guard([&] {
        if (!out_xz) {
            throw std::runtime_error("null output argument");
        }
        gge::Vec2 result = w->impl.MoveCircle(x, z, dx, dz, radius);
        out_xz[0] = result.x;
        out_xz[1] = result.z;
        return 1;
    });
}

GGE_API int gge_world_move_input(GgeWorld* w, float input_x, float input_y, float speed, float dt, float* out_dxdz)
{
    return Guard([&] {
        if (!out_dxdz) {
            throw std::runtime_error("null output argument");
        }
        gge::Vec2 move = w->impl.MoveInput(input_x, input_y, speed, dt);
        out_dxdz[0] = move.x;
        out_dxdz[1] = move.z;
        return 1;
    });
}

GGE_API void gge_world_reset_input_frame(GgeWorld* w) { w->impl.ResetInputFrame(); }

GGE_API int gge_world_add_trigger(GgeWorld* w, const float* xz, int vertex_count, int tag)
{
    return Guard([&] { return w->impl.AddTrigger(xz, vertex_count, tag); });
}

GGE_API int gge_world_set_trigger_enabled(GgeWorld* w, int trigger_id, int enabled)
{
    return Guard([&] { w->impl.SetTriggerEnabled(trigger_id, enabled != 0); return 1; });
}

GGE_API int gge_world_update_triggers(GgeWorld* w, float x, float z)
{
    try {
        return w->impl.UpdateTriggers(x, z);
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return -1;  // 0은 "이벤트 없음"이라는 정상 결과라 실패는 -1로 구분한다
    }
}

GGE_API int gge_world_poll_trigger_event(GgeWorld* w, int* trigger_id, int* kind, int* tag)
{
    gge::TriggerEvent event;
    if (!w->impl.PollTriggerEvent(event)) {
        return 0;
    }
    if (trigger_id) {
        *trigger_id = event.triggerId;
    }
    if (kind) {
        *kind = event.kind;
    }
    if (tag) {
        *tag = event.tag;
    }
    return 1;
}

GGE_API int gge_world_is_inside_trigger(const GgeWorld* w, int trigger_id)
{
    try {
        return w->impl.IsInsideTrigger(trigger_id) ? 1 : 0;
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return -1;
    }
}

GGE_API const char* gge_last_error(void) { return gge::LastError().c_str(); }

GGE_API GgeScene* gge_scene_load(GgeRenderer* r, const char* path_utf8)
{
    try {
        if (!r)
            throw std::runtime_error("null renderer");
        return new GgeScene(r->impl, gge::ReadSceneFile(path_utf8 ? path_utf8 : ""));
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return nullptr;
    }
}

GGE_API void gge_scene_destroy(GgeScene* s) { delete s; }

GGE_API int gge_scene_step(GgeScene* s, float input_x, float input_y, float dt)
{
    return Guard([&] { s->impl.Step(input_x, input_y, dt); return 1; });
}

GGE_API int gge_scene_poll_event(GgeScene* s, int* kind, int* tag, const char** trigger_name)
{
    gge::SceneEvent e;
    if (!s->impl.PollEvent(e))
        return 0;
    s->eventName = std::move(e.triggerName);
    if (kind)
        *kind = e.kind;
    if (tag)
        *tag = e.tag;
    if (trigger_name)
        *trigger_name = s->eventName.c_str();
    return 1;
}

GGE_API int gge_scene_get_player(const GgeScene* s, float* x, float* z, float* yaw)
{
    if (!s->impl.HasPlayer())
        return 0;
    if (x)
        *x = s->impl.PlayerPosition().x;
    if (z)
        *z = s->impl.PlayerPosition().z;
    if (yaw)
        *yaw = s->impl.PlayerYaw();
    return 1;
}

GGE_API const char* gge_scene_active_room_name(const GgeScene* s) { return s->impl.ActiveRoomName().c_str(); }

GGE_API int gge_scene_find_object(const GgeScene* s, const char* node_name_utf8)
{
    return s->impl.FindObject(node_name_utf8 ? node_name_utf8 : "");
}

GGE_API int gge_scene_set_trigger_enabled(GgeScene* s, const char* trigger_name_utf8, int enabled)
{
    return Guard([&] {
        const std::string name = trigger_name_utf8 ? trigger_name_utf8 : "";
        const int count = s->impl.SetTriggerEnabled(name, enabled != 0);
        if (count == 0)
            throw std::runtime_error("no trigger named '" + name + "'");
        return count;
    });
}

GGE_API int gge_scene_set_collider_enabled(GgeScene* s, const char* collider_name_utf8, int enabled)
{
    return Guard([&] {
        const std::string name = collider_name_utf8 ? collider_name_utf8 : "";
        if (name.empty())
            throw std::runtime_error("a collider name must not be empty (unnamed colliders cannot be addressed)");
        const int count = s->impl.SetColliderEnabled(name, enabled != 0);
        if (count == 0)
            throw std::runtime_error("no collider named '" + name + "'");
        return count;
    });
}

GGE_API int gge_scene_is_inside_trigger(const GgeScene* s, const char* trigger_name_utf8)
{
    try {
        return s->impl.IsInsideTrigger(trigger_name_utf8 ? trigger_name_utf8 : "") ? 1 : 0;
    } catch (const std::exception& e) {
        gge::LastError() = e.what();
        return -1;
    }
}

GGE_API int gge_scene_get_node_position(const GgeScene* s, const char* node_name_utf8, float* x, float* y, float* z)
{
    DirectX::XMFLOAT3 position;
    const std::string name = node_name_utf8 ? node_name_utf8 : "";
    if (!s->impl.GetNodePosition(name, position)) {
        gge::LastError() = "no node named '" + name + "'";
        return 0;
    }
    if (x)
        *x = position.x;
    if (y)
        *y = position.y;
    if (z)
        *z = position.z;
    return 1;
}

GGE_API int gge_scene_set_node_position(GgeScene* s, const char* node_name_utf8, float x, float y, float z)
{
    return Guard([&] { s->impl.SetNodePosition(node_name_utf8 ? node_name_utf8 : "", {x, y, z}); return 1; });
}

namespace {

// 노드의 3값 읽기: getter(이름, 출력)가 false면 "no node named" 오류로 0.
template <class Getter>
int GetNodeVector(Getter&& getter, const char* node_name_utf8, float* x, float* y, float* z)
{
    DirectX::XMFLOAT3 v;
    const std::string name = node_name_utf8 ? node_name_utf8 : "";
    if (!getter(name, v)) {
        gge::LastError() = "no node named '" + name + "'";
        return 0;
    }
    if (x)
        *x = v.x;
    if (y)
        *y = v.y;
    if (z)
        *z = v.z;
    return 1;
}

}  // namespace

GGE_API int gge_scene_get_node_rotation(const GgeScene* s, const char* node_name_utf8, float* x, float* y, float* z)
{
    return GetNodeVector([&](const std::string& n, DirectX::XMFLOAT3& v) { return s->impl.GetNodeRotation(n, v); }, node_name_utf8, x, y, z);
}

GGE_API int gge_scene_set_node_rotation(GgeScene* s, const char* node_name_utf8, float x, float y, float z)
{
    return Guard([&] { s->impl.SetNodeRotation(node_name_utf8 ? node_name_utf8 : "", {x, y, z}); return 1; });
}

GGE_API int gge_scene_get_node_scale(const GgeScene* s, const char* node_name_utf8, float* x, float* y, float* z)
{
    return GetNodeVector([&](const std::string& n, DirectX::XMFLOAT3& v) { return s->impl.GetNodeScale(n, v); }, node_name_utf8, x, y, z);
}

GGE_API int gge_scene_set_node_scale(GgeScene* s, const char* node_name_utf8, float x, float y, float z)
{
    return Guard([&] { s->impl.SetNodeScale(node_name_utf8 ? node_name_utf8 : "", {x, y, z}); return 1; });
}

GGE_API int gge_scene_play_clip(GgeScene* s, const char* node_name_utf8, const char* clip_name_utf8, float fade_seconds, int restart, int loop)
{
    return Guard([&] {
        s->impl.PlayClip(node_name_utf8 ? node_name_utf8 : "", clip_name_utf8 ? clip_name_utf8 : "", fade_seconds, restart != 0, loop != 0);
        return 1;
    });
}

GGE_API int gge_scene_stop_clip(GgeScene* s, const char* node_name_utf8)
{
    return Guard([&] { s->impl.StopClip(node_name_utf8 ? node_name_utf8 : ""); return 1; });
}

GGE_API int gge_scene_set_clip_speed(GgeScene* s, const char* node_name_utf8, float speed)
{
    return Guard([&] { s->impl.SetClipSpeed(node_name_utf8 ? node_name_utf8 : "", speed); return 1; });
}

GGE_API int gge_scene_set_clip_playing(GgeScene* s, const char* node_name_utf8, int playing)
{
    return Guard([&] { s->impl.SetClipPlaying(node_name_utf8 ? node_name_utf8 : "", playing != 0); return 1; });
}

GGE_API int gge_scene_get_node_animation(GgeScene* s, const char* node_name_utf8, const char** clip_name, float* time, int* playing,
                                         float* speed, int* fading, int* loop, int* finished)
{
    return Guard([&] {
        const gge::Scene::NodeAnimation state = s->impl.GetNodeAnimation(node_name_utf8 ? node_name_utf8 : "");
        s->clipName = state.clip;
        if (clip_name)
            *clip_name = s->clipName.c_str();
        if (time)
            *time = state.time;
        if (playing)
            *playing = state.playing ? 1 : 0;
        if (speed)
            *speed = state.speed;
        if (fading)
            *fading = state.fading ? 1 : 0;
        if (loop)
            *loop = state.loop ? 1 : 0;
        if (finished)
            *finished = state.finished ? 1 : 0;
        return 1;
    });
}

GGE_API int gge_scene_get_player_speed(const GgeScene* s, float* speed)
{
    if (!s->impl.HasPlayer())
        return 0;
    if (speed)
        *speed = s->impl.PlayerSpeed();
    return 1;
}

GGE_API int gge_scene_get_node_visible(const GgeScene* s, const char* node_name_utf8)
{
    bool visible = false;
    const std::string name = node_name_utf8 ? node_name_utf8 : "";
    if (!s->impl.GetNodeVisible(name, visible)) {
        gge::LastError() = "no node named '" + name + "'";
        return -1;
    }
    return visible ? 1 : 0;
}

GGE_API int gge_scene_set_node_visible(GgeScene* s, const char* node_name_utf8, int visible)
{
    return Guard([&] { s->impl.SetNodeVisible(node_name_utf8 ? node_name_utf8 : "", visible != 0); return 1; });
}

GGE_API int gge_scene_teleport_player(GgeScene* s, float x, float z)
{
    return Guard([&] { s->impl.TeleportPlayer(x, z); return 1; });
}
