// 독립 실행 플레이어: 에디터가 구운 씬(.ggs)을 창에서 실행한다. 엔진의 C API(gge.h)만 쓰는 얇은 껍데기다 -
// 게임 규칙(이동/충돌/방 카메라/트리거/애니메이션)은 모두 엔진의 gge_scene_*가 하고, 이 파일은 창, 입력 장치 읽기, 고정 타임스텝,
// 화면 표시만 맡는다.
//
// 사용: gge_player <scene.ggs> [옵션]
//   --scale N          창 확대 배율(기본 3). 렌더 해상도는 --size로 정한다.
//   --size WxH         렌더 해상도(기본 320x240).
//   --warp             소프트웨어 렌더러(WARP)를 쓴다(기본은 하드웨어 GPU).
//   --no-retro         PS1풍 효과(버텍스 스냅, 15비트 색, 디더, 아핀 텍스처)를 끈다(기본은 에디터와 같게 모두 켬).
//   --headless         창 없이 --frames만큼 진행하고 끝낸다(자동 검증용). 결과 요약을 한 줄 출력한다.
//   --frames N         헤드리스에서 진행할 프레임 수(기본 1, 프레임당 1/30초).
//   --keys 스크립트     헤드리스 입력 대본: "D:60,W:30,DW:20,-:10" = 키 조합을 프레임 수만큼 누름(W A S D, '-'은 아무것도 안 누름).
//   --screenshot 파일   헤드리스 마지막 프레임을 PPM(P6)으로 저장한다.
//   --self-test        입력 매핑/대본 해석의 자체 검사를 하고 끝낸다.
// 조작: WASD/방향키 또는 게임패드 왼쪽 스틱, Esc 종료. 입력은 활성 방 카메라 기준이다.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <gge/gge.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kFrameSeconds = 1.0f / 30.0f;   // 고정 타임스텝(에디터 플레이 모드의 30Hz와 같다)
constexpr int kMaxCatchUpSteps = 5;             // 창이 멈췄다 돌아와도 한 번에 따라잡는 최대 걸음 수(되감기 폭주 방지)
constexpr int kDefaultWidth = 320, kDefaultHeight = 240, kDefaultScale = 3;
constexpr int kMaxRenderSize = 4096, kMaxScale = 16;
constexpr float kThumbMax = 32767.0f;
constexpr int kColorBitsRetro = 5;

// ---- 입력 ----
struct Axes {
    float x = 0, y = 0;
};

// 키 네 개를 축으로: 오른쪽+/화면 위쪽+. 반대 키를 함께 누르면 상쇄된다.
Axes AxesFromKeys(bool left, bool right, bool up, bool down)
{
    return {(right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f), (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f)};
}

// 게임패드 스틱 값(-32768..32767)을 -1..1로. 데드존은 엔진(gge_world_move_input)이 적용한다.
float ThumbToAxis(int value) { return std::max(-1.0f, std::min(1.0f, static_cast<float>(value) / kThumbMax)); }

// 키보드가 눌려 있으면 키보드, 아니면 게임패드.
Axes Combine(Axes keyboard, Axes gamepad) { return (keyboard.x != 0 || keyboard.y != 0) ? keyboard : gamepad; }

// "D:60,W:30,DW:20,-:10" -> 프레임마다의 키 조합. 형식이 틀리면 false.
struct ScriptStep {
    Axes axes;
    int frames = 0;
};

bool ParseKeyScript(const std::string& text, std::vector<ScriptStep>& out)
{
    out.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t comma = text.find(',', pos);
        std::string item = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? text.size() : comma + 1;
        if (comma != std::string::npos && pos == text.size())
            return false;  // 끝에 쉼표만 남은 대본("D:5,")은 항목이 하나 빠진 것이다
        size_t colon = item.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= item.size())
            return false;
        bool left = false, right = false, up = false, down = false;
        for (char c : item.substr(0, colon)) {
            switch (std::toupper(static_cast<unsigned char>(c))) {
                case 'A': left = true; break;
                case 'D': right = true; break;
                case 'W': up = true; break;
                case 'S': down = true; break;
                case '-': break;
                default: return false;
            }
        }
        char* end = nullptr;
        long frames = std::strtol(item.c_str() + colon + 1, &end, 10);
        if (*end != '\0' || frames < 0 || frames > 1000000)
            return false;
        out.push_back({AxesFromKeys(left, right, up, down), static_cast<int>(frames)});
    }
    return !out.empty();
}

// XInput(게임패드)은 DLL을 직접 불러 쓴다: 링크 의존성과 구버전 Windows 호환 문제를 피한다.
struct XInputState {
    DWORD packet;
    WORD buttons;
    BYTE leftTrigger, rightTrigger;
    SHORT thumbLX, thumbLY, thumbRX, thumbRY;
};

class Gamepad {
public:
    Gamepad()
    {
        for (const char* name : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
            _dll = LoadLibraryA(name);
            if (_dll)
                break;
        }
        if (_dll)
            _getState = reinterpret_cast<GetStateFn>(GetProcAddress(_dll, "XInputGetState"));
    }
    ~Gamepad() { if (_dll) FreeLibrary(_dll); }
    Gamepad(const Gamepad&) = delete;
    Gamepad& operator=(const Gamepad&) = delete;

    // 첫 번째로 연결된 패드의 왼쪽 스틱. 없으면 (0, 0).
    Axes Read() const
    {
        if (!_getState)
            return {};
        for (DWORD i = 0; i < 4; ++i) {
            XInputState s{};
            if (_getState(i, &s) == ERROR_SUCCESS)
                return {ThumbToAxis(s.thumbLX), ThumbToAxis(s.thumbLY)};
        }
        return {};
    }

private:
    using GetStateFn = DWORD(WINAPI*)(DWORD, XInputState*);
    HMODULE _dll = nullptr;
    GetStateFn _getState = nullptr;
};

bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// ---- 공통 ----
struct Options {
    std::string scene;
    int width = kDefaultWidth, height = kDefaultHeight, scale = kDefaultScale;
    bool warp = false, retro = true, headless = false, selfTest = false;
    int frames = 1;
    std::string keys, screenshot;
};

std::string ToUtf8(const wchar_t* wide)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool ParseSize(const std::string& text, int& w, int& h)
{
    char* end = nullptr;
    long width = std::strtol(text.c_str(), &end, 10);
    if (*end != 'x' && *end != 'X')
        return false;
    long height = std::strtol(end + 1, &end, 10);
    if (*end != '\0' || width < 1 || height < 1 || width > kMaxRenderSize || height > kMaxRenderSize)
        return false;
    w = static_cast<int>(width);
    h = static_cast<int>(height);
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options& o, std::string& error)
{
    for (int i = 1; i < argc; ++i) {
        std::string a = ToUtf8(argv[i]);
        auto value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                error = std::string(name) + " needs a value";
                return {};
            }
            return ToUtf8(argv[++i]);
        };
        if (a == "--warp") o.warp = true;
        else if (a == "--no-retro") o.retro = false;
        else if (a == "--headless") o.headless = true;
        else if (a == "--self-test") o.selfTest = true;
        else if (a == "--scale") {
            std::string v = value("--scale");
            o.scale = std::atoi(v.c_str());
            if (v.empty() || o.scale < 1 || o.scale > kMaxScale) { error = "--scale must be 1.." + std::to_string(kMaxScale); return false; }
        } else if (a == "--frames") {
            std::string v = value("--frames");
            o.frames = std::atoi(v.c_str());
            if (v.empty() || o.frames < 0) { error = "--frames must be a non-negative number"; return false; }
        } else if (a == "--size") {
            if (!ParseSize(value("--size"), o.width, o.height)) { error = "--size must look like 320x240"; return false; }
        } else if (a == "--keys") o.keys = value("--keys");
        else if (a == "--screenshot") o.screenshot = value("--screenshot");
        else if (!a.empty() && a[0] == '-') { error = "unknown option " + a; return false; }
        else if (o.scene.empty()) o.scene = a;
        else { error = "unexpected argument " + a; return false; }
        if (!error.empty())
            return false;
    }
    if (!o.selfTest && o.scene.empty()) {
        error = "usage: gge_player <scene.ggs> [--scale N] [--size WxH] [--warp] [--no-retro] [--headless --frames N --keys D:60 --screenshot out.ppm]";
        return false;
    }
    return true;
}

bool WritePpm(const std::string& path, const uint8_t* bgra, int width, int height, int stride)
{
    FILE* f = nullptr;
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(wideLen > 0 ? wideLen - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), wideLen);
    if (_wfopen_s(&f, wide.c_str(), L"wb") != 0 || !f)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    std::vector<uint8_t> row(static_cast<size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = bgra + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            row[x * 3 + 0] = src[x * 4 + 2];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 0];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    bool ok = std::fflush(f) == 0;
    std::fclose(f);
    return ok;
}

// 렌더러와 씬을 만든다. 실패하면 사유를 error에 담고 false(만든 것은 모두 정리한다).
struct Game {
    GgeRenderer* renderer = nullptr;
    GgeScene* scene = nullptr;

    bool Start(const Options& o, std::string& error)
    {
        renderer = gge_renderer_create(o.width, o.height, o.warp ? 1 : 0);
        if (!renderer) { error = std::string("cannot create the renderer: ") + gge_last_error(); return false; }
        if (o.retro)
            gge_renderer_set_retro(renderer, 1, kColorBitsRetro, 1, 1);
        scene = gge_scene_load(renderer, o.scene.c_str());
        if (!scene) { error = std::string("cannot load the scene: ") + gge_last_error(); return false; }
        return true;
    }
    ~Game()
    {
        if (scene) gge_scene_destroy(scene);   // 씬은 렌더러보다 먼저
        if (renderer) gge_renderer_destroy(renderer);
    }
};

// 이벤트를 모두 꺼내 한 줄 글로 만든다("Enter Door 7, Exit Door 7").
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

// ---- 헤드리스 ----
int RunHeadless(const Options& o)
{
    std::vector<ScriptStep> script;
    if (!o.keys.empty() && !ParseKeyScript(o.keys, script)) {
        std::fprintf(stderr, "gge_player: invalid --keys script '%s' (expected e.g. D:60,W:30,-:10)\n", o.keys.c_str());
        return 2;
    }
    Game game;
    std::string error;
    if (!game.Start(o, error)) {
        std::fprintf(stderr, "gge_player: %s\n", error.c_str());
        return 1;
    }

    std::string events = DrainEvents(game.scene);
    size_t stepIndex = 0;
    int remainingInStep = script.empty() ? 0 : script[0].frames;
    for (int frame = 0; frame < o.frames; ++frame) {
        while (stepIndex < script.size() && remainingInStep == 0) {
            ++stepIndex;
            remainingInStep = stepIndex < script.size() ? script[stepIndex].frames : 0;
        }
        Axes axes = stepIndex < script.size() ? script[stepIndex].axes : Axes{};
        if (stepIndex < script.size())
            --remainingInStep;
        if (!gge_scene_step(game.scene, axes.x, axes.y, kFrameSeconds)) {
            std::fprintf(stderr, "gge_player: step failed: %s\n", gge_last_error());
            return 1;
        }
        std::string frameEvents = DrainEvents(game.scene);
        if (!frameEvents.empty())
            events += (events.empty() ? "" : ", ") + frameEvents;
    }

    if (!gge_renderer_render(game.renderer)) {
        std::fprintf(stderr, "gge_player: render failed: %s\n", gge_last_error());
        return 1;
    }
    if (!o.screenshot.empty()) {
        int w = 0, h = 0, stride = 0;
        const uint8_t* pixels = gge_renderer_get_pixels(game.renderer, &w, &h, &stride);
        if (!WritePpm(o.screenshot, pixels, w, h, stride)) {
            std::fprintf(stderr, "gge_player: cannot write %s\n", o.screenshot.c_str());
            return 1;
        }
    }

    float x = 0, z = 0, yaw = 0;
    if (gge_scene_get_player(game.scene, &x, &z, &yaw))
        std::printf("frames=%d player=(%.3f, %.3f) yaw=%.3f room=%s events=[%s]\n", o.frames, x, z, yaw, gge_scene_active_room_name(game.scene), events.c_str());
    else
        std::printf("frames=%d player=none events=[%s]\n", o.frames, events.c_str());
    return 0;
}

// ---- 창 ----
struct Window {
    HWND hwnd = nullptr;
    bool quit = false;
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
                DestroyWindow(hwnd);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (window)
                window->quit = true;
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;   // 화면은 매 프레임 전부 다시 그린다
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void Present(HWND hwnd, const uint8_t* pixels, int width, int height, int scale)
{
    HDC dc = GetDC(hwnd);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;   // 위에서 아래로
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);   // 최근접 확대(PS1풍 픽셀을 흐리지 않는다)
    StretchDIBits(dc, 0, 0, width * scale, height * scale, 0, 0, width, height, pixels, &info, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, dc);
}

int RunInteractive(const Options& o)
{
    Game game;
    std::string error;
    if (!game.Start(o, error)) {
        MessageBoxA(nullptr, error.c_str(), "GunGame Player", MB_OK | MB_ICONERROR);
        std::fprintf(stderr, "gge_player: %s\n", error.c_str());
        return 1;
    }
    float x = 0, z = 0, yaw = 0;
    const bool hasPlayer = gge_scene_get_player(game.scene, &x, &z, &yaw) != 0;

    Window window;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof cls;
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    cls.lpszClassName = L"GunGamePlayerWindow";
    RegisterClassExW(&cls);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;   // 크기 고정: 렌더 해상도 x 배율
    RECT rect{0, 0, o.width * o.scale, o.height * o.scale};
    AdjustWindowRect(&rect, style, FALSE);
    window.hwnd = CreateWindowExW(0, cls.lpszClassName, L"GunGame Player", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window.hwnd) {
        std::fprintf(stderr, "gge_player: cannot create the window\n");
        return 1;
    }
    SetWindowLongPtrW(window.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&window));
    ShowWindow(window.hwnd, SW_SHOW);

    Gamepad gamepad;
    std::string lastEvent = DrainEvents(game.scene);
    auto updateTitle = [&] {
        std::string title = "GunGame Player";
        if (hasPlayer)
            title += std::string(" - ") + gge_scene_active_room_name(game.scene);
        if (!lastEvent.empty())
            title += "  [" + lastEvent + "]";
        SetWindowTextA(window.hwnd, title.c_str());
    };
    updateTitle();

    LARGE_INTEGER freq, previous;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&previous);
    double accumulator = 0;

    MSG message;
    while (!window.quit) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                window.quit = true;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (window.quit)
            break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        accumulator += static_cast<double>(now.QuadPart - previous.QuadPart) / static_cast<double>(freq.QuadPart);
        previous = now;

        int steps = 0;
        while (accumulator >= kFrameSeconds && steps < kMaxCatchUpSteps) {
            Axes keyboard = (GetForegroundWindow() == window.hwnd)
                                ? AxesFromKeys(KeyDown('A') || KeyDown(VK_LEFT), KeyDown('D') || KeyDown(VK_RIGHT),
                                               KeyDown('W') || KeyDown(VK_UP), KeyDown('S') || KeyDown(VK_DOWN))
                                : Axes{};
            Axes axes = Combine(keyboard, gamepad.Read());
            if (!gge_scene_step(game.scene, axes.x, axes.y, kFrameSeconds)) {
                MessageBoxA(window.hwnd, gge_last_error(), "GunGame Player", MB_OK | MB_ICONERROR);
                return 1;
            }
            std::string events = DrainEvents(game.scene);
            if (!events.empty()) {
                lastEvent = events;
                updateTitle();
            }
            accumulator -= kFrameSeconds;
            ++steps;
        }
        if (steps == kMaxCatchUpSteps)
            accumulator = 0;   // 너무 뒤처졌으면 따라잡기를 포기한다(창을 끌었다 놓은 뒤 순간이동 방지)

        if (steps > 0) {
            if (gge_renderer_render(game.renderer)) {
                int w = 0, h = 0, stride = 0;
                const uint8_t* pixels = gge_renderer_get_pixels(game.renderer, &w, &h, &stride);
                Present(window.hwnd, pixels, w, h, o.scale);
            }
        } else {
            Sleep(1);
        }
    }
    return 0;
}

// ---- 자체 검사 ----
int SelfTest()
{
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    };
    auto same = [](Axes a, float x, float y) { return a.x == x && a.y == y; };

    check(same(AxesFromKeys(false, false, false, false), 0, 0), "no keys");
    check(same(AxesFromKeys(false, true, false, false), 1, 0), "right");
    check(same(AxesFromKeys(true, false, false, false), -1, 0), "left");
    check(same(AxesFromKeys(false, false, true, false), 0, 1), "up");
    check(same(AxesFromKeys(false, false, false, true), 0, -1), "down");
    check(same(AxesFromKeys(true, true, false, false), 0, 0), "left+right cancel");
    check(same(AxesFromKeys(false, false, true, true), 0, 0), "up+down cancel");
    check(same(AxesFromKeys(false, true, true, false), 1, 1), "diagonal keeps both axes");

    check(ThumbToAxis(0) == 0 && ThumbToAxis(32767) == 1.0f && ThumbToAxis(-32768) == -1.0f, "thumb range is clamped to -1..1");
    check(std::fabs(ThumbToAxis(16384) - 0.5f) < 1e-3f, "thumb midpoint");
    check(same(Combine({1, 0}, {0, 1}), 1, 0) && same(Combine({0, 0}, {0.3f, -0.4f}), 0.3f, -0.4f), "keyboard wins over the gamepad only while pressed");

    std::vector<ScriptStep> steps;
    check(ParseKeyScript("D:60,W:30,dw:20,-:10", steps) && steps.size() == 4, "script parses");
    if (steps.size() == 4) {
        check(same(steps[0].axes, 1, 0) && steps[0].frames == 60, "script step 0");
        check(same(steps[1].axes, 0, 1) && steps[1].frames == 30, "script step 1");
        check(same(steps[2].axes, 1, 1) && steps[2].frames == 20, "script step 2 (lowercase, two keys)");
        check(same(steps[3].axes, 0, 0) && steps[3].frames == 10, "script step 3 (no keys)");
    }
    for (const char* bad : {"", "D", "D:", ":5", "X:5", "D:-1", "D:abc", "D:5,", "D:5,,W:1", "D:5x"})
        check(!ParseKeyScript(bad, steps), (std::string("script rejects '") + bad + "'").c_str());

    int w = 0, h = 0;
    check(ParseSize("320x240", w, h) && w == 320 && h == 240, "size parses");
    check(!ParseSize("320", w, h) && !ParseSize("0x10", w, h) && !ParseSize("10x0", w, h) && !ParseSize("5000x10", w, h) && !ParseSize("10x10z", w, h), "size rejects bad input");

    if (failures == 0)
        std::printf("self-test OK\n");
    return failures == 0 ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error)) {
        std::fprintf(stderr, "gge_player: %s\n", error.c_str());
        return 2;
    }
    if (options.selfTest)
        return SelfTest();
    if (options.headless)
        return RunHeadless(options);

    // 더블클릭으로 실행되면 콘솔 창이 같이 뜬다: 이 프로세스만 붙어 있는 콘솔이면 떼어 낸다(터미널에서 실행했다면 남겨 둔다).
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1)
        FreeConsole();
    return RunInteractive(options);
}
