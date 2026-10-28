using System.Runtime.InteropServices;

namespace GunGame.Interop;

public sealed class EngineException(string message) : Exception(message);

/// <summary>
/// 엔진 C API(gge.dll)의 얇은 래퍼. 프레임버퍼 소유권은 엔진에 있다.
/// 메시/텍스처/오브젝트 ID는 1부터 시작하며 0은 "없음"이다(자세한 규약은 gge.h 참고).
/// </summary>
public sealed unsafe class EngineRenderer : IDisposable
{
    private nint _handle;

    public int Width { get; }
    public int Height { get; }

    public EngineRenderer(int width, int height, bool useWarp = false)
    {
        _handle = Native.gge_renderer_create(width, height, useWarp ? 1 : 0);
        if (_handle == 0)
            throw new EngineException(Native.LastError());
        Width = width;
        Height = height;
    }

    // ---- 리소스 ----
    /// <param name="vertices">정점당 float 8개: position(3), normal(3), uv(2)</param>
    public int CreateMesh(ReadOnlySpan<float> vertices, ReadOnlySpan<uint> indices)
    {
        fixed (float* v = vertices)
        fixed (uint* i = indices)
            return CheckId(Native.gge_mesh_create(Handle, v, vertices.Length / 8, i, indices.Length));
    }

    public int LoadMeshObj(string path) => CheckId(Native.gge_mesh_load_obj(Handle, path));

    public int LoadMeshGgm(string path) => CheckId(Native.gge_mesh_load_ggm(Handle, path));

    /// <summary>OBJ를 .ggm 바이너리 메시로 변환한다(렌더러 인스턴스 불필요).</summary>
    public static void ConvertObjToGgm(string objPath, string ggmPath) =>
        Check(Native.gge_convert_obj_to_ggm(objPath, ggmPath));

    public int CreateTexture(ReadOnlySpan<byte> rgba, int width, int height)
    {
        fixed (byte* p = rgba)
            return CheckId(Native.gge_texture_create(Handle, p, width, height));
    }

    public int LoadTexture(string path) => CheckId(Native.gge_texture_load(Handle, path));

    // ---- 스켈레탈 애니메이션 ----
    /// <param name="vertices">정점당 float 8개</param>
    /// <param name="joints">정점당 uint 4개(본 인덱스)</param>
    /// <param name="weights">정점당 float 4개(엔진이 합 1로 정규화)</param>
    public int CreateSkinnedMesh(ReadOnlySpan<float> vertices, ReadOnlySpan<uint> joints, ReadOnlySpan<float> weights,
                                 ReadOnlySpan<uint> indices)
    {
        fixed (float* v = vertices)
        fixed (uint* j = joints)
        fixed (float* w = weights)
        fixed (uint* i = indices)
            return CheckId(Native.gge_mesh_create_skinned(Handle, v, vertices.Length / 8, j, w, i, indices.Length));
    }

    public int CreateSkeleton(SkeletonData skeleton)
    {
        fixed (int* parents = skeleton.Parents)
        fixed (float* inverseBind = skeleton.InverseBind)
        fixed (float* translation = skeleton.RestTranslation)
        fixed (float* rotation = skeleton.RestRotation)
        fixed (float* scale = skeleton.RestScale)
            return CheckId(Native.gge_skeleton_create(Handle, skeleton.BoneCount, parents, inverseBind, translation, rotation, scale));
    }

    public int CreateClip(ClipData clip)
    {
        var channels = new NativeClipChannel[clip.Channels.Count];
        var pins = new List<GCHandle>();
        try
        {
            for (int i = 0; i < channels.Length; i++)
            {
                ClipChannelData c = clip.Channels[i];
                GCHandle times = GCHandle.Alloc(c.Times, GCHandleType.Pinned);
                pins.Add(times);
                GCHandle values = GCHandle.Alloc(c.Values, GCHandleType.Pinned);
                pins.Add(values);
                channels[i] = new NativeClipChannel
                {
                    Bone = c.Bone,
                    Path = (int)c.Path,
                    KeyCount = c.Times.Length,
                    Times = (float*)times.AddrOfPinnedObject(),
                    Values = (float*)values.AddrOfPinnedObject(),
                };
            }
            fixed (NativeClipChannel* p = channels)
                return CheckId(Native.gge_clip_create(Handle, clip.Duration, channels.Length, p));
        }
        finally
        {
            foreach (GCHandle pin in pins)
                pin.Free();
        }
    }

    public void SetObjectSkeleton(int objectId, int skeletonId) => Check(Native.gge_object_set_skeleton(Handle, objectId, skeletonId));

    /// <summary>시간은 호출자가 구동한다. clipA 0 = 레스트 포즈, clipB 0 = 블렌딩 없음, blend 0..1.</summary>
    public void SetObjectPose(int objectId, int clipA, float timeA, int clipB = 0, float timeB = 0, float blend = 0) =>
        Check(Native.gge_object_set_pose(Handle, objectId, clipA, timeA, clipB, timeB, blend));

    /// <summary>본별 모델 공간 전역 행렬(본당 16 float, 행 벡터 규약, 이동은 12..14번째).</summary>
    public float[] GetObjectPose(int objectId, int boneCount)
    {
        var matrices = new float[boneCount * 16];
        fixed (float* p = matrices)
            Check(Native.gge_object_get_pose(Handle, objectId, p, boneCount));
        return matrices;
    }

    // ---- 오브젝트(씬 그래프) ----
    public int CreateObject(int meshId, int textureId = 0, int parentId = 0) =>
        CheckId(Native.gge_object_create(Handle, meshId, textureId, parentId));

    public void DestroyObject(int objectId) => Check(Native.gge_object_destroy(Handle, objectId));

    public void SetObjectParent(int objectId, int parentId) => Check(Native.gge_object_set_parent(Handle, objectId, parentId));

    public void SetObjectMesh(int objectId, int meshId) => Check(Native.gge_object_set_mesh(Handle, objectId, meshId));

    public void SetObjectTexture(int objectId, int textureId) => Check(Native.gge_object_set_texture(Handle, objectId, textureId));

    /// <param name="rotation">(pitch X, yaw Y, roll Z) 라디안</param>
    public void SetObjectTransform(int objectId, Vec3 position, Vec3 rotation, Vec3 scale)
    {
        float* p = stackalloc float[] { position.X, position.Y, position.Z };
        float* r = stackalloc float[] { rotation.X, rotation.Y, rotation.Z };
        float* s = stackalloc float[] { scale.X, scale.Y, scale.Z };
        Check(Native.gge_object_set_transform(Handle, objectId, p, r, s));
    }

    public void SetObjectColor(int objectId, Vec3 rgb) =>
        Check(Native.gge_object_set_color(Handle, objectId, rgb.X, rgb.Y, rgb.Z));

    // ---- 카메라/렌더 ----
    public void SetCamera(Vec3 eye, Vec3 target, float fovYRadians) =>
        Native.gge_renderer_set_camera(Handle, eye.X, eye.Y, eye.Z, target.X, target.Y, target.Z, fovYRadians);

    /// <summary>PS1풍 효과. colorBits는 채널당 1..8비트(8이면 양자화 없음).</summary>
    public void SetRetro(bool vertexSnap, int colorBits, bool dither, bool affineTexture = false) =>
        Check(Native.gge_renderer_set_retro(Handle, vertexSnap ? 1 : 0, colorBits, dither ? 1 : 0, affineTexture ? 1 : 0));

    public void Render() => Check(Native.gge_renderer_render(Handle));

    /// <summary>BGRA8 프레임버퍼. 다음 Render 호출 전까지만 유효하다.</summary>
    public ReadOnlySpan<byte> Pixels
    {
        get
        {
            byte* p = Native.gge_renderer_get_pixels(Handle, out _, out int height, out int stride);
            return new ReadOnlySpan<byte>(p, stride * height);
        }
    }

    public int Stride => Width * 4;

    public void Dispose()
    {
        if (_handle != 0)
        {
            Native.gge_renderer_destroy(_handle);
            _handle = 0;
        }
    }

    private nint Handle => _handle != 0 ? _handle : throw new ObjectDisposedException(nameof(EngineRenderer));

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

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeClipChannel
    {
        public int Bone;
        public int Path;
        public int KeyCount;
        public float* Times;
        public float* Values;
    }

    private static class Native
    {
        private const string Dll = "gge";

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_mesh_create_skinned(nint renderer, float* vertices, int vertexCount, uint* joints, float* weights, uint* indices, int indexCount);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_skeleton_create(nint renderer, int boneCount, int* parents, float* inverseBind, float* restTranslation, float* restRotation, float* restScale);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_clip_create(nint renderer, float duration, int channelCount, NativeClipChannel* channels);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_skeleton(nint renderer, int objectId, int skeletonId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_pose(nint renderer, int objectId, int clipA, float timeA, int clipB, float timeB, float blend);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_get_pose(nint renderer, int objectId, float* outMatrices, int boneCapacity);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint gge_renderer_create(int width, int height, int useWarp);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_renderer_destroy(nint renderer);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_mesh_create(nint renderer, float* vertices, int vertexCount, uint* indices, int indexCount);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_mesh_load_obj(nint renderer, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_mesh_load_ggm(nint renderer, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_convert_obj_to_ggm(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string objPath,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string ggmPath);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_texture_create(nint renderer, byte* rgba, int width, int height);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_texture_load(nint renderer, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_create(nint renderer, int meshId, int textureId, int parentId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_destroy(nint renderer, int objectId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_parent(nint renderer, int objectId, int parentId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_mesh(nint renderer, int objectId, int meshId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_texture(nint renderer, int objectId, int textureId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_transform(nint renderer, int objectId, float* position, float* rotation, float* scale);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_color(nint renderer, int objectId, float r, float g, float b);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_renderer_set_camera(nint renderer, float ex, float ey, float ez, float tx, float ty, float tz, float fovY);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_set_retro(nint renderer, int vertexSnap, int colorBits, int dither, int affineTexture);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_render(nint renderer);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern byte* gge_renderer_get_pixels(nint renderer, out int width, out int height, out int stride);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        private static extern nint gge_last_error();

        public static string LastError() => Marshal.PtrToStringUTF8(gge_last_error()) ?? "unknown engine error";
    }
}
