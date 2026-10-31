# 외부 에셋 (테스트 픽스처)

임포터/스키닝/굽기를 **실제 파일**로 검증하기 위한 공개 glTF 샘플이다. 코드로 만든 glTF(`GltfBuilder`)로는 드러나지 않는 문제(아마추어/Z-up 변환, 인스턴싱, 다중 재질,
Blender/COLLADA 산출물의 노드 구조)를 잡으려는 것이 목적이며, 테스트(`RealGltfModelTests`, `StaticGltfModelTests`, `RealModelPipelineTests`)가 이 파일들을 직접 읽는다.

> **게임 콘텐츠로 배포하지 않는다.** 아래 모델은 테스트용이다. 특히 CesiumMan과 CesiumMilkTruck에는 Cesium의 로고/상표가 들어 있고, 모델의 CC-BY 라이선스는
> 그 로고의 사용권을 주지 않는다(Cesium이 조건을 바꿀 수 있다고 명시: [LicenseRef-LegalMark-Cesium](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/LICENSES/LicenseRef-LegalMark-Cesium.txt)).

## 원본 (Khronos glTF-Sample-Assets, 수정 없이 그대로)
출처: https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/<모델 이름>/glTF-Binary . 각 폴더의 `README.upstream.md`는 그 저장소의 모델 README 사본이다.

| 폴더/파일 | 특징(이 프로젝트에서 검증하는 것) | 라이선스와 크레딧 |
|---|---|---|
| `RiggedSimple/RiggedSimple.glb` | COLLADA2GLTF 산출물, 스킨 본 2개, Z-up 노드 + 아마추어 `matrix`(조상 변환 합성 본 검증) | CC BY 4.0, (c) 2017 Cesium |
| `CesiumMan/CesiumMan.glb` | 스킨 본 19개 + 걷기 애니메이션 + 텍스처, Z-up/아마추어 변환(**모델이 눕던 결함의 재현 파일**), 앞쪽 +Z 확인 | CC BY 4.0, (c) 2017 Cesium (+ Cesium 로고/상표, 위 참고) |
| `Fox/Fox.glb` | 스킨 본 24개, 클립 3개(Survey/Walk/Run), 인덱스 없는 메시, 크기가 약 100배(cm 단위) | 모델 PixelMannen CC0 1.0, 리깅/애니메이션 tomkranis CC BY 4.0, glTF 변환 @AsoboStudio/@scurest CC BY 4.0 |
| `CesiumMilkTruck/CesiumMilkTruck.glb` | **실제 Blender 산출물**(Khronos glTF Blender I/O 1.0.5), 스킨 없음, 노드 계층 + 같은 메시를 쓰는 노드 2개 + 프리미티브 3개 + 재질 4개 + 노드 애니메이션 | CC BY 4.0, (c) 2017 Cesium (+ Cesium 로고/상표, 위 참고) |

CC BY 4.0 표기: 위 모델은 해당 저작자의 저작물이며 [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/) 라이선스로 사용한다. 변경 사항 없음(원본 glb 바이트 그대로).

원본 SHA-256(앞 16자리): RiggedSimple `3A79DABB67BB0CD5`, CesiumMan `B7001EAEEA8254BD`, Fox `D97044E701822BAC`, CesiumMilkTruck `09371B34608116DE`.

## 파생물 (이 저장소에서 만든 것)
| 파일 | 만든 방법 |
|---|---|
| `*/*_baseColor.png`, `*/*_baseColor.jpg` | 원본 glb에 내장된 이미지를 그대로 꺼낸 것(재인코딩 없음). 엔진은 glb의 재질/텍스처를 가져오지 않아 씬 노드에 텍스처를 따로 지정해야 하기 때문이다. 원본과 같은 라이선스. |
| `demo/real_models.scene.json` | 위 모델들을 한 방에 놓은 데모 씬(CesiumMan이 플레이어). 이 저장소의 코드/데이터라 `LICENSE`를 따른다. |

텍스처 추출 방법(재현 가능):
```
python -I tools/extract_glb_texture.py assets/external/CesiumMan/CesiumMan.glb assets/external/CesiumMan/CesiumMan_baseColor
python -I tools/extract_glb_texture.py assets/external/Fox/Fox.glb assets/external/Fox/Fox_baseColor
python -I tools/extract_glb_texture.py assets/external/CesiumMilkTruck/CesiumMilkTruck.glb assets/external/CesiumMilkTruck/CesiumMilkTruck_baseColor
```
구운 데모 보기: `set GGE_WRITE_DEMO=build\demo` 후 `dotnet test tools\GunGame.Interop.Tests --filter RealModelPipelineTests`, 그다음
`build\bin\gge_player.exe build\demo\real_models.ggs`(또는 `--headless --warp --keys S:20 --screenshot out.ppm`).

## 일부러 넣지 않은 것
- `VirtualCity`(Khronos 샘플): "3DRT license with allowances for glTF Testing"이라 재배포 조건이 불리해 저장소에 두지 않았다. 정적/다중 노드 경로는 CesiumMilkTruck으로 대신 검증한다.
