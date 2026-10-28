using System.ComponentModel;
using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>
/// 씬 문서를 엔진 오브젝트 그래프에 반영한다. 구조가 바뀌면 Rebuild, 노드 속성이 바뀌면 자동으로 Update된다.
/// 같은 경로의 메시/텍스처는 한 번만 로드하고(.obj/.ggm/.glb/.gltf), 스켈레탈 애니메이션은 Tick으로 진행시킨다.
/// </summary>
public sealed class SceneSync : IDisposable
{
    /// <summary>애니메이션 전환 시 이전 클립에서 새 클립으로 섞는 시간(초).</summary>
    public const float CrossfadeSeconds = 0.2f;

    private sealed class LoadedMesh
    {
        public int MeshId;
        public int SkeletonId;
        public Dictionary<string, ClipRef> Clips = [];
        public List<string> ClipNames = [];
    }

    private sealed class Entry
    {
        public int ObjectId;
        public string? MeshPath;
        public string? TexturePath;
        public LoadedMesh? Mesh;
    }

    private readonly EngineRenderer _engine;
    private readonly Dictionary<string, LoadedMesh> _meshes = [];
    private readonly Dictionary<string, int> _textures = [];
    private readonly Dictionary<SceneNode, Entry> _entries = [];
    private readonly Dictionary<SceneNode, AnimationPlayer> _players = [];
    private readonly List<int> _rootObjects = [];
    private readonly List<string> _errors = [];
    private readonly List<string> _warnings = [];
    private SceneDocument? _document;

    public SceneSync(EngineRenderer engine) => _engine = engine;

    /// <summary>마지막 Rebuild/Update 중 로드에 실패한 항목 설명.</summary>
    public IReadOnlyList<string> Errors => _errors;

    /// <summary>로드는 됐지만 일부가 무시된 항목(예: 지원하지 않는 glTF 기능).</summary>
    public IReadOnlyList<string> Warnings => _warnings;

    /// <summary>엔진 오브젝트가 갱신되어 다시 그려야 할 때 발생한다.</summary>
    public event Action? Changed;

    public void Attach(SceneDocument document)
    {
        if (_document is not null)
            _document.StructureChanged -= OnStructureChanged;
        if (!ReferenceEquals(_document, document))
            _players.Clear();
        _document = document;
        document.StructureChanged += OnStructureChanged;
        Rebuild();
    }

    /// <summary>노드의 메시가 가진 애니메이션 클립 이름들(없으면 빈 목록).</summary>
    public IReadOnlyList<string> GetClipNames(SceneNode node) =>
        _entries.TryGetValue(node, out Entry? entry) && entry.Mesh is { } mesh ? mesh.ClipNames : [];

    /// <summary>노드의 현재 재생 시간(초). 애니메이션이 없으면 null.</summary>
    public float? GetAnimationTime(SceneNode node) => _players.TryGetValue(node, out AnimationPlayer? p) && p.Current is not null ? p.Time : null;

    public void Rebuild()
    {
        _errors.Clear();
        _warnings.Clear();
        foreach (SceneNode node in _entries.Keys)
            node.PropertyChanged -= OnNodePropertyChanged;
        foreach (int id in _rootObjects)
            _engine.DestroyObject(id);
        _rootObjects.Clear();
        _entries.Clear();

        if (_document is not null)
            foreach (SceneNode root in _document.Roots)
                Create(root, 0);
        foreach (SceneNode stale in _players.Keys.Where(n => !_entries.ContainsKey(n)).ToList())
            _players.Remove(stale);
        Changed?.Invoke();
    }

    /// <summary>재생 중인 애니메이션을 dt초만큼 진행시키고 엔진에 반영한다.</summary>
    public void Tick(float dt)
    {
        bool any = false;
        foreach ((SceneNode node, AnimationPlayer player) in _players)
        {
            if (!player.Playing || player.Current is null || !_entries.TryGetValue(node, out Entry? entry))
                continue;
            player.Update(dt);
            PushPose(entry, player);
            any = true;
        }
        if (any)
            Changed?.Invoke();
    }

    private void Create(SceneNode node, int parentObjectId)
    {
        LoadedMesh? mesh = LoadMesh(node.MeshPath);
        int texture = LoadTexture(node.TexturePath);

        int objectId = _engine.CreateObject(mesh?.MeshId ?? 0, texture, parentObjectId);
        if (parentObjectId == 0)
            _rootObjects.Add(objectId);
        var entry = new Entry { ObjectId = objectId, MeshPath = node.MeshPath, TexturePath = node.TexturePath, Mesh = mesh };
        _entries[node] = entry;
        node.PropertyChanged += OnNodePropertyChanged;
        ApplyTransform(node, objectId);
        if (mesh is { SkeletonId: > 0 })
            _engine.SetObjectSkeleton(objectId, mesh.SkeletonId);
        ApplyAnimation(node, entry);

        foreach (SceneNode child in node.Children)
            Create(child, objectId);
    }

    // ---- 리소스 로드 (실패는 캐시하지 않아 파일을 고친 뒤 다시 시도할 수 있다) ----
    private LoadedMesh? LoadMesh(string? path)
    {
        if (string.IsNullOrEmpty(path))
            return null;
        if (_meshes.TryGetValue(path, out LoadedMesh? cached))
            return cached;
        try
        {
            string ext = Path.GetExtension(path).ToLowerInvariant();
            LoadedMesh mesh = ext switch
            {
                ".glb" or ".gltf" => LoadGltf(path),
                ".ggm" => new LoadedMesh { MeshId = _engine.LoadMeshGgm(path) },
                _ => new LoadedMesh { MeshId = _engine.LoadMeshObj(path) },
            };
            return _meshes[path] = mesh;
        }
        catch (Exception ex) when (ex is EngineException or GltfException or IOException)
        {
            _errors.Add($"{path}: {ex.Message}");
            return null;
        }
    }

    private LoadedMesh LoadGltf(string path)
    {
        ImportedModel model = GltfImporter.Import(path);
        foreach (string warning in model.Warnings)
            _warnings.Add($"{path}: {warning}");

        var loaded = new LoadedMesh
        {
            MeshId = model.Joints is null
                ? _engine.CreateMesh(model.Vertices, model.Indices)
                : _engine.CreateSkinnedMesh(model.Vertices, model.Joints, model.Weights!, model.Indices),
            SkeletonId = model.Skeleton is null ? 0 : _engine.CreateSkeleton(model.Skeleton),
        };
        foreach (ClipData clip in model.Clips)
        {
            loaded.Clips[clip.Name] = new ClipRef(_engine.CreateClip(clip), clip.Duration);
            loaded.ClipNames.Add(clip.Name);
        }
        return loaded;
    }

    private int LoadTexture(string? path)
    {
        if (string.IsNullOrEmpty(path))
            return 0;
        if (_textures.TryGetValue(path, out int cached))
            return cached;
        try
        {
            return _textures[path] = _engine.LoadTexture(path);
        }
        catch (EngineException ex)
        {
            _errors.Add($"{path}: {ex.Message}");
            return 0;
        }
    }

    // ---- 노드 상태 적용 ----
    private void ApplyTransform(SceneNode node, int objectId)
    {
        _engine.SetObjectTransform(objectId, node.Position, node.Rotation, node.Scale);
        _engine.SetObjectColor(objectId, node.Color);
    }

    private void ApplyAnimation(SceneNode node, Entry entry)
    {
        if (entry.Mesh is not { SkeletonId: > 0 } mesh)
        {
            _players.Remove(node);
            return;
        }

        if (!_players.TryGetValue(node, out AnimationPlayer? player))
            _players[node] = player = new AnimationPlayer();
        player.Playing = node.AnimationPlaying;
        player.Speed = node.AnimationSpeed;

        if (node.AnimationClip is null)
            player.Clear();
        else if (mesh.Clips.TryGetValue(node.AnimationClip, out ClipRef clip))
            player.Play(clip, player.Current is null ? 0 : CrossfadeSeconds);
        else
        {
            _errors.Add($"clip '{node.AnimationClip}' not found in {entry.MeshPath}");
            player.Clear();
        }
        PushPose(entry, player);
    }

    private void PushPose(Entry entry, AnimationPlayer player)
    {
        PoseRequest pose = player.Pose;
        _engine.SetObjectPose(entry.ObjectId, pose.ClipA, pose.TimeA, pose.ClipB, pose.TimeB, pose.Blend);
    }

    private void OnStructureChanged() => Rebuild();

    private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender is not SceneNode node || !_entries.TryGetValue(node, out Entry? entry) || e.PropertyName == nameof(SceneNode.Name))
            return;

        _errors.Clear();
        _warnings.Clear();
        if (node.MeshPath != entry.MeshPath)
        {
            LoadedMesh? mesh = LoadMesh(node.MeshPath);
            if (entry.Mesh is { SkeletonId: > 0 })
                _engine.SetObjectSkeleton(entry.ObjectId, 0);  // 정적 메시로 바꾸기 전에 스켈레톤을 먼저 뗀다
            _engine.SetObjectMesh(entry.ObjectId, mesh?.MeshId ?? 0);
            if (mesh is { SkeletonId: > 0 })
                _engine.SetObjectSkeleton(entry.ObjectId, mesh.SkeletonId);
            entry.Mesh = mesh;
            entry.MeshPath = node.MeshPath;
            _players.Remove(node);
        }
        if (node.TexturePath != entry.TexturePath)
        {
            _engine.SetObjectTexture(entry.ObjectId, LoadTexture(node.TexturePath));
            entry.TexturePath = node.TexturePath;
        }
        ApplyTransform(node, entry.ObjectId);
        ApplyAnimation(node, entry);
        Changed?.Invoke();
    }

    public void Dispose()
    {
        if (_document is not null)
            _document.StructureChanged -= OnStructureChanged;
        foreach (SceneNode node in _entries.Keys)
            node.PropertyChanged -= OnNodePropertyChanged;
        _entries.Clear();
    }
}
