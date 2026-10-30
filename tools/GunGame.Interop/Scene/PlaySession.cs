using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>플레이를 시작할 수 없는 씬(플레이어 노드 없음, 스폰이 방 밖, 엔진이 거부한 영역 등).</summary>
public sealed class PlayException(string message, Exception? inner = null) : Exception(message, inner);

/// <summary>플레이 중 발생한 트리거 이벤트. TriggerName은 씬 파일의 영역 이름이다.</summary>
public readonly record struct PlayEvent(string TriggerName, int Tag, TriggerEventKind Kind);

/// <summary>
/// 씬의 게임플레이 데이터(방/충돌체/트리거/플레이어)로 엔진 월드를 만들어 플레이어를 조작한다. 문서(SceneDocument)는 바꾸지 않는다:
/// 플레이어의 위치/방향은 PlayerPosition/PlayerRotation으로만 노출하며, 화면에 반영하는 쪽(SceneSync.Preview)이 맡는다.
/// WPF에 의존하지 않아 엔진만 있으면 테스트할 수 있다.
///
/// 프레임 처리 순서(Step): 입력 -> 활성 방 카메라 기준 이동량 -> 충돌 이동 -> 활성 방 갱신(카메라 컷) -> 트리거 갱신.
/// 규칙:
///  - 플레이어 노드는 루트 노드여야 한다(부모 변환을 거치면 월드 좌표 이동과 어긋난다).
///  - 플레이어 모델의 앞쪽은 +Z라고 가정하고, 이동 방향으로 Y축 회전(Yaw)을 돌린다.
///  - Step은 입력이 없는 프레임에도 매 프레임 불러야 한다(입력 방향 기준이 그 프레임에 갱신된다).
/// Owner: 만든 쪽(에디터)이 소유하고 Dispose한다. 한 스레드에서만 사용한다.
/// </summary>
public sealed class PlaySession : IDisposable
{
    private readonly EngineWorld _world;
    private readonly List<RoomDef> _rooms;
    private readonly List<TriggerDef> _triggers;
    private readonly float _radius;
    private readonly float _speed;
    private readonly List<PlayEvent> _pending = [];

    private PlaySession(EngineWorld world, SceneNode player, PlayerDef def, Vec2 spawn, List<RoomDef> rooms, List<TriggerDef> triggers)
    {
        _world = world;
        PlayerNode = player;
        _rooms = rooms;
        _triggers = triggers;
        _radius = def.Radius;
        _speed = def.Speed;
        Position = spawn;
        Yaw = player.Rotation.Y;
    }

    public SceneNode PlayerNode { get; }

    /// <summary>플레이어의 바닥 위치(x, z).</summary>
    public Vec2 Position { get; private set; }

    /// <summary>마지막으로 움직인 방향을 바라보는 Y축 회전(라디안). 움직인 적이 없으면 노드의 원래 회전이다.</summary>
    public float Yaw { get; private set; }

    /// <summary>화면에 적용할 플레이어 위치: 높이(Y)는 노드의 값을 유지한다.</summary>
    public Vec3 PlayerPosition => new(Position.X, PlayerNode.Position.Y, Position.Z);

    public Vec3 PlayerRotation => new(PlayerNode.Rotation.X, Yaw, PlayerNode.Rotation.Z);

    /// <summary>현재 활성 방의 번호(씬 파일의 rooms 순서, 0부터). 시작 시 스폰이 방 안임이 보장된다.</summary>
    public int ActiveRoomIndex => _world.ActiveRoom - 1;

    public string ActiveRoomName => _rooms[ActiveRoomIndex].Name;

    /// <summary>현재 활성 방의 고정 카메라.</summary>
    public RoomCameraData Camera => _world.GetRoomCamera(_world.ActiveRoom);

    /// <summary>씬의 게임플레이 데이터로 플레이를 준비한다. 문서는 바꾸지 않는다.</summary>
    /// <exception cref="PlayException">플레이어/방이 없거나, 스폰이 어떤 방에도 속하지 않거나, 엔진이 영역을 거부했을 때.</exception>
    public static PlaySession Start(SceneDocument document)
    {
        GameplayData data = document.Gameplay;
        if (data.Player is not { } def)
            throw new PlayException("the scene has no gameplay.player");
        if (data.Rooms.Count == 0)
            throw new PlayException("the scene has no gameplay.rooms (at least one room is needed for the camera)");
        SceneNode node = document.Nodes().FirstOrDefault(n => n.Name == def.Node)
            ?? throw new PlayException($"player node '{def.Node}' was not found in the scene");
        if (node.Parent is not null)
            throw new PlayException($"player node '{def.Node}' must be a root node");

        var world = new EngineWorld();
        try
        {
            foreach (RoomDef room in data.Rooms)
                Wrap($"room '{room.Name}'", () => world.AddRoom(room.Polygon, new RoomCameraData(room.Eye, room.Target, room.Fov)));
            foreach (ColliderDef c in data.Colliders)
                Wrap($"collider '{c.Name}'", () => world.AddBoxCollider(c.MinX, c.MinZ, c.MaxX, c.MaxZ));
            foreach (TriggerDef t in data.Triggers)
                Wrap($"trigger '{t.Name}'", () => world.AddTrigger(t.Polygon, t.Tag));

            Vec2 spawn = def.Spawn ?? new Vec2(node.Position.X, node.Position.Z);
            if (world.UpdateActiveRoom(spawn.X, spawn.Z) == 0)
                throw new PlayException($"the player spawn ({spawn.X}, {spawn.Z}) is outside every room");

            var session = new PlaySession(world, node, def, spawn, [.. data.Rooms], [.. data.Triggers]);
            session.UpdateTriggers();  // 트리거 안에서 시작하면 첫 Step의 결과에 Enter가 실린다
            return session;
        }
        catch
        {
            world.Dispose();
            throw;
        }
    }

    /// <summary>한 프레임을 진행한다. 입력은 오른쪽+/화면 위쪽+ 축(-1..1)이고 dt는 초다.
    /// 이번 프레임에 생긴 트리거 이벤트를 돌려준다(시작 시 이미 영역 안이었다면 첫 호출에 그 Enter가 포함된다).</summary>
    public IReadOnlyList<PlayEvent> Step(float inputX, float inputY, float dt)
    {
        Vec2 intended = _world.MoveInput(inputX, inputY, _speed, dt);
        Position = _world.MoveCircle(Position, intended, _radius);
        if (intended.X != 0 || intended.Z != 0)
            Yaw = MathF.Atan2(intended.X, intended.Z);  // 의도한 방향(벽에 막혀도 그쪽을 본다). 모델 앞쪽 = +Z 가정
        _world.UpdateActiveRoom(Position.X, Position.Z);  // 방을 넘었으면 카메라가 바뀐다
        UpdateTriggers();

        var events = new List<PlayEvent>(_pending);
        _pending.Clear();
        return events;
    }

    private void UpdateTriggers()
    {
        _world.UpdateTriggers(Position.X, Position.Z);
        while (_world.TryPollTriggerEvent(out TriggerEvent e))
            _pending.Add(new PlayEvent(_triggers[e.TriggerId - 1].Name, e.Tag, e.Kind));
    }

    private static void Wrap(string what, Action add)
    {
        try
        {
            add();
        }
        catch (EngineException ex)
        {
            throw new PlayException($"{what}: {ex.Message}", ex);
        }
    }

    public void Dispose() => _world.Dispose();
}
