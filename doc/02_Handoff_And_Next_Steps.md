# 인수인계: 현재 상태와 다음 단계

브랜치 `feature/engine-viewer-skeleton`(PR 미생성). 상세 설계/규약은 `doc/01_Development_Plan.md`, 테스트 실효성 점검 결과와 재현 방법은 `doc/03_Test_Quality_Report.md` 참고.

## 목표
SIGNALIS 스타일(PS1풍 저해상도 3D, 고정 카메라, 3인칭 탐험/전투/퍼즐) 게임용 C++ 엔진 + C#/WPF 개발 툴.

## 완료된 단계
| 단계 | 내용 |
|---|---|
| 0 | CMake 엔진, `tools/GunGameTools.slnx`, C++/C# 테스트 골격 |
| 1 | D3D11(WARP 지원) 저해상도 렌더 코어, 플랫 셰이딩, OBJ 로더, `extern "C"` API |
| 2 | P/Invoke 래퍼, WPF 뷰어(오빗 카메라), `--selftest` 화면 검증 하네스 |
| 3 | PS1풍 효과: 버텍스 스냅, 색 깊이 제한, 4x4 오더드 디더 |
| 4a | UV, 점 샘플링 텍스처(WIC), 아핀 텍스처 매핑, `.ggm` 바이너리 메시 |
| 4b | 엔진 오브젝트 계층(부모/자식), C# 씬 모델/JSON 저장, 에디터 트리·인스펙터·드래그 재부모화 |
| 5 | GPU 스키닝, 클립 샘플링/블렌딩, glTF(.glb) 임포터, `AnimationPlayer`, 에디터 재생 UI |
| 6 | 게임플레이 기반: 폴리곤 고정 카메라 방, AABB 충돌(벽 슬라이딩), 카메라 상대 입력(컷 래치), 트리거(진입/퇴장 이벤트), 에디터 플레이 모드(Play/Stop) |
| 6+ | **게임플레이 편집 UI**: 방/충돌체/트리거/플레이어 추가·편집·이동·삭제, 방 카메라를 현재 뷰로 설정, 입력 검증 |
| 7 | **조명/그림자/투명**: 앰비언트 + 방향광 + 점광원 8개, 오브젝트 알파 블렌딩 + 텍스처 알파 컷아웃, 바닥 평면 투영 그림자. 노드 알파/그림자 인스펙터, 씬 `lighting` 섹션, 에디터 Lighting/Light 항목 |
| 7+ | **독립 플레이어**: 구운 씬 `.ggs`(에디터 Bake), 엔진 런타임 `gge_scene_*`, `gge_player.exe`(창, 키보드/XInput, 헤드리스) |
| 8 | **C# 스크립팅**: 엔진 쓰기 API(`gge_scene_set_trigger_enabled`, `gge_scene_teleport_player`), 플레이어가 hostfxr로 .NET을 호스팅해 `IGameScript`(`--script game.dll`)를 실행. 상세는 `doc/01`의 "스크립팅" 절 |

각 단계의 규약(데이터 형식, 이벤트 순서, 래치 규칙, 조명 공식, 그림자 제약, `.ggs` 형식)은 `doc/01_Development_Plan.md`가 기준이다.

## 이번 세션 작업 이력 (2026-10-31)
문서가 제안한 다음 단계 A -> B -> C -> D를 순서대로 진행했다. 각 단계마다 테스트를 먼저/함께 만들고 **결함을 일부러 주입해 테스트가 실제로 잡는지** 확인했다.
1. **A. 게임플레이 편집 UI**: `GameplayEditing`(WPF 비의존 로직, 21개 테스트) + `GameplayInspector`/목록/툴바(UI 입력 테스트 16개). 편집한 씬이 저장 -> 다시 열기 -> **실제 플레이**까지 되는 왕복을 테스트로 확인.
2. **B. 테스트 실효성 후속**: C# 뮤테이션 첫 실행(처리율 **78.7% -> 94.7%**, `GltfImporter` 75.6% -> 95.6%), 구멍 메우기, `ReadPlayInput` 키 매핑을 `PlayInputFromKeys`로 분리해 테스트, 간헐 실패 원인(`RenderTargetBitmap` 찢어진 프레임) 수정. 상세는 `doc/03`.
3. **C. 7단계 조명/그림자/알파**: 엔진(셰이더, 스텐실 D24S8 깊이 버퍼, 블렌드 상태) + C API + C# 래퍼/씬 데이터/직렬화/동기화/에디터 UI. 기본 조명이 이전과 비트까지 같아 기존 골든 5개가 그대로 통과. 새 골든 2개(`lit_scene*`), 결함 주입 10건 중 8건 검출(2건 동등 변이).
4. **D. 독립 플레이어 + 씬 bake**: `.ggs` 형식 + C# `SceneBaker` + 엔진 `Scene` 런타임 + `gge_player.exe` + 에디터 Bake 버튼. **C# 플레이 모드와 C++ 런타임을 같은 입력으로 프레임마다 비교**(위치/방향/방/이벤트/화면 바이트)하고, 문서만 보고 따로 만든 작성기로 로더를 검증하고, 체크섬을 맞춘 무작위 손상 400건으로 로더가 죽지 않는지 확인.

## 현재 테스트 (`build.cmd test`, 모두 통과)
- C++ 엔진 `engine_tests`: **164개 케이스, 75499 체크**(골든 이미지 포함; 비트맵 폰트/글자 6건(합성 BDF: 정확한 픽셀, 거부 38건, 줄바꿈 규칙, 잘못된 UTF-8/없는 글자, 클리핑/알파, 무작위 변조 600건); 동적 오브젝트 7건: 구운 값 시작/구운 사본과 바이트 동일, 삭제 후 흔적 없음과 이름 재사용, 거부 후 상태 불변, 한도, 스킨 복제 독립 재생, 중간 실패 되돌림, 무작위 생성/삭제 불변식)
- 플레이어 ctest 32개(전체 ctest 33개; 오브젝트 생성/삭제 4개 추가: 복제 3개 생성(위치/개수), 가장 최근 복제 삭제, 지울 것 없음, 한 프레임 한도 초과 시 사유와 함께 종료, `--report-objects`; 걷기 애니메이션 4개 추가: 걸으면 walk로 전환/크로스페이드/멈추면 idle/벽에 막히면 idle, `--report-animation 노드`): `--self-test`(입력 매핑/대본/동작 버튼 포함), 헤드리스 걷기(이벤트/방 확인), **헤드리스 화면 = 엔진 골든(바이트 동일)**, 없는 씬/잘못된 대본 거부, **열쇠로 잠긴 문 3개**(열쇠 있음 -> 열고 Annex까지 / 열쇠 없음 -> 잠김과 막힘 / 스크립트 없음 대조군), **스크립팅 16개**(실제 .NET 호스팅: 스크립트가 트리거를 끄고/텔레포트하고/`OnUpdate`로 움직이고/**상호작용 키로 오브젝트를 옮기는** 결과, 동작 버튼은 누른 프레임에 한 번만 전달, 예외/폭주/없는 타입/생성자 예외/없는 어셈블리/없는 호스트/옵션 오류 거부, `gge.dll` 사본이 있는 호스트 폴더에서도 같은 모듈 사용. 그중 2개는 데코이 폴더 준비용 픽스처)
- C# `GunGame.Interop.Tests`: **316개**(생성/삭제 래퍼와 컨텍스트 규칙 `SpawnApiTests`, `spawner` 구운 샘플 최신 확인; 클립 재생 `AnimationApiTests`/`AnimationPlayerTests`: loop/restart/Finished/크로스페이드, **Scene(C++) = AnimationPlayer(C#) 프레임별 본 행렬 교차 검증**, `hero.glb`/`walk_animation` 샘플; 오브젝트 쓰기 API `ObjectApiTests`: 표시/회전/크기 래퍼와 컨텍스트 규칙, LockedDoor 샘플 실제 동작, 실제 외부 glb 오라클/정적 모델/파이프라인 테스트 `RealGltfModelTests`/`StaticGltfModelTests`/`RealModelPipelineTests`, 이름 있는 충돌체 `ColliderTests`: v2 굽기, 래퍼 오류, **PlaySession(C#) = Scene(C++) 문 열기/닫기 교차 검증**)
- 에디터 UI 입력 `GunGameEditor.UiTests`: **94개**(충돌체 이름이 비면 거부 포함)
- 뷰어 selftest: OK
- 수동 도구(ctest 미등록): `build\bin\engine_bench.exe [--hw]`(프레임 비용, 기본 WARP: 평면 큐브 1000개 약 4.2~4.7ms, `--hw`는 하드웨어 GPU: 약 0.76ms), `tools/mutation/*`(뮤테이션/커버리지 하네스)

## 저장소 구조
```
engine/            C++ 엔진 (DLL gge.dll)
  include/gge/gge.h  공개 C API (규약은 이 파일 주석이 기준)
  src/               renderer(조명/알파/그림자), animation, obj_loader, mesh_io, texture_loader(파일/메모리 디코딩),
                     world(방/입력/트리거), collision, polygon, scene_io(.ggs 읽기), scene(구운 씬 런타임), capi
  player/             독립 실행 플레이어 gge_player.exe: player.cpp(엔진 C API만 사용), script_host.*(hostfxr로 .NET 호스팅),
                     expect_failure.cmake(ctest 헬퍼: 종료 코드 != 0 이면서 출력이 정규식에 맞아야 통과)
  third_party/dotnet/ hostfxr.h, coreclr_delegates.h(.NET SDK에서 그대로 복사, MIT)
  tests/             engine_tests.cpp, engine_bench.cpp, golden/*.ppm
tools/
  GunGame.Interop/       P/Invoke 래퍼(EngineRenderer, EngineWorld, EngineScene), 씬(Scene/: 문서, 직렬화, SceneSync,
                         GameplayData/GameplayEditing/GameplayOverlay/PlaySession, LightingData, SceneBaker), glTF(Gltf/), 애니메이션(Animation/)
  GunGame.Interop.Tests/ xUnit (GltfBuilder: 테스트용 glTF 생성기). *GapTests = 뮤테이션 생존 변이를 겨냥한 테스트
  GunGame.Scripting/     스크립트 작성자가 참조하는 인터페이스(IGameScript, IScriptContext, TriggerEvent)
  GunGame.ScriptHost/    플레이어가 hostfxr로 부르는 .NET 쪽(Entry = [UnmanagedCallersOnly] 진입점, ScriptRunner = 로직, ScriptContext)
  GunGame.SampleScripts/ 스크립트 예제 + 플레이어 스크립팅 ctest용(DoorLock, ReturnToHall, PingPong 등). 결과는 build\bin\scripts
  GunGameEditor/         WPF 에디터 (GameplayInspector.cs, InspectorPanel.cs, SelfTest.cs = 화면 검증 하네스)
  GunGameEditor.UiTests/ 에디터 UI 입력 자동 테스트(xUnit, 아래 "에디터 UI 입력 테스트" 참고)
  mutation/              뮤테이션/커버리지 하네스 스크립트(doc/03 참고)
  selftest.ps1           에디터 화면 검증 스크립트
assets/samples/      cube.obj, checker.png, tentacle.glb, sample.scene.json, play.scene.json(플레이 모드 샘플),
                     lit.scene.json(조명/알파/그림자 샘플), play.ggs(play.scene.json을 구운 결과, 형식 v2),
                     locked_door.scene.json/.ggs(열쇠로 잠긴 문 샘플, LockedDoor 스크립트), play_v1.ggs(형식 v1 시절의 play.ggs를 그대로
                     보존한 호환 확인용 파일: 재생성하지 않는다)
assets/external/     실제 외부 glb 테스트 픽스처(출처/라이선스/파생물 구분은 그 폴더의 README.md; 게임 콘텐츠로 배포 금지)
doc/                 계획·인수인계·테스트 실효성 보고서
build.cmd            전체 빌드 + (test 인자 시) 전체 테스트
```

## 리셋 후 환경 복구
필요한 것: **Visual Studio 또는 Build Tools(버전 무관, "C++를 사용한 데스크톱 개발" 워크로드)**, CMake(4.x), **.NET SDK 10**, Windows SDK, git, (뮤테이션 하네스용) Python 3. Ninja는 필요 없다(엔진은 MSBuild로 빌드).
1. `git clone https://github.com/PhulGrimLab/GunGameEngine.git` 후 `git checkout feature/engine-viewer-skeleton`
2. `build.cmd test` 실행. VS 경로 하드코딩/`vcvars` 없이 CMake가 설치된 최신 Visual Studio를 찾아 Visual Studio 제너레이터(MSBuild)로 `-A x64` Release 빌드한다. `build\`가 다른 제너레이터(예: 이전 Ninja)로 만들어졌다면 `build\`를 지우고 다시 실행.
3. 에디터: `tools\GunGameEditor\bin\Debug\net10.0-windows\GunGameEditor.exe` → Open Scene → `assets\samples\play.scene.json`(또는 새 씬에서 **Add Room / Set Player**로 직접 만들기) → **Show gameplay**로 영역 확인, **Play**로 조작(WASD/방향키, Stop으로 종료). **Bake Player Scene...**으로 `.ggs`를 만든다.
4. 독립 플레이어: `build\bin\gge_player.exe assets\samples\play.ggs` (WASD/방향키/게임패드, Esc 종료). 옵션은 `engine/player/player.cpp` 맨 위 주석 참고. 헤드리스 예: `gge_player.exe assets\samples\play.ggs --headless --warp --frames 100 --keys D:100 --screenshot out.ppm`.

주의/함정:
- 엔진 출력은 `build\bin\gge.dll`로 고정(`CMakeLists.txt`에서 구성별 출력 폴더 지정). MSBuild 제너레이터는 기본으로 `bin\Release\`를 만들기 때문이다. `gge_player.exe`도 같은 폴더에 나오며 `gge.dll`이 옆에 있어야 한다. `ctest`는 `-C Release`가 필요하다.
- 골든 이미지는 `GGE_UPDATE_GOLDEN=1`로 재생성하되 **반드시 눈으로 확인**(새 골든만 생기고 기존 골든이 `git status`에 안 뜨는지도 확인: 기존 것은 바이트가 같아야 한다). 샘플 glb/`play.ggs`는 `GGE_WRITE_SAMPLES=<assets/samples>`로 `SampleAssetTests`를 실행해 재생성(구운 샘플이 씬/형식과 어긋나면 `BakedPlaySample_IsUpToDate`가 실패).
- **`RenderTargetBitmap`으로 `WriteableBitmap`을 다시 그려 캡처하지 말 것**: WPF 렌더 스레드가 아직 반영하지 못한 이전 프레임의 일부가 섞여 찢어진 그림이 나온다(UI 테스트가 약 1/3 확률로 실패했던 원인). 뷰포트 캡처는 비트맵 픽셀을 직접 복사한다(`CaptureViewport`). 창 전체 캡처(`CaptureWindow`)는 여전히 `RenderTargetBitmap`이다.
- Windows PowerShell 5.1에서 git이 stderr로 진행 메시지를 내서 `NativeCommandError`가 보여도 푸시는 정상인 경우가 많다(`git log origin/...`로 확인).
- 작업 트리의 줄 끝이 LF라 git이 "LF will be replaced by CRLF" 경고를 낸다(무해). 소스 일부는 CRLF가 섞여 있어, 스크립트로 편집할 때는 줄 끝을 보존해야 한다(`newline=''`).
- **한글이 든 파일을 PowerShell `Set-Content`(기본 ANSI)로 되쓰지 말 것**: 주석이 깨진다. UTF-8을 보존하는 도구/스크립트를 쓴다.
- 작업 브랜치는 `main`이 아니라 `feature/engine-viewer-skeleton`. PR 미생성.
- 엔진 깊이 버퍼 형식이 D32 -> D24S8로 바뀌었다(그림자 스텐실용). 기존 골든은 모두 그대로 통과한다.

## 에디터 UI 입력 테스트 (`tools/GunGameEditor.UiTests`)
실제 `MainWindow`를 테스트 프로세스 안의 전용 STA 스레드에 띄우고 입력을 흉내 낸다. **OS 마우스/키보드를 쓰지 않으므로** 테스트 중에도 PC를 평소처럼 쓸 수 있고, 활성 창을 빼앗지 않는다(`ShowActivated=false`).
- 버튼, 트리 선택: UI Automation 패턴(Invoke, SelectionItem). 체크박스: `ButtonBase.OnClick` 호출(Toggle 패턴은 `Click` 이벤트를 안 내서 부적합). **게임플레이 목록**은 UI Automation 항목 피어가 목록 데이터를 통째로 바꾼 뒤 낡은 항목을 들고 있어 쓸 수 없어서, 클릭이 하는 일(컨테이너 `IsSelected`)을 직접 한다. 계층 노드를 실제 마우스 클릭처럼 누르는 `ClickNode`는 `Mouse.PreviewMouseDownEvent`를 올린다(`PreviewMouseLeftButtonDown`을 직접 올리면 핸들러가 호출되지 않는다).
- 텍스트: `Text`를 채우고 Enter 키다운 또는 `LostFocus` 라우티드 이벤트 발생. 마우스 휠, 드래그 앤 드롭(DragOver/Drop)도 실제 요소에 라우티드 이벤트로 발생시켜 XAML 핸들러까지 통과.
- 게임플레이 인스펙터 칸은 순서가 아니라 `AutomationProperties.Name`(라벨)로 찾는다(`EditorSession.Field("Polygon (x, z per line)")`). `GameplayLabels()`는 항상 맨 위에 있는 "Lighting: Scene" 항목을 뺀 목록이고, 전체는 `AllListLabels()`.
- 검증 범위: 계층, 노드 인스펙터(알파/그림자 포함), 게임플레이 편집(추가/편집/거부/이동/삭제/조명/점광원 한도), 뷰포트, 씬 파일(열기/저장/Bake), 플레이 모드(Play/Stop, 편집 잠금, 방 카메라, 트리거 로그, 카메라 컷, 오버레이), 키 매핑(`PlayInputFromKeys`).
- `EditorSession.cs`의 `NameBox/PositionBox/...` 상수는 `InspectorPanel`의 TextBox 생성 순서에 의존한다(`AlphaBox`가 색 칸 뒤에 추가되어 `SpeedBox`가 14가 됐다). 인스펙터에 입력 칸을 추가하면 상수를 같이 고친다.
- `GunGameEditor`에 `InternalsVisibleTo`를 추가했다(`x:Name` 필드와 `AddMesh`/`OpenScenePath`/`StepPlay`/`OrbitDrag`/`BakeTo` 등 internal 멤버 접근용). 제품 동작은 바뀌지 않는다.
- 엔진 DLL(`gge.dll`)이 있어야 한다(`build.cmd`가 먼저 빌드). 렌더 픽셀을 보는 테스트는 DLL이 없으면 실패한다. 하드웨어 D3D 장치를 쓴다.
- **자동 테스트가 못 덮는 것**: ① 키보드 상태(`Keyboard.IsKeyDown`) 읽기 자체(키 -> 축 매핑은 `PlayInputFromKeys`로 분리해 테스트함) ② 트리에서 **끌기 시작**(`DoDragDrop`이 블로킹, 놓기/DragOver 이후 경로는 검증함) ③ **파일 대화상자**(Open/Save As/Add Mesh/OBJ to GGM/Bake/인스펙터 `...`; 대화상자 뒤의 로직은 검증함) ④ `SelfTest`(별도 프로세스 `selftest.ps1`로 검증) ⑤ **`gge_player.exe`의 창/키보드/게임패드 경로**(입력 매핑과 대본 해석은 `--self-test`, 게임 진행/화면은 헤드리스 ctest로 검증함. 창 띄우기와 종료는 손으로 한 번 확인했다).

## 열린 결정
1. ~~플레이어 모델 앞쪽을 +Z로 가정~~ **닫음(2026-10-31)**: glTF 규격(+Z 앞쪽)과 같고, 실제 파일(CesiumMan)을 헤드리스 스크린샷으로 확인했다(S로 걸으면 얼굴, W로 걸으면 뒤통수, D로 걸으면 오른쪽 옆모습). yaw와 이동 방향의 관계는 `RealModelPipelineTests.PlayerYaw_...`가 고정한다. 단, glTF 규격을 따르지 않는 모델은 모델링 툴에서 +Z가 앞이 되게 내보내야 한다.
2. 그림자를 바닥 평면 하나가 아니라 임의의 받는 면으로 넓힐지(그림자 맵) - 지금은 SIGNALIS풍 방 안 바닥에 충분하다고 보고 평면 투영으로 했다.

## 알려진 한계
- 실제 Blender `.glb`는 미검증(코드로 만든 glTF로만 검증). 지원 범위는 계획 문서 "스켈레탈 애니메이션 규약" 참고.
- 뷰포트에 선택 하이라이트/기즈모/본 시각화/재생 시간 슬라이더 없음. 게임플레이 영역은 숫자/텍스트로만 편집한다(뷰포트에서 꼭짓점을 끌 수 없음).
- 그림자: 바닥 평면 하나에만, 점광원은 그림자 없음, 알파 오브젝트 그림자도 불투명도 그대로(자세한 제약은 계획 문서). 알파: 오브젝트 단위 정렬이라 서로 교차하는 반투명 오브젝트는 정확하지 않다. 다수 동적 조명은 점광원 8개까지.
- 충돌: 회전 상자/캡슐 높이/동적 충돌체 없음, 방 폴리곤을 벽으로 쓰지 않음. 트리거: 플레이어 한 명만, 프레임 샘플링이라 한 프레임 이동 거리보다 얇은 영역은 건너뛸 수 있음.
- 입력: 탱크 컨트롤 없음, 캐릭터 회전은 이동 방향 고정 규칙뿐(엔진은 정규화 축만 받는다). 플레이어는 키보드/XInput 첫 패드만 읽는다.
- 구운 씬: 애니메이션 클립 전환/크로스페이드 없음(정적 씬은 클립이 바뀌지 않음), 텍스처는 구울 때 디코딩 검증을 하지 않고 로드할 때(플레이어 시작 또는 Bake의 검증 로드) 걸러진다. 플레이어에 오디오/UI/세이브 없음.
- 하드웨어 GPU 측정(2026-10-31, 64x48, 기본 어댑터): 평면 큐브 1000개 0.76ms, 계층 100x20 1.03ms, 스키닝 50x32 0.37ms(WARP은 4.7/9.2/0.38ms). 이 규모에선 배칭/인스턴싱이 급하지 않다. 해상도를 키운 측정과 어댑터별(Intel Arc A750/AMD 내장) 측정은 아직 없다.
- `build.cmd test-gpu`로 전체 테스트를 하드웨어 GPU에서 돌릴 수 있다(통과 확인). 어댑터를 직접 고르는 기능이 없어 기본 어댑터만 검증된다.
- `LoadGltf`(에디터 경로)가 중간에 실패하면(예: 클립 생성 거부) 이미 만든 메시/스켈레톤이 엔진에 남는다(누수, 기능 영향 없음). glTF 외부 `.bin` 등 부속 파일은 파일 변경 감지(재로드) 대상이 아니다.
- C# 뮤테이션의 남은 생존 변이 약 25개와 새 C++ 코드(렌더러 7단계 부분, `scene*.cpp`)의 뮤테이션 측정 결과는 `doc/03` 참고.

## 다음 단계 제안 (우선순위 순)
### E. 스크립팅 확장 (8단계의 기본은 완료: 위 표와 `doc/01`의 "스크립팅" 절)
8단계로 **이벤트 -> `IGameScript` -> 엔진 쓰기 API**의 한 바퀴는 돈다(독립 플레이어가 hostfxr로 .NET 호스팅). 아직 없는 것:
- **스크립트가 할 수 있는 일(P2 4단계까지)**: 오브젝트 생성/삭제(`SpawnObject`/`DestroyObject`/`ObjectExists`: 씬의 템플릿 노드 복제, 한도 256/64/16)도 가능. 클립 재생 전환(`PlayClip`/`StopClip`/`SetClipSpeed`/`SetClipPlaying`/`GetClip`, 크로스페이드/한 번 재생)과 `PlayerSpeed` 읽기도 가능. 읽기(활성 방, 플레이어 위치, 영역 안 여부, 노드 위치/회전/크기/표시)와 쓰기(트리거 켜기/끄기, **충돌체 켜기/끄기**, 텔레포트, 노드 위치/회전/크기/**표시**/알파), 동작 버튼(상호작용/취소/메뉴) 수신, 로그. 아직 없는 것: 조명 변경(복제도 조명/충돌체/트리거는 제외), 사용자 이벤트/타이머, 씬 전환, 상태 저장(세이브).
- **에디터 플레이 모드는 스크립트를 실행하지 않는다**(독립 플레이어만). 에디터에서 스크립트를 보려면 C# `EngineScene`/`ScriptRunner` 조합으로 `PlaySession` 옆에 연결해야 한다(`ScriptRunner`는 이미 엔진 래퍼만으로 동작해 xUnit에서 그렇게 쓴다).
- 스크립트 핫 리로드, 샌드박스(신뢰할 수 있는 스크립트만 가정), 한 프로세스에 스크립트 세션 하나, 배포 시 대상 PC의 .NET 10 런타임 필요(자체 포함 배포는 미구현).
- 창 모드의 스크립트 오류는 메시지 상자 + 종료다(복구/계속 실행 없음).
### G. 게임 개발을 시작하려면 (2026-10-31 기준 판단)
**지금 가능한 것**: 고정 카메라 방 + 조명/그림자/알파 + 스킨 애니메이션 모델이 있는 씬을 에디터에서 만들고, 독립 플레이어로 걸어 다니며 영역 진입/상호작용 키에 C# 스크립트가 반응해 장치(오브젝트 이동, 트리거 잠금, 방 이동)를 움직이는 **걷기+퍼즐 장치 프로토타입**. **코드에 아예 없는 것**(확인함): 오디오, 텍스트/글꼴 렌더링(UI), 레이캐스트/투사체, 체력/인벤토리/세이브, 이름 있는 충돌체(구운 `.ggs`의 충돌체는 이름이 없어 켜고 끌 수 없다 = 잠긴 문을 못 만든다).
- **1단계 - 탐험/퍼즐 슬라이스를 만들 수 있게(착수선, 필수 4개)**: (a) **텍스트/UI**: 글꼴 렌더링 + 대화창/HUD/아이템 설명(스크립트가 띄움) (b) **오디오**: 효과음/BGM 재생(스크립트 API 포함) (c) **게임 상태와 씬 전환**: 플래그/인벤토리 저장소, 스크립트가 다른 `.ggs`를 불러오기, 세이브/로드 (d) **이름 있는 충돌체 + 켜기/끄기**(`.ggs` 형식 버전 올림 필요) - 잠긴 문, 열리는 문. 여기까지 되면 대화, 아이템, 문, 단서 퍼즐로 이루어진 **실제 콘텐츠 제작을 시작해도 된다**.
- **1단계 병행(권장)**: 스크립트 API 확장(오브젝트 보이기/숨기기, 클립 재생 전환, 조명, 타이머/사용자 이벤트), 플레이어 모델의 걷기/대기 애니메이션 자동 전환과 방향 전환, 에디터 플레이 모드에서 스크립트 실행.
- **2단계 - 전투**: 조준/발사 입력, 레이캐스트(시선/탄도)와 동적 충돌체, 적 엔티티 + 방 안 추적 AI, 체력/피해/피격 연출. 1단계 위에서 콘텐츠 제작과 병행할 수 있다.
- **3단계 - 제작 환경/배포**: 뷰포트 기즈모와 시각 편집(방/영역 꼭짓점 끌기), 자체 포함 배포(.NET 런타임 동봉/설치 프로그램), 에셋 파이프라인(압축/번들), 설정 파일과 키 재매핑, CRT 후처리, 하드웨어 GPU 성능 측정(해상도 확대, 어댑터별).
- 진행 규모 감: 1단계 필수 4개는 지금까지 한 단계(예: 7단계 조명, 독립 플레이어)와 비슷한 크기의 작업 4개다. 그중 (c)와 (d)는 파일 형식/런타임 구조를 건드리고, (a)는 렌더러에 2D 경로를 새로 만드는 것이라 가장 크다.

### F. 성능/품질 병행 과제
- 어댑터 선택 기능(예: `GGE_ADAPTER`)을 엔진에 추가해 Intel/AMD 각각에서 `test-gpu`와 `engine_bench --hw` 실행.
- 실제 Blender `.glb`(스킨/애니메이션) 검증 파일 확보, 선택 하이라이트/기즈모/본 시각화, 뷰포트에서 방/영역 꼭짓점 끌기, CRT/노이즈 후처리.
- 충돌 확장(캡슐/회전 상자/동적 충돌체), 트리거를 여러 개체로 확장, 얇은 영역 터널링 방지(선분-다각형 판정).
- 그림자 받는 면 확장(그림자 맵) 또는 점광원 그림자, 반투명 면 정렬 개선.
- 플레이어: 설정 파일(해상도/전체 화면/키 재매핑), 탱크 컨트롤, 게임패드 버튼, 창 크기 조절.
- `LoadGltf` 부분 실패 시 엔진 리소스 정리, glTF 부속 파일 변경 감지.
### G. 테스트 실효성 후속
- 새 C++ 코드의 뮤테이션 생존 변이를 개별 재확인(일괄 실행의 증분 빌드로 거짓 생존이 나올 수 있다). 새 기능을 추가할 때 같은 방식으로 재측정한다.
- 에디터 코드(`MainWindow.xaml.cs`)의 대화상자 외 경로, 플레이어의 창 경로(입력 장치 읽기를 추상화해 테스트).
