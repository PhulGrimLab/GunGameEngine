using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using GunGame.Interop;
using GunGame.Scene;
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
    private SceneSync? _sync;
    private SceneDocument _document = new();
    private string? _scenePath;
    private Point? _lastMouse;
    private Point _dragStart;
    private SceneNode? _dragNode;
    private readonly DispatcherTimer _animationTimer = new(DispatcherPriority.Render) { Interval = TimeSpan.FromMilliseconds(33) };
    private long _lastTick;

    public MainWindow()
    {
        InitializeComponent();
        Viewport.Source = _bitmap;
        Hierarchy.ItemsSource = _document.Roots;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            _renderer = new EngineRenderer(RenderWidth, RenderHeight, useWarp: SelfTest.IsRequested);
            ApplyRetro();
            _sync = new SceneSync(_renderer);
            _sync.Changed += OnSceneChanged;
            Inspector.ClipNamesProvider = _sync.GetClipNames;
            _sync.Attach(_document);
        }
        catch (Exception ex)
        {
            StatusText.Text = "Engine init failed: " + ex.Message;
            SelfTest.Fail(this, ex.Message);
            return;
        }

        if (SelfTest.IsRequested)
        {
            Dispatcher.InvokeAsync(() => SelfTest.Run(this), System.Windows.Threading.DispatcherPriority.ApplicationIdle);
            return;  // 자동 검증은 시간을 직접 진행시켜 결과를 결정적으로 유지한다
        }

        _lastTick = Stopwatch.GetTimestamp();
        _animationTimer.Tick += (_, _) =>
        {
            long now = Stopwatch.GetTimestamp();
            float dt = (float)Stopwatch.GetElapsedTime(_lastTick, now).TotalSeconds;
            _lastTick = now;
            _sync?.Tick(Math.Min(dt, 0.1f));  // 창이 멈췄다 돌아올 때 애니메이션이 튀지 않게 상한을 둔다
        };
        _animationTimer.Start();
    }

    /// <summary>애니메이션을 dt초만큼 진행시킨다(타이머와 selftest 공용).</summary>
    internal void AdvanceAnimation(float dt) => _sync?.Tick(dt);

    private void OnClosed(object? sender, EventArgs e)
    {
        _animationTimer.Stop();
        _sync?.Dispose();
        _renderer?.Dispose();
    }

    // ---- 씬 파일 ----
    private void OnNewScene(object sender, RoutedEventArgs e) => SetDocument(new SceneDocument(), null);

    private void OnOpenScene(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "Scene (*.json)|*.json" };
        if (dialog.ShowDialog(this) == true)
            OpenScenePath(dialog.FileName);
    }

    internal bool OpenScenePath(string path)
    {
        try
        {
            SetDocument(SceneSerializer.Load(path), path);
            return true;
        }
        catch (Exception ex) when (ex is SceneFormatException or IOException or UnauthorizedAccessException)
        {
            StatusText.Text = "Open failed: " + ex.Message;
            return false;
        }
    }

    private void OnSaveScene(object sender, RoutedEventArgs e)
    {
        if (_scenePath is null)
            OnSaveSceneAs(sender, e);
        else
            SaveTo(_scenePath);
    }

    private void OnSaveSceneAs(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog { Filter = "Scene (*.json)|*.json", DefaultExt = ".json" };
        if (dialog.ShowDialog(this) == true)
            SaveTo(dialog.FileName);
    }

    private void SaveTo(string path)
    {
        try
        {
            SceneSerializer.Save(_document, path);
            _scenePath = path;
            UpdateTitle();
            StatusText.Text = "Saved " + path;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            StatusText.Text = "Save failed: " + ex.Message;
        }
    }

    private void SetDocument(SceneDocument document, string? path)
    {
        _document = document;
        _scenePath = path;
        Hierarchy.ItemsSource = document.Roots;
        Inspector.Show(null);
        _sync?.Attach(document);
        UpdateTitle();
        if (path is not null && _sync?.Errors.Count == 0)
            StatusText.Text = "Opened " + path;
    }

    private void UpdateTitle() =>
        Title = "GunGame Editor - " + (_scenePath is null ? "(unsaved scene)" : Path.GetFileName(_scenePath));

    // ---- 노드 편집 ----
    private SceneNode? Selected => Hierarchy.SelectedItem as SceneNode;

    private void OnAddMesh(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "Mesh (*.obj;*.ggm;*.glb;*.gltf)|*.obj;*.ggm;*.glb;*.gltf" };
        if (dialog.ShowDialog(this) == true)
            AddMesh(dialog.FileName);
    }

    internal void AddMesh(string path)
    {
        var node = new SceneNode { Name = Path.GetFileNameWithoutExtension(path), MeshPath = path };
        _document.Add(node, Selected);
        SelectNode(node);
    }

    private void OnAddGroup(object sender, RoutedEventArgs e)
    {
        var node = new SceneNode { Name = "Group" };
        _document.Add(node, Selected);
        SelectNode(node);
    }

    private void OnDelete(object sender, RoutedEventArgs e)
    {
        if (Selected is not { } node)
            return;
        Inspector.Show(null);
        _document.Remove(node);
    }

    private void OnConvertObj(object sender, RoutedEventArgs e)
    {
        var open = new OpenFileDialog { Filter = "Wavefront OBJ (*.obj)|*.obj" };
        if (open.ShowDialog(this) != true)
            return;
        var save = new SaveFileDialog
        {
            Filter = "GGM mesh (*.ggm)|*.ggm",
            DefaultExt = ".ggm",
            FileName = Path.GetFileNameWithoutExtension(open.FileName) + ".ggm",
        };
        if (save.ShowDialog(this) != true)
            return;
        try
        {
            EngineRenderer.ConvertObjToGgm(open.FileName, save.FileName);
            StatusText.Text = "Converted to " + save.FileName;
        }
        catch (EngineException ex)
        {
            StatusText.Text = "Convert failed: " + ex.Message;
        }
    }

    private void OnSelectionChanged(object sender, RoutedPropertyChangedEventArgs<object> e) => Inspector.Show(Selected);

    private void SelectNode(SceneNode node)
    {
        var path = new Stack<SceneNode>();
        for (SceneNode? n = node; n is not null; n = n.Parent)
            path.Push(n);

        ItemsControl container = Hierarchy;
        foreach (SceneNode n in path)
        {
            container.UpdateLayout();
            if (container.ItemContainerGenerator.ContainerFromItem(n) is not TreeViewItem item)
                return;
            if (n == node)
            {
                item.IsSelected = true;
                item.BringIntoView();
            }
            else
            {
                item.IsExpanded = true;
                container = item;
            }
        }
    }

    // ---- 계층 드래그 앤 드롭 ----
    private static SceneNode? NodeAt(object? source)
    {
        DependencyObject? current = source as DependencyObject;
        while (current is not null)
        {
            if (current is TreeViewItem item)
                return item.DataContext as SceneNode;
            current = current is Visual or System.Windows.Media.Media3D.Visual3D
                ? VisualTreeHelper.GetParent(current)
                : LogicalTreeHelper.GetParent(current);
        }
        return null;
    }

    private void OnTreeMouseDown(object sender, MouseButtonEventArgs e)
    {
        _dragStart = e.GetPosition(null);
        _dragNode = NodeAt(e.OriginalSource);
    }

    private void OnTreeMouseMove(object sender, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed || _dragNode is null)
            return;
        Vector delta = e.GetPosition(null) - _dragStart;
        if (Math.Abs(delta.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(delta.Y) < SystemParameters.MinimumVerticalDragDistance)
            return;
        SceneNode node = _dragNode;
        _dragNode = null;
        DragDrop.DoDragDrop(Hierarchy, new DataObject(typeof(SceneNode), node), DragDropEffects.Move);
    }

    private void OnTreeDragOver(object sender, DragEventArgs e)
    {
        e.Effects = e.Data.GetDataPresent(typeof(SceneNode)) ? DragDropEffects.Move : DragDropEffects.None;
        e.Handled = true;
    }

    private void OnTreeDrop(object sender, DragEventArgs e)
    {
        if (e.Data.GetData(typeof(SceneNode)) is not SceneNode node)
            return;
        try
        {
            _document.Move(node, NodeAt(e.OriginalSource));  // 빈 곳에 놓으면 루트로
            SelectNode(node);
        }
        catch (InvalidOperationException ex)
        {
            StatusText.Text = "Move failed: " + ex.Message;
        }
    }

    // ---- 렌더 ----
    private void OnRetroChanged(object sender, RoutedEventArgs e)
    {
        ApplyRetro();
        Redraw();
    }

    private void ApplyRetro() =>
        _renderer?.SetRetro(SnapCheck.IsChecked == true, BitsCheck.IsChecked == true ? 5 : 8,
                            DitherCheck.IsChecked == true, AffineCheck.IsChecked == true);

    private void OnSceneChanged()
    {
        if (_sync is { Errors.Count: > 0 } sync)
            StatusText.Text = "Load problem: " + sync.Errors[0];
        else if (_sync is { Warnings.Count: > 0 } withWarnings)
            StatusText.Text = "Warning: " + withWarnings.Warnings[0];
        Inspector.UpdateClips();  // 메시가 바뀌면 클립 목록도 바뀐다
        Redraw();
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

    internal BitmapSource CaptureWindow()
    {
        var content = (FrameworkElement)Content;
        var target = new RenderTargetBitmap((int)content.ActualWidth, (int)content.ActualHeight, 96, 96, PixelFormats.Pbgra32);
        target.Render(content);
        return target;
    }

    /// <summary>애니메이션 클립이 있는 첫 노드를(없으면 첫 노드를) 선택한다.</summary>
    internal void SelectFirstNode()
    {
        SceneNode? node = _document.Nodes().FirstOrDefault(n => _sync?.GetClipNames(n).Count > 0) ?? _document.Roots.FirstOrDefault();
        if (node is not null)
            SelectNode(node);
    }

    internal OrbitCamera Camera => _camera;

    internal IReadOnlyList<string> SceneErrors => _sync?.Errors ?? [];

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
