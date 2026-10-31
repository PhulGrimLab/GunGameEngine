# .NET hosting headers

`hostfxr.h`와 `coreclr_delegates.h`는 .NET SDK의 `Microsoft.NETCore.App.Host.win-x64` 팩
(`runtimes/win-x64/native`, 10.0.12)에서 그대로 복사한 것이다. .NET Foundation의 MIT 라이선스이며 파일 머리말에 표기가 남아 있다.
독립 플레이어(`engine/player/script_host.cpp`)가 hostfxr로 .NET 런타임을 띄울 때만 쓴다. 수정하지 않는다.
