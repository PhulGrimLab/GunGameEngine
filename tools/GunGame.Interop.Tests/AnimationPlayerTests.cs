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

    // ---- 한 번 재생(loop=false) ----
    [Fact]
    public void LoopIsTheDefault_AndLoopingClipsNeverFinish()
    {
        var player = new AnimationPlayer();
        Assert.True(player.Loop);                      // 레스트 포즈일 때도 true
        Assert.False(player.Finished);
        player.Play(Walk);
        Assert.True(player.Loop);
        player.Update(5f);
        Assert.False(player.Finished);
        Assert.Equal(1.0f, player.Time, 4);            // 5 mod 2
    }

    [Fact]
    public void OneShot_StopsAtTheEnd_KeepsTheLastPose_AndReportsFinished()
    {
        var player = new AnimationPlayer();
        player.Play(Walk, loop: false);
        Assert.False(player.Loop);
        player.Update(1.5f);
        Assert.Equal(1.5f, player.Time, 4);
        Assert.False(player.Finished);
        player.Update(1.0f);                           // 2.5는 끝(2.0)에서 멈춘다
        Assert.Equal(2.0f, player.Time, 4);
        Assert.True(player.Finished);
        Assert.Equal(new PoseRequest(1, 2.0f, 0, 0, 0), player.Pose);   // 마지막 포즈를 유지한다
        player.Update(10f);
        Assert.Equal(2.0f, player.Time, 4);
        Assert.True(player.Playing);                   // 끝남은 일시정지 스위치와 별개다
    }

    [Fact]
    public void PlayingTheFinishedClipAgain_IsIgnoredUnlessRestart()
    {
        var player = new AnimationPlayer();
        player.Play(Walk, loop: false);
        player.Update(3f);
        Assert.True(player.Finished);

        player.Play(Walk, loop: false);                // 매 프레임 PlayClip을 부르는 패턴: 끝난 클립이 되감기지 않는다
        player.Play(Walk);
        Assert.True(player.Finished);
        Assert.Equal(2.0f, player.Time, 4);

        player.Play(Walk, restart: true, loop: false); // 다시 재생하려면 restart
        Assert.False(player.Finished);
        Assert.Equal(0f, player.Time);
        player.Update(1f);
        Assert.Equal(1f, player.Time, 4);

        player.Play(Walk, restart: true);              // restart는 loop도 새로 적용한다(기본 반복)
        Assert.True(player.Loop);
        player.Update(2.5f);
        Assert.Equal(0.5f, player.Time, 4);
    }

    [Fact]
    public void SameClipWithADifferentLoopFlag_IsIgnoredUnlessRestart()
    {
        var player = new AnimationPlayer();
        player.Play(Walk);                             // 반복 중
        player.Play(Walk, loop: false);                // loop만 바꿔 부르면 조용히 무시된다
        Assert.True(player.Loop);
        player.Update(2.5f);
        Assert.Equal(0.5f, player.Time, 4);
        Assert.False(player.Finished);

        player.Play(Walk, restart: true, loop: false); // 바꾸려면 restart
        Assert.False(player.Loop);
        player.Update(5f);
        Assert.True(player.Finished);
    }

    [Fact]
    public void Finished_FollowsTheDirectionOfPlayback()
    {
        var player = new AnimationPlayer();
        player.Play(Walk, loop: false);
        player.Update(5f);
        Assert.True(player.Finished);                  // 끝 시간

        player.Speed = -1;                             // 끝에서 속도 부호를 바꾸면 거꾸로 재생된다: 더는 끝난 상태가 아니다
        Assert.False(player.Finished);
        player.Update(0.5f);
        Assert.Equal(1.5f, player.Time, 4);
        Assert.False(player.Finished);
        player.Update(5f);                             // 0초에서 멈춘다: 재생 방향(역방향)의 끝
        Assert.Equal(0f, player.Time);
        Assert.True(player.Finished);

        player.Speed = 0;                              // 속도 0은 정방향 취급: 0초는 끝이 아니다
        Assert.False(player.Finished);
        player.Speed = 1;
        player.Update(5f);
        player.Speed = 0;
        Assert.True(player.Finished);                  // 끝 시간에 있으면 속도 0이어도 끝난 상태
    }

    [Fact]
    public void NegativeSpeedOneShot_StartsAtItsEnd_SoItIsFinishedAtOnce()
    {
        var player = new AnimationPlayer { Speed = -1 };
        player.Play(Walk, loop: false);                // 0초에서 시작해 거꾸로: 이미 재생 방향의 끝이다(문서화된 모서리)
        Assert.True(player.Finished);
        player.Update(1f);
        Assert.Equal(0f, player.Time);
    }

    [Fact]
    public void ZeroDurationOneShot_IsFinishedImmediately()
    {
        var player = new AnimationPlayer();
        player.Play(new ClipRef(9, 0f), loop: false);
        Assert.True(player.Finished);
        player.Update(1f);
        Assert.Equal(0f, player.Time);
        Assert.Equal(new PoseRequest(9, 0f, 0, 0, 0), player.Pose);
    }

    [Fact]
    public void CrossfadeFromAFinishedOneShot_HoldsItsEndPoseDuringTheFade()
    {
        var player = new AnimationPlayer();
        player.Play(Walk, loop: false);
        player.Update(3f);                             // 끝(2.0)에서 멈춤
        player.Play(Idle, fadeSeconds: 1f);
        Assert.Equal(new PoseRequest(1, 2.0f, 2, 0f, 0f), player.Pose);
        player.Update(0.5f);
        Assert.Equal(new PoseRequest(1, 2.0f, 2, 0.5f, 0.5f), player.Pose);   // 이전 클립은 끝 포즈에 머물고 새 클립은 진행
        player.Update(0.5f);
        Assert.False(player.IsFading);
        Assert.Equal(new PoseRequest(2, 1.0f, 0, 0, 0), player.Pose);
    }

    [Fact]
    public void CrossfadeFromAnOneShotThatIsStillRunning_ContinuesItUntilItsEnd()
    {
        var player = new AnimationPlayer();
        player.Play(Walk, loop: false);
        player.Update(1.5f);
        player.Play(Idle, fadeSeconds: 2f, loop: false);
        player.Update(1.0f);                           // 이전 클립은 1.5 + 1.0 = 2.5 -> 끝 2.0에서 멈춘다(감기지 않는다)
        PoseRequest pose = player.Pose;
        Assert.Equal(2.0f, pose.TimeA, 4);
        Assert.Equal(1.0f, pose.TimeB, 4);
        Assert.Equal(0.5f, pose.Blend, 4);
    }

    [Fact]
    public void RestartTheSameClip_CutsAnyFadeAndRestartsAtZero()
    {
        var player = new AnimationPlayer();
        player.Play(Walk);
        player.Update(0.5f);
        player.Play(Idle, fadeSeconds: 1f);
        player.Update(0.25f);
        Assert.True(player.IsFading);
        player.Play(Idle, restart: true, fadeSeconds: 1f);   // 같은 클립 restart는 페이드 없이 즉시
        Assert.False(player.IsFading);
        Assert.Equal(0f, player.Time);
        Assert.Equal(new PoseRequest(2, 0f, 0, 0, 0), player.Pose);
    }

    [Fact]
    public void SeekAndClear_RespectTheLoopFlag()
    {
        var player = new AnimationPlayer();
        player.Play(Walk, loop: false);
        player.Seek(5f);
        Assert.Equal(2.0f, player.Time, 4);            // 한 번 재생은 끝으로 고정된다(감지 않는다)
        Assert.True(player.Finished);
        player.Seek(-1f);
        Assert.Equal(0f, player.Time);
        Assert.False(player.Finished);

        player.Update(5f);
        player.Clear();                                // 레스트로 돌아가면 loop도 기본값
        Assert.True(player.Loop);
        Assert.False(player.Finished);
        Assert.Equal(new PoseRequest(0, 0, 0, 0, 0), player.Pose);
    }

    [Fact]
    public void Pause_FreezesAFadeToo_AndOneShotProgress()
    {
        var player = new AnimationPlayer();
        player.Play(Walk, loop: false);
        player.Update(1f);
        player.Play(Idle, fadeSeconds: 1f);
        player.Update(0.25f);
        PoseRequest before = player.Pose;
        player.Playing = false;
        player.Update(10f);
        Assert.Equal(before, player.Pose);             // 시간과 페이드 진행이 모두 멈춘다
        player.Playing = true;
        player.Update(0.25f);
        Assert.NotEqual(before, player.Pose);
    }
}
