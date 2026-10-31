using System.Runtime.InteropServices;

namespace GunGame.Interop;

/// <summary>
/// 비트맵 폰트(BDF) 핸들(C API gge_font_*). 글자는 1비트 픽셀 그대로 렌더러의 화면 위 항목(<see cref="EngineRenderer.CreateUiText"/>)으로 그려진다.
/// 폰트는 공유 참조라서 이 핸들을 Dispose해도 그 폰트를 쓰는 화면 항목은 항목이 지워질 때까지 계속 그려진다.
/// </summary>
public sealed unsafe class EngineFont : IDisposable
{
    private nint _handle;
    private readonly bool _owned;

    private EngineFont(nint handle, bool owned)
    {
        _handle = handle;
        _owned = owned;
    }

    /// <summary>BDF 파일을 읽는다. 잘못된 파일/없는 파일은 사유가 담긴 <see cref="EngineException"/>.</summary>
    public static EngineFont Load(string path)
    {
        nint handle = Native.gge_font_load(path);
        return handle != 0 ? new EngineFont(handle, owned: true) : throw new EngineException(Native.LastError());
    }

    /// <summary>메모리의 BDF 텍스트를 읽는다(규칙은 <see cref="Load"/>와 같다).</summary>
    public static EngineFont LoadFromMemory(ReadOnlySpan<byte> bdf)
    {
        fixed (byte* p = bdf)
        {
            nint handle = Native.gge_font_load_memory(p, (nuint)bdf.Length);
            return handle != 0 ? new EngineFont(handle, owned: true) : throw new EngineException(Native.LastError());
        }
    }

    /// <summary>플레이어가 읽어 둔 폰트 핸들을 <b>빌려 쓴다</b>(Dispose해도 해제되지 않는다).</summary>
    public static EngineFont Attach(nint handle) =>
        handle != 0 ? new EngineFont(handle, owned: false) : throw new ArgumentException("null font handle", nameof(handle));

    /// <summary>줄 높이 = Ascent + Descent(픽셀).</summary>
    public (int Ascent, int Descent) Metrics
    {
        get
        {
            if (Native.gge_font_get_metrics(Handle, out int ascent, out int descent) == 0)
                throw new EngineException(Native.LastError());
            return (ascent, descent);
        }
    }

    public bool HasGlyph(int codePoint) => codePoint >= 0 && Native.gge_font_has_glyph(Handle, (uint)codePoint) == 1;

    /// <summary>
    /// 글자를 줄로 나눠 크기를 잰다(렌더러가 그리는 것과 같은 규칙: '\n' 줄바꿈, maxWidth &gt; 0이면 폭을 넘기 전에 공백/글자 경계에서 줄바꿈, 잘못된 글자/없는 글자는
    /// 대체 글리프). 1024 UTF-8 바이트를 넘으면 <see cref="EngineException"/>.
    /// </summary>
    public (int Width, int Height, int Lines) Measure(string text, int maxWidth = 0)
    {
        if (Native.gge_font_measure_text(Handle, text, maxWidth, out int width, out int height, out int lines) == 0)
            throw new EngineException(Native.LastError());
        return (width, height, lines);
    }

    /// <summary>네이티브 폰트 핸들. 해제된 폰트면 ObjectDisposedException.</summary>
    public nint NativeHandle => Handle;

    private nint Handle => _handle != 0 ? _handle : throw new ObjectDisposedException(nameof(EngineFont));

    public void Dispose()
    {
        if (_handle != 0)
        {
            if (_owned)
                Native.gge_font_destroy(_handle);
            _handle = 0;
        }
    }

    private static class Native
    {
        private const string Dll = "gge";

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint gge_font_load([MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint gge_font_load_memory(byte* data, nuint size);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void gge_font_destroy(nint font);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_font_get_metrics(nint font, out int ascent, out int descent);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_font_has_glyph(nint font, uint codePoint);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int gge_font_measure_text(nint font, [MarshalAs(UnmanagedType.LPUTF8Str)] string text, int maxWidth, out int width, out int height, out int lines);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        private static extern nint gge_last_error();

        public static string LastError() => Marshal.PtrToStringUTF8(gge_last_error()) ?? "unknown engine error";
    }
}
