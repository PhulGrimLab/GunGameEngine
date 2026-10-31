using GunGame.Interop;
using GunGame.Scripting;

namespace GunGame.ScriptHost;

/// <summary>스크립트에게 주는 <see cref="IScriptContext"/>: 엔진 씬/렌더러 래퍼에 그대로 위임한다. 구역은 인터페이스(IWorldApi/IObjectApi/IAnimationApi)와 같다.</summary>
internal sealed class ScriptContext(EngineRenderer renderer, EngineScene scene, TextWriter log) : IScriptContext
{
    // ---- IWorldApi ----
    public string ActiveRoomName => scene.ActiveRoomName;

    public (float X, float Z)? PlayerPosition => scene.Player is { } p ? (p.Position.X, p.Position.Z) : null;

    public float PlayerSpeed => scene.PlayerSpeed;

    public void SetTriggerEnabled(string triggerName, bool enabled) => scene.SetTriggerEnabled(triggerName, enabled);

    public void SetColliderEnabled(string colliderName, bool enabled) => scene.SetColliderEnabled(colliderName, enabled);

    public void TeleportPlayer(float x, float z) => scene.TeleportPlayer(x, z);

    public bool IsPlayerInside(string triggerName) => scene.IsInsideTrigger(triggerName);

    // ---- IObjectApi ----
    public (float X, float Y, float Z)? GetObjectPosition(string nodeName) =>
        scene.GetNodePosition(nodeName) is { } p ? (p.X, p.Y, p.Z) : null;

    public bool SetObjectPosition(string nodeName, float x, float y, float z)
    {
        if (scene.GetNodePosition(nodeName) is null)
            return false;
        scene.SetNodePosition(nodeName, new Vec3(x, y, z));
        return true;
    }

    public bool SetObjectVisible(string nodeName, bool visible)
    {
        if (scene.IsNodeVisible(nodeName) is null)
            return false;
        scene.SetNodeVisible(nodeName, visible);
        return true;
    }

    public bool? IsObjectVisible(string nodeName) => scene.IsNodeVisible(nodeName);

    public (float X, float Y, float Z)? GetObjectRotation(string nodeName) =>
        scene.GetNodeRotation(nodeName) is { } r ? (r.X, r.Y, r.Z) : null;

    public bool SetObjectRotation(string nodeName, float x, float y, float z)
    {
        if (scene.GetNodeRotation(nodeName) is null)
            return false;
        scene.SetNodeRotation(nodeName, new Vec3(x, y, z));
        return true;
    }

    public (float X, float Y, float Z)? GetObjectScale(string nodeName) =>
        scene.GetNodeScale(nodeName) is { } s ? (s.X, s.Y, s.Z) : null;

    public bool SetObjectScale(string nodeName, float x, float y, float z)
    {
        if (scene.GetNodeScale(nodeName) is null)
            return false;
        scene.SetNodeScale(nodeName, new Vec3(x, y, z));
        return true;
    }

    public bool SetObjectAlpha(string nodeName, float alpha)
    {
        int id = scene.FindObject(nodeName);
        if (id == 0)
            return false;
        renderer.SetObjectAlpha(id, alpha);
        return true;
    }

    // ---- IAnimationApi ----
    // 없는 노드는 false(다른 쓰기 API와 같은 규칙), 스킨 메시가 아니거나 없는 클립/잘못된 값은 엔진의 사유가 담긴 예외다.
    public bool PlayClip(string nodeName, string clipName, float fadeSeconds = 0, bool restart = false, bool loop = true)
    {
        if (!NodeExists(nodeName))
            return false;
        scene.PlayClip(nodeName, clipName, fadeSeconds, restart, loop);
        return true;
    }

    public bool StopClip(string nodeName)
    {
        if (!NodeExists(nodeName))
            return false;
        scene.StopClip(nodeName);
        return true;
    }

    public bool SetClipSpeed(string nodeName, float speed)
    {
        if (!NodeExists(nodeName))
            return false;
        scene.SetClipSpeed(nodeName, speed);
        return true;
    }

    public bool SetClipPlaying(string nodeName, bool playing)
    {
        if (!NodeExists(nodeName))
            return false;
        scene.SetClipPlaying(nodeName, playing);
        return true;
    }

    public ClipState? GetClip(string nodeName) =>
        scene.TryGetNodeAnimation(nodeName) is { } a ? new ClipState(a.Clip, a.Time, a.Playing, a.Speed, a.Fading, a.Loop, a.Finished) : null;

    private bool NodeExists(string nodeName) => scene.IsNodeVisible(nodeName) is not null;

    // ---- 로그 ----
    public void Log(string message) => log.WriteLine("[script] " + message);
}
