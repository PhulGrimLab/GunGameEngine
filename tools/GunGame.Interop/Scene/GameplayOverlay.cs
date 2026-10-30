using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>
/// 게임플레이 데이터(방, 충돌체, 트리거, 플레이어 스폰, 방 카메라 위치)를 뷰포트에 얇은 선과 작은 표식으로 그려 준다.
/// 에디터에는 영역 편집 UI가 아직 없어 씬 파일의 좌표를 눈으로 확인하는 용도다. 문서와 SceneSync의 오브젝트는 건드리지 않고
/// 자기가 만든 엔진 오브젝트만 관리한다(엔진에 선 그리기가 없어 가느다란 큐브로 변을 표현한다).
/// 색: 방 = 파랑, 트리거 = 초록, 충돌체 = 빨강, 방 카메라 위치 = 하늘색, 플레이어 스폰 = 노랑.
/// Owner: 에디터가 소유하고 렌더러보다 먼저 Dispose한다. 한 스레드에서만 사용한다.
/// </summary>
public sealed class GameplayOverlay : IDisposable
{
    private const float LineHalfThickness = 0.02f;
    // 같은 높이에 겹쳐 그린 선이 깜빡이지 않도록 종류마다 높이를 다르게 둔다.
    private const float RoomHeight = 0.02f, TriggerHeight = 0.05f, ColliderHeight = 0.08f;
    private const float MarkerHalfSize = 0.15f;

    private static readonly Vec3 RoomColor = new(0.3f, 0.5f, 1.0f);
    private static readonly Vec3 TriggerColor = new(0.3f, 1.0f, 0.4f);
    private static readonly Vec3 ColliderColor = new(1.0f, 0.3f, 0.3f);
    private static readonly Vec3 CameraColor = new(0.3f, 1.0f, 1.0f);
    private static readonly Vec3 SpawnColor = new(1.0f, 1.0f, 0.3f);

    private readonly EngineRenderer _engine;
    private readonly List<int> _objects = [];
    private int _cubeMesh;

    public GameplayOverlay(EngineRenderer engine) => _engine = engine;

    /// <summary>지금 오버레이 오브젝트가 화면에 있는가.</summary>
    public bool Visible => _objects.Count > 0;

    /// <summary>만들어 둔 엔진 오브젝트 수(테스트/진단용).</summary>
    public int ObjectCount => _objects.Count;

    /// <summary>이전 표시를 지우고 data를 다시 그린다. 그릴 것이 없으면 아무것도 만들지 않는다.</summary>
    public void Show(GameplayData data)
    {
        Hide();
        foreach (RoomDef room in data.Rooms)
        {
            AddPolygon(room.Polygon, RoomHeight, RoomColor);
            AddMarker(room.Eye, CameraColor);
        }
        foreach (TriggerDef trigger in data.Triggers)
            AddPolygon(trigger.Polygon, TriggerHeight, TriggerColor);
        foreach (ColliderDef c in data.Colliders)
            AddPolygon([c.MinX, c.MinZ, c.MaxX, c.MinZ, c.MaxX, c.MaxZ, c.MinX, c.MaxZ], ColliderHeight, ColliderColor);
        if (data.Player is { Spawn: { } spawn })
            AddMarker(new Vec3(spawn.X, 0.3f, spawn.Z), SpawnColor);
    }

    public void Hide()
    {
        foreach (int id in _objects)
            _engine.DestroyObject(id);
        _objects.Clear();
    }

    public void Dispose()
    {
        Hide();
        if (_cubeMesh != 0)
        {
            _engine.DestroyMesh(_cubeMesh);
            _cubeMesh = 0;
        }
    }

    private void AddPolygon(float[] xz, float height, Vec3 color)
    {
        int n = xz.Length / 2;
        for (int i = 0; i < n; i++)
        {
            int j = (i + 1) % n;
            AddSegment(xz[2 * i], xz[2 * i + 1], xz[2 * j], xz[2 * j + 1], height, color);
        }
    }

    /// <summary>(x0,z0)-(x1,z1)를 잇는 가느다란 큐브. 큐브는 [-1,1]이라 X 크기를 길이의 절반으로 둔다.</summary>
    private void AddSegment(float x0, float z0, float x1, float z1, float height, Vec3 color)
    {
        float dx = x1 - x0, dz = z1 - z0;
        float length = MathF.Sqrt(dx * dx + dz * dz);
        if (length <= 1e-6f)
            return;
        // Y축 회전 yaw는 +X를 (cos yaw, 0, -sin yaw)로 돌린다: 변 방향(dx, dz)에 맞추려면 yaw = atan2(-dz, dx).
        float yaw = MathF.Atan2(-dz, dx);
        int id = NewCube(color);
        _engine.SetObjectTransform(id, new Vec3((x0 + x1) / 2, height, (z0 + z1) / 2), new Vec3(0, yaw, 0),
            new Vec3(length / 2 + LineHalfThickness, LineHalfThickness, LineHalfThickness));
    }

    private void AddMarker(Vec3 position, Vec3 color)
    {
        int id = NewCube(color);
        _engine.SetObjectTransform(id, position, new Vec3(0, 0, 0), new Vec3(MarkerHalfSize, MarkerHalfSize, MarkerHalfSize));
    }

    private int NewCube(Vec3 color)
    {
        if (_cubeMesh == 0)
        {
            (float[] vertices, uint[] indices) = UnitCube();
            _cubeMesh = _engine.CreateMesh(vertices, indices);
        }
        int id = _engine.CreateObject(_cubeMesh);
        _engine.SetObjectColor(id, color);
        _objects.Add(id);
        return id;
    }

    /// <summary>[-1,1]^3 큐브: 면마다 정점 4개(플랫 노멀), 반시계 앞면. 정점 = 위치(3) + 법선(3) + UV(2).</summary>
    private static (float[] Vertices, uint[] Indices) UnitCube()
    {
        (float[] N, float[][] P)[] faces =
        [
            ([0, 0, 1], [[-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]]),
            ([0, 0, -1], [[1, -1, -1], [-1, -1, -1], [-1, 1, -1], [1, 1, -1]]),
            ([1, 0, 0], [[1, -1, 1], [1, -1, -1], [1, 1, -1], [1, 1, 1]]),
            ([-1, 0, 0], [[-1, -1, -1], [-1, -1, 1], [-1, 1, 1], [-1, 1, -1]]),
            ([0, 1, 0], [[-1, 1, 1], [1, 1, 1], [1, 1, -1], [-1, 1, -1]]),
            ([0, -1, 0], [[-1, -1, -1], [1, -1, -1], [1, -1, 1], [-1, -1, 1]]),
        ];
        float[][] uvs = [[0, 1], [1, 1], [1, 0], [0, 0]];
        var vertices = new List<float>();
        var indices = new List<uint>();
        foreach ((float[] n, float[][] p) in faces)
        {
            uint baseIndex = (uint)(vertices.Count / 8);
            for (int i = 0; i < 4; i++)
                vertices.AddRange([p[i][0], p[i][1], p[i][2], n[0], n[1], n[2], uvs[i][0], uvs[i][1]]);
            indices.AddRange([baseIndex, baseIndex + 1, baseIndex + 2, baseIndex, baseIndex + 2, baseIndex + 3]);
        }
        return (vertices.ToArray(), indices.ToArray());
    }
}
