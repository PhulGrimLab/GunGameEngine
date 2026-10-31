// 엔진 C API 테스트 하네스. 외부 프레임워크 없이 gge.dll을 C#과 동일한 경계로 검증한다.
// 환경 변수: GGE_UPDATE_GOLDEN=1 이면 골든 이미지를 다시 기록한다.
#include <gge/gge.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        ++g_checks;                                                                \
        if (!(cond)) {                                                             \
            ++g_failures;                                                          \
            std::printf("  CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                          \
    } while (0)

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;
    uint32_t At(int x, int y) const
    {
        uint32_t v;
        std::memcpy(&v, &bgra[(static_cast<size_t>(y) * width + x) * 4], 4);
        return v;
    }
};

int Red(uint32_t px) { return (px >> 16) & 0xFF; }
int Green(uint32_t px) { return (px >> 8) & 0xFF; }
int Blue(uint32_t px) { return px & 0xFF; }

// 큐브: 면마다 정점 4개(플랫 노멀), 반시계 앞면. 정점 = pos(3) + normal(3) + uv(2).
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
    const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (const Face& f : faces) {
        uint32_t base = static_cast<uint32_t>(v.size() / 8);
        for (int i = 0; i < 4; ++i)
            v.insert(v.end(), {f.p[i][0], f.p[i][1], f.p[i][2], f.n[0], f.n[1], f.n[2], uvs[i][0], uvs[i][1]});
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

Image Capture(GgeRenderer* r)
{
    Image img;
    int stride = 0;
    const uint8_t* p = gge_renderer_get_pixels(r, &img.width, &img.height, &stride);
    img.bgra.assign(p, p + static_cast<size_t>(stride) * img.height);
    return img;
}

GgeRenderer* NewRenderer(int w = 160, int h = 120)
{
    GgeRenderer* r = gge_renderer_create(w, h, 1);
    if (!r)
        std::printf("  create failed: %s\n", gge_last_error());
    return r;
}

int CubeMesh(GgeRenderer* r)
{
    std::vector<float> v;
    std::vector<uint32_t> idx;
    MakeCube(v, idx);
    int id = gge_mesh_create(r, v.data(), static_cast<int>(v.size() / 8), idx.data(), static_cast<int>(idx.size()));
    CHECK(id > 0);
    return id;
}

// XZ 평면(y=0) 사각형, 앞면은 +Y. uvScale만큼 UV가 반복된다.
int FloorMesh(GgeRenderer* r, float half, float uvScale)
{
    const float v[] = {
        -half, 0,  half, 0, 1, 0, 0,       uvScale,
         half, 0,  half, 0, 1, 0, uvScale, uvScale,
         half, 0, -half, 0, 1, 0, uvScale, 0,
        -half, 0, -half, 0, 1, 0, 0,       0,
    };
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    int id = gge_mesh_create(r, v, 4, idx, 6);
    CHECK(id > 0);
    return id;
}

// XY 평면 사각형(앞면 +Z), uv는 좌하(0,1) 우하(1,1) 우상(1,0) 좌상(0,0).
int QuadMesh(GgeRenderer* r, float half = 1.0f)
{
    const float v[] = {
        -half, -half, 0, 0, 0, 1, 0, 1,
         half, -half, 0, 0, 0, 1, 1, 1,
         half,  half, 0, 0, 0, 1, 1, 0,
        -half,  half, 0, 0, 0, 1, 0, 0,
    };
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    int id = gge_mesh_create(r, v, 4, idx, 6);
    CHECK(id > 0);
    return id;
}

// 큐브 오브젝트 하나 + 코너 뷰 카메라 + 주황색.
int SetupCubeScene(GgeRenderer* r)
{
    int obj = gge_object_create(r, CubeMesh(r), 0, 0);
    CHECK(obj > 0);
    gge_renderer_set_camera(r, 4.0f, 3.0f, 5.0f, 0, 0, 0, 0.8f);
    CHECK(gge_object_set_color(r, obj, 0.9f, 0.6f, 0.3f) == 1);
    return obj;
}

void Place(GgeRenderer* r, int obj, float x, float y, float z, float scale = 1.0f, float yaw = 0.0f)
{
    const float p[3] = {x, y, z}, rot[3] = {0, yaw, 0}, s[3] = {scale, scale, scale};
    CHECK(gge_object_set_transform(r, obj, p, rot, s) == 1);
}

Image Render(GgeRenderer* r)
{
    CHECK(gge_renderer_render(r) == 1);
    return Capture(r);
}

// 2x2 텍스처: 위 행 = 빨강, 초록 / 아래 행 = 파랑, 흰색.
const uint8_t kQuadTexture[16] = {
    255, 0, 0, 255,   0, 255, 0, 255,
    0, 0, 255, 255,   255, 255, 255, 255,
};

// ---- 골든 이미지 (P6 PPM, RGB) ----
bool WritePpm(const fs::path& path, const Image& img)
{
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << img.width << " " << img.height << "\n255\n";
    for (int i = 0; i < img.width * img.height; ++i) {
        const uint8_t* px = &img.bgra[static_cast<size_t>(i) * 4];
        const char rgb[3] = {static_cast<char>(px[2]), static_cast<char>(px[1]), static_cast<char>(px[0])};
        f.write(rgb, 3);
    }
    return true;
}

bool ReadPpm(const fs::path& path, Image& img)
{
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int maxv = 0;
    if (!f || !(f >> magic >> img.width >> img.height >> maxv) || magic != "P6") return false;
    f.get();
    std::vector<uint8_t> rgb(static_cast<size_t>(img.width) * img.height * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!f) return false;
    img.bgra.resize(static_cast<size_t>(img.width) * img.height * 4);
    for (int i = 0; i < img.width * img.height; ++i) {
        img.bgra[i * 4 + 0] = rgb[i * 3 + 2];
        img.bgra[i * 4 + 1] = rgb[i * 3 + 1];
        img.bgra[i * 4 + 2] = rgb[i * 3 + 0];
        img.bgra[i * 4 + 3] = 255;
    }
    return true;
}

// GGE_FORCE_DEVICE=hardware 로 하드웨어 GPU에서 돌리는 중인가(엔진이 같은 변수를 읽는다).
bool RunningOnHardware()
{
    const char* v = std::getenv("GGE_FORCE_DEVICE");
    return v != nullptr && std::string(v) == "hardware";
}

// 골든과 채널 허용 오차를 넘게 다른 픽셀 수. 골든에서 색 경계 위에 있는 픽셀(이웃 8칸 중 하나와 채널이
// 32 넘게 다름)은 edge로, 그 밖의 평평한 영역은 strict로 센다. 경계 여부를 실제 이미지로 판정하면
// 평평한 곳에 생긴 고립된 오류 픽셀이 스스로 경계가 되어 버리므로 골든 기준으로만 본다.
struct GoldenDiff { size_t strict = 0, edge = 0; };

GoldenDiff CompareImages(const Image& expected, const Image& actual, int tolerance)
{
    auto far = [](uint32_t a, uint32_t b, int limit) {
        return std::abs(Red(a) - Red(b)) > limit || std::abs(Green(a) - Green(b)) > limit ||
               std::abs(Blue(a) - Blue(b)) > limit;
    };
    auto onEdge = [&](const Image& img, int x, int y) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < img.width && ny < img.height && far(img.At(nx, ny), img.At(x, y), 32))
                    return true;
            }
        return false;
    };
    GoldenDiff diff;
    for (int y = 0; y < expected.height; ++y)
        for (int x = 0; x < expected.width; ++x) {
            if (!far(expected.At(x, y), actual.At(x, y), tolerance)) continue;
            (onEdge(expected, x, y) ? diff.edge : diff.strict)++;
        }
    return diff;
}

// 하드웨어 GPU는 삼각형 경계의 래스터 규칙이 WARP(골든 기준)과 달라 경계 픽셀만 크게 다를 수 있다.
// 그래서 하드웨어에서는 경계 픽셀 차이를 전체의 1%까지 허용하고, 평평한 영역은 여전히 엄격하게 본다.
constexpr double kHardwareEdgeBudget = 0.01;

bool GoldenAccepts(const GoldenDiff& diff, size_t pixelCount, bool hardware)
{
    if (diff.strict != 0) return false;
    if (!hardware) return diff.edge == 0;
    return static_cast<double>(diff.edge) <= kHardwareEdgeBudget * static_cast<double>(pixelCount);
}

// 채널당 허용 오차 이내인지 비교한다(하드웨어 GPU 실행이면 경계 픽셀 예외 적용).
bool MatchesGolden(const std::string& name, const Image& actual, int tolerance = 2)
{
    fs::path path = fs::path(GGE_TEST_DATA_DIR) / "golden" / (name + ".ppm");
    if (std::getenv("GGE_UPDATE_GOLDEN")) {
        std::printf("  updating golden %s\n", path.string().c_str());
        return WritePpm(path, actual);
    }
    Image expected;
    if (!ReadPpm(path, expected)) {
        std::printf("  golden missing: %s (run with GGE_UPDATE_GOLDEN=1)\n", path.string().c_str());
        return false;
    }
    if (expected.width != actual.width || expected.height != actual.height) return false;
    GoldenDiff diff = CompareImages(expected, actual, tolerance);
    if (!GoldenAccepts(diff, static_cast<size_t>(expected.width) * expected.height, RunningOnHardware())) {
        std::printf("  golden mismatch: %zu flat-area pixels, %zu edge pixels\n", diff.strict, diff.edge);
        WritePpm(fs::temp_directory_path() / (name + "_actual.ppm"), actual);
        return false;
    }
    return true;
}

fs::path TempDir()
{
    fs::path dir = fs::temp_directory_path() / "gge_tests";
    fs::create_directories(dir);
    return dir;
}

void WriteText(const fs::path& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }

std::vector<char> ReadBytes(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(f), {});
}

void WriteBytes(const fs::path& path, const std::vector<char>& bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// 24비트 BMP (아래 행부터 저장). rgb는 위 행부터, 픽셀당 R,G,B.
void WriteBmp(const fs::path& path, int w, int h, const std::vector<uint8_t>& rgb)
{
    int rowSize = (w * 3 + 3) & ~3;
    int dataSize = rowSize * h;
    std::vector<char> b(54 + dataSize, 0);
    auto put32 = [&](int off, uint32_t v) { std::memcpy(&b[off], &v, 4); };
    b[0] = 'B'; b[1] = 'M';
    put32(2, 54 + dataSize); put32(10, 54); put32(14, 40);
    put32(18, w); put32(22, h);
    b[26] = 1; b[28] = 24;
    put32(34, dataSize);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t* src = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            char* dst = &b[54 + (h - 1 - y) * rowSize + x * 3];
            dst[0] = static_cast<char>(src[2]); dst[1] = static_cast<char>(src[1]); dst[2] = static_cast<char>(src[0]);
        }
    WriteBytes(path, b);
}

// ======== 렌더 코어 ========
void Test_CreateRejectsInvalidSize()
{
    CHECK(gge_renderer_create(0, 100, 1) == nullptr);
    CHECK(std::strlen(gge_last_error()) > 0);
}

void Test_EmptySceneIsUniformBackground()
{
    GgeRenderer* r = NewRenderer();
    CHECK(r != nullptr);
    Image img = Render(r);
    CHECK(img.width == 160 && img.height == 120);
    std::set<uint32_t> colors;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) colors.insert(img.At(x, y));
    CHECK(colors.size() == 1);
    gge_renderer_destroy(r);
}

void Test_CubeIsVisibleAndFlatShaded()
{
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    Image img = Render(r);
    CHECK(img.At(img.width / 2, img.height / 2) != img.At(0, 0));
    // 코너 뷰에서는 배경 + 보이는 3개 면 = 서로 다른 단색 4개여야 한다(플랫 셰이딩).
    std::set<uint32_t> colors;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) colors.insert(img.At(x, y));
    CHECK(colors.size() == 4);
    gge_renderer_destroy(r);
}

void Test_RenderIsDeterministic()
{
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    Image a = Render(r);
    Image b = Render(r);
    CHECK(a.bgra == b.bgra);
    gge_renderer_destroy(r);
}

void Test_CameraMoveChangesImage()
{
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    Image a = Render(r);
    gge_renderer_set_camera(r, -4.0f, 3.0f, 5.0f, 0, 0, 0, 0.8f);
    CHECK(a.bgra != Render(r).bgra);
    gge_renderer_destroy(r);
}

void Test_DepthOrderNearFaceWins()
{
    // 정면에서 본 큐브의 중앙 색은 +Z 면의 색(밝기: 0.25+0.75*max(0,-L·n))이어야 한다.
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    gge_renderer_set_camera(r, 0, 0, 6, 0, 0, 0, 0.8f);
    Image img = Render(r);
    float lightLen = std::sqrt(0.4f * 0.4f + 1.0f + 0.6f * 0.6f);
    float shade = 0.25f + 0.75f * (0.6f / lightLen);
    int expectedRed = static_cast<int>(std::lround(0.9f * shade * 255.0f));
    int actualRed = Red(img.At(img.width / 2, img.height / 2));
    if (std::abs(actualRed - expectedRed) > 2)
        std::printf("  expectedRed=%d actualRed=%d\n", expectedRed, actualRed);
    CHECK(std::abs(actualRed - expectedRed) <= 2);
    gge_renderer_destroy(r);
}

void Test_MeshCreateRejectsBadInput()
{
    GgeRenderer* r = NewRenderer();
    float v[8] = {0, 0, 0, 0, 0, 1, 0, 0};
    uint32_t outOfRange[3] = {0, 1, 2};
    CHECK(gge_mesh_create(r, v, 1, outOfRange, 3) == 0);
    CHECK(std::strlen(gge_last_error()) > 0);
    CHECK(gge_mesh_create(r, v, 1, outOfRange, 2) == 0);  // 인덱스 수가 3의 배수가 아님
    CHECK(gge_mesh_create(r, nullptr, 1, outOfRange, 3) == 0);
    gge_renderer_destroy(r);
}

void Test_LoadObj()
{
    fs::path dir = TempDir();
    // 쿼드(사각형 폴리곤) + v/vt/vn 형식을 포함한 OBJ.
    fs::path good = dir / "quad.obj";
    WriteText(good, "# quad facing +Z\n"
                    "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
                    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                    "f 1/1/1 2/2/1 3/3/1 4/4/1\n");
    GgeRenderer* r = NewRenderer();
    int mesh = gge_mesh_load_obj(r, good.string().c_str());
    CHECK(mesh > 0);
    CHECK(gge_object_create(r, mesh, 0, 0) > 0);
    gge_renderer_set_camera(r, 0, 0, 4, 0, 0, 0, 0.8f);
    Image img = Render(r);
    CHECK(img.At(img.width / 2, img.height / 2) != img.At(0, 0));

    fs::path negative = dir / "neg.obj";
    WriteText(negative, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n");
    CHECK(gge_mesh_load_obj(r, negative.string().c_str()) > 0);

    fs::path badIndex = dir / "bad.obj";
    WriteText(badIndex, "v 0 0 0\nf 1 2 3\n");
    CHECK(gge_mesh_load_obj(r, badIndex.string().c_str()) == 0);
    CHECK(std::string(gge_last_error()).find("range") != std::string::npos);

    fs::path badUv = dir / "baduv.obj";
    WriteText(badUv, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/5 2/5 3/5\n");
    CHECK(gge_mesh_load_obj(r, badUv.string().c_str()) == 0);

    CHECK(gge_mesh_load_obj(r, (dir / "missing.obj").string().c_str()) == 0);
    CHECK(gge_mesh_load_obj(r, nullptr) == 0);
    gge_renderer_destroy(r);
}

void Test_GoldenCube()
{
    GgeRenderer* r = NewRenderer(320, 240);
    SetupCubeScene(r);
    CHECK(MatchesGolden("cube_320x240", Render(r)));
    gge_renderer_destroy(r);
}

// ======== 레트로(PS1풍) 효과 ========
std::set<int> ChannelValues(const Image& img, int channel, int x0 = 0, int y0 = 0, int x1 = -1, int y1 = -1)
{
    if (x1 < 0) x1 = img.width;
    if (y1 < 0) y1 = img.height;
    std::set<int> values;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) values.insert(img.bgra[(static_cast<size_t>(y) * img.width + x) * 4 + channel]);
    return values;
}

void Test_RetroRejectsBadColorBits()
{
    GgeRenderer* r = NewRenderer();
    CHECK(gge_renderer_set_retro(r, 0, 0, 0, 0) == 0);
    CHECK(gge_renderer_set_retro(r, 0, 9, 0, 0) == 0);
    CHECK(std::strlen(gge_last_error()) > 0);
    CHECK(gge_renderer_set_retro(r, 0, 8, 0, 0) == 1);
    gge_renderer_destroy(r);
}

void Test_RetroDefaultsLeaveImageUnchanged()
{
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    Image before = Render(r);
    CHECK(gge_renderer_set_retro(r, 0, 8, 0, 0) == 1);
    CHECK(before.bgra == Render(r).bgra);
    gge_renderer_destroy(r);
}

void Test_ColorBitsQuantizeChannels()
{
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);

    CHECK(gge_renderer_set_retro(r, 0, 1, 0, 0) == 1);
    Image one = Render(r);
    for (int ch = 0; ch < 3; ++ch)
        for (int v : ChannelValues(one, ch)) CHECK(v == 0 || v == 255);

    CHECK(gge_renderer_set_retro(r, 0, 5, 0, 0) == 1);  // 15비트 컬러
    Image five = Render(r);
    std::set<int> allowed;
    for (int k = 0; k < 32; ++k) allowed.insert(static_cast<int>(std::lround(k * 255.0 / 31.0)));
    for (int ch = 0; ch < 3; ++ch)
        for (int v : ChannelValues(five, ch)) CHECK(allowed.count(v) == 1);
    gge_renderer_destroy(r);
}

void Test_DitherMixesAdjacentLevels()
{
    // 정면 큐브의 +Z 면은 단색(R≈0.5414). 2비트(레벨 0,85,170,255)에서 디더 없이는 한 색, 디더가 있으면 인접한 두 레벨이 섞인다.
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    gge_renderer_set_camera(r, 0, 0, 6, 0, 0, 0, 0.8f);
    const int cx = 70, cy = 50, size = 20;

    CHECK(gge_renderer_set_retro(r, 0, 2, 0, 0) == 1);
    CHECK(ChannelValues(Render(r), 2, cx, cy, cx + size, cy + size).size() == 1);

    CHECK(gge_renderer_set_retro(r, 0, 2, 1, 0) == 1);
    CHECK(ChannelValues(Render(r), 2, cx, cy, cx + size, cy + size) == std::set<int>({85, 170}));
    gge_renderer_destroy(r);
}

void Test_VertexSnapRemovesSubpixelJitter()
{
    // 카메라를 서브픽셀만큼 움직였을 때, 스냅이 꺼져 있으면 가장자리가 변하고 켜져 있으면 정점이 같은 픽셀 격자점에 머문다.
    GgeRenderer* r = NewRenderer(80, 60);
    SetupCubeScene(r);
    auto render = [&](float eyeX) {
        gge_renderer_set_camera(r, eyeX, 3.0f, 5.0f, 0, 0, 0, 0.8f);
        return Render(r);
    };

    CHECK(gge_renderer_set_retro(r, 0, 8, 0, 0) == 1);
    CHECK(render(4.0f).bgra != render(4.01f).bgra);

    CHECK(gge_renderer_set_retro(r, 1, 8, 0, 0) == 1);
    CHECK(render(4.0f).bgra == render(4.01f).bgra);

    // 충분히 움직이면 스냅이 켜져 있어도 화면은 변해야 한다(고정된 것이 아님).
    CHECK(render(4.0f).bgra != render(4.8f).bgra);
    gge_renderer_destroy(r);
}

void Test_GoldenCubeRetro()
{
    GgeRenderer* r = NewRenderer(320, 240);
    SetupCubeScene(r);
    CHECK(gge_renderer_set_retro(r, 1, 5, 1, 0) == 1);
    CHECK(MatchesGolden("cube_retro_320x240", Render(r)));
    gge_renderer_destroy(r);
}

// ======== 4a: UV / 텍스처 / 아핀 / 메시 포맷 ========
struct TexturedQuad {
    GgeRenderer* r;
    int object;
};

TexturedQuad MakeTexturedQuad(int texture)
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, QuadMesh(r), texture, 0);
    CHECK(obj > 0);
    gge_object_set_color(r, obj, 1, 1, 1);
    gge_renderer_set_camera(r, 0, 0, 4, 0, 0, 0, 0.8f);
    return {r, obj};
}

// 쿼드 네 사분면(화면 기준)의 지배 채널을 검사한다: 좌상 빨강, 우상 초록, 좌하 파랑, 우하 흰색.
void CheckQuadQuadrants(const Image& img)
{
    uint32_t tl = img.At(62, 42), tr = img.At(98, 42), bl = img.At(62, 78), br = img.At(98, 78);
    CHECK(Red(tl) > 100 && Green(tl) < 20 && Blue(tl) < 20);
    CHECK(Green(tr) > 100 && Red(tr) < 20 && Blue(tr) < 20);
    CHECK(Blue(bl) > 100 && Red(bl) < 20 && Green(bl) < 20);
    CHECK(Red(br) > 100 && Green(br) > 100 && Blue(br) > 100);
}

void Test_TextureCreateRejectsBadInput()
{
    GgeRenderer* r = NewRenderer();
    CHECK(gge_texture_create(r, nullptr, 2, 2) == 0);
    CHECK(gge_texture_create(r, kQuadTexture, 0, 2) == 0);
    CHECK(gge_texture_create(r, kQuadTexture, 100000, 2) == 0);
    CHECK(gge_texture_create(r, kQuadTexture, 2, 2) > 0);
    gge_renderer_destroy(r);
}

void Test_TextureIsUprightAndPointSampled()
{
    GgeRenderer* r = NewRenderer();
    int tex = gge_texture_create(r, kQuadTexture, 2, 2);
    int obj = gge_object_create(r, QuadMesh(r), tex, 0);
    gge_object_set_color(r, obj, 1, 1, 1);
    gge_renderer_set_camera(r, 0, 0, 4, 0, 0, 0, 0.8f);
    Image img = Render(r);
    CheckQuadQuadrants(img);

    // 점 샘플링: 필터링 없이 텍셀 4개 + 배경만 나와야 한다(2x2 텍스처를 확대해도 블렌딩 색이 없음).
    std::set<uint32_t> colors;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) colors.insert(img.At(x, y));
    CHECK(colors.size() <= 5);
    gge_renderer_destroy(r);
}

void Test_ObjUvIsUpright()
{
    // OBJ의 v는 아래가 0이므로, 같은 텍스처를 입힌 OBJ 쿼드도 위가 빨강/초록으로 보여야 한다.
    fs::path obj = TempDir() / "uvquad.obj";
    WriteText(obj, "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
                   "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                   "f 1/1 2/2 3/3 4/4\n");
    GgeRenderer* r = NewRenderer();
    int tex = gge_texture_create(r, kQuadTexture, 2, 2);
    int mesh = gge_mesh_load_obj(r, obj.string().c_str());
    int o = gge_object_create(r, mesh, tex, 0);
    gge_object_set_color(r, o, 1, 1, 1);
    gge_renderer_set_camera(r, 0, 0, 4, 0, 0, 0, 0.8f);
    CheckQuadQuadrants(Render(r));
    gge_renderer_destroy(r);
}

void Test_TextureLoadFromBmp()
{
    fs::path bmp = TempDir() / "quad.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer();
    int tex = gge_texture_load(r, bmp.string().c_str());
    CHECK(tex > 0);
    int o = gge_object_create(r, QuadMesh(r), tex, 0);
    gge_object_set_color(r, o, 1, 1, 1);
    gge_renderer_set_camera(r, 0, 0, 4, 0, 0, 0, 0.8f);
    CheckQuadQuadrants(Render(r));

    fs::path notImage = TempDir() / "notimage.bmp";
    WriteText(notImage, "this is not an image");
    CHECK(gge_texture_load(r, notImage.string().c_str()) == 0);
    CHECK(std::strlen(gge_last_error()) > 0);
    CHECK(gge_texture_load(r, (TempDir() / "missing.png").string().c_str()) == 0);
    CHECK(gge_texture_load(r, nullptr) == 0);
    gge_renderer_destroy(r);
}

// ---- 로더 견고성 (리뷰에서 확인된 결함의 재현) ----
void Test_ObjRejectsMalformedVertexLines()
{
    fs::path dir = TempDir();
    GgeRenderer* r = NewRenderer();
    // 좌표가 모자란 v 줄은 초기화되지 않은 값이 정점이 되면 안 된다: 오류로 거부해야 한다.
    fs::path shortVertex = dir / "short_vertex.obj";
    WriteText(shortVertex, "v 1.0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    CHECK(gge_mesh_load_obj(r, shortVertex.string().c_str()) == 0);
    CHECK(std::string(gge_last_error()).find("vertex") != std::string::npos);

    fs::path textVertex = dir / "text_vertex.obj";
    WriteText(textVertex, "v 0 abc 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    CHECK(gge_mesh_load_obj(r, textVertex.string().c_str()) == 0);

    fs::path badTexcoord = dir / "bad_vt.obj";
    WriteText(badTexcoord, "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt abc\nf 1/1 2/1 3/1\n");
    CHECK(gge_mesh_load_obj(r, badTexcoord.string().c_str()) == 0);

    // v 좌표가 없는 vt(vt u)는 OBJ 규격상 유효하다. 선택 성분(v, w)은 생략 가능하다.
    fs::path uOnly = dir / "vt_u_only.obj";
    WriteText(uOnly, "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.5\nf 1/1 2/1 3/1\n");
    CHECK(gge_mesh_load_obj(r, uOnly.string().c_str()) > 0);

    // 숫자가 아닌 면 인덱스는 불투명한 stoi 메시지가 아니라 OBJ 오류로 설명되어야 한다.
    fs::path badFace = dir / "bad_face.obj";
    WriteText(badFace, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 x 3\n");
    CHECK(gge_mesh_load_obj(r, badFace.string().c_str()) == 0);
    CHECK(std::string(gge_last_error()).find("OBJ") != std::string::npos);
    gge_renderer_destroy(r);
}

void Test_HugeImageIsRejectedBeforeDecoding()
{
    // 유효한 1비트 8192x8192 BMP(파일은 약 8MB지만 RGBA로 풀면 약 268MB). 한도(4096)를 넘으므로 픽셀 버퍼를
    // 할당하기 전에 크기만 보고 거부해야 한다. (이전에는 전부 디코딩한 뒤에야 렌더러가 거부했다.)
    constexpr uint32_t kSide = 8192, kRowBytes = ((kSide + 31) / 32) * 4, kPixelBytes = kRowBytes * kSide;
    constexpr uint32_t kPixelOffset = 14 + 40 + 8;  // 파일 헤더 + 정보 헤더 + 팔레트 2개
    std::vector<uint8_t> header(kPixelOffset, 0);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&header[at], &v, 4); };
    header[0] = 'B'; header[1] = 'M';
    put32(2, kPixelOffset + kPixelBytes); put32(10, kPixelOffset); put32(14, 40);
    put32(18, kSide); put32(22, kSide);
    header[26] = 1; header[28] = 1;  // 평면 1, 1비트/픽셀
    put32(34, kPixelBytes); put32(46, 2);
    header[58] = header[59] = header[60] = 255;  // 팔레트[1] = 흰색 (팔레트[0]은 검정)
    fs::path bmp = TempDir() / "huge.bmp";
    {
        std::ofstream f(bmp, std::ios::binary);
        f.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        std::vector<char> zeros(kRowBytes, 0);
        for (uint32_t row = 0; row < kSide; ++row) {
            f.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
        }
    }
    GgeRenderer* r = NewRenderer();
    CHECK(gge_texture_load(r, bmp.string().c_str()) == 0);
    CHECK(std::string(gge_last_error()).find("too large") != std::string::npos);
    gge_renderer_destroy(r);
    fs::remove(bmp);  // 8MB짜리 임시 파일은 남기지 않는다
}

void Test_AffineMatchesPerspectiveWhenFacingCamera()
{
    // 카메라와 정면인 평면은 원근 보정 여부와 무관하게 UV가 선형이므로 결과가 같아야 한다.
    TexturedQuad q = MakeTexturedQuad(0);
    int tex = gge_texture_create(q.r, kQuadTexture, 2, 2);
    gge_object_set_texture(q.r, q.object, tex);
    CHECK(gge_renderer_set_retro(q.r, 0, 8, 0, 0) == 1);
    Image persp = Render(q.r);
    CHECK(gge_renderer_set_retro(q.r, 0, 8, 0, 1) == 1);
    CHECK(persp.bgra == Render(q.r).bgra);
    gge_renderer_destroy(q.r);
}

// 바닥 평면 + 8x8 체크 텍스처를 낮은 각도로 본다.
GgeRenderer* MakeFloorScene(int width, int height)
{
    GgeRenderer* r = NewRenderer(width, height);
    uint8_t checker[8 * 8 * 4];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            bool dark = ((x + y) & 1) != 0;
            uint8_t* p = &checker[(y * 8 + x) * 4];
            p[0] = dark ? 40 : 230; p[1] = dark ? 60 : 200; p[2] = dark ? 140 : 60; p[3] = 255;
        }
    int tex = gge_texture_create(r, checker, 8, 8);
    int obj = gge_object_create(r, FloorMesh(r, 4.0f, 2.0f), tex, 0);
    gge_object_set_color(r, obj, 1, 1, 1);
    gge_renderer_set_camera(r, 0, 1.2f, 4.5f, 0, 0, 0, 0.9f);
    return r;
}

void Test_AffineDistortsReceedingPlane()
{
    GgeRenderer* r = MakeFloorScene(160, 120);
    CHECK(gge_renderer_set_retro(r, 0, 8, 0, 0) == 1);
    Image persp = Render(r);
    CHECK(gge_renderer_set_retro(r, 0, 8, 0, 1) == 1);
    Image affine = Render(r);
    CHECK(persp.bgra != affine.bgra);
    gge_renderer_destroy(r);
}

void Test_GoldenFloorAffine()
{
    GgeRenderer* r = MakeFloorScene(320, 240);
    CHECK(gge_renderer_set_retro(r, 1, 5, 1, 1) == 1);
    CHECK(MatchesGolden("floor_affine_320x240", Render(r)));
    gge_renderer_destroy(r);
}

void Test_GgmRoundTripRendersIdentically()
{
    fs::path dir = TempDir();
    fs::path obj = dir / "ggm_src.obj";
    fs::path ggm = dir / "ggm_out.ggm";
    WriteText(obj, "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nv -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
                   "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                   "f 1/1 2/2 3/3 4/4\nf 6/1 5/2 8/3 7/4\nf 2/1 6/2 7/3 3/4\n"
                   "f 5/1 1/2 4/3 8/4\nf 4/1 3/2 7/3 8/4\nf 5/1 6/2 2/3 1/4\n");
    CHECK(gge_convert_obj_to_ggm(obj.string().c_str(), ggm.string().c_str()) == 1);

    auto renderWith = [&](bool useGgm) {
        GgeRenderer* r = NewRenderer();
        int tex = gge_texture_create(r, kQuadTexture, 2, 2);
        int mesh = useGgm ? gge_mesh_load_ggm(r, ggm.string().c_str()) : gge_mesh_load_obj(r, obj.string().c_str());
        CHECK(mesh > 0);
        int o = gge_object_create(r, mesh, tex, 0);
        gge_object_set_color(r, o, 1, 1, 1);
        gge_renderer_set_camera(r, 4, 3, 5, 0, 0, 0, 0.8f);
        Image img = Render(r);
        gge_renderer_destroy(r);
        return img;
    };
    CHECK(renderWith(true).bgra == renderWith(false).bgra);
}

void Test_GgmRejectsCorruptFiles()
{
    fs::path dir = TempDir();
    fs::path obj = dir / "corrupt_src.obj";
    fs::path good = dir / "corrupt_good.ggm";
    WriteText(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    CHECK(gge_convert_obj_to_ggm(obj.string().c_str(), good.string().c_str()) == 1);
    std::vector<char> bytes = ReadBytes(good);
    CHECK(bytes.size() == 12 + 3 * 8 * 4 + 3 * 4);

    GgeRenderer* r = NewRenderer();
    CHECK(gge_mesh_load_ggm(r, good.string().c_str()) > 0);

    auto expectRejected = [&](const char* name, std::vector<char> mutated) {
        fs::path p = dir / name;
        WriteBytes(p, mutated);
        CHECK(gge_mesh_load_ggm(r, p.string().c_str()) == 0);
        CHECK(std::strlen(gge_last_error()) > 0);
    };

    std::vector<char> badMagic = bytes;
    badMagic[0] = 'X';
    expectRejected("bad_magic.ggm", badMagic);

    expectRejected("truncated.ggm", std::vector<char>(bytes.begin(), bytes.end() - 4));
    expectRejected("tiny.ggm", std::vector<char>(bytes.begin(), bytes.begin() + 5));

    std::vector<char> extra = bytes;
    extra.push_back(0);
    expectRejected("trailing.ggm", extra);

    std::vector<char> badIndex = bytes;
    uint32_t big = 99;
    std::memcpy(&badIndex[badIndex.size() - 4], &big, 4);
    expectRejected("bad_index.ggm", badIndex);

    std::vector<char> hugeCount = bytes;
    uint32_t huge = 0xFFFFFFF0u;
    std::memcpy(&hugeCount[4], &huge, 4);
    expectRejected("huge_count.ggm", hugeCount);

    CHECK(gge_mesh_load_ggm(r, (dir / "missing.ggm").string().c_str()) == 0);
    CHECK(gge_convert_obj_to_ggm((dir / "missing.obj").string().c_str(), good.string().c_str()) == 0);
    gge_renderer_destroy(r);
}

// ======== 4b: 씬 그래프 ========
// 큐브 하나를 월드 (x,y,z)/scale/yaw에 직접 놓은 "평평한" 씬의 이미지.
Image FlatCubes(const std::vector<std::array<float, 5>>& placements)  // {x, y, z, scale, yaw}
{
    GgeRenderer* r = NewRenderer();
    int mesh = CubeMesh(r);
    for (const auto& p : placements) {
        int o = gge_object_create(r, mesh, 0, 0);
        gge_object_set_color(r, o, 0.9f, 0.6f, 0.3f);
        Place(r, o, p[0], p[1], p[2], p[3], p[4]);
    }
    gge_renderer_set_camera(r, 6, 5, 9, 0, 0, 0, 0.8f);
    Image img = Render(r);
    gge_renderer_destroy(r);
    return img;
}

void Test_ChildFollowsParentTransform()
{
    GgeRenderer* r = NewRenderer();
    int mesh = CubeMesh(r);
    int group = gge_object_create(r, 0, 0, 0);  // 그룹 노드(그려지지 않음)
    int child = gge_object_create(r, mesh, 0, group);
    CHECK(group > 0 && child > 0);
    gge_object_set_color(r, child, 0.9f, 0.6f, 0.3f);
    gge_renderer_set_camera(r, 6, 5, 9, 0, 0, 0, 0.8f);

    // 부모 이동만: 자식 로컬 (0,0,0) → 월드 (1,0,0)
    Place(r, group, 1, 0, 0);
    CHECK(Render(r).bgra == FlatCubes({{1, 0, 0, 1, 0}}).bgra);

    // 부모 스케일 2 + 자식 로컬 이동(1,0,0) → 월드 (2,0,0), 크기 2
    Place(r, group, 0, 0, 0, 2.0f);
    Place(r, child, 1, 0, 0);
    CHECK(Render(r).bgra == FlatCubes({{2, 0, 0, 2, 0}}).bgra);

    // 부모 yaw +90°: 로컬 (2,0,0) → 월드 (0,0,-2) (오른손, XM 회전 규약)
    Place(r, group, 0, 0, 0, 1.0f, 3.14159265f / 2);
    Place(r, child, 2, 0, 0);
    Image rotated = Render(r);
    Image expected = FlatCubes({{0, 0, -2, 1, 3.14159265f / 2}});
    int diff = 0;
    for (size_t i = 0; i < rotated.bgra.size(); ++i)
        if (std::abs(int(rotated.bgra[i]) - int(expected.bgra[i])) > 2) ++diff;
    CHECK(diff == 0);
    gge_renderer_destroy(r);
}

void Test_NonUniformScaleKeepsLighting()
{
    // 법선은 역전치 행렬로 변환되므로 +Z 면의 밝기는 X로 늘려도 같아야 한다.
    auto centerColor = [](float sx) {
        GgeRenderer* r = NewRenderer();
        int o = gge_object_create(r, CubeMesh(r), 0, 0);
        gge_object_set_color(r, o, 0.9f, 0.6f, 0.3f);
        const float p[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, s[3] = {sx, 1, 1};
        gge_object_set_transform(r, o, p, rot, s);
        gge_renderer_set_camera(r, 0, 0, 12, 0, 0, 0, 0.8f);
        Image img = Render(r);
        gge_renderer_destroy(r);
        return img.At(img.width / 2, img.height / 2);
    };
    CHECK(centerColor(1.0f) == centerColor(3.0f));
}

void Test_DestroyObjectRemovesDescendants()
{
    GgeRenderer* r = NewRenderer();
    int mesh = CubeMesh(r);
    int parent = gge_object_create(r, mesh, 0, 0);
    int child = gge_object_create(r, mesh, 0, parent);
    int grandchild = gge_object_create(r, mesh, 0, child);
    Place(r, child, 2, 0, 0);
    Place(r, grandchild, 2, 0, 0);
    gge_renderer_set_camera(r, 6, 5, 9, 0, 0, 0, 0.8f);
    Image withAll = Render(r);

    CHECK(gge_object_destroy(r, parent) == 1);
    Image empty = Render(r);
    CHECK(withAll.bgra != empty.bgra);
    std::set<uint32_t> colors;
    for (int y = 0; y < empty.height; ++y)
        for (int x = 0; x < empty.width; ++x) colors.insert(empty.At(x, y));
    CHECK(colors.size() == 1);

    CHECK(gge_object_set_color(r, child, 1, 1, 1) == 0);  // 자손 ID도 무효
    CHECK(gge_object_destroy(r, parent) == 0);
    CHECK(gge_object_destroy(r, 12345) == 0);
    gge_renderer_destroy(r);
}

void Test_SetParentRejectsCyclesAndUnknownIds()
{
    GgeRenderer* r = NewRenderer();
    int mesh = CubeMesh(r);
    int a = gge_object_create(r, mesh, 0, 0);
    int b = gge_object_create(r, mesh, 0, a);
    int c = gge_object_create(r, mesh, 0, b);

    CHECK(gge_object_set_parent(r, a, c) == 0);  // a → b → c → a 순환
    CHECK(std::string(gge_last_error()).find("cycle") != std::string::npos);
    CHECK(gge_object_set_parent(r, a, a) == 0);  // 자기 자신
    CHECK(gge_object_set_parent(r, a, 999) == 0);
    CHECK(gge_object_set_parent(r, 999, a) == 0);
    CHECK(gge_object_set_parent(r, c, a) == 1);  // 재부모화는 허용
    CHECK(gge_object_set_parent(r, c, 0) == 1);  // 루트로

    CHECK(gge_object_create(r, 999, 0, 0) == 0);  // 없는 메시
    CHECK(gge_object_create(r, mesh, 999, 0) == 0);  // 없는 텍스처
    CHECK(gge_object_create(r, mesh, 0, 999) == 0);  // 없는 부모
    CHECK(gge_object_set_mesh(r, a, 999) == 0);
    CHECK(gge_object_set_texture(r, a, 999) == 0);
    const float v3[3] = {0, 0, 0};
    CHECK(gge_object_set_transform(r, a, nullptr, v3, v3) == 0);
    gge_renderer_destroy(r);
}

void Test_ObjectColorAndMeshSwap()
{
    GgeRenderer* r = NewRenderer();
    int cube = CubeMesh(r);
    int quad = QuadMesh(r);
    int o = gge_object_create(r, cube, 0, 0);
    gge_renderer_set_camera(r, 0, 0, 6, 0, 0, 0, 0.8f);
    gge_object_set_color(r, o, 1, 0, 0);
    uint32_t red = Render(r).At(80, 60);
    CHECK(Red(red) > 0 && Green(red) == 0 && Blue(red) == 0);
    gge_object_set_color(r, o, 0, 0, 1);
    uint32_t blue = Render(r).At(80, 60);
    CHECK(Blue(blue) > 0 && Red(blue) == 0);
    CHECK(gge_object_set_mesh(r, o, quad) == 1);
    CHECK(Render(r).At(80, 60) != Render(r).At(0, 0));
    gge_renderer_destroy(r);
}

void Test_GoldenSceneHierarchy()
{
    // 부모-자식 계층: 큐브 위에 회전하는 작은 큐브들이 매달린 장면.
    GgeRenderer* r = NewRenderer(320, 240);
    int mesh = CubeMesh(r);
    int root = gge_object_create(r, mesh, 0, 0);
    gge_object_set_color(r, root, 0.9f, 0.6f, 0.3f);
    int arm = gge_object_create(r, 0, 0, root);
    Place(r, arm, 0, 1.6f, 0, 1.0f, 0.7f);
    for (int i = 0; i < 3; ++i) {
        int c = gge_object_create(r, mesh, 0, arm);
        gge_object_set_color(r, c, 0.3f + 0.3f * i, 0.8f - 0.2f * i, 0.5f);
        Place(r, c, 1.8f * (i - 1), 0.2f * i, 0, 0.4f);
    }
    gge_renderer_set_camera(r, 5, 4, 7, 0, 0.8f, 0, 0.8f);
    gge_renderer_set_retro(r, 1, 5, 1, 0);
    CHECK(MatchesGolden("scene_hierarchy_320x240", Render(r)));
    gge_renderer_destroy(r);
}

// ======== 5: 스켈레탈 애니메이션 ========
constexpr float kPi = 3.14159265358979f;

// 본 N개 체인: 본 i의 부모는 i-1, 레스트 로컬 이동 (0, i>0 ? 1 : 0, 0), 역바인드 = 이동(0,-i,0).
struct ChainData {
    std::vector<int> parents;
    std::vector<float> inverseBind, translation, rotation, scale;
};

ChainData MakeChain(int bones)
{
    ChainData c;
    for (int i = 0; i < bones; ++i) {
        c.parents.push_back(i - 1);
        float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, static_cast<float>(-i), 0, 1};
        c.inverseBind.insert(c.inverseBind.end(), m, m + 16);
        c.translation.insert(c.translation.end(), {0.0f, i > 0 ? 1.0f : 0.0f, 0.0f});
        c.rotation.insert(c.rotation.end(), {0, 0, 0, 1});
        c.scale.insert(c.scale.end(), {1, 1, 1});
    }
    return c;
}

int CreateSkeleton(GgeRenderer* r, const ChainData& c)
{
    return gge_skeleton_create(r, static_cast<int>(c.parents.size()), c.parents.data(), c.inverseBind.data(),
                               c.translation.data(), c.rotation.data(), c.scale.data());
}

int ChainSkeleton(GgeRenderer* r, int bones = 3)
{
    int id = CreateSkeleton(r, MakeChain(bones));
    CHECK(id > 0);
    return id;
}

struct KeyChannel {
    int bone;
    int path;
    std::vector<float> times, values;
};

int CreateClip(GgeRenderer* r, float duration, const std::vector<KeyChannel>& channels)
{
    std::vector<GgeClipChannel> raw;
    for (const KeyChannel& c : channels)
        raw.push_back({c.bone, c.path, static_cast<int>(c.times.size()), c.times.data(), c.values.data()});
    return gge_clip_create(r, duration, static_cast<int>(raw.size()), raw.data());
}

// 본 하나를 Z축으로 a0→a1(라디안) 회전시키는 1초짜리 클립.
KeyChannel ZRotation(int bone, float a0, float a1)
{
    return {bone, 1, {0, 1}, {0, 0, std::sin(a0 / 2), std::cos(a0 / 2), 0, 0, std::sin(a1 / 2), std::cos(a1 / 2)}};
}

int RotationClip(GgeRenderer* r, float a0, float a1)
{
    int id = CreateClip(r, 1.0f, {ZRotation(1, a0, a1)});
    CHECK(id > 0);
    return id;
}

struct V3 { float x, y, z; };

std::vector<V3> BonePositions(GgeRenderer* r, int object, int bones)
{
    std::vector<float> m(bones * 16);
    int written = gge_object_get_pose(r, object, m.data(), bones);
    CHECK(written == bones);
    std::vector<V3> out;
    for (int i = 0; i < bones; ++i)
        out.push_back({m[i * 16 + 12], m[i * 16 + 13], m[i * 16 + 14]});
    return out;
}

bool Near(V3 a, V3 b, float eps = 1e-4f)
{
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

// 높이 y=0,1,2의 3행 x 2열 띠. 행0은 본0, 행1·행2는 본1에 100% 묶인다. 앞면은 +Z.
int SkinnedStrip(GgeRenderer* r)
{
    const float v[] = {
        -0.3f, 0, 0, 0, 0, 1, 0, 1,   0.3f, 0, 0, 0, 0, 1, 1, 1,
        -0.3f, 1, 0, 0, 0, 1, 0, 0.5f, 0.3f, 1, 0, 0, 0, 1, 1, 0.5f,
        -0.3f, 2, 0, 0, 0, 1, 0, 0,   0.3f, 2, 0, 0, 0, 1, 1, 0,
    };
    const uint32_t joints[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const float weights[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const uint32_t idx[] = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
    int id = gge_mesh_create_skinned(r, v, 6, joints, weights, idx, 12);
    CHECK(id > 0);
    return id;
}

void Test_SkeletonRejectsBadInput()
{
    GgeRenderer* r = NewRenderer();
    ChainData ok = MakeChain(3);
    CHECK(CreateSkeleton(r, ok) > 0);

    CHECK(gge_skeleton_create(r, 0, ok.parents.data(), ok.inverseBind.data(), ok.translation.data(),
                              ok.rotation.data(), ok.scale.data()) == 0);
    CHECK(gge_skeleton_create(r, 65, ok.parents.data(), ok.inverseBind.data(), ok.translation.data(),
                              ok.rotation.data(), ok.scale.data()) == 0);
    CHECK(gge_skeleton_create(r, 3, nullptr, ok.inverseBind.data(), ok.translation.data(), ok.rotation.data(),
                              ok.scale.data()) == 0);

    ChainData selfParent = ok;
    selfParent.parents[1] = 1;
    CHECK(CreateSkeleton(r, selfParent) == 0);
    ChainData laterParent = ok;
    laterParent.parents[0] = 2;  // 부모가 자식보다 뒤에 옴
    CHECK(CreateSkeleton(r, laterParent) == 0);
    ChainData badParent = ok;
    badParent.parents[2] = -2;
    CHECK(CreateSkeleton(r, badParent) == 0);
    CHECK(std::strlen(gge_last_error()) > 0);

    ChainData nan = ok;
    nan.translation[4] = std::nanf("");
    CHECK(CreateSkeleton(r, nan) == 0);
    ChainData nanMatrix = ok;
    nanMatrix.inverseBind[5] = INFINITY;
    CHECK(CreateSkeleton(r, nanMatrix) == 0);
    ChainData zeroQuat = ok;
    zeroQuat.rotation[4] = zeroQuat.rotation[5] = zeroQuat.rotation[6] = zeroQuat.rotation[7] = 0;
    CHECK(CreateSkeleton(r, zeroQuat) == 0);

    CHECK(CreateSkeleton(r, MakeChain(64)) > 0);  // 상한은 허용
    gge_renderer_destroy(r);
}

void Test_ClipRejectsBadInput()
{
    GgeRenderer* r = NewRenderer();
    CHECK(RotationClip(r, 0, 1) > 0);
    CHECK(gge_clip_create(r, 1.0f, 0, nullptr) == 0);

    const KeyChannel good = ZRotation(1, 0, 1);
    auto expectRejected = [&](KeyChannel c, float duration = 1.0f) {
        CHECK(CreateClip(r, duration, {c}) == 0);
        CHECK(std::strlen(gge_last_error()) > 0);
    };
    KeyChannel c = good;
    c.times = {0.5f, 0.5f};  expectRejected(c);               // 증가하지 않음
    c = good; c.times = {1.0f, 0.0f};  expectRejected(c);     // 감소
    c = good; c.times = {0.0f, 2.0f};  expectRejected(c);     // duration 초과
    c = good; c.times = {-0.5f, 1.0f}; expectRejected(c);     // 음수 시간
    c = good; c.bone = -1;             expectRejected(c);
    c = good; c.path = 7;              expectRejected(c);
    c = good; c.values[2] = std::nanf(""); expectRejected(c);
    c = good; c.values[0] = c.values[1] = c.values[2] = c.values[3] = 0; expectRejected(c);  // 0 쿼터니언
    c = good; c.times.clear(); c.values.clear(); expectRejected(c);
    expectRejected(good, -1.0f);
    gge_renderer_destroy(r);
}

void Test_RestPoseMatchesBindChain()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    CHECK(gge_object_set_skeleton(r, obj, ChainSkeleton(r)) == 1);
    std::vector<V3> pos = BonePositions(r, obj, 3);
    CHECK(Near(pos[0], {0, 0, 0}) && Near(pos[1], {0, 1, 0}) && Near(pos[2], {0, 2, 0}));

    float tooSmall[16];
    CHECK(gge_object_get_pose(r, obj, tooSmall, 1) == 0);
    CHECK(std::string(gge_last_error()).find("too small") != std::string::npos);
    CHECK(gge_object_get_pose(r, obj, nullptr, 3) == 0);

    int plain = gge_object_create(r, 0, 0, 0);
    CHECK(gge_object_get_pose(r, plain, tooSmall, 1) == 0);  // 스켈레톤 없음
    gge_renderer_destroy(r);
}

void Test_ClipSamplingInterpolatesAndClamps()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    int clip = RotationClip(r, 0, kPi / 2);

    // 본1을 Z축으로 θ 회전하면 본2는 본1 위치 + Rz(θ)(0,1,0) = (-sinθ, 1+cosθ, 0)
    auto expectAt = [&](float time, float theta) {
        CHECK(gge_object_set_pose(r, obj, clip, time, 0, 0, 0) == 1);
        std::vector<V3> pos = BonePositions(r, obj, 3);
        CHECK(Near(pos[1], {0, 1, 0}));  // 본1 자신의 위치는 회전으로 변하지 않는다
        V3 expected{-std::sin(theta), 1 + std::cos(theta), 0};
        if (!Near(pos[2], expected))
            std::printf("  t=%g got (%g,%g,%g) expected (%g,%g,%g)\n", time, pos[2].x, pos[2].y, pos[2].z,
                        expected.x, expected.y, expected.z);
        CHECK(Near(pos[2], expected));
    };
    expectAt(0.0f, 0.0f);
    expectAt(0.5f, kPi / 4);
    expectAt(0.25f, kPi / 8);
    expectAt(1.0f, kPi / 2);
    expectAt(5.0f, kPi / 2);   // duration 초과는 마지막 키로 클램프
    expectAt(-3.0f, 0.0f);     // 음수는 첫 키로 클램프
    gge_renderer_destroy(r);
}

void Test_MultiKeyTranslationAndUnanimatedBonesKeepRest()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    // 본0의 이동: t=0 → y=0, t=1 → y=2, t=3 → y=2
    int clip = CreateClip(r, 3.0f, {{0, 0, {0, 1, 3}, {0, 0, 0, 0, 2, 0, 0, 2, 0}}});
    CHECK(clip > 0);

    gge_object_set_pose(r, obj, clip, 0.5f, 0, 0, 0);
    std::vector<V3> pos = BonePositions(r, obj, 3);
    CHECK(Near(pos[0], {0, 1, 0}));   // 구간 [0,1]의 중간
    CHECK(Near(pos[1], {0, 2, 0}));   // 자식은 부모를 따라간다
    CHECK(Near(pos[2], {0, 3, 0}));

    gge_object_set_pose(r, obj, clip, 2.0f, 0, 0, 0);
    pos = BonePositions(r, obj, 3);
    CHECK(Near(pos[0], {0, 2, 0}));   // 구간 [1,3]은 같은 값
    CHECK(Near(pos[2], {0, 4, 0}));

    // 스케일 채널: 본1을 Y로 2배 → 본2는 본1 + (0,2,0)
    int scaleClip = CreateClip(r, 1.0f, {{1, 2, {0}, {1, 2, 1}}});
    gge_object_set_pose(r, obj, scaleClip, 0.0f, 0, 0, 0);
    pos = BonePositions(r, obj, 3);
    CHECK(Near(pos[0], {0, 0, 0}) && Near(pos[1], {0, 1, 0}) && Near(pos[2], {0, 3, 0}));
    gge_renderer_destroy(r);
}

void Test_BlendInterpolatesBetweenClips()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    int straight = RotationClip(r, 0, 0);
    int bent = RotationClip(r, kPi / 2, kPi / 2);

    auto bone2 = [&](int a, int b, float blend) {
        CHECK(gge_object_set_pose(r, obj, a, 0.5f, b, 0.5f, blend) == 1);
        return BonePositions(r, obj, 3)[2];
    };
    CHECK(Near(bone2(straight, bent, 0.0f), {0, 2, 0}));
    CHECK(Near(bone2(straight, bent, 1.0f), {-1, 1, 0}));
    CHECK(Near(bone2(straight, bent, 0.5f), {-std::sin(kPi / 4), 1 + std::cos(kPi / 4), 0}));
    CHECK(Near(bone2(bent, straight, 0.5f), {-std::sin(kPi / 4), 1 + std::cos(kPi / 4), 0}));
    CHECK(Near(bone2(0, bent, 1.0f), {-1, 1, 0}));  // 레스트 포즈에서 B로 블렌드

    CHECK(gge_object_set_pose(r, obj, straight, 0, bent, 0, 1.5f) == 0);
    CHECK(gge_object_set_pose(r, obj, straight, 0, bent, 0, -0.1f) == 0);
    CHECK(gge_object_set_pose(r, obj, straight, 0, bent, 0, std::nanf("")) == 0);
    CHECK(gge_object_set_pose(r, obj, straight, std::nanf(""), 0, 0, 0) == 0);
    CHECK(gge_object_set_pose(r, obj, 999, 0, 0, 0, 0) == 0);
    CHECK(gge_object_set_pose(r, 999, straight, 0, 0, 0, 0) == 0);
    gge_renderer_destroy(r);
}

void Test_SkeletonCompatibilityIsEnforced()
{
    GgeRenderer* r = NewRenderer();
    int skinned = SkinnedStrip(r);
    int stat = CubeMesh(r);
    int skeleton3 = ChainSkeleton(r, 3);
    int skeleton1 = ChainSkeleton(r, 1);

    int staticObj = gge_object_create(r, stat, 0, 0);
    CHECK(gge_object_set_skeleton(r, staticObj, skeleton3) == 0);  // 정적 메시
    CHECK(std::string(gge_last_error()).find("not skinned") != std::string::npos);

    int obj = gge_object_create(r, skinned, 0, 0);
    CHECK(gge_object_set_skeleton(r, obj, skeleton1) == 0);  // 메시가 본1을 쓰는데 본이 1개
    CHECK(std::string(gge_last_error()).find("bone") != std::string::npos);
    CHECK(gge_object_set_skeleton(r, obj, 999) == 0);
    CHECK(gge_object_set_skeleton(r, obj, skeleton3) == 1);
    CHECK(gge_object_set_mesh(r, obj, stat) == 0);  // 스켈레톤이 붙은 채 정적 메시로 교체 불가
    CHECK(gge_object_set_skeleton(r, obj, 0) == 1);   // 해제 후에는 가능
    CHECK(gge_object_set_mesh(r, obj, stat) == 1);

    // 클립이 존재하지 않는 본을 가리키면 거부
    int skinnedObj = gge_object_create(r, skinned, 0, 0);
    gge_object_set_skeleton(r, skinnedObj, skeleton3);
    int farClip = CreateClip(r, 1.0f, {{5, 0, {0}, {0, 0, 0}}});
    CHECK(farClip > 0);
    CHECK(gge_object_set_pose(r, skinnedObj, farClip, 0, 0, 0, 0) == 0);
    CHECK(std::string(gge_last_error()).find("bone 5") != std::string::npos);
    gge_renderer_destroy(r);
}

void Test_SkinWeightsAreValidatedAndNormalized()
{
    GgeRenderer* r = NewRenderer();
    const float v[] = {-0.3f, 0, 0, 0, 0, 1, 0, 0,  0.3f, 0, 0, 0, 0, 1, 1, 0,  0, 2, 0, 0, 0, 1, 0.5f, 1};
    const uint32_t idx[] = {0, 1, 2};
    const uint32_t joints[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0};
    const float ok[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, ok, idx, 3) > 0);

    float negative[12]; std::memcpy(negative, ok, sizeof ok); negative[0] = -1;
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, negative, idx, 3) == 0);
    float nanWeight[12]; std::memcpy(nanWeight, ok, sizeof ok); nanWeight[1] = std::nanf("");
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, nanWeight, idx, 3) == 0);
    uint32_t farJoint[12]; std::memcpy(farJoint, joints, sizeof joints); farJoint[0] = 64;
    CHECK(gge_mesh_create_skinned(r, v, 3, farJoint, ok, idx, 3) == 0);
    float zeroWeightFar[12]; std::memcpy(zeroWeightFar, ok, sizeof ok);
    uint32_t farButUnused[12]; std::memcpy(farButUnused, joints, sizeof joints); farButUnused[1] = 200;  // 가중치 0이라 무시
    CHECK(gge_mesh_create_skinned(r, v, 3, farButUnused, zeroWeightFar, idx, 3) > 0);
    CHECK(gge_mesh_create_skinned(r, v, 3, nullptr, ok, idx, 3) == 0);
    const float zeroSum[12] = {};
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, zeroSum, idx, 3) > 0);  // 영향 없음 → 본 0

    // 가중치 {2,2,0,0}은 {0.5,0.5,0,0}과 같은 결과를 낸다(정규화).
    const uint32_t jointsMix[] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
    const float big[] = {2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0};
    const float norm[] = {0.5f, 0.5f, 0, 0, 0.5f, 0.5f, 0, 0, 0.5f, 0.5f, 0, 0};
    auto render = [&](const float* weights) {
        int mesh = gge_mesh_create_skinned(r, v, 3, jointsMix, weights, idx, 3);
        int obj = gge_object_create(r, mesh, 0, 0);
        gge_object_set_skeleton(r, obj, ChainSkeleton(r, 2));
        gge_object_set_pose(r, obj, RotationClip(r, kPi / 2, kPi / 2), 0, 0, 0, 0);
        gge_renderer_set_camera(r, 0, 1, 5, 0, 1, 0, 0.8f);
        Image img = Render(r);
        gge_object_destroy(r, obj);
        return img;
    };
    CHECK(render(big).bgra == render(norm).bgra);
    gge_renderer_destroy(r);
}

int DifferingPixels(const Image& a, const Image& b, int tolerance = 2)
{
    int count = 0;
    for (size_t i = 0; i < a.bgra.size(); i += 4)
        for (int ch = 0; ch < 3; ++ch)
            if (std::abs(int(a.bgra[i + ch]) - int(b.bgra[i + ch])) > tolerance) { ++count; break; }
    return count;
}

void Test_SkinnedRenderMatchesDeformedStaticMesh()
{
    // 스키닝 결과를 CPU에서 같은 정점 변환으로 만든 정적 메시와 비교한다.
    const float theta = kPi / 3;
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    gge_object_set_color(r, obj, 0.9f, 0.6f, 0.3f);
    gge_renderer_set_camera(r, 0, 1, 5, 0, 1, 0, 0.8f);

    Image rest = Render(r);
    gge_object_set_pose(r, obj, RotationClip(r, theta, theta), 0, 0, 0, 0);
    Image skinned = Render(r);
    CHECK(DifferingPixels(rest, skinned) > 100);  // 포즈가 실제로 형상을 바꿨다

    // 행1·행2(본1 소속): p' = (0,1) + Rz(θ)·(x, y-1). 법선도 같은 회전.
    const float c = std::cos(theta), s = std::sin(theta);
    auto deform = [&](float x, float y) { return V3{c * x - s * (y - 1), 1 + s * x + c * (y - 1), 0}; };
    const float xs[2] = {-0.3f, 0.3f};
    std::vector<float> v;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col) {
            float x = xs[col], y = static_cast<float>(row);
            V3 p = row == 0 ? V3{x, y, 0} : deform(x, y);
            v.insert(v.end(), {p.x, p.y, p.z, 0, 0, 1, static_cast<float>(col), 0});  // Z축 회전이라 법선은 +Z 그대로
        }
    const uint32_t idx[] = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
    int staticMesh = gge_mesh_create(r, v.data(), 6, idx, 12);
    CHECK(staticMesh > 0);
    gge_object_set_skeleton(r, obj, 0);
    gge_object_set_mesh(r, obj, staticMesh);
    Image expected = Render(r);

    int diff = DifferingPixels(skinned, expected);
    if (diff > 40)
        std::printf("  differing pixels: %d\n", diff);
    CHECK(diff <= 40);
    gge_renderer_destroy(r);
}

void Test_ObjectTransformIsIndependentOfPose()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    gge_object_set_pose(r, obj, RotationClip(r, kPi / 2, kPi / 2), 0.5f, 0, 0, 0);
    std::vector<V3> before = BonePositions(r, obj, 3);
    Place(r, obj, 5, 0, 0, 2.0f);
    std::vector<V3> after = BonePositions(r, obj, 3);  // 본 행렬은 모델 공간이라 오브젝트 변환의 영향이 없다
    for (int i = 0; i < 3; ++i) CHECK(Near(before[i], after[i]));
    gge_renderer_destroy(r);
}

// 높이 3, 반지름 0.3의 8각 튜브. 본(y=0,1,2) 사이를 선형으로 섞는 부드러운 스키닝.
int SkinnedTube(GgeRenderer* r)
{
    const int rings = 13, sides = 8;
    std::vector<float> v, weights;
    std::vector<uint32_t> joints, idx;
    for (int k = 0; k < rings; ++k) {
        float y = k * 0.25f;
        float lower = std::min(std::floor(y), 1.0f);  // 0 또는 1 (y>=2는 마지막 본에 고정)
        float t = y >= 2.0f ? 1.0f : y - lower;
        uint32_t jointA = y >= 2.0f ? 2u : static_cast<uint32_t>(lower), jointB = y >= 2.0f ? 2u : jointA + 1;
        for (int j = 0; j < sides; ++j) {
            float a = 2 * kPi * j / sides;
            float nx = std::cos(a), nz = std::sin(a);
            v.insert(v.end(), {0.3f * nx, y, 0.3f * nz, nx, 0, nz, static_cast<float>(j) / sides, y / 3.0f});
            joints.insert(joints.end(), {jointA, jointB, 0, 0});
            weights.insert(weights.end(), {1 - t, t, 0, 0});
        }
    }
    for (int k = 0; k + 1 < rings; ++k)
        for (int j = 0; j < sides; ++j) {
            uint32_t a = k * sides + j, b = k * sides + (j + 1) % sides;
            uint32_t d = (k + 1) * sides + j, c = (k + 1) * sides + (j + 1) % sides;
            idx.insert(idx.end(), {a, d, c, a, c, b});
        }
    int id = gge_mesh_create_skinned(r, v.data(), rings * sides, joints.data(), weights.data(), idx.data(),
                                     static_cast<int>(idx.size()));
    CHECK(id > 0);
    return id;
}

void Test_GoldenSkinnedBend()
{
    GgeRenderer* r = NewRenderer(320, 240);
    int mesh = SkinnedTube(r);
    int skeleton = ChainSkeleton(r);
    int bend = CreateClip(r, 1.0f, {ZRotation(1, 0, kPi / 3), ZRotation(2, 0, kPi / 3)});
    CHECK(bend > 0);
    const float times[3] = {0.0f, 0.5f, 1.0f};
    for (int i = 0; i < 3; ++i) {
        int obj = gge_object_create(r, mesh, 0, 0);
        gge_object_set_skeleton(r, obj, skeleton);
        gge_object_set_pose(r, obj, bend, times[i], 0, 0, 0);
        gge_object_set_color(r, obj, 0.9f, 0.6f, 0.3f);
        Place(r, obj, -2.2f + 2.2f * i, 0, 0);
    }
    gge_renderer_set_camera(r, 0, 1.6f, 8, 0, 1.4f, 0, 0.8f);
    gge_renderer_set_retro(r, 1, 5, 1, 0);
    CHECK(MatchesGolden("skinned_bend_320x240", Render(r)));
    gge_renderer_destroy(r);
}

// ---- 6단계: 게임플레이 월드(고정 카메라 방) ----
const float kEyeA[3] = {4, 3, 5}, kEyeB[3] = {-4, 3, 5}, kLookAt[3] = {0, 0, 0};

// 축 정렬 사각형 [x0,x1]x[z0,z1]의 꼭짓점 4개(x,z 쌍).
std::array<float, 8> Rect(float x0, float z0, float x1, float z1) { return {x0, z0, x1, z0, x1, z1, x0, z1}; }

int AddRoom(GgeWorld* w, const float* xz, int n, const float* eye = kEyeA)
{
    return gge_world_add_room(w, xz, n, eye, kLookAt, 0.8f);
}

void Test_RoomRejectsBadInput()
{
    GgeWorld* w = gge_world_create();
    auto sq = Rect(0, 0, 2, 2);
    CHECK(AddRoom(w, sq.data(), 4) == 1);

    CHECK(AddRoom(w, sq.data(), 2) == 0);   // 꼭짓점 부족
    CHECK(AddRoom(w, nullptr, 4) == 0);
    const float line[] = {0, 0, 1, 0, 2, 0};  // 면적 0
    CHECK(AddRoom(w, line, 3) == 0);
    const float dup[] = {0, 0, 0, 0, 2, 0, 2, 2};  // 길이 0인 변
    CHECK(AddRoom(w, dup, 4) == 0);
    const float bowtieFlat[] = {0, 0, 2, 2, 2, 0, 0, 2};  // 대칭 나비넥타이: 부호 있는 면적이 0
    CHECK(AddRoom(w, bowtieFlat, 4) == 0);
    const float bowtie[] = {0, 0, 4, 2, 4, 0, 0, 1};  // 비대칭: 면적은 0이 아니지만 두 변이 교차
    CHECK(AddRoom(w, bowtie, 4) == 0);
    CHECK(std::strstr(gge_last_error(), "self-intersecting") != nullptr);
    const float spike[] = {0, 0, 4, 0, 2, 0, 2, 2};  // 같은 직선 위에서 되돌아가는 돌기
    CHECK(AddRoom(w, spike, 4) == 0);
    auto nan = sq;
    nan[3] = std::nanf("");
    CHECK(AddRoom(w, nan.data(), 4) == 0);

    CHECK(gge_world_add_room(w, sq.data(), 4, nullptr, kLookAt, 0.8f) == 0);
    CHECK(gge_world_add_room(w, sq.data(), 4, kEyeA, kEyeA, 0.8f) == 0);  // eye == target
    CHECK(gge_world_add_room(w, sq.data(), 4, kEyeA, kLookAt, 0.0f) == 0);
    CHECK(gge_world_add_room(w, sq.data(), 4, kEyeA, kLookAt, 3.2f) == 0);  // fov >= pi
    CHECK(gge_world_add_room(w, sq.data(), 4, kEyeA, kLookAt, std::nanf("")) == 0);

    auto sq2 = Rect(10, 10, 12, 12);
    CHECK(AddRoom(w, sq2.data(), 4) == 2);  // 실패한 호출이 ID를 소모하지 않았다
    gge_world_destroy(w);
}

void Test_FindRoomHandlesConcaveAndBoundary()
{
    // L자(오목): 아래 가로 막대 + 왼쪽 세로 막대. 오목 모서리 바깥 영역이 (2..4, 2..4)다.
    const float ccw[] = {0, 0, 4, 0, 4, 2, 2, 2, 2, 4, 0, 4};
    const float cw[] = {0, 4, 2, 4, 2, 2, 4, 2, 4, 0, 0, 0};  // 같은 도형, 반대 감김
    const float* shapes[] = {ccw, cw};
    for (const float* shape : shapes) {  // 감는 방향과 무관해야 한다
        GgeWorld* w = gge_world_create();
        int room = AddRoom(w, shape, 6);
        CHECK(room == 1);
        CHECK(gge_world_find_room(w, 1, 1) == room);
        CHECK(gge_world_find_room(w, 3, 1) == room);
        CHECK(gge_world_find_room(w, 1, 3) == room);
        CHECK(gge_world_find_room(w, 3, 3) == 0);    // 오목 모서리 바깥
        CHECK(gge_world_find_room(w, -0.5f, 1) == 0);
        CHECK(gge_world_find_room(w, 5, 1) == 0);
        CHECK(gge_world_find_room(w, 0, 1) == room);  // 변 위
        CHECK(gge_world_find_room(w, 4, 2) == room);  // 꼭짓점
        CHECK(gge_world_find_room(w, 2, 3) == room);  // 오목 모서리의 안쪽 변
        gge_world_destroy(w);
    }

    GgeWorld* w = gge_world_create();
    auto first = Rect(0, 0, 4, 4), second = Rect(2, 0, 6, 4);
    int a = AddRoom(w, first.data(), 4), b = AddRoom(w, second.data(), 4);
    CHECK(gge_world_find_room(w, 3, 1) == a);  // 겹치면 ID가 작은 방이 앞선다
    CHECK(gge_world_find_room(w, 5, 1) == b);
    gge_world_destroy(w);
}

void Test_ActiveRoomSwitchesAndKeepsHysteresis()
{
    GgeWorld* w = gge_world_create();
    CHECK(gge_world_get_active_room(w) == 0);
    CHECK(gge_world_update_active_room(w, 0, 0) == 0);  // 방이 없다

    auto left = Rect(0, 0, 4, 4), right = Rect(4, 0, 8, 4), overlap = Rect(3, 0, 9, 4);
    int a = AddRoom(w, left.data(), 4), b = AddRoom(w, right.data(), 4, kEyeB), c = AddRoom(w, overlap.data(), 4);
    CHECK(a == 1 && b == 2 && c == 3);

    CHECK(gge_world_update_active_room(w, 20, 20) == 0);   // 처음에 어떤 방에도 속하지 않음
    CHECK(gge_world_update_active_room(w, 8.5f, 1) == c);  // c에만 속함
    CHECK(gge_world_find_room(w, 3.5f, 1) == a);           // 조회는 ID가 작은 a를 고르지만 활성 방은 안 바뀐다
    CHECK(gge_world_get_active_room(w) == c);
    CHECK(gge_world_update_active_room(w, 3.5f, 1) == c);  // a·c가 겹치는 곳: 현재 방 c 유지(깜빡임 없음)
    CHECK(gge_world_update_active_room(w, 1, 1) == a);     // c를 벗어나 a로 전환
    CHECK(gge_world_update_active_room(w, 4, 1) == a);     // 맞닿은 경계에서도 a 유지
    CHECK(gge_world_update_active_room(w, 4.5f, 1) == b);  // a를 벗어나 b(b,c 중 ID가 작은 방)로 전환
    CHECK(gge_world_update_active_room(w, 20, 20) == b);   // 방 밖으로 나가도 마지막 방 유지
    CHECK(gge_world_update_active_room(w, 3.5f, 1) == a);  // 돌아오면 다시 전환
    gge_world_destroy(w);
}

void Test_RoomCameraRoundTripAndDrivesRender()
{
    GgeWorld* w = gge_world_create();
    auto left = Rect(0, 0, 4, 4), right = Rect(6, 0, 10, 4);
    int a = AddRoom(w, left.data(), 4, kEyeA), b = AddRoom(w, right.data(), 4, kEyeB);

    float eye[3], target[3], fov = 0;
    CHECK(gge_world_get_room_camera(w, b, eye, target, &fov) == 1);
    CHECK(std::memcmp(eye, kEyeB, sizeof(eye)) == 0 && std::memcmp(target, kLookAt, sizeof(target)) == 0);
    CHECK(fov == 0.8f);
    CHECK(gge_world_get_room_camera(w, 0, eye, target, &fov) == 0);
    CHECK(gge_world_get_room_camera(w, 3, eye, target, &fov) == 0);
    CHECK(gge_world_get_room_camera(w, a, nullptr, target, &fov) == 0);

    // 방 진입으로 고른 카메라를 렌더러에 적용하면 직접 지정한 카메라와 같은 화면이고, 방마다 화면이 다르다.
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    auto renderFromRoomAt = [&](float x, float z) {
        int room = gge_world_update_active_room(w, x, z);
        CHECK(gge_world_get_room_camera(w, room, eye, target, &fov) == 1);
        gge_renderer_set_camera(r, eye[0], eye[1], eye[2], target[0], target[1], target[2], fov);
        return Render(r);
    };
    Image inA = renderFromRoomAt(1, 1);
    gge_renderer_set_camera(r, kEyeA[0], kEyeA[1], kEyeA[2], 0, 0, 0, 0.8f);
    CHECK(inA.bgra == Render(r).bgra);
    Image inB = renderFromRoomAt(8, 1);
    gge_renderer_set_camera(r, kEyeB[0], kEyeB[1], kEyeB[2], 0, 0, 0, 0.8f);
    CHECK(inB.bgra == Render(r).bgra);
    CHECK(inA.bgra != inB.bgra);
    gge_renderer_destroy(r);
    gge_world_destroy(w);
}

// ---- 6단계: 충돌 (원형 이동체 vs 정적 AABB) ----
struct P2 { float x, z; };

P2 Move(GgeWorld* w, float x, float z, float dx, float dz, float radius)
{
    float out[2] = {-999.0f, -999.0f};
    CHECK(gge_world_move_circle(w, x, z, dx, dz, radius, out) == 1);
    return {out[0], out[1]};
}

// 원 중심에서 상자까지의 거리(중심이 상자 안이면 0).
float DistanceToBox(P2 c, float minX, float minZ, float maxX, float maxZ)
{
    float nx = std::fmin(std::fmax(c.x, minX), maxX), nz = std::fmin(std::fmax(c.z, minZ), maxZ);
    return std::hypot(c.x - nx, c.z - nz);
}

constexpr float kTol = 1e-3f;  // 밀어내기 여유(skin)와 부동소수 오차를 흡수하는 허용치

void Test_ColliderAndMoveRejectBadInput()
{
    GgeWorld* w = gge_world_create();
    CHECK(gge_world_add_box_collider(w, 0, 0, 1, 1) == 1);
    CHECK(gge_world_add_box_collider(w, 1, 0, 1, 1) == 0);  // 폭 0
    CHECK(gge_world_add_box_collider(w, 2, 0, 1, 1) == 0);  // min > max
    CHECK(gge_world_add_box_collider(w, 0, 0, std::nanf(""), 1) == 0);
    CHECK(gge_world_add_box_collider(w, 0, 0, INFINITY, 1) == 0);
    CHECK(gge_world_add_box_collider(w, 5, 5, 6, 6) == 2);  // 실패한 호출이 ID를 소모하지 않았다

    float out[2];
    CHECK(gge_world_move_circle(w, 0, 0, 1, 0, 0.0f, out) == 0);
    CHECK(gge_world_move_circle(w, 0, 0, 1, 0, -1.0f, out) == 0);
    CHECK(gge_world_move_circle(w, std::nanf(""), 0, 1, 0, 0.5f, out) == 0);
    CHECK(gge_world_move_circle(w, 0, 0, 1, INFINITY, 0.5f, out) == 0);
    CHECK(gge_world_move_circle(w, 0, 0, 1, 0, 0.5f, nullptr) == 0);
    CHECK(gge_world_move_circle(w, 0, 0, 1e9f, 0, 0.5f, out) == 0);  // 이동량 과다는 멈추지 않고 거부
    CHECK(std::strstr(gge_last_error(), "too large") != nullptr);
    CHECK(gge_world_move_circle(w, 0, 0, 100, 0, 0.5f, out) == 1);   // 한도 안의 큰 이동은 허용
    gge_world_destroy(w);
}

// ---- 콜라이더 켜기/끄기(문) ----
void Test_WorldColliderEnableRules()
{
    GgeWorld* w = gge_world_create();
    CHECK(gge_world_add_box_collider(w, 2, -5, 3, 5) == 1);
    CHECK(gge_world_is_collider_enabled(w, 1) == 1);                          // 처음엔 켜짐
    CHECK(std::fabs(Move(w, 0, 0, 5, 0, 0.5f).x - 1.5f) < 1e-3f);             // 켜진 벽은 막는다(반지름 0.5 -> x=1.5)

    CHECK(gge_world_set_collider_enabled(w, 1, 0) == 1);
    CHECK(gge_world_is_collider_enabled(w, 1) == 0);
    CHECK(std::fabs(Move(w, 0, 0, 5, 0, 0.5f).x - 5.0f) < 1e-4f);             // 끄면 지나간다
    CHECK(gge_world_set_collider_enabled(w, 1, 0) == 1);                      // 이미 꺼져 있어도 성공하고 변화가 없다(멱등)
    CHECK(std::fabs(Move(w, 0, 0, 5, 0, 0.5f).x - 5.0f) < 1e-4f);
    CHECK(gge_world_set_collider_enabled(w, 1, 1) == 1);
    CHECK(gge_world_set_collider_enabled(w, 1, 7) == 1);                      // 0이 아니면 켬
    CHECK(gge_world_is_collider_enabled(w, 1) == 1);
    CHECK(std::fabs(Move(w, 0, 0, 5, 0, 0.5f).x - 1.5f) < 1e-3f);

    for (int id : {0, -1, 2, 1000}) {                                         // 알 수 없는 ID: 0 / -1과 사유, 상태는 그대로
        CHECK(gge_world_set_collider_enabled(w, id, 0) == 0);
        CHECK(std::strstr(gge_last_error(), "unknown collider id") != nullptr);
        CHECK(gge_world_is_collider_enabled(w, id) == -1);
    }
    CHECK(gge_world_is_collider_enabled(w, 1) == 1);

    // 꺼진 동안 안에 있으면 그대로이고, 켜는 순간 겹쳐 있으면 이동량 0이어도 가장 가까운 면 바깥으로 밀려난다(왼쪽 면까지 0.3 < 오른쪽 0.7).
    CHECK(gge_world_set_collider_enabled(w, 1, 0) == 1);
    CHECK(std::fabs(Move(w, 2.3f, 0, 0, 0, 0.5f).x - 2.3f) < 1e-5f);
    CHECK(gge_world_set_collider_enabled(w, 1, 1) == 1);
    CHECK(std::fabs(Move(w, 2.3f, 0, 0, 0, 0.5f).x - 1.5f) < 1e-3f);
    gge_world_destroy(w);
}

void Test_ColliderStateStaysConsistentAcrossTogglesAndAdds()
{
    // 상태가 세 벌(전체/켜짐 표시/켜진 것만)이라, 끄기/켜기/추가를 섞은 뒤에도 결과가 "지금 켜진 것만으로 새로 만든 월드"와 같아야 한다.
    const float boxes[5][4] = {{2, -5, 3, 5}, {-3, -5, -2, 5}, {-5, 2, 5, 3}, {-5, -3, 5, -2}, {6, 6, 7, 7}};
    struct Op { bool add; int index; int enabled; };   // add면 boxes[index]를 추가, 아니면 ID index + 1을 켜기/끄기
    const Op ops[] = {{true, 0, 0}, {true, 1, 0}, {false, 0, 0}, {true, 2, 0}, {false, 1, 0}, {false, 0, 1}, {true, 3, 0}, {false, 2, 0},
                      {false, 1, 1}, {false, 0, 0}, {true, 4, 0}, {false, 3, 0}, {false, 2, 1}, {false, 0, 1}, {false, 4, 0}};
    const float probes[][4] = {{0, 0, 5, 0}, {0, 0, -5, 0}, {0, 0, 0, 5}, {0, 0, 0, -5}, {2.5f, 0, 0, 0}, {-2.5f, 0, 0, 0},
                               {0, 2.5f, 0, 0}, {0, -2.5f, 0, 0}, {6.5f, 6.5f, 0, 0}, {4, 4, -3, -3}};
    GgeWorld* w = gge_world_create();
    bool enabled[5] = {};
    int added = 0;
    for (const Op& op : ops) {
        if (op.add) {
            CHECK(gge_world_add_box_collider(w, boxes[op.index][0], boxes[op.index][1], boxes[op.index][2], boxes[op.index][3]) == added + 1);
            enabled[added++] = true;
        } else {
            CHECK(gge_world_set_collider_enabled(w, op.index + 1, op.enabled) == 1);
            enabled[op.index] = op.enabled != 0;
        }
        GgeWorld* fresh = gge_world_create();
        for (int i = 0; i < added; ++i)
            if (enabled[i]) gge_world_add_box_collider(fresh, boxes[i][0], boxes[i][1], boxes[i][2], boxes[i][3]);
        for (const auto& p : probes) {
            P2 a = Move(w, p[0], p[1], p[2], p[3], 0.5f), b = Move(fresh, p[0], p[1], p[2], p[3], 0.5f);
            CHECK(std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.z - b.z) < 1e-5f);
        }
        gge_world_destroy(fresh);
    }
    gge_world_destroy(w);
}

void Test_FreeMovementIsUnchanged()
{
    GgeWorld* w = gge_world_create();
    gge_world_add_box_collider(w, 10, 10, 11, 11);
    P2 p = Move(w, 0, 0, 1.5f, -2.0f, 0.5f);
    CHECK(std::fabs(p.x - 1.5f) < 1e-5f && std::fabs(p.z + 2.0f) < 1e-5f);
    P2 stay = Move(w, 3, 4, 0, 0, 0.5f);  // 이동량 0
    CHECK(stay.x == 3.0f && stay.z == 4.0f);
    gge_world_destroy(w);

    GgeWorld* empty = gge_world_create();  // 장애물이 없어도 동작
    P2 q = Move(empty, 1, 1, 2, 3, 0.5f);
    CHECK(std::fabs(q.x - 3.0f) < 1e-5f && std::fabs(q.z - 4.0f) < 1e-5f);
    gge_world_destroy(empty);
}

void Test_WallStopsHeadOnAndSlidesAlongSurface()
{
    GgeWorld* w = gge_world_create();
    gge_world_add_box_collider(w, 2, -5, 3, 5);  // x=2 면이 벽, 원 반지름 0.5 → 중심은 x<=1.5
    P2 head = Move(w, 0, 0, 5, 0, 0.5f);
    CHECK(std::fabs(head.x - 1.5f) < kTol && std::fabs(head.z) < kTol);

    P2 slide = Move(w, 0, 0, 3, 2, 0.5f);  // 대각선: 벽 쪽 성분은 막히고 나란한 성분(z +2)은 그대로 남는다
    CHECK(std::fabs(slide.x - 1.5f) < kTol && std::fabs(slide.z - 2.0f) < kTol);

    P2 away = Move(w, 1.5f, 0, -2, 0, 0.5f);  // 벽에 붙은 채 반대로 가면 막히지 않는다
    CHECK(std::fabs(away.x + 0.5f) < kTol);
    gge_world_destroy(w);
}

void Test_CornerDoesNotPenetrate()
{
    GgeWorld* w = gge_world_create();
    gge_world_add_box_collider(w, 0, 0, 2, 2);
    // 바깥 모서리(0,0)를 향해 정확히 대각선으로: 모서리에서 반지름만큼 떨어진 곳에서 멈춘다.
    P2 p = Move(w, -3, -3, 3, 3, 0.5f);
    CHECK(DistanceToBox(p, 0, 0, 2, 2) >= 0.5f - kTol);
    CHECK(std::fabs(p.x - p.z) < kTol);
    CHECK(p.x < 0 && p.x > -0.6f);
    // 모서리를 비스듬히 스치면 모서리를 돌아 미끄러진다(반대편으로 넘어가진 않는다).
    P2 q = Move(w, -1, -0.2f, 4, 0.0f, 0.5f);
    CHECK(DistanceToBox(q, 0, 0, 2, 2) >= 0.5f - kTol);
    gge_world_destroy(w);
}

void Test_FastMoveDoesNotTunnelThroughThinWall()
{
    GgeWorld* w = gge_world_create();
    gge_world_add_box_collider(w, 5, -5, 5.01f, 5);  // 두께 0.01
    P2 p = Move(w, 0, 0, 20, 0, 0.25f);               // 한 번에 벽 너머까지
    CHECK(p.x <= 5.0f - 0.25f + kTol);
    gge_world_destroy(w);
}

void Test_OverlappingStartIsPushedOut()
{
    GgeWorld* w = gge_world_create();
    gge_world_add_box_collider(w, 0, 0, 4, 4);
    P2 inside = Move(w, 1, 2, 0, 0, 0.5f);  // 중심이 상자 안: 가장 가까운 면(minX)으로 나간다
    CHECK(std::fabs(inside.x + 0.5f) < kTol && std::fabs(inside.z - 2.0f) < kTol);
    P2 partial = Move(w, 4.2f, 2, 0, 0, 0.5f);  // 걸쳐 있음: maxX 면 밖으로
    CHECK(std::fabs(partial.x - 4.5f) < kTol && std::fabs(partial.z - 2.0f) < kTol);
    gge_world_destroy(w);
}

void Test_NarrowGapBlocksOnlyWhenSmallerThanDiameter()
{
    // z in [2,3]에 놓인 벽에 x 방향 틈이 하나 있다. 반지름 0.5(지름 1.0)인 원이 +z로 지나가려 한다.
    auto gapResult = [](float halfGap) {
        GgeWorld* w = gge_world_create();
        gge_world_add_box_collider(w, -5, 2, -halfGap, 3);
        gge_world_add_box_collider(w, halfGap, 2, 5, 3);
        P2 p = Move(w, 0, 0, 0, 6, 0.5f);
        gge_world_destroy(w);
        return p;
    };
    P2 narrow = gapResult(0.3f);  // 틈 0.6 < 지름 1.0: 통과 못 함. 두 모서리 (±0.3, 2)에 동시에 접하는 z = 2 - 0.4 = 1.6에서 끼인다.
    CHECK(narrow.z <= 1.6f + 1e-2f);
    CHECK(DistanceToBox(narrow, -5, 2, -0.3f, 3) >= 0.5f - kTol);
    CHECK(DistanceToBox(narrow, 0.3f, 2, 5, 3) >= 0.5f - kTol);
    P2 wide = gapResult(0.7f);    // 틈 1.4 > 지름 1.0: 통과
    CHECK(std::fabs(wide.z - 6.0f) < kTol && std::fabs(wide.x) < kTol);
}

void Test_RandomWalkNeverPenetratesAndStaysInArena()
{
    GgeWorld* w = gge_world_create();
    struct B { float x0, z0, x1, z1; };
    std::vector<B> boxes = {
        {-12, -12, 12, -10}, {-12, 10, 12, 12}, {-12, -12, -10, 12}, {10, -12, 12, 12},  // 둘레 벽(안쪽 면 ±10)
        {-6, -6, -4, -4}, {4, -6, 6, -4}, {-6, 4, -4, 6}, {4, 4, 6, 6}, {-1, -1, 1, 1},  // 기둥(서로 지름보다 넓게 이격)
    };
    for (const B& b : boxes) CHECK(gge_world_add_box_collider(w, b.x0, b.z0, b.x1, b.z1) > 0);

    const float radius = 0.4f;
    uint32_t seed = 12345;
    auto rnd = [&]() {  // [-1, 1) 결정적 난수
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1 << 23) - 1.0f;
    };
    P2 pos = {-8.0f, 0.0f};
    float worstClearance = 1e9f;
    for (int i = 0; i < 3000; ++i) {
        pos = Move(w, pos.x, pos.z, rnd() * 2.5f, rnd() * 2.5f, radius);  // 반지름보다 훨씬 큰 이동 포함
        for (const B& b : boxes) worstClearance = std::fmin(worstClearance, DistanceToBox(pos, b.x0, b.z0, b.x1, b.z1));
        CHECK(std::fabs(pos.x) < 10.0f && std::fabs(pos.z) < 10.0f);
    }
    CHECK(worstClearance >= radius - kTol);
    gge_world_destroy(w);
}

// ---- 6단계: 입력 (카메라 상대 이동) ----
// 북쪽 카메라: -Z를 바라본다(앞 = -Z, 오른쪽 = +X). 동쪽 카메라: +X를 바라본다(앞 = +X, 오른쪽 = +Z).
const float kEyeNorth[3] = {0, 5, 10}, kEyeEast[3] = {-10, 5, 0};

int AddCamRoom(GgeWorld* w, float x0, float z0, float x1, float z1, const float* eye)
{
    auto rect = Rect(x0, z0, x1, z1);
    int id = gge_world_add_room(w, rect.data(), 4, eye, kLookAt, 0.8f);
    CHECK(id > 0);
    return id;
}

P2 Input(GgeWorld* w, float ix, float iy, float speed = 2.0f, float dt = 0.5f)
{
    float out[2] = {-999.0f, -999.0f};
    CHECK(gge_world_move_input(w, ix, iy, speed, dt, out) == 1);
    return {out[0], out[1]};
}

bool Near(P2 p, float x, float z) { return std::fabs(p.x - x) < 1e-4f && std::fabs(p.z - z) < 1e-4f; }

void Test_MoveInputRejectsBadInput()
{
    GgeWorld* w = gge_world_create();
    float out[2];
    CHECK(gge_world_move_input(w, 0, 1, 1, 1, out) == 0);  // 방이 없다
    AddCamRoom(w, -5, -5, 5, 5, kEyeNorth);
    CHECK(gge_world_move_input(w, 0, 1, 1, 1, out) == 0);  // 방은 있으나 활성 방이 아직 없다
    CHECK(std::strstr(gge_last_error(), "no active room") != nullptr);
    gge_world_update_active_room(w, 0, 0);
    CHECK(gge_world_move_input(w, 0, 1, 1, 1, out) == 1);
    CHECK(gge_world_move_input(w, std::nanf(""), 1, 1, 1, out) == 0);
    CHECK(gge_world_move_input(w, 0, INFINITY, 1, 1, out) == 0);
    CHECK(gge_world_move_input(w, 0, 1, -1, 1, out) == 0);
    CHECK(gge_world_move_input(w, 0, 1, 1, -0.1f, out) == 0);
    CHECK(gge_world_move_input(w, 0, 1, 1, 1, nullptr) == 0);

    // 수직으로 내려보는 카메라는 "앞" 방향이 없어 방 등록 단계에서 거부한다(카메라가 약간만 기울어도 허용).
    auto sq = Rect(0, 0, 2, 2);
    const float top[3] = {0, 10, 0};
    CHECK(gge_world_add_room(w, sq.data(), 4, top, kLookAt, 0.8f) == 0);
    CHECK(std::strstr(gge_last_error(), "straight up or down") != nullptr);
    const float steep[3] = {0, 10, 0.5f};
    CHECK(gge_world_add_room(w, sq.data(), 4, steep, kLookAt, 0.8f) > 0);
    const float nearVertical[3] = {0, 1000, 0.001f};  // 높이 대비 수평 성분이 너무 작아 앞 방향이 불안정
    CHECK(gge_world_add_room(w, sq.data(), 4, nearVertical, kLookAt, 0.8f) == 0);
    CHECK(gge_world_move_input(w, 1, 1, 1e30f, 1e30f, out) == 0);  // 유한한 인자의 곱이 넘쳐도 NaN/inf를 성공으로 돌려주지 않는다
    CHECK(std::strstr(gge_last_error(), "overflows") != nullptr);
    gge_world_destroy(w);
}

void Test_InputIsRelativeToCameraDirection()
{
    // 속도 2 x dt 0.5 = 1. 같은 입력이 카메라에 따라 다른 월드 방향이 된다.
    GgeWorld* north = gge_world_create();
    AddCamRoom(north, -5, -5, 5, 5, kEyeNorth);
    gge_world_update_active_room(north, 0, 0);
    CHECK(Near(Input(north, 0, 1), 0, -1));   // 위 = 앞 = -Z
    CHECK(Near(Input(north, 0, -1), 0, 1));
    CHECK(Near(Input(north, 1, 0), 1, 0));    // 오른쪽 = +X
    CHECK(Near(Input(north, -1, 0), -1, 0));
    gge_world_destroy(north);

    GgeWorld* east = gge_world_create();
    AddCamRoom(east, -5, -5, 5, 5, kEyeEast);
    gge_world_update_active_room(east, 0, 0);
    CHECK(Near(Input(east, 0, 1), 1, 0));     // 앞 = +X
    CHECK(Near(Input(east, 1, 0), 0, 1));     // 오른쪽 = +Z (앞 x 위)
    gge_world_destroy(east);

    // 카메라가 기울어도(높이 차) 이동은 바닥 평면에서만 일어나고 크기는 같다.
    GgeWorld* tilted = gge_world_create();
    const float eye[3] = {3, 9, 4};  // 시선 수평 성분 (-3,-4)/5
    AddCamRoom(tilted, -5, -5, 5, 5, eye);
    gge_world_update_active_room(tilted, 0, 0);
    CHECK(Near(Input(tilted, 0, 1), -0.6f, -0.8f));
    CHECK(Near(Input(tilted, 1, 0), 0.8f, -0.6f));
    gge_world_destroy(tilted);
}

void Test_InputMagnitudeIsClampedAndDeadzoned()
{
    GgeWorld* w = gge_world_create();
    AddCamRoom(w, -5, -5, 5, 5, kEyeNorth);
    gge_world_update_active_room(w, 0, 0);
    P2 diagonal = Input(w, 1, 1);  // 대각선도 길이 1(= speed x dt)
    CHECK(std::fabs(std::hypot(diagonal.x, diagonal.z) - 1.0f) < 1e-4f);
    CHECK(Near(Input(w, 0, 5), 0, -1));       // 범위를 넘는 입력도 1로 제한
    CHECK(Near(Input(w, 0, 0.5f), 0, -0.375f));  // 데드존을 빼고 다시 편다: (0.5-0.2)/0.8
    CHECK(Near(Input(w, 0.6f, 0), 0.5f, 0));
    CHECK(Near(Input(w, 0.1f, 0.1f), 0, 0));   // 데드존 안(크기 0.14)
    CHECK(Near(Input(w, 0.19f, 0), 0, 0));
    CHECK(Near(Input(w, 0.2f, 0), 0, 0));      // 경계에서 0부터 시작한다(속도가 튀지 않는다)
    CHECK(Near(Input(w, 0.21f, 0), 0.0125f, 0));  // 경계 바로 위는 아주 느리다: 0.01/0.8
    P2 halfDiagonal = Input(w, 0.5f, 0.5f);       // 크기 0.707: 길이 (0.707-0.2)/0.8 = 0.634
    CHECK(std::fabs(std::hypot(halfDiagonal.x, halfDiagonal.z) - 0.6338835f) < 1e-4f);
    CHECK(Near(Input(w, 0, 1, 0, 0.5f), 0, 0));  // speed 0, dt 0이면 이동 없음
    CHECK(Near(Input(w, 0, 1, 2, 0), 0, 0));
    gge_world_destroy(w);
}

void Test_CameraCutKeepsInputDirectionUntilReleaseOrSharpTurn()
{
    GgeWorld* w = gge_world_create();
    AddCamRoom(w, 0, -5, 10, 5, kEyeNorth);   // 방 1: 앞 = -Z, 오른쪽 = +X
    AddCamRoom(w, 10, -5, 20, 5, kEyeEast);   // 방 2: 앞 = +X, 오른쪽 = +Z
    CHECK(gge_world_update_active_room(w, 5, 0) == 1);
    CHECK(Near(Input(w, 0, 1), 0, -1));

    CHECK(gge_world_update_active_room(w, 15, 0) == 2);  // 앞을 누른 채 방 2로 진입(카메라 컷)
    CHECK(Near(Input(w, 0, 1), 0, -1));                  // 컷 직후 첫 입력: 방 1 기준 유지(되돌아가지 않는다)
    CHECK(Near(Input(w, 0.3f, 1), 0.28735f, -0.95783f));  // 약 17도 흔들림은 여전히 방 1 기준
    CHECK(Near(Input(w, 1, 0), 0, 1));                   // 컷 직후 방향에서 90도 꺾으면 방 2 기준으로 옮긴다
    CHECK(Near(Input(w, 0, 1), 1, 0));                   // 이후는 방 2 기준(앞 = +X)
    CHECK(Near(Input(w, 0, 0), 0, 0));
    CHECK(Near(Input(w, 0, 1), 1, 0));

    CHECK(gge_world_update_active_room(w, 5, 0) == 1);   // 반대로 방 1로 컷
    CHECK(Near(Input(w, 0, 1), 1, 0));                   // 여전히 방 2 기준
    CHECK(Near(Input(w, 0.1f, 0.1f), 0, 0));             // 데드존 안은 "놓음"으로 갱신
    CHECK(Near(Input(w, 0, 1), 0, -1));                  // 방 1 기준

    // 컷 직후 방향에서 60도 안쪽(약 45도)으로 꺾는 것은 고정 유지, 바로 바깥(약 75도)은 해제.
    CHECK(gge_world_update_active_room(w, 15, 0) == 2);
    CHECK(Near(Input(w, 0, 1), 0, -1));                  // 방 1 기준 + 컷 방향 기록
    P2 diag = Input(w, 0.7071068f, 0.7071068f);          // 45도: 방 1 기준 (+X, -Z)
    CHECK(diag.x > 0 && diag.z < 0 && std::fabs(diag.x + diag.z) < 1e-3f);
    P2 wide = Input(w, 0.9659258f, 0.2588190f);          // 75도: 방 2 기준으로 이동 (앞 +X*0.259, 오른쪽 +Z*0.966)
    CHECK(Near(wide, 0.2588190f, 0.9659258f));
    gge_world_destroy(w);
}

void Test_ResetInputFrameDropsStaleLatch()
{
    GgeWorld* w = gge_world_create();
    AddCamRoom(w, 0, -5, 10, 5, kEyeNorth);
    AddCamRoom(w, 10, -5, 20, 5, kEyeEast);
    gge_world_update_active_room(w, 5, 0);
    CHECK(Near(Input(w, 0, 1), 0, -1));
    gge_world_update_active_room(w, 15, 0);              // 텔레포트: 입력은 계속 눌림
    CHECK(Near(Input(w, 0, 1), 0, -1));                  // 같은 방향이라 이전 카메라 기준이 남아 있다
    gge_world_reset_input_frame(w);
    CHECK(Near(Input(w, 0, 1), 1, 0));                   // 리셋하면 곧바로 새 방 카메라 기준
    gge_world_destroy(w);
}

void Test_InputMoveThroughCollisionPerCamera()
{
    // 같은 입력(앞)이라도 카메라가 다르면 가는 방향이 다르고, 벽(x in [3,4])에는 동쪽 카메라일 때만 막힌다.
    auto run = [](const float* eye) {
        GgeWorld* w = gge_world_create();
        AddCamRoom(w, -10, -10, 10, 10, eye);
        gge_world_add_box_collider(w, 3, -10, 4, 10);
        P2 pos = {0, 0};
        for (int i = 0; i < 120; ++i) {  // 2초, 속도 2 => 거리 4
            gge_world_update_active_room(w, pos.x, pos.z);
            P2 d = Input(w, 0, 1, 2.0f, 1.0f / 60.0f);
            pos = Move(w, pos.x, pos.z, d.x, d.z, 0.5f);
        }
        gge_world_destroy(w);
        return pos;
    };
    P2 east = run(kEyeEast);    // +X로 가다 벽 앞(x = 3 - 0.5)에서 멈춘다
    CHECK(std::fabs(east.x - 2.5f) < kTol && std::fabs(east.z) < kTol);
    P2 north = run(kEyeNorth);  // -Z로 자유 이동
    CHECK(std::fabs(north.x) < kTol && std::fabs(north.z + 4.0f) < 1e-2f);
}

// ---- 렌더러: 프레임 사이 월드 행렬 캐시 ----
// 렌더가 부모 체인 월드 행렬을 프레임 단위로 캐시하므로, 프레임 사이에 부모 변환/재부모화를 바꿔도
// 낡은 행렬이 쓰이지 않고 새로 만든 렌더러와 같은 화면이 나와야 한다.
void Test_WorldMatrixCacheTracksChangesBetweenFrames()
{
    // 그룹 노드(부모) 아래 큐브(자식). 자식 로컬 위치 (1,0,0).
    struct Scene {
        GgeRenderer* r;
        int mesh, parent, child, other;
    };
    auto build = [](float parentX) {
        Scene s{NewRenderer(), 0, 0, 0, 0};
        s.mesh = CubeMesh(s.r);
        s.parent = gge_object_create(s.r, 0, 0, 0);
        s.other = gge_object_create(s.r, 0, 0, 0);
        s.child = gge_object_create(s.r, s.mesh, 0, s.parent);
        gge_object_set_color(s.r, s.child, 0.9f, 0.6f, 0.3f);
        Place(s.r, s.parent, parentX, 0, 0);
        Place(s.r, s.other, 0, 2, 0);
        Place(s.r, s.child, 1, 0, 0, 0.5f);
        gge_renderer_set_camera(s.r, 0, 3, 9, 0, 0, 0, 0.8f);
        return s;
    };

    Scene live = build(-2.0f);
    Image before = Render(live.r);          // 캐시가 채워진 프레임
    Place(live.r, live.parent, 2.0f, 0, 0);  // 부모 이동 후 다시 렌더
    Image moved = Render(live.r);
    Scene fresh = build(2.0f);
    CHECK(before.bgra != moved.bgra);
    CHECK(moved.bgra == Render(fresh.r).bgra);

    // 재부모화: 자식을 다른 그룹(0,2,0) 아래로 옮기면 그 그룹 기준으로 그려진다.
    CHECK(gge_object_set_parent(live.r, live.child, live.other) == 1);
    Image reparented = Render(live.r);
    Scene freshOther = build(2.0f);
    CHECK(gge_object_set_parent(freshOther.r, freshOther.child, freshOther.other) == 1);
    CHECK(reparented.bgra == Render(freshOther.r).bgra);
    CHECK(reparented.bgra != moved.bgra);

    // 변경 없이 연속 렌더해도 결과가 같다(캐시가 상태를 오염시키지 않는다).
    CHECK(Render(live.r).bgra == reparented.bgra);
    for (Scene* s : {&live, &fresh, &freshOther}) {
        gge_renderer_destroy(s->r);
    }
}

// ---- 리소스 해제 ----
void Test_ResourceDestroyRespectsUsageAndIds()
{
    GgeRenderer* r = NewRenderer();
    CHECK(gge_mesh_destroy(r, 99) == 0);  // 알 수 없는 ID와 0은 실패
    CHECK(gge_mesh_destroy(r, 0) == 0);
    CHECK(gge_texture_destroy(r, 99) == 0);
    CHECK(gge_skeleton_destroy(r, 99) == 0);
    CHECK(gge_clip_destroy(r, 99) == 0);

    // 메시: 오브젝트가 쓰는 동안은 거부하고(렌더는 그대로 동작), 오브젝트를 없앤 뒤에는 해제된다.
    int mesh = CubeMesh(r);
    int obj = gge_object_create(r, mesh, 0, 0);
    gge_renderer_set_camera(r, 0, 0, 6, 0, 0, 0, 0.8f);
    Image before = Render(r);
    CHECK(gge_mesh_destroy(r, mesh) == 0);
    CHECK(std::strstr(gge_last_error(), "in use by object") != nullptr);
    CHECK(Render(r).bgra == before.bgra);
    CHECK(gge_object_destroy(r, obj) == 1);
    CHECK(gge_mesh_destroy(r, mesh) == 1);
    CHECK(gge_mesh_destroy(r, mesh) == 0);          // 이중 해제
    CHECK(gge_object_create(r, mesh, 0, 0) == 0);    // 해제된 ID는 더 못 쓴다
    int mesh2 = CubeMesh(r);
    CHECK(mesh2 > mesh);                             // ID는 재사용되지 않는다

    // 텍스처: 오브젝트가 다른 텍스처로 바꾸면 해제할 수 있다.
    int tex = gge_texture_create(r, kQuadTexture, 2, 2);
    int obj2 = gge_object_create(r, mesh2, tex, 0);
    CHECK(gge_texture_destroy(r, tex) == 0);
    CHECK(gge_object_set_texture(r, obj2, 0) == 1);
    CHECK(gge_texture_destroy(r, tex) == 1);
    CHECK(gge_object_set_texture(r, obj2, tex) == 0);  // 해제된 텍스처는 지정할 수 없다

    // 스켈레톤/클립: 오브젝트가 스켈레톤을 쓰거나 클립으로 포즈를 잡고 있는 동안은 거부한다.
    int skinnedMesh = SkinnedTube(r);
    int skeleton = ChainSkeleton(r);
    int clip = CreateClip(r, 1.0f, {ZRotation(1, 0, 1)});
    int obj3 = gge_object_create(r, skinnedMesh, 0, 0);
    CHECK(gge_object_set_skeleton(r, obj3, skeleton) == 1);
    CHECK(gge_object_set_pose(r, obj3, clip, 0.5f, 0, 0, 0) == 1);
    CHECK(gge_skeleton_destroy(r, skeleton) == 0);
    CHECK(gge_clip_destroy(r, clip) == 0);
    CHECK(gge_object_set_skeleton(r, obj3, 0) == 1);
    CHECK(gge_skeleton_destroy(r, skeleton) == 1);
    CHECK(gge_clip_destroy(r, clip) == 0);             // 클립은 아직 포즈에 쓰인다
    CHECK(gge_object_set_pose(r, obj3, 0, 0, 0, 0, 0) == 1);
    CHECK(gge_clip_destroy(r, clip) == 1);
    CHECK(gge_object_set_pose(r, obj3, clip, 0, 0, 0, 0) == 0);  // 해제된 클립은 지정할 수 없다
    gge_renderer_destroy(r);
}

// ---- 6단계: 트리거 (영역 진입/퇴장 이벤트) ----
struct Ev { int id, kind, tag; };

// 큐에 쌓인 이벤트를 모두 꺼낸다.
std::vector<Ev> Drain(GgeWorld* w)
{
    std::vector<Ev> out;
    Ev e{};
    while (gge_world_poll_trigger_event(w, &e.id, &e.kind, &e.tag) == 1) {
        out.push_back(e);
    }
    return out;
}

bool Is(const Ev& e, int id, int kind, int tag) { return e.id == id && e.kind == kind && e.tag == tag; }

void Test_TriggerRejectsBadInput()
{
    GgeWorld* w = gge_world_create();
    auto sq = Rect(0, 0, 2, 2);
    CHECK(gge_world_add_trigger(w, sq.data(), 4, 5) == 1);
    CHECK(gge_world_add_trigger(w, sq.data(), 2, 5) == 0);  // 방과 같은 다각형 검사
    CHECK(gge_world_add_trigger(w, nullptr, 4, 5) == 0);
    const float bowtie[] = {0, 0, 4, 2, 4, 0, 0, 1};
    CHECK(gge_world_add_trigger(w, bowtie, 4, 5) == 0);
    CHECK(std::strstr(gge_last_error(), "self-intersecting") != nullptr);
    CHECK(gge_world_add_trigger(w, sq.data(), 4, 6) == 2);  // 실패한 호출이 ID를 소모하지 않았다

    CHECK(gge_world_set_trigger_enabled(w, 0, 1) == 0);
    CHECK(gge_world_set_trigger_enabled(w, 3, 1) == 0);
    CHECK(gge_world_is_inside_trigger(w, 3) == -1);
    CHECK(gge_world_is_inside_trigger(w, 1) == 0);
    CHECK(gge_world_update_triggers(w, std::nanf(""), 0) == -1);  // 0(이벤트 없음)과 구별되는 실패
    CHECK(gge_world_update_triggers(w, 0, INFINITY) == -1);
    CHECK(gge_world_poll_trigger_event(w, nullptr, nullptr, nullptr) == 0);  // 큐가 비었다
    gge_world_destroy(w);
}

void Test_TriggerEnterAndExitEvents()
{
    GgeWorld* w = gge_world_create();
    auto zone = Rect(0, 0, 2, 2);
    int id = gge_world_add_trigger(w, zone.data(), 4, 77);

    CHECK(gge_world_update_triggers(w, -1, 1) == 0);   // 밖: 이벤트 없음
    CHECK(Drain(w).empty());
    CHECK(gge_world_is_inside_trigger(w, id) == 0);

    CHECK(gge_world_update_triggers(w, 1, 1) == 1);    // 들어옴
    int eid = 0, kind = 0, tag = 0;
    CHECK(gge_world_poll_trigger_event(w, &eid, &kind, &tag) == 1);
    CHECK(eid == id && kind == GGE_TRIGGER_ENTER && tag == 77);
    CHECK(gge_world_poll_trigger_event(w, &eid, &kind, &tag) == 0);  // 하나뿐
    CHECK(gge_world_is_inside_trigger(w, id) == 1);

    CHECK(gge_world_update_triggers(w, 1.5f, 0.5f) == 0);  // 안에서 움직임: 이벤트 없음
    CHECK(gge_world_update_triggers(w, 1.9f, 1.9f) == 0);
    CHECK(Drain(w).empty());

    CHECK(gge_world_update_triggers(w, 3, 1) == 1);    // 나감
    std::vector<Ev> out = Drain(w);
    CHECK(out.size() == 1 && Is(out[0], id, GGE_TRIGGER_EXIT, 77));
    CHECK(gge_world_is_inside_trigger(w, id) == 0);

    CHECK(gge_world_update_triggers(w, 2, 1) == 1);    // 경계(x=2)는 안이다
    CHECK(Drain(w).size() == 1);
    CHECK(gge_world_poll_trigger_event(w, nullptr, nullptr, nullptr) == 0);
    gge_world_destroy(w);

    // 오목(L자) 영역: 오목 모서리 바깥(3,3)은 안이 아니다.
    GgeWorld* w2 = gge_world_create();
    const float ell[] = {0, 0, 4, 0, 4, 2, 2, 2, 2, 4, 0, 4};
    gge_world_add_trigger(w2, ell, 6, 1);
    CHECK(gge_world_update_triggers(w2, 3, 3) == 0);
    CHECK(gge_world_update_triggers(w2, 1, 3) == 1);
    gge_world_destroy(w2);
}

void Test_TriggerFiresEnterWhenSpawnedInside()
{
    GgeWorld* w = gge_world_create();
    auto zone = Rect(0, 0, 2, 2);
    gge_world_add_trigger(w, zone.data(), 4, 9);
    CHECK(gge_world_update_triggers(w, 1, 1) == 1);  // 첫 갱신에서 이미 안에 있다
    std::vector<Ev> out = Drain(w);
    CHECK(out.size() == 1 && Is(out[0], 1, GGE_TRIGGER_ENTER, 9));
    gge_world_destroy(w);
}

void Test_TriggerEventsOrderExitsBeforeEnters()
{
    GgeWorld* w = gge_world_create();
    auto a = Rect(0, 0, 2, 2), b = Rect(2, 0, 4, 2), c = Rect(3, 0, 6, 2);  // b와 c는 겹친다. a와 b는 x=2에서 맞닿는다
    gge_world_add_trigger(w, a.data(), 4, 10);  // ID 1
    gge_world_add_trigger(w, b.data(), 4, 20);  // ID 2
    gge_world_add_trigger(w, c.data(), 4, 30);  // ID 3
    gge_world_update_triggers(w, 1, 1);
    Drain(w);                                    // A 안에서 시작

    CHECK(gge_world_update_triggers(w, 3.5f, 1) == 3);  // A에서 나와 B와 C(겹침)로 동시에 들어간다
    std::vector<Ev> out = Drain(w);
    CHECK(out.size() == 3);
    if (out.size() == 3) {
        CHECK(Is(out[0], 1, GGE_TRIGGER_EXIT, 10));    // Exit가 먼저
        CHECK(Is(out[1], 2, GGE_TRIGGER_ENTER, 20));   // 그다음 Enter, 트리거 ID 순
        CHECK(Is(out[2], 3, GGE_TRIGGER_ENTER, 30));
    }
    gge_world_destroy(w);
}

void Test_TriggerDisableAndEnableWhileInside()
{
    GgeWorld* w = gge_world_create();
    auto zone = Rect(0, 0, 2, 2);
    int id = gge_world_add_trigger(w, zone.data(), 4, 1);
    gge_world_update_triggers(w, 1, 1);
    Drain(w);

    CHECK(gge_world_set_trigger_enabled(w, id, 0) == 1);     // 안에 있는 채로 끈다
    CHECK(gge_world_is_inside_trigger(w, id) == 1);          // 다음 update 전까지는 바뀌지 않는다
    CHECK(gge_world_update_triggers(w, 1, 1) == 1);
    std::vector<Ev> out = Drain(w);
    CHECK(out.size() == 1 && Is(out[0], id, GGE_TRIGGER_EXIT, 1));
    CHECK(gge_world_is_inside_trigger(w, id) == 0);

    CHECK(gge_world_update_triggers(w, 5, 5) == 0);          // 꺼진 채 밖으로
    CHECK(gge_world_update_triggers(w, 1, 1) == 0);          // 꺼진 영역에 들어가도 이벤트가 없다
    CHECK(gge_world_set_trigger_enabled(w, id, 1) == 1);     // 안에서 다시 켠다
    CHECK(gge_world_update_triggers(w, 1, 1) == 1);
    out = Drain(w);
    CHECK(out.size() == 1 && Is(out[0], id, GGE_TRIGGER_ENTER, 1));
    gge_world_destroy(w);
}

void Test_TriggerQueueIsFifoAndBounded()
{
    GgeWorld* w = gge_world_create();
    auto zone = Rect(0, 0, 2, 2);
    gge_world_add_trigger(w, zone.data(), 4, 3);
    // 폴링하지 않고 들락거린다: 이벤트 1200개(짝수 번째 Enter, 홀수 번째 Exit)를 만든다.
    for (int i = 0; i < 600; ++i) {
        CHECK(gge_world_update_triggers(w, 1, 1) == 1);
        CHECK(gge_world_update_triggers(w, 5, 5) == 1);
    }
    std::vector<Ev> out = Drain(w);
    CHECK(out.size() == 1024);                  // 상한: 가득 차면 가장 오래된 것부터 버린다
    if (out.size() == 1024) {
        CHECK(out.front().kind == GGE_TRIGGER_ENTER);   // 남은 첫 이벤트는 176번째(짝수 = Enter)
        CHECK(out.back().kind == GGE_TRIGGER_EXIT);     // 마지막은 1199번째(홀수 = Exit): 최신 것이 남는다
        for (size_t i = 1; i < out.size(); ++i) {       // 순서가 유지된다(Enter/Exit이 번갈아)
            CHECK(out[i].kind != out[i - 1].kind);
        }
    }
    CHECK(gge_world_poll_trigger_event(w, nullptr, nullptr, nullptr) == 0);
    gge_world_destroy(w);
}

void Test_TriggerFiresWhileWalkingThroughZone()
{
    // 입력 -> 이동(충돌 포함) -> 트리거 갱신을 프레임 루프로 묶어 문 앞 영역을 지나가는 시나리오.
    GgeWorld* w = gge_world_create();
    AddCamRoom(w, -10, -5, 20, 5, kEyeEast);  // 동쪽 카메라: 앞 입력 = +X
    auto doorZone = Rect(5, -2, 7, 2);
    int door = gge_world_add_trigger(w, doorZone.data(), 4, 100);
    gge_world_add_box_collider(w, 12, -5, 13, 5);  // 더 앞쪽의 벽

    P2 pos = {0, 0};
    int enterFrame = -1, exitFrame = -1, enters = 0, exits = 0;
    for (int frame = 0; frame < 400; ++frame) {
        gge_world_update_active_room(w, pos.x, pos.z);
        P2 d = Input(w, 0, 1, 3.0f, 1.0f / 60.0f);  // 속도 3, 프레임당 0.05
        pos = Move(w, pos.x, pos.z, d.x, d.z, 0.5f);
        CHECK(gge_world_update_triggers(w, pos.x, pos.z) >= 0);
        for (const Ev& e : Drain(w)) {
            CHECK(e.id == door && e.tag == 100);
            if (e.kind == GGE_TRIGGER_ENTER) { ++enters; if (enterFrame < 0) enterFrame = frame; }
            else { ++exits; if (exitFrame < 0) exitFrame = frame; }
        }
    }
    CHECK(enters == 1 && exits == 1);                        // 한 번 들어가고 한 번 나온다(벽에 막혀 더 가지 못한다)
    CHECK(enterFrame >= 0 && exitFrame > enterFrame);
    CHECK(std::fabs(enterFrame * 0.05f - 5.0f) < 0.2f);      // 영역 경계(x=5)에 도달한 시점에 들어왔다
    CHECK(std::fabs(exitFrame * 0.05f - 7.0f) < 0.2f);       // x=7을 넘을 때 나갔다
    CHECK(std::fabs(pos.x - 11.5f) < kTol);                  // 벽 앞(12 - 0.5)에서 멈췄다
    CHECK(gge_world_is_inside_trigger(w, door) == 0);
    gge_world_destroy(w);
}

// ---- 테스트 실효성 보강 ----
// 뮤테이션 테스트(연산자를 하나씩 바꾼 변이를 만들어 테스트가 잡는지 측정)에서 살아남은 변이를 잡기 위한 테스트들이다.
// 공통 원인: (1) 도형이 원점 근처에만 있어 평행이동이 소거되는 연산을 못 잡음, (2) 거부 사유(메시지)를 확인하지 않아 다른
// 검사가 대신 걸러도 통과함, (3) 경계값(한도 바로 위/아래)과 첫 항목 이외의 항목을 확인하지 않음.

bool ErrorContains(const char* fragment) { return std::strstr(gge_last_error(), fragment) != nullptr; }

void Test_PolygonRejectionsNameTheViolatedRule()
{
    GgeWorld* w = gge_world_create();
    auto sq = Rect(0, 0, 2, 2);
    CHECK(AddRoom(w, sq.data(), 2) == 0);   CHECK(ErrorContains("at least 3"));
    CHECK(AddRoom(w, nullptr, 4) == 0);     CHECK(ErrorContains("at least 3"));
    auto nan = sq; nan[3] = std::nanf("");
    CHECK(AddRoom(w, nan.data(), 4) == 0);  CHECK(ErrorContains("non-finite"));
    auto inf = sq; inf[0] = INFINITY;
    CHECK(AddRoom(w, inf.data(), 4) == 0);  CHECK(ErrorContains("non-finite"));
    const float dup[] = {0, 0, 0, 0, 2, 0, 2, 2};
    CHECK(AddRoom(w, dup, 4) == 0);         CHECK(ErrorContains("zero-length"));
    const float line[] = {0, 0, 1, 0, 2, 0};
    CHECK(AddRoom(w, line, 3) == 0);        CHECK(ErrorContains("zero area"));
    const float bowtie[] = {0, 0, 4, 2, 4, 0, 0, 1};
    CHECK(AddRoom(w, bowtie, 4) == 0);      CHECK(ErrorContains("cross"));
    const float touch[] = {0, 0, 6, 0, 6, 4, 3, 0, 0, 4};  // 꼭짓점 (3,0)이 비인접 변 (0,0)-(6,0) 위에 닿는다
    CHECK(AddRoom(w, touch, 5) == 0);       CHECK(ErrorContains("cross"));
    const float touchLater[] = {6, 4, 3, 0, 0, 4, 0, 0, 6, 0};  // 같은 도형을 돌려 시작해, 앞 변의 끝점 (3,0)이 뒤 변 (0,0)-(6,0) 위에 닿게 한다
    CHECK(AddRoom(w, touchLater, 5) == 0);  CHECK(ErrorContains("cross"));

    // 같은 직선 위에서 되돌아가는 돌기: 각각 다른 위치/길이의 변이 겹친다.
    const float spikeMid[] = {0, 0, 4, 0, 2, 0, 2, 2};
    CHECK(AddRoom(w, spikeMid, 4) == 0);    CHECK(ErrorContains("overlap"));
    const float spikeBeyond[] = {0, 0, 4, 0, -2, 0, -2, 3};  // 두 번째 변이 첫 변을 덮고 시작점 너머까지 간다
    CHECK(AddRoom(w, spikeBeyond, 4) == 0); CHECK(ErrorContains("overlap"));
    const float spikeLater[] = {0, 3, 0, 0, 4, 0, 2, 0};      // 둘째/셋째 변 쌍(첫 변이 아닌 곳)에서 되돌아간다
    CHECK(AddRoom(w, spikeLater, 4) == 0);  CHECK(ErrorContains("overlap"));

    // 유효한 도형은 받아야 한다: 한 직선 위를 지나가는(180도) 꼭짓점이 있어도, 삼각형도 유효하다.
    const float throughVertex[] = {0, 0, 2, 0, 4, 0, 4, 4, 0, 4};
    CHECK(AddRoom(w, throughVertex, 5) > 0);
    const float triangle[] = {0, 0, 4, 0, 0, 3};
    CHECK(AddRoom(w, triangle, 3) > 0);
    const float far[] = {1000, -500, 1004, -500, 1004, -496, 1000, -496};  // 원점에서 먼 곳
    CHECK(AddRoom(w, far, 4) > 0);
    gge_world_destroy(w);
}

void Test_RoomCameraRejectionsNameTheViolatedRule()
{
    GgeWorld* w = gge_world_create();
    auto sq = Rect(0, 0, 2, 2);
    auto add = [&](const float* eye, const float* target, float fov) {
        return gge_world_add_room(w, sq.data(), 4, eye, target, fov);
    };
    const float fine[3] = {4, 3, 5}, origin[3] = {0, 0, 0};
    const float nanEye[3] = {std::nanf(""), 3, 5}, nanEyeY[3] = {4, std::nanf(""), 5}, infTarget[3] = {0, INFINITY, 0};
    const float nanTargetZ[3] = {0, 0, std::nanf("")};
    CHECK(add(nanEye, origin, 0.8f) == 0);     CHECK(ErrorContains("non-finite"));
    CHECK(add(nanEyeY, origin, 0.8f) == 0);    CHECK(ErrorContains("non-finite"));
    CHECK(add(fine, infTarget, 0.8f) == 0);    CHECK(ErrorContains("non-finite"));
    CHECK(add(fine, nanTargetZ, 0.8f) == 0);   CHECK(ErrorContains("non-finite"));
    CHECK(add(fine, fine, 0.8f) == 0);         CHECK(ErrorContains("differ"));   // eye == target은 "수직" 검사와 구별되어야 한다
    CHECK(add(fine, origin, 0.0f) == 0);       CHECK(ErrorContains("fov"));
    CHECK(add(fine, origin, std::nanf("")) == 0); CHECK(ErrorContains("fov"));
    CHECK(add(fine, origin, 3.2f) == 0);       CHECK(ErrorContains("fov"));
    CHECK(add(fine, origin, 3.1f) > 0);        // 상한(pi) 바로 아래는 유효
    const float straightDown[3] = {0, 5, 0}, nearVertical[3] = {0, 1000, 0.001f}, steep[3] = {0, 10, 0.5f};
    CHECK(add(straightDown, origin, 0.8f) == 0);  CHECK(ErrorContains("straight"));
    CHECK(add(nearVertical, origin, 0.8f) == 0);  CHECK(ErrorContains("straight"));
    CHECK(add(steep, origin, 0.8f) > 0);
    gge_world_destroy(w);
}

void Test_ColliderRejectsDegenerateBoxes()
{
    GgeWorld* w = gge_world_create();
    CHECK(gge_world_add_box_collider(w, 1, 0, 1, 1) == 0);  CHECK(ErrorContains("min < max"));  // X 폭 0
    CHECK(gge_world_add_box_collider(w, 0, 1, 1, 1) == 0);  CHECK(ErrorContains("min < max"));  // Z 폭 0
    CHECK(gge_world_add_box_collider(w, 2, 0, 1, 1) == 0);  CHECK(ErrorContains("min < max"));  // X 뒤바뀜
    CHECK(gge_world_add_box_collider(w, 0, 2, 1, 1) == 0);  CHECK(ErrorContains("min < max"));  // Z 뒤바뀜
    CHECK(gge_world_add_box_collider(w, 0, 0, 1, 1) == 1);
    gge_world_destroy(w);
}

void Test_MoveCircleRejectsUnreasonableDistancesAndRadii()
{
    GgeWorld* w = gge_world_create();
    float out[2];
    // 반지름 0.5의 한도: 1024 걸음 x (0.5 x 0.5) = 256. 방향과 무관하게 이동 길이(대각선 포함)로 판정한다.
    CHECK(gge_world_move_circle(w, 0, 0, 200, 0, 0.5f, out) == 1);
    CHECK(gge_world_move_circle(w, 0, 0, 0, 200, 0.5f, out) == 1);
    CHECK(gge_world_move_circle(w, 0, 0, 150, 150, 0.5f, out) == 1);  // 길이 212 < 256
    CHECK(gge_world_move_circle(w, 0, 0, 300, 0, 0.5f, out) == 0);    CHECK(ErrorContains("too large"));
    CHECK(gge_world_move_circle(w, 0, 0, 0, 300, 0.5f, out) == 0);    CHECK(ErrorContains("too large"));
    CHECK(gge_world_move_circle(w, 0, 0, 250, 250, 0.5f, out) == 0);  CHECK(ErrorContains("too large"));  // 길이 354 > 256
    CHECK(gge_world_move_circle(w, 0, 0, 500, 0, 0.5f, out) == 0);    CHECK(ErrorContains("too large"));  // 한도의 2배
    CHECK(gge_world_move_circle(w, 0, 0, 1, 0, 0.0f, out) == 0);      CHECK(ErrorContains("radius"));     // 이동량이 작아도 반지름 0은 반지름 오류다
    CHECK(gge_world_move_circle(w, 0, 0, 1, 0, -1.0f, out) == 0);     CHECK(ErrorContains("radius"));
    CHECK(gge_world_move_circle(w, 0, 0, 1, 0, std::nanf(""), out) == 0); CHECK(ErrorContains("non-finite"));
    // 반지름이 클수록 한도도 비례해 커진다(반지름 2: 1024 단위).
    CHECK(gge_world_move_circle(w, 0, 0, 900, 0, 2.0f, out) == 1);
    CHECK(gge_world_move_circle(w, 0, 0, 1100, 0, 2.0f, out) == 0);
    gge_world_destroy(w);
}

void Test_CameraCutLatchUsesTheDirectionRecordedAtTheCut()
{
    // 컷 직후 입력 방향이 대각선(45도)이어도 같은 규칙이 적용된다: 그 방향에서 60도 안쪽이면 고정, 바깥이면 해제.
    GgeWorld* w = gge_world_create();
    AddCamRoom(w, 0, -5, 10, 5, kEyeNorth);   // 방 1: 앞 = -Z, 오른쪽 = +X  => 월드 이동 = (ix, -iy)
    AddCamRoom(w, 10, -5, 20, 5, kEyeEast);   // 방 2: 앞 = +X, 오른쪽 = +Z  => 월드 이동 = (iy, ix)
    gge_world_update_active_room(w, 5, 0);
    const float h = 0.70710678f;
    CHECK(Near(Input(w, h, h), h, -h));
    gge_world_update_active_room(w, 15, 0);                       // 대각선으로 누른 채 컷
    CHECK(Near(Input(w, h, h), h, -h));                           // 컷 직후 첫 입력: 방 1 기준 + 이 방향(45도)을 기록
    const float c50 = 0.99619470f, s50 = -0.08715574f;            // 기록 방향에서 50도 돌린 방향(각도 -5도)
    CHECK(Near(Input(w, c50, s50), c50, -s50));                   // 60도 안쪽: 여전히 방 1 기준
    const float c70 = 0.90630779f, s70 = -0.42261826f;            // 기록 방향에서 70도 돌린 방향(각도 -25도)
    CHECK(Near(Input(w, c70, s70), s70, c70));                    // 60도 바깥: 방 2 기준으로 옮긴다
    gge_world_destroy(w);
}

void Test_CenterInsideABoxExitsThroughTheNearestFace()
{
    // 상자의 min이 0이 아니어야 면까지 거리 계산(center - min, max - center)의 부호/항을 구별할 수 있다.
    GgeWorld* w = gge_world_create();
    gge_world_add_box_collider(w, 10, 10, 14, 14);
    const float r = 0.5f;
    P2 minX = Move(w, 10.5f, 12, 0, 0, r);  CHECK(std::fabs(minX.x - 9.5f) < kTol && std::fabs(minX.z - 12) < kTol);
    P2 maxX = Move(w, 13.5f, 12, 0, 0, r);  CHECK(std::fabs(maxX.x - 14.5f) < kTol && std::fabs(maxX.z - 12) < kTol);
    P2 minZ = Move(w, 12, 10.5f, 0, 0, r);  CHECK(std::fabs(minZ.x - 12) < kTol && std::fabs(minZ.z - 9.5f) < kTol);
    P2 maxZ = Move(w, 12, 13.5f, 0, 0, r);  CHECK(std::fabs(maxZ.x - 12) < kTol && std::fabs(maxZ.z - 14.5f) < kTol);
    gge_world_destroy(w);
}

void Test_InsideCornerOfTwoWallsNeverPenetrates()
{
    GgeWorld* w = gge_world_create();
    gge_world_add_box_collider(w, 4, -20, 5, 20);   // 동쪽 벽(x = 4가 면)
    gge_world_add_box_collider(w, -20, 4, 20, 5);   // 남쪽 벽(z = 4가 면)
    const float r = 1.0f;
    struct Case { float x, z, dx, dz; };
    const Case cases[] = {{0, 0, 20, 20}, {0, 0, 6, 3}, {0, 0, 3, 6}, {3.5f, 0, 0, 20}, {-10, -10, 30, 30}, {2, 2, 0.5f, 0.5f}};
    for (const Case& c : cases) {
        P2 p = Move(w, c.x, c.z, c.dx, c.dz, r);
        CHECK(DistanceToBox(p, 4, -20, 5, 20) >= r - kTol);
        CHECK(DistanceToBox(p, -20, 4, 20, 5) >= r - kTol);
    }
    P2 corner = Move(w, 0, 0, 20, 20, r);              // 모서리 안쪽으로 곧장: 양쪽 벽 앞(3, 3)에서 멈춘다
    CHECK(std::fabs(corner.x - 3.0f) < kTol && std::fabs(corner.z - 3.0f) < kTol);
    gge_world_destroy(w);
}

// 별 모양(중심에서 본 각도순 꼭짓점) 다각형은 항상 단순하고, 점의 내외부를 "점이 속한 쐐기의 변에서 중심과 같은 쪽인가"로
// 독립적으로 판정할 수 있다. 엔진의 교차 횟수 방식과 다른 방법이라 같은 실수를 공유하지 않는다.
void Test_PolygonContainmentMatchesAnIndependentReference()
{
    uint32_t seed = 987654321u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<double>(seed >> 8) / static_cast<double>(1 << 24);  // [0, 1)
    };
    const double kTwoPi = 6.283185307179586;
    int polygons = 0, points = 0, insideCount = 0;
    for (int iter = 0; iter < 300; ++iter) {
        int n = 4 + static_cast<int>(rnd() * 8);                       // 4..11각형
        double cx = (iter % 3 == 0) ? 0.0 : (rnd() - 0.5) * 2000.0;     // 원점에서 먼 도형도 섞는다
        double cz = (iter % 3 == 0) ? 0.0 : (rnd() - 0.5) * 2000.0;
        double scale = 1.0 + rnd() * 9.0;
        std::vector<double> ang(n), rad(n);
        std::vector<float> xz;
        for (int i = 0; i < n; ++i) {
            ang[i] = (i + (rnd() - 0.5) * 0.4) * kTwoPi / n;            // 각도순을 유지하는 지터(쐐기 각도 < 180도)
            rad[i] = scale * (0.4 + rnd() * 0.6);
            xz.push_back(static_cast<float>(cx + rad[i] * std::cos(ang[i])));
            xz.push_back(static_cast<float>(cz + rad[i] * std::sin(ang[i])));
        }
        std::vector<float> submitted = xz;
        if (iter % 2 == 1) {                                            // 감김 방향을 뒤집어도 같아야 한다
            for (int i = 0; i < n; ++i) {
                submitted[2 * i] = xz[2 * (n - 1 - i)];
                submitted[2 * i + 1] = xz[2 * (n - 1 - i) + 1];
            }
        }
        GgeWorld* w = gge_world_create();
        int room = AddRoom(w, submitted.data(), n);
        CHECK(room == 1);
        if (room != 1) {
            gge_world_destroy(w);
            continue;
        }
        ++polygons;

        for (int k = 0; k < 40; ++k) {
            double px = cx + (rnd() - 0.5) * 2.6 * scale, pz = cz + (rnd() - 0.5) * 2.6 * scale;
            double fx = static_cast<float>(px), fz = static_cast<float>(pz);
            // 점이 속한 쐐기: 중심에서 본 각도가 [ang[i], ang[i+1]) 안
            double a = std::atan2(fz - cz, fx - cx);
            int wedge = -1;
            for (int i = 0; i < n; ++i) {
                double gap = std::fmod(ang[(i + 1) % n] - ang[i] + 2 * kTwoPi, kTwoPi);
                double off = std::fmod(a - ang[i] + 2 * kTwoPi, kTwoPi);
                if (off < gap) { wedge = i; break; }
            }
            CHECK(wedge >= 0);
            if (wedge < 0) continue;
            double ax = xz[2 * wedge], az = xz[2 * wedge + 1];
            double bx = xz[2 * ((wedge + 1) % n)], bz = xz[2 * ((wedge + 1) % n) + 1];
            double edgeLen = std::hypot(bx - ax, bz - az);
            double sp = (bx - ax) * (fz - az) - (bz - az) * (fx - ax);   // 점이 변의 어느 쪽인가
            double sc = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);   // 중심이 변의 어느 쪽인가
            if (std::fabs(sp) / edgeLen < 0.02 * scale) continue;        // 경계 근처는 판정이 모호하다
            bool expected = sp * sc > 0;
            int found = gge_world_find_room(w, static_cast<float>(fx), static_cast<float>(fz));
            ++points;
            if (expected) ++insideCount;
            if ((found == 1) != expected) {
                std::printf("  mismatch: polygon %d n=%d point (%g, %g) expected %d\n", iter, n, fx, fz, expected ? 1 : 0);
            }
            CHECK((found == 1) == expected);
        }
        gge_world_destroy(w);
    }
    CHECK(polygons == 300);
    CHECK(points > 5000);                                  // 충분히 많은 점을 비교했다
    CHECK(insideCount > 1500 && insideCount < points - 1500);  // 안/밖이 모두 충분히 섞여 있다(한쪽만 맞는 구현을 거른다)
}

// ---- 클립 샘플링: 모든 키/모든 축 ----
std::vector<float> BoneMatrix(GgeRenderer* r, int object, int bones, int bone)
{
    std::vector<float> m(static_cast<size_t>(bones) * 16);
    CHECK(gge_object_get_pose(r, object, m.data(), bones) == bones);
    return std::vector<float>(m.begin() + bone * 16, m.begin() + bone * 16 + 16);
}

void Test_ClipSamplesEveryKeySegmentOnEveryAxis()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    // 이동: 네 키가 모든 축(x,y,z)에서 서로 다른 값을 갖는다(한 축만 쓰면 성분 인덱스 오류를 못 잡는다).
    int clip = CreateClip(r, 3.0f, {{0, 0, {0, 1, 2, 3}, {1, 2, 3, 10, 20, 30, 100, 200, 300, -5, -6, -7}}});
    CHECK(clip > 0);
    auto expectAt = [&](float t, V3 e) {
        CHECK(gge_object_set_pose(r, obj, clip, t, 0, 0, 0) == 1);
        V3 got = BonePositions(r, obj, 3)[0];
        if (!Near(got, e, 1e-3f)) std::printf("  t=%g got (%g,%g,%g) expected (%g,%g,%g)\n", t, got.x, got.y, got.z, e.x, e.y, e.z);
        CHECK(Near(got, e, 1e-3f));
    };
    expectAt(0.0f, {1, 2, 3});
    expectAt(0.25f, {3.25f, 6.5f, 9.75f});
    expectAt(0.5f, {5.5f, 11, 16.5f});
    expectAt(1.5f, {55, 110, 165});
    expectAt(2.0f, {100, 200, 300});
    expectAt(2.5f, {47.5f, 97, 146.5f});
    expectAt(3.0f, {-5, -6, -7});
    expectAt(9.0f, {-5, -6, -7});     // 끝 너머는 마지막 키
    expectAt(-1.0f, {1, 2, 3});       // 시작 앞은 첫 키
    gge_renderer_destroy(r);
}

void Test_ScaleChannelSamplesEveryKeyOnEveryAxis()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    int clip = CreateClip(r, 2.0f, {{0, 2, {0, 1, 2}, {1, 2, 3, 2, 4, 6, 4, 1, 2}}});
    CHECK(clip > 0);
    auto expectScale = [&](float t, float sx, float sy, float sz) {
        CHECK(gge_object_set_pose(r, obj, clip, t, 0, 0, 0) == 1);
        std::vector<float> m = BoneMatrix(r, obj, 3, 0);
        CHECK(std::fabs(m[0] - sx) < 1e-4f && std::fabs(m[5] - sy) < 1e-4f && std::fabs(m[10] - sz) < 1e-4f);
    };
    expectScale(0.0f, 1, 2, 3);
    expectScale(0.5f, 1.5f, 3, 4.5f);
    expectScale(1.0f, 2, 4, 6);
    expectScale(1.5f, 3, 2.5f, 4);
    expectScale(2.0f, 4, 1, 2);
    gge_renderer_destroy(r);
}

void Test_RotationKeysAreNormalizedAndSlerpedPerSegment()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    // Z축 각도 0, 60, 90, 180도의 쿼터니언을 일부러 정규화하지 않은 크기(2, 0.5, 3, 1)로 준다. 엔진이 모든 키를 정규화해야 한다.
    auto q = [](float angle, float scale) { return std::array<float, 4>{0, 0, std::sin(angle / 2) * scale, std::cos(angle / 2) * scale}; };
    std::vector<float> values;
    for (auto k : {q(0, 2.0f), q(kPi / 3, 0.5f), q(kPi / 2, 3.0f), q(kPi, 1.0f)})
        values.insert(values.end(), k.begin(), k.end());
    int clip = CreateClip(r, 3.0f, {{0, 1, {0, 1, 2, 3}, values}});
    CHECK(clip > 0);
    auto expectAngle = [&](float t, float angle) {
        CHECK(gge_object_set_pose(r, obj, clip, t, 0, 0, 0) == 1);
        std::vector<float> m = BoneMatrix(r, obj, 3, 0);
        // Z 회전(행 벡터): 첫 행 = (cos, sin, 0), 둘째 행 = (-sin, cos, 0). 정규화가 틀리면 행의 길이가 1이 아니다.
        if (std::fabs(m[0] - std::cos(angle)) > 1e-3f) std::printf("  t=%g got cos %g expected %g\n", t, m[0], std::cos(angle));
        CHECK(std::fabs(m[0] - std::cos(angle)) < 1e-3f && std::fabs(m[1] - std::sin(angle)) < 1e-3f);
        CHECK(std::fabs(m[4] + std::sin(angle)) < 1e-3f && std::fabs(m[5] - std::cos(angle)) < 1e-3f);
    };
    expectAngle(0.0f, 0.0f);
    expectAngle(0.5f, kPi / 6);               // 구간 0: 0 -> 60도의 중간
    expectAngle(1.0f, kPi / 3);
    expectAngle(1.5f, 5 * kPi / 12);          // 구간 1: 60 -> 90도의 중간
    expectAngle(2.5f, 3 * kPi / 4);           // 구간 2: 90 -> 180도의 중간
    expectAngle(3.0f, kPi);
    gge_renderer_destroy(r);
}

void Test_SkeletonRejectsNonFiniteRestTransformsIndividually()
{
    GgeRenderer* r = NewRenderer();
    ChainData ok = MakeChain(3);
    ChainData t = ok; t.translation[4] = std::nanf("");
    CHECK(CreateSkeleton(r, t) == 0);  CHECK(ErrorContains("rest transform"));
    ChainData q = ok; q.rotation[5] = INFINITY;
    CHECK(CreateSkeleton(r, q) == 0);  CHECK(ErrorContains("rest transform"));
    ChainData s = ok; s.scale[7] = std::nanf("");
    CHECK(CreateSkeleton(r, s) == 0);  CHECK(ErrorContains("rest transform"));
    gge_renderer_destroy(r);
}

void Test_ClipDurationRules()
{
    GgeRenderer* r = NewRenderer();
    const KeyChannel pose{0, 0, {0}, {0, 1, 0}};
    CHECK(CreateClip(r, 0.0f, {pose}) > 0);                       // 길이 0(키 하나)은 유효한 정지 포즈다
    CHECK(CreateClip(r, std::nanf(""), {pose}) == 0);  CHECK(ErrorContains("duration"));
    CHECK(CreateClip(r, INFINITY, {pose}) == 0);       CHECK(ErrorContains("duration"));
    CHECK(CreateClip(r, -0.5f, {pose}) == 0);          CHECK(ErrorContains("duration"));
    gge_renderer_destroy(r);
}

// ---- 렌더러 인자 검증: 경계값과 항목별 확인 ----
void Test_RendererArgumentValidationTables()
{
    CHECK(gge_renderer_create(10, 0, 1) == nullptr);
    CHECK(gge_renderer_create(0, 10, 1) == nullptr);
    CHECK(gge_renderer_create(-5, 10, 1) == nullptr);
    CHECK(gge_renderer_create(10, -5, 1) == nullptr);
    GgeRenderer* ok = gge_renderer_create(1, 1, 1);
    CHECK(ok != nullptr);
    gge_renderer_destroy(ok);

    GgeRenderer* r = NewRenderer();
    float v[3 * 8] = {};
    for (int i = 0; i < 3; ++i) v[i * 8 + 5] = 1;
    uint32_t idx[3] = {0, 1, 2}, atEnd[3] = {0, 1, 3}, four[4] = {0, 1, 2, 0};
    CHECK(gge_mesh_create(r, v, 3, idx, 3) > 0);
    CHECK(gge_mesh_create(r, v, 3, nullptr, 3) == 0);
    CHECK(gge_mesh_create(r, v, 0, idx, 3) == 0);
    CHECK(gge_mesh_create(r, v, -1, idx, 3) == 0);
    CHECK(gge_mesh_create(r, v, 3, idx, 0) == 0);
    CHECK(gge_mesh_create(r, v, 3, idx, -3) == 0);
    CHECK(gge_mesh_create(r, v, 3, four, 4) == 0);
    CHECK(gge_mesh_create(r, v, 3, atEnd, 3) == 0);  // 인덱스 == 정점 수: 경계 바로 밖

    std::vector<uint8_t> pixels(4097u * 4, 200);
    CHECK(gge_texture_create(r, pixels.data(), 4096, 1) > 0);   // 한도 정확히: 허용
    CHECK(gge_texture_create(r, pixels.data(), 1, 4096) > 0);
    CHECK(gge_texture_create(r, pixels.data(), 4097, 1) == 0);
    CHECK(gge_texture_create(r, pixels.data(), 1, 4097) == 0);
    CHECK(gge_texture_create(r, pixels.data(), 0, 1) == 0);
    CHECK(gge_texture_create(r, pixels.data(), 1, 0) == 0);
    CHECK(gge_texture_create(r, pixels.data(), -1, 1) == 0);
    CHECK(gge_texture_create(r, pixels.data(), 1, -1) == 0);
    CHECK(gge_texture_create(r, nullptr, 1, 1) == 0);
    gge_renderer_destroy(r);
}

void Test_SkinWeightValidationChecksEveryVertexAndSlot()
{
    GgeRenderer* r = NewRenderer();
    float v[3 * 8] = {};
    for (int i = 0; i < 3; ++i) v[i * 8 + 5] = 1;
    const uint32_t tri[3] = {0, 1, 2};
    uint32_t joints[12] = {0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0};
    float weights[12] = {0.5f, 0.5f, 0, 0, 0.5f, 0.5f, 0, 0, 0.5f, 0.5f, 0, 0};
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, weights, tri, 3) > 0);

    float w2 = weights[11];
    weights[11] = -0.1f;                                           // 셋째 정점의 넷째 슬롯: 음수
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, weights, tri, 3) == 0);  CHECK(ErrorContains("weight"));
    weights[11] = w2;
    weights[6] = std::nanf("");                                    // 둘째 정점의 셋째 슬롯: NaN
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, weights, tri, 3) == 0);  CHECK(ErrorContains("weight"));
    weights[6] = 0;
    joints[5] = 64;                                                // 둘째 정점의 둘째 슬롯: 가중치 > 0인데 본 번호가 상한(64) 이상
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, weights, tri, 3) == 0);  CHECK(ErrorContains("joint index"));
    joints[5] = 63;                                                // 마지막 허용 번호
    CHECK(gge_mesh_create_skinned(r, v, 3, joints, weights, tri, 3) > 0);

    // 가중치 0인 슬롯의 본 번호는 쓰이지 않으므로 아무 값이어도 되고, 스켈레톤 호환 검사에도 영향을 주지 않는다.
    uint32_t garbage[12] = {0, 99, 7, 63, 0, 99, 7, 63, 0, 99, 7, 63};
    float onlyFirst[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    int mesh = gge_mesh_create_skinned(r, v, 3, garbage, onlyFirst, tri, 3);
    CHECK(mesh > 0);
    int obj = gge_object_create(r, mesh, 0, 0);
    CHECK(gge_object_set_skeleton(r, obj, ChainSkeleton(r)) == 1);  // 본 3개: 실제로 쓰는 번호는 0뿐
    gge_renderer_destroy(r);
}

void Test_VertexWithoutSkinWeightsFollowsBoneZero()
{
    GgeRenderer* r = NewRenderer(64, 48);
    gge_renderer_set_camera(r, 0, 0, 6, 0, 0, 0, 0.8f);
    const float v[] = {
        -1, -1, 0, 0, 0, 1, 0, 1,
         1, -1, 0, 0, 0, 1, 1, 1,
         0,  1, 0, 0, 0, 1, 0.5f, 0,
    };
    const uint32_t tri[3] = {0, 1, 2}, joints[12] = {};
    const float noInfluence[12] = {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};   // 셋째 정점은 영향이 전혀 없다
    const float allBoneZero[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    int meshA = gge_mesh_create_skinned(r, v, 3, joints, noInfluence, tri, 3);
    int meshB = gge_mesh_create_skinned(r, v, 3, joints, allBoneZero, tri, 3);
    CHECK(meshA > 0 && meshB > 0);
    int skeleton = ChainSkeleton(r);
    int clip = CreateClip(r, 1.0f, {{0, 0, {0, 1}, {0, 0, 0, 0, 1.0f, 0}}});   // 본 0이 위로 1 이동
    auto frame = [&](int mesh, float time) {
        int obj = gge_object_create(r, mesh, 0, 0);
        gge_object_set_skeleton(r, obj, skeleton);
        gge_object_set_pose(r, obj, clip, time, 0, 0, 0);
        gge_object_set_color(r, obj, 0.9f, 0.6f, 0.3f);
        Image img = Render(r);
        gge_object_destroy(r, obj);
        return img;
    };
    Image moved = frame(meshA, 1.0f);
    CHECK(moved.bgra == frame(meshB, 1.0f).bgra);       // 영향 없는 정점은 본 0에 고정: 모든 정점이 본 0인 메시와 같다
    CHECK(moved.bgra != frame(meshA, 0.0f).bgra);       // (포즈가 실제로 화면을 바꾼다)
    gge_renderer_destroy(r);
}

void Test_DestroyObjectKeepsUnrelatedObjects()
{
    GgeRenderer* r = NewRenderer();
    int mesh = CubeMesh(r);
    int parent = gge_object_create(r, mesh, 0, 0);
    int child = gge_object_create(r, mesh, 0, parent);
    int grandchild = gge_object_create(r, mesh, 0, child);
    int unrelatedRoot = gge_object_create(r, mesh, 0, 0);
    int unrelatedChild = gge_object_create(r, mesh, 0, unrelatedRoot);
    CHECK(gge_object_destroy(r, parent) == 1);
    for (int gone : {parent, child, grandchild})
        CHECK(gge_object_set_color(r, gone, 1, 1, 1) == 0);          // 서브트리는 사라졌다
    for (int kept : {unrelatedRoot, unrelatedChild})
        CHECK(gge_object_set_color(r, kept, 1, 1, 1) == 1);          // 관련 없는 오브젝트는 그대로다
    gge_renderer_destroy(r);
}

void Test_ClipBoneRangeIsCheckedAgainstTheSkeletonBothWays()
{
    GgeRenderer* r = NewRenderer();
    int skeleton = ChainSkeleton(r);                       // 본 3개(0..2)
    int clipOk = CreateClip(r, 1.0f, {ZRotation(2, 0, 1)});
    int clipBad = CreateClip(r, 1.0f, {ZRotation(3, 0, 1)});  // 본 번호 == 본 수: 경계 바로 밖
    CHECK(clipOk > 0 && clipBad > 0);

    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    CHECK(gge_object_set_skeleton(r, obj, skeleton) == 1);
    CHECK(gge_object_set_pose(r, obj, clipOk, 0, 0, 0, 0) == 1);
    CHECK(gge_object_set_pose(r, obj, clipBad, 0, 0, 0, 0) == 0);  CHECK(ErrorContains("clip targets bone"));

    // 반대 순서: 스켈레톤이 없을 때 지정한 클립은 스켈레톤을 붙이는 시점에 검사된다.
    int obj2 = gge_object_create(r, SkinnedStrip(r), 0, 0);
    CHECK(gge_object_set_pose(r, obj2, clipBad, 0, 0, 0, 0) == 1);
    CHECK(gge_object_set_skeleton(r, obj2, skeleton) == 0);        CHECK(ErrorContains("clip targets bone"));
    CHECK(gge_object_set_pose(r, obj2, 0, 0, 0, 0, 0) == 1);
    CHECK(gge_object_set_skeleton(r, obj2, skeleton) == 1);
    gge_renderer_destroy(r);
}

void Test_SkinnedMeshWithoutSkeletonRendersInBindPose()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);    // 스켈레톤 없음: 정점을 바인드 포즈 그대로 그린다
    gge_object_set_color(r, obj, 0.9f, 0.6f, 0.3f);
    gge_renderer_set_camera(r, 0, 1, 5, 0, 1, 0, 0.8f);
    Image img = Render(r);
    CHECK(img.At(img.width / 2, img.height / 2) != img.At(0, 0));   // 그려졌다(오류 없이)
    gge_renderer_destroy(r);
}

void Test_BackgroundColorFollowsColorBits()
{
    GgeRenderer* r = NewRenderer(8, 8);
    auto background = [&]() { Image img = Render(r); uint32_t px = img.At(4, 4); return std::array<int, 3>{Red(px), Green(px), Blue(px)}; };
    CHECK((background() == std::array<int, 3>{13, 13, 20}));        // 기본(8비트): (0.05, 0.05, 0.08) x 255
    CHECK(gge_renderer_set_retro(r, 0, 5, 0, 0) == 1);
    CHECK((background() == std::array<int, 3>{16, 16, 16}));        // 5비트: 31단계로 양자화
    CHECK(gge_renderer_set_retro(r, 0, 1, 0, 0) == 1);
    CHECK((background() == std::array<int, 3>{0, 0, 0}));           // 1비트: 2단계
    CHECK(gge_renderer_set_retro(r, 0, 8, 0, 0) == 1);
    CHECK((background() == std::array<int, 3>{13, 13, 20}));        // 8비트로 돌아오면 양자화가 없다
    gge_renderer_destroy(r);
}

// ---- 로더 경계 ----
void Test_ObjIndexBoundaries()
{
    fs::path dir = TempDir();
    GgeRenderer* r = NewRenderer();
    auto load = [&](const char* name, const std::string& text) {
        fs::path p = dir / name;
        WriteText(p, text);
        return gge_mesh_load_obj(r, p.string().c_str());
    };
    CHECK(load("idx_ok.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n") > 0);
    CHECK(load("idx_last.obj", "v 0 0 0\nf 1 1 1\n") > 0);           // 마지막 정점(1)은 유효
    CHECK(load("idx_past.obj", "v 0 0 0\nf 1 1 2\n") == 0);          // 정점 수 바로 다음(2): 경계 밖
    CHECK(ErrorContains("range"));
    CHECK(load("idx_zero.obj", "v 0 0 0\nf 0 1 1\n") == 0);          // 0은 유효한 인덱스가 아니다
    CHECK(ErrorContains("range"));
    CHECK(load("idx_neg_last.obj", "v 0 0 0\nf -1 -1 -1\n") > 0);    // -1 = 마지막 정점
    CHECK(load("idx_neg_past.obj", "v 0 0 0\nf -1 -1 -2\n") == 0);   // -2는 정점 수(1)보다 한 칸 더 뒤
    CHECK(ErrorContains("range"));
    CHECK(load("idx_neg_far.obj", "v 0 0 0\nf -1 -1 -9\n") == 0);
    gge_renderer_destroy(r);
}

void Test_GgmHeaderAndIndexBoundaries()
{
    fs::path dir = TempDir();
    GgeRenderer* r = NewRenderer();
    // 헤더에 적은 개수와 실제로 쓴 데이터가 다를 수 있게 만들어 각 검사를 따로 건드린다.
    auto make = [&](const char* name, uint32_t headerVertices, uint32_t headerIndices, uint32_t realVertices,
                    const std::vector<uint32_t>& indices) {
        fs::path p = dir / name;
        std::ofstream f(p, std::ios::binary);
        f.write("GGM1", 4);
        f.write(reinterpret_cast<const char*>(&headerVertices), 4);
        f.write(reinterpret_cast<const char*>(&headerIndices), 4);
        std::vector<float> vertices(static_cast<size_t>(realVertices) * 8, 0.5f);
        f.write(reinterpret_cast<const char*>(vertices.data()), static_cast<std::streamsize>(vertices.size() * 4));
        f.write(reinterpret_cast<const char*>(indices.data()), static_cast<std::streamsize>(indices.size() * 4));
        f.close();  // 닫아서 내용을 반영한 뒤에 읽는다
        return gge_mesh_load_ggm(r, p.string().c_str());
    };
    const uint32_t kMax = 64u * 1024 * 1024;
    CHECK(make("ggm_ok.ggm", 3, 3, 3, {0, 1, 2}) > 0);
    CHECK(make("ggm_v0.ggm", 0, 3, 0, {0, 1, 2}) == 0);        CHECK(ErrorContains("invalid GGM1 header"));
    CHECK(make("ggm_i0.ggm", 3, 0, 3, {}) == 0);               CHECK(ErrorContains("invalid GGM1 header"));
    CHECK(make("ggm_i4.ggm", 3, 4, 3, {0, 1, 2, 0}) == 0);     CHECK(ErrorContains("invalid GGM1 header"));   // 3의 배수가 아님
    CHECK(make("ggm_vbig.ggm", kMax + 1, 3, 3, {0, 1, 2}) == 0);   CHECK(ErrorContains("invalid GGM1 header")); // 정점 수 상한 초과
    CHECK(make("ggm_ibig.ggm", 3, kMax + 2, 3, {0, 1, 2}) == 0);   CHECK(ErrorContains("invalid GGM1 header")); // (kMax+2)는 3의 배수
    CHECK(make("ggm_edge.ggm", 3, 3, 3, {0, 1, 3}) == 0);      CHECK(ErrorContains("out of range"));         // 인덱스 == 정점 수
    CHECK(make("ggm_short.ggm", 3, 6, 3, {0, 1, 2}) == 0);     CHECK(ErrorContains("does not match"));       // 헤더보다 짧은 파일
    CHECK(make("ggm_long.ggm", 3, 3, 3, {0, 1, 2, 0}) == 0);   CHECK(ErrorContains("does not match"));       // 헤더보다 긴 파일
    gge_renderer_destroy(r);
}

void Test_TextureLoadEnforcesTheSizeLimitPerDimension()
{
    fs::path dir = TempDir();
    GgeRenderer* r = NewRenderer();
    auto load = [&](const char* name, int w, int h) {
        fs::path p = dir / name;
        WriteBmp(p, w, h, std::vector<uint8_t>(static_cast<size_t>(w) * h * 3, 128));
        return gge_texture_load(r, p.string().c_str());
    };
    CHECK(load("tex_1x1.bmp", 1, 1) > 0);
    CHECK(load("tex_4096x1.bmp", 4096, 1) > 0);                      // 한도 정확히: 허용
    CHECK(load("tex_1x4096.bmp", 1, 4096) > 0);
    CHECK(load("tex_4097x1.bmp", 4097, 1) == 0);  CHECK(ErrorContains("too large"));   // 가로만 초과
    CHECK(load("tex_1x4097.bmp", 1, 4097) == 0);  CHECK(ErrorContains("too large"));   // 세로만 초과
    gge_renderer_destroy(r);
}

// 임의 축 회전: x,y,z,w 네 성분이 모두 0이 아닌 쿼터니언 키. Z축 회전만 쓰면 x,y 성분이 항상 0이라 성분 인덱스 오류를 못 잡는다.
// 기대값은 엔진 코드와 독립적으로(표준 slerp 공식 + 쿼터니언 -> 회전 행렬) 계산한다.
struct Quat { double x, y, z, w; };

Quat Normalized(Quat q)
{
    double len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}

Quat SlerpRef(Quat a, Quat b, double t)
{
    double dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0) { b = {-b.x, -b.y, -b.z, -b.w}; dot = -dot; }   // 짧은 쪽으로 돈다
    double theta = std::acos(std::fmin(1.0, dot)), s = std::sin(theta);
    double ka = s < 1e-6 ? 1 - t : std::sin((1 - t) * theta) / s, kb = s < 1e-6 ? t : std::sin(t * theta) / s;
    return {ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z, ka * a.w + kb * b.w};
}

// 행 벡터 규약의 3x3(행 우선): 열 벡터 회전 행렬 R의 전치. 엔진의 본 행렬 앞 3x3(m[0..2], m[4..6], m[8..10])과 비교한다.
std::array<double, 9> RotationRows(Quat q)
{
    q = Normalized(q);
    double x = q.x, y = q.y, z = q.z, w = q.w;
    return {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
            2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
            2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
}

void Test_RotationKeysOnArbitraryAxesMatchAnIndependentReference()
{
    GgeRenderer* r = NewRenderer();
    int obj = gge_object_create(r, SkinnedStrip(r), 0, 0);
    gge_object_set_skeleton(r, obj, ChainSkeleton(r));
    // 정규화하지 않은 크기로 준다(엔진이 키마다 정규화해야 한다). 연속한 키의 내적이 양수/음수인 경우를 모두 섞는다.
    const Quat keys[4] = {{0.1, 0.2, 0.3, 0.9}, {0.5, -0.4, 0.2, 1.4}, {-0.3, 0.6, 0.1, -0.65}, {0.2, 0.2, -0.7, 0.6}};
    std::vector<float> values;
    for (const Quat& k : keys)
        values.insert(values.end(), {static_cast<float>(k.x), static_cast<float>(k.y), static_cast<float>(k.z), static_cast<float>(k.w)});
    int clip = CreateClip(r, 3.0f, {{0, 1, {0, 1, 2, 3}, values}});
    CHECK(clip > 0);

    auto expectAt = [&](float t, Quat expected) {
        CHECK(gge_object_set_pose(r, obj, clip, t, 0, 0, 0) == 1);
        std::vector<float> m = BoneMatrix(r, obj, 3, 0);
        std::array<double, 9> e = RotationRows(expected);
        const int idx[9] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
        bool ok = true;
        for (int i = 0; i < 9; ++i) ok = ok && std::fabs(m[idx[i]] - e[i]) < 2e-3;
        if (!ok) std::printf("  t=%g rotation differs from the reference\n", t);
        CHECK(ok);
    };
    for (int k = 0; k < 4; ++k) expectAt(static_cast<float>(k), Normalized(keys[k]));                       // 키 위치
    for (int k = 0; k < 3; ++k) {
        expectAt(k + 0.5f, SlerpRef(Normalized(keys[k]), Normalized(keys[k + 1]), 0.5));                        // 구간 중간
        expectAt(k + 0.25f, SlerpRef(Normalized(keys[k]), Normalized(keys[k + 1]), 0.25));
    }
    gge_renderer_destroy(r);
}

// ---- 7단계: 조명 / 알파 / 그림자 ----
// 밝기 규약(gge.h): 앰비언트 + 방향광색 * max(0, N.-L) + 점광원 합. 각 테스트는 손으로 계산한 값과 비교한다(채널 오차 +-2).
struct Px { int r, g, b; };

Px PixelAt(const Image& img, int x, int y)
{
    uint32_t p = img.At(x, y);
    return {Red(p), Green(p), Blue(p)};
}

Px Center(const Image& img) { return PixelAt(img, img.width / 2, img.height / 2); }

bool Close(Px a, Px b, int tol = 2)
{
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol;
}

Px Expect(float r, float g, float b)
{
    return {static_cast<int>(std::lround(r * 255)), static_cast<int>(std::lround(g * 255)), static_cast<int>(std::lround(b * 255))};
}

void Report(const char* what, Px actual, Px expected)
{
    std::printf("  %s: actual (%d,%d,%d) expected (%d,%d,%d)\n", what, actual.r, actual.g, actual.b, expected.r,
                expected.g, expected.b);
}

#define CHECK_PX(what, actual, expected)                                   \
    do {                                                                   \
        Px a_ = (actual), e_ = (expected);                                 \
        if (!Close(a_, e_)) Report(what, a_, e_);                          \
        CHECK(Close(a_, e_));                                              \
    } while (0)

const float kBackgroundColor[3] = {0.05f, 0.05f, 0.08f};  // 렌더러의 지우기 색(8비트로는 13,13,20)

// 월드 점이 화면(렌더 타깃 픽셀)의 어디에 찍히는지. 렌더러가 쓰는 오른손 look-at + 세로 시야각 원근을 따로 풀었다.
bool ProjectToPixel(const float eye[3], const float target[3], float fovY, int w, int h, const float p[3], int& px, int& py)
{
    auto sub = [](const float* a, const float* b) { return std::array<double, 3>{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto dot = [](const std::array<double, 3>& a, const std::array<double, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto cross = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
        return std::array<double, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    auto norm = [&](std::array<double, 3> a) {
        double l = std::sqrt(dot(a, a));
        return std::array<double, 3>{a[0] / l, a[1] / l, a[2] / l};
    };
    auto f = norm(sub(target, eye));
    auto right = norm(cross(f, {0, 1, 0}));
    auto up = cross(right, f);
    auto d = sub(p, eye);
    double z = dot(d, f);
    if (z <= 0) return false;
    double tanHalf = std::tan(fovY / 2), aspect = static_cast<double>(w) / h;
    double ndcX = dot(d, right) / (z * tanHalf * aspect), ndcY = dot(d, up) / (z * tanHalf);
    px = static_cast<int>(std::floor((ndcX * 0.5 + 0.5) * w));
    py = static_cast<int>(std::floor((1 - (ndcY * 0.5 + 0.5)) * h));
    return px >= 0 && px < w && py >= 0 && py < h;
}

void SetDirectional(GgeRenderer* r, float dx, float dy, float dz, float cr, float cg, float cb)
{
    const float dir[3] = {dx, dy, dz}, color[3] = {cr, cg, cb};
    CHECK(gge_renderer_set_directional_light(r, dir, color) == 1);
}

int AddPoint(GgeRenderer* r, float x, float y, float z, float cr, float cg, float cb, float range)
{
    const float pos[3] = {x, y, z}, color[3] = {cr, cg, cb};
    int id = gge_light_create_point(r, pos, color, range);
    CHECK(id > 0);
    return id;
}

// 정면 사각형(+Z 면, 한 변 2*half)을 카메라 (0,0,5)에서 본다. 화면 중앙이 사각형 중앙이다.
int FacingQuad(GgeRenderer* r, float cr, float cg, float cb, float z = 0, float half = 2.0f)
{
    int obj = gge_object_create(r, QuadMesh(r, half), 0, 0);
    CHECK(obj > 0);
    CHECK(gge_object_set_color(r, obj, cr, cg, cb) == 1);
    Place(r, obj, 0, 0, z);
    gge_renderer_set_camera(r, 0, 0, 5, 0, 0, 0, 0.8f);
    return obj;
}

// 바닥(y=0, 위 향함)을 비스듬히 본다. 화면 중앙이 월드 원점이다.
int WhiteFloor(GgeRenderer* r, float half = 6.0f)
{
    int obj = gge_object_create(r, FloorMesh(r, half, 1.0f), 0, 0);
    CHECK(obj > 0);
    CHECK(gge_object_set_color(r, obj, 1, 1, 1) == 1);
    gge_renderer_set_camera(r, 0, 6, 3, 0, 0, 0, 0.8f);
    return obj;
}

void Test_DefaultLightingIsTheOldFixedLight()
{
    // 조명 API를 부르지 않으면 이전의 고정 조명 0.25 + 0.75 * N.-L과 같아야 한다(정규화 포함).
    GgeRenderer* r = NewRenderer();
    FacingQuad(r, 1, 1, 1);
    float shade = 0.25f + 0.75f * (0.6f / std::sqrt(0.4f * 0.4f + 1.0f + 0.6f * 0.6f));
    CHECK_PX("default light", Center(Render(r)), Expect(shade, shade, shade));
    gge_renderer_destroy(r);
}

void Test_AmbientAndDirectionalLightSetTheBrightnessPerChannel()
{
    GgeRenderer* r = NewRenderer();
    FacingQuad(r, 1, 1, 1);
    SetDirectional(r, 0, 0, -5, 0.5f, 0.25f, 0.0f);  // +Z 면에 정면으로 닿는다(길이는 정규화된다)
    CHECK(gge_renderer_set_ambient(r, 0.1f, 0.2f, 0.3f) == 1);
    CHECK_PX("ambient + directional", Center(Render(r)), Expect(0.6f, 0.45f, 0.3f));

    SetDirectional(r, 0, 0, 1, 0.5f, 0.25f, 0.0f);   // 뒤에서 비추면 N.-L < 0: 앰비언트만 남는다
    CHECK_PX("light from behind", Center(Render(r)), Expect(0.1f, 0.2f, 0.3f));

    SetDirectional(r, 0, -1, -1, 1, 1, 1);           // 45도: N.-L = cos 45
    CHECK(gge_renderer_set_ambient(r, 0, 0, 0) == 1);
    float c = std::cos(kPi / 4);
    CHECK_PX("45 degrees", Center(Render(r)), Expect(c, c, c));

    CHECK(gge_renderer_set_ambient(r, 3, 3, 3) == 1);  // 과노출은 흰색으로 포화
    CHECK_PX("overexposed", Center(Render(r)), Expect(1, 1, 1));
    gge_renderer_destroy(r);
}

void Test_ObjectColorAndTextureMultiplyTheLight()
{
    GgeRenderer* r = NewRenderer();
    FacingQuad(r, 0.5f, 1.0f, 0.25f);
    SetDirectional(r, 0, 0, -1, 0, 0, 0);
    CHECK(gge_renderer_set_ambient(r, 0.8f, 0.8f, 0.8f) == 1);
    CHECK_PX("color * ambient", Center(Render(r)), Expect(0.4f, 0.8f, 0.2f));
    gge_renderer_destroy(r);
}

void Test_PointLightFallsOffWithDistanceAndAngle()
{
    GgeRenderer* r = NewRenderer();
    WhiteFloor(r);
    SetDirectional(r, 0, -1, 0, 0, 0, 0);
    CHECK(gge_renderer_set_ambient(r, 0, 0, 0) == 1);  // 점광원만 남긴다
    const float pos[3] = {0, 2, 0}, white[3] = {1, 1, 1};
    int light = gge_light_create_point(r, pos, white, 4.0f);
    CHECK(light > 0);

    // 바로 아래: 거리 2, 범위 4 -> a = 0.5, a^2 = 0.25, N.l = 1
    CHECK_PX("directly below", Center(Render(r)), Expect(0.25f, 0.25f, 0.25f));

    const float higher[3] = {0, 3, 0};
    CHECK(gge_light_set_point(r, light, higher, white, 4.0f) == 1);  // 거리 3: a = 0.25, a^2 = 0.0625
    CHECK_PX("farther", Center(Render(r)), Expect(0.0625f, 0.0625f, 0.0625f));

    const float atRange[3] = {0, 4, 0};
    CHECK(gge_light_set_point(r, light, atRange, white, 4.0f) == 1);  // 범위 끝: 정확히 0
    CHECK_PX("at range", Center(Render(r)), Expect(0, 0, 0));

    const float beside[3] = {2, 2, 0};
    CHECK(gge_light_set_point(r, light, beside, white, 4.0f) == 1);  // 거리 2.83, a^2 = (1-0.7071)^2, N.l = 0.7071
    float a = 1 - std::sqrt(8.0f) / 4;
    float lit = a * a * (2.0f / std::sqrt(8.0f));
    CHECK_PX("oblique", Center(Render(r)), Expect(lit, lit, lit));

    const float below[3] = {0, -2, 0};
    CHECK(gge_light_set_point(r, light, below, white, 4.0f) == 1);  // 바닥 아래에서 비추면 윗면은 N.l < 0
    CHECK_PX("light under the surface", Center(Render(r)), Expect(0, 0, 0));
    gge_renderer_destroy(r);
}

void Test_PointLightsAddUpAndAreIndependent()
{
    GgeRenderer* r = NewRenderer();
    WhiteFloor(r);
    SetDirectional(r, 0, -1, 0, 0, 0, 0);
    CHECK(gge_renderer_set_ambient(r, 0, 0, 0) == 1);
    int a = AddPoint(r, 0, 2, 0, 1, 1, 1, 4);   // 중앙에서 0.25 (회색)
    int b = AddPoint(r, 0, 2, 0, 1, 0, 0, 4);   // 같은 자리 빨강: +0.25 R
    CHECK_PX("two lights", Center(Render(r)), Expect(0.5f, 0.25f, 0.25f));

    CHECK(gge_light_destroy(r, a) == 1);
    CHECK_PX("first removed", Center(Render(r)), Expect(0.25f, 0, 0));
    CHECK(gge_light_destroy(r, b) == 1);
    CHECK_PX("all removed", Center(Render(r)), Expect(0, 0, 0));

    int c = AddPoint(r, 0, 2, 0, 0, 0, 1, 4);
    CHECK(c > b);  // ID는 재사용되지 않는다
    CHECK_PX("re-added", Center(Render(r)), Expect(0, 0, 0.25f));
    gge_renderer_destroy(r);
}

void Test_PointLightLimitAndValidation()
{
    GgeRenderer* r = NewRenderer();
    std::vector<int> ids;
    for (int i = 0; i < 8; ++i) ids.push_back(AddPoint(r, 0, 1, 0, 1, 1, 1, 3));
    const float pos[3] = {0, 1, 0}, white[3] = {1, 1, 1};
    CHECK(gge_light_create_point(r, pos, white, 3) == 0);
    CHECK(std::strstr(gge_last_error(), "too many point lights") != nullptr);
    CHECK(gge_light_destroy(r, ids[3]) == 1);
    CHECK(gge_light_create_point(r, pos, white, 3) > 0);  // 자리가 나면 다시 만들 수 있다

    const float nan3[3] = {std::nanf(""), 0, 0}, neg[3] = {-0.1f, 0, 0}, inf3[3] = {0, INFINITY, 0};
    struct Case { const float* p; const float* c; float range; const char* expected; };
    const Case cases[] = {
        {nan3, white, 3, "position must be finite"},
        {pos, neg, 3, "color must be finite and non-negative"},
        {pos, inf3, 3, "color must be finite and non-negative"},
        {pos, white, 0, "range must be a finite number greater than 0"},
        {pos, white, -1, "range must be a finite number greater than 0"},
        {pos, white, std::nanf(""), "range must be a finite number greater than 0"},
        {pos, white, INFINITY, "range must be a finite number greater than 0"},
        {nullptr, white, 3, "null vector"},
        {pos, nullptr, 3, "null vector"},
    };
    CHECK(gge_light_destroy(r, ids[4]) == 1);  // 자리를 하나 비워 두어, 거부가 "가득 참" 때문이 아님을 보인다
    for (const Case& c : cases) {
        CHECK(gge_light_create_point(r, c.p, c.c, c.range) == 0);
        bool named = std::strstr(gge_last_error(), c.expected) != nullptr;
        if (!named) std::printf("  expected '%s', got '%s'\n", c.expected, gge_last_error());
        CHECK(named);
        CHECK(gge_light_set_point(r, ids[0], c.p, c.c, c.range) == 0);
    }
    CHECK(gge_light_set_point(r, 9999, pos, white, 3) == 0);
    CHECK(std::strstr(gge_last_error(), "unknown point light id") != nullptr);
    CHECK(gge_light_destroy(r, 9999) == 0);
    CHECK(gge_light_destroy(r, ids[3]) == 0);  // 이미 해제한 ID

    // 전역 조명 인자
    const float zero[3] = {0, 0, 0}, dir[3] = {0, -1, 0};
    CHECK(gge_renderer_set_ambient(r, -0.1f, 0, 0) == 0);
    CHECK(gge_renderer_set_ambient(r, 0, std::nanf(""), 0) == 0);
    CHECK(gge_renderer_set_directional_light(r, zero, white) == 0);
    CHECK(std::strstr(gge_last_error(), "must not be zero") != nullptr);
    CHECK(gge_renderer_set_directional_light(r, nan3, white) == 0);
    CHECK(gge_renderer_set_directional_light(r, dir, neg) == 0);
    CHECK(gge_renderer_set_directional_light(r, nullptr, white) == 0);
    gge_renderer_destroy(r);
}

void Test_RejectedLightingChangesLeaveTheImageUnchanged()
{
    GgeRenderer* r = NewRenderer();
    FacingQuad(r, 1, 1, 1);
    Image before = Render(r);
    CHECK(gge_renderer_set_ambient(r, -1, 0, 0) == 0);
    const float zero[3] = {0, 0, 0}, white[3] = {1, 1, 1};
    CHECK(gge_renderer_set_directional_light(r, zero, white) == 0);
    CHECK(gge_renderer_set_shadow(r, 1, std::nanf(""), 0.5f) == 0);
    CHECK(Render(r).bgra == before.bgra);
    gge_renderer_destroy(r);
}

void Test_ObjectAlphaBlendsWithWhatIsBehind()
{
    GgeRenderer* r = NewRenderer();
    int back = FacingQuad(r, 1, 0, 0, -1.0f, 3.0f);  // 불투명 빨강(뒤)
    (void)back;
    int front = gge_object_create(r, QuadMesh(r, 2.0f), 0, 0);
    gge_object_set_color(r, front, 0, 0, 1);
    Place(r, front, 0, 0, 1.0f);
    SetDirectional(r, 0, 0, -1, 0, 0, 0);
    CHECK(gge_renderer_set_ambient(r, 1, 1, 1) == 1);  // 밝기 1: 출력 = 오브젝트 색

    CHECK(gge_object_set_alpha(r, front, 0.25f) == 1);
    CHECK_PX("25% blue over red", Center(Render(r)), Expect(0.75f, 0, 0.25f));
    CHECK(gge_object_set_alpha(r, front, 0.5f) == 1);
    CHECK_PX("50%", Center(Render(r)), Expect(0.5f, 0, 0.5f));
    CHECK(gge_object_set_alpha(r, front, 1.0f) == 1);
    CHECK_PX("opaque", Center(Render(r)), Expect(0, 0, 1));
    CHECK(gge_object_set_alpha(r, front, 0.0f) == 1);
    CHECK_PX("invisible", Center(Render(r)), Expect(1, 0, 0));
    gge_renderer_destroy(r);
}

void Test_TranslucentOrderDoesNotDependOnCreationOrderAndNeverHidesWhatIsBehindIt()
{
    // 빨강 불투명(가장 뒤) 위에 초록(중간) 50%, 파랑(앞) 50%를 겹친다: 먼 것부터 섞으면 (0.25, 0.25, 0.5).
    auto build = [&](bool nearFirst) {
        GgeRenderer* r = NewRenderer();
        FacingQuad(r, 1, 0, 0, -2.0f, 4.0f);
        SetDirectional(r, 0, 0, -1, 0, 0, 0);
        gge_renderer_set_ambient(r, 1, 1, 1);
        int mid = 0, nearObj = 0;
        auto make = [&](float cr, float cg, float cb, float z) {
            int o = gge_object_create(r, QuadMesh(r, 2.0f), 0, 0);
            gge_object_set_color(r, o, cr, cg, cb);
            gge_object_set_alpha(r, o, 0.5f);
            Place(r, o, 0, 0, z);
            return o;
        };
        if (nearFirst) { nearObj = make(0, 0, 1, 1.0f); mid = make(0, 1, 0, 0.0f); }
        else { mid = make(0, 1, 0, 0.0f); nearObj = make(0, 0, 1, 1.0f); }
        (void)mid; (void)nearObj;
        Image img = Render(r);
        gge_renderer_destroy(r);
        return img;
    };
    Image a = build(true), b = build(false);
    CHECK(a.bgra == b.bgra);
    CHECK_PX("blue over green over red", Center(a), Expect(0.25f, 0.25f, 0.5f));
}

void Test_OpaqueObjectInFrontOccludesTranslucentOneBehind()
{
    GgeRenderer* r = NewRenderer();
    FacingQuad(r, 1, 0, 0, 1.0f);  // 불투명 빨강(앞)
    SetDirectional(r, 0, 0, -1, 0, 0, 0);
    gge_renderer_set_ambient(r, 1, 1, 1);
    int glass = gge_object_create(r, QuadMesh(r, 2.0f), 0, 0);
    gge_object_set_color(r, glass, 0, 1, 0);
    gge_object_set_alpha(r, glass, 0.5f);
    Place(r, glass, 0, 0, -1.0f);  // 뒤: 깊이 검사로 가려져야 한다
    CHECK_PX("translucent behind opaque", Center(Render(r)), Expect(1, 0, 0));
    gge_renderer_destroy(r);
}

void Test_RenderTargetAlphaStaysOpaque()
{
    GgeRenderer* r = NewRenderer();
    FacingQuad(r, 1, 0, 0, -1.0f, 3.0f);
    int glass = gge_object_create(r, QuadMesh(r, 2.0f), 0, 0);
    gge_object_set_alpha(r, glass, 0.3f);
    Image img = Render(r);
    CHECK(((img.At(img.width / 2, img.height / 2) >> 24) & 0xFF) == 255);  // WPF 비트맵이 반투명해지지 않는다
    gge_renderer_destroy(r);
}

void Test_AlphaAndShadowArgumentsAreValidated()
{
    GgeRenderer* r = NewRenderer();
    int obj = FacingQuad(r, 1, 1, 1);
    for (float bad : {-0.01f, 1.01f, std::nanf(""), INFINITY, -INFINITY}) {
        CHECK(gge_object_set_alpha(r, obj, bad) == 0);
        CHECK(std::strstr(gge_last_error(), "alpha must be") != nullptr);
    }
    CHECK(gge_object_set_alpha(r, obj, 0.0f) == 1 && gge_object_set_alpha(r, obj, 1.0f) == 1);  // 경계는 허용
    CHECK(gge_object_set_alpha(r, 9999, 0.5f) == 0);
    CHECK(gge_object_set_casts_shadow(r, 9999, 1) == 0);

    CHECK(gge_renderer_set_shadow(r, 1, 0, -0.1f) == 0);
    CHECK(gge_renderer_set_shadow(r, 1, 0, 1.1f) == 0);
    CHECK(std::strstr(gge_last_error(), "opacity must be") != nullptr);
    CHECK(gge_renderer_set_shadow(r, 1, INFINITY, 0.5f) == 0);
    CHECK(std::strstr(gge_last_error(), "plane height must be finite") != nullptr);
    CHECK(gge_renderer_set_shadow(r, 1, 0, 0.0f) == 1 && gge_renderer_set_shadow(r, 1, 0, 1.0f) == 1);
    gge_renderer_destroy(r);
}

void Test_TextureAlphaCutsOutTexels()
{
    GgeRenderer* r = NewRenderer();
    // 위 행은 알파 0(빨강, 초록), 아래 행은 불투명(파랑, 흰색).
    const uint8_t texture[16] = {255, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255};
    int tex = gge_texture_create(r, texture, 2, 2);
    CHECK(tex > 0);
    int obj = gge_object_create(r, QuadMesh(r, 2.0f), tex, 0);
    gge_object_set_color(r, obj, 1, 1, 1);
    gge_renderer_set_camera(r, 0, 0, 5, 0, 0, 0, 0.8f);
    SetDirectional(r, 0, 0, -1, 0, 0, 0);
    gge_renderer_set_ambient(r, 1, 1, 1);
    Image img = Render(r);
    Px background = Expect(kBackgroundColor[0], kBackgroundColor[1], kBackgroundColor[2]);
    CHECK_PX("cut-out top-left", PixelAt(img, img.width / 2 - 8, img.height / 2 - 8), background);
    CHECK_PX("cut-out top-right", PixelAt(img, img.width / 2 + 8, img.height / 2 - 8), background);
    CHECK_PX("opaque bottom-left", PixelAt(img, img.width / 2 - 8, img.height / 2 + 8), Expect(0, 0, 1));
    CHECK_PX("opaque bottom-right", PixelAt(img, img.width / 2 + 8, img.height / 2 + 8), Expect(1, 1, 1));

    // 알파가 정확히 절반(128)인 텍셀은 그린다(경계: 절반 미만만 버린다).
    const uint8_t edge[4] = {255, 255, 255, 128};
    int edgeTex = gge_texture_create(r, edge, 1, 1);
    gge_object_set_texture(r, obj, edgeTex);
    CHECK_PX("alpha 128 is kept", Center(Render(r)), Expect(1, 1, 1));
    const uint8_t low[4] = {255, 255, 255, 127};
    gge_object_set_texture(r, obj, gge_texture_create(r, low, 1, 1));
    CHECK_PX("alpha 127 is cut", Center(Render(r)), background);
    gge_renderer_destroy(r);
}

// ---- 그림자 ----
struct ShadowScene {
    GgeRenderer* r;
    int floor, caster;
    float eye[3] = {0, 5, 9}, target[3] = {0, 0, 0};
};

// 바닥 + 높이 1.5에 떠 있는 한 변 1짜리 큐브. 방향광은 바로 아래, 그림자는 불투명도 0.6.
ShadowScene MakeShadowScene()
{
    ShadowScene s;
    s.r = NewRenderer(320, 240);
    s.floor = WhiteFloor(s.r);
    gge_renderer_set_camera(s.r, s.eye[0], s.eye[1], s.eye[2], s.target[0], s.target[1], s.target[2], 0.8f);
    s.caster = gge_object_create(s.r, CubeMesh(s.r), 0, 0);
    gge_object_set_color(s.r, s.caster, 1, 1, 1);
    Place(s.r, s.caster, 0, 1.5f, 0, 0.5f);
    gge_object_set_casts_shadow(s.r, s.caster, 1);
    SetDirectional(s.r, 0, -1, 0, 0.75f, 0.75f, 0.75f);
    gge_renderer_set_shadow(s.r, 1, 0.0f, 0.6f);
    return s;
}

Px FloorAt(const ShadowScene& s, const Image& img, float x, float z)
{
    const float p[3] = {x, 0, z};
    int px = 0, py = 0;
    bool visible = ProjectToPixel(s.eye, s.target, 0.8f, img.width, img.height, p, px, py);
    CHECK(visible);
    return PixelAt(img, px, py);
}

void Test_ShadowDarkensTheFloorExactlyWhereTheLightIsBlocked()
{
    ShadowScene s = MakeShadowScene();
    Image img = Render(s.r);
    // 바닥은 N.-L = 1이라 밝기 1(흰색). 그림자 안은 (1 - 0.6)배.
    CHECK_PX("lit floor", FloorAt(s, img, 2.0f, 0), Expect(1, 1, 1));
    CHECK_PX("lit floor, other side", FloorAt(s, img, -3.0f, 1.0f), Expect(1, 1, 1));
    CHECK_PX("shadow center", FloorAt(s, img, 0, 0), Expect(0.4f, 0.4f, 0.4f));
    CHECK_PX("shadow near edge", FloorAt(s, img, 0.4f, 0.4f), Expect(0.4f, 0.4f, 0.4f));
    CHECK_PX("just outside the footprint", FloorAt(s, img, 0.7f, 0), Expect(1, 1, 1));
    CHECK_PX("just outside, z", FloorAt(s, img, 0, -0.7f), Expect(1, 1, 1));
    gge_renderer_destroy(s.r);
}

void Test_ShadowIsPaintedOncePerPixel()
{
    // 큐브를 바닥으로 누르면 윗면/아랫면/옆면이 같은 자리에 여러 겹 겹친다. 겹침마다 어두워지면 0.4^2 = 0.16(41) 아래로 내려간다.
    ShadowScene s = MakeShadowScene();
    Image img = Render(s.r);
    Px p = FloorAt(s, img, 0.1f, 0.1f);
    CHECK(p.r > 95 && p.r < 110);
    gge_renderer_destroy(s.r);
}

void Test_ShadowFollowsTheLightDirection()
{
    ShadowScene s = MakeShadowScene();
    SetDirectional(s.r, -1, -1, 0, 0.75f, 0.75f, 0.75f);  // 높이 1.5의 중심이 x = -1.5로 밀려 떨어진다
    Image img = Render(s.r);
    float lit = 0.25f + 0.75f * std::cos(kPi / 4);       // 바닥의 N.-L = cos 45
    CHECK_PX("old shadow spot is lit now", FloorAt(s, img, 0, 0), Expect(lit, lit, lit));
    CHECK_PX("shifted shadow center", FloorAt(s, img, -1.5f, 0), Expect(lit * 0.4f, lit * 0.4f, lit * 0.4f));
    CHECK_PX("far side is lit", FloorAt(s, img, 2.0f, 0), Expect(lit, lit, lit));
    gge_renderer_destroy(s.r);
}

void Test_ShadowSwitchesAndGuards()
{
    ShadowScene s = MakeShadowScene();
    Image shadowed = Render(s.r);
    Px dark = FloorAt(s, shadowed, 0, 0);

    gge_renderer_set_shadow(s.r, 0, 0.0f, 0.6f);                   // 끔
    CHECK_PX("disabled", FloorAt(s, Render(s.r), 0, 0), Expect(1, 1, 1));
    gge_renderer_set_shadow(s.r, 1, 0.0f, 0.0f);                   // 불투명도 0
    CHECK_PX("opacity 0", FloorAt(s, Render(s.r), 0, 0), Expect(1, 1, 1));
    gge_renderer_set_shadow(s.r, 1, 0.0f, 1.0f);                   // 불투명도 1: 완전히 검다
    CHECK_PX("opacity 1", FloorAt(s, Render(s.r), 0, 0), Expect(0, 0, 0));
    gge_renderer_set_shadow(s.r, 1, 0.0f, 0.6f);

    gge_object_set_casts_shadow(s.r, s.caster, 0);                 // 드리우지 않는 오브젝트
    CHECK_PX("does not cast", FloorAt(s, Render(s.r), 0, 0), Expect(1, 1, 1));
    gge_object_set_casts_shadow(s.r, s.caster, 1);
    CHECK_PX("casts again", FloorAt(s, Render(s.r), 0, 0), dark);

    SetDirectional(s.r, 1, 0, 0, 0.75f, 0.75f, 0.75f);             // 수평 광선: 투영이 무한히 길어지므로 그리지 않는다
    Image sideways = Render(s.r);
    SetDirectional(s.r, 0, 1, 0, 0.75f, 0.75f, 0.75f);             // 위로 향함
    CHECK(Render(s.r).bgra == sideways.bgra);                      // 둘 다 그림자 없이 같은 장면(바닥은 N.-L = 0이라 앰비언트만)
    gge_renderer_set_shadow(s.r, 0, 0, 0.6f);
    CHECK(Render(s.r).bgra == sideways.bgra);

    gge_object_set_alpha(s.r, s.caster, 0.0f);                     // 안 그려지는 오브젝트는 그림자도 없다
    SetDirectional(s.r, 0, -1, 0, 0.75f, 0.75f, 0.75f);
    gge_renderer_set_shadow(s.r, 1, 0, 0.6f);
    CHECK_PX("invisible caster", FloorAt(s, Render(s.r), 0, 0), Expect(1, 1, 1));
    gge_renderer_destroy(s.r);
}

void Test_ShadowPlaneHeightMovesTheShadow()
{
    // 평면을 y = 0.5에 두면(바닥은 그대로 y = 0) 그림자는 허공(바닥 아래)에 그려져 바닥에 보이지 않는다.
    ShadowScene s = MakeShadowScene();
    gge_renderer_set_shadow(s.r, 1, 0.5f, 0.6f);
    int plateau = gge_object_create(s.r, FloorMesh(s.r, 2.0f, 1.0f), 0, 0);  // 높이 0.5의 받침판
    gge_object_set_color(s.r, plateau, 1, 1, 1);
    Place(s.r, plateau, 0, 0.5f, 0);
    Image img = Render(s.r);
    const float onPlateau[3] = {0, 0.5f, 0};
    int px = 0, py = 0;
    CHECK(ProjectToPixel(s.eye, s.target, 0.8f, img.width, img.height, onPlateau, px, py));
    CHECK_PX("shadow on the raised plane", PixelAt(img, px, py), Expect(0.4f, 0.4f, 0.4f));
    gge_renderer_destroy(s.r);
}

void Test_ObjectInFrontOfTheShadowOccludesIt()
{
    // 그림자 점 (0,0,0)을 가리는 기둥(그림자를 드리우지 않음)이 있으면 화면에는 기둥이 보여야 한다.
    ShadowScene s = MakeShadowScene();
    int pillar = gge_object_create(s.r, CubeMesh(s.r), 0, 0);
    gge_object_set_color(s.r, pillar, 0.3f, 0.6f, 0.9f);
    const float p[3] = {0, 1.0f, 1.8f}, rot[3] = {0, 0, 0}, scale[3] = {0.5f, 1.0f, 0.4f};
    gge_object_set_transform(s.r, pillar, p, rot, scale);
    Image withShadow = Render(s.r);
    gge_renderer_set_shadow(s.r, 0, 0, 0.6f);
    Image without = Render(s.r);
    const float spot[3] = {0, 0, 0};
    int px = 0, py = 0;
    CHECK(ProjectToPixel(s.eye, s.target, 0.8f, withShadow.width, withShadow.height, spot, px, py));
    CHECK(PixelAt(withShadow, px, py).b != 255);                                    // 기둥 색이 보인다(바닥 흰색이 아님)
    CHECK_PX("pillar hides the shadow", PixelAt(withShadow, px, py), PixelAt(without, px, py));
    gge_renderer_destroy(s.r);
}

void Test_SkinnedObjectCastsTheShadowOfItsCurrentPose()
{
    GgeRenderer* r = NewRenderer(320, 240);
    WhiteFloor(r);
    gge_renderer_set_camera(r, 0, 6, 8, 0, 0.5f, 0, 0.8f);
    SetDirectional(r, 0, -1, 0, 0.75f, 0.75f, 0.75f);
    int mesh = SkinnedTube(r);
    int skeleton = ChainSkeleton(r);
    int bend = CreateClip(r, 1.0f, {ZRotation(1, 0, kPi / 3), ZRotation(2, 0, kPi / 3)});
    int obj = gge_object_create(r, mesh, 0, 0);
    gge_object_set_skeleton(r, obj, skeleton);
    gge_object_set_color(r, obj, 0.9f, 0.6f, 0.3f);
    gge_object_set_casts_shadow(r, obj, 1);
    Place(r, obj, 0, 0.5f, 0);

    auto leftmostShadowColumn = [&](float time) {
        gge_object_set_pose(r, obj, bend, time, 0, 0, 0);
        gge_renderer_set_shadow(r, 0, 0, 0.6f);
        Image lit = Render(r);
        gge_renderer_set_shadow(r, 1, 0, 0.6f);
        Image shadowed = Render(r);
        int leftmost = lit.width;
        for (int y = 0; y < lit.height; ++y)
            for (int x = 0; x < lit.width; ++x)
                if (PixelAt(lit, x, y).r - PixelAt(shadowed, x, y).r > 20) leftmost = std::min(leftmost, x);
        return leftmost;
    };
    // 휴식 자세(똑바로 선 튜브)의 그림자는 반지름 0.3 안쪽이고 몸통 뒤에 가려진다. 구부리면 -x 쪽 바닥(x = -0.7 너머)까지 뻗는다.
    const float eye[3] = {0, 6, 8}, target[3] = {0, 0.5f, 0}, probe[3] = {-0.7f, 0, 0};
    int probeX = 0, probeY = 0;
    CHECK(ProjectToPixel(eye, target, 0.8f, 320, 240, probe, probeX, probeY));
    int rest = leftmostShadowColumn(0.0f), bent = leftmostShadowColumn(1.0f);
    std::printf("  leftmost shadow column: rest %d, bent %d, probe %d\n", rest, bent, probeX);
    CHECK(rest > probeX);   // 곧게 선 자세는 그 너머에 그림자가 없다
    CHECK(bent <= probeX);  // 구부린 자세의 그림자는 그 너머까지 닿는다
    gge_renderer_destroy(r);
}

// 방 하나: 체크 바닥, 그림자를 드리우는 큐브와 기둥, 반투명 유리판, 색이 다른 점광원 2개.
void BuildLitScene(GgeRenderer* r)
{
    int checker = gge_texture_create(r, kQuadTexture, 2, 2);
    int floor = gge_object_create(r, FloorMesh(r, 6.0f, 3.0f), checker, 0);
    gge_object_set_color(r, floor, 0.8f, 0.8f, 0.8f);
    int cube = gge_object_create(r, CubeMesh(r), 0, 0);
    gge_object_set_color(r, cube, 0.9f, 0.6f, 0.3f);
    Place(r, cube, -1.2f, 1.1f, 0.0f, 0.6f, 0.5f);
    gge_object_set_casts_shadow(r, cube, 1);
    int pillar = gge_object_create(r, CubeMesh(r), 0, 0);
    gge_object_set_color(r, pillar, 0.5f, 0.6f, 0.7f);
    const float pp[3] = {2.2f, 1.0f, -1.0f}, rot[3] = {0, 0, 0}, ps[3] = {0.4f, 1.0f, 0.4f};
    gge_object_set_transform(r, pillar, pp, rot, ps);
    gge_object_set_casts_shadow(r, pillar, 1);
    int glass = gge_object_create(r, QuadMesh(r, 1.0f), 0, 0);
    gge_object_set_color(r, glass, 0.4f, 0.9f, 0.9f);
    gge_object_set_alpha(r, glass, 0.5f);
    Place(r, glass, 0.3f, 1.0f, 2.0f, 1.0f);
    gge_renderer_set_ambient(r, 0.15f, 0.15f, 0.2f);
    SetDirectional(r, -0.5f, -1.0f, -0.3f, 0.5f, 0.5f, 0.45f);
    AddPoint(r, -3.5f, 1.5f, 2.0f, 1.0f, 0.2f, 0.2f, 6.0f);
    AddPoint(r, 3.5f, 1.5f, 1.0f, 0.2f, 0.3f, 1.0f, 6.0f);
    gge_renderer_set_shadow(r, 1, 0.0f, 0.5f);
    gge_renderer_set_camera(r, 0, 5.0f, 9.0f, 0, 0.5f, 0, 0.8f);
}

void Test_GoldenLitScene()
{
    GgeRenderer* r = NewRenderer(320, 240);
    BuildLitScene(r);
    CHECK(MatchesGolden("lit_scene_320x240", Render(r)));
    gge_renderer_destroy(r);
}

void Test_GoldenLitSceneRetro()
{
    GgeRenderer* r = NewRenderer(320, 240);
    BuildLitScene(r);
    gge_renderer_set_retro(r, 1, 5, 1, 1);
    CHECK(MatchesGolden("lit_scene_retro_320x240", Render(r)));
    gge_renderer_destroy(r);
}

// ---- 8단계: 구운 씬(.ggs) 런타임 ----
// 형식은 engine/src/scene_io.cpp 맨 위 주석이 기준이다. 아래 작성기는 그 문서만 보고 C# SceneBaker와 별개로 만든 것이라,
// 로더가 문서대로 읽는지를 두 구현의 합의가 아니라 문서 기준으로 확인한다.
uint32_t TestCrc32(const std::vector<uint8_t>& data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data) {
        crc ^= b;
        for (int k = 0; k < 8; ++k) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

struct GgsWriter {
    std::vector<uint8_t> bytes;
    void Raw(const void* p, size_t n) { const uint8_t* b = static_cast<const uint8_t*>(p); bytes.insert(bytes.end(), b, b + n); }
    void U8(uint8_t v) { Raw(&v, 1); }
    void U32(uint32_t v) { Raw(&v, 4); }
    void I32(int32_t v) { Raw(&v, 4); }
    void F(float v) { Raw(&v, 4); }
    void Floats(const std::vector<float>& v) { for (float f : v) F(f); }
    void Vec3(float x, float y, float z) { F(x); F(y); F(z); }
    void Str(const std::string& s) { U32(static_cast<uint32_t>(s.size())); Raw(s.data(), s.size()); }
    // 끝에 CRC-32를 붙여 완성된 파일 바이트를 돌려준다.
    std::vector<uint8_t> Sealed() const
    {
        std::vector<uint8_t> out = bytes;
        uint32_t crc = TestCrc32(bytes);
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&crc), reinterpret_cast<uint8_t*>(&crc) + 4);
        return out;
    }
};

struct SpecNode {
    std::string name;
    int parent = -1, mesh = -1, texture = -1;
    float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, scale[3] = {1, 1, 1}, color[3] = {0.8f, 0.8f, 0.8f};
    float alpha = 1;
    bool casts = false;
    std::string clip;
    bool playing = true;
    float speed = 1;
};

struct SpecRoom {
    std::string name;
    std::vector<float> polygon;
    float eye[3], target[3], fov;
};

struct SpecTrigger {
    std::string name;
    std::vector<float> polygon;
    int tag = 0;
};

struct SpecCollider {
    std::string name;   // version 1 파일에는 쓰이지 않는다
    float minX, minZ, maxX, maxZ;
};

// 메시 0 = 정면 사각형(정적), 메시 1 = 3본 체인에 묶인 띠(스킨, 클립 "bend" 하나).
struct Spec {
    std::vector<std::vector<uint8_t>> textures;
    int meshCount = 2;
    std::vector<SpecNode> nodes;
    std::string clipName = "bend";
    std::vector<std::string> extraClips;  // 스킨 띠 메시의 추가 클립 이름(길이 2, 3, 4...초)
    int pointLights = 0;
    std::vector<SpecRoom> rooms;
    std::vector<SpecCollider> colliders;
    std::vector<SpecTrigger> triggers;
    uint32_t version = 2;       // 쓰는 .ggs 버전: 2 = 현재(충돌체 이름 있음), 1 = 옛 형식(이름 없음, 읽기 호환 확인용)
    bool hasPlayer = false;
    int playerNode = 0;
    bool hasSpawn = false;
    float spawn[2] = {0, 0};
    bool badQuadIndex = false;  // 정점 수(4)를 넘는 인덱스를 넣는다
    bool stripFirst = false;    // 메시 순서를 바꾼다: 스킨 띠가 0번, 사각형이 1번
    int bones = 3;              // 스킨 띠 스켈레톤의 본 수(체인)
    bool distinctRest = false;  // 본마다/성분마다 다른 휴식 변환(로더가 본별 인덱스를 제대로 읽는지 보려는 용도)
};

// 본마다 모든 성분이 다른 휴식 이동/회전(단위 쿼터니언)/크기를 가진 체인.
ChainData DistinctRestChain(int bones)
{
    ChainData c = MakeChain(bones);
    for (int i = 0; i < bones; ++i) {
        c.translation[i * 3 + 0] = 0.1f * (i + 1);
        c.translation[i * 3 + 1] = i > 0 ? 1.0f + 0.05f * i : 0.0f;
        c.translation[i * 3 + 2] = -0.2f * i;
        // 네 성분이 모두 0이 아니고 본마다 다른 단위 쿼터니언(x, y 성분 인덱싱 오류도 드러나게)
        const float q[4] = {0.1f + 0.05f * i, 0.2f - 0.03f * i, 0.3f + 0.04f * i, 0.9f};
        const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        for (int k = 0; k < 4; ++k) c.rotation[i * 4 + k] = q[k] / len;
        c.scale[i * 3 + 0] = 1.0f + 0.1f * i; c.scale[i * 3 + 1] = 1.2f + 0.05f * i; c.scale[i * 3 + 2] = 0.9f + 0.07f * i;
    }
    return c;
}

std::vector<uint8_t> ReadFileBytes(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::vector<float> QuadVertices()
{
    return {-1, -1, 0, 0, 0, 1, 0, 1,   1, -1, 0, 0, 0, 1, 1, 1,   1, 1, 0, 0, 0, 1, 1, 0,   -1, 1, 0, 0, 0, 1, 0, 0};
}

std::vector<uint8_t> Serialize(const Spec& spec)
{
    GgsWriter w;
    w.Raw("GGS1", 4);
    w.U32(spec.version);
    w.U32(static_cast<uint32_t>(spec.textures.size()));
    for (const auto& t : spec.textures) { w.U32(static_cast<uint32_t>(t.size())); w.Raw(t.data(), t.size()); }

    w.U32(static_cast<uint32_t>(spec.meshCount));
    for (int m = 0; m < spec.meshCount; ++m) {
        if ((m == 0) != spec.stripFirst) {  // 정적 사각형
            w.U8(0); w.U32(4); w.U32(6);
            w.Floats(QuadVertices());
            for (uint32_t i : {0u, 1u, spec.badQuadIndex ? 9u : 2u, 0u, 2u, 3u}) w.U32(i);
            continue;
        }
        // 스킨 띠: SkinnedStrip과 같은 데이터
        w.U8(1); w.U32(6); w.U32(12);
        w.Floats({-0.3f, 0, 0, 0, 0, 1, 0, 1,   0.3f, 0, 0, 0, 0, 1, 1, 1,
                  -0.3f, 1, 0, 0, 0, 1, 0, 0.5f, 0.3f, 1, 0, 0, 0, 1, 1, 0.5f,
                  -0.3f, 2, 0, 0, 0, 1, 0, 0,   0.3f, 2, 0, 0, 0, 1, 1, 0});
        for (uint32_t i : {0u, 1u, 3u, 0u, 3u, 2u, 2u, 3u, 5u, 2u, 5u, 4u}) w.U32(i);
        for (uint32_t j : {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u}) w.U32(j);
        w.Floats({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
        ChainData chain = spec.distinctRest ? DistinctRestChain(spec.bones) : MakeChain(spec.bones);
        w.U32(static_cast<uint32_t>(spec.bones));
        for (int p : chain.parents) w.I32(p);
        w.Floats(chain.inverseBind); w.Floats(chain.translation); w.Floats(chain.rotation); w.Floats(chain.scale);
        w.U32(static_cast<uint32_t>(1 + spec.extraClips.size()));
        w.Str(spec.clipName);
        w.F(1.0f);
        w.U32(2);
        for (int bone : {1, 2}) {
            KeyChannel c = ZRotation(bone, 0, 1);
            w.I32(c.bone); w.U32(1); w.U32(static_cast<uint32_t>(c.times.size()));
            w.Floats(c.times); w.Floats(c.values);
        }
        // 추가 클립: 길이가 2, 3, 4...초로 서로 달라 반복/끝 정지를 구분할 수 있고, 회전 방향/크기도 달라 포즈가 서로 다르다.
        for (size_t i = 0; i < spec.extraClips.size(); ++i) {
            const float duration = 2.0f + static_cast<float>(i);
            w.Str(spec.extraClips[i]);
            w.F(duration);
            w.U32(2);
            for (int bone : {1, 2}) {
                KeyChannel c = ZRotation(bone, 0, (i % 2 == 0 ? -1.0f : 1.5f) * static_cast<float>(i / 2 + 1));
                c.times = {0, duration};
                w.I32(c.bone); w.U32(1); w.U32(static_cast<uint32_t>(c.times.size()));
                w.Floats(c.times); w.Floats(c.values);
            }
        }
    }

    w.U32(static_cast<uint32_t>(spec.nodes.size()));
    for (const SpecNode& n : spec.nodes) {
        w.Str(n.name); w.I32(n.parent); w.I32(n.mesh); w.I32(n.texture);
        w.Vec3(n.pos[0], n.pos[1], n.pos[2]); w.Vec3(n.rot[0], n.rot[1], n.rot[2]);
        w.Vec3(n.scale[0], n.scale[1], n.scale[2]); w.Vec3(n.color[0], n.color[1], n.color[2]);
        w.F(n.alpha); w.U8(n.casts ? 1 : 0); w.Str(n.clip); w.U8(n.playing ? 1 : 0); w.F(n.speed);
    }

    w.Vec3(0.2f, 0.2f, 0.25f);          // ambient
    w.Vec3(-0.5f, -1.0f, -0.3f);        // sun direction
    w.Vec3(0.5f, 0.5f, 0.45f);          // sun color
    w.U32(static_cast<uint32_t>(spec.pointLights));
    for (int i = 0; i < spec.pointLights; ++i) { w.Vec3(-2.0f + i, 2, 2); w.Vec3(1, 0.4f, 0.2f); w.F(6); }
    w.U8(0); w.F(0); w.F(0.5f);         // shadow off

    w.U32(static_cast<uint32_t>(spec.rooms.size()));
    for (const SpecRoom& r : spec.rooms) {
        w.Str(r.name); w.U32(static_cast<uint32_t>(r.polygon.size() / 2)); w.Floats(r.polygon);
        w.Vec3(r.eye[0], r.eye[1], r.eye[2]); w.Vec3(r.target[0], r.target[1], r.target[2]); w.F(r.fov);
    }
    w.U32(static_cast<uint32_t>(spec.colliders.size()));
    for (const SpecCollider& c : spec.colliders) {
        if (spec.version >= 2) w.Str(c.name);
        w.F(c.minX); w.F(c.minZ); w.F(c.maxX); w.F(c.maxZ);
    }
    w.U32(static_cast<uint32_t>(spec.triggers.size()));
    for (const SpecTrigger& t : spec.triggers) {
        w.Str(t.name); w.U32(static_cast<uint32_t>(t.polygon.size() / 2)); w.Floats(t.polygon); w.I32(t.tag);
    }
    w.U8(spec.hasPlayer ? 1 : 0);
    if (spec.hasPlayer) {
        w.I32(spec.playerNode); w.U8(spec.hasSpawn ? 1 : 0); w.F(spec.spawn[0]); w.F(spec.spawn[1]); w.F(0.4f); w.F(3);
    }
    return w.Sealed();
}

fs::path WriteScene(const std::string& name, const std::vector<uint8_t>& bytes)
{
    fs::path path = TempDir() / name;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

// 사각형(텍스처) 패널 + 그 자식 배지, 스킨 띠(클립 재생), 그룹 노드.
Spec VisualSpec(const fs::path& bmp)
{
    Spec s;
    s.textures.push_back(ReadFileBytes(bmp));
    SpecNode panel; panel.name = "Panel"; panel.mesh = 0; panel.texture = 0;
    panel.pos[0] = 1.5f; panel.pos[1] = 1.0f; panel.rot[1] = 0.4f; panel.scale[0] = panel.scale[1] = panel.scale[2] = 0.8f; panel.color[0] = 1; panel.color[1] = 0.9f; panel.color[2] = 0.8f;
    SpecNode badge; badge.name = "Badge"; badge.parent = 0; badge.mesh = 0; badge.pos[0] = 0.5f; badge.pos[1] = 0.6f; badge.scale[0] = badge.scale[1] = badge.scale[2] = 0.3f;
    badge.color[0] = 0.2f; badge.color[1] = 0.9f; badge.color[2] = 0.3f; badge.alpha = 0.5f;
    SpecNode strip; strip.name = "Strip"; strip.mesh = 1; strip.clip = "bend"; strip.speed = 1.5f; strip.pos[0] = -1.5f; strip.color[0] = 0.9f; strip.color[1] = 0.6f; strip.color[2] = 0.3f;
    SpecNode group; group.name = "Group";
    s.nodes = {panel, badge, strip, group};
    s.pointLights = 2;
    return s;
}

// 위와 같은 장면을 C API로 직접 만든다(기준).
void BuildVisualSceneManually(GgeRenderer* r, const fs::path& bmp, int* stripObject, int* clipId)
{
    int quad = QuadMesh(r, 1.0f);
    int tex = gge_texture_load(r, bmp.string().c_str());
    int strip = SkinnedStrip(r);
    int skeleton = ChainSkeleton(r);
    int clip = CreateClip(r, 1.0f, {ZRotation(1, 0, 1), ZRotation(2, 0, 1)});

    int panel = gge_object_create(r, quad, tex, 0);
    const float pp[3] = {1.5f, 1.0f, 0}, pr[3] = {0, 0.4f, 0}, ps[3] = {0.8f, 0.8f, 0.8f};
    gge_object_set_transform(r, panel, pp, pr, ps);
    gge_object_set_color(r, panel, 1, 0.9f, 0.8f);
    int badge = gge_object_create(r, quad, 0, panel);
    const float bp[3] = {0.5f, 0.6f, 0}, br[3] = {0, 0, 0}, bs[3] = {0.3f, 0.3f, 0.3f};
    gge_object_set_transform(r, badge, bp, br, bs);
    gge_object_set_color(r, badge, 0.2f, 0.9f, 0.3f);
    gge_object_set_alpha(r, badge, 0.5f);
    int stripObj = gge_object_create(r, strip, 0, 0);
    gge_object_set_skeleton(r, stripObj, skeleton);
    gge_object_set_pose(r, stripObj, clip, 0, 0, 0, 0);
    Place(r, stripObj, -1.5f, 0, 0);
    gge_object_set_color(r, stripObj, 0.9f, 0.6f, 0.3f);
    gge_object_create(r, 0, 0, 0);  // 그룹 노드
    *stripObject = stripObj;
    *clipId = clip;  // 호출자가 Step마다 포즈 시간을 직접 지정한다

    gge_renderer_set_ambient(r, 0.2f, 0.2f, 0.25f);
    const float sunDir[3] = {-0.5f, -1.0f, -0.3f}, sunColor[3] = {0.5f, 0.5f, 0.45f};
    gge_renderer_set_directional_light(r, sunDir, sunColor);
    const float color[3] = {1, 0.4f, 0.2f};
    for (int i = 0; i < 2; ++i) {
        const float pos[3] = {-2.0f + i, 2, 2};
        gge_light_create_point(r, pos, color, 6);
    }
    gge_renderer_set_shadow(r, 0, 0, 0.5f);
    gge_renderer_set_camera(r, 0, 2.5f, 7, 0, 0.8f, 0, 0.8f);
}

const char* kSamplePlayGgs = "../../assets/samples/play.ggs";

fs::path SamplePlayScene() { return fs::path(GGE_TEST_DATA_DIR) / kSamplePlayGgs; }

std::string DrainEvents(GgeScene* scene)
{
    std::string text;
    int kind = 0, tag = 0;
    const char* name = nullptr;
    while (gge_scene_poll_event(scene, &kind, &tag, &name)) {
        if (!text.empty()) text += ", ";
        text += std::string(kind == GGE_TRIGGER_ENTER ? "Enter " : "Exit ") + name + " " + std::to_string(tag);
    }
    return text;
}

void Test_SceneBuiltFromTheFormatSpecRendersLikeManualConstruction()
{
    fs::path bmp = TempDir() / "scene_quad.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    fs::path ggs = WriteScene("visual.ggs", Serialize(VisualSpec(bmp)));

    GgeRenderer* a = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(a, ggs.string().c_str());
    if (!scene) std::printf("  load failed: %s\n", gge_last_error());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(a); return; }
    gge_renderer_set_camera(a, 0, 2.5f, 7, 0, 0.8f, 0, 0.8f);

    GgeRenderer* b = NewRenderer(160, 120);
    int stripObject = 0, clip = 0;
    BuildVisualSceneManually(b, bmp, &stripObject, &clip);

    CHECK(scene != nullptr && gge_scene_find_object(scene, "Panel") > 0);
    CHECK(gge_scene_find_object(scene, "Badge") > gge_scene_find_object(scene, "Panel"));   // 자식은 부모 뒤에 만들어진다
    CHECK(gge_scene_find_object(scene, "Strip") > 0 && gge_scene_find_object(scene, "Group") > 0);
    CHECK(gge_scene_find_object(scene, "Nothing") == 0);

    Image first = Render(a);
    CHECK(first.bgra == Render(b).bgra);   // 시간 0: 같은 자세, 같은 조명, 같은 텍스처

    // 클립 "bend"를 속도 1.5로 재생: 0.2초씩 5번 = 1.5초 -> 시간은 감겨서 (0.2*1.5*5) mod 1 = 0.5
    float t = 0;
    for (int i = 0; i < 5; ++i) {
        CHECK(gge_scene_step(scene, 0, 0, 0.2f) == 1);
        t = t + 0.2f * 1.5f;
        t = t - std::floor(t);
        CHECK(gge_object_set_pose(b, stripObject, clip, t, 0, 0, 0) == 1);
        CHECK(Render(a).bgra == Render(b).bgra);
    }
    CHECK(first.bgra != Render(a).bgra);  // 애니메이션이 실제로 진행했다

    gge_scene_destroy(scene);
    gge_renderer_destroy(a);
    gge_renderer_destroy(b);
}

void Test_SceneMeshOrderInTheFileDoesNotMatter()
{
    // 스킨 메시가 0번이어도(경계: 메시 번호 0) 정적/스킨 구분과 애니메이션이 똑같이 동작해야 한다.
    fs::path bmp = TempDir() / "scene_order.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* a = NewRenderer(160, 120);
    GgeRenderer* b = NewRenderer(160, 120);
    int stripObject = 0, clip = 0;
    BuildVisualSceneManually(b, bmp, &stripObject, &clip);

    Spec spec = VisualSpec(bmp);
    spec.stripFirst = true;
    spec.nodes[0].mesh = 1; spec.nodes[1].mesh = 1; spec.nodes[2].mesh = 0;   // 사각형 = 1번, 스킨 띠 = 0번
    GgeScene* scene = gge_scene_load(a, WriteScene("order.ggs", Serialize(spec)).string().c_str());
    if (!scene) std::printf("  load failed: %s\n", gge_last_error());
    CHECK(scene != nullptr);
    if (scene) {
        gge_renderer_set_camera(a, 0, 2.5f, 7, 0, 0.8f, 0, 0.8f);
        CHECK(Render(a).bgra == Render(b).bgra);
        for (int i = 0; i < 3; ++i) gge_scene_step(scene, 0, 0, 0.2f);
        float t = 0;
        for (int i = 0; i < 3; ++i) { t += 0.2f * 1.5f; t -= std::floor(t); }
        gge_object_set_pose(b, stripObject, clip, t, 0, 0, 0);
        CHECK(Render(a).bgra == Render(b).bgra);   // 스킨 메시가 0번이어도 클립이 재생된다
        gge_scene_destroy(scene);
    }
    gge_renderer_destroy(a);
    gge_renderer_destroy(b);
}

void Test_PausedClipStaysAtTimeZeroAndStaticMeshIgnoresClip()
{
    fs::path bmp = TempDir() / "scene_quad2.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    Spec spec = VisualSpec(bmp);
    spec.nodes[2].playing = false;                       // 멈춘 클립: 시간 0 자세로 정지
    spec.nodes[0].clip = "whatever";                      // 정적 메시의 클립 이름은 에디터처럼 무시한다
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, WriteScene("paused.ggs", Serialize(spec)).string().c_str());
    CHECK(scene != nullptr);
    if (scene) {
        gge_renderer_set_camera(r, 0, 2.5f, 7, 0, 0.8f, 0, 0.8f);
        Image before = Render(r);
        for (int i = 0; i < 10; ++i) CHECK(gge_scene_step(scene, 0, 0, 0.25f) == 1);
        CHECK(Render(r).bgra == before.bgra);
        gge_scene_destroy(scene);
    }
    gge_renderer_destroy(r);
}

void Test_SceneLoadRejectsDamagedAndInconsistentFiles()
{
    fs::path bmp = TempDir() / "scene_quad3.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer(64, 48);

    auto expectRejected = [&](const char* label, const std::vector<uint8_t>& bytes, const char* reason) {
        GgeScene* scene = gge_scene_load(r, WriteScene("bad.ggs", bytes).string().c_str());
        bool named = std::strstr(gge_last_error(), reason) != nullptr;
        if (scene || !named) std::printf("  %s: loaded=%d, error='%s' (expected '%s')\n", label, scene != nullptr, gge_last_error(), reason);
        CHECK(scene == nullptr && named);
        if (scene) gge_scene_destroy(scene);
    };
    auto withCrc = [](std::vector<uint8_t> body) {
        uint32_t crc = TestCrc32(body);
        body.insert(body.end(), reinterpret_cast<uint8_t*>(&crc), reinterpret_cast<uint8_t*>(&crc) + 4);
        return body;
    };

    Spec good = VisualSpec(bmp);
    std::vector<uint8_t> file = Serialize(good);
    // 파일 수준
    CHECK(gge_scene_load(r, (TempDir() / "does_not_exist.ggs").string().c_str()) == nullptr);
    CHECK(std::strstr(gge_last_error(), "cannot open file") != nullptr);
    CHECK(gge_scene_load(r, nullptr) == nullptr);
    CHECK(gge_scene_load(nullptr, "x.ggs") == nullptr);
    expectRejected("too short", std::vector<uint8_t>(file.begin(), file.begin() + 10), "not a GGS1 scene file");
    std::vector<uint8_t> badMagic = file; badMagic[0] = 'X';
    expectRejected("bad magic", badMagic, "not a GGS1 scene file");
    std::vector<uint8_t> badVersion = file; badVersion[4] = 9;
    expectRejected("version", badVersion, "unsupported scene version 9");
    std::vector<uint8_t> flipped = file; flipped[file.size() / 2] ^= 1;
    expectRejected("checksum", flipped, "checksum mismatch");
    expectRejected("cut", std::vector<uint8_t>(file.begin(), file.end() - 1), "checksum mismatch");

    // 체크섬을 맞춘 구조 손상
    std::vector<uint8_t> body(file.begin(), file.end() - 4);
    expectRejected("truncated body", withCrc(std::vector<uint8_t>(body.begin(), body.begin() + body.size() / 2)), "truncated");
    std::vector<uint8_t> trailing = body; trailing.push_back(0);
    expectRejected("trailing bytes", withCrc(trailing), "trailing bytes");
    std::vector<uint8_t> huge = body; const uint32_t many = 0xFFFFFFFFu; std::memcpy(&huge[8], &many, 4);
    expectRejected("texture count", withCrc(huge), "too many textures");

    // 내용 수준(작성기로 만든 일관된 파일의 참조 오류)
    auto mutate = [&](auto edit) { Spec s = VisualSpec(bmp); edit(s); return Serialize(s); };
    expectRejected("mesh ref", mutate([](Spec& s) { s.nodes[0].mesh = 5; }), "node 'Panel' references mesh 5 which does not exist");
    expectRejected("texture ref", mutate([](Spec& s) { s.nodes[0].texture = 3; }), "node 'Panel' references texture 3 which does not exist");
    expectRejected("parent after child", mutate([](Spec& s) { s.nodes[0].parent = 1; }), "a parent must come before its children");
    expectRejected("parent self", mutate([](Spec& s) { s.nodes[1].parent = 1; }), "a parent must come before its children");
    expectRejected("parent range", mutate([](Spec& s) { s.nodes[1].parent = -2; }), "a parent must come before its children");
    expectRejected("unknown clip", mutate([](Spec& s) { s.nodes[2].clip = "missing"; }), "clip 'missing' was not found");
    expectRejected("bad image", mutate([](Spec& s) { s.textures[0] = {1, 2, 3, 4}; }), "cannot decode image");
    expectRejected("empty image", mutate([](Spec& s) { s.textures[0].clear(); }), "cannot decode image");
    expectRejected("player node range", mutate([](Spec& s) { s.hasPlayer = true; s.playerNode = 9; }), "the player node 9 does not exist");
    expectRejected("player is a child", mutate([](Spec& s) { s.hasPlayer = true; s.playerNode = 1; }), "must be a root node");
    expectRejected("player without rooms", mutate([](Spec& s) { s.hasPlayer = true; s.playerNode = 0; }), "no rooms");
    expectRejected("spawn outside", mutate([](Spec& s) {
        s.hasPlayer = true; s.playerNode = 0; s.hasSpawn = true; s.spawn[0] = 50; s.spawn[1] = 50;
        s.rooms.push_back({"R", {0, 0, 4, 0, 4, 4, 0, 4}, {2, 5, 9}, {2, 0, 2}, 0.8f});
    }), "outside every room");
    expectRejected("bad room polygon", mutate([](Spec& s) { s.rooms.push_back({"Bad", {0, 0, 1, 1, 2, 2}, {2, 5, 9}, {2, 0, 2}, 0.8f}); }), "room 'Bad'");
    expectRejected("bad mesh data", mutate([](Spec& s) { s.badQuadIndex = true; }), "mesh 0: mesh index out of range");
    gge_renderer_destroy(r);
}

void Test_SceneStepRejectsBadTimeAndFailedLoadLeavesNothingBehind()
{
    fs::path bmp = TempDir() / "scene_quad4.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer(96, 72);
    gge_renderer_set_camera(r, 0, 2.5f, 7, 0, 0.8f, 0, 0.8f);
    Image empty = Render(r);

    GgeScene* scene = gge_scene_load(r, WriteScene("ok.ggs", Serialize(VisualSpec(bmp))).string().c_str());
    CHECK(scene != nullptr);
    if (scene) {
        CHECK(gge_scene_step(scene, 0, 0, -0.1f) == 0);
        CHECK(std::strstr(gge_last_error(), "dt must be") != nullptr);
        CHECK(gge_scene_step(scene, 0, 0, std::nanf("")) == 0);
        CHECK(gge_scene_step(scene, 0, 0, INFINITY) == 0);
        CHECK(gge_scene_step(scene, 0, 0, 0) == 1);   // 0은 허용
        float x = 7, z = 7, yaw = 7;
        CHECK(gge_scene_get_player(scene, &x, &z, &yaw) == 0);        // 플레이어 없는 씬
        CHECK(x == 7 && z == 7 && yaw == 7);                           // 출력은 건드리지 않는다
        CHECK(std::strcmp(gge_scene_active_room_name(scene), "") == 0);
        CHECK(gge_scene_poll_event(scene, nullptr, nullptr, nullptr) == 0);
        gge_scene_destroy(scene);
    }
    gge_renderer_set_camera(r, 0, 2.5f, 7, 0, 0.8f, 0, 0.8f);
    CHECK(Render(r).bgra == empty.bgra);   // 소멸하면 오브젝트가 모두 사라진다(조명은 씬 값으로 남는다: 배경만 보임)

    // 로드의 마지막 단계(스폰 검사)에서 실패하는 씬을 여러 번 불러도 아무것도 쌓이지 않는다:
    // 매번 점광원 5개를 만들므로 정리되지 않으면 8개 한도에서 다른 오류로 바뀐다.
    Spec failing = VisualSpec(bmp);
    failing.pointLights = 5;
    failing.hasPlayer = true; failing.playerNode = 0; failing.hasSpawn = true; failing.spawn[0] = 50; failing.spawn[1] = 50;
    failing.rooms.push_back({"R", {0, 0, 4, 0, 4, 4, 0, 4}, {2, 5, 9}, {2, 0, 2}, 0.8f});
    std::string path = WriteScene("fails.ggs", Serialize(failing)).string();
    for (int i = 0; i < 12; ++i) {
        CHECK(gge_scene_load(r, path.c_str()) == nullptr);
        CHECK(std::strstr(gge_last_error(), "outside every room") != nullptr);
    }
    CHECK(Render(r).bgra == empty.bgra);
    GgeScene* again = gge_scene_load(r, WriteScene("ok2.ggs", Serialize(VisualSpec(bmp))).string().c_str());
    CHECK(again != nullptr);   // 이전 실패들이 점광원 자리를 차지하지 않았다
    gge_scene_destroy(again);
    gge_renderer_destroy(r);
}

void Test_BakedPlaySampleStartsInsideTheFirstTrigger()
{
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    if (!scene) std::printf("  load failed: %s\n", gge_last_error());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }

    float x = 0, z = 0, yaw = 9;
    CHECK(gge_scene_get_player(scene, &x, &z, &yaw) == 1);
    CHECK(x == -4.0f && z == 0.0f && yaw == 0.0f);
    CHECK(std::strcmp(gge_scene_active_room_name(scene), "Hall") == 0);
    CHECK(DrainEvents(scene) == "Enter StartPad 1");   // 스폰이 StartPad 안
    CHECK(DrainEvents(scene).empty());
    CHECK(gge_scene_find_object(scene, "Hero") > 0 && gge_scene_find_object(scene, "Pillar") > 0);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_BakedPlaySampleWalkFollowsTheGameplayRules()
{
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    std::string events = DrainEvents(scene);

    // 오른쪽으로 계속 걷는다: StartPad를 벗어나고, 방 경계(x=0)를 넘어 카메라가 바뀌고, Door에 들어갔다 나오고, 동쪽 벽(x=6, 반지름 0.4)에서 멈춘다.
    std::string rooms;
    std::string lastRoom = gge_scene_active_room_name(scene);
    for (int i = 0; i < 200; ++i) {
        CHECK(gge_scene_step(scene, 1, 0, 1.0f / 30) == 1);
        std::string e = DrainEvents(scene);
        if (!e.empty()) events += (events.empty() ? "" : ", ") + e;
        if (gge_scene_active_room_name(scene) != lastRoom) {
            lastRoom = gge_scene_active_room_name(scene);
            rooms += lastRoom + ";";
        }
    }
    CHECK(events == "Enter StartPad 1, Exit StartPad 1, Enter Door 7, Exit Door 7");
    CHECK(rooms == "Annex;");
    float x = 0, z = 0, yaw = 0;
    CHECK(gge_scene_get_player(scene, &x, &z, &yaw) == 1);
    CHECK(std::fabs(x - 5.6f) < 1e-3f && std::fabs(z) < 1e-3f);          // 동쪽 벽(6)에서 반지름만큼 떨어져 멈춘다
    CHECK(std::fabs(yaw - kPi / 2) < 1e-4f);                              // +X를 향한다(모델 앞쪽 +Z 가정: atan2(x, z))

    // 아래쪽 입력: 화면 위쪽(+) = 카메라가 보는 방향. Annex 카메라는 -X를 향하므로 +Y 입력은 -X로 간다.
    float x0 = x;
    for (int i = 0; i < 10; ++i) gge_scene_step(scene, 0, 1, 1.0f / 30);
    gge_scene_get_player(scene, &x, &z, &yaw);
    CHECK(x < x0 - 0.5f && std::fabs(z) < 1e-3f);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

// ---- 스크립트가 쓰는 쓰기 API: 이름으로 트리거 켜기/끄기, 플레이어 텔레포트 ----
// 사각형 방 하나(-10..10)와 원점에 스폰하는 플레이어(노드 0)를 가진 씬. 트리거는 호출자가 채운다.
Spec RoomWithPlayerSpec(const fs::path& bmp)
{
    Spec s = VisualSpec(bmp);
    SpecRoom room;
    room.name = "Room";
    room.polygon = {-10, -10, 10, -10, 10, 10, -10, 10};
    room.eye[0] = 0; room.eye[1] = 8; room.eye[2] = 8;
    room.target[0] = room.target[1] = room.target[2] = 0;
    room.fov = 0.8f;
    s.rooms = {room};
    s.hasPlayer = true; s.playerNode = 0; s.hasSpawn = true; s.spawn[0] = 0; s.spawn[1] = 0;
    return s;
}

std::vector<float> Square(float cx, float cz, float half)
{
    return {cx - half, cz - half, cx + half, cz - half, cx + half, cz + half, cx - half, cz + half};
}

void Test_SceneSetTriggerEnabledByName()
{
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    CHECK(DrainEvents(scene) == "Enter StartPad 1");

    CHECK(gge_scene_set_trigger_enabled(scene, "StartPad", 0) == 1);
    CHECK(DrainEvents(scene).empty());             // 이벤트는 다음 Step에서 나온다
    CHECK(gge_scene_step(scene, 0, 0, 0) == 1);
    CHECK(DrainEvents(scene) == "Exit StartPad 1");
    CHECK(gge_scene_set_trigger_enabled(scene, "StartPad", 1) == 1);
    CHECK(gge_scene_step(scene, 0, 0, 0) == 1);
    CHECK(DrainEvents(scene) == "Enter StartPad 1");

    CHECK(gge_scene_set_trigger_enabled(scene, "Nope", 0) == 0);
    CHECK(std::string(gge_last_error()).find("no trigger named 'Nope'") != std::string::npos);
    CHECK(gge_scene_set_trigger_enabled(scene, nullptr, 0) == 0);
    CHECK(gge_scene_set_trigger_enabled(scene, "startpad", 0) == 0);   // 이름은 대소문자를 구분한다
    CHECK(gge_scene_step(scene, 0, 0, 0) == 1);
    CHECK(DrainEvents(scene).empty());                                  // 실패한 호출은 아무것도 바꾸지 않는다

    // Door를 끄면 끝까지 걸어도 Door 이벤트가 없다(StartPad만 나간다).
    CHECK(gge_scene_set_trigger_enabled(scene, "Door", 0) == 1);
    std::string events;
    for (int i = 0; i < 200; ++i) {
        gge_scene_step(scene, 1, 0, 1.0f / 30);
        std::string e = DrainEvents(scene);
        if (!e.empty()) events += (events.empty() ? "" : ", ") + e;
    }
    CHECK(events == "Exit StartPad 1");
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_SceneSetTriggerEnabledAffectsEveryTriggerWithTheName()
{
    fs::path bmp = TempDir() / "scene_trigger.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    Spec spec = RoomWithPlayerSpec(bmp);
    spec.triggers = {{"Zone", Square(0, 0, 2), 1}, {"Zone", Square(0, 0, 3), 2}, {"Other", Square(6, 6, 1), 3}};

    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, WriteScene("triggers.ggs", Serialize(spec)).string().c_str());
    if (!scene) std::printf("  load failed: %s\n", gge_last_error());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    CHECK(DrainEvents(scene) == "Enter Zone 1, Enter Zone 2");

    CHECK(gge_scene_set_trigger_enabled(scene, "Zone", 0) == 2);       // 이름이 같은 둘 다
    gge_scene_step(scene, 0, 0, 0);
    CHECK(DrainEvents(scene) == "Exit Zone 1, Exit Zone 2");
    CHECK(gge_scene_set_trigger_enabled(scene, "Zone", 1) == 2);
    gge_scene_step(scene, 0, 0, 0);
    CHECK(DrainEvents(scene) == "Enter Zone 1, Enter Zone 2");
    CHECK(gge_scene_set_trigger_enabled(scene, "Other", 0) == 1);      // 다른 이름은 건드리지 않는다
    gge_scene_step(scene, 0, 0, 0);
    CHECK(DrainEvents(scene).empty());
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_SceneInsideTriggerQueryCoversEveryTriggerWithTheName()
{
    // 같은 이름 둘 중 하나만 안이어도 "안", 둘 다 밖이면 "밖".
    fs::path bmp = TempDir() / "scene_inside.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    Spec spec = RoomWithPlayerSpec(bmp);
    spec.triggers = {{"Near", Square(0, 0, 2), 1}, {"Near", Square(6, 6, 1), 2}, {"Far", Square(6, 6, 1), 3}, {"Far", Square(-6, -6, 1), 4}};

    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, WriteScene("inside.ggs", Serialize(spec)).string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    CHECK(gge_scene_is_inside_trigger(scene, "Near") == 1);   // 첫째는 안, 둘째는 밖
    CHECK(gge_scene_is_inside_trigger(scene, "Far") == 0);    // 둘 다 밖
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_SceneTeleportPlayerSwitchesRoomsAndCameraImmediately()
{
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    CHECK(DrainEvents(scene) == "Enter StartPad 1");
    const Image hall = Render(r);

    CHECK(gge_scene_teleport_player(scene, 4, 0) == 1);
    float x = 0, z = 0, yaw = 9;
    CHECK(gge_scene_get_player(scene, &x, &z, &yaw) == 1);
    CHECK(x == 4.0f && z == 0.0f && yaw == 0.0f);                       // 방향은 그대로
    CHECK(std::strcmp(gge_scene_active_room_name(scene), "Annex") == 0);
    CHECK(DrainEvents(scene) == "Exit StartPad 1, Enter Door 7");       // (4,0)은 Door 안: 트리거가 새 위치로 바로 갱신된다(Exit 먼저)
    CHECK(Render(r).bgra != hall.bgra);                                // 카메라와 플레이어가 바로 옮겨졌다(Step 없이)

    CHECK(gge_scene_teleport_player(scene, -4, 0) == 1);
    CHECK(std::strcmp(gge_scene_active_room_name(scene), "Hall") == 0);
    CHECK(DrainEvents(scene) == "Exit Door 7, Enter StartPad 1");
    CHECK(Render(r).bgra == hall.bgra);                              // 원래 자리로 돌아오면 같은 화면
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_SceneTeleportPlayerRejectsBadTargetsAndLeavesStateAlone()
{
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    DrainEvents(scene);
    gge_scene_teleport_player(scene, 4, 0);
    DrainEvents(scene);
    const Image before = Render(r);

    CHECK(gge_scene_teleport_player(scene, 100, 100) == 0);            // 어떤 방에도 속하지 않음
    CHECK(std::string(gge_last_error()).find("outside every room") != std::string::npos);
    CHECK(gge_scene_teleport_player(scene, std::nanf(""), 0) == 0);
    CHECK(gge_scene_teleport_player(scene, 0, INFINITY) == 0);
    CHECK(std::string(gge_last_error()).find("finite") != std::string::npos);

    float x = 0, z = 0;
    CHECK(gge_scene_get_player(scene, &x, &z, nullptr) == 1);
    CHECK(x == 4.0f && z == 0.0f);
    CHECK(std::strcmp(gge_scene_active_room_name(scene), "Annex") == 0);
    CHECK(DrainEvents(scene).empty());
    CHECK(Render(r).bgra == before.bgra);
    gge_scene_destroy(scene);

    // 플레이어가 없는 씬
    fs::path bmp = TempDir() / "scene_noplayer.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeScene* none = gge_scene_load(r, WriteScene("noplayer.ggs", Serialize(VisualSpec(bmp))).string().c_str());
    CHECK(none != nullptr);
    if (none) {
        CHECK(gge_scene_teleport_player(none, 0, 0) == 0);
        CHECK(std::string(gge_last_error()).find("no player") != std::string::npos);
        gge_scene_destroy(none);
    }
    gge_renderer_destroy(r);
}

void Test_SceneTeleportPlayerResetsTheHeldInputDirection()
{
    // 입력을 누른 채 방이 바뀌면 컷 직후에는 이전 방 카메라 기준 방향을 유지한다. 텔레포트는 그 기준을 비워야 한다.
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    for (int i = 0; i < 3; ++i) gge_scene_step(scene, 1, 0, 1.0f / 30);   // Hall에서 오른쪽 입력 유지(기준 = Hall 카메라)
    CHECK(gge_scene_teleport_player(scene, 4, 0) == 1);                   // Annex 카메라는 -X를 본다
    for (int i = 0; i < 10; ++i) gge_scene_step(scene, 1, 0, 1.0f / 30);
    float x = 0, z = 0;
    gge_scene_get_player(scene, &x, &z, nullptr);
    // 기준을 비웠다면 Annex 카메라의 오른쪽(-Z)으로 간다. 비우지 않았다면 Hall 기준(+X)으로 계속 간다.
    CHECK(std::fabs(x - 4.0f) < 1e-3f);
    CHECK(z < -0.3f);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

// ---- 크기 0(특이 행렬)과 비유한 변환 ----
void Test_ZeroScaleObjectsRenderLikeVeryThinOnes_AndNeverTouchOtherObjects()
{
    // 크기가 한 축이라도 0이면 월드 행렬이 특이 행렬이라 법선 행렬(역전치)이 정의되지 않는다. 화면이 깨지지 않고(= 아주 얇은 오브젝트와 같은 화면),
    // 세 축이 0이면 아무것도 그려지지 않으며(= 숨긴 것과 같은 화면), 다른 오브젝트의 픽셀은 그대로여야 한다.
    GgeRenderer* r = NewRenderer(160, 120);
    int cube = CubeMesh(r);
    gge_renderer_set_camera(r, 0, 6, 6, 0, 0, 0, 0.8f);
    const float sunDirection[3] = {0, -1, -0.3f}, sunColor[3] = {0.8f, 0.8f, 0.8f};
    gge_renderer_set_directional_light(r, sunDirection, sunColor);
    gge_renderer_set_ambient(r, 0.2f, 0.2f, 0.2f);
    int reference = gge_object_create(r, cube, 0, 0), probe = gge_object_create(r, cube, 0, 0);
    Place(r, reference, -2, 0.5f, 0, 0.5f);
    auto place = [&](float sx, float sy, float sz) {
        const float p[3] = {2, 0.5f, 0}, rot[3] = {0.3f, 0.5f, 0.2f}, s[3] = {sx, sy, sz};   // 회전해 두어 축 정렬 우연에 기대지 않는다
        CHECK(gge_object_set_transform(r, probe, p, rot, s) == 1);
    };

    CHECK(gge_object_set_visible(r, probe, 0) == 1);
    const Image hidden = Render(r);
    CHECK(gge_object_set_visible(r, probe, 1) == 1);

    const float thin = 1e-4f, big = 0.6f;
    const float cases[][3] = {{big, 0, big}, {0, big, big}, {big, big, 0}};
    for (const auto& c : cases) {
        place(c[0] == 0 ? thin : c[0], c[1] == 0 ? thin : c[1], c[2] == 0 ? thin : c[2]);
        const Image thinImage = Render(r);
        place(c[0], c[1], c[2]);
        const Image flat = Render(r);
        // 0 축 = 아주 얇은 축: NaN/깨진 조명 없음. 두께 1e-4인 판의 옆면이 가장자리에서 픽셀 한두 개를 더 그릴 수는 있어 몇 픽셀까지 허용한다
        // (깨졌을 때는 수백 픽셀이 달랐다: 법선 행렬이 NaN이면 판 윗면 전체가 틀어진다).
        const int garbled = DifferingPixels(thinImage, flat);
        if (garbled > 4) std::printf("  zero axis case (%g, %g, %g): %d pixels differ from the very thin object\n", c[0], c[1], c[2], garbled);
        CHECK(garbled <= 4);
        CHECK(DifferingPixels(hidden, thinImage) > 20);               // (공허한 비교가 아니도록) 얇아도 화면에 그려진다
        for (int y = 0; y < flat.height; ++y)
            for (int x = 0; x < flat.width / 2 - 8; ++x)              // 왼쪽 절반 = 다른 오브젝트: 그대로
                CHECK(flat.At(x, y) == hidden.At(x, y));
    }
    place(0, 0, 0);
    CHECK(DifferingPixels(hidden, Render(r)) == 0);                   // 세 축 0이면 아무것도 그려지지 않는다
    gge_renderer_destroy(r);
}

// 같은 장면에서 오브젝트를 "아예 없는 것"으로 만드는 기준: 알파 0은 본체도 그림자도 그리지 않는다(이미 있던 규칙).
void SetAbsent(GgeRenderer* r, int obj, bool absent) { CHECK(gge_object_set_alpha(r, obj, absent ? 0.0f : 1.0f) == 1); }

void Test_ObjectVisibilityIsIndependentOfAlpha_AndHidesSubtreeShadowAndSorting()
{
    GgeRenderer* r = NewRenderer(160, 120);
    int cube = CubeMesh(r);
    gge_renderer_set_camera(r, 0, 6, 6, 0, 0, 0, 0.8f);
    const float sunDirection[3] = {0.4f, -1, -0.3f}, sunColor[3] = {0.8f, 0.8f, 0.8f};
    gge_renderer_set_directional_light(r, sunDirection, sunColor);
    gge_renderer_set_ambient(r, 0.2f, 0.2f, 0.2f);
    CHECK(gge_renderer_set_shadow(r, 1, 0.0f, 0.6f) == 1);
    int floor = gge_object_create(r, cube, 0, 0);
    {
        const float p[3] = {0, -0.5f, 0}, rot[3] = {0, 0, 0}, s[3] = {8, 0.5f, 8};
        CHECK(gge_object_set_transform(r, floor, p, rot, s) == 1);
    }
    auto at = [&](int obj, float x, float y, float z, float scale) {
        const float p[3] = {x, y, z}, rot[3] = {0, 0.4f, 0}, s[3] = {scale, scale, scale};
        CHECK(gge_object_set_transform(r, obj, p, rot, s) == 1);
    };

    // (A) 알파와 별개 상태: 반투명 오브젝트를 숨겼다 켜면 같은 반투명으로 돌아온다. 숨긴 화면 = 알파 0 화면(그림자까지 없다).
    int solid = gge_object_create(r, cube, 0, 0);
    at(solid, 1.5f, 0.6f, 0, 0.6f);
    CHECK(gge_object_set_casts_shadow(r, solid, 1) == 1);
    CHECK(gge_object_set_alpha(r, solid, 0.5f) == 1);
    CHECK(gge_object_is_visible(r, solid) == 1);                      // 처음엔 보임
    const Image visible = Render(r);
    CHECK(gge_object_set_visible(r, solid, 0) == 1);
    CHECK(gge_object_is_visible(r, solid) == 0);
    const Image hidden = Render(r);
    CHECK(gge_object_set_visible(r, solid, 7) == 1);                  // 0이 아니면 보임
    CHECK(Render(r).bgra == visible.bgra);                            // 알파 0.5가 그대로 유지됐다
    CHECK(gge_object_set_alpha(r, solid, 0.0f) == 1);
    CHECK(hidden.bgra == Render(r).bgra);                             // 숨김 = 알파 0과 같은 화면(본체도 그림자도 없다)
    CHECK(DifferingPixels(visible, hidden) > 150);                    // 본체와 그림자가 실제로 사라졌다(공허한 비교가 아니다)
    CHECK(gge_object_set_alpha(r, solid, 1.0f) == 1);
    CHECK(gge_object_set_casts_shadow(r, solid, 0) == 1);
    SetAbsent(r, solid, true);

    // (B) 자손까지 숨긴다. 자식 자신의 플래그는 그대로다.
    int parent = gge_object_create(r, cube, 0, 0);
    int child = gge_object_create(r, cube, 0, parent);
    at(parent, -2, 0.6f, 0, 0.5f);
    {
        const float p[3] = {4, 0, 0}, rot[3] = {0, 0, 0}, s[3] = {1, 1, 1};   // 부모 기준 로컬: 월드 (0, 0.6, 0)
        CHECK(gge_object_set_transform(r, child, p, rot, s) == 1);
    }
    CHECK(gge_object_set_casts_shadow(r, parent, 1) == 1);
    CHECK(gge_object_set_casts_shadow(r, child, 1) == 1);
    const Image both = Render(r);
    SetAbsent(r, parent, true); SetAbsent(r, child, true);
    const Image none = Render(r);
    SetAbsent(r, parent, false); SetAbsent(r, child, false);
    CHECK(DifferingPixels(both, none) > 150);

    CHECK(gge_object_set_visible(r, parent, 0) == 1);
    CHECK(gge_object_is_visible(r, parent) == 0 && gge_object_is_visible(r, child) == 1);   // 자식 자신의 플래그는 보임 그대로
    CHECK(Render(r).bgra == none.bgra);                                // 부모를 숨기면 자식도 안 그려진다(그림자 포함)
    CHECK(gge_object_set_visible(r, parent, 1) == 1);
    CHECK(Render(r).bgra == both.bgra);                                // 다시 켜면 둘 다 돌아온다
    CHECK(gge_object_set_visible(r, child, 0) == 1);                   // 자식만 숨기기
    const Image childHidden = Render(r);
    SetAbsent(r, child, true);
    CHECK(Render(r).bgra == childHidden.bgra);                         // = 자식만 알파 0
    SetAbsent(r, child, false);
    CHECK(gge_object_set_visible(r, child, 1) == 1);
    CHECK(Render(r).bgra == both.bgra);
    SetAbsent(r, parent, true); SetAbsent(r, child, true);

    // (C) 반투명 정렬에서 빠진다: 겹친 반투명 둘 중 하나를 숨기면 그 하나만 알파 0인 화면과 같다.
    int near = gge_object_create(r, cube, 0, 0), far = gge_object_create(r, cube, 0, 0);
    at(far, 0, 0.6f, -0.3f, 0.7f);
    at(near, 0.3f, 0.6f, 0.4f, 0.7f);
    CHECK(gge_object_set_alpha(r, near, 0.5f) == 1);
    CHECK(gge_object_set_alpha(r, far, 0.5f) == 1);
    const Image overlapped = Render(r);
    CHECK(gge_object_set_visible(r, near, 0) == 1);
    const Image nearHidden = Render(r);
    CHECK(gge_object_set_alpha(r, near, 0.0f) == 1);
    CHECK(Render(r).bgra == nearHidden.bgra);
    CHECK(DifferingPixels(overlapped, nearHidden) > 20);

    // (D) 알 수 없는 오브젝트
    CHECK(gge_object_set_visible(r, 9999, 0) == 0);
    CHECK(gge_object_is_visible(r, 9999) == -1);
    CHECK(std::strstr(gge_last_error(), "unknown object id 9999") != nullptr);
    gge_renderer_destroy(r);
}

void Test_ObjectTransformRejectsNonFiniteValuesAndKeepsTheOldOne()
{
    GgeRenderer* r = NewRenderer(64, 48);
    int cube = CubeMesh(r);
    int obj = gge_object_create(r, cube, 0, 0);
    gge_renderer_set_camera(r, 0, 3, 6, 0, 0.5f, 0, 0.8f);
    Place(r, obj, 0.5f, 0.5f, 0);
    const Image before = Render(r);

    const float nan = std::nanf(""), inf = INFINITY;
    const float good[3] = {1, 1, 1}, zero[3] = {0, 0, 0};
    struct Bad { const float* position; const float* rotation; const float* scale; };
    float badPosition[3] = {1, nan, 0}, badRotation[3] = {0, 0, inf}, badScale[3] = {-inf, 1, 1};
    for (const Bad& b : {Bad{badPosition, zero, good}, Bad{zero, badRotation, good}, Bad{zero, zero, badScale}}) {   // 한 값만 비유한
        CHECK(gge_object_set_transform(r, obj, b.position, b.rotation, b.scale) == 0);
        CHECK(std::strstr(gge_last_error(), "must be finite") != nullptr);
        CHECK(Render(r).bgra == before.bgra);                          // 실패해도 이전 변환이 그대로다
    }
    CHECK(gge_object_set_transform(r, obj, zero, zero, good) == 1);   // 유한하면 받는다
    CHECK(Render(r).bgra != before.bgra);
    gge_renderer_destroy(r);
}

// ---- 상호작용 퍼즐용: 노드 위치 읽기/쓰기, 영역 안에 있는지 묻기 ----
void Test_SceneNodePositionCanBeReadMovedAndRestored()
{
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    DrainEvents(scene);

    float x = 0, y = 0, z = 0;
    CHECK(gge_scene_get_node_position(scene, "Pillar", &x, &y, &z) == 1);
    CHECK(x == -2.0f && y == 0.5f && z == 2.0f);                         // 구운 값
    CHECK(gge_scene_get_node_position(scene, "Pillar", nullptr, nullptr, nullptr) == 1);
    CHECK(gge_scene_get_node_position(scene, "Nope", &x, &y, &z) == 0);
    CHECK(std::string(gge_last_error()).find("no node named 'Nope'") != std::string::npos);
    CHECK(gge_scene_get_node_position(scene, nullptr, &x, &y, &z) == 0);

    const Image before = Render(r);
    CHECK(gge_scene_set_node_position(scene, "Pillar", 3, 1, -1) == 1);
    CHECK(gge_scene_get_node_position(scene, "Pillar", &x, &y, &z) == 1 && x == 3.0f && y == 1.0f && z == -1.0f);
    CHECK(Render(r).bgra != before.bgra);                                // 기둥이 실제로 옮겨졌다
    CHECK(gge_scene_set_node_position(scene, "Pillar", -2, 0.5f, 2) == 1);
    CHECK(Render(r).bgra == before.bgra);                                // 제자리로 돌리면 같은 화면(회전/크기가 유지된다)

    // 실패는 아무것도 바꾸지 않는다.
    CHECK(gge_scene_set_node_position(scene, "Nope", 0, 0, 0) == 0);
    CHECK(std::string(gge_last_error()).find("no node named 'Nope'") != std::string::npos);
    CHECK(gge_scene_set_node_position(scene, nullptr, 0, 0, 0) == 0);
    CHECK(gge_scene_set_node_position(scene, "Pillar", std::nanf(""), 0, 0) == 0);
    CHECK(std::string(gge_last_error()).find("finite") != std::string::npos);
    CHECK(gge_scene_set_node_position(scene, "Pillar", 0, INFINITY, 0) == 0);
    CHECK(gge_scene_set_node_position(scene, "Hero", 0, 0, 0) == 0);     // 플레이어 노드는 텔레포트로만
    CHECK(std::string(gge_last_error()).find("is the player") != std::string::npos);
    gge_scene_get_node_position(scene, "Pillar", &x, &y, &z);
    CHECK(x == -2.0f && y == 0.5f && z == 2.0f);
    CHECK(Render(r).bgra == before.bgra);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

// ---- 클립 재생 전환(스킨 노드) ----
struct AnimState {
    std::string clip;
    float time = -1, speed = -1;
    int playing = -1, fading = -1, loop = -1, finished = -1;
};

bool GetAnim(GgeScene* s, const char* node, AnimState& out)
{
    const char* clip = nullptr;
    if (gge_scene_get_node_animation(s, node, &clip, &out.time, &out.playing, &out.speed, &out.fading, &out.loop, &out.finished) != 1)
        return false;
    out.clip = clip ? clip : "<null>";
    return true;
}

AnimState Anim(GgeScene* s, const char* node)
{
    AnimState state;
    CHECK(GetAnim(s, node, state));
    return state;
}

// 바깥 두 줄: Strip(초기 클립 bend, 속도 1.5), Rig(초기 클립 없음 = 레스트), Actor(플레이어, 스킨 노드), Panel(스킨이 아님).
// 클립은 bend(1초), sway(2초), spin(3초)이고 Strip은 화면에서 숨긴다(스스로 움직이므로 화면 비교를 흐리지 않게).
Spec AnimatedSpec(const fs::path& bmp)
{
    Spec s = RoomWithPlayerSpec(bmp);
    s.extraClips = {"sway", "spin"};
    SpecNode rig; rig.name = "Rig"; rig.mesh = 1; rig.pos[0] = 3;
    SpecNode actor; actor.name = "Actor"; actor.mesh = 1; actor.pos[0] = 0;
    s.nodes.push_back(rig);
    s.nodes.push_back(actor);
    s.playerNode = static_cast<int>(s.nodes.size()) - 1;   // 플레이어가 스킨 노드다(걷기/대기 전환이 가능해야 한다)
    return s;
}

void Test_SceneClipPlaybackFollowsTheAnimationPlayerRules()
{
    fs::path bmp = TempDir() / "scene_anim.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer(128, 96);
    GgeScene* scene = gge_scene_load(r, WriteScene("anim.ggs", Serialize(AnimatedSpec(bmp))).string().c_str());
    if (!scene) std::printf("  load failed: %s\n", gge_last_error());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    const float dt = 0.25f;
    CHECK(gge_scene_set_node_visible(scene, "Strip", 0) == 1);
    auto step = [&](int n = 1) { for (int i = 0; i < n; ++i) CHECK(gge_scene_step(scene, 0, 0, dt) == 1); };
    DrainEvents(scene);
    const Image rest = Render(r);

    // 초기 상태: 씬이 정한 clip/speed/playing, 클립이 없는 스킨 노드는 레스트 포즈.
    AnimState strip = Anim(scene, "Strip");
    CHECK(strip.clip == "bend" && strip.speed == 1.5f && strip.playing == 1 && strip.loop == 1 && strip.finished == 0);
    AnimState s = Anim(scene, "Rig");
    CHECK(s.clip.empty() && s.time == 0 && s.playing == 1 && s.speed == 1.0f && s.fading == 0 && s.loop == 1 && s.finished == 0);

    // 재생: 0초부터, 시간은 dt씩, 화면이 바뀐다. 같은 클립을 다시 부르면 무시, restart면 0초부터.
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 0, 0, 1) == 1);
    CHECK(Anim(scene, "Rig").clip == "sway" && Anim(scene, "Rig").time == 0);
    const Image atZero = Render(r);
    step(2);
    CHECK(std::fabs(Anim(scene, "Rig").time - 0.5f) < 1e-5f);
    CHECK(DifferingPixels(atZero, Render(r)) > 5);
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 0, 0, 1) == 1);               // 같은 클립: 무시
    CHECK(std::fabs(Anim(scene, "Rig").time - 0.5f) < 1e-5f);
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 0, 0, 0) == 1);               // loop만 다르게: 무시
    CHECK(Anim(scene, "Rig").loop == 1);
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 0, 1, 1) == 1);               // restart
    CHECK(Anim(scene, "Rig").time == 0);

    // 반복(기본): 길이 2초를 넘으면 감긴다. 한 번 재생: 끝(2초)에서 멈추고 finished, 마지막 포즈를 유지한다.
    step(9);                                                                      // 2.25초 -> 0.25로 감김
    CHECK(std::fabs(Anim(scene, "Rig").time - 0.25f) < 1e-5f && Anim(scene, "Rig").finished == 0);
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 0, 1, 0) == 1);               // restart + 한 번 재생
    CHECK(Anim(scene, "Rig").loop == 0 && Anim(scene, "Rig").time == 0);
    step(7);                                                                      // 1.75초: 아직
    CHECK(Anim(scene, "Rig").finished == 0);
    step(2);                                                                      // 2.25초 -> 끝(2초)에서 멈춤
    s = Anim(scene, "Rig");
    CHECK(s.time == 2.0f && s.finished == 1 && s.playing == 1);
    const Image ended = Render(r);
    step(8);
    CHECK(Render(r).bgra == ended.bgra && Anim(scene, "Rig").time == 2.0f);       // 마지막 포즈 유지
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 0, 0, 0) == 1);               // 끝난 클립을 restart 없이 다시 부르면 무시
    CHECK(Anim(scene, "Rig").finished == 1 && Anim(scene, "Rig").time == 2.0f);
    CHECK(gge_scene_set_clip_speed(scene, "Rig", -1) == 1);                       // 속도 부호를 바꾸면 거꾸로 재생: 더는 끝난 상태가 아니다
    CHECK(Anim(scene, "Rig").finished == 0);
    step(1);
    CHECK(std::fabs(Anim(scene, "Rig").time - 1.75f) < 1e-5f);
    step(20);
    CHECK(Anim(scene, "Rig").time == 0 && Anim(scene, "Rig").finished == 1);      // 0초에서 멈춘다(역방향의 끝)
    CHECK(gge_scene_set_clip_speed(scene, "Rig", 0) == 1);
    CHECK(Anim(scene, "Rig").finished == 0);                                      // 속도 0은 정방향 취급: 0초는 끝이 아니다
    CHECK(gge_scene_set_clip_speed(scene, "Rig", 1) == 1);

    // 크로스페이드: 이전 클립에서 페이드 시간 동안 섞이고, 끝나면 새 클립만 남는다. 레스트 포즈에서 시작하면 페이드 없이 즉시.
    CHECK(gge_scene_stop_clip(scene, "Rig") == 1);
    CHECK(Anim(scene, "Rig").clip.empty() && Render(r).bgra == rest.bgra);        // 정지 = 레스트 포즈
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 1.0f, 0, 1) == 1);
    CHECK(Anim(scene, "Rig").fading == 0);                                        // 레스트에서 시작: 페이드 없음
    step(2);
    CHECK(gge_scene_play_clip(scene, "Rig", "spin", 1.0f, 0, 1) == 1);
    CHECK(Anim(scene, "Rig").fading == 1 && Anim(scene, "Rig").clip == "spin" && Anim(scene, "Rig").time == 0);
    step(3);                                                                      // 0.75초: 아직
    CHECK(Anim(scene, "Rig").fading == 1);
    step(1);                                                                      // 1.0초: 끝
    CHECK(Anim(scene, "Rig").fading == 0);

    // 일시정지: 시간과 페이드 진행이 멈추고, 멈춘 채로 클립을 바꿔도 포즈가 바로 반영된다.
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 2.0f, 0, 1) == 1);
    step(1);
    CHECK(gge_scene_set_clip_playing(scene, "Rig", 0) == 1);
    CHECK(Anim(scene, "Rig").playing == 0);
    const Image paused = Render(r);
    const float pausedTime = Anim(scene, "Rig").time;
    step(8);
    CHECK(Anim(scene, "Rig").time == pausedTime && Anim(scene, "Rig").fading == 1 && Render(r).bgra == paused.bgra);
    CHECK(gge_scene_play_clip(scene, "Rig", "spin", 0, 0, 1) == 1);               // 멈춘 채 전환: Step 없이도 화면이 바뀐다
    CHECK(Render(r).bgra != paused.bgra);
    CHECK(gge_scene_set_clip_playing(scene, "Rig", 1) == 1);
    step(1);
    CHECK(Anim(scene, "Rig").time > 0);

    // 속도: 현재 클립과 페이드 중인 이전 클립에 모두 적용된다(여기서는 시간 진행으로 확인).
    CHECK(gge_scene_stop_clip(scene, "Rig") == 1);
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 0, 0, 1) == 1);
    CHECK(gge_scene_set_clip_speed(scene, "Rig", 2.0f) == 1);
    step(1);
    CHECK(std::fabs(Anim(scene, "Rig").time - 0.5f) < 1e-5f && Anim(scene, "Rig").speed == 2.0f);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_SceneClipPlaybackRejectsBadRequestsAndChangesNothing()
{
    fs::path bmp = TempDir() / "scene_anim_err.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer(128, 96);
    GgeScene* scene = gge_scene_load(r, WriteScene("anim_err.ggs", Serialize(AnimatedSpec(bmp))).string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    CHECK(gge_scene_set_node_visible(scene, "Strip", 0) == 1);
    CHECK(gge_scene_play_clip(scene, "Rig", "sway", 0, 0, 1) == 1);
    gge_scene_step(scene, 0, 0, 0.25f);
    const Image before = Render(r);
    const AnimState stateBefore = Anim(scene, "Rig");
    auto error = [&] { return std::string(gge_last_error()); };
    auto has = [&](const char* part) { return error().find(part) != std::string::npos; };
    const float nan = std::nanf(""), inf = INFINITY;

    CHECK(gge_scene_play_clip(scene, "Nope", "sway", 0, 0, 1) == 0 && has("no node named 'Nope'"));
    CHECK(gge_scene_play_clip(scene, nullptr, "sway", 0, 0, 1) == 0 && has("no node named ''"));
    CHECK(gge_scene_play_clip(scene, "Panel", "sway", 0, 0, 1) == 0 && has("has no skeleton animation"));   // 스킨이 아닌 노드
    CHECK(gge_scene_stop_clip(scene, "Panel") == 0 && has("has no skeleton animation"));
    CHECK(gge_scene_set_clip_speed(scene, "Panel", 1) == 0 && has("has no skeleton animation"));
    CHECK(gge_scene_set_clip_playing(scene, "Panel", 1) == 0 && has("has no skeleton animation"));
    AnimState none;
    CHECK(!GetAnim(scene, "Panel", none) && has("has no skeleton animation"));
    CHECK(!GetAnim(scene, "Nope", none) && has("no node named 'Nope'"));
    CHECK(gge_scene_play_clip(scene, "Rig", "nope", 0, 0, 1) == 0 && has("no clip named 'nope' in node 'Rig' (available: bend, sway, spin)"));
    CHECK(gge_scene_play_clip(scene, "Rig", "Sway", 0, 0, 1) == 0 && has("no clip named 'Sway'"));          // 대소문자 구분
    CHECK(gge_scene_play_clip(scene, "Rig", nullptr, 0, 0, 1) == 0 && has("no clip named ''"));
    for (float bad : {-0.01f, nan, inf, 60.01f, 1000.0f}) {
        CHECK(gge_scene_play_clip(scene, "Rig", "spin", bad, 0, 1) == 0);
        CHECK(has("fade seconds must be finite and between 0 and 60"));
    }
    CHECK(gge_scene_set_clip_speed(scene, "Rig", nan) == 0 && has("clip speed must be finite"));
    CHECK(gge_scene_set_clip_speed(scene, "Rig", -inf) == 0 && has("clip speed must be finite"));
    const AnimState stateAfter = Anim(scene, "Rig");
    CHECK(stateAfter.clip == stateBefore.clip && stateAfter.time == stateBefore.time && stateAfter.speed == stateBefore.speed &&
          stateAfter.fading == stateBefore.fading && stateAfter.playing == stateBefore.playing);              // 실패는 아무것도 바꾸지 않는다
    CHECK(Render(r).bgra == before.bgra);

    // 경계: 페이드 0과 60은 받는다. 음수 속도도 받는다.
    CHECK(gge_scene_play_clip(scene, "Rig", "spin", 0.0f, 0, 1) == 1);
    CHECK(gge_scene_play_clip(scene, "Rig", "bend", 60.0f, 0, 1) == 1 && Anim(scene, "Rig").fading == 1);
    CHECK(gge_scene_set_clip_speed(scene, "Rig", -2.5f) == 1 && Anim(scene, "Rig").speed == -2.5f);

    // 이름이 같은 노드가 여럿이면 첫 노드만(다른 노드의 상태는 그대로)은 위치 API와 같은 규칙이라 여기서는 클립 목록 문구만 본다.
    gge_scene_destroy(scene);

    // 클립이 많으면 오류에 앞 20개와 총 개수만 싣는다.
    Spec many = AnimatedSpec(bmp);
    many.extraClips.clear();
    for (int i = 0; i < 25; ++i) many.extraClips.push_back("c" + std::to_string(i));
    GgeScene* big = gge_scene_load(r, WriteScene("anim_many.ggs", Serialize(many)).string().c_str());
    CHECK(big != nullptr);
    if (big) {
        CHECK(gge_scene_play_clip(big, "Rig", "missing", 0, 0, 1) == 0);
        CHECK(has("available: bend, c0, c1,") && has("c18, ...") && has("(20 of 26 clips shown)") && !has("c19,") && !has("c24"));
        gge_scene_destroy(big);
    }
    gge_renderer_destroy(r);
}

void Test_PlayerNodeCanChangeClipsWhileTheMovementSystemOwnsItsTransform()
{
    fs::path bmp = TempDir() / "scene_anim_player.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer(128, 96);
    GgeScene* scene = gge_scene_load(r, WriteScene("anim_player.ggs", Serialize(AnimatedSpec(bmp))).string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    CHECK(Anim(scene, "Actor").clip.empty());
    CHECK(gge_scene_play_clip(scene, "Actor", "sway", 0.2f, 0, 1) == 1);          // 플레이어 노드: 허용
    float x0 = 0, x1 = 0;
    gge_scene_get_player(scene, &x0, nullptr, nullptr);
    for (int i = 0; i < 10; ++i) gge_scene_step(scene, 1, 0, 1.0f / 30);          // 걷는다
    gge_scene_get_player(scene, &x1, nullptr, nullptr);
    CHECK(x1 > x0 + 0.5f);                                                         // 이동 시스템이 위치를 그대로 소유한다
    AnimState state = Anim(scene, "Actor");
    CHECK(state.clip == "sway" && std::fabs(state.time - 10.0f / 30) < 1e-4f);    // 애니메이션은 이동과 독립으로 진행한다
    CHECK(gge_scene_play_clip(scene, "Actor", "spin", 0.15f, 0, 1) == 1 && Anim(scene, "Actor").fading == 1);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_PlayerSpeedIsTheActualMovementOfTheLastStep()
{
    fs::path bmp = TempDir() / "scene_speed.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer(64, 48);
    Spec gate = RoomWithPlayerSpec(bmp);   // 방(-10..10), 오른쪽(+X)으로 걸으면 두 겹 문 "Gate"(x 2..3)에 막힌다
    gate.colliders = {{"Gate", 2, -10, 2.5f, 10}, {"Gate", 2.5f, -10, 3, 10}, {"Wall", 6, -10, 7, 10}};
    GgeScene* scene = gge_scene_load(r, WriteScene("speed.ggs", Serialize(gate)).string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    const float dt = 1.0f / 30;
    auto speed = [&] { float v = -1; CHECK(gge_scene_get_player_speed(scene, &v) == 1); return v; };

    CHECK(speed() == 0);                                                            // 로드 직후
    CHECK(gge_scene_step(scene, 0, 0, dt) == 1 && speed() == 0);                    // 입력 없음 = 멈춤
    for (int i = 0; i < 5; ++i) gge_scene_step(scene, 1, 0, dt);
    CHECK(std::fabs(speed() - 3.0f) < 1e-3f);                                        // 걷기: 플레이어 속도 3
    CHECK(gge_scene_step(scene, 1, 0, 0.0f) == 1 && speed() == 0);                  // dt = 0이면 0(0으로 나누지 않는다)

    // 정면으로 막힘: 문(x=2)에서 반지름만큼 떨어져 멈춘 뒤 계속 밀면 0.
    for (int i = 0; i < 60; ++i) gge_scene_step(scene, 1, 0, dt);
    CHECK(speed() < 1e-3f);

    // 벽을 따라 미끄러짐: 벽(+X 쪽)을 향해 대각선으로 밀면 벽과 나란한 성분이 남아 0이 아니다(입력 오른쪽+위 = +X, -Z).
    gge_scene_step(scene, 1, 1, dt);
    CHECK(speed() > 1.0f && speed() < 3.0f);

    // 텔레포트 직후는 0, 다음 걸음부터 다시 실제 이동량.
    for (int i = 0; i < 5; ++i) gge_scene_step(scene, -1, 0, dt);
    CHECK(speed() > 1.0f);
    CHECK(gge_scene_teleport_player(scene, -5, 3) == 1);
    CHECK(speed() == 0);                                                            // 순간이동이 속도 폭주를 만들지 않는다
    gge_scene_step(scene, 1, 0, dt);
    CHECK(std::fabs(speed() - 3.0f) < 1e-3f);

    // 켜진 충돌체가 겹친 플레이어를 밀어내는 이동도 이동으로 센다(정의대로: 실제로 움직인 거리 / dt).
    CHECK(gge_scene_set_collider_enabled(scene, "Gate", 0) == 2);
    CHECK(gge_scene_teleport_player(scene, 2.2f, 0) == 1);
    CHECK(gge_scene_set_collider_enabled(scene, "Gate", 1) == 2);
    gge_scene_step(scene, 0, 0, dt);
    CHECK(speed() > 1.0f);
    gge_scene_destroy(scene);

    // 플레이어가 없는 씬: 0을 반환한다.
    GgeScene* noPlayer = gge_scene_load(r, WriteScene("speed_none.ggs", Serialize(VisualSpec(bmp))).string().c_str());
    CHECK(noPlayer != nullptr);
    if (noPlayer) {
        float v = 7;
        CHECK(gge_scene_get_player_speed(noPlayer, &v) == 0);
        gge_scene_destroy(noPlayer);
    }
    gge_renderer_destroy(r);
}

bool NearVec(const float* v, float x, float y, float z) { return std::fabs(v[0] - x) < 1e-5f && std::fabs(v[1] - y) < 1e-5f && std::fabs(v[2] - z) < 1e-5f; }

void Test_SceneNodeRotationScaleAndVisibilityFollowThePositionRules()
{
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    DrainEvents(scene);
    const Image hall = Render(r);   // 기둥(Pillar)이 보이는 Hall 화면
    float v[3] = {9, 9, 9};

    // 보이기/숨기기: 플래그 읽기/쓰기, 화면 반영, 기둥만 사라진다.
    CHECK(gge_scene_get_node_visible(scene, "Pillar") == 1);
    CHECK(gge_scene_set_node_visible(scene, "Pillar", 0) == 1);
    CHECK(gge_scene_get_node_visible(scene, "Pillar") == 0);
    const Image pillarHidden = Render(r);
    CHECK(DifferingPixels(hall, pillarHidden) > 30);
    CHECK(gge_scene_set_node_visible(scene, "Pillar", 1) == 1);
    CHECK(Render(r).bgra == hall.bgra);

    // 회전: 읽기/쓰기/되돌리기(회전만 바뀌고 위치/크기는 그대로).
    CHECK(gge_scene_get_node_rotation(scene, "Pillar", &v[0], &v[1], &v[2]) == 1 && NearVec(v, 0, 0, 0));
    CHECK(gge_scene_set_node_rotation(scene, "Pillar", 0, 0.7f, 0.2f) == 1);
    CHECK(gge_scene_get_node_rotation(scene, "Pillar", &v[0], &v[1], &v[2]) == 1 && NearVec(v, 0, 0.7f, 0.2f));
    CHECK(gge_scene_get_node_position(scene, "Pillar", &v[0], &v[1], &v[2]) == 1 && NearVec(v, -2, 0.5f, 2));
    CHECK(gge_scene_get_node_scale(scene, "Pillar", &v[0], &v[1], &v[2]) == 1 && NearVec(v, 0.5f, 0.5f, 0.5f));
    CHECK(DifferingPixels(hall, Render(r)) > 20);
    CHECK(gge_scene_set_node_rotation(scene, "Pillar", 0, 0, 0) == 1);
    CHECK(Render(r).bgra == hall.bgra);

    // 크기: 읽기/쓰기. 0은 받고(세 축 0이면 숨긴 화면과 같다), 되돌리면 원래 화면이다.
    CHECK(gge_scene_set_node_scale(scene, "Pillar", 1, 1, 1) == 1);
    CHECK(gge_scene_get_node_scale(scene, "Pillar", &v[0], &v[1], &v[2]) == 1 && NearVec(v, 1, 1, 1));
    CHECK(DifferingPixels(hall, Render(r)) > 20);
    CHECK(gge_scene_set_node_scale(scene, "Pillar", 0, 0, 0) == 1);
    CHECK(gge_scene_get_node_scale(scene, "Pillar", &v[0], &v[1], &v[2]) == 1 && NearVec(v, 0, 0, 0));
    CHECK(Render(r).bgra == pillarHidden.bgra);
    CHECK(gge_scene_set_node_scale(scene, "Pillar", 0.5f, 0.5f, 0.5f) == 1);
    CHECK(Render(r).bgra == hall.bgra);

    // 실패는 사유와 함께 0/-1이고 아무것도 바꾸지 않는다.
    const float nan = std::nanf(""), inf = INFINITY;
    CHECK(gge_scene_get_node_rotation(scene, "Nope", &v[0], &v[1], &v[2]) == 0 && std::string(gge_last_error()).find("no node named 'Nope'") != std::string::npos);
    CHECK(gge_scene_get_node_scale(scene, "Nope", &v[0], &v[1], &v[2]) == 0);
    CHECK(gge_scene_get_node_visible(scene, "Nope") == -1 && std::string(gge_last_error()).find("no node named 'Nope'") != std::string::npos);
    CHECK(gge_scene_get_node_visible(scene, nullptr) == -1);
    CHECK(gge_scene_set_node_rotation(scene, "Nope", 0, 0, 0) == 0);
    CHECK(gge_scene_set_node_scale(scene, "Nope", 1, 1, 1) == 0);
    CHECK(gge_scene_set_node_visible(scene, "Nope", 0) == 0 && std::string(gge_last_error()).find("no node named 'Nope'") != std::string::npos);
    CHECK(gge_scene_set_node_rotation(scene, "Pillar", nan, 0, 0) == 0 && std::string(gge_last_error()).find("rotation must be finite") != std::string::npos);
    CHECK(gge_scene_set_node_scale(scene, "Pillar", 1, inf, 1) == 0 && std::string(gge_last_error()).find("scale must be finite") != std::string::npos);
    CHECK(gge_scene_set_node_rotation(scene, "Hero", 0, 1, 0) == 0 && std::string(gge_last_error()).find("is the player") != std::string::npos);   // 플레이어는 회전/크기를 스크립트가 못 바꾼다
    CHECK(gge_scene_set_node_scale(scene, "Hero", 2, 2, 2) == 0 && std::string(gge_last_error()).find("is the player") != std::string::npos);
    CHECK(Render(r).bgra == hall.bgra);

    // 플레이어는 숨길 수 있다(컷신/퇴장): 숨긴 채로도 걷고 이벤트가 나온다.
    CHECK(gge_scene_set_node_visible(scene, "Hero", 0) == 1 && gge_scene_get_node_visible(scene, "Hero") == 0);
    const Image heroHidden = Render(r);
    CHECK(DifferingPixels(hall, heroHidden) > 5);
    CHECK(gge_scene_step(scene, 1, 0, 1.0f / 30) == 1);
    float x = 0;
    gge_scene_get_player(scene, &x, nullptr, nullptr);
    CHECK(x > -4.0f);
    CHECK(gge_scene_set_node_visible(scene, "Hero", 1) == 1);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_SceneNodeWritesTouchOnlyTheFirstNodeWithTheName()
{
    fs::path bmp = TempDir() / "scene_dup.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    Spec spec = RoomWithPlayerSpec(bmp);
    SpecNode left, right;
    left.name = right.name = "Dup";
    left.mesh = right.mesh = 0;
    left.pos[0] = -2; left.pos[1] = 1;
    right.pos[0] = 2; right.pos[1] = 1;
    spec.nodes.push_back(left);
    spec.nodes.push_back(right);

    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, WriteScene("dup.ggs", Serialize(spec)).string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    const int first = gge_scene_find_object(scene, "Dup");
    CHECK(first > 0);
    const Image before = Render(r);
    auto rightHalfSame = [&](const Image& now) {
        for (int y = 0; y < now.height; ++y)
            for (int x = now.width / 2 + 4; x < now.width; ++x)
                if (now.At(x, y) != before.At(x, y)) return false;
        return true;
    };

    CHECK(gge_scene_set_node_rotation(scene, "Dup", 0, 1.0f, 0) == 1);
    Image now = Render(r);
    CHECK(DifferingPixels(before, now) > 10 && rightHalfSame(now));              // 왼쪽 노드만 돌아갔다
    CHECK(gge_scene_set_node_rotation(scene, "Dup", 0, 0, 0) == 1);
    CHECK(gge_scene_set_node_scale(scene, "Dup", 2, 2, 2) == 1);
    now = Render(r);
    CHECK(DifferingPixels(before, now) > 10 && rightHalfSame(now));              // 왼쪽 노드만 커졌다
    CHECK(gge_scene_set_node_scale(scene, "Dup", 1, 1, 1) == 1);

    CHECK(gge_scene_set_node_visible(scene, "Dup", 0) == 1);
    CHECK(gge_object_is_visible(r, first) == 0);
    CHECK(gge_object_is_visible(r, first + 1) == 1);                             // 같은 이름의 두 번째 노드는 그대로 보인다
    CHECK(gge_scene_get_node_visible(scene, "Dup") == 0);                        // 읽기도 첫 노드
    now = Render(r);
    CHECK(DifferingPixels(before, now) > 10 && rightHalfSame(now));
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_SceneAnswersWhetherThePlayerIsInsideATrigger()
{
    GgeRenderer* r = NewRenderer(160, 120);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    DrainEvents(scene);

    CHECK(gge_scene_is_inside_trigger(scene, "StartPad") == 1);          // 스폰이 StartPad 안
    CHECK(gge_scene_is_inside_trigger(scene, "Door") == 0);
    CHECK(gge_scene_is_inside_trigger(scene, "Nope") == -1);             // 0(밖)과 구별되는 실패
    CHECK(std::string(gge_last_error()).find("no trigger named 'Nope'") != std::string::npos);
    CHECK(gge_scene_is_inside_trigger(scene, nullptr) == -1);

    CHECK(gge_scene_teleport_player(scene, 4, 0) == 1);                  // 텔레포트도 바로 반영된다
    CHECK(gge_scene_is_inside_trigger(scene, "StartPad") == 0);
    CHECK(gge_scene_is_inside_trigger(scene, "Door") == 1);
    CHECK(gge_scene_set_trigger_enabled(scene, "Door", 0) == 1);
    CHECK(gge_scene_is_inside_trigger(scene, "Door") == 1);              // 이벤트와 같은 시점: 다음 step까지는 그대로
    gge_scene_step(scene, 0, 0, 0);
    CHECK(gge_scene_is_inside_trigger(scene, "Door") == 0);              // 꺼진 트리거는 안에 있어도 밖으로 본다(Exit와 함께)
    CHECK(DrainEvents(scene) == "Exit StartPad 1, Enter Door 7, Exit Door 7");
    CHECK(gge_scene_set_trigger_enabled(scene, "Door", 1) == 1);
    gge_scene_step(scene, 0, 0, 0);
    CHECK(gge_scene_is_inside_trigger(scene, "Door") == 1);              // 다시 켜면 Enter와 함께 안
    CHECK(DrainEvents(scene) == "Enter Door 7");
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

// 방 하나(-10..10)에서 오른쪽(+X)으로 걸으면 두 겹 문 "Gate"(x 2..3, 이름이 같은 둘)에 막히고, 더 가면 "Wall"(x 6..7)에 막힌다.
Spec GateSpec(const fs::path& bmp, uint32_t version = 2)
{
    Spec s = RoomWithPlayerSpec(bmp);
    s.version = version;
    s.colliders = {{"Gate", 2, -10, 2.5f, 10}, {"Gate", 2.5f, -10, 3, 10}, {"Wall", 6, -10, 7, 10}};
    return s;
}

float WalkRight(GgeScene* scene, int frames)
{
    for (int i = 0; i < frames; ++i) gge_scene_step(scene, 1, 0, 1.0f / 30);
    float x = 0;
    gge_scene_get_player(scene, &x, nullptr, nullptr);
    return x;
}

void Test_SceneNamedCollidersOpenAndCloseLikeADoor()
{
    fs::path bmp = TempDir() / "scene_gate.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer(96, 72);
    GgeScene* scene = gge_scene_load(r, WriteScene("gate.ggs", Serialize(GateSpec(bmp))).string().c_str());
    if (!scene) std::printf("  load failed: %s\n", gge_last_error());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }

    CHECK(std::fabs(WalkRight(scene, 60) - 1.6f) < 1e-3f);                   // 닫힌 문에 막힌다(문 x=2 - 반지름 0.4)
    CHECK(gge_scene_set_collider_enabled(scene, "Gate", 0) == 2);            // 같은 이름 둘 다 열린다
    CHECK(std::fabs(WalkRight(scene, 100) - 5.6f) < 1e-3f);                  // 문을 지나 "Wall"에는 여전히 막힌다(이름이 다르면 안 건드린다)

    // 열린 문 안으로 들어가 서 있다가 닫으면 가장 가까운 면 바깥으로 밀려난다(입력이 0인 프레임에서도).
    CHECK(gge_scene_teleport_player(scene, 2.2f, 0) == 1);
    CHECK(gge_scene_set_collider_enabled(scene, "Gate", 1) == 2);
    CHECK(gge_scene_step(scene, 0, 0, 1.0f / 30) == 1);
    float x = 0, z = 9;
    gge_scene_get_player(scene, &x, &z, nullptr);
    CHECK(x <= 1.6f + 1e-3f && std::fabs(z) < 1e-5f);
    CHECK(std::fabs(WalkRight(scene, 30) - 1.6f) < 1e-3f);                   // 다시 닫힌 문에 막힌다

    CHECK(gge_scene_set_collider_enabled(scene, "Gate", 0) == 2);
    CHECK(gge_scene_set_collider_enabled(scene, "Gate", 0) == 2);            // 이미 열려 있어도 성공(멱등), 개수는 같다
    CHECK(WalkRight(scene, 30) > 3.5f);

    // 실패는 사유와 함께 0이고 아무것도 바꾸지 않는다.
    CHECK(gge_scene_set_collider_enabled(scene, "", 0) == 0);
    CHECK(std::string(gge_last_error()).find("must not be empty") != std::string::npos);
    CHECK(gge_scene_set_collider_enabled(scene, nullptr, 0) == 0);
    CHECK(std::string(gge_last_error()).find("must not be empty") != std::string::npos);
    CHECK(gge_scene_set_collider_enabled(scene, "Nope", 0) == 0);
    CHECK(std::string(gge_last_error()).find("no collider named 'Nope'") != std::string::npos);
    CHECK(gge_scene_set_collider_enabled(scene, "gate", 0) == 0);            // 이름은 대소문자를 구분한다
    CHECK(WalkRight(scene, 100) > 5.5f);                                     // Gate는 여전히 열려 있다
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_SceneVersion1FilesHaveUnnamedCollidersThatStillBlock()
{
    fs::path bmp = TempDir() / "scene_gate1.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    GgeRenderer* r = NewRenderer(96, 72);
    GgeScene* scene = gge_scene_load(r, WriteScene("gate_v1.ggs", Serialize(GateSpec(bmp, 1))).string().c_str());
    if (!scene) std::printf("  load failed: %s\n", gge_last_error());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    CHECK(std::fabs(WalkRight(scene, 60) - 1.6f) < 1e-3f);                   // 이름은 없어도 충돌은 그대로 동작한다
    CHECK(gge_scene_set_collider_enabled(scene, "Gate", 0) == 0);            // 이름이 없으므로 지정할 수 없다
    CHECK(std::string(gge_last_error()).find("no collider named 'Gate'") != std::string::npos);
    CHECK(gge_scene_set_collider_enabled(scene, "", 0) == 0);                // 빈 이름 = "이름 없음"을 가리킬 수도 없다
    CHECK(std::string(gge_last_error()).find("must not be empty") != std::string::npos);
    CHECK(std::fabs(WalkRight(scene, 30) - 1.6f) < 1e-3f);
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

void Test_BakedVersion1SampleLoadsAndBehavesLikeTheCurrentSample()
{
    // assets/samples/play_v1.ggs는 형식 version 1 시절의 play.ggs를 그대로 보존한 파일이다(재생성하지 않는다). 현재 play.ggs(version 2)와
    // 헤더 버전만 다르고 같은 게임이어야 한다.
    fs::path v1Path = fs::path(GGE_TEST_DATA_DIR) / "../../assets/samples/play_v1.ggs";
    std::vector<uint8_t> v1Bytes = ReadFileBytes(v1Path), v2Bytes = ReadFileBytes(SamplePlayScene());
    CHECK(v1Bytes.size() > 8 && v2Bytes.size() > 8);
    uint32_t v1Version = 0, v2Version = 0;
    if (v1Bytes.size() > 8) std::memcpy(&v1Version, v1Bytes.data() + 4, 4);
    if (v2Bytes.size() > 8) std::memcpy(&v2Version, v2Bytes.data() + 4, 4);
    CHECK(v1Version == 1 && v2Version == 2);

    GgeRenderer* r1 = NewRenderer(96, 72);
    GgeRenderer* r2 = NewRenderer(96, 72);
    GgeScene* v1 = gge_scene_load(r1, v1Path.string().c_str());
    GgeScene* v2 = gge_scene_load(r2, SamplePlayScene().string().c_str());
    if (!v1) std::printf("  v1 load failed: %s\n", gge_last_error());
    CHECK(v1 != nullptr && v2 != nullptr);
    if (!v1 || !v2) { gge_renderer_destroy(r1); gge_renderer_destroy(r2); return; }

    std::string events1 = DrainEvents(v1), events2 = DrainEvents(v2);
    for (int i = 0; i < 200; ++i) {
        gge_scene_step(v1, 1, 0, 1.0f / 30);
        gge_scene_step(v2, 1, 0, 1.0f / 30);
        std::string e1 = DrainEvents(v1), e2 = DrainEvents(v2);
        if (!e1.empty()) events1 += (events1.empty() ? "" : ", ") + e1;
        if (!e2.empty()) events2 += (events2.empty() ? "" : ", ") + e2;
    }
    CHECK(events1 == events2 && events1 == "Enter StartPad 1, Exit StartPad 1, Enter Door 7, Exit Door 7");
    float x1 = 0, z1 = 0, x2 = 0, z2 = 0;
    gge_scene_get_player(v1, &x1, &z1, nullptr);
    gge_scene_get_player(v2, &x2, &z2, nullptr);
    CHECK(x1 == x2 && z1 == z2 && std::fabs(x1 - 5.6f) < 1e-3f);             // 같은 동쪽 벽에서 멈춘다
    CHECK(std::strcmp(gge_scene_active_room_name(v1), gge_scene_active_room_name(v2)) == 0);

    // 이름은 version 2에만 있다: 샘플의 충돌체 "Pillar"/"WallEast"는 v2에서만 켜고 끌 수 있다.
    CHECK(gge_scene_set_collider_enabled(v1, "Pillar", 0) == 0);
    CHECK(gge_scene_set_collider_enabled(v2, "Pillar", 0) == 1);
    CHECK(gge_scene_set_collider_enabled(v2, "WallEast", 0) == 1);
    CHECK(WalkRight(v2, 100) > 6.2f);                                        // 동쪽 벽을 열면 방 밖(x > 6)으로 나간다
    gge_scene_destroy(v1);
    gge_scene_destroy(v2);
    gge_renderer_destroy(r1);
    gge_renderer_destroy(r2);
}

void Test_GoldenPlaySceneFromTheBakedSample()
{
    GgeRenderer* r = NewRenderer(320, 240);
    GgeScene* scene = gge_scene_load(r, SamplePlayScene().string().c_str());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(r); return; }
    gge_renderer_set_retro(r, 1, 5, 1, 1);
    for (int i = 0; i < 100; ++i) gge_scene_step(scene, 1, 0, 1.0f / 30);   // Annex 방으로 걸어 들어간다
    CHECK(MatchesGolden("play_scene_320x240", Render(r)));
    gge_scene_destroy(scene);
    gge_renderer_destroy(r);
}

// ---- .ggs 로더의 한도/경계 (외부 파일을 읽는 코드라 "경계 바로 안/밖"을 개수와 길이마다 확인한다) ----
// 파일 앞부분(텍스처 0, 메시 0, 노드 0, 기본 조명)까지 쓴 작성기. 이어서 방/충돌체/트리거 개수 등을 직접 쓴다.
GgsWriter PrefixUpToLighting(uint32_t version = 2)
{
    GgsWriter w;
    w.Raw("GGS1", 4);
    w.U32(version);
    w.U32(0);  // textures
    w.U32(0);  // meshes
    w.U32(0);  // nodes
    w.Vec3(0.2f, 0.2f, 0.25f); w.Vec3(-0.5f, -1.0f, -0.3f); w.Vec3(0.5f, 0.5f, 0.45f);
    return w;
}

// 한 줄로 에러 메시지를 돌려주고(로드가 성공하면 "OK"), 성공했으면 곧바로 해제한다.
std::string TryLoad(GgeRenderer* r, const std::string& name, const std::vector<uint8_t>& bytes)
{
    GgeScene* scene = gge_scene_load(r, WriteScene(name, bytes).string().c_str());
    if (scene) {
        gge_scene_destroy(scene);
        return "OK";
    }
    return gge_last_error();
}

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

void Test_SceneCountLimitsAreCheckedAtTheirBoundaries()
{
    GgeRenderer* r = NewRenderer(64, 48);
    auto expect = [&](const char* label, const std::string& got, const char* part) {
        bool ok = Has(got, part);
        if (!ok) std::printf("  %s: got '%s', expected '%s'\n", label, got.c_str(), part);
        CHECK(ok);
    };
    // 한도 개수는 "구조상 허용"이라 데이터가 모자라 truncated가 나와야 하고, 한도 + 1은 "too many"여야 한다.
    {
        for (uint32_t n : {65536u, 65537u}) {
            GgsWriter w; w.Raw("GGS1", 4); w.U32(1); w.U32(n);
            expect("textures", TryLoad(r, "limit.ggs", w.Sealed()), n == 65536u ? "truncated while reading textures" : "too many textures");
        }
        for (uint32_t n : {65536u, 65537u}) {
            GgsWriter w; w.Raw("GGS1", 4); w.U32(1); w.U32(0); w.U32(n);
            expect("meshes", TryLoad(r, "limit.ggs", w.Sealed()), n == 65536u ? "truncated while reading meshes" : "too many meshes");
        }
        for (uint32_t n : {1u << 20, (1u << 20) + 1}) {
            GgsWriter w; w.Raw("GGS1", 4); w.U32(1); w.U32(0); w.U32(0); w.U32(n);
            expect("nodes", TryLoad(r, "limit.ggs", w.Sealed()), n == (1u << 20) ? "truncated while reading nodes" : "too many nodes");
        }
        for (uint32_t n : {8u, 9u}) {
            GgsWriter w = PrefixUpToLighting(); w.U32(n);
            expect("point lights", TryLoad(r, "limit.ggs", w.Sealed()), n == 8u ? "truncated while reading point lights" : "too many point lights");
        }
        // 방/충돌체/트리거: 앞 절을 정상으로 채우고 해당 개수만 쓴다.
        auto afterLighting = [&](uint32_t version = 2) { GgsWriter w = PrefixUpToLighting(version); w.U32(0); w.U8(0); w.F(0); w.F(0.5f); return w; };  // 점광원 0, 그림자 꺼짐
        for (uint32_t n : {65536u, 65537u}) {
            GgsWriter w = afterLighting(); w.U32(n);
            expect("rooms", TryLoad(r, "limit.ggs", w.Sealed()), n == 65536u ? "truncated while reading rooms" : "too many rooms");
            for (uint32_t version : {1u, 2u}) {   // 충돌체 레코드는 버전마다 다르므로 두 형식 모두에서 경계를 본다
                GgsWriter c = afterLighting(version); c.U32(0); c.U32(n);
                expect("colliders", TryLoad(r, "limit.ggs", c.Sealed()), n == 65536u ? "truncated while reading colliders" : "too many colliders");
            }
            GgsWriter t = afterLighting(); t.U32(0); t.U32(0); t.U32(n);
            expect("triggers", TryLoad(r, "limit.ggs", t.Sealed()), n == 65536u ? "truncated while reading triggers" : "too many triggers");
        }
        // 충돌체 레코드의 최소 크기: 버전 2는 이름(길이 4바이트) + 좌표 16바이트 = 20, 버전 1은 16. 개수 x 최소 크기 > 남은 바이트면 큰 배열 전에 거부한다.
        for (uint32_t version : {1u, 2u}) {
            GgsWriter c = afterLighting(version); c.U32(0); c.U32(1000);   // 방 0개, 충돌체 1000개, 데이터 없음
            expect("colliders without data", TryLoad(r, "limit.ggs", c.Sealed()), "truncated while reading colliders");
        }
        // 개수에 비해 남은 바이트가 모자라면(개수 x 항목 최소 크기 > 남은 바이트) 큰 배열을 만들기 전에 거부한다.
        GgsWriter w; w.Raw("GGS1", 4); w.U32(1); w.U32(0); w.U32(0); w.U32(1000);   // 노드 1000개, 데이터 없음
        expect("nodes without data", TryLoad(r, "limit.ggs", w.Sealed()), "truncated while reading nodes");
    }
    gge_renderer_destroy(r);
}

// 정점 1개짜리 메시 앞부분을 쓴다(스킨이면 정점당 조인트/가중치까지). 이어서 인덱스 수 등을 호출자가 정한다.
void WriteMeshHeader(GgsWriter& w, bool skinned, uint32_t vertexCount, uint32_t indexCount)
{
    w.U32(0);                        // textures
    w.U32(1);                        // meshes
    w.U8(skinned ? 1 : 0); w.U32(vertexCount); w.U32(indexCount);
}

void Test_SceneMeshAndSkeletonLimitsAreCheckedAtTheirBoundaries()
{
    GgeRenderer* r = NewRenderer(64, 48);
    auto expect = [&](const char* label, const std::string& got, const char* part) {
        bool ok = Has(got, part);
        if (!ok) std::printf("  %s: got '%s', expected '%s'\n", label, got.c_str(), part);
        CHECK(ok);
    };
    auto start = [] { GgsWriter w; w.Raw("GGS1", 4); w.U32(1); return w; };

    { GgsWriter w = start(); WriteMeshHeader(w, false, 0, 3);          expect("zero vertices", TryLoad(r, "m.ggs", w.Sealed()), "invalid mesh header"); }
    { GgsWriter w = start(); WriteMeshHeader(w, false, 1, 0);          expect("zero indices", TryLoad(r, "m.ggs", w.Sealed()), "invalid mesh header"); }
    { GgsWriter w = start(); WriteMeshHeader(w, false, 8388608u + 1, 3); expect("vertex count over the limit", TryLoad(r, "m.ggs", w.Sealed()), "invalid mesh header"); }
    { GgsWriter w = start(); WriteMeshHeader(w, false, 1, 67108864u + 1); expect("index count over the limit", TryLoad(r, "m.ggs", w.Sealed()), "invalid mesh header"); }
    // 한도 그대로는 헤더 검사를 통과하고, 데이터가 없어서 읽는 단계에서 잘렸다고 해야 한다.
    { GgsWriter w = start(); WriteMeshHeader(w, false, 8388608u, 3);   expect("vertex count at the limit", TryLoad(r, "m.ggs", w.Sealed()), "truncated while reading mesh vertices"); }
    {
        GgsWriter w = start(); WriteMeshHeader(w, false, 1, 67108864u);
        for (int i = 0; i < 8; ++i) w.F(0);   // 정점 1개 분량
        expect("index count at the limit", TryLoad(r, "m.ggs", w.Sealed()), "truncated while reading mesh indices");
    }

    // 본 수: 0과 65는 거부, 64는 허용(조인트가 0..1만 쓰는 스킨 띠로 실제 로드한다).
    for (uint32_t bones : {0u, 65u}) {
        GgsWriter w = start(); WriteMeshHeader(w, true, 1, 3);
        for (int i = 0; i < 8; ++i) w.F(0);
        for (uint32_t i : {0u, 0u, 0u}) w.U32(i);
        for (int i = 0; i < 4; ++i) w.U32(0);   // joints
        for (float f : {1.f, 0.f, 0.f, 0.f}) w.F(f);   // weights
        w.U32(bones);
        expect("bone count", TryLoad(r, "m.ggs", w.Sealed()), bones == 0 ? "invalid bone count 0" : "invalid bone count 65");
    }
    fs::path bmp = TempDir() / "bones64.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    Spec sixtyFour = VisualSpec(bmp);
    sixtyFour.bones = 64;
    expect("64 bones load", TryLoad(r, "m.ggs", Serialize(sixtyFour)), "OK");
    gge_renderer_destroy(r);
}

void Test_SceneStringAndClipLimitsAreCheckedAtTheirBoundaries()
{
    GgeRenderer* r = NewRenderer(64, 48);
    auto expect = [&](const char* label, const std::string& got, const char* part) {
        bool ok = Has(got, part);
        if (!ok) std::printf("  %s: got '%s', expected '%s'\n", label, got.c_str(), part);
        CHECK(ok);
    };
    fs::path bmp = TempDir() / "strings.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});

    // 문자열 길이: 1 MiB는 허용, 1 MiB + 1은 거부.
    for (size_t length : {size_t(1) << 20, (size_t(1) << 20) + 1}) {
        Spec s = VisualSpec(bmp);
        s.nodes[3].name = std::string(length, 'n');   // 그룹 노드의 이름
        expect("long name", TryLoad(r, "s.ggs", Serialize(s)), length == (size_t(1) << 20) ? "OK" : "truncated while reading node name");
    }
    // 충돌체 이름(버전 2)도 같은 문자열 한도를 쓴다: 1 MiB 허용, 1 MiB + 1 거부.
    for (size_t length : {size_t(1) << 20, (size_t(1) << 20) + 1}) {
        Spec s = VisualSpec(bmp);
        s.colliders = {{std::string(length, 'c'), 0, 0, 1, 1}};
        expect("long collider name", TryLoad(r, "s.ggs", Serialize(s)), length == (size_t(1) << 20) ? "OK" : "truncated while reading collider name");
    }
    {   // 남은 바이트보다 긴 충돌체 이름도 잘렸다고 해야 한다(방 0개 + 충돌체 1개, 이름 길이 1000인데 40바이트뿐).
        // 40바이트는 "충돌체 1개의 최소 크기(20)" 검사는 통과하고 이름을 읽는 단계에서 모자란다.
        GgsWriter w = PrefixUpToLighting(); w.U32(0); w.U8(0); w.F(0); w.F(0.5f); w.U32(0); w.U32(1);
        w.U32(1000); w.Raw(std::string(40, 'c').data(), 40);
        expect("collider name longer than the file", TryLoad(r, "s.ggs", w.Sealed()), "truncated while reading collider name");
    }
    // 남은 바이트보다 긴 문자열(한도보다는 짧다)도 잘렸다고 해야 한다.
    {
        GgsWriter w; w.Raw("GGS1", 4); w.U32(1); w.U32(0); w.U32(0); w.U32(1);
        w.U32(1000); w.Raw("abc", 3);   // 이름 길이 1000인데 3바이트뿐
        expect("string longer than the file", TryLoad(r, "s.ggs", w.Sealed()), "truncated while reading node name");
    }

    // 클립 수(4096)와 채널 수(65536)
    auto skinnedUpToClipCount = [&] {
        GgsWriter w; w.Raw("GGS1", 4); w.U32(1);
        WriteMeshHeader(w, true, 1, 3);
        for (int i = 0; i < 8; ++i) w.F(0);
        for (uint32_t i : {0u, 0u, 0u}) w.U32(i);
        for (int i = 0; i < 4; ++i) w.U32(0);
        for (float f : {1.f, 0.f, 0.f, 0.f}) w.F(f);
        ChainData chain = MakeChain(1);
        w.U32(1); w.I32(chain.parents[0]); w.Floats(chain.inverseBind); w.Floats(chain.translation); w.Floats(chain.rotation); w.Floats(chain.scale);
        return w;
    };
    for (uint32_t n : {4096u, 4097u}) {
        GgsWriter w = skinnedUpToClipCount(); w.U32(n);
        expect("clips", TryLoad(r, "s.ggs", w.Sealed()), n == 4096u ? "truncated while reading clips" : "too many clips");
    }
    for (uint32_t n : {65536u, 65537u}) {
        GgsWriter w = skinnedUpToClipCount(); w.U32(1); w.Str("c"); w.F(1.0f); w.U32(n);
        expect("channels", TryLoad(r, "s.ggs", w.Sealed()), n == 65536u ? "truncated while reading clip channels" : "too many clip channels");
    }
    {   // 채널 경로는 0..2만 허용한다(3은 거부).
        GgsWriter w = skinnedUpToClipCount(); w.U32(1); w.Str("c"); w.F(1.0f); w.U32(1);
        w.I32(0); w.U32(3); w.U32(1); w.F(0); for (int i = 0; i < 4; ++i) w.F(0);
        expect("channel path", TryLoad(r, "s.ggs", w.Sealed()), "invalid animation channel path 3");
    }
    gge_renderer_destroy(r);
}

void Test_SceneSkeletonRestPoseIsReadPerBoneAndComponent()
{
    // 본마다/성분마다 값이 다른 휴식 변환을 구워 두고(작성기), 같은 값으로 C API로 직접 만든 스켈레톤과 휴식 자세(전역 행렬)를 비교한다.
    fs::path bmp = TempDir() / "rest.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    Spec spec = VisualSpec(bmp);
    spec.distinctRest = true;
    spec.bones = 4;
    spec.nodes[2].clip.clear();     // 클립 없음: 휴식 자세
    GgeRenderer* a = NewRenderer(64, 48);
    GgeScene* scene = gge_scene_load(a, WriteScene("rest.ggs", Serialize(spec)).string().c_str());
    if (!scene) std::printf("  load failed: %s\n", gge_last_error());
    CHECK(scene != nullptr);
    if (!scene) { gge_renderer_destroy(a); return; }
    const int stripObject = gge_scene_find_object(scene, "Strip");
    std::vector<float> loaded;
    for (int bone = 0; bone < 4; ++bone) {
        std::vector<float> m = BoneMatrix(a, stripObject, 4, bone);
        loaded.insert(loaded.end(), m.begin(), m.end());
    }

    GgeRenderer* b = NewRenderer(64, 48);
    int strip = SkinnedStrip(b);
    int skeleton = CreateSkeleton(b, DistinctRestChain(4));
    int obj = gge_object_create(b, strip, 0, 0);
    gge_object_set_skeleton(b, obj, skeleton);
    std::vector<float> all;
    for (int bone = 0; bone < 4; ++bone) {
        std::vector<float> m = BoneMatrix(b, obj, 4, bone);
        all.insert(all.end(), m.begin(), m.end());
    }
    bool same = all.size() == loaded.size();
    for (size_t i = 0; same && i < all.size(); ++i) same = std::fabs(all[i] - loaded[i]) < 1e-5f;
    if (!same) std::printf("  rest pose differs from the manually built skeleton\n");
    CHECK(same);
    // 서로 다른 값이라는 전제 확인: 휴식 자세가 단순 항등 체인과 달라야 의미 있는 비교다.
    int strip2 = SkinnedStrip(b);
    int obj2 = gge_object_create(b, strip2, 0, 0);
    gge_object_set_skeleton(b, obj2, CreateSkeleton(b, MakeChain(4)));
    CHECK(BoneMatrix(b, obj2, 4, 3) != BoneMatrix(b, obj, 4, 3));
    gge_scene_destroy(scene);
    gge_renderer_destroy(a);
    gge_renderer_destroy(b);
}

}  // namespace

// 골든 비교 규칙: 평평한 영역 차이는 항상 실패, 경계 픽셀 차이는 하드웨어에서만 1% 예산 안에서 허용.
void Test_GoldenCompareRules()
{
    // 왼쪽 절반 검정, 오른쪽 절반 흰색인 20x20(경계는 x=9/10).
    auto make = [] {
        Image img;
        img.width = img.height = 20;
        img.bgra.assign(20 * 20 * 4, 255);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 10; ++x)
                for (int c = 0; c < 3; ++c) img.bgra[(static_cast<size_t>(y) * 20 + x) * 4 + c] = 0;
        return img;
    };
    auto setPixel = [](Image& img, int x, int y, uint8_t v) {
        for (int c = 0; c < 3; ++c) img.bgra[(static_cast<size_t>(y) * 20 + x) * 4 + c] = v;
    };
    const size_t pixels = 400;

    Image golden = make();
    GoldenDiff same = CompareImages(golden, make(), 2);
    CHECK(same.strict == 0 && same.edge == 0);

    Image within = make();
    setPixel(within, 3, 3, 2);  // 허용 오차(2) 이내
    GoldenDiff d = CompareImages(golden, within, 2);
    CHECK(d.strict == 0 && d.edge == 0);
    setPixel(within, 3, 3, 3);  // 허용 오차 초과
    d = CompareImages(golden, within, 2);
    CHECK(d.strict == 1 && d.edge == 0);

    Image flat = make();
    setPixel(flat, 3, 3, 100);  // 평평한 영역(왼쪽 검정) 안의 큰 차이
    d = CompareImages(golden, flat, 2);
    CHECK(d.strict == 1 && d.edge == 0);
    CHECK(!GoldenAccepts(d, pixels, false));
    CHECK(!GoldenAccepts(d, pixels, true));  // 하드웨어에서도 평평한 영역은 거부

    Image edge = make();
    setPixel(edge, 9, 5, 255);  // 경계에 붙은 픽셀 하나가 반대 색
    d = CompareImages(golden, edge, 2);
    CHECK(d.strict == 0 && d.edge == 1);
    CHECK(!GoldenAccepts(d, pixels, false));  // WARP은 경계도 엄격
    CHECK(GoldenAccepts(d, pixels, true));

    // 예산: 400픽셀의 1% = 4개까지 허용, 5개부터 거부.
    Image many = make();
    for (int y = 0; y < 4; ++y) setPixel(many, 9, y, 255);
    d = CompareImages(golden, many, 2);
    CHECK(d.strict == 0 && d.edge == 4);
    CHECK(GoldenAccepts(d, pixels, true));
    setPixel(many, 9, 4, 255);
    d = CompareImages(golden, many, 2);
    CHECK(d.edge == 5);
    CHECK(!GoldenAccepts(d, pixels, true));

    // 경계 판정은 골든 기준이다: 골든의 평평한 곳에 실제가 만든 고립 픽셀은 이웃과 달라도 경계가 아니다.
    Image spurious = make();
    setPixel(spurious, 15, 3, 0);  // 오른쪽 흰 영역 안에 검은 점
    d = CompareImages(golden, spurious, 2);
    CHECK(d.strict == 1 && d.edge == 0);
}

// ---- 동적 오브젝트(생성/삭제) ----
// RoomWithPlayerSpec(0 Panel=플레이어, 1 Badge=플레이어의 자식, 2 Strip, 3 Group)에 템플릿들을 더한다.
//  Crate(4) <- Lid(5) <- Knob(6): 3단 서브트리(구운 값이 일부러 평범하지 않다), Pebble(7): 잎 노드, Dummy(8): 스킨 노드(클립 bend 속도 1.5, sway 있음),
//  "Z/Lid"(9): 인스턴스 "Z"의 자손 이름과 겹치게 만든 구운 노드, D1..D17(10..26): 깊이 17 사슬, E1..E16(27..42): 깊이 16 사슬.
Spec SpawnSpec(const fs::path& bmp)
{
    Spec s = RoomWithPlayerSpec(bmp);
    s.extraClips = {"sway"};
    auto add = [&](SpecNode n) { s.nodes.push_back(n); return static_cast<int>(s.nodes.size()) - 1; };
    SpecNode crate; crate.name = "Crate"; crate.mesh = 0; crate.texture = 0; crate.pos[0] = -3; crate.pos[1] = 0.5f; crate.rot[1] = 0.3f;
    crate.scale[0] = crate.scale[1] = crate.scale[2] = 0.5f; crate.alpha = 0.7f; crate.casts = true;
    crate.color[0] = 0.9f; crate.color[1] = 0.4f; crate.color[2] = 0.2f;
    const int c = add(crate);
    SpecNode lid; lid.name = "Lid"; lid.parent = c; lid.mesh = 0; lid.pos[1] = 1.2f; lid.color[0] = 0.2f; lid.color[1] = 0.3f; lid.color[2] = 0.9f;
    const int l = add(lid);
    SpecNode knob; knob.name = "Knob"; knob.parent = l; knob.mesh = 0; knob.pos[1] = 1.0f; knob.scale[0] = knob.scale[1] = knob.scale[2] = 0.3f;
    add(knob);
    SpecNode pebble; pebble.name = "Pebble"; pebble.mesh = 0; pebble.pos[0] = -3; pebble.pos[1] = 0.2f; pebble.pos[2] = 2;
    pebble.scale[0] = pebble.scale[1] = pebble.scale[2] = 0.2f;
    add(pebble);
    SpecNode dummy; dummy.name = "Dummy"; dummy.mesh = 1; dummy.clip = "bend"; dummy.speed = 1.5f; dummy.pos[0] = 3; dummy.pos[2] = 2;
    add(dummy);
    SpecNode probe; probe.name = "Z/Lid";
    add(probe);
    int parent = -1;
    for (int i = 1; i <= 17; ++i) {
        SpecNode n; n.name = "D" + std::to_string(i); n.parent = parent;
        parent = add(n);
    }
    parent = -1;
    for (int i = 1; i <= 16; ++i) {
        SpecNode n; n.name = "E" + std::to_string(i); n.parent = parent;
        parent = add(n);
    }
    return s;
}

struct SpawnFixture {
    GgeRenderer* r = nullptr;
    GgeScene* scene = nullptr;
    int base = 0;   // 로드 직후 렌더러 오브젝트 수
    explicit SpawnFixture(const char* file, int w = 128, int h = 96)
    {
        fs::path bmp = TempDir() / (std::string(file) + ".bmp");
        WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
        r = NewRenderer(w, h);
        scene = gge_scene_load(r, WriteScene(std::string(file) + ".ggs", Serialize(SpawnSpec(bmp))).string().c_str());
        if (!scene) std::printf("  load failed: %s\n", gge_last_error());
        CHECK(scene != nullptr);
        if (scene) base = gge_renderer_get_object_count(r);
    }
    ~SpawnFixture()
    {
        if (scene) gge_scene_destroy(scene);
        if (r) gge_renderer_destroy(r);
    }
    bool ok() const { return scene != nullptr; }
    int Spawn(const char* tpl, const char* name, float x = 0, float y = 0, float z = 0) { return gge_scene_spawn_object(scene, tpl, name, x, y, z); }
    int Destroy(const char* name) { return gge_scene_destroy_object(scene, name); }
    bool Exists(const char* name) const { return gge_scene_object_exists(scene, name) == 1; }
    int Objects() const { return gge_renderer_get_object_count(r); }
    int Dynamic() const { return gge_scene_get_dynamic_node_count(scene); }
    void Step() { CHECK(gge_scene_step(scene, 0, 0, 0) == 1); }   // dt 0: 프레임 경계(한 프레임의 spawn 횟수 세기)만 넘긴다
};

bool LastErrorHas(const char* part) { return std::string(gge_last_error()).find(part) != std::string::npos; }

void Test_SpawnedObjectStartsFromTheBakedTemplateAndLooksLikeABakedCopy()
{
    // 기준: 같은 모양(Crate 서브트리)을 처음부터 (2, 0.5, 1)에 구워 둔 씬. 복제본은 이 화면과 같아야 한다.
    fs::path bmp = TempDir() / "spawn_ref.bmp";
    WriteBmp(bmp, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    Spec spec = SpawnSpec(bmp);
    Spec refSpec = spec;
    {
        const SpecNode crate = spec.nodes[4], lid = spec.nodes[5], knob = spec.nodes[6];
        SpecNode a = crate; a.name = "Baked"; a.pos[0] = 2; a.pos[1] = 0.5f; a.pos[2] = 1;
        refSpec.nodes.push_back(a);
        const int bi = static_cast<int>(refSpec.nodes.size()) - 1;
        SpecNode b = lid; b.name = "BakedLid"; b.parent = bi; refSpec.nodes.push_back(b);
        SpecNode k = knob; k.name = "BakedKnob"; k.parent = bi + 1; refSpec.nodes.push_back(k);
    }
    GgeRenderer* rr = NewRenderer(128, 96);
    GgeScene* ref = gge_scene_load(rr, WriteScene("spawn_ref.ggs", Serialize(refSpec)).string().c_str());
    CHECK(ref != nullptr);
    if (!ref) { gge_renderer_destroy(rr); return; }
    CHECK(gge_scene_set_node_visible(ref, "Crate", 0) == 1);   // 템플릿은 화면에서 뺀다
    const Image reference = Render(rr);
    gge_scene_destroy(ref);
    gge_renderer_destroy(rr);

    SpawnFixture f("spawn_main");
    if (!f.ok()) return;
    CHECK(gge_scene_set_node_visible(f.scene, "Crate", 0) == 1);
    const Image empty = Render(f.r);

    // 스크립트가 템플릿을 마음대로 바꿔도(옮기고, 돌리고, 키우고, 알파를 바꿔도) 복제는 구운 값으로 시작한다.
    CHECK(gge_scene_set_node_position(f.scene, "Crate", 9, 9, 9) == 1);
    CHECK(gge_scene_set_node_rotation(f.scene, "Crate", 1, 1, 1) == 1);
    CHECK(gge_scene_set_node_scale(f.scene, "Crate", 3, 3, 3) == 1);
    CHECK(gge_object_set_alpha(f.r, gge_scene_find_object(f.scene, "Crate"), 0.1f) == 1);
    CHECK(f.Spawn("Crate", "C1", 2, 0.5f, 1) == 1);
    const Image spawned = Render(f.r);
    CHECK(DifferingPixels(empty, spawned) > 20);                   // 복제가 실제로 그려진다
    CHECK(spawned.bgra == reference.bgra);                         // 구운 사본과 바이트 동일

    CHECK(f.Exists("C1") && f.Exists("C1/Lid") && f.Exists("C1/Knob"));
    CHECK(f.Dynamic() == 3 && f.Objects() == f.base + 3);
    float x = 0, y = 0, z = 0;
    CHECK(gge_scene_get_node_position(f.scene, "C1", &x, &y, &z) == 1 && x == 2 && y == 0.5f && z == 1);   // 인자 위치(템플릿과 같은 부모 기준 로컬)
    CHECK(gge_scene_get_node_position(f.scene, "C1/Lid", &x, &y, &z) == 1 && x == 0 && y == 1.2f && z == 0);
    CHECK(gge_scene_get_node_rotation(f.scene, "C1", &x, &y, &z) == 1 && x == 0 && y == 0.3f && z == 0);   // 구운 회전/크기
    CHECK(gge_scene_get_node_scale(f.scene, "C1", &x, &y, &z) == 1 && x == 0.5f && y == 0.5f && z == 0.5f);
    CHECK(gge_scene_get_node_visible(f.scene, "C1") == 1 && gge_scene_get_node_visible(f.scene, "Crate") == 0);   // 복제는 보이고 템플릿은 그대로 숨김
    CHECK(gge_scene_get_node_position(f.scene, "Crate", &x, &y, &z) == 1 && x == 9 && y == 9 && z == 9);        // 템플릿은 건드리지 않는다

    // 복제본의 변경이 템플릿에 영향을 주지 않는다(기존 이름 API가 복제본에 그대로 동작한다).
    CHECK(gge_scene_set_node_position(f.scene, "C1/Lid", 0, 5, 0) == 1);
    CHECK(gge_scene_set_node_visible(f.scene, "C1/Knob", 0) == 1);
    CHECK(gge_scene_get_node_position(f.scene, "Lid", &x, &y, &z) == 1 && y == 1.2f);          // 구운 Lid는 그대로
    CHECK(gge_scene_get_node_visible(f.scene, "Knob") == 1);
    CHECK(Render(f.r).bgra != spawned.bgra);
}

void Test_DestroyedObjectsLeaveNoTrace_AndTheirNamesCanBeReused()
{
    SpawnFixture f("spawn_destroy");
    if (!f.ok()) return;
    CHECK(gge_scene_set_node_visible(f.scene, "Crate", 0) == 1);
    const Image before = Render(f.r);

    CHECK(f.Spawn("Crate", "C1", 2, 0.5f, 1) == 1);
    CHECK(f.Spawn("Crate", "C2", -1, 0.5f, 2) == 1);
    CHECK(f.Dynamic() == 6 && f.Objects() == f.base + 6);
    const Image two = Render(f.r);
    CHECK(f.Destroy("C1") == 1);                                  // 자손까지
    CHECK(!f.Exists("C1") && !f.Exists("C1/Lid") && !f.Exists("C1/Knob"));
    CHECK(f.Exists("C2") && f.Exists("C2/Lid") && f.Exists("C2/Knob"));
    CHECK(f.Dynamic() == 3 && f.Objects() == f.base + 3);
    float x = 0, y = 0, z = 0;
    CHECK(gge_scene_get_node_position(f.scene, "C1", &x, &y, &z) == 0 && LastErrorHas("no node named 'C1'"));   // 지운 이름 = 없는 노드 규칙
    CHECK(gge_scene_set_node_position(f.scene, "C1/Lid", 0, 0, 0) == 0);
    CHECK(gge_scene_get_node_position(f.scene, "C2", &x, &y, &z) == 1 && x == -1 && z == 2);
    CHECK(Render(f.r).bgra != two.bgra);
    CHECK(f.Destroy("C2") == 1);
    CHECK(f.Dynamic() == 0 && f.Objects() == f.base);
    CHECK(Render(f.r).bgra == before.bgra);                      // 만들기 전과 바이트 동일

    // 이름 재사용 + 슬롯 재사용: 같은 이름으로 다시 만들어도 같은 결과.
    CHECK(f.Spawn("Crate", "C1", 2, 0.5f, 1) == 1);
    CHECK(f.Dynamic() == 3 && f.Objects() == f.base + 3);
    CHECK(gge_scene_get_node_position(f.scene, "C1/Lid", &x, &y, &z) == 1 && y == 1.2f);

    // 부모가 구운 노드(Badge는 Panel의 자식)인 복제: 지우면 부모는 그대로 남고 복제만 사라진다.
    const int objectsBefore = f.Objects();
    CHECK(f.Spawn("Badge", "B1", 0.1f, 0.2f, 0.3f) == 1 && f.Objects() == objectsBefore + 1);
    CHECK(f.Destroy("B1") == 1 && f.Objects() == objectsBefore && f.Exists("Panel") && f.Exists("Badge"));

    // 지울 수 없는 것: 씬 파일의 노드, 복제본의 자손 단독, 없는 이름. 상태는 그대로다.
    const Image kept = Render(f.r);
    CHECK(f.Destroy("Crate") == 0 && LastErrorHas("was not created by spawn_object"));
    CHECK(f.Destroy("Panel") == 0 && LastErrorHas("was not created by spawn_object"));
    CHECK(f.Destroy("C1/Lid") == 0 && LastErrorHas("was not created by spawn_object"));
    CHECK(f.Destroy("Nope") == 0 && LastErrorHas("no node named 'Nope'"));
    CHECK(f.Exists("Crate") && f.Exists("C1/Lid") && f.Dynamic() == 3 && f.Objects() == f.base + 3);
    CHECK(Render(f.r).bgra == kept.bgra);
}

void Test_SpawnRejectsBadRequests_AndChangesNothing()
{
    SpawnFixture f("spawn_reject");
    if (!f.ok()) return;
    CHECK(f.Spawn("Crate", "C1", 2, 0.5f, 1) == 1);
    const Image before = Render(f.r);
    const int objects = f.Objects(), dynamic = f.Dynamic();
    auto rejected = [&](const char* what, int result, const char* reason) {
        if (result != 0 || !LastErrorHas(reason)) std::printf("  not rejected as expected: %s (error: %s)\n", what, gge_last_error());
        CHECK(result == 0);
        CHECK(LastErrorHas(reason));
        CHECK(f.Objects() == objects && f.Dynamic() == dynamic);   // 렌더러 오브젝트도 노드 수도 그대로
    };
    rejected("empty name", f.Spawn("Crate", ""), "must not be empty");
    rejected("null name", gge_scene_spawn_object(f.scene, "Crate", nullptr, 0, 0, 0), "must not be empty");
    rejected("slash in name", f.Spawn("Crate", "a/b"), "must not contain '/'");
    rejected("name too long", f.Spawn("Crate", std::string(129, 'x').c_str()), "too long");
    rejected("duplicate dynamic name", f.Spawn("Crate", "C1"), "already exists");
    rejected("duplicate descendant name", f.Spawn("Pebble", "C1/Lid"), "must not contain '/'");
    rejected("duplicate scene node name", f.Spawn("Pebble", "Panel"), "already exists");
    rejected("descendant name clashes with a scene node", f.Spawn("Crate", "Z"), "a node named 'Z/Lid' already exists");
    rejected("no such template", f.Spawn("Nope", "N1"), "no node named 'Nope'");
    rejected("null template", gge_scene_spawn_object(f.scene, nullptr, "N1", 0, 0, 0), "no node named ''");
    rejected("template is a clone", f.Spawn("C1", "N1"), "is a spawned object");
    rejected("template is the player", f.Spawn("Panel", "N1"), "is or contains the player");
    rejected("NaN position", f.Spawn("Crate", "N1", NAN, 0, 0), "spawn position must be finite");
    rejected("infinite position", f.Spawn("Crate", "N1", 0, INFINITY, 0), "spawn position must be finite");
    CHECK(!f.Exists("N1"));
    CHECK(Render(f.r).bgra == before.bgra);

    // 거부된 뒤에도 정상 동작한다(실패가 내부 상태를 망가뜨리지 않았다).
    CHECK(f.Spawn("Crate", "C2", 0, 0.5f, 0) == 1 && f.Dynamic() == dynamic + 3);
}

void Test_SpawnLimitsAreEnforcedBeforeAnythingIsCreated()
{
    SpawnFixture f("spawn_limits");
    if (!f.ok()) return;
    auto name = [](int i) { return "P" + std::to_string(i); };

    // 한 프레임(Step 사이)에 64번까지. 65번째는 거부, Step을 넘기면 다시 64번 가능.
    for (int i = 0; i < 64; ++i) CHECK(f.Spawn("Pebble", name(i).c_str()) == 1);
    CHECK(f.Spawn("Pebble", "P64") == 0 && LastErrorHas("too many spawns in one frame"));
    CHECK(f.Dynamic() == 64 && !f.Exists("P64"));
    f.Step();
    // 합계 256까지(한 프레임 64씩).
    for (int i = 64; i < 256; ++i) {
        if (i % 64 == 0) f.Step();
        CHECK(f.Spawn("Pebble", name(i).c_str()) == 1);
    }
    CHECK(f.Dynamic() == 256 && f.Objects() == f.base + 256);
    f.Step();
    CHECK(f.Spawn("Pebble", "P256") == 0 && LastErrorHas("too many spawned nodes (max 256, 256 alive, this spawn needs 1)"));
    CHECK(f.Dynamic() == 256 && f.Objects() == f.base + 256 && !f.Exists("P256"));
    CHECK(f.Destroy("P0") == 1);
    CHECK(f.Spawn("Crate", "C1") == 0 && LastErrorHas("this spawn needs 3"));      // 자손 포함 3개가 안 들어간다: 하나도 만들지 않는다
    CHECK(!f.Exists("C1") && !f.Exists("C1/Lid") && f.Dynamic() == 255);
    CHECK(f.Spawn("Pebble", "P256") == 1 && f.Dynamic() == 256);                   // 자리가 나면 다시 된다

    // 깊이: 17단 사슬은 거부(자손 이름도 하나도 안 남는다), 16단은 된다.
    for (int i = 0; i < 40; ++i) CHECK(f.Destroy(name(i + 1).c_str()) == 1 || i >= 255);
    f.Step();
    const int objects = f.Objects(), dynamic = f.Dynamic();
    CHECK(f.Spawn("D1", "D") == 0 && LastErrorHas("nested too deeply") && f.Objects() == objects && f.Dynamic() == dynamic && !f.Exists("D"));
    CHECK(f.Spawn("E1", "E") == 1 && f.Dynamic() == dynamic + 16 && f.Exists("E/E16") && f.Objects() == objects + 16);

    // Scene을 파괴하면 동적 오브젝트도 렌더러에서 사라진다.
    gge_scene_destroy(f.scene);
    f.scene = nullptr;
    CHECK(gge_renderer_get_object_count(f.r) == 0);
}

void Test_SpawnedSkinnedObjectsKeepIndependentAnimationState()
{
    SpawnFixture f("spawn_skinned");
    if (!f.ok()) return;
    auto time = [&](const char* node) { const char* clip = nullptr; float t = -1; int p = 0, fd = 0, lp = 0, fin = 0; float sp = 0;
                                        CHECK(gge_scene_get_node_animation(f.scene, node, &clip, &t, &p, &sp, &fd, &lp, &fin) == 1); return t; };
    auto clipOf = [&](const char* node) { const char* clip = nullptr; float t, sp; int p, fd, lp, fin;
                                          CHECK(gge_scene_get_node_animation(f.scene, node, &clip, &t, &p, &sp, &fd, &lp, &fin) == 1); return std::string(clip ? clip : "?"); };
    for (int i = 0; i < 4; ++i) CHECK(gge_scene_step(f.scene, 0, 0, 0.25f) == 1);      // 템플릿 Dummy는 1.5배속으로 1.5초 -> 시간 0.5
    CHECK(std::fabs(time("Dummy") - 0.5f) < 1e-4f);

    CHECK(f.Spawn("Dummy", "Dm1", 1, 0, 0) == 1 && f.Spawn("Dummy", "Dm2", 2, 0, 0) == 1);
    CHECK(clipOf("Dm1") == "bend" && time("Dm1") == 0 && time("Dm2") == 0);          // 구운 초기 클립으로, 템플릿의 현재 시간과 무관하게 시작
    CHECK(std::fabs(time("Dummy") - 0.5f) < 1e-4f);
    CHECK(gge_scene_play_clip(f.scene, "Dm1", "sway", 0, 0, 1) == 1);                // Dm1만 클립 변경
    CHECK(clipOf("Dm1") == "sway" && clipOf("Dm2") == "bend" && clipOf("Dummy") == "bend");
    for (int i = 0; i < 2; ++i) CHECK(gge_scene_step(f.scene, 0, 0, 0.25f) == 1);
    CHECK(std::fabs(time("Dm2") - 0.75f) < 1e-4f && std::fabs(time("Dm1") - 0.75f) < 1e-4f);
    CHECK(std::fabs(time("Dummy") - 0.25f) < 1e-4f);                                   // 1.25 -> 감김 0.25

    // 먼저 만든 Dm1을 지워도(애니메이션 목록에서 빠져도) 나머지는 각자의 시간으로 계속 진행한다.
    CHECK(f.Destroy("Dm1") == 1);
    float t = 0; const char* clip = nullptr; int p, fd, lp, fin; float sp;
    CHECK(gge_scene_get_node_animation(f.scene, "Dm1", &clip, &t, &p, &sp, &fd, &lp, &fin) == 0 && LastErrorHas("no node named 'Dm1'"));
    CHECK(gge_scene_step(f.scene, 0, 0, 0.25f) == 1);
    CHECK(std::fabs(time("Dm2") - 0.125f) < 1e-4f);                                    // 0.75 + 0.375 = 1.125 -> 0.125
    CHECK(std::fabs(time("Dummy") - 0.625f) < 1e-4f);
    CHECK(f.Spawn("Dummy", "Dm3", 3, 0, 0) == 1 && time("Dm3") == 0 && clipOf("Dm3") == "bend");   // 빈 슬롯을 다시 쓴다
    CHECK(gge_scene_step(f.scene, 0, 0, 0.25f) == 1);
    CHECK(std::fabs(time("Dm3") - 0.375f) < 1e-4f && std::fabs(time("Dm2") - 0.5f) < 1e-4f && std::min(time("Dummy"), 1.0f - time("Dummy")) < 1e-4f);   // 1.0 -> 감겨서 0(부동소수점 오차로 1 바로 아래일 수 있다)

    // 스킨이 아닌 복제본에는 클립을 쓸 수 없다.
    CHECK(f.Spawn("Pebble", "Pb1") == 1);
    CHECK(gge_scene_play_clip(f.scene, "Pb1", "bend", 0, 0, 1) == 0 && LastErrorHas("has no skeleton animation"));
}

void Test_SpawnFailingHalfwayRollsBackEveryObject()
{
    SpawnFixture f("spawn_rollback");
    if (!f.ok()) return;
    CHECK(gge_scene_set_node_visible(f.scene, "Crate", 0) == 1);
    const Image before = Render(f.r);
    for (int failAfter = 1; failAfter <= 3; ++failAfter) {           // 루트 직후/중간/마지막 자손 직후
        CHECK(gge_scene_set_spawn_failure_for_tests(f.scene, failAfter) == 1);
        CHECK(f.Spawn("Crate", "C1", 2, 0.5f, 1) == 0 && LastErrorHas("injected spawn failure"));
        CHECK(f.Objects() == f.base && f.Dynamic() == 0);            // 렌더러 오브젝트가 남지 않는다
        CHECK(!f.Exists("C1") && !f.Exists("C1/Lid") && !f.Exists("C1/Knob"));   // 이름도 남지 않는다
        CHECK(Render(f.r).bgra == before.bgra);
    }
    CHECK(gge_scene_set_spawn_failure_for_tests(f.scene, 1) == 1);   // 부모가 구운 노드(Badge/Panel)인 복제
    CHECK(f.Spawn("Badge", "B1") == 0 && f.Objects() == f.base && !f.Exists("B1"));
    CHECK(gge_scene_set_spawn_failure_for_tests(f.scene, 1) == 1);   // 스킨 노드(애니메이션 상태가 만들어진 뒤 실패)
    CHECK(f.Spawn("Dummy", "Dm1") == 0 && f.Objects() == f.base && !f.Exists("Dm1"));
    CHECK(gge_scene_set_spawn_failure_for_tests(f.scene, -1) == 1);
    CHECK(f.Spawn("Crate", "C1", 2, 0.5f, 1) == 1 && f.Dynamic() == 3 && f.Objects() == f.base + 3);   // 실패 뒤에도 정상
    CHECK(f.Spawn("Dummy", "Dm1") == 1);
    CHECK(gge_scene_step(f.scene, 0, 0, 0.25f) == 1);                // 애니메이션 목록이 온전하다
}

// 무작위로 만들고 지우기를 반복해도 모델(살아 있는 이름 집합)과 렌더러/노드 수가 항상 일치한다.
void Test_SpawnAndDestroyKeepEveryInvariantUnderRandomSequences()
{
    SpawnFixture f("spawn_random", 64, 48);
    if (!f.ok()) return;
    struct Template { const char* name; int size; };
    const Template templates[] = {{"Crate", 3}, {"Pebble", 1}, {"Dummy", 1}, {"Badge", 1}, {"E1", 16}};
    std::map<std::string, const Template*> live;   // 인스턴스 이름 -> 템플릿
    unsigned seed = 12345;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    int total = 0, spawned = 0, destroyed = 0, refused = 0;
    const int failuresBefore = g_failures;
    for (int op = 0; op < 3000; ++op) {
        f.Step();   // 한 프레임 한도에 걸리지 않게 매 연산마다 프레임을 넘긴다
        const std::string name = "I" + std::to_string(next() % 40);
        if (next() % 2 == 0) {
            const Template& t = templates[next() % 5];
            const bool expectOk = !live.count(name) && total + t.size <= 256;
            const int result = f.Spawn(t.name, name.c_str(), static_cast<float>(next() % 5), 0, static_cast<float>(next() % 5));
            CHECK((result == 1) == expectOk);
            if (result == 1) { live[name] = &t; total += t.size; ++spawned; } else ++refused;
        } else {
            const int result = f.Destroy(name.c_str());
            CHECK((result == 1) == (live.count(name) != 0));
            if (result == 1) { total -= live[name]->size; live.erase(name); ++destroyed; } else ++refused;
        }
        CHECK(f.Dynamic() == total && f.Objects() == f.base + total);
        if (op % 100 == 0) {
            for (int i = 0; i < 40; ++i) {
                const std::string n = "I" + std::to_string(i);
                CHECK(f.Exists(n.c_str()) == (live.count(n) != 0));
            }
            CHECK(f.Exists("Crate") && f.Exists("Panel"));
        }
        if (g_failures > failuresBefore) break;   // 첫 실패에서 멈춘다(로그가 쏟아지지 않게)
    }
    CHECK(spawned > 200 && destroyed > 200 && refused > 200);          // 세 경로가 모두 실제로 쓰였다
    for (const auto& [n, t] : live) CHECK(f.Destroy(n.c_str()) == 1);
    CHECK(f.Dynamic() == 0 && f.Objects() == f.base);
    CHECK(gge_scene_get_node_slot_count(f.scene) <= 43 + 256);   // 구운 노드 43개(SpawnSpec) + 동시에 살 수 있는 복제 노드 최대치: 지운 슬롯을 재사용한다(spawned는 1000건이 넘는다)
    // 새 이름으로 가득 차게 만들었다 비우기를 반복해도 렌더러 오브젝트 수가 늘지 않는다(누수 없음).
    for (int round = 0; round < 50; ++round) {
        f.Step();
        for (int i = 0; i < 10; ++i) CHECK(f.Spawn("Crate", ("R" + std::to_string(i)).c_str()) == 1);
        for (int i = 0; i < 10; ++i) CHECK(f.Destroy(("R" + std::to_string(i)).c_str()) == 1);
        CHECK(f.Objects() == f.base && f.Dynamic() == 0);
    }
}

// ---- 비트맵 폰트(BDF)와 글자 그리기 ----
// 작은 합성 BDF: 글꼴 높이 ascent 7 + descent 2 = 줄 높이 9. 글리프: 공백(폭 4), A(5x7, 폭 6), B(4x7, 폭 5), W(8x7, 폭 9), g(4x6, 기준선 아래 2줄, 폭 5),
// 가(9x8, 폭 10), U+FFFD(5x5 마름모, 폭 6), .notdef(5x7 빈 네모, 폭 6).
struct BdfGlyph {
    std::string name;
    int encoding, advance, w, h, xo, yo;
    std::vector<std::string> rows;
};

std::string BdfText(const std::vector<BdfGlyph>& glyphs, int ascent = 7, int descent = 2)
{
    std::string s = "STARTFONT 2.1\nFONT -test-font\nSIZE 9 75 75\nFONTBOUNDINGBOX 10 9 0 -2\nSTARTPROPERTIES 2\nFONT_ASCENT " + std::to_string(ascent) +
                    "\nFONT_DESCENT " + std::to_string(descent) + "\nENDPROPERTIES\nCHARS " + std::to_string(glyphs.size()) + "\n";
    for (const BdfGlyph& g : glyphs) {
        s += "STARTCHAR " + g.name + "\nENCODING " + std::to_string(g.encoding) + "\nSWIDTH 500 0\nDWIDTH " + std::to_string(g.advance) + " 0\nBBX " +
             std::to_string(g.w) + " " + std::to_string(g.h) + " " + std::to_string(g.xo) + " " + std::to_string(g.yo) + "\nBITMAP\n";
        for (const std::string& r : g.rows) s += r + "\n";
        s += "ENDCHAR\n";
    }
    return s + "ENDFONT\n";
}

std::vector<BdfGlyph> TestGlyphs(bool withReplacementAndNotdef = true)
{
    std::vector<BdfGlyph> g = {
        {"U+0020", 32, 4, 0, 0, 0, 0, {}},
        {"U+0041", 65, 6, 5, 7, 0, 0, {"70", "88", "88", "F8", "88", "88", "88"}},
        {"U+0042", 66, 5, 4, 7, 0, 0, {"F0", "90", "F0", "90", "90", "90", "F0"}},
        {"U+0057", 87, 9, 8, 7, 0, 0, {"81", "81", "81", "99", "A5", "A5", "42"}},
        {"U+0067", 103, 5, 4, 6, 1, -2, {"70", "90", "90", "70", "10", "E0"}},   // 왼쪽 위 모서리가 펜에서 1픽셀 오른쪽(xOffset 1)
        {"U+AC00", 0xAC00, 10, 9, 8, 0, -1, {"FF80", "8080", "8080", "FF80", "0080", "0080", "0080", "FF80"}},
    };
    if (withReplacementAndNotdef) {
        g.push_back({"U+FFFD", 0xFFFD, 6, 5, 5, 0, 0, {"20", "70", "F8", "70", "20"}});
        g.push_back({".notdef", -1, 6, 5, 7, 0, 0, {"F8", "88", "88", "88", "88", "88", "F8"}});
    }
    return g;
}

struct FontHandle {
    GgeFont* f = nullptr;
    explicit FontHandle(const std::string& bdf)
    {
        f = gge_font_load_memory(bdf.data(), bdf.size());
        if (!f) std::printf("  font load failed: %s\n", gge_last_error());
        CHECK(f != nullptr);
    }
    ~FontHandle() { gge_font_destroy(f); }
};

struct Canvas {
    int w, h;
    std::vector<uint8_t> px;
    Canvas(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * height * 4, 0x55) {}
    int Stride() const { return w * 4; }
    // '#': 색이 그려진 픽셀(기본 0x336699), '.': 손대지 않은 픽셀, '?': 그 밖
    std::string Ascii(uint32_t rgb = 0x336699) const
    {
        std::string out;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
                if (p[0] == (rgb & 0xFF) && p[1] == ((rgb >> 8) & 0xFF) && p[2] == ((rgb >> 16) & 0xFF) && p[3] == 255) out += '#';
                else if (p[0] == 0x55 && p[1] == 0x55 && p[2] == 0x55 && p[3] == 0x55) out += '.';
                else out += '?';
            }
            out += '\n';
        }
        return out;
    }
};

std::string Rows(std::initializer_list<const char*> rows)
{
    std::string s;
    for (const char* r : rows) { s += r; s += '\n'; }
    return s;
}

int DrawText(const FontHandle& f, Canvas& c, const char* text, int x, int y, int maxWidth = 0, uint32_t rgb = 0x336699)
{
    return gge_font_draw_text(f.f, text, maxWidth, c.px.data(), c.w, c.h, c.Stride(), x, y, rgb);
}

void Test_BitmapFontParsesGlyphsMetricsAndDrawsExactPixels()
{
    FontHandle f(BdfText(TestGlyphs()));
    if (!f.f) return;
    int ascent = -1, descent = -1;
    CHECK(gge_font_get_metrics(f.f, &ascent, &descent) == 1 && ascent == 7 && descent == 2);
    CHECK(gge_font_has_glyph(f.f, 'A') == 1 && gge_font_has_glyph(f.f, 0xAC00) == 1 && gge_font_has_glyph(f.f, 0xFFFD) == 1);
    CHECK(gge_font_has_glyph(f.f, 'Z') == 0 && gge_font_has_glyph(f.f, 0x1F600) == 0 && gge_font_has_glyph(f.f, 0xFFFFFFFFu) == 0);

    // 'A'를 (2,1)에: 기준선 = 1 + 7, 비트맵 맨 위 줄 = 1. 비트맵과 한 픽셀도 다르지 않아야 한다.
    Canvas c(10, 10);
    CHECK(DrawText(f, c, "A", 2, 1) == 1);
    CHECK(c.Ascii() == Rows({"..........", "...###....", "..#...#...", "..#...#...", "..#####...", "..#...#...", "..#...#...", "..#...#...", "..........", ".........."}));
    // 그려진 픽셀의 바이트: B, G, R, 알파 255. 나머지 바이트는 그대로.
    const uint8_t* lit = &c.px[(static_cast<size_t>(1) * 10 + 3) * 4];
    CHECK(lit[0] == 0x99 && lit[1] == 0x66 && lit[2] == 0x33 && lit[3] == 255);

    // 내려가는 글자 g: 비트맵 아래쪽 2줄이 기준선(y + 7) 아래로 나온다.
    Canvas g(8, 10);
    CHECK(DrawText(f, g, "g", 1, 0) == 1);
    CHECK(g.Ascii() == Rows({"........", "........", "........", "...###..", "..#..#..", "..#..#..", "...###..", ".....#..", "..###...", "........"}));

    // 두 글자: 펜이 advance(6)만큼 간다. 한글(9x8, 기준선 아래 1줄).
    Canvas two(16, 10);
    CHECK(DrawText(f, two, "AB", 0, 0) == 1);
    CHECK(two.Ascii() == Rows({".###..####......","#...#.#..#......", "#...#.####......", "#####.#..#......", "#...#.#..#......", "#...#.#..#......", "#...#.####......", "................", "................", "................"}));
    Canvas hangul(12, 10);
    CHECK(DrawText(f, hangul, "가", 1, 0) == 1);
    CHECK(hangul.Ascii() == Rows({".#########..", ".#.......#..", ".#.......#..", ".#########..", ".........#..", ".........#..", ".........#..", ".#########..", "............", "............"}));
}

void Test_BitmapFontRejectsMalformedFilesWithAReason()
{
    const std::string good = BdfText(TestGlyphs());
    auto replace = [&](const std::string& from, const std::string& to) {
        std::string s = good;
        const size_t at = s.find(from);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) s.replace(at, from.size(), to);
        return s;
    };
    // from(포함)부터 until(포함)까지 지운다.
    auto cut = [](std::string s, const std::string& from, const std::string& until) {
        const size_t a = s.find(from);
        CHECK(a != std::string::npos);
        const size_t b = a == std::string::npos ? a : s.find(until, a);
        CHECK(b != std::string::npos);
        if (a != std::string::npos && b != std::string::npos) s.erase(a, b + until.size() - a);
        return s;
    };
    struct Case { const char* what; std::string bdf; const char* reason; };
    const Case cases[] = {
        {"empty", "", "the font file is empty"},
        {"no STARTFONT", "FONT x\nENDFONT\n", "must start with STARTFONT"},
        {"truncated before ENDFONT", good.substr(0, good.size() - 8), "ends before ENDFONT"},
        {"truncated inside a bitmap", good.substr(0, good.find("88\n88\n88\n") + 3), "ends inside a bitmap"},
        {"truncated inside a glyph", good.substr(0, good.find("DWIDTH 6")), "ends inside the glyph"},
        {"CHARS larger than the file has", replace("CHARS 8", "CHARS 9"), "CHARS declares 9 glyphs but the file has 7 with a character code (8 in all)"},
        {"CHARS smaller than the file has", replace("CHARS 8", "CHARS 6"), "more glyphs than CHARS declares"},
        {"CHARS too big", replace("CHARS 8", "CHARS 200001"), "CHARS 200001 is out of range"},
        {"CHARS not a number", replace("CHARS 8", "CHARS x"), "CHARS must be an integer"},
        {"no CHARS line", replace("CHARS 8\n", ""), "CHARS must come before the first glyph"},
        {"bitmap row too short", replace("\n70\n88\n", "\n7\n88\n"), "hex digits, expected 2"},
        {"no ascent and no bounding box", cut(cut(good, "FONTBOUNDINGBOX", "\n"), "STARTPROPERTIES", "ENDPROPERTIES\n"), "neither FONT_ASCENT/FONT_DESCENT nor FONTBOUNDINGBOX"},
        {"bitmap row too long", replace("\n70\n88\n", "\n7000\n88\n"), "hex digits, expected 2"},
        {"bitmap row not hex", replace("\n70\n88\n", "\nZZ\n88\n"), "must be hexadecimal"},
        {"too many bitmap rows", replace("F8\n88\n88\n88\n", "F8\n88\n88\n88\n88\n88\n"), "expected ENDCHAR"},
        {"too few bitmap rows", replace("\n70\n88\n88\nF8\n88\n88\n88\nENDCHAR", "\n70\n88\n88\nF8\n88\n88\nENDCHAR"), "hex digits, expected 2"},
        {"negative BBX width", replace("BBX 5 7 0 0", "BBX -5 7 0 0"), "BBX width -5 is out of range"},
        {"huge BBX width", replace("BBX 5 7 0 0", "BBX 65 7 0 0"), "BBX width 65 is out of range"},
        {"huge BBX height", replace("BBX 5 7 0 0", "BBX 5 1234567890123 0 0"), "BBX height must be an integer"},
        {"BBX height beyond the limit", replace("BBX 5 7 0 0", "BBX 5 99999999 0 0"), "BBX height 99999999 is out of range"},
        {"huge DWIDTH", replace("DWIDTH 6 0\nBBX 5 7 0 0\nBITMAP\n70", "DWIDTH 257 0\nBBX 5 7 0 0\nBITMAP\n70"), "DWIDTH 257 is out of range"},
        {"negative DWIDTH", replace("DWIDTH 6 0\nBBX 5 7 0 0\nBITMAP\n70", "DWIDTH -1 0\nBBX 5 7 0 0\nBITMAP\n70"), "DWIDTH -1 is out of range"},
        {"duplicate encoding", replace("ENCODING 66", "ENCODING 65"), "duplicate glyph"},
        {"encoding out of range", replace("ENCODING 66", "ENCODING 1114112"), "ENCODING 1114112 is out of range"},
        {"surrogate encoding", replace("ENCODING 66", "ENCODING 55296"), "is a surrogate"},
        {"missing BBX", replace("BBX 4 7 0 0\n", ""), "needs ENCODING, DWIDTH and BBX"},
        {"missing ENCODING", replace("ENCODING 66\n", ""), "needs ENCODING, DWIDTH and BBX"},
        {"glyph without ENDCHAR", replace("ENDCHAR\nSTARTCHAR U+0042", "STARTCHAR U+0042"), "expected ENDCHAR"},
        {"glyph block restarts before BITMAP", replace("BITMAP\n70", "STARTCHAR U+0043\nBITMAP\n70"), "is not closed with ENDCHAR"},
        {"control character", replace("SIZE 9 75 75", std::string("SIZE 9 75 \x01")), "control character"},
    };
    for (const Case& c : cases) {
        GgeFont* font = gge_font_load_memory(c.bdf.data(), c.bdf.size());
        if (font || !LastErrorHas(c.reason)) std::printf("  not rejected as expected: %s (error: %s)\n", c.what, gge_last_error());
        CHECK(font == nullptr);
        CHECK(LastErrorHas(c.reason));
        gge_font_destroy(font);
    }

    // 높이가 없는 글꼴은 거부(ascent + descent == 0), ascent/descent가 없으면 FONTBOUNDINGBOX로 대신한다.
    {
        const std::string noMetric = BdfText(TestGlyphs(), 0, 0);
        GgeFont* font = gge_font_load_memory(noMetric.data(), noMetric.size());
        CHECK(font == nullptr && LastErrorHas("no line height"));
        std::string fallback = good;
        fallback.erase(fallback.find("STARTPROPERTIES 2\n"), std::string("STARTPROPERTIES 2\nFONT_ASCENT 7\nFONT_DESCENT 2\nENDPROPERTIES\n").size());
        GgeFont* viaBox = gge_font_load_memory(fallback.data(), fallback.size());
        int a = 0, d = 0;
        CHECK(viaBox != nullptr && gge_font_get_metrics(viaBox, &a, &d) == 1 && a == 7 && d == 2);   // 상자 높이 9, y 오프셋 -2
        gge_font_destroy(viaBox);
    }
    // 크기 한도(32MB 초과)와 없는 파일, null 인자.
    {
        const std::string big((32u << 20) + 1, ' ');   // 파일 크기 상한 32MB를 1바이트 넘는다
        CHECK(gge_font_load_memory(big.data(), big.size()) == nullptr && LastErrorHas("too large"));
        CHECK(gge_font_load("Z:\\no\\such\\font.bdf") == nullptr && LastErrorHas("cannot open font file"));
        CHECK(gge_font_load(nullptr) == nullptr && LastErrorHas("cannot open font file"));
        CHECK(gge_font_load_memory(nullptr, 5) == nullptr && LastErrorHas("null font data"));
        CHECK(gge_font_load_memory(nullptr, 0) == nullptr && LastErrorHas("empty"));
    }
    // CHARS는 코드 포인트가 있는 글리프 수(7: 실제 Galmuri 파일이 이렇다)로 세어도, .notdef까지 전부(8) 세어도 받는다. 그 밖의 수는 거부한다.
    for (int declared : {7, 8}) {
        const std::string bdf = replace("CHARS 8", "CHARS " + std::to_string(declared));
        GgeFont* font = gge_font_load_memory(bdf.data(), bdf.size());
        CHECK(font != nullptr);
        gge_font_destroy(font);
    }
    // 줄바꿈이 CRLF인 파일도 같은 결과다.
    {
        std::string crlf;
        for (char ch : good) { if (ch == '\n') crlf += '\r'; crlf += ch; }
        FontHandle f(crlf);
        CHECK(f.f && gge_font_has_glyph(f.f, 'A') == 1);
    }
}

void Test_TextMeasuringFollowsTheLineRules()
{
    FontHandle f(BdfText(TestGlyphs()));
    if (!f.f) return;
    auto measure = [&](const char* text, int maxWidth, int& w, int& h, int& n) { w = h = n = -1; CHECK(gge_font_measure_text(f.f, text, maxWidth, &w, &h, &n) == 1); };
    int w, h, n;
    measure("AB", 0, w, h, n);            CHECK(w == 11 && h == 9 && n == 1);          // 6 + 5, 줄 높이 9
    measure("", 0, w, h, n);              CHECK(w == 0 && h == 0 && n == 0);
    measure(nullptr, 0, w, h, n);         CHECK(w == 0 && h == 0 && n == 0);
    measure("\t\r", 0, w, h, n);          CHECK(n == 0);                                // 제어 문자만: 줄이 없다
    measure("A\tB\r", 0, w, h, n);        CHECK(w == 11 && n == 1);                     // 탭/CR은 무시
    measure("A\nB", 0, w, h, n);          CHECK(w == 6 && h == 18 && n == 2);
    measure("A\n", 0, w, h, n);           CHECK(n == 2 && h == 18);                     // 끝의 '\n'은 빈 마지막 줄
    measure("\n", 0, w, h, n);            CHECK(n == 2 && w == 0);
    measure("\n\n", 0, w, h, n);          CHECK(n == 3);
    measure("ABABABABAB", 0, w, h, n);    CHECK(n == 1 && w == 55);                     // 폭 0 이하 = 줄바꿈 없음
    measure("ABABABABAB", -5, w, h, n);   CHECK(n == 1);

    // 폭에 맞춘 줄바꿈: 공백이 있으면 마지막 공백에서, 없으면 글자 경계에서.
    measure("AB AB", 20, w, h, n);        CHECK(n == 2 && w == 11);                     // "AB"(11) + 공백(4) + A(6) = 21 > 20
    measure("AB AB", 21, w, h, n);        CHECK(n == 2 && w == 11);                     // 21까지는 A에서 들어가지만 마지막 B에서 넘어 마지막 공백에서 끊는다
    measure("AB AB", 26, w, h, n);        CHECK(n == 1 && w == 26);                     // 딱 맞으면(<= 폭) 한 줄
    measure("ABABAB", 15, w, h, n);       CHECK(n == 3 && w == 11);                     // 공백 없음: 글자 경계에서
    measure("AB A", 11, w, h, n);         CHECK(n == 2 && w == 11);                     // 넘는 자리의 공백은 사라진다: 줄 둘, 둘째 줄은 공백으로 시작하지 않는다
    measure("AB ", 11, w, h, n);          CHECK(n == 1 && w == 11);                     // 줄 끝 공백이 사라져도 빈 줄이 생기지 않는다
    measure("A B C", 6, w, h, n);         CHECK(n == 3);                                // 6 폭에 한 글자씩
    measure("W", 5, w, h, n);             CHECK(n == 1 && w == 9);                      // 폭보다 넓은 글자 하나는 혼자 한 줄(무한 루프 없음)
    measure("WW", 5, w, h, n);            CHECK(n == 2 && w == 9);
    measure("가가가", 20, w, h, n);        CHECK(n == 2 && w == 20);                     // 10 + 10 = 20은 들어간다(넘어야 줄을 바꾼다)
    measure("가가가", 19, w, h, n);        CHECK(n == 3 && w == 10);
    measure("AAAA BBBB", 24, w, h, n);    CHECK(n == 2 && w == 24);                     // 24 + 4 > 24: 공백에서 끊는다. 둘째 줄 BBBB = 20
    measure("ABAB\nAB", 8, w, h, n);      CHECK(n == 6);                                // 낱글자마다 한 줄(4) + '\n' 뒤의 AB도 폭 8에 한 글자씩(2)

    // 줄바꿈된 결과를 실제로 그린다: "AB AB"를 폭 20으로 -> 줄 높이 9로 두 줄.
    Canvas c(12, 20);
    CHECK(DrawText(f, c, "AB AB", 0, 0, 20) == 1);
    const std::string line = Rows({".###..####..", "#...#.#..#..","#...#.####..", "#####.#..#..", "#...#.#..#..", "#...#.#..#..", "#...#.####..", "............", "............"});
    CHECK(c.Ascii() == line + line + Rows({"............", "............"}));
}

void Test_TextTreatsInvalidUtf8AndMissingGlyphsWithoutCrashing()
{
    FontHandle f(BdfText(TestGlyphs()));
    FontHandle bare(BdfText(TestGlyphs(false)));   // U+FFFD도 .notdef도 없는 폰트
    if (!f.f || !bare.f) return;
    auto width = [&](const FontHandle& font, const char* text) { int w = -1, n = -1; CHECK(gge_font_measure_text(font.f, text, 0, &w, nullptr, &n) == 1); return w; };
    // 있는 글자는 그대로: A 6, 한글 10. 폰트에 없는 유효한 글자(é, 이모지)는 .notdef(폭 6) 하나.
    CHECK(width(f, "A") == 6 && width(f, "가") == 10);
    CHECK(width(f, "\xC3\xA9") == 6);                // é U+00E9: 없음 -> .notdef
    CHECK(width(f, "\xF0\x9F\x98\x80") == 6);        // 이모지(4바이트): .notdef 하나
    CHECK(width(f, "Z") == 6);                       // 없는 ASCII도 같다
    // 잘못된 UTF-8: WHATWG의 최대 부분 열 하나당 U+FFFD(폭 6) 하나.
    CHECK(width(f, "\xFF") == 6);                    // 시작 바이트가 될 수 없는 값
    CHECK(width(f, "\x80") == 6);                    // 단독 연속 바이트
    CHECK(width(f, "\xC3") == 6);                    // 잘린 2바이트
    CHECK(width(f, "\xE2\x82") == 6);                // 잘린 3바이트 = 하나
    CHECK(width(f, "\xC0\x80") == 12);               // overlong: C0은 시작 불가, 80은 단독 -> 둘
    CHECK(width(f, "\xE0\x80\x80") == 18);           // overlong 3바이트 -> 셋
    CHECK(width(f, "\xED\xA0\x80") == 18);           // 서로게이트(U+D800) -> 셋
    CHECK(width(f, "\xF4\x90\x80\x80") == 24);       // U+110000 -> 넷
    CHECK(width(f, "A\xC3(") == 18);                 // A 6 + 깨진 C3 6 + '('(없는 글자 .notdef) 6: '('는 다시 읽는다
    CHECK(width(f, "A\xFFZ") == 18);
    // 폰트에 U+FFFD와 .notdef가 없으면 대체 글자는 폭 0의 빈 글리프다(크래시 없음, 줄 규칙 그대로).
    CHECK(width(bare, "\xFF") == 0 && width(bare, "Z") == 0 && width(bare, "AZA") == 12);
    // 같은 입력을 그려도 크래시 없이 끝난다(그릴 것이 없는 폰트 포함).
    Canvas c(40, 20);
    CHECK(DrawText(f, c, "A\xFF\xC3\xA9\xF0\x9F\x98\x80Z", 0, 0) == 1);
    CHECK(gge_font_draw_text(bare.f, "A\xFFZ", 0, c.px.data(), c.w, c.h, c.Stride(), 0, 0, 0xFFFFFF) == 1);
    // 깨진 글자는 .notdef가 아니라 U+FFFD 모양(마름모)으로 그려진다.
    Canvas d(8, 10);
    CHECK(DrawText(f, d, "\xFF", 0, 0) == 1);
    CHECK(d.Ascii() == Rows({"........", "........", "..#.....", ".###....", "#####...", ".###....", "..#.....", "........", "........", "........"}));
    // 문자열 한도: 1024바이트는 되고 1025바이트는 거부(버퍼 그대로).
    const std::string ok(1024, 'A'), tooLong(1025, 'A');
    CHECK(gge_font_measure_text(f.f, ok.c_str(), 0, nullptr, nullptr, nullptr) == 1);
    Canvas e(8, 8);
    CHECK(gge_font_measure_text(f.f, tooLong.c_str(), 0, nullptr, nullptr, nullptr) == 0 && LastErrorHas("too long"));
    CHECK(DrawText(f, e, tooLong.c_str(), 0, 0) == 0 && e.Ascii() == Canvas(8, 8).Ascii());
    // C 문자열이므로 NUL에서 끝난다. null 폰트/버퍼는 거부.
    CHECK(width(f, "A\0B") == 6);
    CHECK(gge_font_measure_text(nullptr, "A", 0, nullptr, nullptr, nullptr) == 0 && LastErrorHas("null font"));
    CHECK(gge_font_draw_text(f.f, "A", 0, nullptr, 8, 8, 32, 0, 0, 0) == 0 && LastErrorHas("invalid pixel buffer"));
    CHECK(gge_font_draw_text(f.f, "A", 0, e.px.data(), 8, 8, 31, 0, 0, 0) == 0 && LastErrorHas("invalid pixel buffer"));   // stride < width * 4
    CHECK(gge_font_draw_text(f.f, "A", 0, e.px.data(), 0, 8, 32, 0, 0, 0) == 0);
    CHECK(gge_font_get_metrics(nullptr, nullptr, nullptr) == 0 && gge_font_has_glyph(nullptr, 'A') == 0);
}

void Test_TextAndRectanglesAreClippedAtTheBufferEdges()
{
    FontHandle f(BdfText(TestGlyphs()));
    if (!f.f) return;
    // 왼쪽/위로 걸친 'A': 보이는 부분만 그려지고 나머지는 잘린다(x = -3, y = -2).
    Canvas c(6, 6);
    CHECK(DrawText(f, c, "A", -3, -2) == 1);
    CHECK(c.Ascii() == Rows({".#....", "##....", ".#....", ".#....", ".#....", "......"}));
    // 오른쪽/아래로 걸친 글자도 같다(버퍼 6x6, 'A'를 (4,3)에: 위쪽 세 줄, 왼쪽 두 열만 보인다).
    Canvas r(6, 6);
    CHECK(DrawText(f, r, "A", 4, 3) == 1);
    CHECK(r.Ascii() == Rows({"......", "......", "......", ".....#", "....#.", "....#."}));
    // 완전히 밖(극단 좌표 포함)은 아무것도 바꾸지 않고 실패도 아니다.
    const int far[] = {100, -100, 2147483647, -2147483647 - 1};
    for (int x : far)
        for (int y : far) {
            Canvas e(6, 6);
            CHECK(DrawText(f, e, "AB가", x, y) == 1);
            CHECK(e.Ascii() == Canvas(6, 6).Ascii());
        }
    // 사각형: 부분/완전 밖/극단 크기. 채운 범위 = 버퍼와 겹치는 부분.
    Canvas rect(6, 4);
    CHECK(gge_fill_rect_bgra(rect.px.data(), 6, 4, rect.Stride(), -2, 1, 4, 2, 0x336699, 255) == 1);
    CHECK(rect.Ascii() == Rows({"......", "##....", "##....", "......"}));
    Canvas huge(6, 4);
    CHECK(gge_fill_rect_bgra(huge.px.data(), 6, 4, huge.Stride(), -2147483647 - 1, -2147483647 - 1, 2147483647, 2147483647, 0x336699, 255) == 1);
    CHECK(huge.Ascii() == Canvas(6, 4).Ascii());   // [-2^31, -1]에 그려지므로 버퍼와 안 겹친다
    Canvas whole(6, 4);
    CHECK(gge_fill_rect_bgra(whole.px.data(), 6, 4, whole.Stride(), -5, -5, 2147483647, 2147483647, 0x336699, 255) == 1);
    CHECK(whole.Ascii() == Rows({"######", "######", "######", "######"}));
    Canvas out(6, 4);
    CHECK(gge_fill_rect_bgra(out.px.data(), 6, 4, out.Stride(), 6, 0, 3, 3, 0x336699, 255) == 1 && out.Ascii() == Canvas(6, 4).Ascii());
    // 크기가 다른 렌더 타깃에서도 같은 규칙(1x1, 가로로 긴 버퍼).
    Canvas one(1, 1);
    CHECK(gge_fill_rect_bgra(one.px.data(), 1, 1, one.Stride(), 0, 0, 5, 5, 0x336699, 255) == 1 && one.Ascii() == "#\n");
    // 행 간격이 가로보다 큰 버퍼(stride): 패딩 바이트는 건드리지 않는다.
    std::vector<uint8_t> padded(static_cast<size_t>(8 * 4 * 3), 0x55);
    CHECK(gge_fill_rect_bgra(padded.data(), 4, 3, 8 * 4, 0, 0, 9, 9, 0x336699, 255) == 1);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 8; ++x) CHECK((padded[(static_cast<size_t>(y) * 8 + x) * 4] == 0x99) == (x < 4));

    // 알파 혼합: 정수 식 (src*a + dst*(255-a) + 127) / 255. 알파 채널은 건드리지 않는다.
    Canvas blend(2, 1);
    for (int i = 0; i < 2; ++i) { blend.px[i * 4 + 0] = 100; blend.px[i * 4 + 1] = 200; blend.px[i * 4 + 2] = 50; blend.px[i * 4 + 3] = 77; }
    CHECK(gge_fill_rect_bgra(blend.px.data(), 2, 1, blend.Stride(), 0, 0, 1, 1, 0xFF0000, 128) == 1);   // 빨강 R=255
    CHECK(blend.px[0] == 50 && blend.px[1] == 100 && blend.px[2] == 153 && blend.px[3] == 77);        // B (0*128+100*127+127)/255, G, R
    CHECK(blend.px[4] == 100 && blend.px[5] == 200 && blend.px[6] == 50);                             // 옆 픽셀은 그대로
    CHECK(gge_fill_rect_bgra(blend.px.data(), 2, 1, blend.Stride(), 1, 0, 1, 1, 0xFF0000, 0) == 1 && blend.px[4] == 100);   // 알파 0 = 그리지 않음
    CHECK(gge_fill_rect_bgra(blend.px.data(), 2, 1, blend.Stride(), 1, 0, 1, 1, 0xFF0000, 255) == 1 && blend.px[4] == 0 && blend.px[6] == 255 && blend.px[7] == 255);   // 255 = 덮어쓰기(알파 255)
    CHECK(gge_fill_rect_bgra(blend.px.data(), 2, 1, blend.Stride(), 0, 0, 1, 1, 0, 256) == 0 && LastErrorHas("alpha must be 0..255"));
    CHECK(gge_fill_rect_bgra(blend.px.data(), 2, 1, blend.Stride(), 0, 0, 1, 1, 0, -1) == 0);
    CHECK(gge_fill_rect_bgra(blend.px.data(), 2, 1, blend.Stride(), 0, 0, 0, 1, 0, 255) == 0 && LastErrorHas("rectangle size must be positive"));
    CHECK(gge_fill_rect_bgra(blend.px.data(), 2, 1, blend.Stride(), 0, 0, 1, -3, 0, 255) == 0);
    CHECK(gge_fill_rect_bgra(nullptr, 2, 1, 8, 0, 0, 1, 1, 0, 255) == 0 && LastErrorHas("invalid pixel buffer"));
}

// 정상 폰트를 무작위로 망가뜨려(줄 삭제/복제, 바이트 바꾸기, 자르기, 쓰레기 끼우기) 읽어도 죽지 않고, 읽힌 폰트로 글자를 그려도 죽지 않는다.
void Test_BitmapFontLoaderSurvivesRandomDamage()
{
    const std::string good = BdfText(TestGlyphs());
    unsigned seed = 2024;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    int accepted = 0, rejected = 0;
    for (int round = 0; round < 600; ++round) {
        std::string s = good;
        const int edits = 1 + static_cast<int>(next() % 3);
        for (int e = 0; e < edits && !s.empty(); ++e) {
            const size_t at = next() % s.size();
            switch (next() % 6) {
                case 0: s[at] = static_cast<char>(next()); break;                                           // 바이트 바꾸기
                case 1: s.erase(at, 1 + next() % 12); break;                                                // 잘라내기
                case 2: s.resize(at); break;                                                                // 자르기
                case 3: s.insert(at, std::string(1 + next() % 6, static_cast<char>(next() % 256))); break;  // 쓰레기 끼우기
                case 4: { size_t b = s.rfind('\n', at); size_t eol = s.find('\n', at); if (b != std::string::npos && eol != std::string::npos) s.erase(b, eol - b); break; }   // 줄 삭제
                case 5: { size_t b = s.rfind('\n', at); size_t eol = s.find('\n', at); if (b != std::string::npos && eol != std::string::npos) s.insert(b, s.substr(b, eol - b)); break; }   // 줄 복제
            }
        }
        GgeFont* font = gge_font_load_memory(s.data(), s.size());
        if (!font) {
            CHECK(std::string(gge_last_error()).size() > 0);
            ++rejected;
            continue;
        }
        ++accepted;
        Canvas c(30, 20);
        CHECK(gge_font_draw_text(font, "ABg W\n가A\xFF", 25, c.px.data(), c.w, c.h, c.Stride(), -2, -1, 0xFFFFFF) == 1);
        gge_font_destroy(font);
    }
    CHECK(accepted > 20 && rejected > 100);   // 두 경로가 모두 실제로 쓰였다
}

// ---- 화면 위 항목(렌더러 CPU 패스) ----
struct UiScene {
    GgeRenderer* r;
    int objects = 0;
    explicit UiScene(int w, int h) : r(NewRenderer(w, h))
    {
        if (!r) return;
        int obj = gge_object_create(r, CubeMesh(r), 0, 0);   // 배경에 3D 물체가 있어 3D가 UI 뒤에 깔리는지 보인다
        objects = obj;
        gge_renderer_set_camera(r, 0, 0, 5, 0, 0, 0, 0.9f);
    }
    ~UiScene() { gge_renderer_destroy(r); }
};

// 두 화면에서 달라진 픽셀 위치를 'x'/'.' 글자 그림으로(경계 상자 안: 시작 (x0,y0), 크기 w x h). 상자 밖에 달라진 픽셀이 있으면 '!'를 하나 덧붙인다.
std::string DiffMask(const Image& a, const Image& b, int x0, int y0, int w, int h)
{
    std::string s;
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x)
            if (a.At(x, y) != b.At(x, y) && (x < x0 || y < y0 || x >= x0 + w || y >= y0 + h)) return "!outside";
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) s += a.At(x, y) != b.At(x, y) ? 'x' : '.';
        s += '\n';
    }
    return s;
}

// 색이 정확히 rgb인 픽셀 수(알파 255).
int CountExactPixels(const Image& img, uint32_t rgb)
{
    int n = 0;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            const uint32_t p = img.At(x, y);   // BGRA 리틀엔디언: 0xAARRGGBB
            if ((p & 0x00FFFFFF) == rgb && (p >> 24) == 255) ++n;
        }
    return n;
}

void Test_UiItemsDrawOverTheFinishedFrameWithoutPs1Effects()
{
    FontHandle f(BdfText(TestGlyphs()));
    if (!f.f) return;
    UiScene s(64, 48);
    if (!s.r) return;
    const Image base = Render(s.r);

    // 글자: 3D 위에 정확한 픽셀로 덮이고, 그 밖의 모든 픽셀은 3D 결과 그대로다.
    const int text = gge_ui_create_text(s.r, f.f, "AB", 10, 20, 0, 0xFF00FF);
    CHECK(text == 1);
    const Image withText = Render(s.r);
    CHECK(DiffMask(base, withText, 10, 20, 12, 9) == Rows({".xxx..xxxx..", "x...x.x..x..", "x...x.xxxx..", "xxxxx.x..x..", "x...x.x..x..", "x...x.x..x..", "x...x.xxxx..", "............", "............"}));
    CHECK(CountExactPixels(base, 0xFF00FF) == 0 && CountExactPixels(withText, 0xFF00FF) == 38);   // 켜진 픽셀: A 18개 + B 20개, 색은 바이트까지 정확

    // PS1 효과(색 깊이 3비트 + 디더)를 켜도 글자 색은 요청한 0x336699 그대로다(UI는 후처리 뒤에 그려진다). 3D 쪽은 양자화돼 달라진다.
    CHECK(gge_renderer_set_retro(s.r, 0, 3, 1, 0) == 1);
    CHECK(gge_ui_set_color(s.r, text, 0x336699) == 1);
    const Image retro = Render(s.r);
    CHECK(DifferingPixels(withText, retro, 0) > 50);                 // 3D는 PS1 효과를 받았다
    CHECK(CountExactPixels(retro, 0x336699) == 38);                  // 3비트 양자화로는 0x33/0x66/0x99가 나올 수 없으니 38개 모두 UI 픽셀이다
    CHECK(gge_renderer_set_retro(s.r, 0, 8, 0, 0) == 1);
}

void Test_UiItemsKeepTheirOrderVisibilityAndAlpha()
{
    FontHandle f(BdfText(TestGlyphs()));
    if (!f.f) return;
    UiScene s(64, 48);
    if (!s.r) return;
    const Image base = Render(s.r);

    // 사각형 -> 글자 순으로 만들면 글자가 사각형 위에 보인다. 반투명 사각형은 3D 픽셀과 정수 식으로 섞인다.
    const int back = gge_ui_create_rect(s.r, 4, 4, 30, 20, 0x102030, 255);
    const int text = gge_ui_create_text(s.r, f.f, "A", 6, 6, 0, 0xFFFF00);
    CHECK(back == 1 && text == 2);
    Image img = Render(s.r);
    CHECK(CountExactPixels(img, 0x102030) == 30 * 20 - 18 && CountExactPixels(img, 0xFFFF00) == 18);   // 글자 18픽셀이 사각형을 덮었다
    // 순서는 만든 순서다: 글자를 먼저 만들고 사각형을 나중에 만들면 사각형이 글자를 가린다.
    UiScene t(64, 48);
    const int textFirst = gge_ui_create_text(t.r, f.f, "A", 6, 6, 0, 0xFFFF00);
    const int rectAfter = gge_ui_create_rect(t.r, 4, 4, 30, 20, 0x102030, 255);
    CHECK(textFirst == 1 && rectAfter == 2);
    img = Render(t.r);
    CHECK(CountExactPixels(img, 0x102030) == 30 * 20 && CountExactPixels(img, 0xFFFF00) == 0);

    // 반투명 사각형: 결과 픽셀 = (src*a + dst*(255-a) + 127) / 255, 모든 채널을 3D 기준 화면에서 따로 계산한 값과 비교한다. 두 번 그려도 누적되지 않는다.
    CHECK(gge_ui_destroy(s.r, back) == 1 && gge_ui_destroy(s.r, text) == 1);
    const int glass = gge_ui_create_rect(s.r, 0, 0, 64, 48, 0xC03020, 100);
    CHECK(glass == 3);                                          // ID는 재사용하지 않는다
    const Image once = Render(s.r), twice = Render(s.r);
    CHECK(once.bgra == twice.bgra);
    bool allBlended = true;
    for (int y = 0; y < 48 && allBlended; ++y)
        for (int x = 0; x < 64; ++x) {
            const uint32_t b = base.At(x, y), o = once.At(x, y);
            const int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
            const int er = (0xC0 * 100 + br * 155 + 127) / 255, eg = (0x30 * 100 + bg * 155 + 127) / 255, eb = (0x20 * 100 + bb * 155 + 127) / 255;
            if (static_cast<int>((o >> 16) & 255) != er || static_cast<int>((o >> 8) & 255) != eg || static_cast<int>(o & 255) != eb) { allBlended = false; break; }
        }
    CHECK(allBlended);

    // 숨기기/보이기/이동/색/크기/글자 바꾸기.
    CHECK(gge_ui_set_visible(s.r, glass, 0) == 1);
    CHECK(Render(s.r).bgra == base.bgra);                       // 숨기면 3D 그대로
    CHECK(gge_ui_set_visible(s.r, glass, 1) == 1 && Render(s.r).bgra == once.bgra);
    CHECK(gge_ui_set_rect(s.r, glass, 10, 5, 255) == 1 && gge_ui_set_color(s.r, glass, 0x00FF00) == 1 && gge_ui_set_position(s.r, glass, 3, 2) == 1);
    CHECK(CountExactPixels(Render(s.r), 0x00FF00) == 50);
    CHECK(CountExactPixels(base, 0xFFFFFF) == 0);              // 3D 장면에 순백이 없어야 아래 개수가 UI 픽셀만 센다
    const int label = gge_ui_create_text(s.r, f.f, "A", 0, 0, 0, 0xFFFFFF);
    CHECK(gge_ui_set_text(s.r, label, "B") == 1 && CountExactPixels(Render(s.r), 0xFFFFFF) == 20);   // B의 켜진 픽셀 20개
    CHECK(gge_ui_set_text(s.r, label, "A B") == 1 && gge_ui_set_max_width(s.r, label, 8) == 1);   // 폭 8에 한 글자씩: 세 줄(A, B)이 겹치지 않고 아래로 쌓인다
    const Image wrapped = Render(s.r);
    CHECK(CountExactPixels(wrapped, 0xFFFFFF) == 18 + 20);
    CHECK(gge_ui_set_max_width(s.r, label, 0) == 1);
    CHECK(CountExactPixels(Render(s.r), 0xFFFFFF) == 38 && Render(s.r).bgra != wrapped.bgra);   // 줄바꿈이 없으면 같은 줄에 나란히
    CHECK(gge_ui_set_max_width(s.r, glass, 5) == 0 && LastErrorHas("is a rectangle, not text"));
    CHECK(gge_ui_set_max_width(s.r, 99, 5) == 0 && LastErrorHas("unknown ui item id 99"));
    CHECK(gge_ui_set_text(s.r, label, nullptr) == 1 && CountExactPixels(Render(s.r), 0xFFFFFF) == 0);
    CHECK(gge_ui_get_count(s.r) == 2 && gge_ui_clear(s.r) == 1 && gge_ui_get_count(s.r) == 0);
    CHECK(Render(s.r).bgra == base.bgra);                       // 지우면 3D 그대로
}

void Test_UiItemsRejectBadRequestsAndChangeNothing()
{
    FontHandle f(BdfText(TestGlyphs()));
    if (!f.f) return;
    UiScene s(64, 48);
    if (!s.r) return;
    const int text = gge_ui_create_text(s.r, f.f, "A", 1, 1, 0, 0xFFFFFF);
    const int rect = gge_ui_create_rect(s.r, 0, 0, 5, 5, 0x123456, 255);
    CHECK(text == 1 && rect == 2);
    const Image before = Render(s.r);
    auto rejected = [&](const char* what, int result, const char* reason) {
        if (result != 0 || !LastErrorHas(reason)) std::printf("  not rejected as expected: %s (error: %s)\n", what, gge_last_error());
        CHECK(result == 0);
        CHECK(LastErrorHas(reason));
        CHECK(gge_ui_get_count(s.r) == 2);
    };
    rejected("null font", gge_ui_create_text(s.r, nullptr, "x", 0, 0, 0, 0), "null font");
    rejected("null renderer", gge_ui_create_text(nullptr, f.f, "x", 0, 0, 0, 0), "null renderer");
    rejected("text too long", gge_ui_create_text(s.r, f.f, std::string(1025, 'A').c_str(), 0, 0, 0, 0), "too long");
    rejected("rect width 0", gge_ui_create_rect(s.r, 0, 0, 0, 5, 0, 255), "rectangle size must be positive");
    rejected("rect height < 0", gge_ui_create_rect(s.r, 0, 0, 5, -1, 0, 255), "rectangle size must be positive");
    rejected("rect alpha 256", gge_ui_create_rect(s.r, 0, 0, 5, 5, 0, 256), "alpha must be 0..255");
    rejected("rect alpha -1", gge_ui_create_rect(s.r, 0, 0, 5, 5, 0, -1), "alpha must be 0..255");
    rejected("set text on a rectangle", gge_ui_set_text(s.r, rect, "x"), "is a rectangle, not text");
    rejected("set rect on text", gge_ui_set_rect(s.r, text, 5, 5, 255), "is text, not a rectangle");
    rejected("set rect size 0", gge_ui_set_rect(s.r, rect, 0, 5, 255), "rectangle size must be positive");
    rejected("set rect alpha", gge_ui_set_rect(s.r, rect, 5, 5, 300), "alpha must be 0..255");
    rejected("set text too long", gge_ui_set_text(s.r, text, std::string(1025, 'A').c_str()), "too long");
    rejected("unknown id (set)", gge_ui_set_position(s.r, 99, 0, 0), "unknown ui item id 99");
    rejected("unknown id (color)", gge_ui_set_color(s.r, 0, 0), "unknown ui item id 0");
    rejected("unknown id (visible)", gge_ui_set_visible(s.r, -3, 1), "unknown ui item id -3");
    rejected("unknown id (destroy)", gge_ui_destroy(s.r, 99), "unknown ui item id 99");
    CHECK(Render(s.r).bgra == before.bgra);                      // 거부된 요청은 화면도 바꾸지 않는다

    // 한도 64: 가득 차면 거부, 하나 지우면 다시 만들 수 있고 ID는 계속 늘어난다.
    for (int i = 0; i < 62; ++i) CHECK(gge_ui_create_rect(s.r, 0, 0, 1, 1, 0, 0) == 3 + i);
    CHECK(gge_ui_get_count(s.r) == 64);
    CHECK(gge_ui_create_rect(s.r, 0, 0, 1, 1, 0, 255) == 0 && LastErrorHas("too many ui items (max 64)"));
    CHECK(gge_ui_create_text(s.r, f.f, "x", 0, 0, 0, 0) == 0 && LastErrorHas("too many ui items (max 64)"));
    CHECK(gge_ui_destroy(s.r, 3) == 1 && gge_ui_create_rect(s.r, 0, 0, 1, 1, 0, 255) == 65);
    CHECK(gge_ui_destroy(s.r, 3) == 0 && LastErrorHas("unknown ui item id 3"));   // 이미 지운 ID
}

void Test_UiTextKeepsItsFontAliveAndSurvivesRendererSizes()
{
    FontHandle f(BdfText(TestGlyphs()));
    if (!f.f) return;
    // 폰트 핸들을 먼저 destroy해도 그 폰트를 쓰는 항목은 계속 그려진다(공유 참조). 렌더러를 파괴하면 항목과 폰트가 함께 정리된다.
    GgeFont* temp = gge_font_load_memory(BdfText(TestGlyphs()).data(), BdfText(TestGlyphs()).size());
    CHECK(temp != nullptr);
    GgeRenderer* r = NewRenderer(64, 48);
    if (!temp || !r) { gge_font_destroy(temp); if (r) gge_renderer_destroy(r); return; }
    const Image base = Render(r);
    CHECK(gge_ui_create_text(r, temp, "AB", 3, 3, 0, 0xFFFFFF) == 1);
    const Image drawn = Render(r);
    CHECK(CountExactPixels(drawn, 0xFFFFFF) == 38);
    gge_font_destroy(temp);                                      // 핸들만 사라진다
    CHECK(Render(r).bgra == drawn.bgra);
    CHECK(gge_ui_set_text(r, 1, "가") == 1 && CountExactPixels(Render(r), 0xFFFFFF) > 0);   // 새 글자도 같은 폰트로 그린다
    gge_renderer_destroy(r);

    // 렌더 타깃 크기가 달라도(아주 작은 것, 가로로 긴 것, 글자가 걸치는 것) 같은 규칙으로 잘려서 그려지고 크래시가 없다.
    const int sizes[][2] = {{1, 1}, {7, 5}, {33, 17}, {200, 12}};
    for (const auto& sz : sizes) {
        GgeRenderer* small = NewRenderer(sz[0], sz[1]);
        if (!small) continue;
        CHECK(gge_ui_create_rect(small, -2, -2, sz[0] + 4, 3, 0x0000FF, 255) == 1);
        CHECK(gge_ui_create_text(small, f.f, "AB가\nW", sz[0] - 3, 1, 8, 0xFFFFFF) == 2);
        const Image img = Render(small);
        CHECK(img.width == sz[0] && img.height == sz[1]);
        CHECK(CountExactPixels(img, 0x0000FF) >= std::min(sz[1], 1) );   // 위쪽 사각형이 줄어든 타깃에도 걸린다
        gge_renderer_destroy(small);
    }
}

int main()
{
    struct Case { const char* name; std::function<void()> fn; };
    const Case cases[] = {
        {"BitmapFontParsesGlyphsMetricsAndDrawsExactPixels", Test_BitmapFontParsesGlyphsMetricsAndDrawsExactPixels},
        {"BitmapFontRejectsMalformedFilesWithAReason", Test_BitmapFontRejectsMalformedFilesWithAReason},
        {"TextMeasuringFollowsTheLineRules", Test_TextMeasuringFollowsTheLineRules},
        {"TextTreatsInvalidUtf8AndMissingGlyphsWithoutCrashing", Test_TextTreatsInvalidUtf8AndMissingGlyphsWithoutCrashing},
        {"TextAndRectanglesAreClippedAtTheBufferEdges", Test_TextAndRectanglesAreClippedAtTheBufferEdges},
        {"BitmapFontLoaderSurvivesRandomDamage", Test_BitmapFontLoaderSurvivesRandomDamage},
        {"UiItemsDrawOverTheFinishedFrameWithoutPs1Effects", Test_UiItemsDrawOverTheFinishedFrameWithoutPs1Effects},
        {"UiItemsKeepTheirOrderVisibilityAndAlpha", Test_UiItemsKeepTheirOrderVisibilityAndAlpha},
        {"UiItemsRejectBadRequestsAndChangeNothing", Test_UiItemsRejectBadRequestsAndChangeNothing},
        {"UiTextKeepsItsFontAliveAndSurvivesRendererSizes", Test_UiTextKeepsItsFontAliveAndSurvivesRendererSizes},
        {"SpawnedObjectStartsFromTheBakedTemplateAndLooksLikeABakedCopy", Test_SpawnedObjectStartsFromTheBakedTemplateAndLooksLikeABakedCopy},
        {"DestroyedObjectsLeaveNoTrace_AndTheirNamesCanBeReused", Test_DestroyedObjectsLeaveNoTrace_AndTheirNamesCanBeReused},
        {"SpawnRejectsBadRequests_AndChangesNothing", Test_SpawnRejectsBadRequests_AndChangesNothing},
        {"SpawnLimitsAreEnforcedBeforeAnythingIsCreated", Test_SpawnLimitsAreEnforcedBeforeAnythingIsCreated},
        {"SpawnedSkinnedObjectsKeepIndependentAnimationState", Test_SpawnedSkinnedObjectsKeepIndependentAnimationState},
        {"SpawnFailingHalfwayRollsBackEveryObject", Test_SpawnFailingHalfwayRollsBackEveryObject},
        {"SpawnAndDestroyKeepEveryInvariantUnderRandomSequences", Test_SpawnAndDestroyKeepEveryInvariantUnderRandomSequences},
        {"GoldenCompareRules", Test_GoldenCompareRules},
        {"ZeroScaleObjectsRenderLikeVeryThinOnes_AndNeverTouchOtherObjects", Test_ZeroScaleObjectsRenderLikeVeryThinOnes_AndNeverTouchOtherObjects},
        {"ObjectVisibilityIsIndependentOfAlpha_AndHidesSubtreeShadowAndSorting", Test_ObjectVisibilityIsIndependentOfAlpha_AndHidesSubtreeShadowAndSorting},
        {"SceneClipPlaybackFollowsTheAnimationPlayerRules", Test_SceneClipPlaybackFollowsTheAnimationPlayerRules},
        {"SceneClipPlaybackRejectsBadRequestsAndChangesNothing", Test_SceneClipPlaybackRejectsBadRequestsAndChangesNothing},
        {"PlayerNodeCanChangeClipsWhileTheMovementSystemOwnsItsTransform", Test_PlayerNodeCanChangeClipsWhileTheMovementSystemOwnsItsTransform},
        {"PlayerSpeedIsTheActualMovementOfTheLastStep", Test_PlayerSpeedIsTheActualMovementOfTheLastStep},
        {"SceneNodeRotationScaleAndVisibilityFollowThePositionRules", Test_SceneNodeRotationScaleAndVisibilityFollowThePositionRules},
        {"SceneNodeWritesTouchOnlyTheFirstNodeWithTheName", Test_SceneNodeWritesTouchOnlyTheFirstNodeWithTheName},
        {"ObjectTransformRejectsNonFiniteValuesAndKeepsTheOldOne", Test_ObjectTransformRejectsNonFiniteValuesAndKeepsTheOldOne},
        {"WorldColliderEnableRules", Test_WorldColliderEnableRules},
        {"ColliderStateStaysConsistentAcrossTogglesAndAdds", Test_ColliderStateStaysConsistentAcrossTogglesAndAdds},
        {"SceneNamedCollidersOpenAndCloseLikeADoor", Test_SceneNamedCollidersOpenAndCloseLikeADoor},
        {"SceneVersion1FilesHaveUnnamedCollidersThatStillBlock", Test_SceneVersion1FilesHaveUnnamedCollidersThatStillBlock},
        {"BakedVersion1SampleLoadsAndBehavesLikeTheCurrentSample", Test_BakedVersion1SampleLoadsAndBehavesLikeTheCurrentSample},
        {"SceneInsideTriggerQueryCoversEveryTriggerWithTheName", Test_SceneInsideTriggerQueryCoversEveryTriggerWithTheName},
        {"SceneNodePositionCanBeReadMovedAndRestored", Test_SceneNodePositionCanBeReadMovedAndRestored},
        {"SceneAnswersWhetherThePlayerIsInsideATrigger", Test_SceneAnswersWhetherThePlayerIsInsideATrigger},
        {"SceneSetTriggerEnabledByName", Test_SceneSetTriggerEnabledByName},
        {"SceneSetTriggerEnabledAffectsEveryTriggerWithTheName", Test_SceneSetTriggerEnabledAffectsEveryTriggerWithTheName},
        {"SceneTeleportPlayerSwitchesRoomsAndCameraImmediately", Test_SceneTeleportPlayerSwitchesRoomsAndCameraImmediately},
        {"SceneTeleportPlayerRejectsBadTargetsAndLeavesStateAlone", Test_SceneTeleportPlayerRejectsBadTargetsAndLeavesStateAlone},
        {"SceneTeleportPlayerResetsTheHeldInputDirection", Test_SceneTeleportPlayerResetsTheHeldInputDirection},
        {"CreateRejectsInvalidSize", Test_CreateRejectsInvalidSize},
        {"EmptySceneIsUniformBackground", Test_EmptySceneIsUniformBackground},
        {"CubeIsVisibleAndFlatShaded", Test_CubeIsVisibleAndFlatShaded},
        {"RenderIsDeterministic", Test_RenderIsDeterministic},
        {"CameraMoveChangesImage", Test_CameraMoveChangesImage},
        {"DepthOrderNearFaceWins", Test_DepthOrderNearFaceWins},
        {"MeshCreateRejectsBadInput", Test_MeshCreateRejectsBadInput},
        {"LoadObj", Test_LoadObj},
        {"GoldenCube", Test_GoldenCube},
        {"RetroRejectsBadColorBits", Test_RetroRejectsBadColorBits},
        {"RetroDefaultsLeaveImageUnchanged", Test_RetroDefaultsLeaveImageUnchanged},
        {"ColorBitsQuantizeChannels", Test_ColorBitsQuantizeChannels},
        {"DitherMixesAdjacentLevels", Test_DitherMixesAdjacentLevels},
        {"VertexSnapRemovesSubpixelJitter", Test_VertexSnapRemovesSubpixelJitter},
        {"GoldenCubeRetro", Test_GoldenCubeRetro},
        {"TextureCreateRejectsBadInput", Test_TextureCreateRejectsBadInput},
        {"TextureIsUprightAndPointSampled", Test_TextureIsUprightAndPointSampled},
        {"ObjUvIsUpright", Test_ObjUvIsUpright},
        {"TextureLoadFromBmp", Test_TextureLoadFromBmp},
        {"ObjRejectsMalformedVertexLines", Test_ObjRejectsMalformedVertexLines},
        {"HugeImageIsRejectedBeforeDecoding", Test_HugeImageIsRejectedBeforeDecoding},
        {"AffineMatchesPerspectiveWhenFacingCamera", Test_AffineMatchesPerspectiveWhenFacingCamera},
        {"AffineDistortsReceedingPlane", Test_AffineDistortsReceedingPlane},
        {"GoldenFloorAffine", Test_GoldenFloorAffine},
        {"GgmRoundTripRendersIdentically", Test_GgmRoundTripRendersIdentically},
        {"GgmRejectsCorruptFiles", Test_GgmRejectsCorruptFiles},
        {"ChildFollowsParentTransform", Test_ChildFollowsParentTransform},
        {"NonUniformScaleKeepsLighting", Test_NonUniformScaleKeepsLighting},
        {"DestroyObjectRemovesDescendants", Test_DestroyObjectRemovesDescendants},
        {"SetParentRejectsCyclesAndUnknownIds", Test_SetParentRejectsCyclesAndUnknownIds},
        {"ObjectColorAndMeshSwap", Test_ObjectColorAndMeshSwap},
        {"GoldenSceneHierarchy", Test_GoldenSceneHierarchy},
        {"SkeletonRejectsBadInput", Test_SkeletonRejectsBadInput},
        {"ClipRejectsBadInput", Test_ClipRejectsBadInput},
        {"RestPoseMatchesBindChain", Test_RestPoseMatchesBindChain},
        {"ClipSamplingInterpolatesAndClamps", Test_ClipSamplingInterpolatesAndClamps},
        {"MultiKeyTranslationAndUnanimatedBonesKeepRest", Test_MultiKeyTranslationAndUnanimatedBonesKeepRest},
        {"BlendInterpolatesBetweenClips", Test_BlendInterpolatesBetweenClips},
        {"SkeletonCompatibilityIsEnforced", Test_SkeletonCompatibilityIsEnforced},
        {"SkinWeightsAreValidatedAndNormalized", Test_SkinWeightsAreValidatedAndNormalized},
        {"SkinnedRenderMatchesDeformedStaticMesh", Test_SkinnedRenderMatchesDeformedStaticMesh},
        {"ObjectTransformIsIndependentOfPose", Test_ObjectTransformIsIndependentOfPose},
        {"GoldenSkinnedBend", Test_GoldenSkinnedBend},
        {"RoomRejectsBadInput", Test_RoomRejectsBadInput},
        {"FindRoomHandlesConcaveAndBoundary", Test_FindRoomHandlesConcaveAndBoundary},
        {"ActiveRoomSwitchesAndKeepsHysteresis", Test_ActiveRoomSwitchesAndKeepsHysteresis},
        {"RoomCameraRoundTripAndDrivesRender", Test_RoomCameraRoundTripAndDrivesRender},
        {"ColliderAndMoveRejectBadInput", Test_ColliderAndMoveRejectBadInput},
        {"FreeMovementIsUnchanged", Test_FreeMovementIsUnchanged},
        {"WallStopsHeadOnAndSlidesAlongSurface", Test_WallStopsHeadOnAndSlidesAlongSurface},
        {"CornerDoesNotPenetrate", Test_CornerDoesNotPenetrate},
        {"FastMoveDoesNotTunnelThroughThinWall", Test_FastMoveDoesNotTunnelThroughThinWall},
        {"OverlappingStartIsPushedOut", Test_OverlappingStartIsPushedOut},
        {"NarrowGapBlocksOnlyWhenSmallerThanDiameter", Test_NarrowGapBlocksOnlyWhenSmallerThanDiameter},
        {"RandomWalkNeverPenetratesAndStaysInArena", Test_RandomWalkNeverPenetratesAndStaysInArena},
        {"MoveInputRejectsBadInput", Test_MoveInputRejectsBadInput},
        {"InputIsRelativeToCameraDirection", Test_InputIsRelativeToCameraDirection},
        {"InputMagnitudeIsClampedAndDeadzoned", Test_InputMagnitudeIsClampedAndDeadzoned},
        {"CameraCutKeepsInputDirectionUntilReleaseOrSharpTurn", Test_CameraCutKeepsInputDirectionUntilReleaseOrSharpTurn},
        {"ResetInputFrameDropsStaleLatch", Test_ResetInputFrameDropsStaleLatch},
        {"InputMoveThroughCollisionPerCamera", Test_InputMoveThroughCollisionPerCamera},
        {"WorldMatrixCacheTracksChangesBetweenFrames", Test_WorldMatrixCacheTracksChangesBetweenFrames},
        {"ResourceDestroyRespectsUsageAndIds", Test_ResourceDestroyRespectsUsageAndIds},
        {"TriggerRejectsBadInput", Test_TriggerRejectsBadInput},
        {"TriggerEnterAndExitEvents", Test_TriggerEnterAndExitEvents},
        {"TriggerFiresEnterWhenSpawnedInside", Test_TriggerFiresEnterWhenSpawnedInside},
        {"TriggerEventsOrderExitsBeforeEnters", Test_TriggerEventsOrderExitsBeforeEnters},
        {"TriggerDisableAndEnableWhileInside", Test_TriggerDisableAndEnableWhileInside},
        {"TriggerQueueIsFifoAndBounded", Test_TriggerQueueIsFifoAndBounded},
        {"TriggerFiresWhileWalkingThroughZone", Test_TriggerFiresWhileWalkingThroughZone},
        {"PolygonRejectionsNameTheViolatedRule", Test_PolygonRejectionsNameTheViolatedRule},
        {"RoomCameraRejectionsNameTheViolatedRule", Test_RoomCameraRejectionsNameTheViolatedRule},
        {"ColliderRejectsDegenerateBoxes", Test_ColliderRejectsDegenerateBoxes},
        {"MoveCircleRejectsUnreasonableDistancesAndRadii", Test_MoveCircleRejectsUnreasonableDistancesAndRadii},
        {"CameraCutLatchUsesTheDirectionRecordedAtTheCut", Test_CameraCutLatchUsesTheDirectionRecordedAtTheCut},
        {"CenterInsideABoxExitsThroughTheNearestFace", Test_CenterInsideABoxExitsThroughTheNearestFace},
        {"InsideCornerOfTwoWallsNeverPenetrates", Test_InsideCornerOfTwoWallsNeverPenetrates},
        {"PolygonContainmentMatchesAnIndependentReference", Test_PolygonContainmentMatchesAnIndependentReference},
        {"ClipSamplesEveryKeySegmentOnEveryAxis", Test_ClipSamplesEveryKeySegmentOnEveryAxis},
        {"ScaleChannelSamplesEveryKeyOnEveryAxis", Test_ScaleChannelSamplesEveryKeyOnEveryAxis},
        {"RotationKeysAreNormalizedAndSlerpedPerSegment", Test_RotationKeysAreNormalizedAndSlerpedPerSegment},
        {"SkeletonRejectsNonFiniteRestTransformsIndividually", Test_SkeletonRejectsNonFiniteRestTransformsIndividually},
        {"ClipDurationRules", Test_ClipDurationRules},
        {"RendererArgumentValidationTables", Test_RendererArgumentValidationTables},
        {"SkinWeightValidationChecksEveryVertexAndSlot", Test_SkinWeightValidationChecksEveryVertexAndSlot},
        {"VertexWithoutSkinWeightsFollowsBoneZero", Test_VertexWithoutSkinWeightsFollowsBoneZero},
        {"DestroyObjectKeepsUnrelatedObjects", Test_DestroyObjectKeepsUnrelatedObjects},
        {"ClipBoneRangeIsCheckedAgainstTheSkeletonBothWays", Test_ClipBoneRangeIsCheckedAgainstTheSkeletonBothWays},
        {"SkinnedMeshWithoutSkeletonRendersInBindPose", Test_SkinnedMeshWithoutSkeletonRendersInBindPose},
        {"BackgroundColorFollowsColorBits", Test_BackgroundColorFollowsColorBits},
        {"ObjIndexBoundaries", Test_ObjIndexBoundaries},
        {"GgmHeaderAndIndexBoundaries", Test_GgmHeaderAndIndexBoundaries},
        {"TextureLoadEnforcesTheSizeLimitPerDimension", Test_TextureLoadEnforcesTheSizeLimitPerDimension},
        {"RotationKeysOnArbitraryAxesMatchAnIndependentReference", Test_RotationKeysOnArbitraryAxesMatchAnIndependentReference},
        {"DefaultLightingIsTheOldFixedLight", Test_DefaultLightingIsTheOldFixedLight},
        {"AmbientAndDirectionalLightSetTheBrightnessPerChannel", Test_AmbientAndDirectionalLightSetTheBrightnessPerChannel},
        {"ObjectColorAndTextureMultiplyTheLight", Test_ObjectColorAndTextureMultiplyTheLight},
        {"PointLightFallsOffWithDistanceAndAngle", Test_PointLightFallsOffWithDistanceAndAngle},
        {"PointLightsAddUpAndAreIndependent", Test_PointLightsAddUpAndAreIndependent},
        {"PointLightLimitAndValidation", Test_PointLightLimitAndValidation},
        {"RejectedLightingChangesLeaveTheImageUnchanged", Test_RejectedLightingChangesLeaveTheImageUnchanged},
        {"ObjectAlphaBlendsWithWhatIsBehind", Test_ObjectAlphaBlendsWithWhatIsBehind},
        {"TranslucentOrderDoesNotDependOnCreationOrderAndNeverHidesWhatIsBehindIt", Test_TranslucentOrderDoesNotDependOnCreationOrderAndNeverHidesWhatIsBehindIt},
        {"OpaqueObjectInFrontOccludesTranslucentOneBehind", Test_OpaqueObjectInFrontOccludesTranslucentOneBehind},
        {"RenderTargetAlphaStaysOpaque", Test_RenderTargetAlphaStaysOpaque},
        {"AlphaAndShadowArgumentsAreValidated", Test_AlphaAndShadowArgumentsAreValidated},
        {"TextureAlphaCutsOutTexels", Test_TextureAlphaCutsOutTexels},
        {"ShadowDarkensTheFloorExactlyWhereTheLightIsBlocked", Test_ShadowDarkensTheFloorExactlyWhereTheLightIsBlocked},
        {"ShadowIsPaintedOncePerPixel", Test_ShadowIsPaintedOncePerPixel},
        {"ShadowFollowsTheLightDirection", Test_ShadowFollowsTheLightDirection},
        {"ShadowSwitchesAndGuards", Test_ShadowSwitchesAndGuards},
        {"ShadowPlaneHeightMovesTheShadow", Test_ShadowPlaneHeightMovesTheShadow},
        {"ObjectInFrontOfTheShadowOccludesIt", Test_ObjectInFrontOfTheShadowOccludesIt},
        {"SkinnedObjectCastsTheShadowOfItsCurrentPose", Test_SkinnedObjectCastsTheShadowOfItsCurrentPose},
        {"GoldenLitScene", Test_GoldenLitScene},
        {"GoldenLitSceneRetro", Test_GoldenLitSceneRetro},
        {"SceneBuiltFromTheFormatSpecRendersLikeManualConstruction", Test_SceneBuiltFromTheFormatSpecRendersLikeManualConstruction},
        {"PausedClipStaysAtTimeZeroAndStaticMeshIgnoresClip", Test_PausedClipStaysAtTimeZeroAndStaticMeshIgnoresClip},
        {"SceneLoadRejectsDamagedAndInconsistentFiles", Test_SceneLoadRejectsDamagedAndInconsistentFiles},
        {"SceneStepRejectsBadTimeAndFailedLoadLeavesNothingBehind", Test_SceneStepRejectsBadTimeAndFailedLoadLeavesNothingBehind},
        {"BakedPlaySampleStartsInsideTheFirstTrigger", Test_BakedPlaySampleStartsInsideTheFirstTrigger},
        {"BakedPlaySampleWalkFollowsTheGameplayRules", Test_BakedPlaySampleWalkFollowsTheGameplayRules},
        {"GoldenPlaySceneFromTheBakedSample", Test_GoldenPlaySceneFromTheBakedSample},
        {"SceneMeshOrderInTheFileDoesNotMatter", Test_SceneMeshOrderInTheFileDoesNotMatter},
        {"SceneCountLimitsAreCheckedAtTheirBoundaries", Test_SceneCountLimitsAreCheckedAtTheirBoundaries},
        {"SceneMeshAndSkeletonLimitsAreCheckedAtTheirBoundaries", Test_SceneMeshAndSkeletonLimitsAreCheckedAtTheirBoundaries},
        {"SceneStringAndClipLimitsAreCheckedAtTheirBoundaries", Test_SceneStringAndClipLimitsAreCheckedAtTheirBoundaries},
        {"SceneSkeletonRestPoseIsReadPerBoneAndComponent", Test_SceneSkeletonRestPoseIsReadPerBoneAndComponent},
    };
    for (const Case& c : cases) {
        int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", c.name);
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
