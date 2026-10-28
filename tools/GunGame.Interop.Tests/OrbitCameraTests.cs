using GunGame.Interop;

namespace GunGame.Interop.Tests;

public class OrbitCameraTests
{
    private static float Length(Vec3 a, Vec3 b) =>
        MathF.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));

    [Fact]
    public void Eye_IsAtDistanceFromTarget()
    {
        var cam = new OrbitCamera { Target = new Vec3(1, 2, 3) };
        cam.Rotate(1.1f, 0.3f);
        Assert.Equal(cam.Distance, Length(cam.Eye, cam.Target), 3);
    }

    [Fact]
    public void Pitch_IsClamped()
    {
        var cam = new OrbitCamera();
        cam.Rotate(0, 10);
        Assert.Equal(OrbitCamera.MaxPitch, cam.Pitch);
        cam.Rotate(0, -20);
        Assert.Equal(-OrbitCamera.MaxPitch, cam.Pitch);
    }

    [Fact]
    public void Zoom_IsClamped()
    {
        var cam = new OrbitCamera();
        cam.Zoom(1000);
        Assert.Equal(OrbitCamera.MaxDistance, cam.Distance);
        cam.Zoom(0.0001f);
        Assert.Equal(OrbitCamera.MinDistance, cam.Distance);
    }

    [Fact]
    public void YawRotation_MovesEyeAroundYAxis()
    {
        var cam = new OrbitCamera();
        cam.Rotate(-cam.Yaw, -cam.Pitch);  // yaw=0, pitch=0 → +Z 위
        Assert.Equal(cam.Distance, cam.Eye.Z, 3);
        Assert.Equal(0f, cam.Eye.X, 3);
        cam.Rotate(MathF.PI / 2, 0);        // +X 위
        Assert.Equal(cam.Distance, cam.Eye.X, 3);
        Assert.Equal(0f, cam.Eye.Z, 3);
    }
}
