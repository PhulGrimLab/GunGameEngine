using System.Runtime.InteropServices;

namespace GunGame.Interop;

/// <summary>바닥(XZ) 평면 위의 2D 좌표. 엔진 월드 API가 쓰는 (x, z)다.</summary>
public readonly record struct Vec2(float X, float Z);

/// <summary>방에 배정된 고정 카메라. EngineRenderer.SetCamera에 그대로 넘길 수 있다.</summary>
public readonly record struct RoomCameraData(Vec3 Eye, Vec3 Target, float FovY);

public enum TriggerEventKind
{
    Enter = 1,
    Exit = 2,
}

/// <summary>트리거 영역 진입/퇴장 이벤트. Tag는 영역을 만들 때 넘긴 호출자 정의 값이다.</summary>
public readonly record struct TriggerEvent(int TriggerId, TriggerEventKind Kind, int Tag);

/// <summary>
/// 엔진 게임플레이 월드(gge_world_*)의 얇은 래퍼: 고정 카메라 방, 충돌, 카메라 상대 입력, 트리거.
/// 렌더러와 독립이다. 한 스레드에서만 사용한다. 방/충돌체/트리거 ID는 1부터이며 0은 "없음"이다(자세한 규약은 gge.h 참고).
/// </summary>
public sealed unsafe class EngineWorld : IDisposable
{
    private nint _handle;

    public EngineWorld()
    {
        _handle = Native.gge_world_create();
        if (_handle == 0)
            throw new EngineException(Native.LastError());
    }

    private nint Handle => _handle != 0 ? _handle : throw new ObjectDisposedException(nameof(EngineWorld));

    // ---- 고정 카메라 방 ----
    /// <param name="polygonXz">(x, z) 쌍의 꼭짓점들(시계/반시계 모두 가능). 길이는 짝수여야 한다.</param>
    /// <exception cref="ArgumentException">polygonXz 길이가 홀수일 때.</exception>
    public int AddRoom(ReadOnlySpan<float> polygonXz, RoomCameraData camera)
    {
        RequireEven(polygonXz);
        float* eye = stackalloc float[] { camera.Eye.X, camera.Eye.Y, camera.Eye.Z };
        float* target = stackalloc float[] { camera.Target.X, camera.Target.Y, camera.Target.Z };
        fixed (float* p = polygonXz)
            return CheckId(Native.gge_world_add_room(Handle, p, polygonXz.Length / 2, eye, target, camera.FovY));
    }

    /// <summary>점 (x, z)를 포함하는 방 중 ID가 가장 작은 것. 없으면 0. 활성 방은 바뀌지 않는다.</summary>
    public int FindRoom(float x, float z) => Native.gge_world_find_room(Handle, x, z);

    /// <summary>플레이어 위치로 활성 방을 갱신하고 활성 방 ID를 돌려준다(방에 한 번도 들어가기 전에는 0).</summary>
    public int UpdateActiveRoom(float x, float z) => Native.gge_world_update_active_room(Handle, x, z);

    public int ActiveRoom => Native.gge_world_get_active_room(Handle);

    public RoomCameraData GetRoomCamera(int roomId)
    {
        float* eye = stackalloc float[3];
        float* target = stackalloc float[3];
        float fov;
        Check(Native.gge_world_get_room_camera(Handle, roomId, eye, target, &fov));
        return new RoomCameraData(new Vec3(eye[0], eye[1], eye[2]), new Vec3(target[0], target[1], target[2]), fov);
    }

    // ---- 충돌 ----
    public int AddBoxCollider(float minX, float minZ, float maxX, float maxZ) =>
        CheckId(Native.gge_world_add_box_collider(Handle, minX, minZ, maxX, maxZ));

    /// <summary>
    /// 콜라이더를 켜거나 끈다(처음엔 모두 켜짐). 꺼진 콜라이더는 <see cref="MoveCircle"/>이 없는 것으로 본다. 켜는 순간 안에 겹쳐 있던 이동체는
    /// 다음 MoveCircle(이동량 0이어도)에서 가장 가까운 면 바깥으로 밀려난다. 알 수 없는 ID면 <see cref="EngineException"/>.
    /// </summary>
    public void SetColliderEnabled(int colliderId, bool enabled)
    {
        if (Native.gge_world_set_collider_enabled(Handle, colliderId, enabled ? 1 : 0) == 0)
            throw new EngineException(Native.LastError());
    }

    /// <summary>콜라이더가 켜져 있는가. 알 수 없는 ID면 <see cref="EngineException"/>.</summary>
    public bool IsColliderEnabled(int colliderId)
    {
        int result = Native.gge_world_is_collider_enabled(Handle, colliderId);
        if (result < 0)
            throw new EngineException(Native.LastError());
        return result == 1;
    }

    /// <summary>반지름 radius인 원형 이동체를 pos에서 delta만큼 움직인 최종 위치(장애물에 막히면 벽을 따라 미끄러진다).</summary>
    public Vec2 MoveCircle(Vec2 pos, Vec2 delta, float radius)
    {
        float* result = stackalloc float[2];
        Check(Native.gge_world_move_circle(Handle, pos.X, pos.Z, delta.X, delta.Z, radius, result));
        return new Vec2(result[0], result[1]);
    }

    // ---- 카메라 상대 입력 ----
    /// <summary>입력 축(오른쪽+, 화면 위쪽+, -1..1)을 활성 방 카메라 기준의 한 프레임 월드 이동량으로 바꾼다.
    /// 활성 방이 없으면 EngineException. 매 프레임 호출해야 한다(입력 없음 프레임이 방향 기준을 갱신한다).</summary>
    public Vec2 MoveInput(float inputX, float inputY, float speed, float dt)
    {
        float* result = stackalloc float[2];
        Check(Native.gge_world_move_input(Handle, inputX, inputY, speed, dt, result));
        return new Vec2(result[0], result[1]);
    }

    /// <summary>입력이 끊기지 않는 채 장면이 바뀌는 텔레포트/컷신 뒤에 방향 기준을 비운다.</summary>
    public void ResetInputFrame() => Native.gge_world_reset_input_frame(Handle);

    // ---- 트리거 ----
    /// <param name="polygonXz">(x, z) 쌍의 꼭짓점들. 길이는 짝수여야 한다.</param>
    /// <param name="tag">이벤트에 그대로 실리는 호출자 정의 값(문/아이템 식별 등).</param>
    public int AddTrigger(ReadOnlySpan<float> polygonXz, int tag)
    {
        RequireEven(polygonXz);
        fixed (float* p = polygonXz)
            return CheckId(Native.gge_world_add_trigger(Handle, p, polygonXz.Length / 2, tag));
    }

    public void SetTriggerEnabled(int triggerId, bool enabled) =>
        Check(Native.gge_world_set_trigger_enabled(Handle, triggerId, enabled ? 1 : 0));

    /// <summary>플레이어 위치를 반영하고 이번 호출에서 생긴 이벤트 수를 돌려준다. 이벤트는 TryPollTriggerEvent로 꺼낸다.</summary>
    public int UpdateTriggers(float x, float z)
    {
        int count = Native.gge_world_update_triggers(Handle, x, z);
        if (count < 0)
            throw new EngineException(Native.LastError());
        return count;
    }

    /// <summary>가장 오래된 이벤트 하나를 꺼낸다. 없으면 false. 큐는 1024개까지이며 가득 차면 오래된 것부터 버려진다(매 프레임 비운다).</summary>
    public bool TryPollTriggerEvent(out TriggerEvent triggerEvent)
    {
        int id, kind, tag;
        if (Native.gge_world_poll_trigger_event(Handle, &id, &kind, &tag) == 0)
        {
            triggerEvent = default;
            return false;
        }
        triggerEvent = new TriggerEvent(id, (TriggerEventKind)kind, tag);
        return true;
    }

    public bool IsInsideTrigger(int triggerId)
    {
        int result = Native.gge_world_is_inside_trigger(Handle, triggerId);
        if (result < 0)
            throw new EngineException(Native.LastError());
        return result == 1;
    }

    public void Dispose()
    {
        if (_handle != 0)
        {
            Native.gge_world_destroy(_handle);
            _handle = 0;
        }
    }

    private static void RequireEven(ReadOnlySpan<float> polygonXz)
    {
        if (polygonXz.Length % 2 != 0)
            throw new ArgumentException("polygon must be (x, z) pairs: the length must be even", nameof(polygonXz));
    }

    private static void Check(int result)
    {
        if (result == 0)
            throw new EngineException(Native.LastError());
    }

    private static int CheckId(int id)
    {
        if (id == 0)
            throw new EngineException(Native.LastError());
        return id;
    }

    private static class Native
    {
        private const string Dll = "gge";

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint gge_world_create();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_world_destroy(nint world);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_add_room(nint world, float* xz, int vertexCount, float* eye, float* target, float fovYRadians);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_find_room(nint world, float x, float z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_update_active_room(nint world, float x, float z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_get_active_room(nint world);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_get_room_camera(nint world, int roomId, float* eye, float* target, float* fovYRadians);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_add_box_collider(nint world, float minX, float minZ, float maxX, float maxZ);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_set_collider_enabled(nint world, int colliderId, int enabled);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_is_collider_enabled(nint world, int colliderId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_move_circle(nint world, float x, float z, float dx, float dz, float radius, float* outXz);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_move_input(nint world, float inputX, float inputY, float speed, float dt, float* outDxDz);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_world_reset_input_frame(nint world);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_add_trigger(nint world, float* xz, int vertexCount, int tag);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_set_trigger_enabled(nint world, int triggerId, int enabled);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_update_triggers(nint world, float x, float z);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_poll_trigger_event(nint world, int* triggerId, int* kind, int* tag);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_world_is_inside_trigger(nint world, int triggerId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint gge_last_error();

        public static string LastError() => Marshal.PtrToStringUTF8(gge_last_error()) ?? "unknown engine error";
    }
}
