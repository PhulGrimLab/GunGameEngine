namespace GunGame.Scripting;

/// <summary>트리거 이벤트 종류. 값은 엔진(GGE_TRIGGER_ENTER/EXIT)과 같다.</summary>
public enum TriggerKind
{
    Enter = 1,
    Exit = 2,
}

/// <summary>플레이어가 트리거 영역에 들어오거나 나갔다. <see cref="Name"/>은 씬에서 정한 영역 이름, <see cref="Tag"/>는 그 영역의 태그다.</summary>
public readonly record struct TriggerEvent(TriggerKind Kind, int Tag, string Name);

/// <summary>
/// 스크립트가 게임 세계를 읽고 바꾸는 창구. 호출은 모두 게임 스레드에서 이벤트/업데이트 처리 도중에 일어나며, 실패하면 예외를 던진다
/// (스크립트가 예외를 처리하지 않으면 플레이어는 사유를 출력하고 종료한다).
/// </summary>
public interface IScriptContext
{
    /// <summary>활성 방 이름(방이 없으면 빈 문자열).</summary>
    string ActiveRoomName { get; }

    /// <summary>플레이어의 바닥 위치. 플레이어가 없는 씬이면 null.</summary>
    (float X, float Z)? PlayerPosition { get; }

    /// <summary>
    /// 이름이 같은 트리거를 모두 켜거나 끈다. 그 이름이 없으면 예외. 안에 있는 채로 끄면 다음 프레임에 Exit,
    /// 다시 켜면 Enter 이벤트가 나온다.
    /// </summary>
    void SetTriggerEnabled(string triggerName, bool enabled);

    /// <summary>
    /// 플레이어를 바닥 위치로 즉시 옮긴다(문/방 이동). 방향은 그대로이고, 활성 방과 카메라와 트리거가 바로 갱신된다.
    /// 어떤 방에도 속하지 않는 위치거나 플레이어가 없으면 예외(상태는 그대로).
    /// </summary>
    void TeleportPlayer(float x, float z);

    /// <summary>이름이 같은 첫 노드의 불투명도를 바꾼다(0 = 안 보임, 1 = 불투명). 그런 노드가 없으면 false.</summary>
    bool SetObjectAlpha(string nodeName, float alpha);

    /// <summary>플레이어 프로세스의 표준 오류에 "[script] 메시지"를 남긴다.</summary>
    void Log(string message);
}

/// <summary>
/// 게임 스크립트. 공개 클래스로 만들고 매개변수 없는 생성자를 두면 플레이어가 <c>--script 어셈블리.dll</c>로 불러 실행한다.
/// 필요한 메서드만 구현하면 된다. 호출 순서: <see cref="OnStart"/>(첫 프레임 전) -> 프레임마다 이벤트마다 <see cref="OnTrigger"/>
/// -> <see cref="OnUpdate"/>. 시작할 때 이미 영역 안이면 그 Enter 이벤트도 <see cref="OnStart"/> 다음에 전달된다.
/// </summary>
public interface IGameScript
{
    void OnStart(IScriptContext context) { }

    void OnTrigger(IScriptContext context, TriggerEvent trigger) { }

    /// <param name="deltaSeconds">고정 타임스텝(1/30초).</param>
    void OnUpdate(IScriptContext context, float deltaSeconds) { }
}
