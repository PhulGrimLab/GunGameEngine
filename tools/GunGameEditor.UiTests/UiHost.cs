using System.Windows;
using System.Windows.Threading;

namespace GunGameEditor.UiTests;

/// <summary>
/// 모든 UI 테스트가 공유하는 STA 스레드 + Dispatcher. WPF는 프로세스당 Application이 하나뿐이고
/// 창은 만든 스레드에서만 만질 수 있으므로, 테스트 본문은 항상 이 스레드에서 실행한다.
/// </summary>
public sealed class UiHost : IDisposable
{
    private readonly Dispatcher _dispatcher;

    public UiHost()
    {
        Dispatcher? dispatcher = null;
        using var ready = new ManualResetEventSlim();
        var thread = new Thread(() =>
        {
            _ = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            dispatcher = Dispatcher.CurrentDispatcher;
            ready.Set();
            Dispatcher.Run();
        }) { IsBackground = true, Name = "UiTestHost" };
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        ready.Wait();
        _dispatcher = dispatcher!;
    }

    public void Run(Action action) => _dispatcher.Invoke(action);

    public void Dispose() => _dispatcher.InvokeShutdown();
}

/// <summary>같은 컬렉션의 테스트는 직렬로 실행되므로 한 번에 창 하나만 떠 있다.</summary>
[CollectionDefinition(Name)]
public sealed class UiCollection : ICollectionFixture<UiHost>
{
    public const string Name = "Editor UI";
}
