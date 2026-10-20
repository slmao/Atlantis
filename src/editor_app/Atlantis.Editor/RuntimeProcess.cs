// Spec 0064 Phase 1 (Plan 0064 P4; ADR-0125 D1, ADR-0126 D2/D3): the editor
// starts atlantis_runtime as any user would -- `--listen 0 --session-file
// <tmp>`, with `--parent-window <h>` when it embeds the Viewport -- and reads
// the port, token and (embedded) child window from the session file. The
// Runtime's stdout and stderr go to runtime.log beside the session file.
using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor;

public sealed record RuntimeLaunch(string RuntimePath, string Scene, ulong? ParentWindow, string WorkDirectory);

public sealed class RuntimeProcess : IDisposable
{
    private readonly Process _process;
    private readonly StreamWriter _log;
    private readonly object _logLock = new();

    private RuntimeProcess(RuntimeLaunch launch, Process process, StreamWriter log, string sessionPath)
    {
        Launch = launch;
        _process = process;
        _log = log;
        SessionPath = sessionPath;
    }

    public RuntimeLaunch Launch { get; }
    public string SessionPath { get; }
    public string LogPath => Path.Combine(Launch.WorkDirectory, "runtime.log");
    public int Pid => _process.Id;
    public bool HasExited => _process.HasExited;
    public int ExitCode => _process.ExitCode;

    /// <summary>A fresh directory under %TEMP%\atlantis_editor for one run's session file and logs.</summary>
    public static string NewWorkDirectory()
    {
        string dir = Path.Combine(Path.GetTempPath(), "atlantis_editor",
                                  DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture) + "-" +
                                  Guid.NewGuid().ToString("N")[..6]);
        Directory.CreateDirectory(dir);
        return dir;
    }

    public static RuntimeProcess Start(RuntimeLaunch launch)
    {
        if (!File.Exists(launch.RuntimePath)) throw new FileNotFoundException("atlantis_runtime not found", launch.RuntimePath);
        Directory.CreateDirectory(launch.WorkDirectory);
        string sessionPath = Path.Combine(launch.WorkDirectory, "runtime.session.json");
        var start = new ProcessStartInfo(launch.RuntimePath)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            WorkingDirectory = Path.GetDirectoryName(launch.RuntimePath)!,
        };
        start.ArgumentList.Add("--scene");
        start.ArgumentList.Add(launch.Scene);
        start.ArgumentList.Add("--listen");
        start.ArgumentList.Add("0");
        start.ArgumentList.Add("--session-file");
        start.ArgumentList.Add(sessionPath);
        if (launch.ParentWindow is ulong parent)
        {
            start.ArgumentList.Add("--parent-window");
            start.ArgumentList.Add(parent.ToString(CultureInfo.InvariantCulture));
        }
        var log = new StreamWriter(Path.Combine(launch.WorkDirectory, "runtime.log"), append: false) { AutoFlush = true };
        log.WriteLine("# " + launch.RuntimePath + " " + string.Join(' ', start.ArgumentList));
        Process process = Process.Start(start) ?? throw new InvalidOperationException("atlantis_runtime did not start");
        var runtime = new RuntimeProcess(launch, process, log, sessionPath);
        process.OutputDataReceived += (_, e) => runtime.Append("out", e.Data);
        process.ErrorDataReceived += (_, e) => runtime.Append("err", e.Data);
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        return runtime;
    }

    /// <summary>
    /// Waits for the session file (with its window field when embedded); throws if the Runtime exits first or the
    /// time runs out.
    /// </summary>
    public SessionInfo WaitForSession(TimeSpan timeout)
    {
        long deadline = Environment.TickCount64 + (long)timeout.TotalMilliseconds;
        while (Environment.TickCount64 < deadline)
        {
            if (_process.HasExited)
            {
                throw new InvalidOperationException("atlantis_runtime exited with " + _process.ExitCode + " before listening; see " +
                                                    LogPath);
            }
            if (File.Exists(SessionPath))
            {
                try
                {
                    SessionInfo session = SessionFile.Read(SessionPath);
                    if (Launch.ParentWindow is null || session.Window is not null) return session;
                }
                catch (SessionFileException)
                {
                    // Being written: try again.
                }
            }
            Thread.Sleep(50);
        }
        throw new TimeoutException("no session file at " + SessionPath + " after " + timeout.TotalSeconds + " s");
    }

    /// <summary>Waits for the Runtime to exit; kills it after <paramref name="timeout"/>. Returns its exit code, or null if killed.</summary>
    public int? WaitForExit(TimeSpan timeout)
    {
        if (_process.WaitForExit((int)timeout.TotalMilliseconds))
        {
            _process.WaitForExit();  // drains the redirected streams
            return _process.ExitCode;
        }
        try
        {
            _process.Kill(entireProcessTree: true);
        }
        catch (InvalidOperationException)
        {
            // Exited meanwhile.
        }
        _process.WaitForExit();
        return null;
    }

    /// <summary>Asks a top-level (not embedded) Runtime to close, as its window's close button would.</summary>
    public bool CloseMainWindow() => _process.CloseMainWindow();

    public void Dispose()
    {
        if (!_process.HasExited) WaitForExit(TimeSpan.FromSeconds(1));
        _process.Dispose();
        lock (_logLock) _log.Dispose();
    }

    private void Append(string stream, string? line)
    {
        if (line is null) return;
        lock (_logLock)
        {
            try
            {
                _log.WriteLine(stream + " " + line);
            }
            catch (ObjectDisposedException)
            {
                // After Dispose.
            }
        }
    }
}
