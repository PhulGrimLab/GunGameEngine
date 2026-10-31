// script_host.h 참고. hostfxr 사용 순서: hostfxr.dll 찾기 -> hostfxr_initialize_for_runtime_config(호스트의 runtimeconfig.json)
// -> hostfxr_get_runtime_delegate(load_assembly_and_get_function_pointer) -> GunGame.ScriptHost.Entry의 [UnmanagedCallersOnly]
// 메서드 포인터를 얻어 호출. nethost는 쓰지 않고(링크/배포 의존성을 줄이려고) hostfxr.dll 위치를 직접 찾는다.
#include "script_host.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>

#pragma warning(push, 0)
#include "../third_party/dotnet/coreclr_delegates.h"
#include "../third_party/dotnet/hostfxr.h"
#pragma warning(pop)

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <tuple>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

namespace gge_player {

namespace {

constexpr wchar_t kHostAssembly[] = L"GunGame.ScriptHost.dll";
constexpr wchar_t kHostRuntimeConfig[] = L"GunGame.ScriptHost.runtimeconfig.json";
constexpr wchar_t kEntryType[] = L"GunGame.ScriptHost.Entry, GunGame.ScriptHost";

std::string ToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Hex(int32_t code)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08X", static_cast<unsigned>(code));
    return buffer;
}

// ---- hostfxr.dll 찾기 ----
// .NET 설치 위치: DOTNET_ROOT, 레지스트리(x64 InstallLocation), 기본 위치 순.
std::vector<fs::path> DotnetRoots()
{
    std::vector<fs::path> roots;
    wchar_t env[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"DOTNET_ROOT", env, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        roots.emplace_back(env);

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64", 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        wchar_t reg[MAX_PATH];
        DWORD type = 0, size = sizeof reg - sizeof(wchar_t);   // 마지막에 NUL을 둘 자리를 남긴다
        if (RegQueryValueExW(key, L"InstallLocation", nullptr, &type, reinterpret_cast<BYTE*>(reg), &size) == ERROR_SUCCESS && type == REG_SZ) {
            reg[size / sizeof(wchar_t)] = L'\0';
            roots.emplace_back(reg);
        }
        RegCloseKey(key);
    }

    n = GetEnvironmentVariableW(L"ProgramFiles", env, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        roots.emplace_back(fs::path(env) / L"dotnet");
    return roots;
}

// "10.0.12" / "11.0.0-preview.1" -> (주, 부, 패치, 정식 여부). 형식이 아니면 false.
bool ParseVersion(const std::wstring& name, std::tuple<int, int, int, int>& out)
{
    int major = 0, minor = 0, patch = 0, consumed = 0;
    if (swscanf_s(name.c_str(), L"%d.%d.%d%n", &major, &minor, &patch, &consumed) < 3)
        return false;
    const bool release = static_cast<size_t>(consumed) == name.size();
    out = {major, minor, patch, release ? 1 : 0};
    return true;
}

// 설치된 hostfxr 중 가장 높은 버전의 경로. 없으면 빈 경로.
fs::path FindHostfxr()
{
    for (const fs::path& root : DotnetRoots()) {
        std::error_code ec;
        fs::path fxr = root / L"host" / L"fxr";
        if (!fs::is_directory(fxr, ec))
            continue;
        fs::path best;
        std::tuple<int, int, int, int> bestVersion{-1, 0, 0, 0};
        for (const fs::directory_entry& entry : fs::directory_iterator(fxr, ec)) {
            std::tuple<int, int, int, int> version;
            fs::path dll = entry.path() / L"hostfxr.dll";
            if (entry.is_directory(ec) && ParseVersion(entry.path().filename().wstring(), version) && fs::exists(dll, ec) &&
                version > bestVersion) {
                best = dll;
                bestVersion = version;
            }
        }
        if (!best.empty())
            return best;
    }
    return {};
}

// hostfxr가 실패하면서 쓰는 설명 글(예: 어떤 프레임워크 버전이 없는지)을 모은다.
thread_local std::string g_hostfxrMessages;

void HOSTFXR_CALLTYPE CollectHostfxrMessage(const char_t* message)
{
    if (!g_hostfxrMessages.empty())
        g_hostfxrMessages += "; ";
    g_hostfxrMessages += ToUtf8(message);
}

bool IsSuccess(int32_t rc)
{
    // 0 Success, 1 Success_HostAlreadyInitialized, 2 Success_DifferentRuntimeProperties
    return rc == 0 || rc == 1 || rc == 2;
}

}  // namespace

// 관리 코드 진입점(GunGame.ScriptHost.Entry). 시그니처는 Entry.cs와 같아야 한다: nint = void*, char* = UTF-16 문자열(wchar_t).
struct ScriptHost::Entries {
    using InitializeFn = int(CORECLR_DELEGATE_CALLTYPE*)(void* ggeModule, void* renderer, int width, int height, void* scene, void* font,
                                                         const wchar_t* scriptPath, const wchar_t* typeFilter);
    using StartFn = int(CORECLR_DELEGATE_CALLTYPE*)();
    using HandleTriggerFn = int(CORECLR_DELEGATE_CALLTYPE*)(int kind, int tag, const char* nameUtf8);
    using HandleActionFn = int(CORECLR_DELEGATE_CALLTYPE*)(int action);
    using UpdateFn = int(CORECLR_DELEGATE_CALLTYPE*)(float deltaSeconds);
    using GetLastErrorFn = int(CORECLR_DELEGATE_CALLTYPE*)(char* buffer, int capacity);
    using ShutdownFn = void(CORECLR_DELEGATE_CALLTYPE*)();

    InitializeFn initialize = nullptr;
    StartFn start = nullptr;
    HandleTriggerFn handleTrigger = nullptr;
    HandleActionFn handleAction = nullptr;
    UpdateFn update = nullptr;
    GetLastErrorFn getLastError = nullptr;
    ShutdownFn shutdown = nullptr;
    bool initialized = false;
};

std::unique_ptr<ScriptHost> ScriptHost::Create(const ScriptOptions& options, GgeRenderer* renderer, int width, int height,
                                               GgeScene* scene, GgeFont* font, std::string& error)
{
    std::error_code ec;
    const fs::path hostAssembly = fs::path(options.hostDir) / kHostAssembly;
    const fs::path hostConfig = fs::path(options.hostDir) / kHostRuntimeConfig;
    if (!fs::exists(hostAssembly, ec) || !fs::exists(hostConfig, ec)) {
        error = "script host not found in " + ToUtf8(options.hostDir) +
                " (needs GunGame.ScriptHost.dll and .runtimeconfig.json; build tools\\GunGameTools.slnx or pass --script-host-dir)";
        return nullptr;
    }
    if (!fs::exists(options.assembly, ec)) {
        error = "script assembly not found: " + ToUtf8(options.assembly);
        return nullptr;
    }

    const fs::path hostfxrPath = FindHostfxr();
    if (hostfxrPath.empty()) {
        error = "no .NET runtime found (hostfxr.dll under DOTNET_ROOT, the registered install location, or %ProgramFiles%\\dotnet); install the .NET 10 runtime";
        return nullptr;
    }
    HMODULE hostfxr = LoadLibraryW(hostfxrPath.c_str());   // 런타임은 되돌릴 수 없으므로 프로세스가 끝날 때까지 둔다
    if (!hostfxr) {
        error = "cannot load " + ToUtf8(hostfxrPath.wstring()) + " (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    auto init = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(GetProcAddress(hostfxr, "hostfxr_initialize_for_runtime_config"));
    auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetProcAddress(hostfxr, "hostfxr_get_runtime_delegate"));
    auto close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(hostfxr, "hostfxr_close"));
    auto setErrorWriter = reinterpret_cast<hostfxr_set_error_writer_fn>(GetProcAddress(hostfxr, "hostfxr_set_error_writer"));
    if (!init || !getDelegate || !close) {
        error = ToUtf8(hostfxrPath.wstring()) + " does not export the hosting API";
        return nullptr;
    }

    g_hostfxrMessages.clear();
    if (setErrorWriter)
        setErrorWriter(CollectHostfxrMessage);
    auto describe = [&](const char* what, int32_t rc) {
        std::string text = std::string(what) + " failed (" + Hex(rc) + ")";
        if (!g_hostfxrMessages.empty())
            text += ": " + g_hostfxrMessages;
        return text;
    };

    hostfxr_handle context = nullptr;
    int32_t rc = init(hostConfig.c_str(), nullptr, &context);
    if (!IsSuccess(rc) || !context) {
        error = describe("hostfxr_initialize_for_runtime_config", rc);
        if (context)
            close(context);
        if (setErrorWriter)
            setErrorWriter(nullptr);
        return nullptr;
    }
    load_assembly_and_get_function_pointer_fn load = nullptr;
    rc = getDelegate(context, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&load));
    close(context);
    if (!IsSuccess(rc) || !load) {
        error = describe("hostfxr_get_runtime_delegate", rc);
        if (setErrorWriter)
            setErrorWriter(nullptr);
        return nullptr;
    }

    std::unique_ptr<ScriptHost> host(new ScriptHost());
    host->_entries = std::make_unique<Entries>();
    Entries& e = *host->_entries;
    auto bind = [&](const wchar_t* method, auto& slot) {
        void* pointer = nullptr;
        int32_t result = load(hostAssembly.c_str(), kEntryType, method, UNMANAGEDCALLERSONLY_METHOD, nullptr, &pointer);
        if (!IsSuccess(result) || !pointer) {
            error = describe((std::string("binding GunGame.ScriptHost.Entry.") + ToUtf8(method)).c_str(), result);
            return false;
        }
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(pointer);
        return true;
    };
    const bool bound = bind(L"Initialize", e.initialize) && bind(L"Start", e.start) && bind(L"HandleTrigger", e.handleTrigger) &&
                       bind(L"HandleAction", e.handleAction) && bind(L"Update", e.update) && bind(L"GetLastError", e.getLastError) && bind(L"Shutdown", e.shutdown);
    if (setErrorWriter)
        setErrorWriter(nullptr);
    if (!bound)
        return nullptr;

    HMODULE gge = GetModuleHandleW(L"gge.dll");   // 스크립트 쪽 P/Invoke가 이 사본을 그대로 쓰게 한다
    if (!gge) {
        error = "gge.dll is not loaded in this process";
        return nullptr;
    }
    const std::wstring absoluteAssembly = fs::absolute(options.assembly, ec).wstring();
    if (!e.initialize(gge, renderer, width, height, scene, font, absoluteAssembly.c_str(), options.typeFilter.c_str())) {
        error = host->LastManagedError();
        return nullptr;
    }
    e.initialized = true;
    return host;
}

ScriptHost::~ScriptHost()
{
    if (_entries && _entries->initialized)
        _entries->shutdown();
}

std::string ScriptHost::LastManagedError() const
{
    const int length = _entries->getLastError(nullptr, 0);
    std::string text(static_cast<size_t>(std::max(length, 0)) + 1, '\0');
    _entries->getLastError(text.data(), static_cast<int>(text.size()));
    text.resize(static_cast<size_t>(std::max(length, 0)));
    return text.empty() ? "unknown script error" : text;
}

bool ScriptHost::Start(std::string& error)
{
    if (_entries->start())
        return true;
    error = LastManagedError();
    return false;
}

bool ScriptHost::OnTrigger(int kind, int tag, const char* name, std::string& error)
{
    if (_entries->handleTrigger(kind, tag, name ? name : ""))
        return true;
    error = LastManagedError();
    return false;
}

bool ScriptHost::OnAction(int action, std::string& error)
{
    if (_entries->handleAction(action))
        return true;
    error = LastManagedError();
    return false;
}

bool ScriptHost::OnUpdate(float deltaSeconds, std::string& error)
{
    if (_entries->update(deltaSeconds))
        return true;
    error = LastManagedError();
    return false;
}

bool ScriptHost::VerifySingleEngineModule(std::string& error)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        error = "cannot list the loaded modules (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    int count = 0;
    std::string paths;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof entry;
    for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szModule, L"gge.dll") == 0) {
            ++count;
            paths += (paths.empty() ? "" : "; ") + ToUtf8(entry.szExePath);
        }
    }
    CloseHandle(snapshot);
    if (count == 1)
        return true;
    error = std::to_string(count) + " copies of gge.dll are loaded (" + paths + "); the script runtime must share the player's engine module";
    return false;
}

}  // namespace gge_player
