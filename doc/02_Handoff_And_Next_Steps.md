# 인수인계: 현재 상태와 다음 단계

브랜치 `feature/engine-viewer-skeleton`(PR 미생성). 상세 설계/규약은 `doc/01_Development_Plan.md`, 테스트 실효성 점검 결과와 재현 방법은 `doc/03_Test_Quality_Report.md` 참고.

## 목표
SIGNALIS 스타일(PS1풍 저해상도 3D, 고정 카메라, 3인칭 탐험/전투/퍼즐) 게임용 C++ 엔진 + C#/WPF 개발 툴.

## 완료된 단계 (0~6)
| 단계 | 내용 |
|---|---|
| 0 | CMake 엔진, `tools/GunGameTools.slnx`, C++/C# 테스트 골격 |
| 1 | D3D11(WARP 지원) 저해상도 렌더 코어, 플랫 셰이딩, OBJ 로더, `extern "C"` API |
| 2 | P/Invoke 래퍼, WPF 뷰어(오빗 카메라), `--selftest` 화면 검증 하네스 |
| 3 | PS1풍 효과: 버텍스 스냅, 색 깊이 제한, 4x4 오더드 디더 |
| 4a | UV, 점 샘플링 텍스처(WIC), 아핀 텍스처 매핑, `.ggm` 바이너리 메시 |
| 4b | 엔진 오브젝트 계층(부모/자식), C# 씬 모델/JSON 저장, 에디터 트리·인스펙터·드래그 재부모화 |
| 5 | GPU 스키닝, 클립 샘플링/블렌딩, glTF(.glb) 임포터, `AnimationPlayer`, 에디터 재생 UI |
| 6 | 게임플레이 기반: 폴리곤 고정 카메라 방, AABB 충돌(벽 슬라이딩), 카메라 상대 입력(컷 래치), 트리거(진입/퇴장 이벤트), **에디터 플레이 모드**(Play/Stop, 영역 표시) |

6단계의 규약(데이터 형식, 이벤트 순서, 래치 규칙, 플레이 모드 동작)은 `doc/01_Development_Plan.md`의 "게임플레이 월드 규약", "플레이 모드 (에디터)" 절이 기준이다.

## 최근 작업 이력 (이번 세션)
1. 고정 카메라 방 -> 충돌 -> 카메라 상대 입력 -> 트리거 -> 에디터 플레이 모드 구현(각 단계에 테스트 동반, 결함 주입으로 실효성 확인).
2. 코드 리뷰: 렌더러 월드 행렬 프레임 캐시(깊은 계층 약 10% 개선), 로더 3건, glTF 노드 순환 무한 루프 등을 **재현 테스트 후** 수정. 재현되지 않은 지적은 수정하지 않고 사유를 기록(`01_Development_Plan.md` "코드 리뷰 후속 처리").
3. 리소스 해제 API(`gge_*_destroy`)와 에디터 파일 변경 감지 재로드.
4. 테스트 실효성 점검(뮤테이션 + 커버리지): C++ 엔진 유효 변이 처리율 65.7% -> 81.1%, C# 라인 커버리지 88.8% -> 91.7%. 상세는 `doc/03_Test_Quality_Report.md`.

## 현재 테스트 (`build.cmd test`, 모두 통과)
- C++ 엔진: 94개 케이스, 35005 체크(골든 이미지 포함)
- C# `GunGame.Interop.Tests`: 146개
- 에디터 UI 입력 `GunGameEditor.UiTests`: 56개
- 뷰어 selftest: OK
- 수동 도구(ctest 미등록): `build\bin\engine_bench.exe`(CPU 프레임 비용), `tools/mutation/*`(뮤테이션/커버리지 하네스)

## 저장소 구조
```
engine/            C++ 엔진 (DLL gge.dll)
  include/gge/gge.h  공개 C API (규약은 이 파일 주석이 기준)
  src/               renderer, animation, obj_loader, mesh_io, texture_loader, capi,
                     world(방/입력/트리거), collision, polygon(방·트리거 공용)
  tests/             engine_tests.cpp, engine_bench.cpp, golden/*.ppm
tools/
  GunGame.Interop/       P/Invoke 래퍼(EngineRenderer, EngineWorld), 씬(Scene/: 문서, 직렬화, SceneSync,
                         GameplayData, PlaySession, GameplayOverlay), glTF(Gltf/), 애니메이션(Animation/)
  GunGame.Interop.Tests/ xUnit (GltfBuilder: 테스트용 glTF 생성기)
  GunGameEditor/         WPF 에디터 (SelfTest.cs = 화면 검증 하네스)
  GunGameEditor.UiTests/ 에디터 UI 입력 자동 테스트(xUnit, 아래 "에디터 UI 입력 테스트" 참고)
  mutation/              뮤테이션/커버리지 하네스 스크립트(doc/03 참고)
  selftest.ps1           에디터 화면 검증 스크립트
assets/samples/      cube.obj, checker.png, tentacle.glb, sample.scene.json, play.scene.json(플레이 모드 샘플)
doc/                 계획·인수인계·테스트 실효성 보고서
build.cmd            전체 빌드 + (test 인자 시) 전체 테스트
```

## 리셋 후 환경 복구
필요한 것: **Visual Studio 또는 Build Tools(버전 무관, "C++를 사용한 데스크톱 개발" 워크로드)**, CMake(4.x), **.NET SDK 10**, Windows SDK, git, (뮤테이션 하네스용) Python 3. Ninja는 필요 없다(엔진은 MSBuild로 빌드).
1. `git clone https://github.com/PhulGrimLab/GunGameEngine.git` 후 `git checkout feature/engine-viewer-skeleton`
2. `build.cmd test` 실행. VS 경로 하드코딩/`vcvars` 없이 CMake가 설치된 최신 Visual Studio를 찾아 Visual Studio 제너레이터(MSBuild)로 `-A x64` Release 빌드한다. `build\`가 다른 제너레이터(예: 이전 Ninja)로 만들어졌다면 `build\`를 지우고 다시 실행.
3. 에디터: `tools\GunGameEditor\bin\Debug\net10.0-windows\GunGameEditor.exe` → Open Scene → `assets\samples\play.scene.json` → **Show gameplay**로 영역 확인, **Play**로 조작(WASD/방향키, Stop으로 종료).

주의/함정:
- 엔진 출력은 `build\bin\gge.dll`로 고정(`CMakeLists.txt`에서 구성별 출력 폴더 지정). MSBuild 제너레이터는 기본으로 `bin\Release\`를 만들기 때문이다. `ctest`는 `-C Release`가 필요하다.
- 골든 이미지는 `GGE_UPDATE_GOLDEN=1`로 재생성하되 **반드시 눈으로 확인**. 샘플 glb는 `GGE_WRITE_SAMPLES=<폴더>`로 `SampleAssetTests`를 실행해 재생성.
- Windows PowerShell 5.1에서 git이 stderr로 진행 메시지를 내서 `NativeCommandError`가 보여도 푸시는 정상인 경우가 많다(`git log origin/...`로 확인).
- 작업 트리의 줄 끝이 LF라 git이 "LF will be replaced by CRLF" 경고를 낸다(무해). 소스 일부는 CRLF가 섞여 있어, 스크립트로 편집할 때는 줄 끝을 보존해야 한다(`newline=''`).
- **한글이 든 파일을 PowerShell `Set-Content`(기본 ANSI)로 되쓰지 말 것**: 주석이 깨진다. UTF-8을 보존하는 도구/스크립트를 쓴다.
- 작업 브랜치는 `main`이 아니라 `feature/engine-viewer-skeleton`. PR 미생성.

## 에디터 UI 입력 테스트 (`tools/GunGameEditor.UiTests`)
실제 `MainWindow`를 테스트 프로세스 안의 전용 STA 스레드에 띄우고 입력을 흉내 낸다. **OS 마우스/키보드를 쓰지 않으므로** 테스트 중에도 PC를 평소처럼 쓸 수 있고, 활성 창을 빼앗지 않는다(`ShowActivated=false`).
- 버튼, 트리 선택: UI Automation 패턴(Invoke, SelectionItem). 체크박스: `ButtonBase.OnClick` 호출(Toggle 패턴은 `Click` 이벤트를 안 내서 부적합).
- 텍스트: `Text`를 채우고 Enter 키다운 또는 `LostFocus` 라우티드 이벤트 발생. 마우스 휠, 드래그 앤 드롭(DragOver/Drop)도 실제 요소에 라우티드 이벤트로 발생시켜 XAML 핸들러까지 통과.
- 검증 범위: 계층, 인스펙터(편집한 칸만 반영, 잘못된 입력 되돌림 포함), 뷰포트(휠 줌, 마우스 드래그 오빗은 `OrbitDrag`로 좌표를 직접 넣어 검증), 씬 파일(열기 성공/실패, 저장 실패 표시), 플레이 모드(Play/Stop, 편집 잠금, 방 카메라, 트리거 로그, 카메라 컷, 오버레이).
- `EditorSession.cs`의 `NameBox/PositionBox/...` 상수는 `InspectorPanel`의 TextBox 생성 순서에 의존한다. 인스펙터에 입력 칸을 추가하면 상수를 같이 고친다.
- `GunGameEditor`에 `InternalsVisibleTo`를 추가했다(`x:Name` 필드와 `AddMesh`/`OpenScenePath`/`StepPlay`/`OrbitDrag` 등 internal 멤버 접근용). 제품 동작은 바뀌지 않는다.
- 엔진 DLL(`gge.dll`)이 있어야 한다(`build.cmd`가 먼저 빌드). 렌더 픽셀을 보는 테스트는 DLL이 없으면 실패한다. 하드웨어 D3D 장치를 쓴다.
- **자동 테스트가 못 덮는 것**: ① 키보드 상태(`Keyboard.IsKeyDown`)를 읽는 `ReadPlayInput`의 키 매핑(OS 입력이라 만들 수 없음. 대신 타이머가 부르는 `StepPlay`를 직접 호출) ② 트리에서 **끌기 시작**(`DoDragDrop`이 블로킹, 놓기/DragOver 이후 경로는 검증함) ③ **파일 대화상자**(Open/Save As/Add Mesh/OBJ to GGM/인스펙터 `...`; 대화상자 뒤의 로직은 검증함) ④ `SelfTest`(별도 프로세스 `selftest.ps1`로 검증).

## 열린 결정
1. **런타임 씬 로더**: 현재 씬 JSON은 C#만 읽는다. 독립 실행 C++ 플레이어를 위해 (a) C++ JSON 로더 또는 (b) 에디터가 바이너리 씬으로 굽기(bake). **권장: (b)**. 메시/텍스처/glTF/게임플레이 데이터도 같은 bake 단계에서 변환.
2. 애니메이션 상태(루프/크로스페이드)는 현재 C# `AnimationPlayer`. 독립 플레이어를 C++로 만들 때 이식 필요. 6단계의 게임플레이 로직(월드)은 이미 C++에 있다.
3. 플레이어 모델 앞쪽을 +Z로 가정하고 이동 방향으로 회전한다(Blender 에셋 규약과 맞는지 실제 파일로 확인 필요).

## 알려진 한계
- 실제 Blender `.glb`는 미검증(코드로 만든 glTF로만 검증). 지원 범위는 계획 문서 "스켈레탈 애니메이션 규약" 참고.
- 뷰포트에 선택 하이라이트/기즈모/본 시각화/재생 시간 슬라이더 없음.
- 그림자, 다수 동적 조명, 투명(알파)은 미구현(7단계).
- 충돌: 회전 상자/캡슐 높이/동적 충돌체 없음, 방 폴리곤을 벽으로 쓰지 않음. 트리거: 플레이어 한 명만, 프레임 샘플링이라 한 프레임 이동 거리보다 얇은 영역은 건너뛸 수 있음.
- 입력: 탱크 컨트롤 없음, 캐릭터 회전은 이동 방향 고정 규칙뿐, 게임패드 읽기 없음(엔진은 정규화 축만 받는다).
- 게임플레이 데이터(방/충돌체/트리거/플레이어)의 **에디터 편집 UI가 없다**(JSON 직접 편집 + 오버레이로 확인).
- 렌더 성능은 WARP 기준으로만 측정했다(하드웨어 GPU 미측정). 프레임 비용의 약 98%는 오브젝트별 D3D 호출이라 더 줄이려면 배칭/인스턴싱이 필요하다.
- `LoadGltf`가 중간에 실패하면(예: 클립 생성 거부) 이미 만든 메시/스켈레톤이 엔진에 남는다(누수, 기능 영향 없음).
- glTF 외부 `.bin` 등 부속 파일은 파일 변경 감지(재로드) 대상이 아니다.

## 다음 단계 제안 (우선순위 순)
### A. 게임플레이 데이터 에디터 편집 UI (6단계 마무리, 가장 체감이 큼)
지금은 좌표를 JSON으로 직접 써야 한다. 최소 범위: ① 방 추가(현재 뷰 카메라를 방 카메라로, 다각형은 기본 사각형 후 꼭짓점 편집) ② 충돌체/트리거 추가와 이동 ③ 플레이어/스폰 지정 ④ 저장/로드(이미 지원). 검증: UI 입력 테스트 + 저장->로드 왕복.
### B. 테스트 실효성 후속
- **C# 뮤테이션 실행**: `tools/mutation/mut_cs.py`를 아직 돌리지 못했다(C++만 완료). 임포터/직렬화/SceneSync/PlaySession 대상. 생존 변이는 반드시 개별 재확인(일괄 실행의 증분 빌드로 거짓 생존이 나올 수 있다).
- 남은 C++ 생존 변이는 대부분 동등 변이로 분류했다(`doc/03`). 새 코드를 추가할 때 같은 방식으로 재측정한다.
- 에디터 코드(`MainWindow.xaml.cs` 커버리지 72%)의 대화상자 외 경로 보강, `ReadPlayInput`의 키 매핑을 순수 함수로 분리해 테스트.
### C. 7단계: 조명/그림자/투명
다수 동적 조명, 그림자, 알파. 검증은 시나리오별 골든 이미지(눈으로 확인 후 고정).
### D. 독립 실행 플레이어 + 런타임 씬 bake
열린 결정 1번. 에디터 없이 `play.scene.json` 같은 씬을 C++ 플레이어로 실행(입력 장치 읽기 포함).
### E. 8단계: C# 스크립팅(.NET 호스팅)
트리거 이벤트가 이미 `tag`로 스크립트 연결 지점을 제공한다. 스크립트가 엔티티를 제어하는 테스트.
### F. 성능/품질 병행 과제
- 하드웨어 GPU에서 `engine_bench` 측정 후 배칭/인스턴싱 필요 여부 판단.
- 실제 Blender `.glb`(스킨/애니메이션) 검증 파일 확보, 선택 하이라이트/기즈모/본 시각화, CRT/노이즈 후처리.
- 충돌 확장(캡슐/회전 상자/동적 충돌체), 트리거를 여러 개체로 확장, 얇은 영역 터널링 방지(선분-다각형 판정).
- `LoadGltf` 부분 실패 시 엔진 리소스 정리, glTF 부속 파일 변경 감지.
