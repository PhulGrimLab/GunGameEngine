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

테스트: C++ 40개 케이스(412 체크, 골든 이미지 포함), C# 69개, 뷰어 selftest. 모두 통과 상태.

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
  selftest.ps1           에디터 화면 검증 스크립트
assets/samples/      cube.obj, checker.png, tentacle.glb, sample.scene.json
doc/                 계획·인수인계 문서
build.cmd            전체 빌드 + (test 인자 시) 전체 테스트
```

## 리셋 후 환경 복구
필요한 것: **Visual Studio 2026(버전 18) Community + "C++를 사용한 데스크톱 개발"**, CMake(4.x), Ninja, **.NET SDK 10**, Windows SDK, git.
1. `git clone https://github.com/PhulGrimLab/GunGameEngine.git` 후 `git checkout feature/engine-viewer-skeleton`
2. `build.cmd test` 실행 (VS 경로가 `C:\Program Files\Microsoft Visual Studio\18\Community`로 하드코딩되어 있으니 다르면 `build.cmd` 상단 수정)
3. 에디터: `tools\GunGameEditor\bin\Debug\net10.0-windows\GunGameEditor.exe` → Open Scene → `assets\samples\sample.scene.json`

주의/함정:
- `build.cmd`는 `vcvars64`가 설정하는 `Platform=x64`를 dotnet 빌드 전에 비운다(안 비우면 솔루션 구성 오류).
- 골든 이미지는 `GGE_UPDATE_GOLDEN=1`로 재생성하되 **반드시 눈으로 확인**. 샘플 glb는 `GGE_WRITE_SAMPLES=<폴더>`로 `SampleAssetTests`를 실행해 재생성.
- Windows PowerShell 5.1에서 git이 stderr로 진행 메시지를 내서 `NativeCommandError`가 보여도 푸시는 정상인 경우가 많다(`git log origin/...`로 확인).
- `CLAUDE.md`(저장소 루트)는 한글이 깨진 상태로 untracked였다. 인코딩(UTF-8) 확인 후 필요하면 커밋.
- 작업 브랜치는 `main`이 아니라 `feature/engine-viewer-skeleton`. PR 미생성.

## 열린 결정
1. **런타임 씬 로더**: 현재 씬 JSON은 C#만 읽는다. 독립 실행 C++ 플레이어(5단계 이후)를 위해 (a) C++ JSON 로더 또는 (b) 에디터가 바이너리 씬으로 굽기(bake). **권장: (b)**. 메시/텍스처/glTF도 같은 bake 단계에서 `.ggm` 등으로 변환.
2. 애니메이션 상태(루프/크로스페이드)는 현재 C# `AnimationPlayer`. 플레이어를 C++로 만들 때 이식 필요.

## 알려진 한계
- 에디터 UI 입력(드래그 앤 드롭, 인스펙터 입력, 파일 대화상자)은 자동 테스트 없음(창 캡처로 표시만 확인).
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
