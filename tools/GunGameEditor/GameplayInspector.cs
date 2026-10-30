using System.Globalization;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Input;
using GunGame.Interop;
using GunGame.Scene;

namespace GunGameEditor;

/// <summary>게임플레이 항목의 종류(방/충돌체/트리거/플레이어).</summary>
public enum GameplayKind { Room, Collider, Trigger, Player, Lighting, PointLight }

/// <summary>게임플레이 목록의 한 항목: 종류와 원본 데이터(RoomDef/ColliderDef/TriggerDef/PlayerDef/LightingData/PointLightDef)를 가리킨다.</summary>
public sealed record GameplayItem(GameplayKind Kind, object Def)
{
    /// <summary>목록에 보이는 글자: "Room: Hall" 형태. Player는 연결된 노드 이름을 보인다.</summary>
    public string Display => Kind switch
    {
        GameplayKind.Room => "Room: " + ((RoomDef)Def).Name,
        GameplayKind.Collider => "Collider: " + ((ColliderDef)Def).Name,
        GameplayKind.Trigger => "Trigger: " + ((TriggerDef)Def).Name,
        GameplayKind.Lighting => "Lighting: Scene",
        GameplayKind.PointLight => "Light: " + ((PointLightDef)Def).Name,
        _ => "Player: " + ((PlayerDef)Def).Node,
    };
}

/// <summary>
/// 선택한 게임플레이 항목(방/충돌체/트리거/플레이어)의 값을 편집하는 패널. 값은 Enter(한 줄 칸) 또는 포커스를 잃을 때 반영된다.
/// 잘못된 입력은 거부하고 이전 값으로 되돌리며 Rejected로 사유를 알린다. 값이 바뀌면 Changed가 발생한다(목록/오버레이 갱신용).
/// 칸은 AutomationProperties.Name에 라벨을 달아 두어 자동 테스트가 순서가 아닌 이름으로 찾는다.
/// Owner: 에디터 창이 소유한다. UI 스레드에서만 사용한다.
/// </summary>
public sealed class GameplayInspector : StackPanel
{
    private const string MoveLabel = "Move area by (dx, dz)";
    private const string ViewCameraLabel = "Use view camera";

    private readonly List<(TextBox Box, Func<string> Get)> _fields = [];

    /// <summary>값이 바뀌었다(이름 포함). 목록 표시와 오버레이를 다시 만들 때 쓴다.</summary>
    public event Action? Changed;

    /// <summary>입력이 거부되었다. 인자는 사유.</summary>
    public event Action<string>? Rejected;

    /// <summary>방 카메라로 쓸 현재 뷰 카메라(눈, 목표, 시야각)를 돌려주는 함수(에디터가 제공).</summary>
    public Func<(Vec3 Eye, Vec3 Target, float Fov)>? ViewCameraProvider { get; set; }

    public GameplayInspector()
    {
        Margin = new Thickness(8);
        Show(null);
    }

    /// <summary>item의 편집 칸을 만든다. null이면 비우고 비활성화한다.</summary>
    public void Show(GameplayItem? item)
    {
        Children.Clear();
        _fields.Clear();
        IsEnabled = item is not null;
        if (item is null)
            return;

        switch (item.Kind)
        {
            case GameplayKind.Room:
                BuildRoom((RoomDef)item.Def);
                break;
            case GameplayKind.Collider:
                BuildCollider((ColliderDef)item.Def);
                break;
            case GameplayKind.Trigger:
                BuildTrigger((TriggerDef)item.Def);
                break;
            case GameplayKind.Lighting:
                BuildLighting((LightingData)item.Def);
                break;
            case GameplayKind.PointLight:
                BuildPointLight((PointLightDef)item.Def);
                break;
            default:
                BuildPlayer((PlayerDef)item.Def);
                break;
        }
    }

    // ---- 종류별 칸 ----
    private void BuildRoom(RoomDef room)
    {
        AddText("Name", () => room.Name, v => SetName(v, n => room.Name = n));
        AddPolygon(() => room.Polygon, p => room.Polygon = p);
        AddNumbers("Eye (x, y, z)", 3, () => Fmt(room.Eye.X, room.Eye.Y, room.Eye.Z), v => room.Eye = new Vec3(v[0], v[1], v[2]));
        AddNumbers("Target (x, y, z)", 3, () => Fmt(room.Target.X, room.Target.Y, room.Target.Z), v => room.Target = new Vec3(v[0], v[1], v[2]));
        AddNumbers("Fov (radians)", 1, () => Fmt(room.Fov), v => v[0] > 0 && v[0] < MathF.PI ? null : "fov must be between 0 and pi", v => room.Fov = v[0]);

        var useView = new Button { Content = ViewCameraLabel, Margin = new Thickness(0, 6, 0, 0) };
        useView.Click += (_, _) =>
        {
            if (ViewCameraProvider is not { } provider)
                return;
            (room.Eye, room.Target, room.Fov) = provider();
            Refresh();
            Changed?.Invoke();
        };
        Children.Add(useView);
        AddMove((dx, dz) => room.Polygon = GameplayEditing.Translate(room.Polygon, dx, dz));
    }

    private void BuildCollider(ColliderDef c)
    {
        AddText("Name", () => c.Name, v => SetName(v, n => c.Name = n));
        // 최소/최대를 한 칸씩 고치다 보면 잠깐 뒤집히므로, 뒤집힌 값은 거부한다(옮길 때는 아래 Move를 쓴다).
        AddNumbers("Min (x, z)", 2, () => Fmt(c.MinX, c.MinZ),
            v => v[0] < c.MaxX && v[1] < c.MaxZ ? null : "min must be less than max", v => { c.MinX = v[0]; c.MinZ = v[1]; });
        AddNumbers("Max (x, z)", 2, () => Fmt(c.MaxX, c.MaxZ),
            v => v[0] > c.MinX && v[1] > c.MinZ ? null : "max must be greater than min", v => { c.MaxX = v[0]; c.MaxZ = v[1]; });
        AddMove((dx, dz) => GameplayEditing.Translate(c, dx, dz));
    }

    private void BuildTrigger(TriggerDef t)
    {
        AddText("Name", () => t.Name, v => SetName(v, n => t.Name = n));
        AddText("Tag", () => t.Tag.ToString(CultureInfo.InvariantCulture),
            v => int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out int tag) ? Apply(() => t.Tag = tag) : "tag must be an integer");
        AddPolygon(() => t.Polygon, p => t.Polygon = p);
        AddMove((dx, dz) => t.Polygon = GameplayEditing.Translate(t.Polygon, dx, dz));
    }

    private void BuildLighting(LightingData l)
    {
        const string ColorRule = " must not be negative";
        AddNumbers("Ambient (r, g, b)", 3, () => Fmt(l.Ambient.X, l.Ambient.Y, l.Ambient.Z),
            v => v.Any(x => x < 0) ? "ambient" + ColorRule : null, v => l.Ambient = new Vec3(v[0], v[1], v[2]));
        AddNumbers("Sun direction (x, y, z)", 3, () => Fmt(l.SunDirection.X, l.SunDirection.Y, l.SunDirection.Z),
            v => v.All(x => x == 0) ? "sun direction must not be zero" : null, v => l.SunDirection = new Vec3(v[0], v[1], v[2]));
        AddNumbers("Sun color (r, g, b)", 3, () => Fmt(l.SunColor.X, l.SunColor.Y, l.SunColor.Z),
            v => v.Any(x => x < 0) ? "sun color" + ColorRule : null, v => l.SunColor = new Vec3(v[0], v[1], v[2]));
        AddCheck("Shadows", () => l.ShadowEnabled, on => l.ShadowEnabled = on);
        AddNumbers("Shadow plane Y", 1, () => Fmt(l.ShadowPlaneY), v => l.ShadowPlaneY = v[0]);
        AddNumbers("Shadow opacity (0-1)", 1, () => Fmt(l.ShadowOpacity),
            v => v[0] >= 0 && v[0] <= 1 ? null : "shadow opacity must be between 0 and 1", v => l.ShadowOpacity = v[0]);
    }

    private void BuildPointLight(PointLightDef p)
    {
        AddText("Name", () => p.Name, v => SetName(v, n => p.Name = n));
        AddNumbers("Position (x, y, z)", 3, () => Fmt(p.Position.X, p.Position.Y, p.Position.Z), v => p.Position = new Vec3(v[0], v[1], v[2]));
        AddNumbers("Color (r, g, b)", 3, () => Fmt(p.Color.X, p.Color.Y, p.Color.Z),
            v => v.Any(x => x < 0) ? "light color must not be negative" : null, v => p.Color = new Vec3(v[0], v[1], v[2]));
        AddNumbers("Range", 1, () => Fmt(p.Range), v => v[0] > 0 ? null : "range must be greater than 0", v => p.Range = v[0]);
    }

    private void BuildPlayer(PlayerDef p)
    {
        AddText("Node", () => p.Node, v => string.IsNullOrWhiteSpace(v) ? "node name is empty" : Apply(() => p.Node = v.Trim()));
        // 비우면 스폰을 지워 노드의 현재 위치에서 시작한다.
        AddNumbersOrEmpty("Spawn (x, z)", () => p.Spawn is { } s ? Fmt(s.X, s.Z) : "", v => p.Spawn = v.Length == 0 ? null : new Vec2(v[0], v[1]));
        AddNumbers("Radius", 1, () => Fmt(p.Radius), v => v[0] > 0 ? null : "radius must be greater than 0", v => p.Radius = v[0]);
        AddNumbers("Speed", 1, () => Fmt(p.Speed), v => v[0] > 0 ? null : "speed must be greater than 0", v => p.Speed = v[0]);
    }

    // ---- 칸 만들기 ----
    private static string Fmt(params float[] values) => string.Join(", ", values.Select(GameplayEditing.Format));

    private static string? Apply(Action assign)
    {
        assign();
        return null;
    }

    private static string? SetName(string value, Action<string> assign) =>
        string.IsNullOrWhiteSpace(value) ? "name is empty" : Apply(() => assign(value.Trim()));

    private void AddNumbers(string label, int count, Func<string> get, Action<float[]> assign) =>
        AddNumbers(label, count, get, _ => null, assign);

    /// <summary>count개의 숫자를 받는 칸. validate가 null이 아닌 사유를 돌려주면 거부한다.</summary>
    private void AddNumbers(string label, int count, Func<string> get, Func<float[], string?> validate, Action<float[]> assign) =>
        AddText(label, get, text =>
        {
            if (!GameplayEditing.TryParseNumbers(text, out float[] v) || v.Length != count)
                return $"{label}: enter {count} number(s)";
            return validate(v) ?? Apply(() => assign(v));
        });

    private void AddNumbersOrEmpty(string label, Func<string> get, Action<float[]> assign) =>
        AddText(label, get, text =>
        {
            if (!GameplayEditing.TryParseNumbers(text, out float[] v) || (v.Length != 0 && v.Length != 2))
                return $"{label}: enter 2 numbers or leave empty";
            return Apply(() => assign(v));
        });

    private void AddPolygon(Func<float[]> get, Action<float[]> assign) =>
        AddText("Polygon (x, z per line)", () => GameplayEditing.FormatPolygon(get()), text =>
            GameplayEditing.TryParsePolygon(text, out float[] polygon)
                ? Apply(() => assign(polygon))
                : "polygon needs at least 3 vertices of finite x, z numbers", multiline: true);

    private void AddMove(Action<float, float> move)
    {
        var box = new TextBox { Text = "0, 0", Margin = new Thickness(0, 0, 4, 0) };
        AutomationProperties.SetName(box, MoveLabel);
        var apply = new Button { Content = "Move", Margin = new Thickness(0, 4, 0, 0) };
        apply.Click += (_, _) =>
        {
            if (!GameplayEditing.TryParseNumbers(box.Text, out float[] v) || v.Length != 2)
            {
                Rejected?.Invoke($"{MoveLabel}: enter 2 numbers");
                return;
            }
            move(v[0], v[1]);
            box.Text = "0, 0";
            Refresh();
            Changed?.Invoke();
        };
        AddLabel(MoveLabel);
        Children.Add(box);
        Children.Add(apply);
    }

    /// <summary>
    /// 라벨과 입력 칸 하나. set은 성공하면 null, 거부하면 사유를 돌려준다. 칸이 보여 준 값 그대로(편집 안 함)이면 아무것도 하지 않는다
    /// (포커스만 옮겼을 때 값이 다시 쓰이거나 변경 알림이 가지 않게).
    /// </summary>
    private void AddText(string label, Func<string> get, Func<string, string?> set, bool multiline = false)
    {
        var box = new TextBox { Text = get() };
        AutomationProperties.SetName(box, label);
        if (multiline)
        {
            box.AcceptsReturn = true;
            box.MinHeight = 80;
            box.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
        }

        void Commit()
        {
            if (box.Text == get())
                return;
            string? error = set(box.Text);
            if (error is null)
                Changed?.Invoke();
            else
                Rejected?.Invoke(error);
            Refresh();  // 거부된 값은 이전 값으로 되돌리고, 받아들인 값은 정규화된 모양으로 다시 보인다
        }

        box.LostFocus += (_, _) => Commit();
        if (!multiline)
        {
            box.KeyDown += (_, e) =>
            {
                if (e.Key == Key.Enter)
                    Commit();
            };
        }
        else
        {
            // 여러 줄 칸은 Enter가 줄바꿈이라 포커스를 잃을 때만 반영한다.
        }
        AddLabel(label);
        Children.Add(box);
        _fields.Add((box, get));
    }

    /// <summary>체크박스 칸: 클릭하면 바로 반영하고 Changed를 알린다.</summary>
    private void AddCheck(string label, Func<bool> get, Action<bool> set)
    {
        var box = new CheckBox { Content = label, IsChecked = get(), Margin = new Thickness(0, 8, 0, 0) };
        AutomationProperties.SetName(box, label);
        box.Click += (_, _) =>
        {
            set(box.IsChecked == true);
            Changed?.Invoke();
        };
        Children.Add(box);
    }

    private void AddLabel(string label) =>
        Children.Add(new TextBlock { Text = label, Margin = new Thickness(0, 8, 0, 2), FontWeight = FontWeights.SemiBold });

    private void Refresh()
    {
        foreach ((TextBox box, Func<string> get) in _fields)
            box.Text = get();
    }
}
