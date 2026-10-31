using System.Reflection;
using System.Runtime.Loader;
using GunGame.Interop;
using GunGame.Scripting;
using TriggerEvent = GunGame.Scripting.TriggerEvent;

namespace GunGame.ScriptHost;

/// <summary>스크립트를 불러오지 못했거나 스크립트가 실행 중 예외를 던졌다. 메시지에 스크립트 이름과 호출 지점이 들어 있다.</summary>
public sealed class ScriptException(string message, Exception? inner = null) : Exception(message, inner);

/// <summary>
/// 게임 스크립트(<see cref="IGameScript"/>)를 불러 순서대로 호출한다. 네이티브 호스팅(<see cref="Entry"/>)과 분리해 두어
/// 같은 코드를 xUnit에서 실제 엔진 씬 위에 바로 돌려 볼 수 있다. 스레드 안전하지 않다(게임 스레드 하나에서만 부른다).
/// </summary>
public sealed class ScriptRunner
{
    private readonly (string Name, IGameScript Script)[] _scripts;
    private readonly IScriptContext _context;
    private bool _started;

    public ScriptRunner(IScriptContext context, IEnumerable<IGameScript> scripts)
    {
        _context = context;
        _scripts = [.. scripts.Select(s => (s.GetType().FullName ?? s.GetType().Name, s))];
    }

    public int ScriptCount => _scripts.Length;

    /// <summary>어셈블리에서 스크립트를 찾아 엔진 씬에 연결한다. 실패하면 <see cref="ScriptException"/>.</summary>
    public static ScriptRunner Create(EngineRenderer renderer, EngineScene scene, string assemblyPath, string? typeFilter, TextWriter log) =>
        Create(renderer, scene, LoadScripts(assemblyPath, typeFilter), log);

    /// <summary>이미 만든 스크립트 객체들을 엔진 씬에 연결한다(어셈블리를 거치지 않는 경우, 예: 테스트).</summary>
    public static ScriptRunner Create(EngineRenderer renderer, EngineScene scene, IEnumerable<IGameScript> scripts, TextWriter log) =>
        new(new ScriptContext(renderer, scene, log), scripts);

    /// <summary>
    /// 어셈블리의 공개 <see cref="IGameScript"/> 구현을 모두 만든다(타입 이름 순서). typeFilter가 있으면 전체 이름 또는 짧은 이름이 같은
    /// 타입만 만든다. 파일이 없거나, 구현이 없거나, 필터에 맞는 타입이 없거나, 생성자가 없거나 던지면 <see cref="ScriptException"/>.
    /// </summary>
    public static IReadOnlyList<IGameScript> LoadScripts(string assemblyPath, string? typeFilter)
    {
        string full = Path.GetFullPath(assemblyPath);
        if (!File.Exists(full))
            throw new ScriptException($"script assembly not found: {full}");

        Assembly assembly;
        try
        {
            // 스크립트가 참조하는 GunGame.Scripting이 호스트와 같은 것이 되도록 같은 로드 컨텍스트에 올린다.
            AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(typeof(IGameScript).Assembly) ?? AssemblyLoadContext.Default;
            assembly = context.LoadFromAssemblyPath(full);
        }
        catch (Exception e) when (e is BadImageFormatException or FileLoadException or FileNotFoundException)
        {
            throw new ScriptException($"cannot load script assembly {full}: {e.Message}", e);
        }

        Type[] types;
        try
        {
            types = assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException e)
        {
            string reasons = string.Join("; ", e.LoaderExceptions.Where(x => x is not null).Select(x => x!.Message).Distinct());
            throw new ScriptException($"cannot read the types of {assembly.GetName().Name}: {reasons}", e);
        }

        Type[] candidates = [.. types
            .Where(t => t is { IsPublic: true, IsAbstract: false, IsInterface: false } && typeof(IGameScript).IsAssignableFrom(t))
            .OrderBy(t => t.FullName, StringComparer.Ordinal)];
        if (candidates.Length == 0)
            throw new ScriptException($"no public IGameScript implementation found in {assembly.GetName().Name}");

        if (typeFilter is not null)
        {
            Type[] matching = [.. candidates.Where(t => t.FullName == typeFilter || t.Name == typeFilter)];
            if (matching.Length == 0)
                throw new ScriptException($"no script type '{typeFilter}' in {assembly.GetName().Name} (available: {string.Join(", ", candidates.Select(t => t.FullName))})");
            candidates = matching;
        }

        var scripts = new List<IGameScript>();
        foreach (Type type in candidates)
        {
            try
            {
                scripts.Add((IGameScript)(Activator.CreateInstance(type) ?? throw new InvalidOperationException("constructor returned null")));
            }
            catch (Exception e) when (e is MissingMethodException or TargetInvocationException or InvalidOperationException)
            {
                string reason = e is TargetInvocationException { InnerException: { } inner } ? inner.Message : e.Message;
                throw new ScriptException($"cannot create script {type.FullName}: {reason}", e);
            }
        }
        return scripts;
    }

    /// <summary>첫 프레임 전에 한 번: 모든 스크립트의 OnStart.</summary>
    public void Start()
    {
        if (_started)
            throw new InvalidOperationException("scripts were already started");
        _started = true;
        foreach ((string name, IGameScript script) in _scripts)
            Invoke(name, nameof(IGameScript.OnStart), () => script.OnStart(_context));
    }

    public void HandleTrigger(TriggerEvent trigger)
    {
        EnsureStarted();
        foreach ((string name, IGameScript script) in _scripts)
            Invoke(name, nameof(IGameScript.OnTrigger), () => script.OnTrigger(_context, trigger));
    }

    public void HandleAction(GameAction action)
    {
        EnsureStarted();
        foreach ((string name, IGameScript script) in _scripts)
            Invoke(name, nameof(IGameScript.OnAction), () => script.OnAction(_context, action));
    }

    public void Update(float deltaSeconds)
    {
        EnsureStarted();
        foreach ((string name, IGameScript script) in _scripts)
            Invoke(name, nameof(IGameScript.OnUpdate), () => script.OnUpdate(_context, deltaSeconds));
    }

    private void EnsureStarted()
    {
        if (!_started)
            throw new InvalidOperationException("scripts were not started");
    }

    private static void Invoke(string scriptName, string method, Action call)
    {
        try
        {
            call();
        }
        catch (Exception e)
        {
            throw new ScriptException($"script {scriptName} failed in {method}: {e.GetType().Name}: {e.Message}", e);
        }
    }
}
