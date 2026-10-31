using GunGame.Interop;
using GunGame.Scripting;

namespace GunGame.ScriptHost;

/// <summary>스크립트에게 주는 <see cref="IScriptContext"/>: 엔진 씬/렌더러 래퍼에 그대로 위임한다.</summary>
internal sealed class ScriptContext(EngineRenderer renderer, EngineScene scene, TextWriter log) : IScriptContext
{
    public string ActiveRoomName => scene.ActiveRoomName;

    public (float X, float Z)? PlayerPosition => scene.Player is { } p ? (p.Position.X, p.Position.Z) : null;

    public void SetTriggerEnabled(string triggerName, bool enabled) => scene.SetTriggerEnabled(triggerName, enabled);

    public void SetColliderEnabled(string colliderName, bool enabled) => scene.SetColliderEnabled(colliderName, enabled);

    public void TeleportPlayer(float x, float z) => scene.TeleportPlayer(x, z);

    public bool IsPlayerInside(string triggerName) => scene.IsInsideTrigger(triggerName);

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

    public void Log(string message) => log.WriteLine("[script] " + message);
}
