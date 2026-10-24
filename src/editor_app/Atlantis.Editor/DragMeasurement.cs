// Plan 0066 M1 (Spec 0066 R8, Q3, Q6): the input investigation's measurement
// -- measurement-only, not an editing feature. While --measure-drag is on, a
// Shift + left drag in the Viewport translates the selected entity in the view
// plane by writing its WorldMatrix.column3 on the Runtime World (runtime-side
// edits, recorded nowhere: Spec 0066 Q5), and this recorder keeps:
//   - each write: pointer input -> the write answered -> the first frame drawn
//     with it (the next answer, after the frame that applied it), with the
//     Runtime frame number; the worker never blocks a write to measure the
//     previous one (EditorWorker.WriteDrag);
//   - the UI thread's 16 ms timer gaps, each with its time, and the drags'
//     press and release times, to classify the gaps as during a drag, at a
//     transition (within 250 ms of a press or a release) or idle.
// The report (drag-report.json beside editor.log) states the medians, p95s and
// maxima Spec 0066 Q6's budgets are read against. InputDriver replays the
// protocol's drags with real OS input (SendInput) so a run is repeatable.
// Thread-safe: the UI thread and the session worker both record.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace Atlantis.Editor;

/// <summary>One measurement write: its latencies and the Runtime frame its answer named.</summary>
public readonly record struct DragWrite(long InputTicks, double InputToAckMs, double InputToFrameMs, ulong Frame, int Coalesced);

/// <summary>Percentiles of one set of samples (milliseconds).</summary>
public sealed record Stats(int Count, double Median, double P95, double P99, double Max, int Over50, int Over100)
{
    public static Stats Of(IReadOnlyCollection<double> samples)
    {
        if (samples.Count == 0) return new Stats(0, 0, 0, 0, 0, 0, 0);
        var sorted = samples.OrderBy(x => x).ToList();
        double Percentile(double p) => sorted[Math.Min(sorted.Count - 1, Math.Max(0, (int)Math.Ceiling(p * sorted.Count) - 1))];
        return new Stats(sorted.Count, Percentile(0.5), Percentile(0.95), Percentile(0.99), sorted[^1], sorted.Count(x => x > 50.0),
                         sorted.Count(x => x > 100.0));
    }

    public string ToJson() => string.Format(CultureInfo.InvariantCulture,
        "{{\"count\":{0},\"median\":{1:F1},\"p95\":{2:F1},\"p99\":{3:F1},\"max\":{4:F1},\"over50\":{5},\"over100\":{6}}}", Count,
        Median, P95, P99, Max, Over50, Over100);
}

public sealed class DragRecorder
{
    /// <summary>A gap within this long after a press or a release is a transition's.</summary>
    public const double TransitionMs = 250.0;

    private readonly object _lock = new();
    private readonly List<(long EndTicks, double GapMs)> _uiGaps = new();
    private readonly List<long> _presses = new();
    private readonly List<long> _releases = new();
    private readonly List<DragWrite> _writes = new();
    private readonly List<(long Ticks, ulong Frame)> _frames = new();

    public void UiGap(long endTicks, double gapMs)
    {
        lock (_lock) _uiGaps.Add((endTicks, gapMs));
    }

    public void Press(long ticks)
    {
        lock (_lock) _presses.Add(ticks);
    }

    public void Release(long ticks)
    {
        lock (_lock) _releases.Add(ticks);
    }

    public void Write(DragWrite write)
    {
        lock (_lock) _writes.Add(write);
    }

    public void Frame(long ticks, ulong frame)
    {
        lock (_lock) _frames.Add((ticks, frame));
    }

    public int Drags
    {
        get
        {
            lock (_lock) return _releases.Count;
        }
    }

    /// <summary>The report; <paramref name="label"/> names the run (scene, embedded or control).</summary>
    public DragReport Report(string label)
    {
        lock (_lock)
        {
            var drag = new List<double>();
            var transition = new List<double>();
            var idle = new List<double>();
            var drags = _presses.Zip(_releases, (p, r) => (Press: p, Release: r)).ToList();
            long transitionTicks = (long)(TransitionMs * Stopwatch.Frequency / 1000.0);
            foreach ((long end, double gap) in _uiGaps)
            {
                long start = end - (long)(gap * Stopwatch.Frequency / 1000.0);
                bool atTransition = _presses.Concat(_releases).Any(t => start <= t + transitionTicks && end >= t);
                bool inDrag = drags.Any(d => end > d.Press && start < d.Release);
                if (atTransition) transition.Add(gap);
                else if (inDrag) drag.Add(gap);
                else idle.Add(gap);
            }
            // The Runtime's frame time over the drags: consecutive frame samples inside a drag.
            var frameTimes = new List<double>();
            for (int i = 1; i < _frames.Count; ++i)
            {
                (long t0, ulong f0) = _frames[i - 1];
                (long t1, ulong f1) = _frames[i];
                if (f1 <= f0 || !drags.Any(d => t0 >= d.Press && t1 <= d.Release)) continue;
                frameTimes.Add(Stopwatch.GetElapsedTime(t0, t1).TotalMilliseconds / (f1 - f0));
            }
            Stats frame = Stats.Of(frameTimes);
            Stats toFrame = Stats.Of(_writes.Select(w => w.InputToFrameMs).ToList());
            return new DragReport(label, drags.Count, _writes.Count, Stats.Of(_writes.Select(w => w.InputToAckMs).ToList()), toFrame,
                                  frame, Stats.Of(drag), Stats.Of(transition), Stats.Of(idle));
        }
    }
}

/// <summary>
/// One run's results. Latencies in milliseconds; <c>InputToFrame</c> is pointer input to the end of the first Runtime
/// frame drawn with the write (submitted for presentation); "frames" divide by the median Runtime frame time.
/// </summary>
public sealed record DragReport(string Label, int Drags, int Writes, Stats InputToAck, Stats InputToFrame, Stats RuntimeFrame,
                                Stats UiGapsInDrag, Stats UiGapsAtTransitions, Stats UiGapsIdle)
{
    public double MedianFrames => RuntimeFrame.Median > 0 ? InputToFrame.Median / RuntimeFrame.Median : 0;
    public double P95Frames => RuntimeFrame.Median > 0 ? InputToFrame.P95 / RuntimeFrame.Median : 0;
    public double MaxUiGap => Math.Max(UiGapsInDrag.Max, Math.Max(UiGapsAtTransitions.Max, UiGapsIdle.Max));

    public string ToJson() => string.Format(CultureInfo.InvariantCulture,
        "{{\"label\":\"{0}\",\"drags\":{1},\"writes\":{2},\"inputToAckMs\":{3},\"inputToFrameMs\":{4},\"runtimeFrameMs\":{5}," +
        "\"inputToFrameFrames\":{{\"median\":{6:F2},\"p95\":{7:F2}}},\"uiGapsMs\":{{\"inDrag\":{8},\"atTransitions\":{9},\"idle\":{10}}}," +
        "\"maxUiGapMs\":{11:F1}}}", Label, Drags, Writes, InputToAck.ToJson(), InputToFrame.ToJson(), RuntimeFrame.ToJson(), MedianFrames,
        P95Frames, UiGapsInDrag.ToJson(), UiGapsAtTransitions.ToJson(), UiGapsIdle.ToJson(), MaxUiGap);

    public override string ToString() => string.Format(CultureInfo.InvariantCulture,
        "{0}: {1} drags, {2} writes; input->frame median {3:F1} ms ({4:F2} frames), p95 {5:F1} ms ({6:F2} frames); Runtime frame median {7:F1} ms; " +
        "UI gaps max {8:F1} ms (drag {9:F1}, transitions {10:F1}, idle {11:F1}), over 100 ms: {12}", Label, Drags, Writes, InputToFrame.Median,
        MedianFrames, InputToFrame.P95, P95Frames, RuntimeFrame.Median, MaxUiGap, UiGapsInDrag.Max, UiGapsAtTransitions.Max, UiGapsIdle.Max,
        UiGapsInDrag.Over100 + UiGapsAtTransitions.Over100 + UiGapsIdle.Over100);
}

/// <summary>
/// Replays the M1 protocol's drags with real OS mouse input (SendInput), so the input travels the same path a hand
/// does -- through the Runtime's child window and R-a's forwarding when embedded. Each drag: left down at the
/// Viewport's centre, 1.2 s of movement along a circle, a 0.5 s pause, 1.2 s more, left up, then 1 s apart; after
/// the last, 10 s idle. No keyboard input is injected (it would go to the foreground window, which the editor cannot
/// claim): in a driven run every left drag is a measurement drag. It takes over the mouse for the run. Runs on its
/// own thread.
/// </summary>
public static class InputDriver
{
    public const int StepMs = 10;

    public static void Run(Func<(int Left, int Top, int Width, int Height)> viewportOnScreen, int drags, Action<string> log,
                           Action finished)
    {
        var thread = new Thread(() =>
        {
            try
            {
                for (int i = 0; i < drags; ++i)
                {
                    (int left, int top, int width, int height) = viewportOnScreen();
                    int cx = left + width / 2;
                    int cy = top + height / 2;
                    int radius = Math.Max(20, Math.Min(width, height) / 8);
                    MoveTo(cx + radius, cy);
                    Thread.Sleep(200);
                    log("driver: press at screen " + (cx + radius) + "," + cy);
                    Button(true);
                    Circle(cx, cy, radius, 0.0, 1200);
                    Thread.Sleep(500);
                    Circle(cx, cy, radius, Math.PI, 1200);
                    Button(false);
                    log("driver: release");
                    log("driver: drag " + (i + 1) + " of " + drags + " done");
                    Thread.Sleep(1000);
                }
                log("driver: 10 s idle");
                Thread.Sleep(10000);
            }
            catch (Exception e)
            {
                log("driver failed: " + e.Message);
                Button(false);
            }
            finished();
        }) { IsBackground = true, Name = "Atlantis.Editor input driver" };
        thread.Start();
    }

    // Half a circle from angle `from`, in steps of StepMs over `durationMs`.
    private static void Circle(int cx, int cy, int radius, double from, int durationMs)
    {
        int steps = durationMs / StepMs;
        long start = Stopwatch.GetTimestamp();
        for (int s = 1; s <= steps; ++s)
        {
            double angle = from + Math.PI * s / steps;
            MoveTo(cx + (int)Math.Round(radius * Math.Cos(angle)), cy + (int)Math.Round(radius * Math.Sin(angle)));
            double due = s * StepMs;
            double elapsed = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            if (due > elapsed) Thread.Sleep((int)(due - elapsed));
        }
    }

    private static void MoveTo(int x, int y)
    {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = Math.Max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN)), vh = Math.Max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
        var input = new INPUT { type = INPUT_MOUSE };
        input.u.mi.dx = (int)Math.Round((x - vx) * 65535.0 / (vw - 1));
        input.u.mi.dy = (int)Math.Round((y - vy) * 65535.0 / (vh - 1));
        input.u.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        Send(input);
    }

    private static void Button(bool down)
    {
        var input = new INPUT { type = INPUT_MOUSE };
        input.u.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        Send(input);
    }

    private static void Send(INPUT input)
    {
        if (SendInput(1, new[] { input }, Marshal.SizeOf<INPUT>()) != 1) throw new InvalidOperationException("SendInput was refused");
    }

    // --- user32.

    [StructLayout(LayoutKind.Sequential)]
    private struct MOUSEINPUT
    {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public MOUSEINPUT mi;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct INPUT
    {
        public uint type;
        public InputUnion u;
    }

    private const uint INPUT_MOUSE = 0;
    private const uint MOUSEEVENTF_MOVE = 0x0001;
    private const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    private const uint MOUSEEVENTF_LEFTUP = 0x0004;
    private const uint MOUSEEVENTF_VIRTUALDESK = 0x4000;
    private const uint MOUSEEVENTF_ABSOLUTE = 0x8000;
    private const int SM_XVIRTUALSCREEN = 76;
    private const int SM_YVIRTUALSCREEN = 77;
    private const int SM_CXVIRTUALSCREEN = 78;
    private const int SM_CYVIRTUALSCREEN = 79;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, INPUT[] inputs, int size);

    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int index);
}

/// <summary>Writes the report beside editor.log, as JSON and as one human line.</summary>
public static class DragReportFile
{
    public static string Write(string directory, DragReport report)
    {
        string path = System.IO.Path.Combine(directory, "drag-report.json");
        System.IO.File.WriteAllText(path, report.ToJson() + "\n", new UTF8Encoding(false));
        return path;
    }
}
