// Plan 0058 P4/P9 (J4): starts atlantis_csharp_fixture_server, waits for its
// session file, and stops it by closing its standard input.
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Gameplay.Tests;

public sealed class FixtureServer : IDisposable
{
    private readonly Process _process;
    private readonly string _dir;

    public FixtureServer(TestContext t, string? mutate = null, bool record = false)
    {
        _dir = Path.Combine(Path.GetTempPath(), "atlantis_csharp_tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_dir);
        SessionPath = Path.Combine(_dir, "runtime.session.json");
        RecordPath = record ? Path.Combine(_dir, "record.jsonl") : null;
        var start = new ProcessStartInfo(t.FixtureServerPath)
        {
            RedirectStandardInput = true,
            UseShellExecute = false,
        };
        start.ArgumentList.Add("--session-file");
        start.ArgumentList.Add(SessionPath);
        if (RecordPath is not null)
        {
            start.ArgumentList.Add("--record");
            start.ArgumentList.Add(RecordPath);
        }
        if (mutate is not null)
        {
            start.ArgumentList.Add("--mutate");
            start.ArgumentList.Add(mutate);
        }
        _process = Process.Start(start) ?? throw new AssertionFailed("the fixture server did not start");
        long deadline = Environment.TickCount64 + 60000;
        while (!File.Exists(SessionPath))
        {
            if (_process.HasExited) throw new AssertionFailed("the fixture server exited with " + _process.ExitCode);
            if (Environment.TickCount64 > deadline) throw new AssertionFailed("the fixture server wrote no session file");
            Thread.Sleep(10);
        }
        Session = SessionFile.Read(SessionPath);
    }

    public string SessionPath { get; }
    public string? RecordPath { get; }
    public SessionInfo Session { get; }

    public static RemoteOptions Options { get; } = new() { ResponseTimeoutMilliseconds = 20000 };

    public RemoteSession Attach() => RemoteSession.Connect(Session, Options);

    /// <summary>Closes standard input and waits for a clean exit; its exit code.</summary>
    public int Stop()
    {
        if (!_process.HasExited)
        {
            _process.StandardInput.Close();
            if (!_process.WaitForExit(20000))
            {
                _process.Kill(true);
                _process.WaitForExit();
                return -1;
            }
        }
        return _process.ExitCode;
    }

    public void Dispose()
    {
        Stop();
        _process.Dispose();
        try
        {
            Directory.Delete(_dir, true);
        }
        catch (IOException)
        {
        }
    }
}
