using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using GunGame.Interop;
using Microsoft.Win32;

namespace GunGameEditor;

public partial class MainWindow : Window
{
    private const int RenderWidth = 320;
    private const int RenderHeight = 240;
    private const float FovY = 0.8f;

    private readonly OrbitCamera _camera = new();
    private readonly WriteableBitmap _bitmap = new(RenderWidth, RenderHeight, 96, 96, PixelFormats.Bgra32, null);
    private EngineRenderer? _renderer;
    private Point? _lastMouse;

    public MainWindow()
    {
        InitializeComponent();
        Viewport.Source = _bitmap;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            _renderer = new EngineRenderer(RenderWidth, RenderHeight, useWarp: SelfTest.IsRequested);
            Redraw();
        }
        catch (Exception ex)
        {
            StatusText.Text = "Engine init failed: " + ex.Message;
            SelfTest.Fail(this, ex.Message);
            return;
        }

        if (SelfTest.IsRequested)
            Dispatcher.InvokeAsync(() => SelfTest.Run(this), System.Windows.Threading.DispatcherPriority.ApplicationIdle);
    }

    private void OnClosed(object? sender, EventArgs e) => _renderer?.Dispose();

    private void OnOpenObj(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "Wavefront OBJ (*.obj)|*.obj" };
        if (dialog.ShowDialog(this) == true)
            LoadObj(dialog.FileName);
    }

    internal bool LoadObj(string path)
    {
        if (_renderer is null) return false;
        try
        {
            _renderer.LoadObj(path);
            StatusText.Text = Path.GetFileName(path);
            Redraw();
            return true;
        }
        catch (EngineException ex)
        {
            StatusText.Text = "Load failed: " + ex.Message;
            return false;
        }
    }

    internal void Redraw()
    {
        if (_renderer is null) return;
        _renderer.SetCamera(_camera.Eye, _camera.Target, FovY);
        _renderer.Render();
        unsafe
        {
            fixed (byte* p = _renderer.Pixels)
                _bitmap.WritePixels(new Int32Rect(0, 0, RenderWidth, RenderHeight), (nint)p, _renderer.Stride * RenderHeight, _renderer.Stride);
        }
    }

    internal BitmapSource CaptureViewport()
    {
        var target = new RenderTargetBitmap(RenderWidth, RenderHeight, 96, 96, PixelFormats.Pbgra32);
        var visual = new DrawingVisual();
        using (var dc = visual.RenderOpen())
            dc.DrawImage(_bitmap, new Rect(0, 0, RenderWidth, RenderHeight));
        target.Render(visual);
        return target;
    }

    internal OrbitCamera Camera => _camera;

    private void OnMouseDown(object sender, MouseButtonEventArgs e)
    {
        _lastMouse = e.GetPosition(Viewport);
        Viewport.CaptureMouse();
    }

    private void OnMouseUp(object sender, MouseButtonEventArgs e)
    {
        _lastMouse = null;
        Viewport.ReleaseMouseCapture();
    }

    private void OnMouseMove(object sender, MouseEventArgs e)
    {
        if (_lastMouse is not { } last) return;
        Point now = e.GetPosition(Viewport);
        _camera.Rotate(-(float)(now.X - last.X) * 0.01f, (float)(now.Y - last.Y) * 0.01f);
        _lastMouse = now;
        Redraw();
    }

    private void OnMouseWheel(object sender, MouseWheelEventArgs e)
    {
        _camera.Zoom(e.Delta > 0 ? 0.9f : 1.1f);
        Redraw();
    }
}
