// 독립 플레이어의 .NET 스크립트 호스트: hostfxr로 .NET 런타임을 띄우고 GunGame.ScriptHost(관리 코드)를 통해 게임 스크립트
// (IGameScript)를 실행한다. 스크립트는 플레이어가 프레임마다 꺼낸 트리거 이벤트와 매 프레임 업데이트를 받고, 엔진의
// gge_scene_* 쓰기 API(트리거 켜기/끄기, 텔레포트)와 오브젝트 알파로 게임 세계를 바꾼다.
//
// 런타임은 --script를 줬을 때만 올라간다(안 주면 .NET이 전혀 로드되지 않는다). 스레드 안전하지 않다(게임 스레드에서만 부른다).
// 한 프로세스에 하나만 만들 수 있다.
#pragma once
#include <gge/gge.h>

#include <memory>
#include <string>

namespace gge_player {

struct ScriptOptions {
    std::wstring hostDir;     // GunGame.ScriptHost.dll과 .runtimeconfig.json이 있는 폴더(빌드가 build\bin\script_host에 모은다)
    std::wstring assembly;    // 스크립트 어셈블리(.dll)
    std::wstring typeFilter;  // 비어 있으면 어셈블리의 모든 IGameScript, 아니면 그 이름(전체/짧은)의 타입만
};

class ScriptHost {
public:
    // 런타임을 올리고 스크립트를 불러 연결한다(OnStart는 Start에서). 실패하면 nullptr이고 사유가 error에 담긴다
    // (.NET 런타임 없음, 호스트 파일 없음, 스크립트 불러오기 실패 등). renderer/scene/font는 호출자가 소유하며 이 객체보다 오래 살아야 한다.
    // font는 화면 글자(IUiApi)에 쓸 폰트(없으면 nullptr: 글자 API는 "no font" 오류, 사각형은 폰트 없이 된다).
    static std::unique_ptr<ScriptHost> Create(const ScriptOptions& options, GgeRenderer* renderer, int width, int height,
                                              GgeScene* scene, GgeFont* font, std::string& error);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // 첫 프레임 전에 한 번: 스크립트의 OnStart. 스크립트가 예외를 던지면 false(사유는 error).
    bool Start(std::string& error);
    // 트리거 이벤트 하나를 스크립트의 OnTrigger로 넘긴다. kind는 GGE_TRIGGER_ENTER/EXIT. name은 UTF-8.
    bool OnTrigger(int kind, int tag, const char* name, std::string& error);
    // 새로 눌린 동작 버튼 하나를 스크립트의 OnAction으로 넘긴다. action은 1 = Interact, 2 = Cancel, 3 = Menu.
    bool OnAction(int action, std::string& error);
    // 프레임마다: 스크립트의 OnUpdate(고정 타임스텝).
    bool OnUpdate(float deltaSeconds, std::string& error);

    // 프로세스에 gge.dll이 정확히 하나만 올라와 있는지 확인한다. 스크립트 쪽 P/Invoke가 플레이어가 올린 것과 다른 두 번째 사본을
    // 불러오면(예: 스크립트 호스트 폴더에 gge.dll이 있고 해석기가 안 먹힘) 엔진 상태가 둘로 갈라지므로 끝나기 전에 잡는다.
    static bool VerifySingleEngineModule(std::string& error);

private:
    ScriptHost() = default;
    std::string LastManagedError() const;

    struct Entries;
    std::unique_ptr<Entries> _entries;
};

}  // namespace gge_player
