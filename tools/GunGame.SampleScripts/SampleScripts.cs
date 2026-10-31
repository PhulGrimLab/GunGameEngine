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
