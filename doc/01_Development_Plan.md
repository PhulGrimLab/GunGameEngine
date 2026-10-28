# GunGameEngine 개발 계획

## 목표
SIGNALIS 스타일(PS1풍 저해상도 3D, 고정 카메라, 3인칭 탐험/전투/퍼즐)의 게임을 만드는 C++ 엔진과 C#/WPF 개발 툴.

## 기술 결정
| 항목 | 결정 | 이유 |
|---|---|---|
| 렌더러 | D3D11, 저해상도 오프스크린 렌더 타깃 후 최근접 업스케일 | 텍스처/조명/그림자/스키닝/후처리를 감당하면서 PS1 룩은 후처리로 구현 |
| 툴 표시 | 엔진이 CPU로 읽은 BGRA 프레임을 WPF `WriteableBitmap`에 복사 | 해상도가 낮아 비용이 작고 D3DImage 연동이 필요 없음 |
| C++ ↔ C# | `extern "C"` API + P/Invoke (`engine/include/gge/gge.h`) | C++/CLI보다 단순, 컴파일러 버전에 덜 묶임 |
| 좌표계 | 오른손, Y-up, 앞면 CCW | 문서로 고정 |
| 카메라 | 고정 카메라가 1급 개념 (방마다 앵글 배치) | 목표 게임 장르 |
| 스크립트 | C#, .NET 호스팅은 후순위 | 큰 별도 과제 |

## 단계와 검증
| 단계 | 내용 | 자동 검증 | 상태 |
|---|---|---|---|
| 0. 골격 | CMake+Ninja, .slnx, 테스트 프로젝트 | `build.cmd test` | 완료 |
| 1. 렌더 코어 | D3D11(WARP 지원), 저해상도 RT, 메시+단일 조명, OBJ 로더 | `engine/tests` 9개 케이스(골든 이미지 포함) | 완료 |
| 2. C API + WPF 뷰어 | P/Invoke 래퍼, 오빗 카메라, Open OBJ, 마우스 조작 | xUnit 11개 + `tools/selftest.ps1`(WPF 비트맵을 PNG로 저장해 검증) | 완료 |
| 3. 레트로 후처리 | 디더링, 팔레트 제한, 버텍스 스냅, 아핀 텍스처, CRT/노이즈 | 옵션별 골든 이미지 | 예정 |
| 4. 씬/에셋 | 씬 그래프, 자체 에셋 포맷, 에디터 계층/인스펙터 | 저장→로드 왕복 후 동일 렌더 | 예정 |
| 5. 애니메이션 | 스켈레탈 스키닝, 클립 블렌딩 | 특정 프레임의 본 행렬 검증 | 예정 |
| 6. 게임플레이 기반 | 고정 카메라 전환, 충돌, 입력, 트리거 | 전환/충돌 단위 테스트 | 예정 |
| 7. 조명/그림자/투명 | 다수 동적 조명, 그림자, 알파 | 시나리오별 골든 이미지 | 예정 |
| 8. C# 스크립트 | .NET 호스팅, 스크립트 API | 스크립트가 엔티티를 제어하는 테스트 | 예정 |

## 테스트 하네스
- **C++**: `engine/tests/engine_tests.cpp`. 외부 프레임워크 없이 C API 경계를 검증. 골든 이미지는 `engine/tests/golden/*.ppm`, `GGE_UPDATE_GOLDEN=1`로 재생성(재생성 후에는 반드시 이미지를 눈으로 확인).
- **C#**: `tools/GunGame.Interop.Tests` (xUnit). 실제 `gge.dll`을 WARP로 호출.
- **WPF 화면**: `GunGameEditor.exe --selftest <obj> <png>`. OS 화면 캡처 없이 앱이 그린 비트맵을 직접 저장.
- 테스트는 GPU 대신 WARP(소프트웨어 어댑터)를 사용해 결과를 안정화한다.

## 빌드
`build.cmd` (빌드), `build.cmd test` (빌드 + 전체 테스트). 요구: VS 2026(MSVC 14.51), CMake, Ninja, .NET SDK 10.
