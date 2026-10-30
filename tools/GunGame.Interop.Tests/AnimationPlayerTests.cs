using GunGame.Interop;

namespace GunGame.Interop.Tests;

public class AnimationPlayerTests
{
    private static readonly ClipRef Walk = new(1, 2.0f);
    private static readonly ClipRef Idle = new(2, 4.0f);

    [Fact]
    public void NoClip_IsRestPose()
    {
        var player = new AnimationPlayer();
        player.Update(1);
        Assert.Equal(new PoseRequest(0, 0, 0, 0, 0), player.Pose);
    }

    [Fact]
    public void Update_AdvancesAndLoops()
    {
        var player = new AnimationPlayer();
        player.Play(Walk);
        player.Update(0.5f);
        Assert.Equal(new PoseRequest(1, 0.5f, 0, 0, 0), player.Pose);
        player.Update(1.75f);  // 2.25 → 0.25로 반복
        Assert.Equal(0.25f, player.Time, 4);
    }

    [Fact]
    public void Speed_ScalesTimeAndNegativeSpeedRunsBackwards()
    {
        var player = new AnimationPlayer { Speed = 2 };
        player.Play(Walk);
        player.Update(0.5f);
        Assert.Equal(1.0f, player.Time, 4);

        player.Speed = -1;
        player.Update(0.25f);
        Assert.Equal(0.75f, player.Time, 4);
        player.Update(1.0f);  // 0.75 - 1.0 = -0.25 → 1.75
        Assert.Equal(1.75f, player.Time, 4);
    }

    [Fact]
    public void Pause_FreezesTimeButPlayRestartsFromZero()
    {
        var player = new AnimationPlayer();
        player.Play(Walk);
        player.Update(0.7f);
        player.Playing = false;
        player.Update(5);
        Assert.Equal(0.7f, player.Time, 4);

        player.Play(Walk);  // 이미 재생 중인 클립은 무시
        Assert.Equal(0.7f, player.Time, 4);
        player.Play(Idle);
        Assert.Equal(0f, player.Time);
    }

    [Fact]
    public void Crossfade_BlendsFromPreviousClipThenCompletes()
    {
        var player = new AnimationPlayer();
        player.Play(Walk);
        player.Update(0.5f);
        player.Play(Idle, fadeSeconds: 0.4f);
        Assert.True(player.IsFading);
        Assert.Equal(new PoseRequest(1, 0.5f, 2, 0f, 0f), player.Pose);

        player.Update(0.1f);
        PoseRequest mid = player.Pose;
        Assert.Equal(1, mid.ClipA);
        Assert.Equal(2, mid.ClipB);
        Assert.Equal(0.6f, mid.TimeA, 4);   // 이전 클립도 계속 진행한다
        Assert.Equal(0.1f, mid.TimeB, 4);
        Assert.Equal(0.25f, mid.Blend, 4);

        player.Update(0.3f);  // 페이드 끝
        Assert.False(player.IsFading);
        Assert.Equal(new PoseRequest(2, 0.4f, 0, 0, 0), player.Pose);
    }

    [Fact]
    public void Crossfade_OnlyWhenPreviousClipExists_AndZeroFadeCuts()
    {
        var first = new AnimationPlayer();
        first.Play(Walk, 1f);
        Assert.False(first.IsFading);  // 이전 클립이 없으면 페이드하지 않는다

        var cut = new AnimationPlayer();
        cut.Play(Walk);
        cut.Update(1);
        cut.Play(Idle, 0f);
        Assert.False(cut.IsFading);
        Assert.Equal(new PoseRequest(2, 0f, 0, 0, 0), cut.Pose);
    }

    [Fact]
    public void Seek_WrapsAndZeroDurationClipStaysAtZero()
    {
        var player = new AnimationPlayer();
        player.Play(Walk);
        player.Seek(5.5f);
        Assert.Equal(1.5f, player.Time, 4);

        player.Play(new ClipRef(9, 0));
        player.Update(3);
        Assert.Equal(0f, player.Time);
    }

    [Fact]
    public void Clear_ReturnsToRestPose()
    {
        var player = new AnimationPlayer();
        player.Play(Walk);
        player.Update(1);
        player.Clear();
        Assert.Equal(new PoseRequest(0, 0, 0, 0, 0), player.Pose);
        Assert.Null(player.Current);
    }

    [Fact]
    public void InvalidArguments_Throw()
    {
        var player = new AnimationPlayer();
        Assert.Throws<ArgumentOutOfRangeException>(() => player.Update(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => player.Update(float.NaN));
        Assert.Throws<ArgumentOutOfRangeException>(() => player.Play(Walk, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => player.Seek(float.PositiveInfinity));
    }

    [Theory]
    [InlineData(float.NaN, 0.1f)]
    [InlineData(float.PositiveInfinity, 0.1f)]
    [InlineData(3e38f, 3f)]   // 속도와 dt는 모두 유한하지만 곱이 float 범위를 넘어 무한대가 된다
    public void NonFiniteStep_DoesNotPoisonThePlaybackTime(float speed, float dt)
    {
        var player = new AnimationPlayer { Speed = 1f };
        player.Play(Walk);
        player.Update(0.5f);
        float before = player.Time;

        player.Speed = speed;
        player.Update(dt);  // 이 프레임의 진행은 무시된다
        Assert.True(float.IsFinite(player.Time));
        Assert.Equal(before, player.Time);

        player.Speed = 1f;  // 정상 속도로 돌아오면 계속 재생된다(시간이 NaN으로 굳지 않는다)
        player.Update(0.25f);
        Assert.Equal(before + 0.25f, player.Time, 4);
        Assert.True(float.IsFinite(player.Pose.TimeA));
    }

    [Fact]
    public void NonFiniteStep_AlsoLeavesTheFadeSourceFinite()
    {
        var player = new AnimationPlayer();
        player.Play(Walk);
        player.Update(0.5f);
        player.Play(Idle, fadeSeconds: 1f);
        player.Speed = float.NaN;
        player.Update(0.1f);
        PoseRequest pose = player.Pose;
        Assert.True(float.IsFinite(pose.TimeA) && float.IsFinite(pose.TimeB) && float.IsFinite(pose.Blend));
    }
}
