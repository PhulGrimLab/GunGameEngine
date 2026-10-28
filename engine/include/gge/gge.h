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
// 메시 정점 레이아웃: float[6] = position(xyz), normal(xyz).

typedef struct GgeRenderer GgeRenderer;

// 실패 시 NULL 또는 0을 반환하고 gge_last_error()로 사유를 얻는다.
GGE_API GgeRenderer* gge_renderer_create(int width, int height, int use_warp);
GGE_API void gge_renderer_destroy(GgeRenderer* r);

GGE_API int gge_renderer_set_mesh(GgeRenderer* r, const float* vertices, int vertex_count,
                                  const uint32_t* indices, int index_count);
// OBJ를 읽어 면 단위 플랫 노멀 메시로 설정한다. path는 UTF-8.
GGE_API int gge_renderer_load_obj(GgeRenderer* r, const char* path_utf8);

GGE_API void gge_renderer_set_camera(GgeRenderer* r, float eye_x, float eye_y, float eye_z,
                                     float target_x, float target_y, float target_z,
                                     float fov_y_radians);
GGE_API void gge_renderer_set_color(GgeRenderer* r, float red, float green, float blue);

GGE_API int gge_renderer_render(GgeRenderer* r);

// BGRA8 프레임버퍼. 다음 render 호출 전까지 유효하며 소유권은 엔진에 있다.
GGE_API const uint8_t* gge_renderer_get_pixels(GgeRenderer* r, int* width, int* height, int* stride);

// 마지막 오류 메시지(UTF-8). 스레드별로 유지된다.
GGE_API const char* gge_last_error(void);
