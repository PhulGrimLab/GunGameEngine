using System.Runtime.InteropServices;

namespace GunGame.Interop;

/// <summary>구운 씬 런타임이 내보낸 트리거 이벤트(영역 이름 포함).</summary>
public readonly record struct SceneEventData(TriggerEventKind Kind, int Tag, string TriggerName);

/// <summary>
/// 엔진의 구운 씬 런타임(gge_scene_*)의 얇은 래퍼. 독립 플레이어와 같은 코드를 C#에서 돌려 보거나(교차 검증, 도구) 구운 파일이
/// 올바르게 로드되는지 확인하는 데 쓴다. 씬이 만든 오브젝트/조명/리소스는 Dispose에서 렌더러에서 제거되므로
/// <b>렌더러보다 먼저 Dispose</b>해야 한다. 한 스레드에서만 사용한다. 규칙은 gge.h의 "구운 씬 런타임" 절을 따른다.
/// </summary>
public sealed unsafe class EngineScene : IDisposable
{
    private nint _handle;
    private readonly bool _owned;

    private EngineScene(nint handle, bool owned = true)
    {
        _handle = handle;
        _owned = owned;
    }

    /// <summary>
    /// 이미 만들어진 네이티브 씬(예: 독립 플레이어가 호스팅한 .NET 스크립트가 받은 핸들)을 <b>빌려 쓴다</b>.
    /// Dispose해도 네이티브 씬은 해제되지 않으며, 씬의 수명은 만든 쪽이 책임진다.
    /// </summary>
    public static EngineScene Attach(nint sceneHandle) =>
        sceneHandle != 0 ? new EngineScene(sceneHandle, owned: false) : throw new ArgumentException("null scene handle", nameof(sceneHandle));

    /// <summary>.ggs 파일을 렌더러에 올린다. 렌더러의 조명/그림자 설정은 씬의 값으로 바뀐다.</summary>
    /// <exception cref="EngineException">파일을 읽을 수 없거나 형식/체크섬/내용이 잘못되었을 때(사유 포함).</exception>
    public static EngineScene Load(EngineRenderer renderer, string path)
    {
        nint handle = Native.gge_scene_load(renderer.NativeHandle, path);
        if (handle == 0)
            throw new EngineException(Native.LastError());
        return new EngineScene(handle);
    }

    /// <summary>한 프레임 진행: 입력(오른쪽+/화면 위쪽+, -1..1) -> 이동/충돌 -> 방 -> 트리거 -> 애니메이션 -> 카메라.</summary>
    public void Step(float inputX, float inputY, float dt)
    {
        if (Native.gge_scene_step(Handle, inputX, inputY, dt) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>가장 오래된 트리거 이벤트를 꺼낸다. 없으면 false.</summary>
    public bool TryPollEvent(out SceneEventData e)
    {
        int kind, tag;
        byte* name;
        if (Native.gge_scene_poll_event(Handle, &kind, &tag, &name) == 0)
        {
            e = default;
            return false;
        }
        e = new SceneEventData((TriggerEventKind)kind, tag, Marshal.PtrToStringUTF8((nint)name) ?? "");
        return true;
    }

    /// <summary>플레이어의 바닥 위치와 Y축 회전. 플레이어가 없는 씬이면 null.</summary>
    public (Vec2 Position, float Yaw)? Player
    {
        get
        {
            float x, z, yaw;
            return Native.gge_scene_get_player(Handle, &x, &z, &yaw) == 0 ? null : (new Vec2(x, z), yaw);
        }
    }

    /// <summary>활성 방 이름(방이 없으면 빈 문자열).</summary>
    public string ActiveRoomName => Marshal.PtrToStringUTF8(Native.gge_scene_active_room_name(Handle)) ?? "";

    /// <summary>이름이 같은 첫 노드의 렌더러 오브젝트 ID. 없으면 0.</summary>
    public int FindObject(string nodeName) => Native.gge_scene_find_object(Handle, nodeName);

    /// <summary>
    /// 이름이 같은 트리거를 모두 켜거나 끄고 바뀐 개수를 돌려준다. 그 이름이 없으면 <see cref="EngineException"/>.
    /// 안에 있는 채로 끄면 다음 <see cref="Step"/>에서 Exit, 다시 켜면 Enter 이벤트가 나온다.
    /// </summary>
    public int SetTriggerEnabled(string triggerName, bool enabled)
    {
        int count = Native.gge_scene_set_trigger_enabled(Handle, triggerName, enabled ? 1 : 0);
        if (count == 0)
            throw new EngineException(Native.LastError());
        return count;
    }

    /// <summary>
    /// 플레이어를 바닥 위치로 즉시 옮긴다(문/방 이동). 활성 방, 카메라, 트리거가 바로 갱신되고 방향은 그대로다.
    /// 플레이어가 없거나, 값이 유한하지 않거나, 어떤 방에도 속하지 않으면 <see cref="EngineException"/>(상태는 그대로).
    /// </summary>
    public void TeleportPlayer(float x, float z)
    {
        if (Native.gge_scene_teleport_player(Handle, x, z) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>네이티브 씬 핸들(<see cref="Attach"/>와 짝). 해제된 씬이면 ObjectDisposedException.</summary>
    public nint NativeHandle => Handle;

    private nint Handle => _handle != 0 ? _handle : throw new ObjectDisposedException(nameof(EngineScene));

    public void Dispose()
    {
        if (_handle == 0)
            return;
        if (_owned)
            Native.gge_scene_destroy(_handle);
        _handle = 0;
    }

    private static class Native
    {
        private const string Dll = "gge";

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint gge_scene_load(nint renderer, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_scene_destroy(nint scene);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_step(nint scene, float inputX, float inputY, float dt);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_poll_event(nint scene, int* kind, int* tag, byte** triggerName);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_get_player(nint scene, float* x, float* z, float* yaw);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint gge_scene_active_room_name(nint scene);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_find_object(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_set_trigger_enabled(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string triggerName, int enabled);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_teleport_player(nint scene, float x, float z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        private static extern nint gge_last_error();

        public static string LastError() => Marshal.PtrToStringUTF8(gge_last_error()) ?? "unknown engine error";
    }
}
