using GunGame.Interop;
using GunGame.Scripting;

namespace GunGame.ScriptHost;

/// <summary>스크립트에게 주는 <see cref="IScriptContext"/>: 엔진 씬/렌더러 래퍼에 그대로 위임한다.</summary>
internal sealed class ScriptContext(EngineRenderer renderer, EngineScene scene, TextWriter log) : IScriptContext
{
    public string ActiveRoomName => scene.ActiveRoomName;

    public (float X, float Z)? PlayerPosition => scene.Player is { } p ? (p.Position.X, p.Position.Z) : null;

    public void SetTriggerEnabled(string triggerName, bool enabled) => scene.SetTriggerEnabled(triggerName, enabled);

    public void TeleportPlayer(float x, float z) => scene.TeleportPlayer(x, z);

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
