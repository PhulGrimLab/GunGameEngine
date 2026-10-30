# 인수인계: 현재 상태와 다음 단계

작성 시점 기준 브랜치 `feature/engine-viewer-skeleton`, 최신 커밋 `ca58fea`. 상세 설계/규약은 `doc/01_Development_Plan.md` 참고.

## 목표
SIGNALIS 스타일(PS1풍 저해상도 3D, 고정 카메라, 3인칭 탐험/전투/퍼즐) 게임용 C++ 엔진 + C#/WPF 개발 툴.

## 완료된 단계 (0~5)
| 단계 | 내용 |
|---|---|
| 0 | CMake+Ninja 엔진, `tools/GunGameTools.slnx`, C++/C# 테스트 골격 |
| 1 | D3D11(WARP 지원) 저해상도 렌더 코어, 플랫 셰이딩, OBJ 로더, `extern "C"` API |
| 2 | P/Invoke 래퍼, WPF 뷰어(오빗 카메라), `--selftest` 화면 검증 하네스 |
| 3 | PS1풍 효과: 버텍스 스냅, 색 깊이 제한, 4x4 오더드 디더 |
| 4a | UV, 점 샘플링 텍스처(WIC), 아핀 텍스처 매핑, `.ggm` 바이너리 메시 |
| 4b | 엔진 오브젝트 계층(부모/자식), C# 씬 모델/JSON 저장, 에디터 트리·인스펙터·드래그 재부모화 |
| 5 | GPU 스키닝, 클립 샘플링/블렌딩, glTF(.glb) 임포터, `AnimationPlayer`, 에디터 재생 UI |

테스트: C++ 40개 케이스(412 체크, 골든 이미지 포함), C# 69개, 에디터 UI 입력 45개, 뷰어 selftest. 모두 통과 상태.

## 저장소 구조
```
engine/            C++ 엔진 (DLL gge.dll)
  include/gge/gge.h  공개 C API (규약은 이 파일 주석이 기준)
  src/               renderer, animation, obj_loader, mesh_io, texture_loader, capi
  tests/             engine_tests.cpp, golden/*.ppm
tools/
  GunGame.Interop/       P/Invoke 래퍼, 씬(Scene/), glTF(Gltf/), 애니메이션(Animation/)
  GunGame.Interop.Tests/ xUnit (GltfBuilder: 테스트용 glTF 생성기)
  GunGameEditor/         WPF 에디터 (SelfTest.cs = 화면 검증 하네스)
  GunGameEditor.UiTests/ 에디터 UI 입력 자동 테스트(xUnit, 아래 "에디터 UI 입력 테스트" 참고)
  selftest.ps1           에디터 화면 검증 스크립트
assets/samples/      cube.obj, checker.png, tentacle.glb, sample.scene.json
doc/                 계획·인수인계 문서
build.cmd            전체 빌드 + (test 인자 시) 전체 테스트
```

## 리셋 후 환경 복구
필요한 것: **Visual Studio 또는 Build Tools(버전 무관, "C++를 사용한 데스크톱 개발" 워크로드)**, CMake(4.x), **.NET SDK 10**, Windows SDK, git. Ninja는 필요 없다(엔진은 MSBuild로 빌드).
1. `git clone https://github.com/PhulGrimLab/GunGameEngine.git` 후 `git checkout feature/engine-viewer-skeleton`
2. `build.cmd test` 실행. VS 경로 하드코딩/`vcvars` 없이 CMake가 설치된 최신 Visual Studio를 찾아 Visual Studio 제너레이터(MSBuild)로 `-A x64` Release 빌드한다. `build\`가 다른 제너레이터(예: 이전 Ninja)로 만들어졌다면 `build\`를 지우고 다시 실행.
3. 에디터: `tools\GunGameEditor\bin\Debug\net10.0-windows\GunGameEditor.exe` → Open Scene → `assets\samples\sample.scene.json`

주의/함정:
- 엔진 출력은 `build\bin\gge.dll`로 고정(`CMakeLists.txt`에서 구성별 출력 폴더 지정). MSBuild 제너레이터는 기본으로 `bin\Release\`를 만들기 때문이다. `ctest`는 `-C Release`가 필요하다.
- 골든 이미지는 `GGE_UPDATE_GOLDEN=1`로 재생성하되 **반드시 눈으로 확인**. 샘플 glb는 `GGE_WRITE_SAMPLES=<폴더>`로 `SampleAssetTests`를 실행해 재생성.
- Windows PowerShell 5.1에서 git이 stderr로 진행 메시지를 내서 `NativeCommandError`가 보여도 푸시는 정상인 경우가 많다(`git log origin/...`로 확인).
- `CLAUDE.md`(저장소 루트)는 한글이 깨진 상태로 untracked였다. 인코딩(UTF-8) 확인 후 필요하면 커밋.
- 작업 브랜치는 `main`이 아니라 `feature/engine-viewer-skeleton`. PR 미생성.

## 에디터 UI 입력 테스트 (`tools/GunGameEditor.UiTests`)
실제 `MainWindow`를 테스트 프로세스 안의 전용 STA 스레드에 띄우고 입력을 흉내 낸다. **OS 마우스/키보드를 쓰지 않으므로** 테스트 중에도 PC를 평소처럼 쓸 수 있고, 활성 창을 빼앗지 않는다(`ShowActivated=false`).
- 버튼, 트리 선택: UI Automation 패턴(Invoke, SelectionItem). 체크박스: `ButtonBase.OnClick` 호출(Toggle 패턴은 `Click` 이벤트를 안 내서 부적합).
- 텍스트: `Text`를 채우고 Enter 키다운 또는 `LostFocus` 라우티드 이벤트 발생. 마우스 휠, 드래그 앤 드롭(DragOver/Drop)도 실제 요소에 라우티드 이벤트로 발생시켜 XAML 핸들러까지 통과.
- 검증 범위: 계층(추가/삭제/선택/New/재부모화/자기·자손 아래 이동 거부), 인스펙터(이름·위치·회전(도↔라디안)·크기·색·애니메이션 클립/재생/속도, 잘못된 입력 되돌림, 외부 변경 반영), 뷰포트(휠 줌/한계, 색 입력과 레트로 체크박스가 실제 화면 픽셀을 바꾸는지), 씬 파일(열기 성공/실패, 편집 후 Save 버튼 → 파일 재로드).
- `EditorSession.cs`의 `NameBox/PositionBox/...` 상수는 `InspectorPanel`의 TextBox 생성 순서에 의존한다. 인스펙터에 입력 칸을 추가하면 상수를 같이 고친다.
- `GunGameEditor`에 `InternalsVisibleTo`를 추가했다(`x:Name` 필드와 `AddMesh`/`OpenScenePath` 등 internal 멤버 접근용). 제품 동작은 바뀌지 않는다.
- 엔진 DLL(`gge.dll`)이 있어야 한다(`build.cmd`가 먼저 빌드). 렌더 픽셀을 보는 테스트와 클립 목록 테스트는 DLL이 없으면 실패한다. 하드웨어 D3D 장치를 쓴다.
- 결함 주입으로 실효 확인: 줌 방향 반전, 드롭 대상 무시, 회전 도/라디안 변환 누락을 각각 넣었을 때 해당 테스트가 실패함을 확인.

## 열린 결정
1. **런타임 씬 로더**: 현재 씬 JSON은 C#만 읽는다. 독립 실행 C++ 플레이어(5단계 이후)를 위해 (a) C++ JSON 로더 또는 (b) 에디터가 바이너리 씬으로 굽기(bake). **권장: (b)**. 메시/텍스처/glTF도 같은 bake 단계에서 `.ggm` 등으로 변환.
2. 애니메이션 상태(루프/크로스페이드)는 현재 C# `AnimationPlayer`. 플레이어를 C++로 만들 때 이식 필요.

## 알려진 한계
- 에디터 UI 입력 중 자동 테스트가 못 덮는 것: ① 뷰포트 **마우스 드래그 오빗**(핸들러가 `e.GetPosition`으로 실제 커서 위치를 읽어 합성 이벤트로는 좌표를 줄 수 없음. 해결하려면 `OnMouseMove`의 회전 계산을 좌표를 인자로 받는 메서드로 분리해야 함) ② 트리에서 **끌기 시작**(`DoDragDrop`이 블로킹이고 같은 이유로 `GetPosition` 사용. 놓기/DragOver 이후 경로는 검증함) ③ **파일 대화상자**(Open/Save As/Add Mesh/OBJ to GGM/인스펙터 `...`). 대화상자 뒤의 로직(`AddMesh`, `OpenScenePath`, Save)은 검증함.
- 실제 Blender `.glb`는 미검증(코드로 만든 glTF로만 검증). 지원 범위는 계획 문서 "스켈레탈 애니메이션 규약" 참고.
- 뷰포트에 선택 하이라이트/기즈모/본 시각화/재생 시간 슬라이더 없음.
- 그림자, 다수 동적 조명, 투명(알파)은 미구현(7단계).

## 다음 단계 제안
### 6단계: 게임플레이 기반 (고정 카메라, 충돌, 입력, 트리거)
권장 순서와 검증:
1. **고정 카메라 시스템**: 카메라 노드(위치/타깃/FOV)를 씬 노드로 추가, "방(영역)" 단위로 활성 카메라 전환. 에디터에서 카메라 배치/미리보기 도구.
   - 검증: 영역 진입 시 올바른 카메라가 선택되는 단위 테스트, 전환 전후 렌더 골든.
2. **충돌**: 단순 형상(AABB/캡슐) + 이동 충돌 해결(벽 슬라이딩). 에디터에서 충돌체 편집/표시.
   - 검증: 벽 앞에서 이동량 보정, 모서리/끼임 케이스 단위 테스트.
3. **입력**: 키보드/게임패드 추상화, 고정 카메라 기준 이동(탱크 컨트롤 vs 카메라 상대 이동 선택 필요).
4. **트리거**: 영역 진입/퇴장 이벤트(카메라 전환, 문, 아이템).
5. 플레이 모드 미리보기(에디터 안에서 캐릭터 조작).

### 이후
- 7단계: 다수 동적 조명, 그림자, 알파 투명.
- 8단계: C# 스크립팅(.NET 호스팅).
- 병행: 런타임 씬 bake + 독립 실행 플레이어, 선택 하이라이트/기즈모, 실제 Blender 파일 검증, CRT/노이즈 후처리.

### 6단계 시작 전에 정할 것
- 이동 방식: 탱크 컨트롤(레지던트 이블식) vs 카메라 상대 이동(SIGNALIS는 카메라 상대 이동에 가까움 — 확인 필요).
- 고정 카메라 영역 정의 방식: 박스 볼륨 vs 폴리곤 영역.
- 플레이어/적 캐릭터를 C++ 엔진에서 처리할지, 6단계에서는 C# 툴 측 프로토타입으로 먼저 검증할지.
