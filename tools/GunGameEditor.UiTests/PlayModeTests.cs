using System.IO;
using System.Windows.Controls;
using GunGame.Interop;
using GunGame.Scene;

namespace GunGameEditor.UiTests;

/// <summary>
/// 플레이 모드와 게임플레이 오버레이. 키보드 상태(Keyboard.IsKeyDown)는 OS 입력이라 테스트에서 만들 수 없으므로
/// 타이머가 키를 읽어 부르는 StepPlay를 직접 호출해 같은 경로(세션 -> 카메라/화면/상태 표시)를 검증한다.
/// </summary>
public sealed class PlayModeTests(UiHost host) : UiTestBase(host)
{
    private static string TempPlayScene()
    {
        string dir = Path.Combine(Path.GetTempPath(), "gge_ui_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        File.Copy(EditorSession.Sample("cube.obj"), Path.Combine(dir, "cube.obj"));
        string path = Path.Combine(dir, "play.scene.json");
        File.Copy(EditorSession.Sample("play.scene.json"), path);
        return path;
    }

    /// <summary>샘플 플레이 씬을 연 에디터에서 body를 실행한다.</summary>
    private void WithPlayScene(Action<EditorSession> body) => Ui(s =>
    {
        string path = TempPlayScene();
        try
        {
            Assert.True(s.Window.OpenScenePath(path));
            s.Pump();
            body(s);
        }
        finally { Directory.Delete(Path.GetDirectoryName(path)!, true); }
    });

    [Fact]
    public void PlayWithoutGameplayData_ReportsWhy() => Ui(s =>
    {
        s.Click("Play");

        Assert.False(s.Window.IsPlaying);
        Assert.StartsWith("Play failed", s.Status);
        Assert.Contains("gameplay.player", s.Status);
        s.ButtonByLabel("Play");  // 버튼은 그대로 Play다(Stop으로 바뀌지 않는다)
    });

    [Fact]
    public void PlayAndStop_SwitchCameraAndLockEditing_ThenRestoreEverything() => WithPlayScene(s =>
    {
        SceneNode hero = s.Roots.Single(n => n.Name == "Hero");
        Vec3 heroBefore = hero.Position;
        byte[] edit = s.ViewportPixels();

        s.Click("Play");
        Assert.True(s.Window.IsPlaying);
        // 실제 30Hz 타이머가 Pump 중에 첫 Step을 돌릴 수 있다(스폰이 StartPad 안이라 Enter가 상태 표시줄을 덮는다): 둘 중 하나면 정상.
        Assert.True(s.Status.StartsWith("Playing") || s.Window.PlayLog.Count > 0, s.Status);
        s.ButtonByLabel("Stop");
        Assert.False(s.ButtonByLabel("Add Group").IsEnabled);  // 편집은 잠긴다
        Assert.False(s.ButtonByLabel("Open Scene...").IsEnabled);
        Assert.False(s.Hierarchy.IsEnabled);
        Assert.False(s.Inspector.IsEnabled);
        Assert.NotEqual(edit, s.ViewportPixels());              // 편집 카메라 대신 홀의 고정 카메라

        byte[] start = s.ViewportPixels();
        for (int i = 0; i < 10; i++) s.Window.StepPlay(1, 0, 1 / 30f);
        Assert.NotEqual(start, s.ViewportPixels());             // 플레이어가 움직인 것이 화면에 반영된다
        Assert.Equal(heroBefore, hero.Position);                // 문서는 바뀌지 않았다
        Assert.True(s.Window.CurrentPlay!.Position.X > -4f);

        s.Click("Stop");
        Assert.False(s.Window.IsPlaying);
        s.ButtonByLabel("Play");
        Assert.True(s.ButtonByLabel("Add Group").IsEnabled);    // 편집이 풀린다
        Assert.True(s.Hierarchy.IsEnabled);
        Assert.Equal(edit, s.ViewportPixels());                 // 편집 카메라와 플레이어 위치가 그대로 돌아왔다
        Assert.Equal(heroBefore, hero.Position);
    });

    [Fact]
    public void WalkingThroughTheScene_LogsTriggersAndCameraCuts_InOrder() => WithPlayScene(s =>
    {
        s.Click("Play");
        for (int i = 0; i < 300 && s.Window.CurrentPlay!.Position.X < 5; i++)
            s.Window.StepPlay(1, 0, 1 / 30f);

        Assert.Equal(
            [
                "Enter 'StartPad' (tag 1)",
                "Exit 'StartPad' (tag 1)",
                "Camera: room 'Annex'",
                "Enter 'Door' (tag 7)",
                "Exit 'Door' (tag 7)",
            ],
            s.Window.PlayLog);
        Assert.Equal("Exit 'Door' (tag 7)", s.Status);  // 가장 최근 이벤트가 상태 표시줄에 있다
    });

    [Fact]
    public void ViewportMouseIsIgnoredWhilePlaying_AndReturnsAfterStop() => WithPlayScene(s =>
    {
        float distance = s.Window.Camera.Distance;
        s.Click("Play");
        s.Wheel(s.Window.Viewport, 120);
        Assert.Equal(distance, s.Window.Camera.Distance);  // 플레이 중에는 카메라가 방에 고정되어 있다

        s.Click("Stop");
        s.Wheel(s.Window.Viewport, 120);
        Assert.NotEqual(distance, s.Window.Camera.Distance);
    });

    [Fact]
    public void OpeningAnotherSceneWhilePlaying_EndsThePlaySession() => WithPlayScene(s =>
    {
        s.Click("Play");
        Assert.True(s.Window.IsPlaying);

        s.Window.OpenScenePath(TempPlayScene());  // 프로그램에서 씬을 바꾸는 경로(대화상자 없음)

        Assert.False(s.Window.IsPlaying);
        s.ButtonByLabel("Play");
        Assert.True(s.Hierarchy.IsEnabled);
    });

    [Fact]
    public void ShowGameplayCheckbox_DrawsAndHidesTheOverlay_AndFollowsTheOpenedScene() => Ui(s =>
    {
        CheckBox check = s.CheckByLabel("Show gameplay");
        s.Toggle(check);  // 빈 씬: 켜도 그릴 것이 없다
        Assert.False(s.Window.GameplayOverlayVisible);
        s.Toggle(check);  // 다시 끈다

        string path = TempPlayScene();
        try
        {
            Assert.True(s.Window.OpenScenePath(path));
            s.Pump();
            byte[] hidden = s.ViewportPixels();
            Assert.False(s.Window.GameplayOverlayVisible);

            s.Toggle(check);
            Assert.True(s.Window.GameplayOverlayVisible);
            Assert.NotEqual(hidden, s.ViewportPixels());

            s.Toggle(check);
            Assert.False(s.Window.GameplayOverlayVisible);
            Assert.Equal(hidden, s.ViewportPixels());  // 끄면 원래 화면으로

            // 켜 둔 채 다른 씬을 열면(여기서는 빈 씬) 이전 씬의 오버레이가 남지 않는다.
            s.Toggle(check);
            Assert.True(s.Window.GameplayOverlayVisible);
            s.Click("New");
            Assert.False(s.Window.GameplayOverlayVisible);
        }
        finally { Directory.Delete(Path.GetDirectoryName(path)!, true); }
    });

    [Fact]
    public void DraggingTheViewport_OrbitsTheEditCamera() => Ui(s =>
    {
        s.Window.AddMesh(EditorSession.Sample("cube.obj"));
        float yaw = s.Window.Camera.Yaw, pitch = s.Window.Camera.Pitch;
        byte[] before = s.ViewportPixels();

        s.Window.OrbitDrag(new System.Windows.Point(100, 100), new System.Windows.Point(160, 120));
        Assert.True(s.Window.Camera.Yaw < yaw);        // 오른쪽으로 끌면 시계 방향(-yaw)
        Assert.True(s.Window.Camera.Pitch > pitch);    // 아래로 끌면 위에서 내려다본다(+pitch)
        byte[] moved = s.ViewportPixels();
        Assert.NotEqual(before, moved);

        s.Window.OrbitDrag(new System.Windows.Point(160, 120), new System.Windows.Point(100, 100));  // 반대로 끌면 원래대로
        Assert.Equal(yaw, s.Window.Camera.Yaw, 4);
        Assert.Equal(pitch, s.Window.Camera.Pitch, 4);
        Assert.Equal(before, s.ViewportPixels());
    });

    [Fact]
    public void OrbitDragIsIgnoredWhilePlaying() => WithPlayScene(s =>
    {
        float yaw = s.Window.Camera.Yaw;
        s.Click("Play");
        byte[] playing = s.ViewportPixels();
        s.Window.OrbitDrag(new System.Windows.Point(0, 0), new System.Windows.Point(200, 80));
        Assert.Equal(yaw, s.Window.Camera.Yaw);
        Assert.Equal(playing, s.ViewportPixels());
    });
}

/// <summary>키 매핑(OS 키 상태를 읽는 부분만 분리한 순수 함수). 창이 필요 없다.</summary>
public class PlayKeyMappingTests
{
    private static (float X, float Y) Map(params System.Windows.Input.Key[] down) =>
        MainWindow.PlayInputFromKeys(down.Contains);

    [Theory]
    [InlineData(System.Windows.Input.Key.D, 1, 0)]
    [InlineData(System.Windows.Input.Key.Right, 1, 0)]
    [InlineData(System.Windows.Input.Key.A, -1, 0)]
    [InlineData(System.Windows.Input.Key.Left, -1, 0)]
    [InlineData(System.Windows.Input.Key.W, 0, 1)]
    [InlineData(System.Windows.Input.Key.Up, 0, 1)]
    [InlineData(System.Windows.Input.Key.S, 0, -1)]
    [InlineData(System.Windows.Input.Key.Down, 0, -1)]
    public void EachKeyMapsToOneAxisDirection(System.Windows.Input.Key key, float x, float y) =>
        Assert.Equal((x, y), Map(key));

    [Fact]
    public void NoKeys_IsZero_OppositeKeysCancel_AndAltKeysDoNotDoubleCount()
    {
        Assert.Equal((0f, 0f), Map());
        Assert.Equal((0f, 0f), Map(System.Windows.Input.Key.A, System.Windows.Input.Key.D));
        Assert.Equal((0f, 0f), Map(System.Windows.Input.Key.Up, System.Windows.Input.Key.S));
        Assert.Equal((1f, 0f), Map(System.Windows.Input.Key.D, System.Windows.Input.Key.Right));  // 같은 방향 두 키는 1
        Assert.Equal((0f, 0f), Map(System.Windows.Input.Key.A, System.Windows.Input.Key.Right));  // 서로 다른 계열 반대 키도 상쇄
    }

    [Fact]
    public void Diagonal_KeepsBothAxes_AndIgnoresUnrelatedKeys() =>
        Assert.Equal((1f, -1f), Map(System.Windows.Input.Key.D, System.Windows.Input.Key.S, System.Windows.Input.Key.Space, System.Windows.Input.Key.Q));
}
