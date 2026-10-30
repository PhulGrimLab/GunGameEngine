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
    private GameplayOverlay? _overlay;
    private PlaySession? _play;
    private readonly List<string> _playLog = [];
    private bool _rebuildingGameplayList;

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
            _overlay = new GameplayOverlay(_renderer);
            _sync.Changed += OnSceneChanged;
            _sync.PoseChanged += Redraw;  // 애니메이션 틱은 다시 그리기만 한다(상태 표시줄/클립 목록은 건드리지 않는다)
            Inspector.ClipNamesProvider = _sync.GetClipNames;
            GameplayInspectorPanel.ViewCameraProvider = () => (_camera.Eye, _camera.Target, FovY);
            GameplayInspectorPanel.Changed += OnGameplayEdited;
            GameplayInspectorPanel.Rejected += reason => StatusText.Text = "Rejected: " + reason;
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
            float step = Math.Min(dt, 0.1f);
            if (_play is not null)
            {
                (float inputX, float inputY) = ReadPlayInput();
                StepPlay(inputX, inputY, step);
            }
            _sync?.Tick(step);  // 창이 멈췄다 돌아올 때 애니메이션이 튀지 않게 상한을 둔다
        };
        _animationTimer.Start();
    }

    /// <summary>애니메이션을 dt초만큼 진행시킨다(타이머와 selftest 공용).</summary>
    internal void AdvanceAnimation(float dt) => _sync?.Tick(dt);

    private void OnClosed(object? sender, EventArgs e)
    {
        _animationTimer.Stop();
        _play?.Dispose();
        _overlay?.Dispose();  // 렌더러보다 먼저 정리한다
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
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or SceneFormatException)
        {
            StatusText.Text = "Save failed: " + ex.Message;
        }
    }

    private void OnBakeScene(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog
        {
            Filter = "Baked scene (*.ggs)|*.ggs",
            DefaultExt = ".ggs",
            FileName = (_scenePath is null ? "scene" : Path.GetFileNameWithoutExtension(_scenePath)) + ".ggs",
        };
        if (dialog.ShowDialog(this) == true)
            BakeTo(dialog.FileName);
    }

    /// <summary>
    /// 씬을 독립 실행 플레이어(gge_player.exe)용 바이너리 씬(.ggs)으로 굽는다. 씬에 로드 문제가 있으면(없는 메시/텍스처 등)
    /// 문제가 있는 채로 굽지 않고 거부한다. 구운 결과는 파일에 쓰기 전에 엔진으로 다시 로드해 검증한다(SceneBaker).
    /// </summary>
    internal bool BakeTo(string path)
    {
        if (_sync is { Errors.Count: > 0 } sync)
        {
            StatusText.Text = "Bake refused: the scene has load problems: " + sync.Errors[0];
            return false;
        }
        try
        {
            SceneBaker.Bake(_document, path);
            StatusText.Text = $"Baked {path} (run it with gge_player.exe)";
            return true;
        }
        catch (BakeException ex)
        {
            StatusText.Text = "Bake failed: " + ex.Message;
            return false;
        }
    }

    private void SetDocument(SceneDocument document, string? path)
    {
        StopPlay();  // 플레이 중에 씬이 바뀌면 이전 씬의 세션을 끝낸다
        _document = document;
        _scenePath = path;
        Hierarchy.ItemsSource = document.Roots;
        Inspector.Show(null);
        GameplayInspectorPanel.Show(null);
        _sync?.Attach(document);
        RefreshGameplayList();
        UpdateOverlay();
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

    private void OnSelectionChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (Selected is not null)
            GameplayList.SelectedItem = null;  // 한 번에 하나의 인스펙터만 보인다(노드 또는 게임플레이 항목)
        Inspector.Show(Selected);
        ShowActiveInspector();
    }

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
        if (_dragNode is not null)
            GameplayList.SelectedItem = null;  // 이미 선택된 노드를 다시 눌러도 노드 인스펙터로 돌아온다
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

    // ---- 게임플레이 편집 ----
    internal GameplayItem? SelectedGameplayItem => GameplayList.SelectedItem as GameplayItem;

    /// <summary>게임플레이 목록을 문서의 현재 데이터로 다시 채운다. keep이 목록에 있으면 그 항목을 다시 선택한다.
    /// reshowInspector가 false면 인스펙터는 그대로 둔다(편집 중인 입력 칸을 날리지 않으려고, 값 편집 직후에 쓴다).</summary>
    private void RefreshGameplayList(object? keep = null, bool reshowInspector = true)
    {
        GameplayData data = _document.Gameplay;
        var items = new List<GameplayItem> { new(GameplayKind.Lighting, _document.Lighting) };
        items.AddRange(_document.Lighting.PointLights.Select(p => new GameplayItem(GameplayKind.PointLight, p)));
        items.AddRange(data.Rooms.Select(r => new GameplayItem(GameplayKind.Room, r)));
        items.AddRange(data.Colliders.Select(c => new GameplayItem(GameplayKind.Collider, c)));
        items.AddRange(data.Triggers.Select(t => new GameplayItem(GameplayKind.Trigger, t)));
        if (data.Player is { } player)
            items.Add(new GameplayItem(GameplayKind.Player, player));

        _rebuildingGameplayList = true;  // 다시 채우는 동안의 선택 해제 이벤트는 무시한다(인스펙터 입력 칸을 날리지 않게)
        GameplayList.ItemsSource = items;
        GameplayList.SelectedItem = keep is null ? null : items.FirstOrDefault(i => ReferenceEquals(i.Def, keep));
        _rebuildingGameplayList = false;
        if (reshowInspector)
        {
            GameplayInspectorPanel.Show(SelectedGameplayItem);
            ShowActiveInspector();
        }
    }

    private void OnGameplaySelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rebuildingGameplayList)
            return;
        GameplayInspectorPanel.Show(SelectedGameplayItem);
        ShowActiveInspector();
    }

    /// <summary>게임플레이 항목이 선택되어 있으면 그 인스펙터를, 아니면 노드 인스펙터를 보인다.</summary>
    private void ShowActiveInspector()
    {
        bool gameplay = SelectedGameplayItem is not null;
        GameplayInspectorPanel.Visibility = gameplay ? Visibility.Visible : Visibility.Collapsed;
        Inspector.Visibility = gameplay ? Visibility.Collapsed : Visibility.Visible;
    }

    /// <summary>인스펙터에서 값이 바뀌었다: 목록 이름과 오버레이를 갱신한다.</summary>
    private void OnGameplayEdited()
    {
        RefreshGameplayList(SelectedGameplayItem?.Def, reshowInspector: false);
        UpdateOverlay();
        if (SelectedGameplayItem is { Kind: GameplayKind.Lighting or GameplayKind.PointLight })
            ApplyLighting();
    }

    /// <summary>문서의 조명을 엔진에 반영하고 다시 그린다. 엔진이 거부한 값이 있으면 상태 표시줄에 사유를 보인다.</summary>
    private void ApplyLighting()
    {
        if (_sync is null)
            return;
        int before = _sync.Errors.Count;
        _sync.ApplyLighting();
        if (_sync.Errors.Count > before)
            StatusText.Text = "Lighting problem: " + _sync.Errors[^1];
        Redraw();
    }

    /// <summary>"Add Light": 편집 카메라가 보는 점(목표점) 위 2 단위에 점광원을 추가한다.</summary>
    private void OnAddLight(object sender, RoutedEventArgs e)
    {
        LightingData lighting = _document.Lighting;
        if (lighting.PointLights.Count >= LightingData.MaxPointLights)
        {
            StatusText.Text = $"At most {LightingData.MaxPointLights} point lights are supported";
            return;
        }
        Vec3 target = _camera.Target;
        var light = new PointLightDef
        {
            Name = GameplayEditing.UniqueName(lighting.PointLights.Select(p => p.Name), "Light"),
            Position = new Vec3(target.X, target.Y + 2f, target.Z),
        };
        lighting.PointLights.Add(light);
        ApplyLighting();
        AddGameplayItem(GameplayKind.PointLight, light);
    }

    private void OnAddRoom(object sender, RoutedEventArgs e) =>
        AddGameplayItem(GameplayKind.Room, GameplayEditing.AddRoom(_document.Gameplay, _camera.Eye, _camera.Target, FovY));

    private void OnAddCollider(object sender, RoutedEventArgs e) =>
        AddGameplayItem(GameplayKind.Collider, GameplayEditing.AddCollider(_document.Gameplay, _camera.Target.X, _camera.Target.Z));

    private void OnAddTrigger(object sender, RoutedEventArgs e) =>
        AddGameplayItem(GameplayKind.Trigger, GameplayEditing.AddTrigger(_document.Gameplay, _camera.Target.X, _camera.Target.Z));

    /// <summary>"Set Player": 계층에서 고른 루트 노드를 플레이어로 지정하고 스폰을 그 위치로 둔다.</summary>
    private void OnSetPlayer(object sender, RoutedEventArgs e)
    {
        if (Selected is not { } node)
        {
            StatusText.Text = "Select a root node in the hierarchy first";
            return;
        }
        try
        {
            GameplayEditing.SetPlayer(_document.Gameplay, node);
        }
        catch (InvalidOperationException ex)
        {
            StatusText.Text = "Set player failed: " + ex.Message;
            return;
        }
        AddGameplayItem(GameplayKind.Player, _document.Gameplay.Player!);
    }

    /// <summary>방금 만든(또는 지정한) 항목을 목록에서 고르고, 오버레이를 켜서 바로 보이게 한다.</summary>
    private void AddGameplayItem(GameplayKind kind, object def)
    {
        RefreshGameplayList(def);
        if (kind != GameplayKind.PointLight)  // 점광원은 오버레이가 그리지 않는다(영역 표시와 무관)
            GameplayCheck.IsChecked = true;
        UpdateOverlay();
        StatusText.Text = $"Added {kind}: edit values in the inspector";
    }

    private void OnDeleteGameplayItem(object sender, RoutedEventArgs e)
    {
        if (SelectedGameplayItem is not { } item)
        {
            StatusText.Text = "Select a gameplay item to delete";
            return;
        }
        GameplayData data = _document.Gameplay;
        switch (item.Kind)
        {
            case GameplayKind.Room:
                data.Rooms.Remove((RoomDef)item.Def);
                break;
            case GameplayKind.Collider:
                data.Colliders.Remove((ColliderDef)item.Def);
                break;
            case GameplayKind.Trigger:
                data.Triggers.Remove((TriggerDef)item.Def);
                break;
            case GameplayKind.PointLight:
                _document.Lighting.PointLights.Remove((PointLightDef)item.Def);
                ApplyLighting();
                break;
            case GameplayKind.Lighting:
                StatusText.Text = "The scene lighting cannot be deleted (edit its values instead)";
                return;
            default:
                data.Player = null;
                break;
        }
        RefreshGameplayList();
        UpdateOverlay();
    }

    // ---- 게임플레이 오버레이 ----
    private void OnGameplayOverlayChanged(object sender, RoutedEventArgs e) => UpdateOverlay();

    /// <summary>"Show gameplay"가 켜져 있으면 문서의 방/충돌체/트리거를 뷰포트에 표시하고, 아니면 지운다.</summary>
    private void UpdateOverlay()
    {
        if (_overlay is null)
            return;
        if (GameplayCheck.IsChecked == true)
            _overlay.Show(_document.Gameplay);
        else
            _overlay.Hide();
        Redraw();
    }

    internal bool GameplayOverlayVisible => _overlay?.Visible == true;

    // ---- 플레이 모드 ----
    internal bool IsPlaying => _play is not null;

    /// <summary>진행 중인 플레이 세션(테스트가 위치/활성 방을 확인하는 용도). 플레이 중이 아니면 null.</summary>
    internal PlaySession? CurrentPlay => _play;

    /// <summary>플레이 중 발생한 트리거 이벤트/카메라 전환 기록(시작할 때 비워진다).</summary>
    internal IReadOnlyList<string> PlayLog => _playLog;

    private void OnPlayToggle(object sender, RoutedEventArgs e)
    {
        if (_play is null)
            StartPlay();
        else
            StopPlay();
    }

    /// <summary>씬의 gameplay 데이터로 플레이를 시작한다. 시작할 수 없으면 사유를 상태 표시줄에 띄우고 false.</summary>
    internal bool StartPlay()
    {
        if (_play is not null || _sync is null)
            return false;
        try
        {
            _play = PlaySession.Start(_document);
        }
        catch (PlayException ex)
        {
            StatusText.Text = "Play failed: " + ex.Message;
            return false;
        }
        _playLog.Clear();
        SetEditingEnabled(false);
        PlayButton.Content = "Stop";
        ApplyPlayPose();
        StatusText.Text = $"Playing in '{_play.ActiveRoomName}': WASD / arrow keys to move";
        return true;
    }

    /// <summary>플레이를 끝내고 플레이어를 문서의 위치로 되돌리고 편집 카메라/UI를 복원한다.</summary>
    internal void StopPlay()
    {
        if (_play is null)
            return;
        SceneNode player = _play.PlayerNode;
        _play.Dispose();
        _play = null;
        _sync?.ClearPreview(player);  // 문서는 바뀐 적이 없으므로 화면만 원래 값으로 되돌리면 된다
        SetEditingEnabled(true);
        PlayButton.Content = "Play";
        StatusText.Text = "Stopped";
        Redraw();
    }

    /// <summary>한 프레임을 진행한다(타이머가 키보드 입력으로 부르고, 테스트는 직접 부른다).</summary>
    internal void StepPlay(float inputX, float inputY, float dt)
    {
        if (_play is null)
            return;
        try
        {
            string roomBefore = _play.ActiveRoomName;
            foreach (PlayEvent e in _play.Step(inputX, inputY, dt))
                Log($"{(e.Kind == TriggerEventKind.Enter ? "Enter" : "Exit")} '{e.TriggerName}' (tag {e.Tag})");
            if (_play.ActiveRoomName != roomBefore)
                Log($"Camera: room '{_play.ActiveRoomName}'");
            ApplyPlayPose();
        }
        catch (EngineException ex)
        {
            StatusText.Text = "Play error: " + ex.Message;
            StopPlay();
        }
    }

    private void Log(string line)
    {
        _playLog.Add(line);
        StatusText.Text = line;
    }

    private void ApplyPlayPose()
    {
        if (_play is { } play)
            _sync?.Preview(play.PlayerNode, play.PlayerPosition, play.PlayerRotation);  // PoseChanged -> 다시 그리기
    }

    /// <summary>WASD/방향키를 입력 축으로 읽는다(오른쪽+, 화면 위쪽+). 키 상태는 창이 활성일 때만 갱신된다.</summary>
    private static (float X, float Y) ReadPlayInput() => PlayInputFromKeys(Keyboard.IsKeyDown);

    /// <summary>
    /// 키 상태 조회 함수로 입력 축을 만든다: D/오른쪽 = X+, A/왼쪽 = X-, W/위 = Y+, S/아래 = Y-.
    /// 반대 키를 같이 누르면 상쇄되어 0이다. 대각선은 (±1, ±1)로 두고 길이 제한은 엔진이 한다.
    /// 키 상태(OS 입력)와 분리해 두어 매핑을 자동 테스트할 수 있다.
    /// </summary>
    internal static (float X, float Y) PlayInputFromKeys(Func<Key, bool> isDown)
    {
        float Axis(Key positive, Key positiveAlt, Key negative, Key negativeAlt) =>
            (isDown(positive) || isDown(positiveAlt) ? 1f : 0f) -
            (isDown(negative) || isDown(negativeAlt) ? 1f : 0f);
        return (Axis(Key.D, Key.Right, Key.A, Key.Left), Axis(Key.W, Key.Up, Key.S, Key.Down));
    }

    /// <summary>플레이 중에는 씬 편집(툴바, 계층, 인스펙터)을 막는다: 노드가 바뀌거나 사라지면 세션이 어긋난다.</summary>
    private void SetEditingEnabled(bool enabled)
    {
        FileBar.IsEnabled = enabled;
        NodeBar.IsEnabled = enabled;
        GameplayBar.IsEnabled = enabled;
        Hierarchy.IsEnabled = enabled;
        GameplayList.IsEnabled = enabled;
        if (enabled)
        {
            Inspector.Show(Selected);  // 선택 상태에 맞게 인스펙터 활성 여부를 되돌린다
            GameplayInspectorPanel.IsEnabled = SelectedGameplayItem is not null;
        }
        else
        {
            Inspector.IsEnabled = false;
            GameplayInspectorPanel.IsEnabled = false;
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
        if (_play is not null)
        {
            RoomCameraData room = _play.Camera;  // 플레이 중에는 활성 방의 고정 카메라
            _renderer.SetCamera(room.Eye, room.Target, room.FovY);
        }
        else
        {
            _renderer.SetCamera(_camera.Eye, _camera.Target, FovY);
        }
        _renderer.Render();
        unsafe
        {
            fixed (byte* p = _renderer.Pixels)
                _bitmap.WritePixels(new Int32Rect(0, 0, RenderWidth, RenderHeight), (nint)p, _renderer.Stride * RenderHeight, _renderer.Stride);
        }
    }

    /// <summary>
    /// 뷰포트 비트맵의 현재 픽셀을 복사한다(고정된 사본). RenderTargetBitmap으로 비트맵을 다시 그리면 WPF 렌더 스레드가 아직 반영하지
    /// 못한 이전 프레임의 일부가 섞여 찢어진 그림이 나올 수 있어(드물게 재현됨), UI 스레드가 쓴 값을 직접 복사한다.
    /// </summary>
    internal BitmapSource CaptureViewport()
    {
        var copy = new WriteableBitmap(_bitmap);
        copy.Freeze();
        return copy;
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
        if (_play is not null)
            return;  // 플레이 중에는 카메라가 방에 고정되어 있다
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
        if (_play is not null)
            return;
        if (_lastMouse is not { } last) return;
        Point now = e.GetPosition(Viewport);
        OrbitDrag(last, now);
        _lastMouse = now;
    }

    /// <summary>마우스를 from에서 to로 끌었을 때의 궤도 회전. 이벤트 핸들러와 분리해 두어(핸들러는 실제 커서 위치를 읽는다)
    /// 좌표만으로 테스트할 수 있다. 플레이 중에는 카메라가 방에 고정되어 있어 무시한다.</summary>
    internal void OrbitDrag(Point from, Point to)
    {
        if (_play is not null)
            return;
        _camera.Rotate(-(float)(to.X - from.X) * 0.01f, (float)(to.Y - from.Y) * 0.01f);
        Redraw();
    }

    private void OnMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (_play is not null)
            return;
        _camera.Zoom(e.Delta > 0 ? 0.9f : 1.1f);
        Redraw();
    }
}
