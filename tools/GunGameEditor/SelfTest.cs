using System.IO;
using System.Windows;
using System.Windows.Media.Imaging;

namespace GunGameEditor;

/// <summary>
/// 화면 검증 하네스: GunGameEditor.exe --selftest &lt;scene.json|model.obj&gt; &lt;out.png&gt;
/// 씬(또는 OBJ 하나)을 불러 렌더하고 비트맵을 PNG로 저장한 뒤 종료한다(종료 코드 0=성공).
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
            string input = Args[Index + 1], output = Args[Index + 2];
            if (input.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
            {
                if (!window.OpenScenePath(input)) { Fail(window, "scene load failed"); return; }
            }
            else
            {
                window.AddMesh(input);
            }
            if (window.SceneErrors.Count > 0) { Fail(window, window.SceneErrors[0]); return; }

            window.Camera.Rotate(0.3f, 0.1f);  // 마우스 오빗과 같은 경로를 한 번 통과시킨다
            window.AdvanceAnimation(0.5f);      // 재생 중인 애니메이션 진행(타이머 대신 결정적으로)
            window.Redraw();

            SavePng(window.CaptureViewport(), output);

            // 계층 트리와 인스펙터까지 포함한 창 전체 (선택 노드의 속성이 채워진 상태)
            window.SelectFirstNode();
            window.UpdateLayout();
            SavePng(window.CaptureWindow(), Path.ChangeExtension(output, ".window.png"));
            Application.Current.Shutdown(0);
        }
        catch (Exception ex)
        {
            Fail(window, ex.Message);
        }
    }

    private static void SavePng(BitmapSource image, string path)
    {
        using var stream = File.Create(path);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(image));
        encoder.Save(stream);
    }

    public static void Fail(Window window, string message)
    {
        if (!IsRequested) return;
        Console.Error.WriteLine("selftest failed: " + message);
        Application.Current.Shutdown(1);
    }
}
