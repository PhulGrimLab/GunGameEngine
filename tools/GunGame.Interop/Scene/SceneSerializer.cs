using System.Text.Json;
using System.Text.Json.Serialization;
using GunGame.Interop;

namespace GunGame.Scene;

public sealed class SceneFormatException(string message, Exception? inner = null) : Exception(message, inner);

/// <summary>
/// 씬 파일(JSON, 버전 1). 메시/텍스처 경로는 씬 파일 위치 기준 상대 경로(슬래시)로 저장하고,
/// 읽을 때 절대 경로로 풀어 준다.
/// </summary>
public static class SceneSerializer
{
    public const int CurrentVersion = 1;
    private const int MaxDepth = 64;
    // 노드 한 단계마다 JSON 깊이가 2(객체 + children 배열) 늘고, 최상위 객체와 nodes 배열이 4를 더한다.
    // System.Text.Json 기본 한도(64)면 약 31단계에서 저장/로드가 실패해 MaxDepth(64단계)가 의미가 없어진다.
    private const int JsonMaxDepth = 2 * MaxDepth + 8;

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        MaxDepth = JsonMaxDepth,
    };

    private sealed class SceneFile
    {
        public int Version { get; set; }
        public List<NodeDto>? Nodes { get; set; }
    }

    private sealed class NodeDto
    {
        public string? Name { get; set; }
        public string? Mesh { get; set; }
        public string? Texture { get; set; }
        public float[]? Position { get; set; }
        public float[]? Rotation { get; set; }
        public float[]? Scale { get; set; }
        public float[]? Color { get; set; }
        public string? Clip { get; set; }
        public bool? Playing { get; set; }
        public float? Speed { get; set; }
        public List<NodeDto>? Children { get; set; }
    }

    /// <summary>씬을 저장한다. 임시 파일에 먼저 쓰고 교체하므로 쓰는 도중 실패해도 기존 파일이 남는다.
    /// 계층이 한도(64단계)를 넘으면 파일을 건드리지 않고 SceneFormatException을 던진다.</summary>
    public static void Save(SceneDocument document, string path)
    {
        if (document.Roots.Any(root => DepthOf(root) > MaxDepth))
            throw new SceneFormatException("scene hierarchy is too deep");
        string baseDir = Path.GetDirectoryName(Path.GetFullPath(path))!;
        var file = new SceneFile { Version = CurrentVersion, Nodes = document.Roots.Select(n => ToDto(n, baseDir)).ToList() };
        string json = JsonSerializer.Serialize(file, Options);

        string temp = $"{path}.{Guid.NewGuid():N}.tmp";
        try
        {
            File.WriteAllText(temp, json);
            if (File.Exists(path))
                File.Replace(temp, path, null);  // 같은 폴더 안이라 같은 볼륨: 대상이 잠겨 있으면 IOException, 기존 파일은 그대로
            else
                File.Move(temp, path);
        }
        catch
        {
            try { File.Delete(temp); } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { /* 지우지 못해도 원래 오류를 우선한다 */ }
            throw;
        }
    }

    private static int DepthOf(SceneNode node) => 1 + (node.Children.Count == 0 ? 0 : node.Children.Max(DepthOf));

    public static SceneDocument Load(string path)
    {
        string baseDir = Path.GetDirectoryName(Path.GetFullPath(path))!;
        SceneFile? file;
        try
        {
            file = JsonSerializer.Deserialize<SceneFile>(File.ReadAllText(path), Options);
        }
        catch (JsonException ex)
        {
            throw new SceneFormatException("scene file is not valid JSON: " + ex.Message, ex);
        }

        if (file is null || file.Nodes is null)
            throw new SceneFormatException("scene file has no nodes");
        if (file.Version != CurrentVersion)
            throw new SceneFormatException($"unsupported scene version {file.Version} (expected {CurrentVersion})");

        var document = new SceneDocument();
        foreach (NodeDto? dto in file.Nodes)
            document.Add(FromDto(dto, baseDir, depth: 1), null);
        return document;
    }

    private static NodeDto ToDto(SceneNode node, string baseDir) => new()
    {
        Name = node.Name,
        Mesh = ToRelative(node.MeshPath, baseDir),
        Texture = ToRelative(node.TexturePath, baseDir),
        Position = ToArray(node.Position),
        Rotation = ToArray(node.Rotation),
        Scale = ToArray(node.Scale),
        Color = ToArray(node.Color),
        Clip = node.AnimationClip,
        Playing = node.AnimationClip is null ? null : node.AnimationPlaying,
        Speed = node.AnimationClip is null || node.AnimationSpeed == 1f ? null : node.AnimationSpeed,
        Children = node.Children.Count == 0 ? null : node.Children.Select(c => ToDto(c, baseDir)).ToList(),
    };

    private static SceneNode FromDto(NodeDto? dto, string baseDir, int depth)
    {
        if (dto is null)
            throw new SceneFormatException("scene contains a null node");
        if (depth > MaxDepth)
            throw new SceneFormatException("scene hierarchy is too deep");

        var node = new SceneNode
        {
            Name = dto.Name ?? "Node",
            MeshPath = ToAbsolute(dto.Mesh, baseDir),
            TexturePath = ToAbsolute(dto.Texture, baseDir),
        };
        if (dto.Position is not null) node.Position = ToVec3(dto.Position, "position");
        if (dto.Rotation is not null) node.Rotation = ToVec3(dto.Rotation, "rotation");
        if (dto.Scale is not null) node.Scale = ToVec3(dto.Scale, "scale");
        if (dto.Color is not null) node.Color = ToVec3(dto.Color, "color");
        node.AnimationClip = dto.Clip;
        if (dto.Playing is { } playing) node.AnimationPlaying = playing;
        if (dto.Speed is { } speed)
            node.AnimationSpeed = float.IsFinite(speed) ? speed : throw new SceneFormatException("'speed' must be a finite number");

        foreach (NodeDto? childDto in dto.Children ?? [])
        {
            SceneNode child = FromDto(childDto, baseDir, depth + 1);
            node.Children.Add(child);
            child.Parent = node;
        }
        return node;
    }

    private static float[] ToArray(Vec3 v) => [v.X, v.Y, v.Z];

    private static Vec3 ToVec3(float[] values, string field)
    {
        if (values.Length != 3 || values.Any(v => !float.IsFinite(v)))
            throw new SceneFormatException($"'{field}' must be three finite numbers");
        return new Vec3(values[0], values[1], values[2]);
    }

    private static string? ToRelative(string? path, string baseDir) =>
        string.IsNullOrEmpty(path) ? null : Path.GetRelativePath(baseDir, path).Replace('\\', '/');

    private static string? ToAbsolute(string? path, string baseDir)
    {
        if (string.IsNullOrEmpty(path))
            return null;
        try
        {
            return Path.GetFullPath(Path.Combine(baseDir, path));
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            // 경로 문자열이 잘못된 경우(예: 문자열 안의 NUL). 파일 형식 오류로 알려 열기 실패 메시지가 되게 한다.
            throw new SceneFormatException($"scene file contains an invalid path: '{path}'", ex);
        }
    }
}
