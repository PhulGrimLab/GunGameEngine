# Galmuri11 (한글 비트맵 폰트)

게임 화면 글자(P3)용 폰트다. 엔진은 이 **BDF 비트맵 파일을 원본 그대로** 런타임에 읽는다(`engine/src/font.*`). 폰트를 고치거나 다시 내보내지 않는다.

| 항목 | 값 |
|---|---|
| 폰트 | Galmuri11 (Regular, 11px 계열, 비례폭) |
| 저작권자 | Copyright (c) 2019-2025 Lee Minseo (quiple@quiple.dev) |
| 출처 | https://github.com/quiple/galmuri/releases/tag/v2.40.4 (`Galmuri-v2.40.4.zip` 안의 `Galmuri11.bdf`, `LICENSE.txt`) |
| 폰트 버전 | 2.404 (BDF의 `FONT_VERSION`), 릴리스 v2.40.4 (2026-07-12) |
| 라이선스 | SIL Open Font License, Version 1.1 — 전문은 같은 폴더의 `LICENSE.txt`(원본 그대로) |
| `Galmuri11.bdf` | 2,902,129 바이트, SHA-256 `98716D08AA5762E6CE440D5A757D107CB5D3F32A53861878BE06C00AACC23304` |
| `LICENSE.txt` | 4,360 바이트, SHA-256 `86A3EE9495F942F0243F18C103DA9FACA27ADB88142613EDB8BB852E56C892C1` |
| 글리프 | 20965개: 현대 한글 완성형 11172자 전부, 인쇄 가능 ASCII 전부, `U+FFFD`, `.notdef`(빈 네모). `CHARS`는 `.notdef`를 세지 않는다(파서가 두 방식 모두 허용) |

## OFL 1.1 의무 (게임을 배포할 때)
- **이 폰트 파일을 게임과 함께 배포하면 `LICENSE.txt`(저작권 표기 + OFL 전문)를 같이 넣는다.** 폰트를 엔진 DLL에 내장하지 않고 파일로 두는 이유 중 하나다. 빌드는 `gge_player.exe` 옆에 `Galmuri11.bdf`와 `Galmuri11.LICENSE.txt`(= 이 폴더의 `LICENSE.txt`)를 복사하고(post-build), ctest가 두 파일이 원본과 같은지 확인한다. 게임을 배포할 때는 그 두 파일을 실행 파일 옆에 그대로 둔다(P10 체크리스트).
- **폰트 파일 단독으로 판매하지 않는다.** 게임/소프트웨어에 묶어 배포하거나 판매하는 것은 허용된다.
- 폰트를 고쳐서(파생물) 배포하면 같은 OFL을 따라야 하고 **예약 폰트명**을 쓰면 안 된다. 이 저장소는 **원본 무수정**이라 파생물이 없다. 이 폰트의 라이선스 문구에는 "Reserved Font Name"이 따로 지정되어 있지 않다(OFL 전문의 정의 문구만 있다).
- 폰트를 다른 라이선스로 바꿀 수 없다.

## 무결성과 교체 절차
- 테스트(`FontFileTests`)가 위 SHA-256과 일치하는지 확인한다. 파일이 바뀌면(다른 버전/변형) 테스트가 실패한다.
- **폰트를 바꾸려면**: ① 새 파일과 `LICENSE.txt`를 내려받아 이 표(버전, 출처, 크기, SHA-256)를 갱신 ② 11172자 전수/ASCII 시험이 통과하는지 확인 ③ **한글 골든(`engine/tests/golden/player_dialog_160x120.ppm` 등)을 다시 만든다**(`build.cmd test`가 실패하면 만든 `build\player_*.ppm`을 PNG로 확대해 눈으로 확인한 뒤 골든 위치에 복사) ④ 테스트 상수의 해시를 갱신.
- `Galmuri14.bdf`(2.9MB)는 큰 해상도가 필요해질 때까지 두지 않는다(저장소 용량).
