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

    /// <summary>로드 시점의 파일 상태. 다시 Rebuild할 때 달라졌으면 캐시를 버리고 다시 읽는다.</summary>
    private readonly record struct FileStamp(DateTime LastWriteUtc, long Length);

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
    private readonly Dictionary<string, FileStamp?> _meshStamps = [];
    private readonly Dictionary<string, FileStamp?> _textureStamps = [];
    private readonly Dictionary<SceneNode, Entry> _entries = [];
    private readonly Dictionary<SceneNode, AnimationPlayer> _players = [];
    private readonly List<int> _rootObjects = [];
    private readonly List<int> _pointLights = [];
    private readonly List<string> _errors = [];
    private readonly List<string> _warnings = [];
    private SceneDocument? _document;

    public SceneSync(EngineRenderer engine) => _engine = engine;

    /// <summary>마지막 Rebuild/Update 중 로드에 실패한 항목 설명.</summary>
    public IReadOnlyList<string> Errors => _errors;

    /// <summary>로드는 됐지만 일부가 무시된 항목(예: 지원하지 않는 glTF 기능).</summary>
    public IReadOnlyList<string> Warnings => _warnings;

    /// <summary>구조/속성/로드 결과가 바뀌어 엔진 오브젝트가 갱신되었을 때 발생한다(상태 표시와 목록도 갱신해야 한다).</summary>
    public event Action? Changed;

    /// <summary>Tick이 재생 중인 포즈만 진행시켰을 때 발생한다. 다시 그리기만 하면 된다(30Hz로 오므로 무거운 갱신을 붙이지 않는다).</summary>
    public event Action? PoseChanged;

    public void Attach(SceneDocument document)
    {
        if (_document is not null)
            _document.StructureChanged -= OnStructureChanged;
        if (!ReferenceEquals(_document, document))
            _players.Clear();
        _document = document;
        document.StructureChanged += OnStructureChanged;
        Rebuild();
        ApplyLighting();  // 문서마다 조명이 다르므로 Rebuild가 지운 오류 목록 뒤에 반영한다(실패 사유가 남게)
    }

    /// <summary>노드의 메시가 가진 애니메이션 클립 이름들(없으면 빈 목록).</summary>
    public IReadOnlyList<string> GetClipNames(SceneNode node) =>
        _entries.TryGetValue(node, out Entry? entry) && entry.Mesh is { } mesh ? mesh.ClipNames : [];

    /// <summary>노드의 현재 재생 시간(초). 애니메이션이 없으면 null.</summary>
    public float? GetAnimationTime(SceneNode node) => _players.TryGetValue(node, out AnimationPlayer? p) && p.Current is not null ? p.Time : null;

    /// <summary>노드의 위치/회전을 문서(SceneNode)는 건드리지 않고 화면(엔진 오브젝트)에만 덮어쓴다. 플레이 모드에서 플레이어를
    /// 움직일 때 쓴다. 크기와 색은 노드의 값을 유지한다. 씬에 없는 노드면 false. 다시 그리기만 하면 되므로 PoseChanged를 발생시킨다.</summary>
    public bool Preview(SceneNode node, Vec3 position, Vec3 rotation)
    {
        if (!_entries.TryGetValue(node, out Entry? entry))
            return false;
        _engine.SetObjectTransform(entry.ObjectId, position, rotation, node.Scale);
        PoseChanged?.Invoke();
        return true;
    }

    /// <summary>Preview로 덮어쓴 변환을 지우고 노드의 문서 값으로 되돌린다.</summary>
    public void ClearPreview(SceneNode node)
    {
        if (!_entries.TryGetValue(node, out Entry? entry))
            return;
        ApplyTransform(node, entry.ObjectId);
        PoseChanged?.Invoke();
    }

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

        // 모든 오브젝트가 없어진 지금이 파일이 바뀐 캐시 리소스를 안전하게 해제하고 다시 읽을 수 있는 시점이다.
        if (PurgeStaleResources())
            _players.Clear();  // 해제된 클립 ID를 들고 있는 재생 상태를 버린다(다시 Play될 때 새 클립으로 시작)

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
            PoseChanged?.Invoke();
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
            _meshStamps[path] = StampOf(path);
            return _meshes[path] = mesh;
        }
        catch (Exception ex) when (ex is EngineException or GltfException or IOException or UnauthorizedAccessException or NotSupportedException)  // 폴더/권한 거부/지원하지 않는 경로 형식도 오류 목록으로
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
            int id = _engine.LoadTexture(path);
            _textureStamps[path] = StampOf(path);
            return _textures[path] = id;
        }
        catch (EngineException ex)
        {
            _errors.Add($"{path}: {ex.Message}");
            return 0;
        }
    }

    // ---- 캐시 무효화 ----
    private static FileStamp? StampOf(string path)
    {
        try
        {
            var info = new FileInfo(path);
            return info.Exists ? new FileStamp(info.LastWriteTimeUtc, info.Length) : null;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            return null;  // 상태를 못 읽으면 "알 수 없음"으로 두고, 다음 비교에서도 같으면 바뀌지 않은 것으로 본다
        }
    }

    /// <summary>로드 이후 파일이 바뀌었거나 사라진 메시/텍스처 캐시를 해제한다. 메시 캐시를 버렸으면 true.
    /// 호출 시점에는 엔진 오브젝트가 모두 없어져 있어야 한다(쓰는 오브젝트가 있으면 해제가 거부되고, 그 사실을 Errors에 남긴다).
    /// glTF의 외부 .bin 같은 부속 파일은 추적하지 않는다(주 파일의 시각/크기만 본다).</summary>
    private bool PurgeStaleResources()
    {
        bool anyMesh = false;
        foreach (string path in _meshes.Keys.Where(p => !_meshStamps.TryGetValue(p, out FileStamp? s) || s != StampOf(p)).ToList())
        {
            LoadedMesh mesh = _meshes[path];
            // 해제에 실패해도 캐시에서는 뺀다: 이미 일부가 해제됐을 수 있어 남겨 두면 해제된 ID를 가리키게 된다(누수가 낫다).
            _meshes.Remove(path);
            _meshStamps.Remove(path);
            anyMesh = true;
            try
            {
                foreach (ClipRef clip in mesh.Clips.Values)
                    _engine.DestroyClip(clip.Id);
                if (mesh.SkeletonId > 0)
                    _engine.DestroySkeleton(mesh.SkeletonId);
                _engine.DestroyMesh(mesh.MeshId);
            }
            catch (EngineException ex)
            {
                _errors.Add($"{path}: could not release the previous version ({ex.Message})");
            }
        }
        foreach (string path in _textures.Keys.Where(p => !_textureStamps.TryGetValue(p, out FileStamp? s) || s != StampOf(p)).ToList())
        {
            int id = _textures[path];
            _textures.Remove(path);
            _textureStamps.Remove(path);
            try
            {
                _engine.DestroyTexture(id);
            }
            catch (EngineException ex)
            {
                _errors.Add($"{path}: could not release the previous version ({ex.Message})");
            }
        }
        return anyMesh;
    }

    // ---- 노드 상태 적용 ----
    private void ApplyTransform(SceneNode node, int objectId)
    {
        _engine.SetObjectTransform(objectId, node.Position, node.Rotation, node.Scale);
        _engine.SetObjectColor(objectId, node.Color);
        _engine.SetObjectAlpha(objectId, node.Alpha);
        _engine.SetObjectCastsShadow(objectId, node.CastsShadow);
    }

    /// <summary>
    /// 문서의 조명(앰비언트, 방향광, 점광원, 그림자)을 엔진에 반영한다: 이전 점광원은 모두 없애고 다시 만든다.
    /// 문서에 값이 있어도 엔진이 받아들이지 못한 경우(파일에서 읽을 때 걸러지므로 코드로 직접 넣은 값뿐)는 Errors에 사유를 남긴다.
    /// Attach가 부르며, 조명을 편집한 쪽(에디터)도 값을 바꿀 때마다 부른다. 다시 그리기는 호출자가 한다.
    /// </summary>
    public void ApplyLighting()
    {
        if (_document is null)
            return;
        LightingData lighting = _document.Lighting;
        try
        {
            foreach (int id in _pointLights)
                _engine.DestroyPointLight(id);
            _pointLights.Clear();
            _engine.SetAmbient(lighting.Ambient);
            _engine.SetDirectionalLight(lighting.SunDirection, lighting.SunColor);
            foreach (PointLightDef light in lighting.PointLights)
                _pointLights.Add(_engine.CreatePointLight(light.Position, light.Color, light.Range));
            _engine.SetShadow(lighting.ShadowEnabled, lighting.ShadowPlaneY, lighting.ShadowOpacity);
        }
        catch (EngineException ex)
        {
            _errors.Add("lighting: " + ex.Message);
        }
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
