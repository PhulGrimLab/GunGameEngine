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
/// 노드의 현재 클립 재생 상태(<see cref="IAnimationApi.GetClip"/>). <see cref="Clip"/>은 재생 중인 클립 이름(씬의 이름 그대로, 레스트 포즈면 빈 문자열),
/// <see cref="Time"/>은 그 클립의 시간(초), <see cref="Playing"/>은 일시정지가 아닌가(한 번 재생이 끝난 뒤에도 true: 끝남은 <see cref="Finished"/>로 본다),
/// <see cref="Fading"/>은 크로스페이드 중, <see cref="Loop"/>는 반복 재생, <see cref="Finished"/>는 한 번 재생이 재생 방향의 끝
/// (속도 0 이상이면 끝 시간, 음수면 0초)에 닿아 마지막 포즈로 멈춘 상태다(반복/레스트는 항상 false).
/// </summary>
public readonly record struct ClipState(string Clip, float Time, bool Playing, float Speed, bool Fading, bool Loop, bool Finished);

/// <summary>플레이어, 방, 트리거, 충돌체, 순간이동 같은 게임 세계의 읽기/쓰기.</summary>
public interface IWorldApi
{
    /// <summary>활성 방 이름(방이 없으면 빈 문자열).</summary>
    string ActiveRoomName { get; }

    /// <summary>플레이어의 바닥 위치. 플레이어가 없는 씬이면 null.</summary>
    (float X, float Z)? PlayerPosition { get; }

    /// <summary>
    /// 마지막 프레임에서 충돌/슬라이딩을 반영한 뒤 플레이어가 <b>실제로 이동한</b> 거리 / dt(월드 단위/초). 걷기/멈춤 애니메이션 전환에 쓴다
    /// (<c>PlayerSpeed &gt; 0.1f</c>이면 걷는 중). 벽을 정면으로 밀면 0이고 벽을 따라 미끄러지면 0보다 크다. <b>텔레포트 직후는 0</b>이라 순간이동이
    /// 속도 폭주를 만들지 않는다. 켜진 충돌체가 겹친 플레이어를 밀어내는 이동도 이동으로 센다. 플레이어가 없으면 0.
    /// </summary>
    float PlayerSpeed { get; }

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
}

/// <summary>
/// 씬 노드(오브젝트)의 위치/회전/크기/표시/알파. 대상은 모두 <b>이름이 같은 첫 노드</b>이고 값은 부모 기준(로컬)이다. 규칙: 읽기는 없는 노드면 null,
/// 쓰기는 없는 노드면 false, 값이 유한하지 않거나 <b>플레이어 노드의 위치/회전/크기</b>면 예외(상태는 그대로; 플레이어는 <see cref="IWorldApi.TeleportPlayer"/>로 옮긴다).
/// </summary>
public interface IObjectApi
{
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
    /// 예외(플레이어는 <see cref="IWorldApi.TeleportPlayer"/>로 옮긴다).
    /// </summary>
    bool SetObjectPosition(string nodeName, float x, float y, float z);
}

/// <summary>
/// 스킨 메시 노드의 클립 재생(플레이어 노드 포함: 플레이어 시스템은 위치/회전/크기만 정하고 애니메이션은 독립이라 걷기/대기 전환이 이 용도다).
/// 대상은 이름이 같은 첫 노드. 클립 이름은 씬의 이름 <b>그대로</b>(대소문자 구분; Blender glb는 "Armature|Walk" 형태일 수 있고,
/// <see cref="GetClip"/>이 돌려주는 이름과 에디터의 클립 목록이 그것이다).
/// </summary>
public interface IAnimationApi
{
    /// <summary>
    /// 노드의 클립을 0초부터 재생한다. <b>이미 그 클립이 현재 클립이면 아무것도 하지 않는다</b>(loop 값이 달라도): 매 프레임 <c>PlayClip("Hero", "walk")</c>를 불러도
    /// 되감기지 않는다. 대신 끝난 한 번 재생 클립을 다시 재생하거나, 반복 중인 클립을 한 번 재생으로 바꾸려면(또는 그 반대) <paramref name="restart"/>를 true로 줘야 한다
    /// (restart는 페이드 없이 즉시 0초부터, loop도 새로 적용). 다른 클립이고 이미 재생 중이면 <paramref name="fadeSeconds"/> &gt; 0일 때 이전 클립에서 크로스페이드한다
    /// (이전 클립은 페이드 동안 계속 진행하고 한 번 재생이었다면 끝 포즈에 머문다; 레스트 포즈에서 시작하면 즉시). <paramref name="loop"/>가 false면 한 번 재생하고
    /// 재생 방향의 끝 시간에서 멈춰 마지막 포즈를 유지한다(<see cref="ClipState.Finished"/>). 그런 노드가 없으면 false. 스킨 메시가 아닌 노드, 없는 클립 이름(가능한
    /// 이름 목록이 오류에 들어간다), fadeSeconds가 유한한 0..60이 아니면 예외.
    /// </summary>
    bool PlayClip(string nodeName, string clipName, float fadeSeconds = 0, bool restart = false, bool loop = true);

    /// <summary>재생을 멈추고 레스트 포즈로 돌아간다. 그런 노드가 없으면 false, 스킨 메시가 아니면 예외.</summary>
    bool StopClip(string nodeName);

    /// <summary>재생 속도 배율(유한해야 한다, 음수면 거꾸로). 현재 클립과 크로스페이드 중인 이전 클립에 모두 적용된다. 그런 노드가 없으면 false.</summary>
    bool SetClipSpeed(string nodeName, float speed);

    /// <summary>false면 일시정지(시간과 크로스페이드 진행이 모두 멈춘다), true면 재개. 그런 노드가 없으면 false.</summary>
    bool SetClipPlaying(string nodeName, bool playing);

    /// <summary>
    /// 노드의 현재 재생 상태. 그런 노드가 없거나 스킨 메시가 아니면 null. 한 번 재생이 끝났는지 알려면 <see cref="ClipState.Finished"/>를 매 프레임
    /// 읽으면 된다(이벤트는 없다). 속도 부호는 끝의 방향을 정하므로, 끝에서 속도 부호를 바꾸면 Finished가 false가 되어 거꾸로 재생된다.
    /// </summary>
    ClipState? GetClip(string nodeName);
}

/// <summary>
/// 오브젝트 생성/삭제: 씬 파일의 <b>템플릿 노드</b>(와 모든 자손)를 복제해 런타임에 인스턴스를 만든다(총알, 아이템, 적 ...). 메시/텍스처/스켈레톤/클립은
/// 템플릿과 공유하고 오브젝트만 새로 만든다. 템플릿은 에디터에서 평범한 노드로 만들고 화면에 보이지 않게 해 두면 된다: 구운 노드는 항상 보이는 상태로 시작하므로
/// <c>OnStart</c>에서 <see cref="IObjectApi.SetObjectVisible"/>(false)로 숨기거나 화면 밖에 두는 것이 관례다(복제는 템플릿의 <b>구운 값</b>으로 시작하므로
/// 숨기거나 옮겨도 복제본은 영향을 받지 않고 항상 보인다).
/// <para>이름: 복제 루트 = 인스턴스 이름, 자손 = "인스턴스/원래이름". 이후 <see cref="IObjectApi"/>/<see cref="IAnimationApi"/>의 모든 이름 기반 API가 이 이름으로 동작한다.</para>
/// </summary>
public interface ISpawnApi
{
    /// <summary>
    /// 템플릿을 복제해 인스턴스를 만든다. 위치는 <b>템플릿과 같은 부모 기준의 로컬 좌표</b>다(템플릿의 부모가 루트가 아니면 월드 좌표와 다르다). 회전/크기는 템플릿의 구운 값이라
    /// 방향은 직후에 <see cref="IObjectApi.SetObjectRotation"/>으로 정한다(그 사이 프레임이 지나지 않으므로 한 프레임도 잘못된 방향으로 그려지지 않는다).
    /// 그런 템플릿이 없으면 false. 인스턴스 이름이 비었거나 '/'를 포함하거나 128바이트를 넘거나 이미 있는 노드 이름(자손 이름 "인스턴스/원래이름" 포함)과 같거나,
    /// 템플릿이 복제본/플레이어(를 포함)거나, 값이 유한하지 않거나, 한도(살아 있는 복제 노드 256개, 한 프레임 spawn 64번, 서브트리 깊이 16)를 넘으면 예외이며
    /// 아무것도 만들어지지 않는다. 조명/충돌체/트리거는 복제하지 않는다.
    /// </summary>
    bool SpawnObject(string templateName, string instanceName, float x, float y, float z);

    /// <summary>
    /// <see cref="SpawnObject"/>로 만든 인스턴스(루트 이름)를 자손까지 지운다. 이름은 다시 쓸 수 있다. 그런 노드가 없으면 false.
    /// 씬 파일의 노드, 복제본의 자손 단독이면 예외(씬 노드는 <see cref="IObjectApi.SetObjectVisible"/>로 숨긴다). 지운 뒤 그 이름의 API는 "없는 노드" 규칙을 따른다.
    /// </summary>
    bool DestroyObject(string instanceName);

    /// <summary>그 이름의 노드가 있는가(씬 파일/복제 구분 없음).</summary>
    bool ObjectExists(string nodeName);
}

/// <summary><see cref="IUiApi.MeasureText"/>의 결과: 가장 넓은 줄의 폭, 전체 높이(줄 수 x 줄 높이), 줄 수(픽셀). 빈 문자열은 모두 0.</summary>
public readonly record struct TextSize(int Width, int Height, int Lines);

/// <summary>
/// 화면 위 글자와 사각형(HUD/대화창). 항목은 <b>이름</b>으로 가리키고(대소문자 구분), 3D가 모두 그려지고 PS1 후처리가 끝난 뒤 화면 픽셀에 만든 순서대로 덮어 그려진다
/// (<b>PS1 효과를 받지 않는다</b>: 색이 요청한 그대로다). 좌표는 렌더 타깃의 정수 픽셀(왼쪽 위 기준, 해상도는 플레이어의 --size)이고 화면 밖이면 잘린다.
/// 색은 0xRRGGBB. 글자는 BDF 비트맵 폰트(플레이어의 <c>--font 파일.bdf</c>)의 1비트 픽셀이고, 폰트에 없는 글자는 빈 네모, 잘못된 UTF-8은 대체 글자로 보이며
/// 탭/CR 같은 제어 문자는 무시되고 '\n'은 줄바꿈이다. 글자는 1024 UTF-8 바이트까지, 항목은 모두 합쳐 64개까지다(넘으면 예외).
/// 대화창은 <see cref="ShowRect"/>로 배경을 먼저, <see cref="ShowText"/>를 나중에 만들어 구성한다(타자기 효과 등은 스크립트가 글자를 늘려 가며 직접).
/// </summary>
public interface IUiApi
{
    /// <summary>렌더 타깃의 가로 픽셀 수(플레이어의 --size). 화면 아래에 대화창을 놓는 것처럼 화면 크기에 맞춘 배치에 쓴다.</summary>
    int ScreenWidth { get; }

    /// <summary>렌더 타깃의 세로 픽셀 수.</summary>
    int ScreenHeight { get; }

    /// <summary>
    /// 이름의 글자 항목을 보인다: 없으면 만들고(그리는 순서 = 만든 순서), 있으면 <b>제자리에서</b> 글자/위치/색/폭을 고친다(순서 유지).
    /// <paramref name="maxWidth"/> &gt; 0이면 그 픽셀 폭을 넘기 전에 줄바꿈(마지막 공백에서, 공백이 없으면 글자 경계에서), 0 이하면 '\n'에서만 줄을 바꾼다.
    /// 이름이 비었거나, 그 이름이 사각형이거나, 폰트가 없거나(<c>--font</c> 없음), 글자가 1024바이트를 넘거나, 항목이 64개를 넘으면 예외(상태는 그대로).
    /// </summary>
    void ShowText(string name, string text, int x, int y, uint rgb = 0xFFFFFF, int maxWidth = 0);

    /// <summary>글자 항목을 지우고 이름을 비운다. 그런 글자 항목이 없으면(사각형이거나 없는 이름) false.</summary>
    bool HideText(string name);

    /// <summary>
    /// 이름의 채운 사각형을 보인다(<see cref="ShowText"/>와 같은 규칙). 크기는 0보다 커야 하고 alpha는 0..255(255 = 불투명, 0 = 안 보임, 그 사이는 정수 혼합:
    /// 반투명 대화창 배경). 이름이 글자 항목이면 예외. 폰트는 필요 없다.
    /// </summary>
    void ShowRect(string name, int x, int y, int width, int height, uint rgb, int alpha = 255);

    /// <summary>사각형 항목을 지우고 이름을 비운다. 그런 사각형이 없으면 false.</summary>
    bool HideRect(string name);

    /// <summary>모든 화면 항목을 지운다.</summary>
    void ClearUi();

    /// <summary>
    /// 글자가 차지할 크기(그려지는 것과 같은 규칙: 줄바꿈, 대체 글자). 대화창 크기를 글자에 맞출 때 쓴다. 폰트가 없거나 글자가 1024바이트를 넘으면 예외.
    /// </summary>
    TextSize MeasureText(string text, int maxWidth = 0);
}

/// <summary>
/// 스크립트가 게임 세계를 읽고 바꾸는 창구: <see cref="IWorldApi"/>(플레이어/방/트리거/충돌체) + <see cref="IObjectApi"/>(노드 변환/표시/알파) +
/// <see cref="IAnimationApi"/>(클립 재생) + <see cref="ISpawnApi"/>(생성/삭제) + <see cref="IUiApi"/>(화면 글자/사각형) + 로그. 호출은 모두 게임 스레드에서
/// 이벤트/업데이트 처리 도중에 일어나며, 실패하면 예외를 던진다(스크립트가 예외를 처리하지 않으면 플레이어는 사유를 출력하고 종료한다).
/// </summary>
public interface IScriptContext : IWorldApi, IObjectApi, IAnimationApi, ISpawnApi, IUiApi
{
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
