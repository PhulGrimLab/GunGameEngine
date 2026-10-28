using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using GunGame.Interop;
using GunGame.Scene;
using Microsoft.Win32;

namespace GunGameEditor;

/// <summary>선택한 노드의 속성을 편집하는 패널. 값은 Enter 또는 포커스를 잃을 때 노드에 반영된다.</summary>
public sealed class InspectorPanel : StackPanel
{
    private const float DegreesPerRadian = 180f / MathF.PI;

    private readonly TextBox _name = new();
    private readonly TextBlock _mesh = new() { TextTrimming = TextTrimming.CharacterEllipsis };
    private readonly TextBlock _texture = new() { TextTrimming = TextTrimming.CharacterEllipsis };
    private readonly TextBox[] _position = NewVecBoxes();
    private readonly TextBox[] _rotation = NewVecBoxes();
    private readonly TextBox[] _scale = NewVecBoxes();
    private readonly TextBox[] _color = NewVecBoxes();
    private SceneNode? _node;
    private bool _refreshing;

    public InspectorPanel()
    {
        Margin = new Thickness(8);
        _name.LostFocus += (_, _) => Commit();
        _name.KeyDown += (_, e) => { if (e.Key == System.Windows.Input.Key.Enter) Commit(); };

        AddRow("Name", _name);
        AddPathRow("Mesh", _mesh, "OBJ/GGM mesh|*.obj;*.ggm", (n, p) => n.MeshPath = p);
        AddPathRow("Texture", _texture, "Image|*.png;*.bmp;*.jpg;*.jpeg", (n, p) => n.TexturePath = p);
        AddVecRow("Position", _position);
        AddVecRow("Rotation (deg)", _rotation);
        AddVecRow("Scale", _scale);
        AddVecRow("Color (0-1)", _color);
        Show(null);
    }

    public void Show(SceneNode? node)
    {
        if (_node is not null)
            _node.PropertyChanged -= OnNodeChanged;
        _node = node;
        if (node is not null)
            node.PropertyChanged += OnNodeChanged;
        IsEnabled = node is not null;
        Refresh();
    }

    private void OnNodeChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e) => Refresh();

    private void Refresh()
    {
        _refreshing = true;
        SceneNode? n = _node;
        _name.Text = n?.Name ?? "";
        _mesh.Text = n?.MeshPath is { Length: > 0 } m ? Path.GetFileName(m) : "(none)";
        _mesh.ToolTip = n?.MeshPath;
        _texture.Text = n?.TexturePath is { Length: > 0 } t ? Path.GetFileName(t) : "(none)";
        _texture.ToolTip = n?.TexturePath;
        Fill(_position, n?.Position ?? default, 1f);
        Fill(_rotation, n?.Rotation ?? default, DegreesPerRadian);
        Fill(_scale, n?.Scale ?? new Vec3(1, 1, 1), 1f);
        Fill(_color, n?.Color ?? default, 1f);
        _refreshing = false;
    }

    private void Commit()
    {
        if (_node is null || _refreshing)
            return;
        _node.Name = string.IsNullOrWhiteSpace(_name.Text) ? _node.Name : _name.Text.Trim();
        _node.Position = Read(_position, _node.Position, 1f);
        _node.Rotation = Read(_rotation, _node.Rotation, DegreesPerRadian);
        _node.Scale = Read(_scale, _node.Scale, 1f);
        _node.Color = Read(_color, _node.Color, 1f);
        Refresh();  // 잘못 입력한 값은 이전 값으로 되돌린다
    }

    private static TextBox[] NewVecBoxes() => [new(), new(), new()];

    private static void Fill(TextBox[] boxes, Vec3 v, float scale)
    {
        boxes[0].Text = Format(v.X * scale);
        boxes[1].Text = Format(v.Y * scale);
        boxes[2].Text = Format(v.Z * scale);
    }

    private static Vec3 Read(TextBox[] boxes, Vec3 fallback, float scale) => new(
        Parse(boxes[0].Text, fallback.X * scale) / scale,
        Parse(boxes[1].Text, fallback.Y * scale) / scale,
        Parse(boxes[2].Text, fallback.Z * scale) / scale);

    private static float Parse(string text, float fallback) =>
        float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float v) && float.IsFinite(v) ? v : fallback;

    private static string Format(float v) => MathF.Round(v, 4).ToString("0.####", CultureInfo.InvariantCulture);

    private void AddRow(string label, UIElement editor)
    {
        Children.Add(new TextBlock { Text = label, Margin = new Thickness(0, 8, 0, 2), FontWeight = FontWeights.SemiBold });
        Children.Add(editor);
    }

    private void AddVecRow(string label, TextBox[] boxes)
    {
        var grid = new System.Windows.Controls.Primitives.UniformGrid { Columns = 3 };
        foreach (TextBox box in boxes)
        {
            box.Margin = new Thickness(0, 0, 4, 0);
            box.LostFocus += (_, _) => Commit();
            box.KeyDown += (_, e) => { if (e.Key == System.Windows.Input.Key.Enter) Commit(); };
            grid.Children.Add(box);
        }
        AddRow(label, grid);
    }

    private void AddPathRow(string label, TextBlock display, string filter, Action<SceneNode, string?> assign)
    {
        var browse = new Button { Content = "...", Width = 28, Margin = new Thickness(4, 0, 0, 0) };
        var clear = new Button { Content = "x", Width = 22, Margin = new Thickness(4, 0, 0, 0) };
        browse.Click += (_, _) =>
        {
            var dialog = new OpenFileDialog { Filter = filter };
            if (_node is not null && dialog.ShowDialog() == true)
                assign(_node, dialog.FileName);
        };
        clear.Click += (_, _) => { if (_node is not null) assign(_node, null); };

        var row = new DockPanel();
        DockPanel.SetDock(clear, Dock.Right);
        DockPanel.SetDock(browse, Dock.Right);
        row.Children.Add(clear);
        row.Children.Add(browse);
        row.Children.Add(display);
        AddRow(label, row);
    }
}
