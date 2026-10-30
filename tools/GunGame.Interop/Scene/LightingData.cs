using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>점광원: 위치에서 모든 방향으로 비추며 Range에서 정확히 0이 되는 거리 감쇠를 갖는다(엔진 규약은 gge.h).</summary>
public sealed class PointLightDef
{
    public string Name { get; set; } = "Light";
    public Vec3 Position { get; set; }
    public Vec3 Color { get; set; } = new(1, 1, 1);
    public float Range { get; set; } = 6f;
}

/// <summary>
/// 씬의 조명 설정: 앰비언트, 방향광("해"), 점광원 최대 8개, 바닥 평면 그림자. 기본값은 조명 기능이 없던 때의 고정 조명
/// (앰비언트 0.25 + 방향광 0.75)과 같아서, 조명 섹션이 없는 이전 씬 파일은 이전과 똑같이 보인다.
/// </summary>
public sealed class LightingData
{
    /// <summary>엔진이 지원하는 점광원 수의 상한(gge.h의 kMaxPointLights).</summary>
    public const int MaxPointLights = 8;

    public static readonly Vec3 DefaultAmbient = new(0.25f, 0.25f, 0.25f);
    public static readonly Vec3 DefaultSunDirection = new(-0.4f, -1f, -0.6f);
    public static readonly Vec3 DefaultSunColor = new(0.75f, 0.75f, 0.75f);
    public const float DefaultShadowOpacity = 0.5f;

    public Vec3 Ambient { get; set; } = DefaultAmbient;

    /// <summary>방향광이 나아가는 방향(0이 아닌 벡터, 엔진이 정규화). 그림자는 이 방향이 아래를 향할 때만 생긴다.</summary>
    public Vec3 SunDirection { get; set; } = DefaultSunDirection;
    public Vec3 SunColor { get; set; } = DefaultSunColor;
    public List<PointLightDef> PointLights { get; } = [];

    public bool ShadowEnabled { get; set; }

    /// <summary>그림자가 떨어지는 평면의 높이(월드 Y). 바닥 메시의 높이와 맞춘다.</summary>
    public float ShadowPlaneY { get; set; }
    public float ShadowOpacity { get; set; } = DefaultShadowOpacity;

    /// <summary>모든 값이 기본이라 씬 파일에 쓸 필요가 없는가.</summary>
    public bool IsDefault =>
        Ambient == DefaultAmbient && SunDirection == DefaultSunDirection && SunColor == DefaultSunColor &&
        PointLights.Count == 0 && !ShadowEnabled && ShadowPlaneY == 0 && ShadowOpacity == DefaultShadowOpacity;
}
