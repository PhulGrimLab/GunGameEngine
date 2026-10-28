using System.ComponentModel;
using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>
/// 씬 문서를 엔진 오브젝트 그래프에 반영한다. 구조가 바뀌면 Rebuild, 노드 속성이 바뀌면 자동으로 Update된다.
/// 같은 경로의 메시/텍스처는 한 번만 로드한다.
/// </summary>
public sealed class SceneSync : IDisposable
{
    private sealed class Entry
    {
        public int ObjectId;
        public string? MeshPath;
        public string? TexturePath;
    }

    private readonly EngineRenderer _engine;
    private readonly Dictionary<string, int> _meshes = [];
    private readonly Dictionary<string, int> _textures = [];
    private readonly Dictionary<SceneNode, Entry> _entries = [];
    private readonly List<int> _rootObjects = [];
    private readonly List<string> _errors = [];
    private SceneDocument? _document;

    public SceneSync(EngineRenderer engine) => _engine = engine;

    /// <summary>마지막 Rebuild/Update 중 로드에 실패한 항목 설명.</summary>
    public IReadOnlyList<string> Errors => _errors;

    /// <summary>엔진 오브젝트가 갱신되어 다시 그려야 할 때 발생한다.</summary>
    public event Action? Changed;

    public void Attach(SceneDocument document)
    {
        if (_document is not null)
            _document.StructureChanged -= OnStructureChanged;
        _document = document;
        document.StructureChanged += OnStructureChanged;
        Rebuild();
    }

    public void Rebuild()
    {
        _errors.Clear();
        foreach (SceneNode node in _entries.Keys)
            node.PropertyChanged -= OnNodePropertyChanged;
        foreach (int id in _rootObjects)
            _engine.DestroyObject(id);
        _rootObjects.Clear();
        _entries.Clear();

        if (_document is not null)
            foreach (SceneNode root in _document.Roots)
                Create(root, 0);
        Changed?.Invoke();
    }

    private void Create(SceneNode node, int parentObjectId)
    {
        int mesh = Resolve(node.MeshPath, _meshes, LoadMesh);
        int texture = Resolve(node.TexturePath, _textures, _engine.LoadTexture);

        int objectId = _engine.CreateObject(mesh, texture, parentObjectId);
        if (parentObjectId == 0)
            _rootObjects.Add(objectId);
        _entries[node] = new Entry { ObjectId = objectId, MeshPath = node.MeshPath, TexturePath = node.TexturePath };
        node.PropertyChanged += OnNodePropertyChanged;
        ApplyTransform(node, objectId);

        foreach (SceneNode child in node.Children)
            Create(child, objectId);
    }

    private int LoadMesh(string path) =>
        Path.GetExtension(path).Equals(".ggm", StringComparison.OrdinalIgnoreCase)
            ? _engine.LoadMeshGgm(path)
            : _engine.LoadMeshObj(path);

    private int Resolve(string? path, Dictionary<string, int> cache, Func<string, int> load)
    {
        if (string.IsNullOrEmpty(path))
            return 0;
        if (cache.TryGetValue(path, out int cached))
            return cached;
        try
        {
            return cache[path] = load(path);
        }
        catch (EngineException ex)
        {
            _errors.Add($"{path}: {ex.Message}");
            return 0;  // 실패는 캐시하지 않아 파일을 고친 뒤 다시 시도할 수 있다
        }
    }

    private void ApplyTransform(SceneNode node, int objectId)
    {
        _engine.SetObjectTransform(objectId, node.Position, node.Rotation, node.Scale);
        _engine.SetObjectColor(objectId, node.Color);
    }

    private void OnStructureChanged() => Rebuild();

    private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender is not SceneNode node || !_entries.TryGetValue(node, out Entry? entry) || e.PropertyName == nameof(SceneNode.Name))
            return;

        _errors.Clear();
        if (node.MeshPath != entry.MeshPath)
        {
            _engine.SetObjectMesh(entry.ObjectId, Resolve(node.MeshPath, _meshes, LoadMesh));
            entry.MeshPath = node.MeshPath;
        }
        if (node.TexturePath != entry.TexturePath)
        {
            _engine.SetObjectTexture(entry.ObjectId, Resolve(node.TexturePath, _textures, _engine.LoadTexture));
            entry.TexturePath = node.TexturePath;
        }
        ApplyTransform(node, entry.ObjectId);
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
