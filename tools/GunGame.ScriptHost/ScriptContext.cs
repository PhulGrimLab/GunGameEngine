using GunGame.Interop;
using GunGame.Scripting;

namespace GunGame.ScriptHost;

/// <summary>스크립트에게 주는 <see cref="IScriptContext"/>: 엔진 씬/렌더러 래퍼에 그대로 위임한다. 구역은 인터페이스(IWorldApi/IObjectApi/IAnimationApi)와 같다.</summary>
internal sealed class ScriptContext(EngineRenderer renderer, EngineScene scene, EngineFont? font, TextWriter log) : IScriptContext
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

    // ---- ISpawnApi ----
    // 없는 템플릿/인스턴스는 false(다른 쓰기 API와 같은 규칙), 이름 충돌/한도/씬 노드 삭제 같은 실제 오류는 엔진의 사유가 담긴 예외다.
    public bool SpawnObject(string templateName, string instanceName, float x, float y, float z)
    {
        if (!NodeExists(templateName))
            return false;
        scene.SpawnObject(templateName, instanceName, new Vec3(x, y, z));
        return true;
    }

    public bool DestroyObject(string instanceName)
    {
        if (!NodeExists(instanceName))
            return false;
        scene.DestroyObject(instanceName);
        return true;
    }

    public bool ObjectExists(string nodeName) => scene.ObjectExists(nodeName);

    // ---- IUiApi ----
    // 스크립트가 붙인 이름 -> 엔진 항목 ID. 같은 이름으로 다시 Show하면 그 항목을 제자리에서 고치고(그리는 순서 유지), Hide는 항목을 지우며 이름도 비운다(숨김 상태는 없다).
    private enum UiKind { Text, Rect }

    private readonly Dictionary<string, (int Id, UiKind Kind)> _ui = new(StringComparer.Ordinal);

    private EngineFont Font => font ?? throw new InvalidOperationException("no font: start the player with --font file.bdf");

    private static void RequireUiName(string name)
    {
        if (string.IsNullOrEmpty(name))
            throw new ArgumentException("the ui item name must not be empty", nameof(name));
    }

    public void ShowText(string name, string text, int x, int y, uint rgb = 0xFFFFFF, int maxWidth = 0)
    {
        RequireUiName(name);
        if (_ui.TryGetValue(name, out var item))
        {
            if (item.Kind != UiKind.Text)
                throw new InvalidOperationException($"ui item '{name}' is a rectangle (use ShowRect, or HideRect first)");
            renderer.SetUiText(item.Id, text);   // 실패할 수 있는 것(글자 한도)을 먼저 해서, 실패해도 항목은 그대로다
            renderer.SetUiMaxWidth(item.Id, maxWidth);
            renderer.SetUiPosition(item.Id, x, y);
            renderer.SetUiColor(item.Id, rgb);
            return;
        }
        _ui[name] = (renderer.CreateUiText(Font, text, x, y, maxWidth, rgb), UiKind.Text);
    }

    public void ShowRect(string name, int x, int y, int width, int height, uint rgb, int alpha = 255)
    {
        RequireUiName(name);
        if (_ui.TryGetValue(name, out var item))
        {
            if (item.Kind != UiKind.Rect)
                throw new InvalidOperationException($"ui item '{name}' is text (use ShowText, or HideText first)");
            renderer.SetUiRect(item.Id, width, height, alpha);   // 검사가 있는 것을 먼저
            renderer.SetUiPosition(item.Id, x, y);
            renderer.SetUiColor(item.Id, rgb);
            return;
        }
        _ui[name] = (renderer.CreateUiRect(x, y, width, height, rgb, alpha), UiKind.Rect);
    }

    public bool HideText(string name) => Hide(name, UiKind.Text);

    public bool HideRect(string name) => Hide(name, UiKind.Rect);

    private bool Hide(string name, UiKind kind)
    {
        if (string.IsNullOrEmpty(name) || !_ui.TryGetValue(name, out var item) || item.Kind != kind)
            return false;
        renderer.DestroyUi(item.Id);
        _ui.Remove(name);
        return true;
    }

    public void ClearUi()
    {
        renderer.ClearUi();
        _ui.Clear();
    }

    public TextSize MeasureText(string text, int maxWidth = 0)
    {
        (int width, int height, int lines) = Font.Measure(text, maxWidth);
        return new TextSize(width, height, lines);
    }

    // ---- 로그 ----
    public void Log(string message) => log.WriteLine("[script] " + message);
}
