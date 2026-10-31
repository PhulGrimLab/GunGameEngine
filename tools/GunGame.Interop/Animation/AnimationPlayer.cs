namespace GunGame.Interop;

public readonly record struct ClipRef(int Id, float Duration);

/// <summary>엔진 gge_object_set_pose에 그대로 넘길 값. ClipA 0 = 레스트 포즈, ClipB 0 = 블렌딩 없음.</summary>
public readonly record struct PoseRequest(int ClipA, float TimeA, int ClipB, float TimeB, float Blend);

/// <summary>
/// 클립 재생 상태(시간, 반복, 속도, 크로스페이드)를 관리한다. 엔진은 시간을 구동하지 않으므로
/// 호출자가 Update(dt)로 진행시키고 Pose를 엔진에 전달한다. 엔진/UI에 의존하지 않는 순수 로직이다.
/// </summary>
public sealed class AnimationPlayer
{
    private ClipRef? _current;
    private bool _currentLoop = true;
    private float _currentTime;
    private ClipRef? _from;
    private bool _fromLoop = true;
    private float _fromTime;
    private float _fadeElapsed;
    private float _fadeDuration;

    public bool Playing { get; set; } = true;

    /// <summary>재생 속도 배율(음수면 거꾸로). 반복(loop)이면 시간이 감기고, 한 번 재생이면 끝 시간에서 멈춘다.</summary>
    public float Speed { get; set; } = 1f;

    public ClipRef? Current => _current;

    public float Time => _currentTime;

    public bool IsFading => _from is not null;

    /// <summary>지금 클립이 반복 재생인가(기본 true). 레스트 포즈일 때는 true.</summary>
    public bool Loop => _currentLoop;

    /// <summary>
    /// 한 번 재생(loop=false)이 재생 방향의 끝에 닿아 마지막 포즈로 멈춰 있다: 속도가 0 이상이면 끝 시간(Duration), 음수면 0초.
    /// 반복 재생과 레스트 포즈는 항상 false다. <see cref="Playing"/>(일시정지 스위치)과는 별개라 끝난 뒤에도 Playing은 true다.
    /// 길이 0인 클립은 한 번 재생하면 바로 끝난 상태다.
    /// </summary>
    public bool Finished => !_currentLoop && _current is { } c && (Speed >= 0 ? _currentTime >= c.Duration : _currentTime <= 0);

    /// <summary>
    /// 클립을 처음부터 재생한다. 이미 재생 중인 클립이면(loop 값이 달라도) 무시하고, fadeSeconds &gt; 0이면 이전 클립에서 크로스페이드한다
    /// (이전 클립은 페이드 동안 계속 진행하고, 한 번 재생이었다면 끝 포즈에 머문다). 레스트 포즈에서 시작할 때는 페이드 없이 즉시 바뀐다.
    /// restart가 true면 같은 클립도 0초부터 다시 시작한다(페이드 없이 즉시, loop 값은 새로 적용).
    /// </summary>
    public void Play(ClipRef clip, float fadeSeconds = 0, bool restart = false, bool loop = true)
    {
        if (!float.IsFinite(fadeSeconds) || fadeSeconds < 0)
            throw new ArgumentOutOfRangeException(nameof(fadeSeconds));
        if (_current == clip && !restart)
            return;

        if (_current is not null && _current != clip && fadeSeconds > 0)
        {
            _from = _current;
            _fromLoop = _currentLoop;
            _fromTime = _currentTime;
            _fadeElapsed = 0;
            _fadeDuration = fadeSeconds;
        }
        else
        {
            _from = null;
        }
        _current = clip;
        _currentLoop = loop;
        _currentTime = 0;
    }

    /// <summary>재생을 멈추고 레스트 포즈로 돌아간다.</summary>
    public void Clear()
    {
        _current = null;
        _currentLoop = true;
        _from = null;
        _currentTime = 0;
    }

    public void Seek(float time)
    {
        if (!float.IsFinite(time))
            throw new ArgumentOutOfRangeException(nameof(time));
        if (_current is { } c)
            _currentTime = Advance(0, time, c.Duration, _currentLoop);
    }

    public void Update(float dt)
    {
        if (!float.IsFinite(dt) || dt < 0)
            throw new ArgumentOutOfRangeException(nameof(dt));
        if (!Playing || _current is not { } current)
            return;

        float delta = dt * Speed;
        if (!float.IsFinite(delta))
            return;  // Speed가 NaN/무한대이거나 곱이 float 범위를 넘었다: 이 프레임의 진행은 무시한다(시간이 NaN으로 굳으면 복구되지 않는다)
        _currentTime = Advance(_currentTime, delta, current.Duration, _currentLoop);
        if (_from is { } from)
        {
            _fromTime = Advance(_fromTime, delta, from.Duration, _fromLoop);
            _fadeElapsed += dt;
            if (_fadeElapsed >= _fadeDuration)
                _from = null;
        }
    }

    public PoseRequest Pose
    {
        get
        {
            if (_current is not { } current)
                return new PoseRequest(0, 0, 0, 0, 0);
            if (_from is { } from)
                return new PoseRequest(from.Id, _fromTime, current.Id, _currentTime, Math.Clamp(_fadeElapsed / _fadeDuration, 0f, 1f));
            return new PoseRequest(current.Id, _currentTime, 0, 0, 0);
        }
    }

    // 시간을 delta만큼 진행한다: 반복이면 [0, duration)으로 감고, 한 번 재생이면 [0, duration]에서 멈춘다(길이 0이면 0).
    private static float Advance(float time, float delta, float duration, bool loop) =>
        loop ? Wrap(time + delta, duration) : duration <= 0 ? 0 : Math.Clamp(time + delta, 0f, duration);

    private static float Wrap(float time, float duration)
    {
        if (duration <= 0)
            return 0;
        float wrapped = time - duration * MathF.Floor(time / duration);
        return wrapped >= duration ? 0 : wrapped;  // 부동소수점 오차로 duration과 같아지는 경우
    }
}
