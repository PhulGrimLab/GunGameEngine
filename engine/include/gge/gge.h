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

// ---- 스켈레탈 애니메이션 ----
// 스키닝 메시: 일반 정점(float[8]) 배열 + 정점당 조인트 4개(uint32)와 가중치 4개(float).
// 가중치는 합이 1이 되도록 정규화되고, 영향이 없는 정점(합 0)은 본 0에 고정된다. 본은 최대 64개.
GGE_API int gge_mesh_create_skinned(GgeRenderer* r, const float* vertices, int vertex_count,
                                    const uint32_t* joints, const float* weights,
                                    const uint32_t* indices, int index_count);

// 본은 부모가 자식보다 앞에 오도록 정렬해야 한다(parents[i] == -1 또는 < i).
// inverse_bind: 본당 float[16], 행 벡터 규약(이동이 마지막 행; glTF의 열 우선 배열과 메모리 배치가 같다).
// rest_*: 레스트 포즈 로컬 변환. 회전은 쿼터니언 xyzw(정규화됨).
GGE_API int gge_skeleton_create(GgeRenderer* r, int bone_count, const int* parents, const float* inverse_bind,
                                const float* rest_translation, const float* rest_rotation,
                                const float* rest_scale);

// path: 0 = translation(3 float/key), 1 = rotation(xyzw 4 float/key), 2 = scale(3 float/key).
// times는 엄격히 증가하고 [0, duration] 안에 있어야 하며, 키 사이는 선형(회전은 slerp) 보간한다.
typedef struct GgeClipChannel {
    int bone;
    int path;
    int key_count;
    const float* times;
    const float* values;
} GgeClipChannel;
GGE_API int gge_clip_create(GgeRenderer* r, float duration, int channel_count, const GgeClipChannel* channels);

// 스켈레톤은 스키닝 메시와 함께 써야 하며, 메시가 쓰는 최대 본 번호가 본 수보다 작아야 한다. 0이면 해제.
GGE_API int gge_object_set_skeleton(GgeRenderer* r, int object_id, int skeleton_id);
// 시간은 호출자가 구동한다(엔진은 [0, duration]으로 클램프). clip_a 0 = 레스트 포즈, clip_b 0 = 블렌딩 없음.
// blend 0..1: 0이면 A, 1이면 B.
GGE_API int gge_object_set_pose(GgeRenderer* r, int object_id, int clip_a, float time_a, int clip_b,
                                float time_b, float blend);
// 본별 모델 공간 전역 행렬(float[16], 행 벡터 규약)을 out에 쓰고 본 수를 반환한다. 용량이 부족하거나 실패하면 0.
GGE_API int gge_object_get_pose(GgeRenderer* r, int object_id, float* out_matrices, int bone_capacity);

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

// ---- 게임플레이 월드 (렌더러와 독립) ----
// 방(room) = 바닥(XZ 평면) 위 단순 다각형 영역 + 고정 카메라. Y(높이)는 보지 않는다. 방 ID는 1부터, 0은 "없음".
// 월드는 한 스레드에서만 사용한다.
typedef struct GgeWorld GgeWorld;

GGE_API GgeWorld* gge_world_create(void);
GGE_API void gge_world_destroy(GgeWorld* w);

// xz: (x,z) 쌍 vertex_count개(시계/반시계 모두 허용). 3개 미만, 면적 0, 변 자기 교차, 비유한 값은 실패.
// eye/target/fov: eye != target, 0 < fov_y_radians < pi. 성공하면 방 ID를 반환한다.
GGE_API int gge_world_add_room(GgeWorld* w, const float* xz, int vertex_count, const float* eye,
                               const float* target, float fov_y_radians);

// (x,z)를 포함하는 방 중 ID가 가장 작은 것. 없으면 0. 경계 위는 포함이다. 활성 방은 바뀌지 않는다.
GGE_API int gge_world_find_room(const GgeWorld* w, float x, float z);

// 플레이어 위치로 활성 방을 갱신하고 활성 방 ID를 반환한다. 현재 활성 방이 점을 포함하면 유지(겹친 경계에서
// 깜빡임 방지), 아니면 find_room 결과로 전환, 어떤 방에도 속하지 않으면 마지막 활성 방을 유지한다(처음엔 0).
GGE_API int gge_world_update_active_room(GgeWorld* w, float x, float z);
GGE_API int gge_world_get_active_room(const GgeWorld* w);

// 방 카메라를 읽는다. eye/target은 float[3], fov는 포인터(각각 NULL 불가). gge_renderer_set_camera에 그대로 넘기면 된다.
GGE_API int gge_world_get_room_camera(const GgeWorld* w, int room_id, float* eye, float* target,
                                      float* fov_y_radians);

// 정적 장애물: 바닥(XZ 평면) 위 축 정렬 사각형. min < max이고 유한해야 한다. 성공하면 ID(>0)를 반환한다.
GGE_API int gge_world_add_box_collider(GgeWorld* w, float min_x, float min_z, float max_x, float max_z);

// 반지름 radius인 원형 이동체를 (x,z)에서 (dx,dz)만큼 움직이고, 장애물에 막히면 벽을 따라 미끄러진 최종 위치를
// out_xz(float[2])에 쓴다. 이동은 작은 걸음으로 나뉘어 얇은 벽을 통과하지 않는다. 시작 위치가 장애물과 겹쳐 있으면
// 밖으로 밀려난다. radius <= 0, 비유한 값, radius 대비 지나치게 큰 이동량(radius/2의 1024배 초과)은 실패한다.
// 월드 상태(활성 방 등)는 바뀌지 않는다.
GGE_API int gge_world_move_circle(const GgeWorld* w, float x, float z, float dx, float dz, float radius,
                                  float* out_xz);

// 카메라 상대 이동. input_x(오른쪽+), input_y(화면 위쪽/앞+)는 -1..1 정규화 축(키보드/게임패드 읽기는 호출자 몫).
// 활성 방 카메라 시선의 수평 성분이 "앞", 앞 x 위가 "오른쪽"이다. 입력 크기는 1로 제한(대각선 속도 동일)하고 0.2 미만은
// 입력 없음이다. out_dxdz(float[2])에 한 프레임 이동량 = 방향 x speed(월드 단위/초) x dt(초)를 쓴다.
// 카메라 컷: 입력을 누르고 있는 동안 활성 방이 바뀌어도 방향 기준은 입력을 시작한 방 카메라로 유지하고, 입력을
// 놓으면(0.2 미만) 현재 활성 방 기준으로 갱신한다. 활성 방이 없거나 비유한/음수 speed,dt이면 실패한다.
// 결과를 gge_world_move_circle에 넘기면 충돌까지 반영된다. gge_world_add_room은 거의 수직으로 내려보는 카메라를 거부한다.
GGE_API int gge_world_move_input(GgeWorld* w, float input_x, float input_y, float speed, float dt, float* out_dxdz);

// 마지막 오류 메시지(UTF-8). 스레드별로 유지된다.
GGE_API const char* gge_last_error(void);
