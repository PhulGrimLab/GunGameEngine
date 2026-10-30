using System.Collections.ObjectModel;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Automation.Provider;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using GunGame.Scene;

namespace GunGameEditor.UiTests;

/// <summary>
/// 에디터 창 하나를 띄워 사용자 입력을 흉내 낸다. OS 마우스/키보드는 쓰지 않으므로 테스트 중 사용자 입력과 충돌하지 않는다.
///  - 버튼/트리 선택: UI Automation 패턴(Invoke, SelectionItem) - 스크린 리더가 쓰는 경로와 같다. 체크박스는 OnClick 호출.
///  - 텍스트 입력: Text를 채우고 Enter 키다운 또는 LostFocus 라우티드 이벤트를 발생시킨다.
///  - 휠/드래그 앤 드롭: 라우티드 이벤트를 실제 요소에 발생시켜 XAML에 연결된 핸들러까지 통과시킨다.
/// </summary>
public sealed class EditorSession : IDisposable
{
    // 인스펙터 TextBox 순서(InspectorPanel 생성 순서): 이름, 위치xyz, 회전xyz, 크기xyz, 색xyz, 재생 속도
    public const int NameBox = 0, PositionBox = 1, RotationBox = 4, ScaleBox = 7, ColorBox = 10, SpeedBox = 13;

    public MainWindow Window { get; } = new()
    {
        ShowActivated = false,
        ShowInTaskbar = false,
        WindowStartupLocation = WindowStartupLocation.Manual,
        Left = 40,
        Top = 40,
    };

    public EditorSession()
    {
        Window.Show();
        Pump();
    }

    public TreeView Hierarchy => Window.Hierarchy;
    public InspectorPanel Inspector => Window.Inspector;
    public string Status => Window.StatusText.Text;
    public SceneNode? Selected => Hierarchy.SelectedItem as SceneNode;
    public ObservableCollection<SceneNode> Roots => (ObservableCollection<SceneNode>)Hierarchy.ItemsSource;

    /// <summary>대기 중인 레이아웃/렌더/Loaded 작업이 끝날 때까지 디스패처를 돌린다.</summary>
    public void Pump()
    {
        var frame = new DispatcherFrame();
        Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, () => frame.Continue = false);
        Dispatcher.PushFrame(frame);
        Window.UpdateLayout();
    }

    // ---- 요소 찾기 ----
    public static IEnumerable<T> FindAll<T>(DependencyObject root, Func<T, bool>? match = null) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(root); i++)
        {
            DependencyObject child = VisualTreeHelper.GetChild(root, i);
            if (child is T t && (match is null || match(t)))
                yield return t;
            foreach (T nested in FindAll(child, match))
                yield return nested;
        }
    }

    public T Find<T>(DependencyObject root, Func<T, bool>? match = null) where T : DependencyObject =>
        FindAll(root, match).FirstOrDefault() ?? throw new InvalidOperationException($"{typeof(T).Name} not found");

    public Button ButtonByLabel(string label) => Find<Button>(Window, b => b.Content as string == label);
    public CheckBox CheckByLabel(string label) => Find<CheckBox>(Window, c => c.Content as string == label);

    /// <summary>인스펙터의 TextBox들을 선언 순서대로 돌려준다.</summary>
    public List<TextBox> Boxes() => FindAll<TextBox>(Inspector).ToList();

    public TextBox Box(int index) => Boxes()[index];

    public List<TreeViewItem> TreeItems(ItemsControl? parent = null)
    {
        parent ??= Hierarchy;
        parent.UpdateLayout();
        var result = new List<TreeViewItem>();
        foreach (object data in parent.Items)
            if (parent.ItemContainerGenerator.ContainerFromItem(data) is TreeViewItem item)
            {
                result.Add(item);
                result.AddRange(TreeItems(item));
            }
        return result;
    }

    public TreeViewItem ItemOf(SceneNode node) => TreeItems().First(i => i.DataContext == node);

    // ---- 입력 ----
    public void Click(Button button)
    {
        Assert.True(button.IsEnabled, $"button '{button.Content}' is disabled");
        ((IInvokeProvider)Peer(button).GetPattern(PatternInterface.Invoke)!).Invoke();
        Pump();
    }

    public void Click(string label) => Click(ButtonByLabel(label));

    /// <summary>
    /// 체크박스를 클릭한다. UI Automation의 Toggle 패턴은 IsChecked만 바꾸고 Click 이벤트를 내지 않아
    /// (XAML의 Click 핸들러가 실행되지 않음) 사용자 클릭과 다르다. 실제 클릭이 부르는 ButtonBase.OnClick을 호출한다.
    /// </summary>
    public void Toggle(CheckBox box)
    {
        Assert.True(box.IsEnabled, $"checkbox '{box.Content}' is disabled");
        typeof(ButtonBase).GetMethod("OnClick", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(box, null);
        Pump();
    }

    /// <summary>계층 트리에서 항목을 클릭해 선택한다.</summary>
    public void Select(SceneNode node)
    {
        ((ISelectionItemProvider)Peer(ItemOf(node)).GetPattern(PatternInterface.SelectionItem)!).Select();
        Pump();
    }

    /// <summary>값을 입력하고 Enter를 친다.</summary>
    public void TypeEnter(TextBox box, string text)
    {
        box.Text = text;
        box.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(box)!, 0, Key.Enter) { RoutedEvent = Keyboard.KeyDownEvent });
        Pump();
    }

    /// <summary>값을 입력하고 다른 곳을 눌러 포커스를 잃게 한다.</summary>
    public void TypeBlur(TextBox box, string text)
    {
        box.Text = text;
        box.RaiseEvent(new RoutedEventArgs(UIElement.LostFocusEvent, box));
        Pump();
    }

    public void Wheel(UIElement target, int delta)
    {
        target.RaiseEvent(new MouseWheelEventArgs(Mouse.PrimaryDevice, Environment.TickCount, delta) { RoutedEvent = UIElement.MouseWheelEvent });
        Pump();
    }

    /// <summary>계층 트리에 노드를 끌어다 놓는다. target이 null이면 빈 영역에 놓는 것이다. 놓기 직전 DragOver의 효과를 돌려준다.</summary>
    public DragDropEffects Drop(SceneNode dragged, SceneNode? target)
    {
        UIElement over = target is null ? Hierarchy : ItemOf(target);
        var data = new DataObject(typeof(SceneNode), dragged);
        DragEventArgs dragOver = NewDragArgs(data, DragDrop.DragOverEvent);
        over.RaiseEvent(dragOver);
        over.RaiseEvent(NewDragArgs(data, DragDrop.DropEvent));
        Pump();
        return dragOver.Effects;
    }

    /// <summary>SceneNode가 아닌 데이터를 끌고 올 때의 DragOver 효과.</summary>
    public DragDropEffects DragOverForeignData()
    {
        DragEventArgs e = NewDragArgs(new DataObject(DataFormats.Text, "foreign"), DragDrop.DragOverEvent);
        Hierarchy.RaiseEvent(e);
        return e.Effects;
    }

    // DragEventArgs 생성자는 internal이다. 시그니처가 바뀌면 여기서 바로 실패하도록 인자 형태를 확인한다.
    private DragEventArgs NewDragArgs(IDataObject data, RoutedEvent routed)
    {
        ConstructorInfo ctor = typeof(DragEventArgs).GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic).Single();
        object?[] args = ctor.GetParameters().Select(p => p.ParameterType switch
        {
            var t when t == typeof(IDataObject) => (object?)data,
            var t when t == typeof(DragDropKeyStates) => DragDropKeyStates.LeftMouseButton,
            var t when t == typeof(DragDropEffects) => DragDropEffects.Move,
            var t when t == typeof(DependencyObject) => Hierarchy,
            var t when t == typeof(Point) => new Point(1, 1),
            _ => throw new NotSupportedException("unexpected DragEventArgs parameter " + p.ParameterType),
        }).ToArray();
        var e = (DragEventArgs)ctor.Invoke(args);
        e.RoutedEvent = routed;
        return e;
    }

    private static AutomationPeer Peer(UIElement element) =>
        UIElementAutomationPeer.CreatePeerForElement(element) ?? throw new InvalidOperationException("no automation peer");

    // ---- 화면 ----
    public byte[] ViewportPixels()
    {
        BitmapSource bmp = Window.CaptureViewport();
        var pixels = new byte[bmp.PixelWidth * bmp.PixelHeight * 4];
        bmp.CopyPixels(pixels, bmp.PixelWidth * 4, 0);
        return pixels;
    }

    public void Dispose() => Window.Close();

    // ---- 에셋 ----
    public static string Sample(string name)
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            string candidate = Path.Combine(dir, "assets", "samples", name);
            if (File.Exists(candidate))
                return candidate;
        }
        throw new FileNotFoundException("sample asset not found: " + name);
    }
}
