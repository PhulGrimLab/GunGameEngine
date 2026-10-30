using System.IO;
using System.Windows;
using System.Windows.Controls;
using GunGame.Interop;
using GunGame.Scene;

namespace GunGameEditor.UiTests;

[Collection(UiCollection.Name)]
public abstract class UiTestBase(UiHost host)
{
    /// <summary>새 에디터 창을 띄우고 body를 UI 스레드에서 실행한 뒤 창을 닫는다.</summary>
    protected void Ui(Action<EditorSession> body) => host.Run(() =>
    {
        using var session = new EditorSession();
        body(session);
    });
}

/// <summary>툴바 버튼과 계층 트리 입력.</summary>
public sealed class HierarchyInputTests(UiHost host) : UiTestBase(host)
{
    [Fact]
    public void StartsEmptyWithInspectorDisabled() => Ui(s =>
    {
        Assert.Empty(s.Roots);
        Assert.False(s.Inspector.IsEnabled);
        Assert.Equal("GunGame Editor", s.Window.Title);
    });

    [Fact]
    public void AddGroupButtonAddsAndSelectsNode() => Ui(s =>
    {
        s.Click("Add Group");

        SceneNode node = Assert.Single(s.Roots);
        Assert.Equal("Group", node.Name);
        Assert.Same(node, s.Selected);
        Assert.True(s.Inspector.IsEnabled);
        Assert.Equal("Group", s.Box(EditorSession.NameBox).Text);
    });

    [Fact]
    public void AddGroupWithSelectionCreatesChild() => Ui(s =>
    {
        s.Click("Add Group");
        SceneNode parent = s.Roots[0];
        s.Click("Add Group");

        SceneNode child = Assert.Single(parent.Children);
        Assert.Same(parent, child.Parent);
        Assert.Same(child, s.Selected);
        Assert.Single(s.Roots);
    });

    [Fact]
    public void ClickingTreeItemSelectsNodeAndFillsInspector() => Ui(s =>
    {
        s.Click("Add Group");
        s.Click("Add Group");
        SceneNode first = s.Roots[0];
        first.Name = "First";
        first.Position = new Vec3(1, 2, 3);
        SceneNode second = first.Children[0];
        Assert.Same(second, s.Selected);

        s.Select(first);

        Assert.Same(first, s.Selected);
        Assert.Equal("First", s.Box(EditorSession.NameBox).Text);
        Assert.Equal(["1", "2", "3"], s.Boxes().Skip(EditorSession.PositionBox).Take(3).Select(b => b.Text));
    });

    [Fact]
    public void DeleteButtonRemovesSelectedSubtreeAndDisablesInspector() => Ui(s =>
    {
        s.Click("Add Group");
        s.Click("Add Group");

        s.Select(s.Roots[0]);
        s.Click("Delete");

        Assert.Empty(s.Roots);
        Assert.False(s.Inspector.IsEnabled);
    });

    [Fact]
    public void DeleteWithoutSelectionDoesNothing() => Ui(s =>
    {
        s.Click("Add Group");
        s.Hierarchy.Focus();
        s.ItemOf(s.Roots[0]).IsSelected = false;
        Assert.Null(s.Selected);

        s.Click("Delete");

        Assert.Single(s.Roots);
    });

    [Fact]
    public void NewButtonClearsSceneAndInspector() => Ui(s =>
    {
        s.Click("Add Group");
        s.Click("Add Group");

        s.Click("New");

        Assert.Empty(s.Roots);
        Assert.False(s.Inspector.IsEnabled);
        Assert.Contains("unsaved scene", s.Window.Title);
    });

    [Fact]
    public void DropOnNodeReparentsIt() => Ui(s =>
    {
        s.Click("Add Group");
        SceneNode a = s.Roots[0];
        s.ItemOf(a).IsSelected = false;
        s.Click("Add Group");
        SceneNode b = s.Roots[1];
        a.Name = "A";
        b.Name = "B";

        DragDropEffects effects = s.Drop(b, a);

        Assert.Equal(DragDropEffects.Move, effects);
        Assert.Single(s.Roots);
        Assert.Same(b, Assert.Single(a.Children));
        Assert.Same(b, s.Selected);
    });

    [Fact]
    public void DropOnEmptyAreaMovesNodeToRoot() => Ui(s =>
    {
        s.Click("Add Group");
        s.Click("Add Group");
        SceneNode parent = s.Roots[0];
        SceneNode child = parent.Children[0];

        s.Drop(child, null);

        Assert.Equal(2, s.Roots.Count);
        Assert.Empty(parent.Children);
        Assert.Null(child.Parent);
    });

    [Fact]
    public void DropOnOwnDescendantIsRejectedWithStatusMessage() => Ui(s =>
    {
        s.Click("Add Group");
        s.Click("Add Group");
        SceneNode parent = s.Roots[0];
        SceneNode child = parent.Children[0];

        s.Drop(parent, child);

        Assert.Same(parent, Assert.Single(s.Roots));
        Assert.Same(child, Assert.Single(parent.Children));
        Assert.StartsWith("Move failed", s.Status);
    });

    [Fact]
    public void DropOnItselfIsRejected() => Ui(s =>
    {
        s.Click("Add Group");
        SceneNode node = s.Roots[0];

        s.Drop(node, node);

        Assert.Same(node, Assert.Single(s.Roots));
        Assert.StartsWith("Move failed", s.Status);
    });

    [Fact]
    public void DragOverAcceptsOnlySceneNodes() => Ui(s =>
    {
        Assert.Equal(DragDropEffects.None, s.DragOverForeignData());
    });
}

/// <summary>인스펙터 입력: 이름, 변환, 색, 애니메이션.</summary>
public sealed class InspectorInputTests(UiHost host) : UiTestBase(host)
{
    private static SceneNode NewGroup(EditorSession s)
    {
        s.Click("Add Group");
        return s.Selected!;
    }

    [Fact]
    public void NameEnterCommitsAndTreeLabelFollows() => Ui(s =>
    {
        SceneNode node = NewGroup(s);

        s.TypeEnter(s.Box(EditorSession.NameBox), "  Door  ");

        Assert.Equal("Door", node.Name);  // 앞뒤 공백은 제거된다
        s.Pump();
        Assert.Contains(EditorSession.FindAll<TextBlock>(s.Hierarchy), t => t.Text == "Door");
    });

    [Fact]
    public void BlankNameKeepsPreviousName() => Ui(s =>
    {
        SceneNode node = NewGroup(s);
        node.Name = "Keep";

        s.TypeEnter(s.Box(EditorSession.NameBox), "   ");

        Assert.Equal("Keep", node.Name);
        Assert.Equal("Keep", s.Box(EditorSession.NameBox).Text);
    });

    [Fact]
    public void PositionEnterCommitsAllThreeAxes() => Ui(s =>
    {
        SceneNode node = NewGroup(s);

        s.TypeEnter(s.Box(EditorSession.PositionBox), "1.5");
        s.TypeEnter(s.Box(EditorSession.PositionBox + 1), "-2");
        s.TypeEnter(s.Box(EditorSession.PositionBox + 2), "0.25");

        Assert.Equal(new Vec3(1.5f, -2f, 0.25f), node.Position);
    });

    [Fact]
    public void LosingFocusCommitsTheValue() => Ui(s =>
    {
        SceneNode node = NewGroup(s);

        s.TypeBlur(s.Box(EditorSession.PositionBox), "7");

        Assert.Equal(7f, node.Position.X);
    });

    [Fact]
    public void RotationIsEnteredInDegreesAndStoredInRadians() => Ui(s =>
    {
        SceneNode node = NewGroup(s);

        s.TypeEnter(s.Box(EditorSession.RotationBox + 1), "90");

        Assert.Equal(MathF.PI / 2, node.Rotation.Y, 1e-5f);
        Assert.Equal("90", s.Box(EditorSession.RotationBox + 1).Text);
    });

    [Fact]
    public void ScaleAndColorEnterCommit() => Ui(s =>
    {
        SceneNode node = NewGroup(s);

        s.TypeEnter(s.Box(EditorSession.ScaleBox), "2");
        s.TypeEnter(s.Box(EditorSession.ColorBox + 2), "0.5");

        Assert.Equal(2f, node.Scale.X);
        Assert.Equal(1f, node.Scale.Y);
        Assert.Equal(0.5f, node.Color.Z);
    });

    [Theory]
    [InlineData("abc")]
    [InlineData("")]
    [InlineData("1,5")]
    [InlineData("NaN")]
    [InlineData("Infinity")]
    [InlineData("-Infinity")]
    public void InvalidNumberRevertsToPreviousValue(string input) => Ui(s =>
    {
        SceneNode node = NewGroup(s);
        node.Position = new Vec3(3, 4, 5);
        TextBox x = s.Box(EditorSession.PositionBox);

        s.TypeEnter(x, input);

        Assert.Equal(new Vec3(3, 4, 5), node.Position);
        Assert.Equal("3", x.Text);
    });

    [Fact]
    public void ScientificNotationIsAccepted() => Ui(s =>
    {
        SceneNode node = NewGroup(s);

        s.TypeEnter(s.Box(EditorSession.PositionBox), "1e2");

        Assert.Equal(100f, node.Position.X);
    });

    [Fact]
    public void InvalidFieldDoesNotDiscardOtherPendingEdits() => Ui(s =>
    {
        SceneNode node = NewGroup(s);
        s.Box(EditorSession.PositionBox + 1).Text = "9";  // 아직 확정 전
        s.TypeEnter(s.Box(EditorSession.PositionBox), "oops");

        Assert.Equal(new Vec3(0, 9, 0), node.Position);
    });

    [Fact]
    public void ExternalChangeRefreshesTextBoxes() => Ui(s =>
    {
        SceneNode node = NewGroup(s);

        node.Name = "Renamed";
        node.Scale = new Vec3(3, 3, 3);
        node.Rotation = new Vec3(0, MathF.PI, 0);

        Assert.Equal("Renamed", s.Box(EditorSession.NameBox).Text);
        Assert.Equal("3", s.Box(EditorSession.ScaleBox).Text);
        Assert.Equal("180", s.Box(EditorSession.RotationBox + 1).Text);
    });

    [Fact]
    public void EditsGoToTheSelectedNodeOnly() => Ui(s =>
    {
        SceneNode first = NewGroup(s);
        s.ItemOf(first).IsSelected = false;
        SceneNode second = NewGroup(s);

        s.TypeEnter(s.Box(EditorSession.PositionBox), "5");
        Assert.Equal(5f, second.Position.X);
        Assert.Equal(0f, first.Position.X);

        s.Select(first);
        s.TypeEnter(s.Box(EditorSession.PositionBox), "8");
        Assert.Equal(8f, first.Position.X);
        Assert.Equal(5f, second.Position.X);
    });

    [Fact]
    public void ClearButtonRemovesTexturePath() => Ui(s =>
    {
        SceneNode node = NewGroup(s);
        node.TexturePath = EditorSession.Sample("checker.png");
        Button[] clears = EditorSession.FindAll<Button>(s.Inspector, b => b.Content as string == "x").ToArray();

        s.Click(clears[1]);  // 0 = Mesh, 1 = Texture

        Assert.Null(node.TexturePath);
    });

    [Fact]
    public void ClearButtonRemovesMeshPath() => Ui(s =>
    {
        s.Window.AddMesh(EditorSession.Sample("cube.obj"));
        SceneNode node = s.Selected!;
        Assert.NotNull(node.MeshPath);
        Button mesh = EditorSession.FindAll<Button>(s.Inspector, b => b.Content as string == "x").First();

        s.Click(mesh);

        Assert.Null(node.MeshPath);
    });

    [Fact]
    public void AnimationSectionIsHiddenForNodesWithoutClips() => Ui(s =>
    {
        NewGroup(s);
        StackPanel section = EditorSession.FindAll<StackPanel>(s.Inspector, p => p.Children.OfType<ComboBox>().Any()).Single();
        Assert.Equal(Visibility.Collapsed, section.Visibility);
    });

    [Fact]
    public void ClipSelectionPlayingAndSpeedAreEditable() => Ui(s =>
    {
        s.Window.AddMesh(EditorSession.Sample("tentacle.glb"));
        SceneNode node = s.Selected!;
        ComboBox clips = EditorSession.FindAll<ComboBox>(s.Inspector).Single();
        Assert.True(clips.Items.Count > 1, "tentacle.glb should expose clips");
        Assert.Equal(0, clips.SelectedIndex);  // 처음엔 레스트 포즈

        clips.SelectedIndex = 1;
        s.Pump();
        Assert.Equal(clips.Items[1], node.AnimationClip);

        CheckBox playing = EditorSession.FindAll<CheckBox>(s.Inspector).Single();
        Assert.True(node.AnimationPlaying);
        s.Toggle(playing);
        Assert.False(node.AnimationPlaying);

        s.TypeEnter(s.Box(EditorSession.SpeedBox), "2.5");
        Assert.Equal(2.5f, node.AnimationSpeed);

        clips.SelectedIndex = 0;
        s.Pump();
        Assert.Null(node.AnimationClip);
    });
}

/// <summary>뷰포트와 렌더 옵션 입력.</summary>
public sealed class ViewportInputTests(UiHost host) : UiTestBase(host)
{
    [Fact]
    public void MouseWheelZoomsCameraInAndOut() => Ui(s =>
    {
        float start = s.Window.Camera.Distance;

        s.Wheel(s.Window.Viewport, 120);
        float zoomedIn = s.Window.Camera.Distance;
        s.Wheel(s.Window.Viewport, -120);

        Assert.True(zoomedIn < start);
        Assert.True(s.Window.Camera.Distance > zoomedIn);
    });

    [Fact]
    public void WheelZoomStopsAtLimits() => Ui(s =>
    {
        for (int i = 0; i < 200; i++)
            s.Wheel(s.Window.Viewport, 120);
        float near = s.Window.Camera.Distance;
        s.Wheel(s.Window.Viewport, 120);
        Assert.Equal(near, s.Window.Camera.Distance);

        for (int i = 0; i < 400; i++)
            s.Wheel(s.Window.Viewport, -120);
        float far = s.Window.Camera.Distance;
        s.Wheel(s.Window.Viewport, -120);
        Assert.Equal(far, s.Window.Camera.Distance);
    });

    [Fact]
    public void WheelRedrawsTheViewport() => Ui(s =>
    {
        s.Window.AddMesh(EditorSession.Sample("cube.obj"));
        s.Pump();
        byte[] before = s.ViewportPixels();

        for (int i = 0; i < 5; i++)
            s.Wheel(s.Window.Viewport, 120);

        Assert.NotEqual(before, s.ViewportPixels());
    });

    [Fact]
    public void ColorInputChangesRenderedPixels() => Ui(s =>
    {
        s.Window.AddMesh(EditorSession.Sample("cube.obj"));
        s.Pump();
        s.Window.Redraw();
        byte[] before = s.ViewportPixels();

        s.TypeEnter(s.Box(EditorSession.ColorBox), "0.1");
        s.TypeEnter(s.Box(EditorSession.ColorBox + 1), "0.9");
        s.TypeEnter(s.Box(EditorSession.ColorBox + 2), "0.1");
        s.Window.Redraw();

        Assert.NotEqual(before, s.ViewportPixels());
    });

    [Fact]
    public void RetroCheckboxesToggleAndRerender() => Ui(s =>
    {
        s.Window.AddMesh(EditorSession.Sample("cube.obj"));
        s.Pump();
        s.Window.Redraw();
        byte[] withBits = s.ViewportPixels();

        CheckBox bits = s.CheckByLabel("15-bit color");
        Assert.True(bits.IsChecked);
        s.Toggle(bits);
        Assert.False(bits.IsChecked);
        s.Window.Redraw();
        byte[] fullColor = s.ViewportPixels();
        Assert.NotEqual(withBits, fullColor);

        s.Toggle(bits);
        s.Window.Redraw();
        Assert.Equal(withBits, s.ViewportPixels());  // 되돌리면 원래 화면이 나온다
    });

    [Theory]
    [InlineData("Vertex snap")]
    [InlineData("Dither")]
    [InlineData("Affine texture")]
    public void EachRetroCheckboxTogglesWithoutError(string label) => Ui(s =>
    {
        s.Window.AddMesh(EditorSession.Sample("cube.obj"));
        CheckBox box = s.CheckByLabel(label);
        Assert.True(box.IsChecked);

        s.Toggle(box);
        Assert.False(box.IsChecked);
        s.Toggle(box);
        Assert.True(box.IsChecked);

        Assert.Empty(s.Window.SceneErrors);
    });
}

/// <summary>씬 파일 입력(대화상자를 거치지 않는 경로).</summary>
public sealed class SceneFileInputTests(UiHost host) : UiTestBase(host)
{
    private static string TempScene(string name = "scene.json")
    {
        string dir = Path.Combine(Path.GetTempPath(), "gge_ui_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        foreach (string asset in new[] { "cube.obj", "checker.png", "tentacle.glb" })
            File.Copy(EditorSession.Sample(asset), Path.Combine(dir, asset));
        string path = Path.Combine(dir, name);
        File.Copy(EditorSession.Sample("sample.scene.json"), path);
        return path;
    }

    [Fact]
    public void OpeningSampleSceneFillsTreeAndTitle() => Ui(s =>
    {
        string path = TempScene();
        try
        {
            Assert.True(s.Window.OpenScenePath(path));
            s.Pump();

            Assert.Equal(3, s.Roots.Count);
            Assert.Equal("GunGame Editor - scene.json", s.Window.Title);
            Assert.Contains(s.TreeItems(), i => ((SceneNode)i.DataContext).Name == "Red Block");  // 중첩 자손까지 표시
            Assert.False(s.Inspector.IsEnabled);
        }
        finally { Directory.Delete(Path.GetDirectoryName(path)!, true); }
    });

    [Fact]
    public void OpeningMissingFileReportsFailureAndKeepsScene() => Ui(s =>
    {
        s.Click("Add Group");

        bool ok = s.Window.OpenScenePath(Path.Combine(Path.GetTempPath(), "gge_missing_" + Guid.NewGuid().ToString("N") + ".json"));

        Assert.False(ok);
        Assert.StartsWith("Open failed", s.Status);
        Assert.Single(s.Roots);
    });

    [Fact]
    public void OpeningMalformedFileReportsFailure() => Ui(s =>
    {
        string path = Path.Combine(Path.GetTempPath(), "gge_bad_" + Guid.NewGuid().ToString("N") + ".json");
        File.WriteAllText(path, "{ not json");
        try
        {
            Assert.False(s.Window.OpenScenePath(path));
            Assert.StartsWith("Open failed", s.Status);
        }
        finally { File.Delete(path); }
    });

    [Fact]
    public void EditThenSaveButtonWritesChangesToOpenedFile() => Ui(s =>
    {
        string path = TempScene();
        try
        {
            s.Window.OpenScenePath(path);
            s.Pump();
            SceneNode crate = s.Roots.First(n => n.Name == "Crate");
            s.Select(crate);
            s.TypeEnter(s.Box(EditorSession.NameBox), "Big Crate");
            s.TypeEnter(s.Box(EditorSession.PositionBox), "4");

            s.Click("Save");  // 경로가 이미 있으므로 대화상자 없이 저장된다

            Assert.StartsWith("Saved", s.Status);
            SceneDocument reloaded = SceneSerializer.Load(path);
            SceneNode saved = reloaded.Roots.First(n => n.Name == "Big Crate");
            Assert.Equal(4f, saved.Position.X);
        }
        finally { Directory.Delete(Path.GetDirectoryName(path)!, true); }
    });

    [Fact]
    public void AddingMeshViaEditorSelectsItAndShowsFileName() => Ui(s =>
    {
        s.Window.AddMesh(EditorSession.Sample("cube.obj"));

        SceneNode node = Assert.Single(s.Roots);
        Assert.Equal("cube", node.Name);
        Assert.Same(node, s.Selected);
        Assert.Contains(EditorSession.FindAll<TextBlock>(s.Inspector), t => t.Text == "cube.obj");
        Assert.Empty(s.Window.SceneErrors);
    });
}
