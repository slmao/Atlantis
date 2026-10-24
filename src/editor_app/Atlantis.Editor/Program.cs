// Spec 0064 Phase 1 (Plan 0064 P4, P5): Atlantis Editor App's entry point.
//
//   Atlantis.Editor [--runtime <atlantis_runtime.exe>] [--scene <name>]
//       The editor shell, embedding the Runtime's Viewport (V-a, route R-a).
//   Atlantis.Editor --smoke [--runtime <exe>] [--scene <name>] [--out <file>]
//       Headless: starts the Runtime without embedding, connects, lists the
//       entities, sets and clears a camera (checking the frame's view), and
//       ray-casts; exit 0 when every check passed.
//   Atlantis.Editor --measure-frames <n> --session <session file> [--out <file>]
//       Attaches to a running Runtime and samples n frame times (P5 U1's
//       standalone baseline, measured as the shell measures embedded).
//   Atlantis.Editor --measure-drag [--runtime <exe>] [--scene <name>] [--attach <session file>]
//                   [--drive <n> [--settle <s>] [--exit-after]]
//       Plan 0066 M1's input investigation (measurement-only): Shift + left
//       drag moves the selected entity in the view plane, timed; --attach
//       drives an already running, unembedded Runtime (the no-attachment
//       control); --drive replays n protocol drags with real OS input (it
//       takes over the mouse), --settle seconds after connecting (default 5),
//       and writes drag-report.json beside editor.log.
//
// --runtime defaults to ATLANTIS_RUNTIME; --scene defaults to the Runtime's
// own default scene, integrated_showcase_demo (src/runtime/cli.h).
using System;
using System.IO;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;

namespace Atlantis.Editor;

public static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        string? runtime = Environment.GetEnvironmentVariable("ATLANTIS_RUNTIME");
        string scene = "integrated_showcase_demo";
        string? outPath = null;
        string? sessionPath = null;
        int measureFrames = 0;
        bool smoke = false;
        bool measureDrag = false;
        string? attach = null;
        int drive = 0;
        bool exitAfter = false;
        int settle = 5;
        for (int i = 0; i < args.Length; ++i)
        {
            string arg = args[i];
            string Next() => i + 1 < args.Length ? args[++i] : throw new ArgumentException(arg + " requires a value");
            switch (arg)
            {
                case "--runtime": runtime = Next(); break;
                case "--scene": scene = Next(); break;
                case "--out": outPath = Next(); break;
                case "--session": sessionPath = Next(); break;
                case "--measure-frames": measureFrames = int.Parse(Next(), System.Globalization.CultureInfo.InvariantCulture); break;
                case "--smoke": smoke = true; break;
                case "--measure-drag": measureDrag = true; break;
                case "--attach": attach = Next(); break;
                case "--drive": drive = int.Parse(Next(), System.Globalization.CultureInfo.InvariantCulture); break;
                case "--exit-after": exitAfter = true; break;
                case "--settle": settle = int.Parse(Next(), System.Globalization.CultureInfo.InvariantCulture); break;
                default: return Usage("unknown argument " + arg);
            }
        }

        if (measureFrames > 0)
        {
            if (sessionPath is null) return Usage("--measure-frames needs --session <session file>");
            using RemoteSession session = RemoteSession.Connect(SessionFile.Read(sessionPath));
            FrameTimes times = FrameSampler.Measure(session.Control, measureFrames,
                                                    Path.Combine(Path.GetTempPath(), "atlantis_editor_measure.png"));
            return Write(outPath, times.ToJson()) ? 0 : 1;
        }
        if ((attach is not null || drive > 0 || exitAfter) && !measureDrag) return Usage("--attach, --drive, --settle and --exit-after need --measure-drag");
        if (attach is null && runtime is null) return Usage("pass --runtime <atlantis_runtime.exe> or set ATLANTIS_RUNTIME");
        runtime = runtime is null ? "" : Path.GetFullPath(runtime);
        if (smoke) return Smoke.Run(runtime, scene, outPath);

        var options = new EditorOptions(runtime, scene)
        {
            MeasureDrag = measureDrag,
            AttachSession = attach is null ? null : Path.GetFullPath(attach),
            DriveDrags = drive,
            ExitAfterDrive = exitAfter,
            SettleSeconds = settle,
        };
        return AppBuilder.Configure(() => new EditorApplication(options))
            .UsePlatformDetect()
            .StartWithClassicDesktopLifetime(args);
    }

    private static int Usage(string why)
    {
        Console.Error.WriteLine("Atlantis.Editor: " + why);
        Console.Error.WriteLine(
            "usage: Atlantis.Editor [--runtime <exe>] [--scene <name>]\n" +
            "       Atlantis.Editor --smoke [--runtime <exe>] [--scene <name>] [--out <file>]\n" +
            "       Atlantis.Editor --measure-frames <n> --session <session file> [--out <file>]\n" +
            "       Atlantis.Editor --measure-drag [--runtime <exe>] [--scene <name>] [--attach <session file>] [--drive <n> [--settle <s>] [--exit-after]]");
        return 2;
    }

    internal static bool Write(string? path, string text)
    {
        if (path is null)
        {
            Console.Out.WriteLine(text);
            return true;
        }
        File.WriteAllText(path, text + "\n");
        return true;
    }
}

public sealed class EditorApplication : Application
{
    private readonly EditorOptions _options;

    public EditorApplication(EditorOptions options) => _options = options;

    public override void Initialize() => Styles.Add(new FluentTheme());

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop) desktop.MainWindow = new MainWindow(_options);
        base.OnFrameworkInitializationCompleted();
    }
}
