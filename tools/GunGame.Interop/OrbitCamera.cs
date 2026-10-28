namespace GunGame.Interop;

public readonly record struct Vec3(float X, float Y, float Z);

/// <summary>대상 주위를 도는 뷰어용 카메라. 오른손 좌표계, Y-up.</summary>
public sealed class OrbitCamera
{
    public const float MaxPitch = 1.5f;
    public const float MinDistance = 0.5f;
    public const float MaxDistance = 100f;

    public Vec3 Target { get; set; } = new(0, 1, 0);
    public float Yaw { get; private set; } = 0.6f;
    public float Pitch { get; private set; } = 0.4f;
    public float Distance { get; private set; } = 10f;

    public Vec3 Eye
    {
        get
        {
            float cp = MathF.Cos(Pitch);
            return new Vec3(
                Target.X + Distance * cp * MathF.Sin(Yaw),
                Target.Y + Distance * MathF.Sin(Pitch),
                Target.Z + Distance * cp * MathF.Cos(Yaw));
        }
    }

    public void Rotate(float deltaYaw, float deltaPitch)
    {
        Yaw += deltaYaw;
        Pitch = Math.Clamp(Pitch + deltaPitch, -MaxPitch, MaxPitch);
    }

    /// <summary>factor &gt; 1이면 멀어진다.</summary>
    public void Zoom(float factor) => Distance = Math.Clamp(Distance * factor, MinDistance, MaxDistance);
}
