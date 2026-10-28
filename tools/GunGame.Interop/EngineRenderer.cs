using System.Runtime.InteropServices;

namespace GunGame.Interop;

public sealed class EngineException(string message) : Exception(message);

/// <summary>엔진 C API(gge.dll)의 얇은 래퍼. 프레임버퍼 소유권은 엔진에 있다.</summary>
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

    public void LoadObj(string path) => Check(Native.gge_renderer_load_obj(Handle, path));

    public void SetMesh(ReadOnlySpan<float> vertices, ReadOnlySpan<uint> indices)
    {
        fixed (float* v = vertices)
        fixed (uint* i = indices)
            Check(Native.gge_renderer_set_mesh(Handle, v, vertices.Length / 6, i, indices.Length));
    }

    public void SetCamera(Vec3 eye, Vec3 target, float fovYRadians) =>
        Native.gge_renderer_set_camera(Handle, eye.X, eye.Y, eye.Z, target.X, target.Y, target.Z, fovYRadians);

    public void SetColor(float r, float g, float b) => Native.gge_renderer_set_color(Handle, r, g, b);

    /// <summary>PS1풍 효과. colorBits는 채널당 1..8비트(8이면 양자화 없음).</summary>
    public void SetRetro(bool vertexSnap, int colorBits, bool dither) =>
        Check(Native.gge_renderer_set_retro(Handle, vertexSnap ? 1 : 0, colorBits, dither ? 1 : 0));

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

    private static class Native
    {
        private const string Dll = "gge";

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint gge_renderer_create(int width, int height, int useWarp);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_renderer_destroy(nint renderer);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_set_mesh(nint renderer, float* vertices, int vertexCount, uint* indices, int indexCount);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_load_obj(nint renderer, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_renderer_set_camera(nint renderer, float ex, float ey, float ez, float tx, float ty, float tz, float fovY);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_renderer_set_color(nint renderer, float r, float g, float b);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_set_retro(nint renderer, int vertexSnap, int colorBits, int dither);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_renderer_render(nint renderer);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern byte* gge_renderer_get_pixels(nint renderer, out int width, out int height, out int stride);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        private static extern nint gge_last_error();

        public static string LastError() => Marshal.PtrToStringUTF8(gge_last_error()) ?? "unknown engine error";
    }
}
