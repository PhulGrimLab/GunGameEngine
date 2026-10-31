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
/// 플레이어가 누른 동작 버튼. 키보드: Interact = E/Enter/Space, Cancel = Q/Backspace, Menu = Tab/M.
/// 게임패드: Interact = A, Cancel = B, Menu = Start. 헤드리스 대본(--keys)에서는 E / Q / M 글자다.
/// </summary>
public enum GameAction
{
    Interact = 1,
    Cancel = 2,
    Menu = 3,
}

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
    /// 이름이 같은 충돌체를 모두 켜거나 끈다(처음엔 모두 켜짐): 문을 열고 닫는 데 쓴다. 이름이 비어 있거나 그 이름이 없으면 예외.
    /// 닫을 때 플레이어가 안에 겹쳐 있으면 다음 프레임에 가장 가까운 면 바깥으로 밀려난다(문이 방 경계에 걸치면 반대편으로 나갈 수 있으니 문은 방 안쪽에 둔다).
    /// </summary>
    void SetColliderEnabled(string colliderName, bool enabled);

    /// <summary>
    /// 플레이어를 바닥 위치로 즉시 옮긴다(문/방 이동). 방향은 그대로이고, 활성 방과 카메라와 트리거가 바로 갱신된다.
    /// 어떤 방에도 속하지 않는 위치거나 플레이어가 없으면 예외(상태는 그대로).
    /// </summary>
    void TeleportPlayer(float x, float z);

    /// <summary>
    /// 플레이어가 지금 그 이름의 트리거(여럿이면 어느 하나) 안에 있는가. 이벤트와 같은 시점의 상태다: 트리거를 끄거나 켠 결과는
    /// 다음 프레임부터 반영된다. 그 이름이 없으면 예외. "안에서 상호작용 키를 누르면 문이 열린다" 같은 조건에 쓴다.
    /// </summary>
    bool IsPlayerInside(string triggerName);

    /// <summary>
    /// 이름이 같은 첫 노드의 불투명도를 바꾼다(0 = 안 보임, 1 = 불투명). 그런 노드가 없으면 false. 숨기려면 알파 0보다
    /// <see cref="SetObjectVisible"/>이 낫다(알파는 그대로 두고 자손까지 숨기며, 다시 켜면 이전 알파로 돌아온다).
    /// </summary>
    bool SetObjectAlpha(string nodeName, float alpha);

    /// <summary>
    /// 노드를 보이게/숨긴다(처음엔 보임). 숨기면 그 노드와 <b>모든 자손</b>이 그려지지 않고 그림자도 없다. 알파와 별개 상태다.
    /// 그런 노드가 없으면 false. 플레이어 노드도 숨길 수 있다(위치/회전/크기와 달리).
    /// </summary>
    bool SetObjectVisible(string nodeName, bool visible);

    /// <summary>
    /// 이름이 같은 첫 노드가 보이는가. <b>그 노드 자신의 플래그만</b> 돌려준다: 부모가 숨겨져서 안 그려지는 중이어도 true일 수 있다.
    /// 그런 노드가 없으면 null.
    /// </summary>
    bool? IsObjectVisible(string nodeName);

    /// <summary>
    /// 이름이 같은 첫 노드의 로컬 회전(오일러 pitch X, yaw Y, roll Z, <b>라디안</b>). 그런 노드가 없으면 null. 오일러라 여러 축을 섞으면 짐벌 한계가
    /// 있다(문 열림처럼 한 축 회전엔 문제없다).
    /// </summary>
    (float X, float Y, float Z)? GetObjectRotation(string nodeName);

    /// <summary>위치와 같은 규칙: 그런 노드가 없으면 false, 값이 유한하지 않거나 플레이어 노드면 예외(상태는 그대로).</summary>
    bool SetObjectRotation(string nodeName, float x, float y, float z);

    /// <summary>이름이 같은 첫 노드의 로컬 크기. 그런 노드가 없으면 null.</summary>
    (float X, float Y, float Z)? GetObjectScale(string nodeName);

    /// <summary>위치와 같은 규칙: 그런 노드가 없으면 false, 값이 유한하지 않거나 플레이어 노드면 예외. 크기 0은 받는다(납작해지거나, 세 축이 0이면 안 그려진다).</summary>
    bool SetObjectScale(string nodeName, float x, float y, float z);

    /// <summary>이름이 같은 첫 노드의 부모 기준(로컬) 위치(마지막으로 설정한 값, 처음엔 씬에 구운 값). 그런 노드가 없으면 null.</summary>
    (float X, float Y, float Z)? GetObjectPosition(string nodeName);

    /// <summary>
    /// 이름이 같은 첫 노드의 로컬 위치를 바꾼다(회전/크기 유지). 그런 노드가 없으면 false. 값이 유한하지 않거나 플레이어 노드면
    /// 예외(플레이어는 <see cref="TeleportPlayer"/>로 옮긴다).
    /// </summary>
    bool SetObjectPosition(string nodeName, float x, float y, float z);

    /// <summary>플레이어 프로세스의 표준 오류에 "[script] 메시지"를 남긴다.</summary>
    void Log(string message);
}

/// <summary>
/// 게임 스크립트. 공개 클래스로 만들고 매개변수 없는 생성자를 두면 플레이어가 <c>--script 어셈블리.dll</c>로 불러 실행한다.
/// 필요한 메서드만 구현하면 된다. 호출 순서: <see cref="OnStart"/>(첫 프레임 전) -> 프레임마다 이벤트마다 <see cref="OnTrigger"/>
/// -> 새로 눌린 동작마다 <see cref="OnAction"/> -> <see cref="OnUpdate"/>. 시작할 때 이미 영역 안이면 그 Enter 이벤트도 <see cref="OnStart"/> 다음에 전달된다.
/// </summary>
public interface IGameScript
{
    void OnStart(IScriptContext context) { }

    void OnTrigger(IScriptContext context, TriggerEvent trigger) { }

    /// <summary>동작 버튼을 <b>누른 프레임</b>에 한 번(누르고 있는 동안 반복되지 않는다). 그 프레임의 이벤트 처리 뒤, <see cref="OnUpdate"/> 앞에 불린다.</summary>
    void OnAction(IScriptContext context, GameAction action) { }

    /// <param name="deltaSeconds">고정 타임스텝(1/30초).</param>
    void OnUpdate(IScriptContext context, float deltaSeconds) { }
}
