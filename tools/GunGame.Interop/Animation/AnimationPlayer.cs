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
    private float _currentTime;
    private ClipRef? _from;
    private float _fromTime;
    private float _fadeElapsed;
    private float _fadeDuration;

    public bool Playing { get; set; } = true;

    /// <summary>재생 속도 배율(음수면 거꾸로). 클립은 항상 반복된다.</summary>
    public float Speed { get; set; } = 1f;

    public ClipRef? Current => _current;

    public float Time => _currentTime;

    public bool IsFading => _from is not null;

    /// <summary>
    /// 클립을 처음부터 재생한다. 이미 재생 중인 클립이면 무시하고, fadeSeconds &gt; 0이면 이전 클립에서 크로스페이드한다.
    /// </summary>
    public void Play(ClipRef clip, float fadeSeconds = 0)
    {
        if (!float.IsFinite(fadeSeconds) || fadeSeconds < 0)
            throw new ArgumentOutOfRangeException(nameof(fadeSeconds));
        if (_current == clip)
            return;

        if (_current is not null && fadeSeconds > 0)
        {
            _from = _current;
            _fromTime = _currentTime;
            _fadeElapsed = 0;
            _fadeDuration = fadeSeconds;
        }
        else
        {
            _from = null;
        }
        _current = clip;
        _currentTime = 0;
    }

    /// <summary>재생을 멈추고 레스트 포즈로 돌아간다.</summary>
    public void Clear()
    {
        _current = null;
        _from = null;
        _currentTime = 0;
    }

    public void Seek(float time)
    {
        if (!float.IsFinite(time))
            throw new ArgumentOutOfRangeException(nameof(time));
        if (_current is { } c)
            _currentTime = Wrap(time, c.Duration);
    }

    public void Update(float dt)
    {
        if (!float.IsFinite(dt) || dt < 0)
            throw new ArgumentOutOfRangeException(nameof(dt));
        if (!Playing || _current is not { } current)
            return;

        float delta = dt * Speed;
        _currentTime = Wrap(_currentTime + delta, current.Duration);
        if (_from is { } from)
        {
            _fromTime = Wrap(_fromTime + delta, from.Duration);
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

    private static float Wrap(float time, float duration)
    {
        if (duration <= 0)
            return 0;
        float wrapped = time - duration * MathF.Floor(time / duration);
        return wrapped >= duration ? 0 : wrapped;  // 부동소수점 오차로 duration과 같아지는 경우
    }
}
