#pragma once
#include <stdint.h>

#ifdef GGE_EXPORTS
#define GGE_API extern "C" __declspec(dllexport)
#else
#ifdef __cplusplus
#define GGE_API extern "C" __declspec(dllimport)
#else
#define GGE_API __declspec(dllimport)
#endif
#endif

// 좌표계: 오른손 좌표계, Y-up, 삼각형 앞면은 반시계(CCW).
// 메시 정점 레이아웃: float[8] = position(xyz), normal(xyz), uv(uv). uv 원점은 이미지 좌상단.
// 회전: (pitch X, yaw Y, roll Z) 라디안.
//
// ID(메시/텍스처/오브젝트)는 1부터 시작하고 종류별로 독립이다. 0은 "없음" 또는 실패를 뜻한다.
// 실패 시 NULL/0을 반환하고 gge_last_error()로 사유를 얻는다. 반환값이 int인 함수는 성공 1, 실패 0.

typedef struct GgeRenderer GgeRenderer;

GGE_API GgeRenderer* gge_renderer_create(int width, int height, int use_warp);
GGE_API void gge_renderer_destroy(GgeRenderer* r);

// ---- 리소스 ----
GGE_API int gge_mesh_create(GgeRenderer* r, const float* vertices, int vertex_count,
                            const uint32_t* indices, int index_count);
// OBJ를 읽어 면 단위 플랫 노멀 메시로 만든다. path는 UTF-8.
GGE_API int gge_mesh_load_obj(GgeRenderer* r, const char* path_utf8);
GGE_API int gge_mesh_load_ggm(GgeRenderer* r, const char* path_utf8);
// OBJ를 .ggm 바이너리 메시로 변환한다(렌더러 불필요).
GGE_API int gge_convert_obj_to_ggm(const char* obj_path_utf8, const char* ggm_path_utf8);

// RGBA8, 행 우선, 첫 행이 이미지 위쪽.
GGE_API int gge_texture_create(GgeRenderer* r, const uint8_t* rgba, int width, int height);
// PNG/BMP/JPEG 등(WIC가 읽을 수 있는 형식).
GGE_API int gge_texture_load(GgeRenderer* r, const char* path_utf8);

// ---- 오브젝트(씬 그래프) ----
// mesh_id 0이면 그리지 않는 그룹 노드, texture_id 0이면 흰색, parent_id 0이면 루트.
GGE_API int gge_object_create(GgeRenderer* r, int mesh_id, int texture_id, int parent_id);
// 자손까지 함께 제거한다.
GGE_API int gge_object_destroy(GgeRenderer* r, int object_id);
// 순환이 생기는 부모 지정은 실패한다.
GGE_API int gge_object_set_parent(GgeRenderer* r, int object_id, int parent_id);
GGE_API int gge_object_set_mesh(GgeRenderer* r, int object_id, int mesh_id);
GGE_API int gge_object_set_texture(GgeRenderer* r, int object_id, int texture_id);
// position/rotation/scale은 각각 float[3].
GGE_API int gge_object_set_transform(GgeRenderer* r, int object_id, const float* position,
                                     const float* rotation, const float* scale);
GGE_API int gge_object_set_color(GgeRenderer* r, int object_id, float red, float green, float blue);

// ---- 카메라/렌더 ----
GGE_API void gge_renderer_set_camera(GgeRenderer* r, float eye_x, float eye_y, float eye_z,
                                     float target_x, float target_y, float target_z,
                                     float fov_y_radians);

// PS1풍 효과. 기본값은 모두 꺼짐(vertex_snap=0, color_bits=8, dither=0, affine_texture=0).
// color_bits는 채널당 비트 수(1..8, 8이면 양자화 없음). 범위를 벗어나면 0을 반환한다.
GGE_API int gge_renderer_set_retro(GgeRenderer* r, int vertex_snap, int color_bits, int dither,
                                   int affine_texture);

GGE_API int gge_renderer_render(GgeRenderer* r);

// BGRA8 프레임버퍼. 다음 render 호출 전까지 유효하며 소유권은 엔진에 있다.
GGE_API const uint8_t* gge_renderer_get_pixels(GgeRenderer* r, int* width, int* height, int* stride);

// 마지막 오류 메시지(UTF-8). 스레드별로 유지된다.
GGE_API const char* gge_last_error(void);
