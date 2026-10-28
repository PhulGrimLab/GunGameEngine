using System.IO;
using System.Windows;
using System.Windows.Media.Imaging;

namespace GunGameEditor;

/// <summary>
/// 화면 검증 하네스: GunGameEditor.exe --selftest &lt;model.obj&gt; &lt;out.png&gt;
/// OBJ를 불러 렌더하고 비트맵을 PNG로 저장한 뒤 종료한다(종료 코드 0=성공).
/// OS 화면 캡처 대신 WPF 비트맵을 직접 저장하므로 헤드리스 환경에서도 동작한다.
/// </summary>
internal static class SelfTest
{
    private static readonly string[] Args = Environment.GetCommandLineArgs();
    private static int Index => Array.IndexOf(Args, "--selftest");

    public static bool IsRequested => Index >= 0 && Args.Length > Index + 2;

    public static void Run(MainWindow window)
    {
        try
        {
            string obj = Args[Index + 1], output = Args[Index + 2];
            if (!window.LoadObj(obj)) { Fail(window, "load failed"); return; }

            window.Camera.Rotate(0.3f, 0.1f);  // 마우스 오빗과 같은 경로를 한 번 통과시킨다
            window.Redraw();

            BitmapSource image = window.CaptureViewport();
            using (var stream = File.Create(output))
            {
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(image));
                encoder.Save(stream);
            }
            Application.Current.Shutdown(0);
        }
        catch (Exception ex)
        {
            Fail(window, ex.Message);
        }
    }

    public static void Fail(Window window, string message)
    {
        if (!IsRequested) return;
        Console.Error.WriteLine("selftest failed: " + message);
        Application.Current.Shutdown(1);
    }
}
