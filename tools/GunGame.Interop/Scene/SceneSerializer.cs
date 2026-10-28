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

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
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
        public List<NodeDto>? Children { get; set; }
    }

    public static void Save(SceneDocument document, string path)
    {
        string baseDir = Path.GetDirectoryName(Path.GetFullPath(path))!;
        var file = new SceneFile { Version = CurrentVersion, Nodes = document.Roots.Select(n => ToDto(n, baseDir)).ToList() };
        File.WriteAllText(path, JsonSerializer.Serialize(file, Options));
    }

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
        foreach (NodeDto dto in file.Nodes)
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
        Children = node.Children.Count == 0 ? null : node.Children.Select(c => ToDto(c, baseDir)).ToList(),
    };

    private static SceneNode FromDto(NodeDto dto, string baseDir, int depth)
    {
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

        foreach (NodeDto childDto in dto.Children ?? [])
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

    private static string? ToAbsolute(string? path, string baseDir) =>
        string.IsNullOrEmpty(path) ? null : Path.GetFullPath(Path.Combine(baseDir, path));
}
