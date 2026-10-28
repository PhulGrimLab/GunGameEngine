// 엔진 C API 테스트 하네스. 외부 프레임워크 없이 gge.dll을 C#과 동일한 경계로 검증한다.
// 환경 변수: GGE_UPDATE_GOLDEN=1 이면 골든 이미지를 다시 기록한다.
#include <gge/gge.h>

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

// 큐브: 면마다 정점 4개(플랫 노멀), 반시계 앞면.
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
        uint32_t base = static_cast<uint32_t>(v.size() / 6);
        for (const auto& p : f.p)
            v.insert(v.end(), {p[0], p[1], p[2], f.n[0], f.n[1], f.n[2]});
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

void SetupCubeScene(GgeRenderer* r)
{
    std::vector<float> v;
    std::vector<uint32_t> idx;
    MakeCube(v, idx);
    CHECK(gge_renderer_set_mesh(r, v.data(), static_cast<int>(v.size() / 6), idx.data(),
                                static_cast<int>(idx.size())) == 1);
    gge_renderer_set_camera(r, 4.0f, 3.0f, 5.0f, 0, 0, 0, 0.8f);
    gge_renderer_set_color(r, 0.9f, 0.6f, 0.3f);
}

// ---- 골든 이미지 (P6 PPM, RGB) ----
bool WritePpm(const std::filesystem::path& path, const Image& img)
{
    std::filesystem::create_directories(path.parent_path());
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

bool ReadPpm(const std::filesystem::path& path, Image& img)
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
    std::filesystem::path path = std::filesystem::path(GGE_TEST_DATA_DIR) / "golden" / (name + ".ppm");
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
            WritePpm(std::filesystem::temp_directory_path() / (name + "_actual.ppm"), actual);
            return false;
        }
    }
    return true;
}

// ---- 테스트 ----
void Test_CreateRejectsInvalidSize()
{
    GgeRenderer* r = gge_renderer_create(0, 100, 1);
    CHECK(r == nullptr);
    CHECK(std::strlen(gge_last_error()) > 0);
}

void Test_EmptySceneIsUniformBackground()
{
    GgeRenderer* r = NewRenderer();
    CHECK(r != nullptr);
    CHECK(gge_renderer_render(r) == 1);
    Image img = Capture(r);
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
    CHECK(gge_renderer_render(r) == 1);
    Image img = Capture(r);
    uint32_t background = img.At(0, 0);
    CHECK(img.At(img.width / 2, img.height / 2) != background);
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
    gge_renderer_render(r);
    Image a = Capture(r);
    gge_renderer_render(r);
    Image b = Capture(r);
    CHECK(a.bgra == b.bgra);
    gge_renderer_destroy(r);
}

void Test_CameraMoveChangesImage()
{
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    gge_renderer_render(r);
    Image a = Capture(r);
    gge_renderer_set_camera(r, -4.0f, 3.0f, 5.0f, 0, 0, 0, 0.8f);
    gge_renderer_render(r);
    Image b = Capture(r);
    CHECK(a.bgra != b.bgra);
    gge_renderer_destroy(r);
}

void Test_DepthOrderNearFaceWins()
{
    // 정면에서 본 큐브의 중앙 색은 +Z 면의 색(밝기: 0.25+0.75*max(0,-L·n))이어야 한다.
    GgeRenderer* r = NewRenderer();
    SetupCubeScene(r);
    gge_renderer_set_camera(r, 0, 0, 6, 0, 0, 0, 0.8f);
    gge_renderer_render(r);
    Image img = Capture(r);
    uint32_t center = img.At(img.width / 2, img.height / 2);
    // 광원 L=(-0.4,-1,-0.6)/|..|, n=(0,0,1) → dot(n,-L)=0.6/|L| ≈ 0.4688
    float lightLen = std::sqrt(0.4f * 0.4f + 1.0f + 0.6f * 0.6f);
    float shade = 0.25f + 0.75f * (0.6f / lightLen);
    int expectedRed = static_cast<int>(std::lround(0.9f * shade * 255.0f));
    int actualRed = (center >> 16) & 0xFF;
    if (std::abs(actualRed - expectedRed) > 2)
        std::printf("  center=%08X expectedRed=%d actualRed=%d\n", center, expectedRed, actualRed);
    CHECK(std::abs(actualRed - expectedRed) <= 2);
    gge_renderer_destroy(r);
}

void Test_SetMeshRejectsBadInput()
{
    GgeRenderer* r = NewRenderer();
    float v[6] = {0, 0, 0, 0, 0, 1};
    uint32_t outOfRange[3] = {0, 1, 2};
    CHECK(gge_renderer_set_mesh(r, v, 1, outOfRange, 3) == 0);
    CHECK(std::strlen(gge_last_error()) > 0);
    CHECK(gge_renderer_set_mesh(r, v, 1, outOfRange, 2) == 0);  // 인덱스 수가 3의 배수가 아님
    CHECK(gge_renderer_set_mesh(r, nullptr, 1, outOfRange, 3) == 0);
    gge_renderer_destroy(r);
}

void Test_LoadObj()
{
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "gge_tests";
    fs::create_directories(dir);

    // 쿼드(사각형 폴리곤) + 음수 인덱스 + v/vt/vn 형식을 포함한 OBJ.
    fs::path good = dir / "quad.obj";
    std::ofstream(good) << "# quad facing +Z\n"
                           "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
                           "f 1/1/1 2/2/1 3/3/1 4/4/1\n";
    GgeRenderer* r = NewRenderer();
    CHECK(gge_renderer_load_obj(r, good.string().c_str()) == 1);
    gge_renderer_set_camera(r, 0, 0, 4, 0, 0, 0, 0.8f);
    gge_renderer_render(r);
    Image img = Capture(r);
    CHECK(img.At(img.width / 2, img.height / 2) != img.At(0, 0));

    fs::path negative = dir / "neg.obj";
    std::ofstream(negative) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n";
    CHECK(gge_renderer_load_obj(r, negative.string().c_str()) == 1);

    fs::path badIndex = dir / "bad.obj";
    std::ofstream(badIndex) << "v 0 0 0\nf 1 2 3\n";
    CHECK(gge_renderer_load_obj(r, badIndex.string().c_str()) == 0);
    CHECK(std::string(gge_last_error()).find("range") != std::string::npos);

    CHECK(gge_renderer_load_obj(r, (dir / "missing.obj").string().c_str()) == 0);
    CHECK(gge_renderer_load_obj(r, nullptr) == 0);
    gge_renderer_destroy(r);
}

void Test_GoldenCube()
{
    GgeRenderer* r = NewRenderer(320, 240);
    SetupCubeScene(r);
    gge_renderer_render(r);
    CHECK(MatchesGolden("cube_320x240", Capture(r)));
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
        {"SetMeshRejectsBadInput", Test_SetMeshRejectsBadInput},
        {"LoadObj", Test_LoadObj},
        {"GoldenCube", Test_GoldenCube},
    };
    for (const Case& c : cases) {
        int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", c.name);
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
