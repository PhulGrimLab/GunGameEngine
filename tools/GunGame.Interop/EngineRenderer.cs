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
    private readonly bool _owned = true;

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

    /// <exception cref="ArgumentException">rgba가 width x height x 4바이트보다 짧을 때(네이티브가 배열 뒤를 읽지 않도록).</exception>
    public int CreateTexture(ReadOnlySpan<byte> rgba, int width, int height)
    {
        // 크기가 양수가 아닌 경우는 엔진이 같은 오류로 설명하므로 여기서는 길이 부족만 막는다.
        if (width > 0 && height > 0 && rgba.Length < (long)width * height * 4)
            throw new ArgumentException($"rgba has {rgba.Length} bytes but a {width}x{height} texture needs {(long)width * height * 4}", nameof(rgba));
        fixed (byte* p = rgba)
            return CheckId(Native.gge_texture_create(Handle, p, width, height));
    }

    public int LoadTexture(string path) => CheckId(Native.gge_texture_load(Handle, path));

    // ---- 리소스 해제: 어떤 오브젝트가 아직 쓰고 있으면 EngineException(먼저 오브젝트를 없애거나 바꾼다). 해제한 ID는 재사용되지 않는다. ----
    public void DestroyMesh(int meshId) => Check(Native.gge_mesh_destroy(Handle, meshId));

    public void DestroyTexture(int textureId) => Check(Native.gge_texture_destroy(Handle, textureId));

    public void DestroySkeleton(int skeletonId) => Check(Native.gge_skeleton_destroy(Handle, skeletonId));

    public void DestroyClip(int clipId) => Check(Native.gge_clip_destroy(Handle, clipId));

    // ---- 스켈레탈 애니메이션 ----
    /// <param name="vertices">정점당 float 8개</param>
    /// <param name="joints">정점당 uint 4개(본 인덱스)</param>
    /// <param name="weights">정점당 float 4개(엔진이 합 1로 정규화)</param>
    public int CreateSkinnedMesh(ReadOnlySpan<float> vertices, ReadOnlySpan<uint> joints, ReadOnlySpan<float> weights,
                                 ReadOnlySpan<uint> indices)
    {
        int vertexCount = vertices.Length / 8;
        if (joints.Length < (long)vertexCount * 4)
            throw new ArgumentException($"joints has {joints.Length} entries but {vertexCount} vertices need {(long)vertexCount * 4}", nameof(joints));
        if (weights.Length < (long)vertexCount * 4)
            throw new ArgumentException($"weights has {weights.Length} entries but {vertexCount} vertices need {(long)vertexCount * 4}", nameof(weights));
        fixed (float* v = vertices)
        fixed (uint* j = joints)
        fixed (float* w = weights)
        fixed (uint* i = indices)
            return CheckId(Native.gge_mesh_create_skinned(Handle, v, vertices.Length / 8, j, w, i, indices.Length));
    }

    public int CreateSkeleton(SkeletonData skeleton)
    {
        int bones = skeleton.BoneCount;
        RequireBoneArray(skeleton.InverseBind, bones, 16, nameof(skeleton.InverseBind));
        RequireBoneArray(skeleton.RestTranslation, bones, 3, nameof(skeleton.RestTranslation));
        RequireBoneArray(skeleton.RestRotation, bones, 4, nameof(skeleton.RestRotation));
        RequireBoneArray(skeleton.RestScale, bones, 3, nameof(skeleton.RestScale));
        fixed (int* parents = skeleton.Parents)
        fixed (float* inverseBind = skeleton.InverseBind)
        fixed (float* translation = skeleton.RestTranslation)
        fixed (float* rotation = skeleton.RestRotation)
        fixed (float* scale = skeleton.RestScale)
            return CheckId(Native.gge_skeleton_create(Handle, skeleton.BoneCount, parents, inverseBind, translation, rotation, scale));
    }

    private static void RequireBoneArray(float[] array, int bones, int perBone, string name)
    {
        if (array.Length < (long)bones * perBone)
            throw new ArgumentException($"{name} has {array.Length} floats but {bones} bones need {(long)bones * perBone}", name);
    }

    public int CreateClip(ClipData clip)
    {
        foreach (ClipChannelData c in clip.Channels)  // 값 배열은 키 수 x (회전 4 / 이동·스케일 3)개여야 한다. 네이티브는 길이를 모른다.
        {
            long needed = (long)c.Times.Length * (c.Path == ChannelPath.Rotation ? 4 : 3);
            if (c.Values.Length < needed)
                throw new ArgumentException($"clip '{clip.Name}': channel for bone {c.Bone} has {c.Values.Length} values but {c.Times.Length} keys need {needed}", nameof(clip));
        }
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

    /// <summary>오브젝트 불투명도 0..1. 1 = 불투명(기본), 0 = 그리지 않음, 사이 = 알파 블렌딩.</summary>
    public void SetObjectAlpha(int objectId, float alpha) => Check(Native.gge_object_set_alpha(Handle, objectId, alpha));

    /// <summary>SetShadow가 켜져 있을 때 이 오브젝트가 바닥 평면에 그림자를 드리울지.</summary>
    public void SetObjectCastsShadow(int objectId, bool casts) =>
        Check(Native.gge_object_set_casts_shadow(Handle, objectId, casts ? 1 : 0));

    /// <summary>
    /// 표시 여부(처음엔 보임). 숨기면 이 오브젝트와 모든 자손이 그려지지 않고 그림자도 드리우지 않으며 반투명 정렬에서도 빠진다.
    /// 알파/그림자 설정과는 별개 상태라 다시 보이게 하면 그대로 돌아온다. 점광원은 영향받지 않는다.
    /// </summary>
    public void SetObjectVisible(int objectId, bool visible) =>
        Check(Native.gge_object_set_visible(Handle, objectId, visible ? 1 : 0));

    /// <summary>그 오브젝트 <b>자신의</b> 표시 플래그(부모가 숨겨졌는지는 보지 않는다). 알 수 없는 ID면 <see cref="EngineException"/>.</summary>
    public bool IsObjectVisible(int objectId)
    {
        int result = Native.gge_object_is_visible(Handle, objectId);
        if (result < 0)
            throw new EngineException(Native.LastError());
        return result == 1;
    }

    // ---- 조명 (규약은 gge.h) ----
    public void SetAmbient(Vec3 rgb) => Check(Native.gge_renderer_set_ambient(Handle, rgb.X, rgb.Y, rgb.Z));

    /// <param name="direction">빛이 나아가는 방향(0이 아닌 벡터, 엔진이 정규화)</param>
    public void SetDirectionalLight(Vec3 direction, Vec3 color)
    {
        float* d = stackalloc float[3] { direction.X, direction.Y, direction.Z };
        float* c = stackalloc float[3] { color.X, color.Y, color.Z };
        Check(Native.gge_renderer_set_directional_light(Handle, d, c));
    }

    /// <summary>점광원을 만든다(렌더러당 최대 8개). 돌려주는 ID는 SetPointLight/DestroyPointLight에 쓴다.</summary>
    public int CreatePointLight(Vec3 position, Vec3 color, float range)
    {
        float* p = stackalloc float[3] { position.X, position.Y, position.Z };
        float* c = stackalloc float[3] { color.X, color.Y, color.Z };
        return CheckId(Native.gge_light_create_point(Handle, p, c, range));
    }

    public void SetPointLight(int lightId, Vec3 position, Vec3 color, float range)
    {
        float* p = stackalloc float[3] { position.X, position.Y, position.Z };
        float* c = stackalloc float[3] { color.X, color.Y, color.Z };
        Check(Native.gge_light_set_point(Handle, lightId, p, c, range));
    }

    public void DestroyPointLight(int lightId) => Check(Native.gge_light_destroy(Handle, lightId));

    /// <summary>평면 투영 그림자: 방향광을 y = planeY 평면에 투영해 opacity(0..1)만큼 어둡게 칠한다.</summary>
    public void SetShadow(bool enabled, float planeY, float opacity) =>
        Check(Native.gge_renderer_set_shadow(Handle, enabled ? 1 : 0, planeY, opacity));

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
            if (_owned)
                Native.gge_renderer_destroy(_handle);
            _handle = 0;
        }
    }

    /// <summary>
    /// 이미 만들어진 네이티브 렌더러(독립 플레이어가 호스팅한 .NET 스크립트가 받은 핸들)를 <b>빌려 쓴다</b>.
    /// Dispose해도 네이티브 렌더러는 해제되지 않는다. 크기는 만든 쪽이 알려 준 값이다.
    /// </summary>
    public static EngineRenderer Attach(nint rendererHandle, int width, int height) =>
        rendererHandle != 0 ? new EngineRenderer(rendererHandle, width, height) : throw new ArgumentException("null renderer handle", nameof(rendererHandle));

    private EngineRenderer(nint handle, int width, int height)
    {
        _handle = handle;
        _owned = false;
        Width = width;
        Height = height;
    }

    private nint Handle => _handle != 0 ? _handle : throw new ObjectDisposedException(nameof(EngineRenderer));

    /// <summary>네이티브 렌더러 핸들(EngineScene이 쓰고, <see cref="Attach"/>와 짝이다). 해제된 렌더러면 ObjectDisposedException.</summary>
    public nint NativeHandle => Handle;

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
        public static extern int gge_mesh_destroy(nint renderer, int meshId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_texture_destroy(nint renderer, int textureId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_skeleton_destroy(nint renderer, int skeletonId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_clip_destroy(nint renderer, int clipId);

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
        public static extern int gge_object_set_alpha(nint renderer, int objectId, float alpha);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_casts_shadow(nint renderer, int objectId, int casts);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_set_visible(nint renderer, int objectId, int visible);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_object_is_visible(nint renderer, int objectId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_set_ambient(nint renderer, float r, float g, float b);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_set_directional_light(nint renderer, float* direction, float* color);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_light_create_point(nint renderer, float* position, float* color, float range);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_light_set_point(nint renderer, int lightId, float* position, float* color, float range);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_light_destroy(nint renderer, int lightId);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_set_shadow(nint renderer, int enabled, float planeY, float opacity);

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
