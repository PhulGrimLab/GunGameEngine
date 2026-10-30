using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;
using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>
/// 게임플레이 데이터(방/충돌체/트리거/플레이어)를 만들고 고치는 순수 로직. WPF에 의존하지 않아 에디터 UI와 별개로 테스트한다.
/// 새 항목은 보고 있는 위치(카메라 목표점의 바닥 좌표) 주변에 기본 크기로 만들어, 사용자가 숫자로 다듬는다.
/// 다각형은 (x, z) 쌍의 float 배열이며 텍스트로는 줄마다 "x, z"로 표현한다.
/// 이 클래스는 상태가 없다(모든 메서드가 인자로 받은 데이터만 바꾼다).
/// </summary>
public static partial class GameplayEditing
{
    /// <summary>새 방의 기본 다각형 크기(가로 x 세로, 월드 단위).</summary>
    public const float DefaultRoomWidth = 8f, DefaultRoomDepth = 6f;
    public const float DefaultColliderSize = 1f;
    public const float DefaultTriggerSize = 2f;

    /// <summary>다각형은 최소 꼭짓점 3개가 있어야 면적이 생긴다.</summary>
    public const int MinPolygonVertices = 3;

    private const int FloatsPerVertex = 2;

    [GeneratedRegex(@"[\s,;]+")]
    private static partial Regex Separators();

    /// <summary>방을 추가한다. 다각형은 카메라 목표점 (x, z) 주변의 사각형, 카메라는 주어진 값이다.</summary>
    public static RoomDef AddRoom(GameplayData data, Vec3 eye, Vec3 target, float fov)
    {
        var room = new RoomDef
        {
            Name = UniqueName(data.Rooms.Select(r => r.Name), "Room"),
            Polygon = Rectangle(target.X, target.Z, DefaultRoomWidth, DefaultRoomDepth),
            Eye = eye,
            Target = target,
            Fov = fov,
        };
        data.Rooms.Add(room);
        return room;
    }

    /// <summary>충돌체를 (centerX, centerZ) 주변 정사각형으로 추가한다.</summary>
    public static ColliderDef AddCollider(GameplayData data, float centerX, float centerZ)
    {
        float half = DefaultColliderSize / 2;
        var collider = new ColliderDef
        {
            Name = UniqueName(data.Colliders.Select(c => c.Name), "Collider"),
            MinX = centerX - half,
            MinZ = centerZ - half,
            MaxX = centerX + half,
            MaxZ = centerZ + half,
        };
        data.Colliders.Add(collider);
        return collider;
    }

    /// <summary>트리거를 (centerX, centerZ) 주변 정사각형으로 추가한다. Tag는 기존 최댓값 + 1(겹치지 않는 식별자 기본값).</summary>
    public static TriggerDef AddTrigger(GameplayData data, float centerX, float centerZ)
    {
        var trigger = new TriggerDef
        {
            Name = UniqueName(data.Triggers.Select(t => t.Name), "Trigger"),
            Polygon = Rectangle(centerX, centerZ, DefaultTriggerSize, DefaultTriggerSize),
            Tag = data.Triggers.Count == 0 ? 1 : data.Triggers.Max(t => t.Tag) + 1,
        };
        data.Triggers.Add(trigger);
        return trigger;
    }

    /// <summary>
    /// node를 플레이어로 지정한다: 스폰은 노드의 현재 (x, z). 이미 플레이어가 있으면 반지름/속도는 유지한다.
    /// 루트가 아닌 노드는 플레이 시작에서 거부되므로 여기서 미리 거부한다.
    /// </summary>
    /// <exception cref="InvalidOperationException">node가 루트가 아닐 때.</exception>
    public static void SetPlayer(GameplayData data, SceneNode node)
    {
        if (node.Parent is not null)
            throw new InvalidOperationException("the player node must be a root node");
        PlayerDef player = data.Player ?? new PlayerDef();
        player.Node = node.Name;
        player.Spawn = new Vec2(node.Position.X, node.Position.Z);
        data.Player = player;
    }

    /// <summary>중심 (cx, cz), 가로(x) width, 세로(z) depth인 사각형 다각형.</summary>
    public static float[] Rectangle(float cx, float cz, float width, float depth)
    {
        float hx = width / 2, hz = depth / 2;
        return [cx - hx, cz - hz, cx + hx, cz - hz, cx + hx, cz + hz, cx - hx, cz + hz];
    }

    /// <summary>prefix, prefix 2, prefix 3... 중 names에 없는 첫 이름.</summary>
    public static string UniqueName(IEnumerable<string> names, string prefix)
    {
        var used = new HashSet<string>(names);
        if (!used.Contains(prefix))
            return prefix;
        for (int i = 2; ; i++)
        {
            string candidate = $"{prefix} {i}";
            if (!used.Contains(candidate))
                return candidate;
        }
    }

    /// <summary>다각형을 줄마다 "x, z"로 만든다(float의 최단 왕복 표현이라 다시 읽어도 값이 같다).</summary>
    public static string FormatPolygon(float[] polygon)
    {
        var text = new StringBuilder();
        for (int i = 0; i + 1 < polygon.Length; i += FloatsPerVertex)
        {
            if (i > 0)
                text.Append(Environment.NewLine);
            text.Append(Format(polygon[i])).Append(", ").Append(Format(polygon[i + 1]));
        }
        return text.ToString();
    }

    /// <summary>
    /// 텍스트를 다각형으로 읽는다. 구분자는 공백/쉼표/세미콜론/줄바꿈 아무거나. 숫자 개수가 짝수이고 꼭짓점이 3개 이상이며
    /// 모든 값이 유한해야 한다. 아니면 false(polygon은 빈 배열). 오목/감김 방향은 검사하지 않는다(엔진이 허용하는 범위).
    /// </summary>
    public static bool TryParsePolygon(string text, out float[] polygon)
    {
        polygon = [];
        if (!TryParseNumbers(text, out float[] numbers) ||
            numbers.Length % FloatsPerVertex != 0 ||
            numbers.Length / FloatsPerVertex < MinPolygonVertices)
        {
            return false;
        }
        polygon = numbers;
        return true;
    }

    /// <summary>구분자로 나뉜 유한한 숫자들을 읽는다. 하나라도 숫자가 아니거나 유한하지 않으면 false. 빈 텍스트는 빈 배열로 성공.</summary>
    public static bool TryParseNumbers(string text, out float[] numbers)
    {
        numbers = [];
        string[] tokens = Separators().Split(text.Trim()).Where(t => t.Length > 0).ToArray();
        var values = new float[tokens.Length];
        for (int i = 0; i < tokens.Length; i++)
        {
            if (!float.TryParse(tokens[i], NumberStyles.Float, CultureInfo.InvariantCulture, out values[i]) || !float.IsFinite(values[i]))
                return false;
        }
        numbers = values;
        return true;
    }

    /// <summary>다각형 전체를 (dx, dz)만큼 옮긴 새 배열.</summary>
    public static float[] Translate(float[] polygon, float dx, float dz)
    {
        var moved = new float[polygon.Length];
        for (int i = 0; i + 1 < polygon.Length; i += FloatsPerVertex)
        {
            moved[i] = polygon[i] + dx;
            moved[i + 1] = polygon[i + 1] + dz;
        }
        return moved;
    }

    /// <summary>충돌체를 (dx, dz)만큼 옮긴다.</summary>
    public static void Translate(ColliderDef collider, float dx, float dz)
    {
        collider.MinX += dx;
        collider.MaxX += dx;
        collider.MinZ += dz;
        collider.MaxZ += dz;
    }

    /// <summary>float를 최단 왕복 표현(InvariantCulture)으로 문자열화한다.</summary>
    public static string Format(float value) => value.ToString(CultureInfo.InvariantCulture);
}
