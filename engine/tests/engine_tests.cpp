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
    };
    for (const Case& c : cases) {
        int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", c.name);
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
