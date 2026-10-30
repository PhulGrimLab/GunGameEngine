using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>고정 카메라 방: 바닥(XZ) 평면 위 다각형 영역과 그 안에서 쓰는 카메라. 다각형은 (x, z) 쌍의 배열이다.</summary>
public sealed class RoomDef
{
    public string Name { get; set; } = "Room";
    public float[] Polygon { get; set; } = [];
    public Vec3 Eye { get; set; }
    public Vec3 Target { get; set; }
    public float Fov { get; set; } = 0.8f;
}

/// <summary>정적 충돌체: 바닥(XZ) 평면 위 축 정렬 사각형.</summary>
public sealed class ColliderDef
{
    public string Name { get; set; } = "Collider";
    public float MinX { get; set; }
    public float MinZ { get; set; }
    public float MaxX { get; set; }
    public float MaxZ { get; set; }
}

/// <summary>트리거 영역: 바닥(XZ) 평면 위 다각형. Tag는 이벤트에 그대로 실리는 호출자 정의 값(문/아이템 식별 등)이다.</summary>
public sealed class TriggerDef
{
    public string Name { get; set; } = "Trigger";
    public float[] Polygon { get; set; } = [];
    public int Tag { get; set; }
}

/// <summary>플레이어: Node 이름의 루트 노드를 조작한다. Spawn이 null이면 노드의 현재 (x, z)에서 시작한다.</summary>
public sealed class PlayerDef
{
    public string Node { get; set; } = "";
    public Vec2? Spawn { get; set; }
    public float Radius { get; set; } = 0.4f;
    public float Speed { get; set; } = 3f;
}

/// <summary>씬에 딸린 게임플레이 데이터(방, 충돌체, 트리거, 플레이어). 플레이 모드가 엔진 월드를 만드는 원본이다.</summary>
public sealed class GameplayData
{
    public List<RoomDef> Rooms { get; } = [];
    public List<ColliderDef> Colliders { get; } = [];
    public List<TriggerDef> Triggers { get; } = [];
    public PlayerDef? Player { get; set; }

    public bool IsEmpty => Rooms.Count == 0 && Colliders.Count == 0 && Triggers.Count == 0 && Player is null;
}
