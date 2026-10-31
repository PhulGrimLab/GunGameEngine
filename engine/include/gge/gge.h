#pragma once
#include <stddef.h>
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

// ---- 리소스 해제 ----
// 알 수 없는 ID이거나 어떤 오브젝트가 아직 쓰고 있으면 실패한다(gge_last_error에 사용 중인 오브젝트 ID). 쓰는 오브젝트를
// 먼저 destroy하거나 다른 리소스로 바꾼 뒤 호출한다. 해제한 ID는 재사용되지 않는다.
GGE_API int gge_mesh_destroy(GgeRenderer* r, int mesh_id);
GGE_API int gge_texture_destroy(GgeRenderer* r, int texture_id);
GGE_API int gge_skeleton_destroy(GgeRenderer* r, int skeleton_id);
GGE_API int gge_clip_destroy(GgeRenderer* r, int clip_id);

// ---- 오브젝트(씬 그래프) ----
// mesh_id 0이면 그리지 않는 그룹 노드, texture_id 0이면 흰색, parent_id 0이면 루트.
GGE_API int gge_object_create(GgeRenderer* r, int mesh_id, int texture_id, int parent_id);
// 자손까지 함께 제거한다.
GGE_API int gge_object_destroy(GgeRenderer* r, int object_id);
// 살아 있는 오브젝트 수(씬이 만든 것 포함). 진단/누수 검사용.
GGE_API int gge_renderer_get_object_count(const GgeRenderer* r);
// 순환이 생기는 부모 지정은 실패한다.
GGE_API int gge_object_set_parent(GgeRenderer* r, int object_id, int parent_id);
GGE_API int gge_object_set_mesh(GgeRenderer* r, int object_id, int mesh_id);
GGE_API int gge_object_set_texture(GgeRenderer* r, int object_id, int texture_id);
// position/rotation/scale은 각각 float[3]. rotation은 오일러(pitch X, yaw Y, roll Z) 라디안. 비유한 값이 하나라도 있으면 실패하고 상태는 그대로다.
// 크기 0(한 축이라도)은 유한하므로 받는다: 월드 행렬이 특이 행렬이라 법선은 여인수 행렬로 구하며, 그 축으로 납작해진 오브젝트가 아주 얇은 오브젝트와
// 같은 조명으로 그려지고(테스트로 확인), 세 축이 모두 0이면 아무것도 그려지지 않는다.
GGE_API int gge_object_set_transform(GgeRenderer* r, int object_id, const float* position,
                                     const float* rotation, const float* scale);
GGE_API int gge_object_set_color(GgeRenderer* r, int object_id, float red, float green, float blue);
// 불투명도 0..1(유한). 1 = 불투명(기본), 0 = 그리지 않음, 그 사이 = 알파 블렌딩. 반투명 오브젝트는 불투명을 모두 그린 뒤
// 카메라에서 먼 것부터 깊이 쓰기 없이 섞는다(오브젝트 단위 정렬: 한 오브젝트 안의 면끼리는 정렬하지 않는다).
// 텍스처 알파는 별개로 컷아웃에 쓰인다: 알파가 절반 미만인 텍셀은 그리지 않는다.
GGE_API int gge_object_set_alpha(GgeRenderer* r, int object_id, float alpha);
// 그림자를 드리울지(gge_renderer_set_shadow가 켜져 있을 때). 기본은 0.
GGE_API int gge_object_set_casts_shadow(GgeRenderer* r, int object_id, int casts);
// 표시 여부(visible != 0이면 보임, 처음엔 보임). 숨기면 그 오브젝트와 모든 자손이 그려지지 않고 그림자도 드리우지 않으며 반투명 정렬에서도 빠진다.
// 알파/그림자 설정과는 별개 상태라 다시 보이게 하면 그대로 돌아온다. 점광원은 오브젝트가 아니라 영향받지 않는다.
GGE_API int gge_object_set_visible(GgeRenderer* r, int object_id, int visible);
// 그 오브젝트 자신의 표시 플래그: 보임 1, 숨김 0, 알 수 없는 ID면 -1(부모가 숨겨졌는지는 보지 않는다).
GGE_API int gge_object_is_visible(GgeRenderer* r, int object_id);

// ---- 조명 ----
// 밝기 = 앰비언트 + 방향광색 * max(0, N.-L) + 점광원 합. 점광원은 색 * max(0, N.l) * a^2, a = saturate(1 - 거리/범위).
// 기본값은 앰비언트 (0.25,0.25,0.25), 방향광 방향 (-0.4,-1,-0.6), 방향광색 (0.75,0.75,0.75)로, 조명 API가 없던 때의 고정 조명과 같다.
// 색 성분은 유한하고 0 이상(1 초과 허용). 잘못된 값은 0을 반환한다.
GGE_API int gge_renderer_set_ambient(GgeRenderer* r, float red, float green, float blue);
// direction은 빛이 나아가는 방향(float[3], 0이 아닌 유한 벡터, 정규화는 엔진이 한다). color는 float[3].
GGE_API int gge_renderer_set_directional_light(GgeRenderer* r, const float* direction, const float* color);
// 점광원은 렌더러당 최대 8개. position/color는 float[3], range > 0. ID는 1부터이며 해제 후에도 재사용되지 않는다.
GGE_API int gge_light_create_point(GgeRenderer* r, const float* position, const float* color, float range);
GGE_API int gge_light_set_point(GgeRenderer* r, int light_id, const float* position, const float* color, float range);
GGE_API int gge_light_destroy(GgeRenderer* r, int light_id);
// 평면 투영 그림자: 방향광을 y = plane_y 평면에 투영한 모양을 opacity(0..1)만큼 어둡게 칠한다. 겹치는 부분도 한 번만 칠한다.
// 방향광이 아래를 향할 때(y 성분 < 0)만 그려진다. 바닥 평면에만 떨어지며(다른 면에는 안 떨어짐), 드리우는 오브젝트는
// 평면 위에 있어야 한다. 기본은 꺼짐.
GGE_API int gge_renderer_set_shadow(GgeRenderer* r, int enabled, float plane_y, float opacity);

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
// 콜라이더를 켜거나 끈다(처음엔 모두 켜짐, enabled != 0이면 켬). 꺼진 콜라이더는 gge_world_move_circle이 없는 것으로 본다. 켜는 순간 안에
// 겹쳐 있던 이동체는 다음 gge_world_move_circle(이동량 0이어도)에서 가장 가까운 면 바깥으로 밀려난다. 알 수 없는 ID면 0.
GGE_API int gge_world_set_collider_enabled(GgeWorld* w, int collider_id, int enabled);
// 콜라이더가 켜져 있으면 1, 꺼져 있으면 0, 알 수 없는 ID면 -1(gge_last_error에 사유).
GGE_API int gge_world_is_collider_enabled(const GgeWorld* w, int collider_id);

// 반지름 radius인 원형 이동체를 (x,z)에서 (dx,dz)만큼 움직이고, 장애물에 막히면 벽을 따라 미끄러진 최종 위치를
// out_xz(float[2])에 쓴다. 이동은 작은 걸음으로 나뉘어 얇은 벽을 통과하지 않는다. 시작 위치가 장애물과 겹쳐 있으면
// 밖으로 밀려난다. radius <= 0, 비유한 값, radius 대비 지나치게 큰 이동량(radius/2의 1024배 초과)은 실패한다.
// 월드 상태(활성 방 등)는 바뀌지 않는다.
GGE_API int gge_world_move_circle(const GgeWorld* w, float x, float z, float dx, float dz, float radius,
                                  float* out_xz);

// 카메라 상대 이동. input_x(오른쪽+), input_y(화면 위쪽/앞+)는 -1..1 정규화 축(키보드/게임패드 읽기는 호출자 몫).
// 활성 방 카메라 시선의 수평 성분이 "앞", 앞 x 위가 "오른쪽"이다. 입력 크기는 1로 제한(대각선 속도 동일)하고 0.2 이하는
// 입력 없음, 그 위는 (크기-0.2)/0.8로 0부터 늘어난다. out_dxdz(float[2])에 한 프레임 이동량 = 방향 x speed(월드 단위/초)
// x dt(초)를 쓴다.
// 카메라 컷: 입력이 눌린 채 활성 방이 바뀌면, 컷 직후 입력 방향에서 60도 넘게 꺾기 전까지 방향 기준을 입력을 시작한 방
// 카메라로 유지하고, 놓거나(0.2 미만) 크게 꺾으면 현재 활성 방 기준으로 갱신한다. 입력이 끊기지 않는 채 장면이 바뀌는
// 텔레포트/컷신에는 gge_world_reset_input_frame을 부른다.
// 활성 방이 없거나 비유한/음수 speed,dt, 결과 오버플로이면 실패한다. 결과를 gge_world_move_circle에 넘기면 충돌까지
// 반영된다. gge_world_add_room은 거의 수직으로 내려보는 카메라를 거부한다.
GGE_API int gge_world_move_input(GgeWorld* w, float input_x, float input_y, float speed, float dt, float* out_dxdz);
// 입력 방향 기준을 비운다. 다음 gge_world_move_input은 그 시점의 활성 방 카메라 기준으로 시작한다.
GGE_API void gge_world_reset_input_frame(GgeWorld* w);

// ---- 트리거 (영역 진입/퇴장 이벤트) ----
// 플레이어 한 명의 위치(점)로 판정한다. 영역은 방과 같은 규칙의 XZ 평면 다각형(경계 포함, 오목 가능)이다.
#define GGE_TRIGGER_ENTER 1
#define GGE_TRIGGER_EXIT 2

// 영역을 추가하고 트리거 ID(1부터)를 반환한다. 다각형 검사는 gge_world_add_room과 같다. tag는 이벤트에 그대로 실리는
// 호출자 정의 값(문/아이템 식별 등)이다.
GGE_API int gge_world_add_trigger(GgeWorld* w, const float* xz, int vertex_count, int tag);

// 꺼진 트리거는 "플레이어가 밖에 있는 것"으로 본다: 안에 있는 채로 끄면 다음 update에서 EXIT, 안에서 다시 켜면 ENTER가 생긴다.
GGE_API int gge_world_set_trigger_enabled(GgeWorld* w, int trigger_id, int enabled);

// 플레이어 위치를 반영하고 이번 호출에서 생긴 이벤트 수(0 이상)를 반환한다. 실패(비유한 값)는 -1.
// 한 호출 안에서는 EXIT가 먼저, 그다음 ENTER이며 각각 트리거 ID 순이다. 처음부터 안에 있으면 ENTER가 생긴다.
// 위치는 프레임마다 샘플링하므로 한 프레임 이동 거리보다 얇은 영역은 건너뛸 수 있다.
GGE_API int gge_world_update_triggers(GgeWorld* w, float x, float z);

// 가장 오래된 이벤트 하나를 꺼내 1을 반환한다. 없으면 0. 각 출력 포인터는 필요 없으면 NULL이어도 된다.
// 큐는 1024개까지이며 가득 차면 가장 오래된 것부터 버린다(매 프레임 폴링해야 한다).
GGE_API int gge_world_poll_trigger_event(GgeWorld* w, int* trigger_id, int* kind, int* tag);

// 마지막 update 기준으로 영역 안이면 1, 밖(또는 꺼짐)이면 0, 알 수 없는 ID면 -1.
GGE_API int gge_world_is_inside_trigger(const GgeWorld* w, int trigger_id);

// 마지막 오류 메시지(UTF-8). 스레드별로 유지된다.
// ---- 구운 씬 런타임 (독립 플레이어가 쓴다) ----
// .ggs는 에디터(C# SceneBaker)가 씬과 거기서 참조하는 메시/텍스처/glTF를 하나로 합친 파일이다. 로드하면 렌더러에 메시/텍스처/
// 오브젝트/조명을 만들고 게임플레이 월드(방/충돌체/트리거/플레이어)를 구성한다. 규칙은 에디터의 플레이 모드와 같다:
// 프레임 순서(gge_scene_step) = 입력 -> 활성 방 카메라 기준 이동 -> 충돌 -> 활성 방 갱신 -> 트리거 -> 애니메이션 ->
// 플레이어 변환 적용 -> 렌더러 카메라를 활성 방 카메라로. 플레이어 모델 앞쪽은 +Z로 가정한다.
typedef struct GgeScene GgeScene;

// 실패하면 NULL(gge_last_error에 사유: 파일을 못 읽음, 형식/체크섬 오류, 항목별 오류). 렌더러의 조명/그림자 설정은 씬의 값으로 바뀐다.
GGE_API GgeScene* gge_scene_load(GgeRenderer* r, const char* path_utf8);
// 씬이 만든 오브젝트/조명/리소스를 렌더러에서 제거한다. 반드시 렌더러보다 먼저 파괴해야 한다.
GGE_API void gge_scene_destroy(GgeScene* s);
// 한 프레임 진행. 입력 축은 -1..1(오른쪽+, 화면 위쪽+), dt는 유한하고 0 이상인 초. 플레이어가 없는 씬은 애니메이션만 진행한다.
GGE_API int gge_scene_step(GgeScene* s, float input_x, float input_y, float dt);
// 가장 오래된 트리거 이벤트를 꺼낸다(없으면 0). kind는 GGE_TRIGGER_ENTER/EXIT. trigger_name은 다음 호출 전까지 유효한 UTF-8(NULL 가능).
GGE_API int gge_scene_poll_event(GgeScene* s, int* kind, int* tag, const char** trigger_name);
// 플레이어의 바닥 위치와 Y축 회전(라디안). 플레이어가 없는 씬이면 0을 반환한다. 각 포인터는 NULL 가능.
GGE_API int gge_scene_get_player(const GgeScene* s, float* x, float* z, float* yaw);
// 활성 방 이름(UTF-8, 다음 호출 전까지 유효). 방이 없으면 빈 문자열.
GGE_API const char* gge_scene_active_room_name(const GgeScene* s);
// 이름이 같은 첫 노드의 렌더러 오브젝트 ID(없으면 0). 테스트/도구가 오브젝트를 직접 만질 때 쓴다.
GGE_API int gge_scene_find_object(const GgeScene* s, const char* node_name_utf8);
// 이름이 같은 트리거를 모두 켜거나 끈다(enabled != 0이면 켬). 바뀐 트리거 수(>0)를 반환하고, 그 이름이 없으면 0(gge_last_error에 사유).
// 안에 있는 채로 끄면 다음 gge_scene_step에서 Exit, 안에 있는 채로 다시 켜면 Enter 이벤트가 나온다.
GGE_API int gge_scene_set_trigger_enabled(GgeScene* s, const char* trigger_name_utf8, int enabled);
// 플레이어를 바닥 위치 (x,z)로 즉시 옮긴다(문/방 이동). 활성 방, 카메라, 트리거(이벤트는 poll로)가 새 위치로 갱신되고 방향은 그대로다.
// 플레이어가 없거나, 값이 유한하지 않거나, 어떤 방에도 속하지 않으면 0(상태는 그대로). 충돌체 안인지는 검사하지 않는다. 성공하면 1.
GGE_API int gge_scene_teleport_player(GgeScene* s, float x, float z);
// 이름이 같은 충돌체를 모두 켜거나 끈다(enabled != 0이면 켬, 처음엔 모두 켜짐): 문을 열고 닫는 데 쓴다. 바뀐 충돌체 수(>0)를 반환하고, 그 이름이
// 없거나 이름이 비어 있으면 0(gge_last_error에 사유; 이름은 대소문자를 구분하고 .ggs version 1 파일의 충돌체는 이름이 없어 지정할 수 없다).
// 닫을 때 플레이어가 안에 겹쳐 있으면 다음 gge_scene_step에서 가장 가까운 면 바깥으로 밀려난다(얇은 문이 방 경계에 걸치면 반대편으로 나갈 수 있다).
GGE_API int gge_scene_set_collider_enabled(GgeScene* s, const char* collider_name_utf8, int enabled);
// 플레이어가 지금 그 이름의 트리거(여럿이면 어느 하나) 안에 있으면 1, 밖이면 0, 그 이름이 없으면 -1(gge_last_error에 사유).
// 이벤트와 같은 시점의 상태다: 마지막 gge_scene_step/텔레포트 기준이고, 트리거를 끄거나 켠 결과는 다음 gge_scene_step부터 보인다
// (끈 트리거는 그때부터 밖).
GGE_API int gge_scene_is_inside_trigger(const GgeScene* s, const char* trigger_name_utf8);
// 이름이 같은 첫 노드의 부모 기준(로컬) 위치를 읽는다(마지막으로 설정한 값, 처음엔 구운 값). 없으면 0. 각 포인터는 NULL 가능.
GGE_API int gge_scene_get_node_position(const GgeScene* s, const char* node_name_utf8, float* x, float* y, float* z);
// 그 노드의 로컬 위치를 바꾼다(회전/크기 유지). 노드가 없거나, 값이 유한하지 않거나, 플레이어 노드면 0(상태는 그대로, 플레이어는
// gge_scene_teleport_player로만 옮긴다). 성공하면 1.
GGE_API int gge_scene_set_node_position(GgeScene* s, const char* node_name_utf8, float x, float y, float z);
// 이름이 같은 첫 노드의 로컬 회전(오일러 pitch X, yaw Y, roll Z, 라디안)과 로컬 크기. 읽기는 없으면 0, 쓰기는 위치와 같은 규칙:
// 노드가 없거나, 비유한 값이거나, 플레이어 노드면 0(상태는 그대로). 성공하면 1.
GGE_API int gge_scene_get_node_rotation(const GgeScene* s, const char* node_name_utf8, float* x, float* y, float* z);
GGE_API int gge_scene_set_node_rotation(GgeScene* s, const char* node_name_utf8, float x, float y, float z);
GGE_API int gge_scene_get_node_scale(const GgeScene* s, const char* node_name_utf8, float* x, float* y, float* z);
GGE_API int gge_scene_set_node_scale(GgeScene* s, const char* node_name_utf8, float x, float y, float z);
// 이름이 같은 첫 노드의 표시 여부(처음엔 보임). 숨기면 그 노드와 모든 자손이 그려지지 않고 그림자도 없다. 알파와 별개 상태라 다시 보이게 하면
// 알파는 그대로다. 읽기는 보임 1 / 숨김 0 / 없는 노드 -1(gge_last_error에 사유). 쓰기는 없는 노드면 0. 플레이어 노드도 숨길 수 있다
// (위치/회전/크기와 달리 플레이어 규칙과 충돌하지 않는다: 컷신/퇴장 연출용).
// ---- 클립 재생(스킨 메시 노드, 플레이어 노드 포함) ----
// 규칙은 C# AnimationPlayer와 같다(교차 검증 테스트가 프레임마다 본 행렬을 비교). 대상은 이름이 같은 첫 노드. 실패하면 0(gge_last_error에 사유):
// 없는 노드, 스킨이 아닌 노드("has no skeleton animation"), fade_seconds가 유한한 0..60이 아님, speed가 유한하지 않음, 없는 클립 이름(대소문자 구분,
// 쓸 수 있는 이름 목록이 오류에 들어가며 20개를 넘으면 앞 20개와 총 개수). 클립 이름은 씬의 클립 이름 그대로 쓴다(Blender glb는 "Armature|Walk"
// 형태일 수 있고, 에디터의 클립 목록/GetNodeAnimation이 돌려주는 이름이 그것이다).
// 플레이어 노드도 허용한다: 플레이어 시스템은 위치/회전/크기만 정하고 애니메이션은 독립이라 충돌하지 않는다(걷기/대기 전환이 이 용도).
// gge_scene_play_clip: 그 클립을 0초부터 재생한다. 이미 그 클립이 현재 클립이면(loop 값이 달라도) 아무것도 하지 않는다: 끝난 한 번 재생 클립이나
// 반복 중인 클립을 loop만 바꿔 다시 부르면 무시되므로 처음부터 다시 하려면 restart != 0을 준다(restart는 페이드 없이 즉시 0초부터, loop도 새로 적용).
// 다른 클립이고 이미 클립을 재생 중이며 fade_seconds > 0이면 이전 클립에서 크로스페이드한다(이전 클립은 페이드 동안 계속 진행하고, 한 번 재생이었다면 끝 포즈에
// 머문다). 레스트 포즈에서 시작할 때는 페이드 없이 즉시 바뀐다. loop != 0이면 반복(기본 용도), 0이면 한 번 재생하고 재생 방향의 끝 시간에서 멈춰 마지막
// 포즈를 유지한다(finished). 일시정지 중에도 바뀐 포즈가 바로 반영된다.
GGE_API int gge_scene_play_clip(GgeScene* s, const char* node_name_utf8, const char* clip_name_utf8, float fade_seconds, int restart, int loop);
// 재생을 멈추고 레스트 포즈로 돌아간다.
GGE_API int gge_scene_stop_clip(GgeScene* s, const char* node_name_utf8);
// 재생 속도 배율(유한, 음수면 거꾸로). 현재 클립과 크로스페이드 중인 이전 클립에 모두 적용된다.
GGE_API int gge_scene_set_clip_speed(GgeScene* s, const char* node_name_utf8, float speed);
// playing == 0이면 일시정지(시간과 크로스페이드 진행이 모두 멈춘다), 아니면 재개. 한 번 재생이 끝난 뒤에도 이 값은 그대로(끝남은 finished로 따로 본다).
GGE_API int gge_scene_set_clip_playing(GgeScene* s, const char* node_name_utf8, int playing);
// 현재 재생 상태. clip_name은 재생 중인 클립 이름(레스트 포즈면 빈 문자열, 같은 씬의 다음 gge_scene_get_node_animation 호출 전까지 유효한 UTF-8 - 그 사이 다른 API를 불러도 바뀌지 않지만 다음 호출이 덮어쓰니 필요하면 복사), time은 그 클립의 현재 시간(초),
// fading은 크로스페이드 중인가, loop는 반복인가, finished는 한 번 재생이 재생 방향의 끝(speed >= 0이면 끝 시간, 음수면 0초)에 닿아 멈춘 상태인가
// (반복/레스트는 항상 0, 끝에서 speed 부호를 바꾸면 0이 되어 거꾸로 재생된다). 각 포인터는 NULL 가능. 성공하면 1.
GGE_API int gge_scene_get_node_animation(GgeScene* s, const char* node_name_utf8, const char** clip_name, float* time, int* playing,
                                         float* speed, int* fading, int* loop, int* finished);
// 마지막 gge_scene_step에서 충돌/슬라이딩을 반영한 뒤 플레이어가 실제로 이동한 거리 / dt(월드 단위/초). 벽을 정면으로 밀면 0이고 벽을 따라 미끄러지면
// 0보다 크다. 텔레포트 직후(다음 step 전), dt가 0인 step은 0이다(순간이동이 속도 폭주를 만들지 않는다). 켜진 충돌체가 겹친 플레이어를 밀어내는 이동도
// 이동으로 센다. 플레이어가 없는 씬이면 0을 반환한다.
GGE_API int gge_scene_get_player_speed(const GgeScene* s, float* speed);
GGE_API int gge_scene_get_node_visible(const GgeScene* s, const char* node_name_utf8);
GGE_API int gge_scene_set_node_visible(GgeScene* s, const char* node_name_utf8, int visible);

// ---- 동적 오브젝트(생성/삭제) ----
// 씬 파일의 노드(템플릿)와 그 모든 자손을 복제한다. 메시/텍스처/스켈레톤/클립은 공유하고 오브젝트만 새로 만든다.
// 복제 루트 이름 = instance_name, 자손 이름 = "instance_name/원래이름"이며 이후 모든 이름 기반 gge_scene_* 가 이 이름으로 동작한다.
// 시작 상태는 템플릿의 **구운 값**(스크립트가 템플릿을 바꿔도 영향 없음), 복제 루트는 보인다. (x,y,z)는 **템플릿과 같은 부모 기준의 로컬 위치**이고
// 회전/크기는 구운 값이라 방향은 직후에 gge_scene_set_node_rotation으로 정한다. 조명/충돌체/트리거는 복제하지 않는다.
// 실패(0, 상태 그대로, 누수 없음): 없는 템플릿, 템플릿이 복제본이거나 플레이어(를 포함), 인스턴스 이름이 비었거나 '/' 포함/128바이트 초과/이미 있는 노드 이름과 같음
// (자손 이름 포함), 값이 유한하지 않음, 한도 초과(살아 있는 복제 노드 256, 한 step 사이 64번 spawn, 서브트리 깊이 16). 성공하면 1.
GGE_API int gge_scene_spawn_object(GgeScene* s, const char* template_name_utf8, const char* instance_name_utf8, float x, float y, float z);
// gge_scene_spawn_object로 만든 인스턴스(루트 이름)를 자손까지 지운다. 그 이름은 다시 쓸 수 있다. 씬 파일의 노드, 복제본의 자손 단독, 없는 이름은 0.
GGE_API int gge_scene_destroy_object(GgeScene* s, const char* instance_name_utf8);
// 그 이름의 노드가 있으면 1, 없으면 0(씬 파일/복제 구분 없음).
GGE_API int gge_scene_object_exists(const GgeScene* s, const char* node_name_utf8);
// 살아 있는 복제 노드 수(자손 포함).
GGE_API int gge_scene_get_dynamic_node_count(const GgeScene* s);
// 노드 슬롯 수(씬 파일의 노드 + 복제가 쓴 슬롯의 최대치). 지운 슬롯은 재사용되므로 씬 파일 노드 수 + 256(복제 노드 한도)을 넘어 늘지 않는다. 진단/테스트용이며 안정 API가 아니다.
GGE_API int gge_scene_get_node_slot_count(const GgeScene* s);
// 테스트 전용(안정 API 아님, 배포 빌드에서 제거/비활성 대상: doc/04 P10): 이후 spawn이 n번째(1부터) 렌더러 오브젝트를 만든 직후 실패하게 한다(-1 = 끔).
// 중간 실패 되돌림 검증용이며 게임 코드가 쓸 일은 없다. C# 래퍼/스크립트 API에는 노출하지 않는다.
GGE_API int gge_scene_set_spawn_failure_for_tests(GgeScene* s, int after_nodes);

// ---- 비트맵 폰트와 글자 그리기(P3) ----
// 폰트는 BDF 비트맵 파일(예: Galmuri11.bdf, 한글 완성형 11172자 포함)이고 글자는 1비트 픽셀 그대로 정수 좌표에 그린다. 그리기는 호출자가 준 BGRA 버퍼에
// 정수 연산만으로 덮어쓰므로 어느 렌더 장치/프로세스에서나 바이트 단위로 같다. 렌더러의 화면에 올리는 항목은 P3.2에서 추가한다.
// 폰트 핸들은 렌더러/씬과 독립이고, 핸들을 destroy해도 그 폰트를 참조 중인 화면 항목이 있으면 항목이 지워질 때까지 폰트는 살아 있다(참조 공유).
typedef struct GgeFont GgeFont;
// BDF 파일(UTF-8 경로)을 읽는다. 잘못된 파일(잘림, 글리프 수 불일치, 한도 초과(파일 32MB, 글리프 20만, 글리프 한 변 64px), 중복 인코딩 등)은 NULL이고 사유("font file 'x': line N: ...")는 gge_last_error.
GGE_API GgeFont* gge_font_load(const char* path_utf8);
// 메모리의 BDF 텍스트(NUL 불필요)를 읽는다. 규칙은 gge_font_load와 같다.
GGE_API GgeFont* gge_font_load_memory(const void* data, size_t size);
GGE_API void gge_font_destroy(GgeFont* font);
// 줄 높이 = ascent + descent(픽셀). 각 포인터는 NULL 가능.
GGE_API int gge_font_get_metrics(const GgeFont* font, int* ascent, int* descent);
// 코드 포인트(유니코드 스칼라 값)의 글리프가 폰트에 있으면 1, 없으면 0.
GGE_API int gge_font_has_glyph(const GgeFont* font, uint32_t codepoint);
// UTF-8 글자를 줄로 나눠 크기를 잰다. 규칙(gge_font_draw_text와 같다): '\n'은 줄바꿈(N개면 N+1줄), 탭/CR/그 밖의 제어 문자는 무시, max_width > 0이면 그 폭을
// 넘기 전에 줄을 바꾸고(줄 안의 마지막 공백에서, 공백이 없으면 글자 경계에서; 폭보다 넓은 글자 하나는 혼자 한 줄) 0 이하면 '\n'에서만 바꾼다. 잘못된 UTF-8은
// U+FFFD 글자로, 폰트에 없는 글자는 폰트의 .notdef(빈 네모)로 보이며 폭/줄 규칙은 그대로다. 문자열은 1024바이트까지(초과는 0, 사유는 gge_last_error).
// text가 NULL이면 빈 문자열(0줄). width/height/lines는 NULL 가능. 성공하면 1.
GGE_API int gge_font_measure_text(const GgeFont* font, const char* text_utf8, int max_width, int* width, int* height, int* lines);
// BGRA 버퍼(행 간격 stride 바이트, 픽셀당 4바이트)에 글자를 그린다. (x, y)는 첫 줄 윗변의 왼쪽 위이고 줄 높이는 ascent + descent다. 글자는 불투명 1비트이며
// rgb는 0xRRGGBB. 버퍼 밖/음수 좌표/걸친 글자는 잘라서 그린다. 규칙과 한도는 gge_font_measure_text와 같고 실패하면 0(버퍼는 그대로).
GGE_API int gge_font_draw_text(const GgeFont* font, const char* text_utf8, int max_width, uint8_t* bgra, int width, int height, int stride, int x, int y, uint32_t rgb);
// BGRA 버퍼에 채운 사각형을 그린다. alpha 0..255(0은 아무것도 안 그림, 255는 덮어쓰기, 그 사이는 정수 혼합 (src*a + dst*(255-a) + 127) / 255). 버퍼 밖은 잘라서 그린다.
// 범위 밖 alpha, 크기가 0 이하인 사각형, 잘못된 버퍼 크기는 0(버퍼는 그대로).
GGE_API int gge_fill_rect_bgra(uint8_t* bgra, int width, int height, int stride, int x, int y, int rect_width, int rect_height, uint32_t rgb, int alpha);

// ---- 화면 위 항목(HUD/대화창): 렌더러가 매 gge_renderer_render의 끝에서 그린다 ----
// 항목은 3D가 모두 그려지고 GPU 후처리(PS1 효과)가 끝난 뒤 gge_renderer_get_pixels가 돌려주는 CPU 픽셀에 덮어 그려진다: PS1 효과를 받지 않고, WARP/GPU/에디터/
// 플레이어/헤드리스에서 바이트 단위로 같다. 좌표는 렌더 타깃의 정수 픽셀(왼쪽 위 기준)이며 화면 밖/음수/걸친 항목은 잘려서 그려진다. 그리는 순서는 만든 순서다
// (먼저 만든 것이 아래: 대화창 배경 사각형을 글자보다 먼저 만든다). ID는 1부터, 해제 후 재사용하지 않고, 렌더러당 최대 64개. 실패하면 0이고 사유는 gge_last_error(상태 그대로).
// 글자 항목은 폰트를 공유 참조하므로 gge_font_destroy를 먼저 해도 항목이 지워지거나 렌더러가 파괴될 때까지 폰트가 산다. gge_renderer_destroy는 항목을 모두 정리한다.
// 글자 규칙(줄바꿈/잘못된 UTF-8/없는 글자/1024바이트 한도)은 gge_font_measure_text와 같다. max_width <= 0이면 '\n'에서만 줄을 바꾼다. rgb는 0xRRGGBB.
GGE_API int gge_ui_create_text(GgeRenderer* r, const GgeFont* font, const char* text_utf8, int x, int y, int max_width, uint32_t rgb);
// 채운 사각형: 크기 > 0, alpha 0..255(0 = 안 보임, 255 = 불투명, 그 사이는 정수 혼합).
GGE_API int gge_ui_create_rect(GgeRenderer* r, int x, int y, int width, int height, uint32_t rgb, int alpha);
// 글자 항목의 글자를 바꾼다(사각형 항목이면 실패). text가 NULL이면 빈 문자열.
GGE_API int gge_ui_set_text(GgeRenderer* r, int item_id, const char* text_utf8);
// 글자 항목의 줄바꿈 폭을 바꾼다(0 이하 = '\n'에서만 줄을 바꾼다, 사각형 항목이면 실패).
GGE_API int gge_ui_set_max_width(GgeRenderer* r, int item_id, int max_width);
// 사각형 항목의 크기/알파를 바꾼다(글자 항목이면 실패). 체력 바 길이 같은 용도.
GGE_API int gge_ui_set_rect(GgeRenderer* r, int item_id, int width, int height, int alpha);
GGE_API int gge_ui_set_position(GgeRenderer* r, int item_id, int x, int y);
GGE_API int gge_ui_set_color(GgeRenderer* r, int item_id, uint32_t rgb);
// visible == 0이면 항목을 그리지 않는다(지우지 않고 숨김).
GGE_API int gge_ui_set_visible(GgeRenderer* r, int item_id, int visible);
GGE_API int gge_ui_destroy(GgeRenderer* r, int item_id);
// 모든 항목을 지운다.
GGE_API int gge_ui_clear(GgeRenderer* r);
// 살아 있는 항목 수.
GGE_API int gge_ui_get_count(const GgeRenderer* r);

GGE_API const char* gge_last_error(void);
