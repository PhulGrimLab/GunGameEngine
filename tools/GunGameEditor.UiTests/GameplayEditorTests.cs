using System.IO;
using System.Windows;
using System.Windows.Controls;
using GunGame.Interop;
using GunGame.Scene;

namespace GunGameEditor.UiTests;

/// <summary>
/// 게임플레이 데이터 편집 UI(방/충돌체/트리거/플레이어 추가, 값 편집, 이동, 삭제)와 편집 결과의 저장/플레이 연결.
/// 입력은 다른 UI 테스트와 같은 방식이다(UI Automation 선택/Invoke, 칸에 값을 넣고 Enter/LostFocus).
/// </summary>
public sealed class GameplayEditorTests(UiHost host) : UiTestBase(host)
{
    /// <summary>cube.obj를 쓰는 노드 "Hero" 하나가 있는 임시 씬 파일(저장 왕복용).</summary>
    private static string TempHeroScene()
    {
        string dir = Path.Combine(Path.GetTempPath(), "gge_gp_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        File.Copy(EditorSession.Sample("cube.obj"), Path.Combine(dir, "cube.obj"));
        string path = Path.Combine(dir, "hero.scene.json");
        File.WriteAllText(path, """{"version":1,"nodes":[{"name":"Hero","mesh":"cube.obj","position":[1,0.5,2]}]}""");
        return path;
    }

    private void WithHeroScene(Action<EditorSession, string> body) => Ui(s =>
    {
        string path = TempHeroScene();
        try
        {
            Assert.True(s.Window.OpenScenePath(path));
            s.Pump();
            body(s, path);
        }
        finally { Directory.Delete(Path.GetDirectoryName(path)!, true); }
    });

    [Fact]
    public void StartsWithEmptyList_AndNoGameplayInspector() => Ui(s =>
    {
        Assert.Empty(s.GameplayLabels());
        Assert.False(s.GameplayPanel.IsEnabled);
        Assert.Equal(Visibility.Collapsed, s.GameplayPanel.Visibility);
        Assert.Equal(Visibility.Visible, s.Inspector.Visibility);
    });

    [Fact]
    public void AddRoom_UsesViewCamera_SelectsIt_AndTurnsTheOverlayOn() => Ui(s =>
    {
        Vec3 eye = s.Window.Camera.Eye, target = s.Window.Camera.Target;
        Assert.False(s.Window.GameplayOverlayVisible);

        s.Click("Add Room");

        Assert.Equal(["Room: Room"], s.GameplayLabels());
        var room = Assert.IsType<RoomDef>(s.SelectedGameplay!.Def);
        Assert.Equal((eye, target), (room.Eye, room.Target));
        Assert.Equal(GameplayEditing.Rectangle(target.X, target.Z, 8, 6), room.Polygon);
        Assert.True(s.CheckByLabel("Show gameplay").IsChecked);
        Assert.True(s.Window.GameplayOverlayVisible);
        Assert.Equal(Visibility.Visible, s.GameplayPanel.Visibility);   // 게임플레이 인스펙터가 보이고
        Assert.Equal(Visibility.Collapsed, s.Inspector.Visibility);     // 노드 인스펙터는 숨는다
        Assert.True(s.GameplayPanel.IsEnabled);
        Assert.Equal("Room", s.Field("Name").Text);
    });

    [Fact]
    public void AddColliderAndTrigger_AppearInListWithUniqueNamesAndNextTag() => Ui(s =>
    {
        s.Click("Add Collider");
        s.Click("Add Collider");
        s.Click("Add Trigger");
        s.Click("Add Trigger");

        Assert.Equal(["Collider: Collider", "Collider: Collider 2", "Trigger: Trigger", "Trigger: Trigger 2"], s.GameplayLabels());
        Assert.Equal("2", s.Field("Tag").Text);  // 마지막으로 추가한 트리거가 선택되어 있고 태그는 1, 2 순서
    });

    [Fact]
    public void EditingRoomFields_UpdatesDataAndListAndOverlay() => Ui(s =>
    {
        s.Click("Add Room");
        var room = (RoomDef)s.SelectedGameplay!.Def;
        s.Toggle(s.CheckByLabel("Show gameplay"));  // 오버레이 끄기/켜기를 오가며 화면 변화를 비교하지 않고, 편집 후 화면이 바뀌는지만 본다
        s.Toggle(s.CheckByLabel("Show gameplay"));
        byte[] before = s.ViewportPixels();

        s.TypeEnter(s.Field("Name"), "Lobby");
        s.TypeBlur(s.Field("Polygon (x, z per line)"), "0 0\n10 0\n10 10\n0 10");
        s.TypeEnter(s.Field("Eye (x, y, z)"), "5, 8, 20");
        s.TypeEnter(s.Field("Target (x, y, z)"), "5 0 5");
        s.TypeEnter(s.Field("Fov (radians)"), "0.5");

        Assert.Equal("Lobby", room.Name);
        Assert.Equal([0f, 0f, 10f, 0f, 10f, 10f, 0f, 10f], room.Polygon);
        Assert.Equal((new Vec3(5, 8, 20), new Vec3(5, 0, 5), 0.5f), (room.Eye, room.Target, room.Fov));
        Assert.Equal(["Room: Lobby"], s.GameplayLabels());
        Assert.Same(room, s.SelectedGameplay!.Def);          // 목록을 다시 만들어도 선택이 유지된다
        Assert.NotEqual(before, s.ViewportPixels());         // 오버레이가 새 다각형으로 다시 그려졌다
    });

    [Fact]
    public void InvalidInput_IsRejectedWithReason_AndRevertsTheField() => Ui(s =>
    {
        s.Click("Add Room");
        var room = (RoomDef)s.SelectedGameplay!.Def;
        float[] polygon = room.Polygon;

        s.TypeBlur(s.Field("Polygon (x, z per line)"), "0 0, 1 1");  // 꼭짓점 2개
        Assert.StartsWith("Rejected: polygon", s.Status);
        Assert.Same(polygon, room.Polygon);
        Assert.Equal(GameplayEditing.FormatPolygon(polygon), s.Field("Polygon (x, z per line)").Text);

        s.TypeEnter(s.Field("Fov (radians)"), "4");  // pi 초과
        Assert.StartsWith("Rejected: fov", s.Status);
        Assert.Equal(0.8f, room.Fov);
        Assert.Equal("0.8", s.Field("Fov (radians)").Text);

        s.TypeEnter(s.Field("Eye (x, y, z)"), "1, 2");  // 숫자 개수 부족
        Assert.StartsWith("Rejected: Eye", s.Status);

        s.TypeEnter(s.Field("Name"), "   ");
        Assert.Equal("Rejected: name is empty", s.Status);
        Assert.Equal("Room", room.Name);
    });

    [Fact]
    public void ColliderMinMax_RejectsFlippedBox_AndMoveShiftsIt() => Ui(s =>
    {
        s.Click("Add Collider");
        var c = (ColliderDef)s.SelectedGameplay!.Def;

        s.TypeEnter(s.Field("Min (x, z)"), "5, 5");  // max(0.5)보다 큼
        Assert.StartsWith("Rejected: min", s.Status);
        Assert.Equal((-0.5f, -0.5f), (c.MinX, c.MinZ));

        s.TypeEnter(s.Field("Max (x, z)"), "3, 4");
        Assert.Equal((3f, 4f), (c.MaxX, c.MaxZ));

        s.TypeEnter(s.Field("Move area by (dx, dz)"), "10, -2");
        s.Click(s.PanelButton("Move"));
        Assert.Equal((9.5f, -2.5f, 13f, 2f), (c.MinX, c.MinZ, c.MaxX, c.MaxZ));
        Assert.Equal("0, 0", s.Field("Move area by (dx, dz)").Text);  // 적용 후 입력 칸은 초기화된다
        Assert.Equal("9.5, -2.5", s.Field("Min (x, z)").Text);        // 값 칸도 새 값을 보여 준다
    });

    [Fact]
    public void TriggerTagAndMove_AndBadMoveText() => Ui(s =>
    {
        s.Click("Add Trigger");
        var t = (TriggerDef)s.SelectedGameplay!.Def;

        s.TypeEnter(s.Field("Tag"), "42");
        Assert.Equal(42, t.Tag);
        s.TypeEnter(s.Field("Tag"), "abc");
        Assert.Equal("Rejected: tag must be an integer", s.Status);
        Assert.Equal(42, t.Tag);
        Assert.Equal("42", s.Field("Tag").Text);

        float[] before = t.Polygon;
        s.TypeEnter(s.Field("Move area by (dx, dz)"), "oops");
        s.Click(s.PanelButton("Move"));
        Assert.StartsWith("Rejected: Move area by", s.Status);
        Assert.Same(before, t.Polygon);

        s.TypeEnter(s.Field("Move area by (dx, dz)"), "1, 1");
        s.Click(s.PanelButton("Move"));
        Assert.Equal(GameplayEditing.Translate(before, 1, 1), t.Polygon);
    });

    [Fact]
    public void UseViewCamera_CopiesTheCurrentOrbitCameraIntoTheRoom() => Ui(s =>
    {
        s.Click("Add Room");
        var room = (RoomDef)s.SelectedGameplay!.Def;
        s.Window.OrbitDrag(new Point(100, 100), new Point(180, 130));  // 편집 카메라를 돌린다
        Assert.NotEqual(room.Eye, s.Window.Camera.Eye);

        s.Click(s.PanelButton("Use view camera"));

        Assert.Equal(s.Window.Camera.Eye, room.Eye);
        Assert.Equal(s.Window.Camera.Target, room.Target);
    });

    [Fact]
    public void SetPlayer_NeedsASelectedRootNode_AndTakesItsPosition() => WithHeroScene((s, _) =>
    {
        s.Click("Set Player");  // 노드를 고르지 않았다
        Assert.Equal("Select a root node in the hierarchy first", s.Status);
        Assert.Empty(s.GameplayLabels());

        s.Select(s.Roots.Single());
        s.Click("Set Player");

        Assert.Equal(["Player: Hero"], s.GameplayLabels());
        var player = Assert.IsType<PlayerDef>(s.SelectedGameplay!.Def);
        Assert.Equal(new Vec2(1, 2), player.Spawn);
        Assert.Equal("1, 2", s.Field("Spawn (x, z)").Text);

        s.TypeEnter(s.Field("Radius"), "0");
        Assert.StartsWith("Rejected: radius", s.Status);
        s.TypeEnter(s.Field("Speed"), "6");
        Assert.Equal(6f, player.Speed);
        s.TypeEnter(s.Field("Spawn (x, z)"), "");  // 비우면 스폰을 지운다(노드 위치에서 시작)
        Assert.Null(player.Spawn);
    });

    [Fact]
    public void PlayerNodeAndSpawnFields_ValidateInput_AndRenameUpdatesTheList() => WithHeroScene((s, _) =>
    {
        s.Click("Add Room");  // 플레이 시작은 방이 있어야 노드 검사까지 간다
        s.Select(s.Roots.Single());
        s.Click("Set Player");
        var player = (PlayerDef)s.SelectedGameplay!.Def;

        s.TypeEnter(s.Field("Spawn (x, z)"), "7");  // 숫자 1개
        Assert.StartsWith("Rejected: Spawn", s.Status);
        s.TypeEnter(s.Field("Spawn (x, z)"), "a, b");
        Assert.StartsWith("Rejected: Spawn", s.Status);
        Assert.Equal(new Vec2(1, 2), player.Spawn);
        Assert.Equal("1, 2", s.Field("Spawn (x, z)").Text);

        s.TypeEnter(s.Field("Node"), "  ");
        Assert.Equal("Rejected: node name is empty", s.Status);
        Assert.Equal("Hero", player.Node);
        s.TypeEnter(s.Field("Node"), " Other ");
        Assert.Equal("Other", player.Node);  // 앞뒤 공백은 지운다
        Assert.Equal(["Room: Room", "Player: Other"], s.GameplayLabels());

        s.Click("Play");  // 없는 노드는 플레이 시작에서 사유와 함께 거부된다
        Assert.False(s.Window.IsPlaying);
        Assert.Contains("'Other' was not found", s.Status);
    });

    [Fact]
    public void SetPlayer_RejectsChildNode() => WithHeroScene((s, _) =>
    {
        s.Select(s.Roots.Single());
        s.Click("Add Group");  // 선택한 노드 아래에 자식이 생기고 선택된다
        Assert.NotNull(s.Selected!.Parent);

        s.Click("Set Player");

        Assert.StartsWith("Set player failed", s.Status);
        Assert.Empty(s.GameplayLabels());
    });

    [Fact]
    public void SelectingNodeAndGameplayItem_SwitchesInspectors()
    {
        Ui(s =>
        {
            s.Window.AddMesh(EditorSession.Sample("cube.obj"));
            SceneNode node = s.Roots.Single();
            s.Click("Add Trigger");
            Assert.Equal(Visibility.Visible, s.GameplayPanel.Visibility);

            s.ClickNode(node);  // 이미 선택된 노드를 눌러도 목록 선택이 풀리고 노드 인스펙터가 돌아온다
            Assert.Null(s.SelectedGameplay);
            Assert.Equal(Visibility.Visible, s.Inspector.Visibility);
            Assert.Equal(Visibility.Collapsed, s.GameplayPanel.Visibility);
            Assert.True(s.Inspector.IsEnabled);
            Assert.False(s.GameplayPanel.IsEnabled);

            s.SelectGameplay("Trigger: Trigger");
            Assert.Equal(Visibility.Visible, s.GameplayPanel.Visibility);
            Assert.Equal(Visibility.Collapsed, s.Inspector.Visibility);
            Assert.Same(node, s.Selected);  // 트리 선택은 그대로다(Set Player가 쓴다)
        });
    }

    [Fact]
    public void DeleteItem_RemovesOnlyThatItem_AndClearsTheInspector() => Ui(s =>
    {
        s.Click("Add Room");
        s.Click("Add Collider");
        s.Click("Add Trigger");
        s.SelectGameplay("Collider: Collider");

        s.Click("Delete Item");

        Assert.Equal(["Room: Room", "Trigger: Trigger"], s.GameplayLabels());
        Assert.Null(s.SelectedGameplay);
        Assert.False(s.GameplayPanel.IsEnabled);
        Assert.Equal(Visibility.Visible, s.Inspector.Visibility);

        s.Click("Delete Item");  // 선택이 없으면 안내만 한다
        Assert.Equal("Select a gameplay item to delete", s.Status);
        Assert.Equal(2, s.GameplayLabels().Count);

        s.SelectGameplay("Room: Room");
        s.Click("Delete Item");
        s.SelectGameplay("Trigger: Trigger");
        s.Click("Delete Item");
        Assert.Empty(s.GameplayLabels());
        Assert.False(s.Window.GameplayOverlayVisible);  // 그릴 것이 없으면 오버레이도 비어 있다
    });

    [Fact]
    public void DeletingPlayer_ClearsPlayerData() => WithHeroScene((s, _) =>
    {
        s.Select(s.Roots.Single());
        s.Click("Set Player");
        s.Click("Delete Item");

        Assert.Empty(s.GameplayLabels());
        s.Click("Play");
        Assert.Contains("gameplay.player", s.Status);
    });

    [Fact]
    public void BuildSceneFromScratch_Play_Save_Reload_PlayAgain() => WithHeroScene((s, path) =>
    {
        // 에디터 UI만으로 방/충돌체/트리거/플레이어를 만들고 플레이까지 한다(JSON 직접 편집 없음).
        s.Click("Add Room");
        s.TypeBlur(s.Field("Polygon (x, z per line)"), "-6 -6\n6 -6\n6 6\n-6 6");
        s.Click(s.PanelButton("Use view camera"));
        s.Click("Add Trigger");
        s.TypeBlur(s.Field("Polygon (x, z per line)"), "0 1\n1.9 1\n1.9 3\n0 3");
        s.TypeEnter(s.Field("Tag"), "9");
        s.Click("Add Collider");
        s.TypeEnter(s.Field("Max (x, z)"), "2.5, 3");  // 기본 상자(-0.5..0.5)보다 먼저 키워야 뒤집힘 검사를 통과한다
        s.TypeEnter(s.Field("Min (x, z)"), "2, 1");
        s.Select(s.Roots.Single());
        s.Click("Set Player");
        Assert.Equal(["Room: Room", "Collider: Collider", "Trigger: Trigger", "Player: Hero"], s.GameplayLabels());

        s.Click("Play");
        Assert.True(s.Window.IsPlaying, s.Status);
        Assert.Equal(new Vec2(1, 2), s.Window.CurrentPlay!.Position);
        for (int i = 0; i < 60; i++) s.Window.StepPlay(1, 0, 1 / 30f);
        Assert.True(s.Window.CurrentPlay.Position.X > 1.5f);  // 움직였다
        s.Click("Stop");

        s.Click("Save");
        Assert.StartsWith("Saved", s.Status);
        GameplayData saved = SceneSerializer.Load(path).Gameplay;
        Assert.Equal(new Vec2(1, 2), saved.Player!.Spawn);
        Assert.Equal(9, saved.Triggers.Single().Tag);
        Assert.Equal([2f, 1f, 2.5f, 3f], [saved.Colliders[0].MinX, saved.Colliders[0].MinZ, saved.Colliders[0].MaxX, saved.Colliders[0].MaxZ]);

        // 다시 열면 목록이 복원되고 플레이도 된다.
        s.Click("New");
        Assert.Empty(s.GameplayLabels());
        Assert.True(s.Window.OpenScenePath(path));
        s.Pump();
        Assert.Equal(["Room: Room", "Collider: Collider", "Trigger: Trigger", "Player: Hero"], s.GameplayLabels());
        s.Click("Play");
        Assert.True(s.Window.IsPlaying, s.Status);
        s.Window.StepPlay(0, 0, 1 / 30f);
        Assert.Equal(["Enter 'Trigger' (tag 9)"], s.Window.PlayLog);  // 스폰(1, 2)이 편집한 트리거 안이라 첫 Step에 Enter
    });

    [Fact]
    public void GameplayEditingIsLockedWhilePlaying_AndRestoredAfterStop() => WithHeroScene((s, _) =>
    {
        s.Click("Add Room");
        s.Select(s.Roots.Single());
        s.Click("Set Player");
        s.SelectGameplay("Room: Room");

        s.Click("Play");
        Assert.True(s.Window.IsPlaying, s.Status);
        Assert.False(s.ButtonByLabel("Add Room").IsEnabled);
        Assert.False(s.ButtonByLabel("Delete Item").IsEnabled);
        Assert.False(s.GameplayList.IsEnabled);
        Assert.False(s.GameplayPanel.IsEnabled);

        s.Click("Stop");
        Assert.True(s.ButtonByLabel("Add Room").IsEnabled);
        Assert.True(s.GameplayList.IsEnabled);
        Assert.True(s.GameplayPanel.IsEnabled);  // 방이 선택된 채였으므로 인스펙터가 다시 활성
        Assert.Equal("Room", s.Field("Name").Text);
    });

    [Fact]
    public void OpeningAScene_ResetsGameplayListAndInspector() => WithHeroScene((s, path) =>
    {
        s.Click("Add Room");
        Assert.Single(s.GameplayLabels());

        s.Click("New");

        Assert.Empty(s.GameplayLabels());
        Assert.Null(s.SelectedGameplay);
        Assert.False(s.GameplayPanel.IsEnabled);
        Assert.Equal(Visibility.Visible, s.Inspector.Visibility);
    });

    // ---- 7단계: 노드 알파/그림자, 조명 항목, 점광원 ----
    [Fact]
    public void NodeInspector_EditsAlphaAndCastsShadow_AndRejectsBadAlpha() => WithHeroScene((s, _) =>
    {
        SceneNode hero = s.Roots.Single();
        s.Select(hero);
        byte[] opaque = s.ViewportPixels();

        s.TypeEnter(s.Box(EditorSession.AlphaBox), "0.4");
        Assert.Equal(0.4f, hero.Alpha);
        Assert.NotEqual(opaque, s.ViewportPixels());                // 화면에 바로 반영된다

        foreach (string bad in new[] { "2", "-0.1", "abc" })         // 범위 밖/숫자 아님은 이전 값으로 되돌린다
        {
            s.TypeEnter(s.Box(EditorSession.AlphaBox), bad);
            Assert.Equal(0.4f, hero.Alpha);
            Assert.Equal("0.4", s.Box(EditorSession.AlphaBox).Text);
        }

        s.TypeEnter(s.Box(EditorSession.AlphaBox), "0");             // 경계: 완전 투명 = 그리지 않음
        Assert.Equal(0f, hero.Alpha);
        s.TypeEnter(s.Box(EditorSession.AlphaBox), "1");
        Assert.Equal(opaque, s.ViewportPixels());

        CheckBox casts = s.Find<CheckBox>(s.Inspector, c => c.Content as string == "Casts shadow");
        Assert.False(casts.IsChecked);
        s.Toggle(casts);
        Assert.True(hero.CastsShadow);
        s.Toggle(casts);
        Assert.False(hero.CastsShadow);

        hero.Alpha = 0.25f;  // 코드로 바꾼 값도 인스펙터 칸에 반영된다
        Assert.Equal("0.25", s.Box(EditorSession.AlphaBox).Text);
    });

    [Fact]
    public void LightingItem_IsFirstInTheList_AndItsEditsReachTheScreenAndDocument() => WithHeroScene((s, _) =>
    {
        Assert.Equal(EditorSession.LightingLabel, s.AllListLabels()[0]);
        s.SelectGameplay(EditorSession.LightingLabel);
        var lighting = Assert.IsType<LightingData>(s.SelectedGameplay!.Def);
        Assert.Equal("0.25, 0.25, 0.25", s.Field("Ambient (r, g, b)").Text);
        byte[] before = s.ViewportPixels();

        s.TypeEnter(s.Field("Ambient (r, g, b)"), "1, 0.5, 0");
        Assert.Equal(new Vec3(1, 0.5f, 0), lighting.Ambient);
        Assert.NotEqual(before, s.ViewportPixels());

        s.TypeEnter(s.Field("Ambient (r, g, b)"), "-1, 0, 0");
        Assert.Equal("Rejected: ambient must not be negative", s.Status);
        Assert.Equal(new Vec3(1, 0.5f, 0), lighting.Ambient);
        s.TypeEnter(s.Field("Sun direction (x, y, z)"), "0, 0, 0");
        Assert.Equal("Rejected: sun direction must not be zero", s.Status);
        s.TypeEnter(s.Field("Sun color (r, g, b)"), "1, 1, -1");
        Assert.StartsWith("Rejected: sun color", s.Status);
        s.TypeEnter(s.Field("Shadow opacity (0-1)"), "1.5");
        Assert.StartsWith("Rejected: shadow opacity", s.Status);

        s.TypeEnter(s.Field("Sun direction (x, y, z)"), "0, -1, 0");
        s.TypeEnter(s.Field("Sun color (r, g, b)"), "0.5, 0.5, 0.5");
        s.TypeEnter(s.Field("Shadow plane Y"), "-0.5");
        s.TypeEnter(s.Field("Shadow opacity (0-1)"), "0.8");
        s.Toggle(s.Find<CheckBox>(s.GameplayPanel, c => c.Content as string == "Shadows"));
        Assert.Equal((new Vec3(0, -1, 0), new Vec3(0.5f, 0.5f, 0.5f), -0.5f, 0.8f, true),
                     (lighting.SunDirection, lighting.SunColor, lighting.ShadowPlaneY, lighting.ShadowOpacity, lighting.ShadowEnabled));
    });

    [Fact]
    public void AddLight_PlacesItAboveTheViewTarget_AppliesIt_AndDeleteRemovesIt() => WithHeroScene((s, _) =>
    {
        Vec3 target = s.Window.Camera.Target;
        byte[] before = s.ViewportPixels();

        s.Click("Add Light");

        Assert.Equal(["Light: Light"], s.GameplayLabels());
        var light = Assert.IsType<PointLightDef>(s.SelectedGameplay!.Def);
        Assert.Equal(new Vec3(target.X, target.Y + 2, target.Z), light.Position);
        Assert.NotEqual(before, s.ViewportPixels());                 // 점광원이 바로 장면을 밝힌다
        Assert.False(s.CheckByLabel("Show gameplay").IsChecked);     // 점광원은 영역 오버레이를 켜지 않는다

        s.TypeEnter(s.Field("Color (r, g, b)"), "1, 0, 0");
        s.TypeEnter(s.Field("Range"), "12");
        s.TypeEnter(s.Field("Position (x, y, z)"), "1, 3, 1");
        s.TypeEnter(s.Field("Name"), "Lamp");
        Assert.Equal((new Vec3(1, 0, 0), 12f, new Vec3(1, 3, 1), "Lamp"), (light.Color, light.Range, light.Position, light.Name));
        Assert.Equal(["Light: Lamp"], s.GameplayLabels());

        s.TypeEnter(s.Field("Range"), "0");
        Assert.Equal("Rejected: range must be greater than 0", s.Status);
        s.TypeEnter(s.Field("Color (r, g, b)"), "1, -1, 0");
        Assert.StartsWith("Rejected: light color", s.Status);
        Assert.Equal(12f, light.Range);

        byte[] lit = s.ViewportPixels();
        s.Click("Delete Item");
        Assert.Empty(s.GameplayLabels());
        Assert.NotEqual(lit, s.ViewportPixels());                    // 삭제하면 엔진에서도 빠진다
        Assert.Equal(before, s.ViewportPixels());                    // 원래 화면으로 돌아온다

        s.SelectGameplay(EditorSession.LightingLabel);
        s.Click("Delete Item");
        Assert.StartsWith("The scene lighting cannot be deleted", s.Status);
        Assert.Equal(EditorSession.LightingLabel, s.AllListLabels()[0]);
    });

    [Fact]
    public void AddLight_StopsAtTheEngineLimit() => Ui(s =>
    {
        for (int i = 0; i < LightingData.MaxPointLights; i++)
            s.Click("Add Light");
        Assert.Equal(LightingData.MaxPointLights, s.GameplayLabels().Count);
        Assert.Equal(["Light: Light", "Light: Light 2"], s.GameplayLabels().Take(2));

        s.Click("Add Light");
        Assert.Equal($"At most {LightingData.MaxPointLights} point lights are supported", s.Status);
        Assert.Equal(LightingData.MaxPointLights, s.GameplayLabels().Count);
    });

    [Fact]
    public void LightingAndNodeFlags_SurviveSaveAndReopen() => WithHeroScene((s, path) =>
    {
        SceneNode hero = s.Roots.Single();
        hero.Alpha = 0.6f;
        hero.CastsShadow = true;
        s.Click("Add Light");
        s.TypeEnter(s.Field("Color (r, g, b)"), "0.2, 0.4, 1");
        s.SelectGameplay(EditorSession.LightingLabel);
        s.TypeEnter(s.Field("Ambient (r, g, b)"), "0.3, 0.3, 0.4");
        s.Toggle(s.Find<CheckBox>(s.GameplayPanel, c => c.Content as string == "Shadows"));
        s.Click("Save");
        Assert.StartsWith("Saved", s.Status);

        s.Click("New");
        Assert.Empty(s.GameplayLabels());
        Assert.True(s.Window.OpenScenePath(path));
        s.Pump();

        Assert.Equal(["Light: Light"], s.GameplayLabels());
        SceneNode reopened = s.Roots.Single();
        Assert.Equal((0.6f, true), (reopened.Alpha, reopened.CastsShadow));
        s.SelectGameplay(EditorSession.LightingLabel);
        var lighting = (LightingData)s.SelectedGameplay!.Def;
        Assert.Equal((new Vec3(0.3f, 0.3f, 0.4f), true), (lighting.Ambient, lighting.ShadowEnabled));
        Assert.Equal(new Vec3(0.2f, 0.4f, 1f), lighting.PointLights.Single().Color);
    });

    [Fact]
    public void LightingEditingIsLockedWhilePlaying() => WithHeroScene((s, _) =>
    {
        s.Click("Add Room");
        s.Select(s.Roots.Single());
        s.Click("Set Player");
        s.Click("Play");
        Assert.True(s.Window.IsPlaying, s.Status);
        Assert.False(s.ButtonByLabel("Add Light").IsEnabled);
        s.Click("Stop");
        Assert.True(s.ButtonByLabel("Add Light").IsEnabled);
    });
}
