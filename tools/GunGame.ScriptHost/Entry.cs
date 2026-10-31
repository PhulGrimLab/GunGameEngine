using System.Runtime.InteropServices;
using System.Text;
using GunGame.Interop;
using GunGame.Scripting;
using TriggerEvent = GunGame.Scripting.TriggerEvent;

namespace GunGame.ScriptHost;

/// <summary>
/// 네이티브(gge_player.exe)가 hostfxr의 <c>load_assembly_and_get_function_pointer</c>로 찾아 부르는 진입점. 모든 메서드는
/// 성공하면 1, 실패하면 0을 돌려주고(예외는 절대 네이티브로 넘기지 않는다) 사유는 <see cref="GetLastError"/>로 읽는다.
/// 한 프로세스에 스크립트 세션은 하나다. 실제 로직은 <see cref="ScriptRunner"/>에 있다.
/// </summary>
public static unsafe class Entry
{
    private static ScriptRunner? s_runner;
    private static EngineRenderer? s_renderer;
    private static EngineScene? s_scene;
    private static EngineFont? s_font;
    private static string s_lastError = "";
    private static nint s_ggeModule;
    private static bool s_resolverInstalled;

    /// <param name="ggeModule">플레이어가 이미 올려 둔 gge.dll의 모듈 핸들. 스크립트 쪽 P/Invoke가 같은 사본을 쓰게 한다.</param>
    /// <param name="typeFilter">null이거나 빈 문자열이면 어셈블리의 모든 스크립트.</param>
    /// <param name="font">플레이어가 읽어 둔 폰트 핸들(없으면 0: 글자 API는 "no font" 오류). 빌려 쓰며 해제하지 않는다.</param>
    [UnmanagedCallersOnly]
    public static int Initialize(nint ggeModule, nint renderer, int width, int height, nint scene, nint font, char* scriptPath, char* typeFilter) =>
        Run(() =>
        {
            if (s_runner is not null)
                throw new InvalidOperationException("a script session is already running");
            InstallGgeResolver(ggeModule);
            string path = new(scriptPath);
            string? filter = typeFilter is null || *typeFilter == '\0' ? null : new string(typeFilter);
            s_renderer = EngineRenderer.Attach(renderer, width, height);
            s_scene = EngineScene.Attach(scene);
            s_font = font != 0 ? EngineFont.Attach(font) : null;
            s_runner = ScriptRunner.Create(s_renderer, s_scene, path, filter, Console.Error, s_font);
        });

    [UnmanagedCallersOnly]
    public static int Start() => Run(() => Runner().Start());

    /// <param name="kind">1 = Enter, 2 = Exit(GGE_TRIGGER_*).</param>
    [UnmanagedCallersOnly]
    public static int HandleTrigger(int kind, int tag, byte* nameUtf8) =>
        Run(() =>
        {
            if (kind is not ((int)TriggerKind.Enter or (int)TriggerKind.Exit))
                throw new ArgumentOutOfRangeException(nameof(kind), kind, "unknown trigger event kind");
            string name = Marshal.PtrToStringUTF8((nint)nameUtf8) ?? "";
            Runner().HandleTrigger(new TriggerEvent((TriggerKind)kind, tag, name));
        });

    /// <param name="action">1 = Interact, 2 = Cancel, 3 = Menu(<see cref="GameAction"/>).</param>
    [UnmanagedCallersOnly]
    public static int HandleAction(int action) =>
        Run(() =>
        {
            if (!Enum.IsDefined((GameAction)action))
                throw new ArgumentOutOfRangeException(nameof(action), action, "unknown action");
            Runner().HandleAction((GameAction)action);
        });

    [UnmanagedCallersOnly]
    public static int Update(float deltaSeconds) => Run(() => Runner().Update(deltaSeconds));

    /// <summary>마지막 실패 사유(UTF-8)를 buffer에 NUL 종료로 복사하고(잘릴 수 있다) 전체 바이트 수(NUL 제외)를 돌려준다.</summary>
    [UnmanagedCallersOnly]
    public static int GetLastError(byte* buffer, int capacity)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(s_lastError);
        if (buffer is not null && capacity > 0)
        {
            int count = Math.Min(bytes.Length, capacity - 1);
            Marshal.Copy(bytes, 0, (nint)buffer, count);
            buffer[count] = 0;
        }
        return bytes.Length;
    }

    /// <summary>스크립트 세션을 끝낸다(빌려 쓴 엔진 핸들은 해제하지 않는다).</summary>
    [UnmanagedCallersOnly]
    public static void Shutdown()
    {
        s_runner = null;
        s_scene?.Dispose();
        s_renderer?.Dispose();
        s_font?.Dispose();
        s_scene = null;
        s_renderer = null;
        s_font = null;
    }

    private static ScriptRunner Runner() => s_runner ?? throw new InvalidOperationException("the script session is not initialized");

    private static int Run(Action action)
    {
        try
        {
            action();
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e is ScriptException ? e.Message : $"{e.GetType().Name}: {e.Message}";
            return 0;
        }
    }

    // "gge"를 항상 플레이어가 올려 둔 모듈로 해석한다. 기본 탐색은 어셈블리 폴더의 gge.dll을 먼저 보므로, 그런 파일이 있으면
    // 두 번째 사본이 올라가 스크립트가 보는 엔진 상태와 플레이어의 엔진 상태가 갈라진다.
    private static void InstallGgeResolver(nint ggeModule)
    {
        if (ggeModule == 0)
            throw new ArgumentException("null gge module handle", nameof(ggeModule));
        s_ggeModule = ggeModule;
        if (s_resolverInstalled)
            return;
        NativeLibrary.SetDllImportResolver(typeof(EngineScene).Assembly,
            (name, _, _) => name == "gge" ? s_ggeModule : 0);
        s_resolverInstalled = true;
    }
}
