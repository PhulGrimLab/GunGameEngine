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

// 채널당 허용 오차 이내인지 비교한다.
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
    for (size_t i = 0; i < expected.bgra.size(); ++i) {
        if ((i % 4) == 3) continue;
        if (std::abs(int(expected.bgra[i]) - int(actual.bgra[i])) > tolerance) {
            std::printf("  golden mismatch at byte %zu\n", i);
            WritePpm(fs::temp_directory_path() / (name + "_actual.ppm"), actual);
            return false;
        }
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

}  // namespace

int main()
{
    struct Case { const char* name; std::function<void()> fn; };
    const Case cases[] = {
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
    };
    for (const Case& c : cases) {
        int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", c.name);
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
