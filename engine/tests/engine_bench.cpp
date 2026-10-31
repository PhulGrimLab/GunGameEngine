// 엔진 CPU 쪽 프레임 비용 벤치마크. 테스트가 아니므로 ctest에 등록하지 않고 수동으로 실행한다:
//   build\bin\engine_bench.exe [--hw]   (--hw: WARP 대신 하드웨어 GPU)
// 해상도를 작게 해서 래스터화(WARP)보다 오브젝트 순회/행렬/스키닝 준비 비용이 드러나게 한다.
// 각 시나리오는 워밍업 후 프레임당 평균 ms를 출력한다.
#include <gge/gge.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kWidth = 64, kHeight = 48;
constexpr int kWarmupFrames = 5, kMeasuredFrames = 60;
int g_useWarp = 1;  // --hw로 하드웨어 GPU 사용

// 단위 큐브(면당 정점 4개) 정점/인덱스.
void MakeCube(std::vector<float>& v, std::vector<uint32_t>& idx)
{
    struct Face { float n[3]; float p[4][3]; };
    const Face faces[] = {
        {{0, 0, 1},  {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}},
        {{0, 0, -1}, {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}},
        {{1, 0, 0},  {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}},
        {{-1, 0, 0}, {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}},
        {{0, 1, 0},  {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}},
        {{0, -1, 0}, {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}},
    };
    for (const Face& f : faces) {
        uint32_t base = static_cast<uint32_t>(v.size() / 8);
        for (int i = 0; i < 4; ++i)
            v.insert(v.end(), {f.p[i][0], f.p[i][1], f.p[i][2], f.n[0], f.n[1], f.n[2], 0, 0});
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

// 프레임당 평균 렌더 시간(ms). 객체 변환은 매 프레임 바꾸지 않는다(렌더 자체 비용만 잰다).
double TimeRender(GgeRenderer* r)
{
    for (int i = 0; i < kWarmupFrames; ++i)
        gge_renderer_render(r);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kMeasuredFrames; ++i)
        gge_renderer_render(r);
    std::chrono::duration<double, std::milli> total = std::chrono::steady_clock::now() - start;
    return total.count() / kMeasuredFrames;
}

// 평면(루트) 큐브 count개를 격자로 배치.
double BenchFlat(int count)
{
    GgeRenderer* r = gge_renderer_create(kWidth, kHeight, g_useWarp);
    std::vector<float> v;
    std::vector<uint32_t> idx;
    MakeCube(v, idx);
    int mesh = gge_mesh_create(r, v.data(), static_cast<int>(v.size() / 8), idx.data(), static_cast<int>(idx.size()));
    const float rot[3] = {0, 0, 0}, scale[3] = {0.2f, 0.2f, 0.2f};
    for (int i = 0; i < count; ++i) {
        int obj = gge_object_create(r, mesh, 0, 0);
        const float pos[3] = {static_cast<float>(i % 40) - 20, static_cast<float>(i / 40) - 5, 0};
        gge_object_set_transform(r, obj, pos, rot, scale);
    }
    gge_renderer_set_camera(r, 0, 0, 40, 0, 0, 0, 0.8f);
    double ms = TimeRender(r);
    gge_renderer_destroy(r);
    return ms;
}

// 깊이 depth의 부모-자식 체인 chains개(마지막 단계 노드만 메시).
double BenchDeepHierarchy(int chains, int depth)
{
    GgeRenderer* r = gge_renderer_create(kWidth, kHeight, g_useWarp);
    std::vector<float> v;
    std::vector<uint32_t> idx;
    MakeCube(v, idx);
    int mesh = gge_mesh_create(r, v.data(), static_cast<int>(v.size() / 8), idx.data(), static_cast<int>(idx.size()));
    const float rot[3] = {0, 0.01f, 0}, scale[3] = {1, 1, 1};
    for (int c = 0; c < chains; ++c) {
        int parent = 0;
        for (int d = 0; d < depth; ++d) {
            int obj = gge_object_create(r, mesh, 0, parent);
            const float pos[3] = {d == 0 ? static_cast<float>(c % 20) * 3 - 30 : 0.0f, 0.02f, 0};
            gge_object_set_transform(r, obj, pos, rot, scale);
            parent = obj;
        }
    }
    gge_renderer_set_camera(r, 0, 0, 80, 0, 0, 0, 0.8f);
    double ms = TimeRender(r);
    gge_renderer_destroy(r);
    return ms;
}

// 본 bones개 체인 스켈레톤을 쓰는 스키닝 오브젝트 count개(클립 재생 중).
double BenchSkinned(int count, int bones)
{
    GgeRenderer* r = gge_renderer_create(kWidth, kHeight, g_useWarp);
    std::vector<float> v;
    std::vector<uint32_t> idx;
    MakeCube(v, idx);
    int vertexCount = static_cast<int>(v.size() / 8);
    std::vector<uint32_t> joints(static_cast<size_t>(vertexCount) * 4, 0);
    std::vector<float> weights(static_cast<size_t>(vertexCount) * 4, 0.0f);
    for (int i = 0; i < vertexCount; ++i)
        weights[static_cast<size_t>(i) * 4] = 1.0f;
    int mesh = gge_mesh_create_skinned(r, v.data(), vertexCount, joints.data(), weights.data(), idx.data(),
                                       static_cast<int>(idx.size()));

    std::vector<int> parents(bones);
    std::vector<float> inverseBind(static_cast<size_t>(bones) * 16, 0.0f), restT(static_cast<size_t>(bones) * 3, 0.0f),
        restR(static_cast<size_t>(bones) * 4, 0.0f), restS(static_cast<size_t>(bones) * 3, 1.0f);
    for (int b = 0; b < bones; ++b) {
        parents[b] = b - 1;
        for (int k = 0; k < 4; ++k)
            inverseBind[static_cast<size_t>(b) * 16 + k * 5] = 1.0f;  // 단위 행렬
        restR[static_cast<size_t>(b) * 4 + 3] = 1.0f;
        restT[static_cast<size_t>(b) * 3 + 1] = 0.1f;
    }
    int skeleton = gge_skeleton_create(r, bones, parents.data(), inverseBind.data(), restT.data(), restR.data(),
                                       restS.data());

    // 모든 본에 회전 채널(키 3개).
    const float times[3] = {0.0f, 0.5f, 1.0f};
    const float rotations[12] = {0, 0, 0, 1, 0, 0, 0.1f, 0.995f, 0, 0, 0, 1};
    std::vector<GgeClipChannel> channels(bones);
    for (int b = 0; b < bones; ++b)
        channels[b] = {b, 1, 3, times, rotations};
    int clip = gge_clip_create(r, 1.0f, bones, channels.data());

    const float rot[3] = {0, 0, 0}, scale[3] = {1, 1, 1};
    for (int i = 0; i < count; ++i) {
        int obj = gge_object_create(r, mesh, 0, 0);
        gge_object_set_skeleton(r, obj, skeleton);
        gge_object_set_pose(r, obj, clip, 0.25f, 0, 0, 0);
        const float pos[3] = {static_cast<float>(i % 10) * 3 - 15, static_cast<float>(i / 10) * 3 - 6, 0};
        gge_object_set_transform(r, obj, pos, rot, scale);
    }
    gge_renderer_set_camera(r, 0, 0, 40, 0, 0, 0, 0.8f);
    double ms = TimeRender(r);
    gge_renderer_destroy(r);
    return ms;
}

// 동적 오브젝트: 템플릿 Crate(+Lid) 복제 128개(= 노드 256, 한도)를 올린 씬에서 (1) 프레임 렌더 비용, (2) 이름 API(노드 위치 읽기) 한 번의 비용,
// (3) 인스턴스 128개 생성+삭제 한 바퀴의 비용을 잰다(이름 조회가 해시 맵이라 복제 수와 무관해야 한다).
void BenchSpawner()
{
    GgeRenderer* r = gge_renderer_create(kWidth, kHeight, g_useWarp);
    GgeScene* scene = gge_scene_load(r, GGE_SPAWNER_SCENE);
    if (!scene) {
        std::printf("  spawner scene              : load failed (%s)\n", gge_last_error());
        gge_renderer_destroy(r);
        return;
    }
    gge_scene_set_node_visible(scene, "Crate", 0);
    const double emptyMs = TimeRender(r);
    char name[32];
    auto cycle = [&](bool destroy) {
        for (int i = 0; i < 128; ++i) {
            if (i % 64 == 0) gge_scene_step(scene, 0, 0, 0);   // 프레임당 spawn 64번 한도
            std::snprintf(name, sizeof name, "I%d", i);
            if (destroy) gge_scene_destroy_object(scene, name);
            else gge_scene_spawn_object(scene, "Crate", name, static_cast<float>(i % 12) - 6, 0.3f, static_cast<float>(i / 12) - 4);
        }
    };
    cycle(false);
    const double fullMs = TimeRender(r);

    float x, y, z;
    const int calls = 200000;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < calls; ++i) {
        std::snprintf(name, sizeof name, "I%d/Lid", i % 128);
        gge_scene_get_node_position(scene, name, &x, &y, &z);
    }
    std::chrono::duration<double, std::micro> lookups = std::chrono::steady_clock::now() - start;
    start = std::chrono::steady_clock::now();
    const int rounds = 200;
    for (int i = 0; i < rounds; ++i) {
        cycle(true);
        cycle(false);
    }
    std::chrono::duration<double, std::micro> cycles = std::chrono::steady_clock::now() - start;
    std::printf("  spawner 0 vs 256 clone nodes: %8.3f vs %8.3f ms/frame\n", emptyMs, fullMs);
    std::printf("  name API w/ 256 clone nodes : %8.3f us/call (includes the name formatting)\n", lookups.count() / calls);
    std::printf("  destroy+spawn 128 instances : %8.1f us per round (256 nodes)\n", cycles.count() / rounds);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

// 화면 위 글자: 64개 항목 x 한글/영문 섞인 100자(대화창 몇 개 + HUD보다 훨씬 많다)를 320x240 화면에 올린 프레임과 올리지 않은 프레임을 **번갈아** 재서(5라운드 중앙값)
// 오버레이 비용(글자 해독 + 줄바꿈 + 픽셀 복사)만 본다. 항목은 숨기기/보이기로 켜고 끄므로 두 쪽의 3D와 장면이 완전히 같다.
void BenchUi()
{
    GgeFont* font = gge_font_load(GGE_GALMURI_FONT);
    if (!font) {
        std::printf("  ui overlay                 : font load failed (%s)\n", gge_last_error());
        return;
    }
    GgeRenderer* r = gge_renderer_create(320, 240, g_useWarp);
    std::vector<float> v;
    std::vector<uint32_t> idx;
    MakeCube(v, idx);
    int mesh = gge_mesh_create(r, v.data(), static_cast<int>(v.size() / 8), idx.data(), static_cast<int>(idx.size()));
    for (int i = 0; i < 50; ++i) {
        int obj = gge_object_create(r, mesh, 0, 0);
        const float pos[3] = {static_cast<float>(i % 10) - 5, static_cast<float>(i / 10) - 2, 0}, rot[3] = {0.3f, 0.4f, 0}, scale[3] = {0.3f, 0.3f, 0.3f};
        gge_object_set_transform(r, obj, pos, rot, scale);
    }
    gge_renderer_set_camera(r, 0, 0, 12, 0, 0, 0, 0.8f);
    const char* text = "마을 사람: 여행자여, 이 마을에 온 것을 환영한다. 북쪽 문은 열쇠가 있어야 열린다네. Hello 123! 조심해서 다녀오게요.";   // 약 60 글자(한글+영문), 바이트는 100자 이상
    std::vector<int> items;
    for (int i = 0; i < 64; ++i)
        items.push_back(gge_ui_create_text(r, font, text, 2, (i * 12) % 230, 316, 0xFFFFFF));
    auto setVisible = [&](bool on) { for (int id : items) gge_ui_set_visible(r, id, on ? 1 : 0); };
    std::vector<double> off, on;
    for (int round = 0; round < 5; ++round) {
        setVisible(false);
        off.push_back(TimeRender(r));
        setVisible(true);
        on.push_back(TimeRender(r));
    }
    std::sort(off.begin(), off.end());
    std::sort(on.begin(), on.end());
    std::printf("  ui 64 items x ~60 glyphs    : %8.3f ms/frame (3D alone %8.3f, overlay +%.3f; median of 5 interleaved rounds, 320x240)\n", on[2], off[2], on[2] - off[2]);
    gge_renderer_destroy(r);
    gge_font_destroy(font);
}

}  // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--hw") == 0)
            g_useWarp = 0;
    std::printf("engine_bench (%dx%d, %s, %d frames avg)\n", kWidth, kHeight, g_useWarp ? "WARP" : "hardware GPU",
                kMeasuredFrames);
    std::printf("  flat    1000 cubes          : %8.3f ms/frame\n", BenchFlat(1000));
    std::printf("  hierarchy 100 chains x d20  : %8.3f ms/frame\n", BenchDeepHierarchy(100, 20));
    std::printf("  skinned  50 objs x 32 bones : %8.3f ms/frame\n", BenchSkinned(50, 32));
    BenchSpawner();
    BenchUi();
    return 0;
}
