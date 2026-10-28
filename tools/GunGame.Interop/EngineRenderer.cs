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

    private static class Native
    {
        private const string Dll = "gge";

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
