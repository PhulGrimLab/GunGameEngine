using System.Runtime.InteropServices;

namespace GunGame.Interop;

/// <summary>구운 씬 런타임이 내보낸 트리거 이벤트(영역 이름 포함).</summary>
public readonly record struct SceneEventData(TriggerEventKind Kind, int Tag, string TriggerName);

/// <summary>
/// 노드의 현재 클립 재생 상태. <see cref="Clip"/>은 재생 중인 클립 이름(씬의 이름 그대로, 레스트 포즈면 빈 문자열), <see cref="Time"/>은 그 클립의 시간(초),
/// <see cref="Fading"/>은 크로스페이드 중, <see cref="Loop"/>는 반복, <see cref="Finished"/>는 한 번 재생이 재생 방향의 끝에 닿아 멈춘 상태다.
/// </summary>
public readonly record struct NodeAnimationState(string Clip, float Time, bool Playing, float Speed, bool Fading, bool Loop, bool Finished);

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

    /// <summary>
    /// 이름이 같은 충돌체를 모두 켜거나 끄고(처음엔 모두 켜짐) 바뀐 개수를 돌려준다: 문을 열고 닫는 데 쓴다. 이름이 비어 있거나(이름 없는 충돌체는 지정할 수
    /// 없다) 그 이름이 없으면 <see cref="EngineException"/>. 닫을 때 플레이어가 안에 겹쳐 있으면 다음 <see cref="Step"/>에서 가장 가까운 면 바깥으로 밀려난다.
    /// </summary>
    public int SetColliderEnabled(string colliderName, bool enabled)
    {
        int count = Native.gge_scene_set_collider_enabled(Handle, colliderName, enabled ? 1 : 0);
        if (count == 0)
            throw new EngineException(Native.LastError());
        return count;
    }

    /// <summary>
    /// 플레이어가 지금 그 이름의 트리거(여럿이면 어느 하나) 안에 있는가. 이벤트와 같은 시점의 상태라서, 트리거를 끄거나 켠 결과는
    /// 다음 <see cref="Step"/>부터 보인다. 그 이름이 없으면 <see cref="EngineException"/>.
    /// </summary>
    public bool IsInsideTrigger(string triggerName)
    {
        int result = Native.gge_scene_is_inside_trigger(Handle, triggerName);
        if (result < 0)
            throw new EngineException(Native.LastError());
        return result == 1;
    }

    /// <summary>이름이 같은 첫 노드의 부모 기준(로컬) 위치(마지막으로 설정한 값). 그런 노드가 없으면 null.</summary>
    public Vec3? GetNodePosition(string nodeName)
    {
        float x, y, z;
        return Native.gge_scene_get_node_position(Handle, nodeName, &x, &y, &z) == 0 ? null : new Vec3(x, y, z);
    }

    /// <summary>
    /// 이름이 같은 첫 노드의 로컬 위치를 바꾼다(회전/크기 유지). 노드가 없거나, 값이 유한하지 않거나, 플레이어 노드면
    /// <see cref="EngineException"/>(상태는 그대로). 플레이어는 <see cref="TeleportPlayer"/>로만 옮긴다.
    /// </summary>
    public void SetNodePosition(string nodeName, Vec3 position)
    {
        if (Native.gge_scene_set_node_position(Handle, nodeName, position.X, position.Y, position.Z) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>
    /// 이름이 같은 첫 노드의 로컬 회전(오일러 pitch X, yaw Y, roll Z, 라디안; 한 축 회전은 문제없고 여러 축을 섞으면 오일러의 짐벌 한계가 있다).
    /// 그런 노드가 없으면 null.
    /// </summary>
    public Vec3? GetNodeRotation(string nodeName)
    {
        float x, y, z;
        return Native.gge_scene_get_node_rotation(Handle, nodeName, &x, &y, &z) == 0 ? null : new Vec3(x, y, z);
    }

    /// <summary>위치와 같은 규칙: 노드가 없거나, 값이 유한하지 않거나, 플레이어 노드면 <see cref="EngineException"/>(상태는 그대로).</summary>
    public void SetNodeRotation(string nodeName, Vec3 rotation)
    {
        if (Native.gge_scene_set_node_rotation(Handle, nodeName, rotation.X, rotation.Y, rotation.Z) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>이름이 같은 첫 노드의 로컬 크기. 그런 노드가 없으면 null.</summary>
    public Vec3? GetNodeScale(string nodeName)
    {
        float x, y, z;
        return Native.gge_scene_get_node_scale(Handle, nodeName, &x, &y, &z) == 0 ? null : new Vec3(x, y, z);
    }

    /// <summary>위치와 같은 규칙. 크기 0은 받는다(납작해지거나, 세 축이 0이면 그려지지 않는다).</summary>
    public void SetNodeScale(string nodeName, Vec3 scale)
    {
        if (Native.gge_scene_set_node_scale(Handle, nodeName, scale.X, scale.Y, scale.Z) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>
    /// 이름이 같은 첫 노드가 보이는가(처음엔 true). <b>그 노드 자신의 플래그만</b> 돌려준다: 부모가 숨겨져서 안 그려지는 중이어도 true일 수 있다.
    /// 그런 노드가 없으면 null.
    /// </summary>
    public bool? IsNodeVisible(string nodeName)
    {
        int result = Native.gge_scene_get_node_visible(Handle, nodeName);
        return result < 0 ? null : result == 1;
    }

    /// <summary>
    /// 노드를 보이게/숨긴다. 숨기면 그 노드와 모든 자손이 그려지지 않고 그림자도 없다. 알파와 별개 상태다(다시 보이게 하면 알파 그대로).
    /// 노드가 없으면 <see cref="EngineException"/>. 플레이어 노드도 숨길 수 있다.
    /// </summary>
    public void SetNodeVisible(string nodeName, bool visible)
    {
        if (Native.gge_scene_set_node_visible(Handle, nodeName, visible ? 1 : 0) == 0)
            throw new EngineException(Native.LastError());
    }

    // ---- 클립 재생(스킨 메시 노드, 플레이어 노드 포함; 규칙은 gge.h와 C# AnimationPlayer와 같다) ----
    /// <summary>
    /// 노드의 클립을 0초부터 재생한다. 이미 그 클립이 현재 클립이면(loop가 달라도) 아무것도 하지 않는다: 끝난 한 번 재생 클립이나 반복 중인 클립을 loop만 바꿔
    /// 다시 부르면 무시되므로 처음부터 다시 하려면 <paramref name="restart"/>를 true로 준다. 다른 클립이고 이미 재생 중이면 <paramref name="fadeSeconds"/> &gt; 0일 때
    /// 이전 클립에서 크로스페이드한다(레스트 포즈에서 시작하면 즉시). <paramref name="loop"/>가 false면 한 번 재생하고 끝 시간에서 멈춰 마지막 포즈를 유지한다.
    /// 클립 이름은 씬의 이름 그대로(대소문자 구분, Blender glb는 "Armature|Walk" 형태일 수 있다). 없는 노드/스킨 아닌 노드/없는 클립(가능한 이름 목록 포함)/
    /// fadeSeconds가 0..60이 아님은 <see cref="EngineException"/>.
    /// </summary>
    public void PlayClip(string nodeName, string clipName, float fadeSeconds = 0, bool restart = false, bool loop = true)
    {
        if (Native.gge_scene_play_clip(Handle, nodeName, clipName, fadeSeconds, restart ? 1 : 0, loop ? 1 : 0) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>재생을 멈추고 레스트 포즈로 돌아간다.</summary>
    public void StopClip(string nodeName)
    {
        if (Native.gge_scene_stop_clip(Handle, nodeName) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>재생 속도 배율(유한, 음수면 거꾸로). 속도가 유한하지 않으면 <see cref="EngineException"/>.</summary>
    public void SetClipSpeed(string nodeName, float speed)
    {
        if (Native.gge_scene_set_clip_speed(Handle, nodeName, speed) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>false면 일시정지(시간과 크로스페이드 진행이 모두 멈춘다), true면 재개.</summary>
    public void SetClipPlaying(string nodeName, bool playing)
    {
        if (Native.gge_scene_set_clip_playing(Handle, nodeName, playing ? 1 : 0) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>노드의 현재 재생 상태. 없는 노드/스킨 아닌 노드는 <see cref="EngineException"/>.</summary>
    public NodeAnimationState GetNodeAnimation(string nodeName) =>
        TryGetNodeAnimation(nodeName) ?? throw new EngineException(Native.LastError());

    /// <summary>노드의 현재 재생 상태. 없는 노드이거나 스킨 메시가 아니면 null.</summary>
    public NodeAnimationState? TryGetNodeAnimation(string nodeName)
    {
        byte* clip;
        float time, speed;
        int playing, fading, loop, finished;
        if (Native.gge_scene_get_node_animation(Handle, nodeName, &clip, &time, &playing, &speed, &fading, &loop, &finished) == 0)
            return null;
        return new NodeAnimationState(Marshal.PtrToStringUTF8((nint)clip) ?? "", time, playing == 1, speed, fading == 1, loop == 1, finished == 1);
    }

    /// <summary>
    /// 마지막 <see cref="Step"/>에서 충돌/슬라이딩을 반영한 뒤 플레이어가 실제로 이동한 거리 / dt(월드 단위/초). 벽을 정면으로 밀면 0이고 벽을 따라 미끄러지면
    /// 0보다 크다. 텔레포트 직후(다음 Step 전)와 dt가 0인 Step은 0이다. 플레이어가 없으면 0.
    /// </summary>
    public float PlayerSpeed
    {
        get
        {
            float speed;
            return Native.gge_scene_get_player_speed(Handle, &speed) == 0 ? 0 : speed;
        }
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
        public static extern int gge_scene_set_collider_enabled(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string colliderName, int enabled);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_is_inside_trigger(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string triggerName);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_get_node_position(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, float* x, float* y, float* z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_set_node_position(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, float x, float y, float z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_get_node_rotation(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, float* x, float* y, float* z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_set_node_rotation(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, float x, float y, float z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_get_node_scale(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, float* x, float* y, float* z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_set_node_scale(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, float x, float y, float z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_get_node_visible(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_set_node_visible(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, int visible);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_play_clip(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, [MarshalAs(UnmanagedType.LPUTF8Str)] string clipName, float fadeSeconds, int restart, int loop);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_stop_clip(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_set_clip_speed(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, float speed);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_set_clip_playing(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, int playing);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_get_node_animation(nint scene, [MarshalAs(UnmanagedType.LPUTF8Str)] string nodeName, byte** clipName, float* time, int* playing, float* speed, int* fading, int* loop, int* finished);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_scene_get_player_speed(nint scene, float* speed);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        private static extern nint gge_last_error();

        public static string LastError() => Marshal.PtrToStringUTF8(gge_last_error()) ?? "unknown engine error";
    }
}
