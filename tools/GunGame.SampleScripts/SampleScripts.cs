using GunGame.Scripting;

namespace GunGame.SampleScripts;

// 샘플 씬(assets/samples/play.ggs)을 기준으로 한 예제들: Hall(-x쪽)과 Annex 두 방, StartPad(태그 1), Door(태그 7) 트리거,
// 플레이어 Hero, 기둥 Pillar. 한 어셈블리에 여럿을 두었으므로 플레이어에서는 --script-type으로 하나를 고른다.

/// <summary>시작할 때 Door 트리거를 꺼서 문 이벤트가 나오지 않게 한다.</summary>
public sealed class DoorLock : IGameScript
{
    public void OnStart(IScriptContext context) => context.SetTriggerEnabled("Door", false);
}

/// <summary>Door(태그 7)에 들어서면 플레이어를 Hall의 시작 지점으로 되돌린다.</summary>
public sealed class ReturnToHall : IGameScript
{
    public void OnTrigger(IScriptContext context, TriggerEvent trigger)
    {
        if (trigger is { Kind: TriggerKind.Enter, Tag: 7 })
        {
            context.Log($"{trigger.Name}: sending the player back to the hall");
            context.TeleportPlayer(-4, 0);
        }
    }
}

/// <summary>Door에 들어서면 기둥을 반투명하게, 나가면 다시 불투명하게 만든다.</summary>
public sealed class FadePillarAtDoor : IGameScript
{
    public void OnTrigger(IScriptContext context, TriggerEvent trigger)
    {
        if (trigger.Tag != 7)
            return;
        if (!context.SetObjectAlpha("Pillar", trigger.Kind == TriggerKind.Enter ? 0.25f : 1f))
            throw new InvalidOperationException("the scene has no node named 'Pillar'");
    }
}

/// <summary>Door에 들어서면 일부러 예외를 던진다(스크립트 오류 처리 확인용).</summary>
public sealed class ExplodeAtDoor : IGameScript
{
    public void OnTrigger(IScriptContext context, TriggerEvent trigger)
    {
        if (trigger is { Kind: TriggerKind.Enter, Tag: 7 })
            throw new InvalidOperationException("the door script exploded");
    }
}

/// <summary>두 방 사이를 오가며 무한히 이벤트를 만든다: Door에 들어서면 Hall로, StartPad에 들어서면 다시 Door로 보낸다(폭주 방어 확인용).</summary>
public sealed class PingPong : IGameScript
{
    public void OnTrigger(IScriptContext context, TriggerEvent trigger)
    {
        if (trigger.Kind != TriggerKind.Enter)
            return;
        if (trigger.Tag == 7)
            context.TeleportPlayer(-4, 0);
        else if (trigger.Tag == 1)
            context.TeleportPlayer(4, 0);
    }
}

/// <summary>Door 영역 안에서 상호작용 버튼을 누르면 기둥을 위로 3 올린다(문이 열리는 식의 가장 단순한 퍼즐 장치).</summary>
public sealed class OpenWithInteract : IGameScript
{
    public void OnAction(IScriptContext context, GameAction action)
    {
        if (action != GameAction.Interact || !context.IsPlayerInside("Door"))
            return;
        (float X, float Y, float Z) p = context.GetObjectPosition("Pillar") ?? throw new InvalidOperationException("the scene has no node named 'Pillar'");
        context.SetObjectPosition("Pillar", p.X, p.Y + 3f, p.Z);
        context.Log($"opened: Pillar y {p.Y} -> {p.Y + 3f}");
    }
}

/// <summary>눌린 동작 버튼을 전부 기록한다(동작 전달 확인용).</summary>
public sealed class LogActions : IGameScript
{
    public void OnAction(IScriptContext context, GameAction action) => context.Log($"action {action}");
}

/// <summary>
/// 열쇠로 잠긴 문(assets/samples/locked_door.scene.json): KeyPad 영역에서 상호작용하면 열쇠를 줍고(Key 노드를 숨김), DoorPad 영역에서 상호작용하면
/// 열쇠가 있을 때 문 충돌체 "Door"를 끄고(통과 가능) 문판 DoorPanel을 바닥 아래로 내려 열린 모습으로 만들며, 없으면 잠겨 있다고 알린다.
/// 스크립트가 쌓는 상태(열쇠, 문 열림)가 곧 게임 상태다.
/// </summary>
public sealed class LockedDoor : IGameScript
{
    // 문판은 높이 2(y 0..2)라 이만큼 내리면 바닥 아래로 완전히 들어간다.
    private const float PanelSinkDistance = 2.2f;

    private bool _hasKey, _doorOpen;

    public void OnAction(IScriptContext context, GameAction action)
    {
        if (action != GameAction.Interact)
            return;
        if (!_hasKey && context.IsPlayerInside("KeyPad"))
        {
            _hasKey = true;
            context.SetObjectVisible("Key", false);
            context.Log("picked up the key");
        }
        else if (context.IsPlayerInside("DoorPad"))
        {
            if (!_hasKey)
                context.Log("the door is locked");
            else if (!_doorOpen)
            {
                _doorOpen = true;
                context.SetColliderEnabled("Door", false);
                (float X, float Y, float Z) panel = context.GetObjectPosition("DoorPanel") ?? throw new InvalidOperationException("the scene has no node named 'DoorPanel'");
                context.SetObjectPosition("DoorPanel", panel.X, panel.Y - PanelSinkDistance, panel.Z);
                context.Log("the door opens");
            }
        }
    }
}

/// <summary>
/// 플레이어의 걷기/대기 애니메이션 자동 전환(assets/samples/walk_animation.scene.json): 실제로 움직이는 중(PlayerSpeed)이면 walk, 아니면 idle.
/// 벽을 정면으로 밀면 PlayerSpeed가 0이라 idle이 된다. 매 프레임 PlayClip을 불러도 같은 클립이면 무시되어 되감기지 않는다.
/// 문턱 근처에서 깜빡이면 히스테리시스(켜는 문턱 > 끄는 문턱)를 두면 되지만 샘플은 단순하게 둔다.
/// 이 패턴이 여러 게임에서 반복되면 씬 데이터 필드(.ggs v3)로 올리는 것을 검토한다.
/// </summary>
public sealed class PlayerWalkAnimation : IGameScript
{
    private const float WalkSpeedThreshold = 0.1f;   // 월드 단위/초: 이보다 느리면 멈춘 것으로 본다
    private const float FadeSeconds = 0.15f;

    public void OnUpdate(IScriptContext context, float deltaSeconds) =>
        context.PlayClip("Hero", context.PlayerSpeed > WalkSpeedThreshold ? "walk" : "idle", FadeSeconds);
}

/// <summary>
/// 오브젝트 생성/삭제(assets/samples/spawner.scene.json): 씬의 템플릿 노드 Crate(자손 Lid 포함)를 시작할 때 숨기고,
/// 상호작용 버튼을 누를 때마다 플레이어 위치에 복제(Crate1, Crate2, ...)를 만들며, 취소 버튼은 가장 최근 복제를 지운다.
/// 템플릿 숨기기는 구운 데이터에 '템플릿' 표식이 없어서 쓰는 관례이고, 복제는 템플릿의 구운 값(위치/크기/색)으로 시작하므로 숨긴 템플릿과 무관하게 보인다.
/// </summary>
public sealed class Spawner : IGameScript
{
    private readonly Stack<string> _spawned = new();
    private int _counter;

    public void OnStart(IScriptContext context) => context.SetObjectVisible("Crate", false);

    public void OnAction(IScriptContext context, GameAction action)
    {
        if (action == GameAction.Interact)
        {
            (float X, float Z) player = context.PlayerPosition ?? throw new InvalidOperationException("the scene has no player");
            string name = $"Crate{++_counter}";
            // 위치는 템플릿과 같은 부모(여기서는 루트) 기준이다. 높이는 템플릿의 구운 값(0.3)을 따른다.
            context.SpawnObject("Crate", name, player.X, 0.3f, player.Z);
            _spawned.Push(name);
            context.Log($"spawned {name} at {player.X:F2}, {player.Z:F2}");
        }
        else if (action == GameAction.Cancel)
        {
            if (!_spawned.TryPop(out string? name))
            {
                context.Log("nothing to destroy");
                return;
            }
            context.DestroyObject(name);
            context.Log($"destroyed {name}");
        }
    }
}

/// <summary>한 프레임에 한도(64)보다 많이 만들려고 한다(한도 초과가 사유와 함께 스크립트 예외가 되어 플레이어가 멈추는지 확인용).</summary>
public sealed class SpawnFlood : IGameScript
{
    public void OnAction(IScriptContext context, GameAction action)
    {
        for (int i = 0; i < 65; ++i)
            context.SpawnObject("Crate", $"Flood{i}", 0, 0.3f, 0);
    }
}

/// <summary>업데이트가 30번 불린 프레임(1초 뒤)에 플레이어를 Annex로 옮긴다(OnUpdate 호출 확인용).</summary>
public sealed class TeleportAfterOneSecond : IGameScript
{
    private int _frames;

    public void OnUpdate(IScriptContext context, float deltaSeconds)
    {
        if (++_frames == 30)
            context.TeleportPlayer(4, 0);
    }
}

/// <summary>생성자가 던진다(불러오기 오류 처리 확인용).</summary>
public sealed class BrokenConstructor : IGameScript
{
    public BrokenConstructor() => throw new InvalidOperationException("cannot be constructed");
}
