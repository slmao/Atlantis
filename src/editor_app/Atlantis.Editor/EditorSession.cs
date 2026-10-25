// Spec 0064 Phase 1 (Plan 0064 P4, P5): what the editor reads from the World
// and asks of the Runtime, over the Atlantis C# SDK only (ADR-0125 D1/D2):
// the entity list, the scene camera, SetViewportCamera, Raycast, the
// frame-time and latency measurements of the Phase 1 investigation, and Plan
// 0066 M1's measurement drag (WorldMatrix.column3 writes, timed), and Plan
// 0066 M5's pick overlay (SetViewportOverlay: an outline and a Gizmo) and M6's
// overlay measurement (OverlayMeasurement.cs). One
// worker thread owns the RemoteSession (the SDK is not thread-safe), so the
// UI thread never waits on the Runtime's frame. UI-free.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using W = Atlantis.Gameplay.World;

namespace Atlantis.Editor;

public sealed record EntityRow(EntityGuid Guid, string Label)
{
    public override string ToString() => Label;
}

/// <summary>The entities (sorted by GUID), the scene camera's pose, and the first Renderable's position.</summary>
public sealed record SceneSnapshot(IReadOnlyList<EntityRow> Entities, CameraPose? ScenePose, EntityGuid? FirstRenderable,
                                   Vector3? FirstRenderablePosition);

public static class SceneReader
{
    public static SceneSnapshot Read(GameplayWorld world)
    {
        IReadOnlyList<EntityGuid> entities = world.Entities();
        IReadOnlyList<Outcome<IReadOnlyList<TypeId>, AccessError>> components = world.Session.ListComponents(entities);
        var names = world.Schema.ToDictionary(type => type.Id, type => type.ShortName);
        var rows = new List<EntityRow>(entities.Count);
        EntityGuid? camera = null;
        EntityGuid? renderable = null;
        for (int i = 0; i < entities.Count; ++i)
        {
            var kinds = new List<string>();
            if (components[i].IsOk)
            {
                foreach (TypeId type in components[i].Value)
                {
                    string name = names.TryGetValue(type, out string? n) ? n : type.ToString();
                    if (name is "Transform" or "WorldMatrix") continue;
                    kinds.Add(name);
                    if (name == "Camera") camera ??= entities[i];
                    if (name == "Renderable") renderable ??= entities[i];
                }
            }
            string guid = entities[i].ToString();
            rows.Add(new EntityRow(entities[i], guid[..8] + "  " + (kinds.Count == 0 ? "(node)" : string.Join(", ", kinds))));
        }
        CameraPose? pose = null;
        if (camera is EntityGuid c)
        {
            W.Camera lens = world.Get<W.Camera>(c);
            pose = new CameraPose(Columns(world.Get<W.WorldMatrix>(c)), new CameraLens(lens.FovYRadians, lens.NearZ, lens.FarZ));
        }
        Vector3? position = null;
        if (renderable is EntityGuid r)
        {
            Vector4 column3 = world.Get<W.WorldMatrix>(r).Column3;
            position = new Vector3(column3.X, column3.Y, column3.Z);
        }
        return new SceneSnapshot(rows, pose, renderable, position);
    }

    private static float[] Columns(W.WorldMatrix m) => new[]
    {
        m.Column0.X, m.Column0.Y, m.Column0.Z, m.Column0.W, m.Column1.X, m.Column1.Y, m.Column1.Z, m.Column1.W,
        m.Column2.X, m.Column2.Y, m.Column2.Z, m.Column2.W, m.Column3.X, m.Column3.Y, m.Column3.Z, m.Column3.W,
    };

    public static ViewportCamera ToViewportCamera(CameraPose pose) =>
        new(pose.WorldMatrix, pose.Lens.FovYRadians, pose.Lens.NearZ, pose.Lens.FarZ);
}

/// <summary>
/// Per-frame times from back-to-back control.status calls (one is answered per Runtime frame), and the frame's
/// size in pixels (from one captured frame after the samples; 0 when not captured).
/// </summary>
public sealed record FrameTimes(int Frames, double MedianMs, double P95Ms, double MeanMs, double MaxMs, int MultiFrameGaps,
                                uint Width = 0, uint Height = 0)
{
    public string ToJson() => string.Format(CultureInfo.InvariantCulture,
        "{{\"frames\":{0},\"medianMs\":{1:F2},\"p95Ms\":{2:F2},\"meanMs\":{3:F2},\"maxMs\":{4:F2},\"multiFrameGaps\":{5},\"width\":{6},\"height\":{7}}}",
        Frames, MedianMs, P95Ms, MeanMs, MaxMs, MultiFrameGaps, Width, Height);

    public override string ToString() => string.Format(CultureInfo.InvariantCulture,
        "{0} frames at {5}x{6}: median {1:F2} ms, p95 {2:F2} ms, mean {3:F2} ms, max {4:F2} ms", Frames, MedianMs, P95Ms,
        MeanMs, MaxMs, Width, Height);
}

public static class FrameSampler
{
    /// <summary>
    /// Samples <paramref name="frames"/> frame intervals: the time between consecutive status answers whose frame
    /// number advanced, divided by how far it advanced.
    /// </summary>
    public static FrameTimes Measure(RemoteControl control, int frames, string? capturePath = null)
    {
        var samples = new List<double>(frames);
        int gaps = 0;
        ulong lastFrame = control.Status().Frame;
        long lastTicks = Stopwatch.GetTimestamp();
        // Align to a frame boundary first.
        while (control.Status().Frame == lastFrame) { }
        lastFrame = control.Status().Frame;
        lastTicks = Stopwatch.GetTimestamp();
        while (samples.Count < frames)
        {
            ulong frame = control.Status().Frame;
            long now = Stopwatch.GetTimestamp();
            if (frame == lastFrame) continue;
            ulong advanced = frame - lastFrame;
            double ms = Stopwatch.GetElapsedTime(lastTicks, now).TotalMilliseconds / advanced;
            if (advanced > 1) ++gaps;
            for (ulong i = 0; i < advanced && samples.Count < frames; ++i) samples.Add(ms);
            lastFrame = frame;
            lastTicks = now;
        }
        FrameTimes times = Summarize(samples, gaps);
        if (capturePath is null) return times;
        // The size the frames were drawn at: one more frame, captured.
        StepResult captured = control.Step(new StepRequest(1, capturePath));
        return captured.Report?.Image is CapturedImage image ? times with { Width = image.Width, Height = image.Height } : times;
    }

    public static FrameTimes Summarize(List<double> samples, int gaps)
    {
        var sorted = samples.OrderBy(x => x).ToList();
        double Percentile(double p) => sorted[Math.Min(sorted.Count - 1, (int)Math.Ceiling(p * sorted.Count) - 1)];
        return new FrameTimes(sorted.Count, Percentile(0.5), Percentile(0.95), sorted.Average(), sorted[^1], gaps);
    }
}

/// <summary>A pick's answer, with when its input arrived and when the answer did.</summary>
public sealed record PickResult(RaycastResult Result, PickRay Ray, double InputToAnswerMs);

/// <summary>
/// A camera update's timing: input arrival to the Runtime's acknowledgement, and (when latency is measured) to the
/// end of the first frame drawn from it.
/// </summary>
public sealed record CameraAck(double InputToAckMs, double? InputToFrameMs, int Coalesced);

/// <summary>One line per event, with milliseconds since the editor started, to editor.log.</summary>
public sealed class EditorLog : IDisposable
{
    private readonly StreamWriter _writer;
    private readonly object _lock = new();
    private readonly long _start = Stopwatch.GetTimestamp();

    public EditorLog(string path)
    {
        Path = path;
        _writer = new StreamWriter(path, append: false, new UTF8Encoding(false)) { AutoFlush = true };
    }

    public string Path { get; }

    public void Write(string line)
    {
        string stamp = Stopwatch.GetElapsedTime(_start).TotalMilliseconds.ToString("F1", CultureInfo.InvariantCulture);
        lock (_lock)
        {
            try
            {
                _writer.WriteLine(stamp + " " + line);
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }

    public void Dispose()
    {
        lock (_lock) _writer.Dispose();
    }
}

/// <summary>
/// Owns the RemoteSession on its own thread. Camera updates are coalesced (only the newest is sent); ray-casts and
/// measurements are queued. Results are posted back through <c>post</c> (the UI thread's dispatcher).
/// </summary>
public sealed class EditorWorker : IDisposable
{
    private readonly SessionInfo _info;
    private readonly Action<Action> _post;
    private readonly EditorLog _log;
    private readonly Thread _thread;
    private readonly string _capturePath;
    private readonly object _lock = new();
    private readonly Queue<Action<RemoteSession>> _work = new();
    private (ViewportCamera? Camera, long InputTicks, int Count)? _pendingCamera;
    private bool _stopping;
    private volatile bool _measureLatency;
    // Plan 0066 M1: the measurement drag (worker-thread state, except the pending update under _lock).
    private (float Dx, float Dy, long InputTicks, int Count)? _pendingDrag;
    private bool _dragActive;  // set and cleared by the worker, read under _lock
    private GameplayWorld? _world;
    private EntityGuid _dragEntity;
    private Vector4 _dragStart;
    private Vector3 _dragRight;
    private Vector3 _dragUp;
    private float _dragUnitsPerPixel;
    // The last write, until a later answer (after the frame that applied it) dates its first visible frame.
    private (long InputTicks, double AckMs, ulong Frame, int Coalesced)? _unseenWrite;

    public EditorWorker(SessionInfo info, Action<Action> post, EditorLog log)
    {
        _info = info;
        _post = post;
        _log = log;
        _capturePath = System.IO.Path.Combine(System.IO.Path.GetDirectoryName(log.Path)!, "measure.png");
        _thread = new Thread(Run) { IsBackground = true, Name = "Atlantis.Editor session" };
    }

    public event Action<SceneSnapshot>? Connected;
    public event Action<string>? Failed;
    public event Action<PickResult>? Picked;
    public event Action<CameraAck>? CameraAcked;
    public event Action<FrameTimes>? Measured;

    /// <summary>The session file's path, for the CLI command lines the log prints.</summary>
    public string? SessionPath { get; init; }

    /// <summary>Plan 0066 M1: where the measurement drag's writes and frames are recorded (null: no measurement).</summary>
    public DragRecorder? Recorder { get; init; }

    /// <summary>When on, each camera update also waits for the first frame drawn from it (one more frame of round trips).</summary>
    public bool MeasureLatency
    {
        get => _measureLatency;
        set => _measureLatency = value;
    }

    public void Start() => _thread.Start();

    public void SetCamera(ViewportCamera? camera, long inputTicks)
    {
        lock (_lock)
        {
            int count = _pendingCamera is { } pending ? pending.Count + 1 : 1;
            long first = _pendingCamera is { } p ? p.InputTicks : inputTicks;  // latency from the oldest coalesced input
            _pendingCamera = (camera, first, count);
            Monitor.Pulse(_lock);
        }
    }

    public void Raycast(PickRay ray, long inputTicks) => Enqueue(session =>
    {
        RaycastResult result = session.Control.Raycast(new Ray(ray.Origin, ray.Direction));
        double ms = Stopwatch.GetElapsedTime(inputTicks).TotalMilliseconds;
        _log.Write(string.Format(CultureInfo.InvariantCulture, "pick {0} input->answer {1:F1} ms",
                                 result.Error?.ToString() ?? result.Hit?.Entity.ToString() ?? "miss", ms));
        // Spec 0064's north star: the CLI's `runtime raycast` for the same ray names the same entity.
        _log.Write(string.Format(CultureInfo.InvariantCulture,
                                 "  same ray: atlantis --session \"{0}\" runtime raycast --origin {1:G9} {2:G9} {3:G9} --direction {4:G9} {5:G9} {6:G9}",
                                 SessionPath ?? "<session file>", ray.Origin.X, ray.Origin.Y, ray.Origin.Z, ray.Direction.X,
                                 ray.Direction.Y, ray.Direction.Z));
        var answer = new PickResult(result, ray, ms);
        _post(() => Picked?.Invoke(answer));
    });

    /// <summary>
    /// Plan 0066 M5 (Spec 0066 R7): the overlay for a pick -- <paramref name="entity"/> outlined, with a translate
    /// Gizmo at its WorldMatrix's translation (read here, on the worker thread) -- or none (null: nothing picked).
    /// An entity whose WorldMatrix cannot be read is outlined without a Gizmo.
    /// </summary>
    public void ShowPick(EntityGuid? entity) => Enqueue(session =>
    {
        ViewportOverlay? overlay = null;
        if (entity is EntityGuid picked)
        {
            overlay = ViewportOverlay.Select(picked);
            try
            {
                Vector4 column3 = _world!.Get<W.WorldMatrix>(picked).Column3;
                overlay = GizmoOverlay.ForPick(picked, new Vector3(column3.X, column3.Y, column3.Z));
            }
            catch (RefusedException e)
            {
                _log.Write("overlay: no WorldMatrix for " + picked + " (" + e.Message + "), outline only");
            }
        }
        ControlError? error = session.Control.SetViewportOverlay(overlay);
        string what = overlay is null ? "cleared" : (overlay.Groups.Count > 0 ? "outline and Gizmo for " : "outline for ") + entity;
        _log.Write("overlay " + what + (error is ControlError refused ? " refused: " + refused : ""));
    });

    /// <summary>Plan 0066 M6: the overlay measurement (OverlayMeasurement.cs); <paramref name="done"/> runs on the UI thread.</summary>
    public void MeasureOverlay(OverlayMeasureRequest request, string label, Action<OverlayReport> done) => Enqueue(session =>
    {
        OverlayReport report = MeasureOverlay(session, request, label);
        _log.Write("overlay measurement " + report);
        _post(() => done(report));
    });

    private OverlayReport MeasureOverlay(RemoteSession session, OverlayMeasureRequest request, string label)
    {
        RemoteControl control = session.Control;
        var phases = new List<OverlayPhase>();
        OverlayReport Failed(string why) => new(label, null, 0, 0, Stats.Of(Array.Empty<double>()), phases, why);

        EntityGuid entity;
        if (request.Entity is EntityGuid given)
        {
            entity = given;
        }
        else
        {
            RaycastResult hit = control.Raycast(new Ray(request.Ray.Origin, request.Ray.Direction));
            if (hit.Hit is not RayHit picked) return Failed("the pick ray hit nothing (" + (hit.Error?.ToString() ?? "miss") + ")");
            entity = picked.Entity;
        }
        Vector4 column3 = _world!.Get<W.WorldMatrix>(entity).Column3;
        ViewportOverlay overlay = GizmoOverlay.ForPick(entity, new Vector3(column3.X, column3.Y, column3.Z));
        ViewportCamera still = SceneReader.ToViewportCamera(request.Pose);
        control.SetViewportCamera(still);
        double framePeriod = FrameSampler.Measure(control, 120).MedianMs;

        // A phase: the overlay set (or not), then PhaseSeconds of frames, timed by status answers at both ends.
        void Phase(string name, ViewportOverlay? shown)
        {
            if (control.SetViewportOverlay(shown) is ControlError refused) throw new InvalidOperationException("overlay refused: " + refused);
            System.Threading.Thread.Sleep(1000);  // the change is drawn and settles before the phase starts
            RuntimeStatus first = control.Status();
            DateTime start = DateTime.UtcNow;
            long startTicks = Stopwatch.GetTimestamp();
            System.Threading.Thread.Sleep(TimeSpan.FromSeconds(request.PhaseSeconds));
            RuntimeStatus last = control.Status();
            double ms = Stopwatch.GetElapsedTime(startTicks).TotalMilliseconds;
            double period = last.Frame > first.Frame ? ms / (last.Frame - first.Frame) : 0;
            phases.Add(new OverlayPhase(name, shown is not null, start, DateTime.UtcNow, period));
        }

        // Send -> the answer after the first frame drawn with it.
        double Dated(ViewportOverlay? shown)
        {
            long start = Stopwatch.GetTimestamp();
            if (control.SetViewportOverlay(shown) is ControlError refused) throw new InvalidOperationException("overlay refused: " + refused);
            _ = control.Status();
            return Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        }

        Phase("pristine", null);
        double firstSend = Dated(overlay);
        var orbit = new OrbitCamera();
        orbit.Reset(request.Pose);
        var sends = new List<double>(request.Sends);
        for (int i = 0; i < request.Sends; ++i)
        {
            orbit.Orbit(4.0f, 0.0f);
            control.SetViewportCamera(SceneReader.ToViewportCamera(orbit.Pose()));
            control.SetViewportOverlay(null);
            orbit.Orbit(4.0f, 0.0f);
            control.SetViewportCamera(SceneReader.ToViewportCamera(orbit.Pose()));
            sends.Add(Dated(overlay));
        }
        control.SetViewportCamera(still);
        for (int pair = 0; pair < request.PhasePairs; ++pair)
        {
            Phase("off " + (pair + 1), null);
            Phase("on " + (pair + 1), overlay);
        }
        control.SetViewportOverlay(null);
        control.SetViewportCamera(null);
        return new OverlayReport(label, entity, framePeriod, firstSend, Stats.Of(sends), phases);
    }

    /// <summary>
    /// Plan 0066 M1: starts a measurement drag of <paramref name="entity"/> in the view plane of
    /// <paramref name="pose"/>, a pixel being one pane pixel at the entity's depth.
    /// </summary>
    public void BeginDrag(EntityGuid entity, CameraPose pose, int paneHeight) => Enqueue(_ =>
    {
        if (_world is null) return;
        _dragEntity = entity;
        _dragStart = _world.Get<W.WorldMatrix>(entity).Column3;
        float[] m = pose.WorldMatrix;
        _dragRight = new Vector3(m[0], m[1], m[2]);
        _dragUp = new Vector3(m[4], m[5], m[6]);
        var forward = new Vector3(-m[8], -m[9], -m[10]);
        var eye = new Vector3(m[12], m[13], m[14]);
        float depth = MathF.Max(pose.Lens.NearZ, Vector3.Dot(new Vector3(_dragStart.X, _dragStart.Y, _dragStart.Z) - eye, forward));
        _dragUnitsPerPixel = 2.0f * depth * MathF.Tan(pose.Lens.FovYRadians * 0.5f) / Math.Max(1, paneHeight);
        lock (_lock) _dragActive = true;
        _log.Write(string.Format(CultureInfo.InvariantCulture, "drag begin {0} at depth {1:F2}", entity, depth));
    });

    /// <summary>Plan 0066 M1: the drag's offset from its press, in pane pixels; coalesced like camera updates.</summary>
    public void DragTo(float dx, float dy, long inputTicks)
    {
        lock (_lock)
        {
            int count = _pendingDrag is { } pending ? pending.Count + 1 : 1;
            long first = _pendingDrag is { } p ? p.InputTicks : inputTicks;
            _pendingDrag = (dx, dy, first, count);
            Monitor.Pulse(_lock);
        }
    }

    /// <summary>Plan 0066 M1: the release -- the last offset is written, then the drag ends.</summary>
    public void EndDrag(float dx, float dy, long inputTicks)
    {
        DragTo(dx, dy, inputTicks);
        Enqueue(session =>
        {
            CloseUnseenWrite(session);
            lock (_lock) _dragActive = false;
            int refused = session.DrainFailures().Count;
            _log.Write("drag end" + (refused > 0 ? ", " + refused + " write(s) refused" : ""));
        });
    }

    public void Measure(int frames) => Enqueue(session =>
    {
        FrameTimes times = FrameSampler.Measure(session.Control, frames, _capturePath);
        _log.Write("frametimes " + times.ToJson());
        _post(() => Measured?.Invoke(times));
    });

    public void Dispose()
    {
        lock (_lock)
        {
            _stopping = true;
            Monitor.Pulse(_lock);
        }
        _thread.Join(TimeSpan.FromSeconds(5));
    }

    private void Enqueue(Action<RemoteSession> work)
    {
        lock (_lock)
        {
            _work.Enqueue(work);
            Monitor.Pulse(_lock);
        }
    }

    private void Run()
    {
        RemoteSession session;
        try
        {
            session = RemoteSession.Connect(_info);
            var world = new GameplayWorld(session);
            _world = world;
            SceneSnapshot snapshot = SceneReader.Read(world);
            _log.Write("connected: port " + _info.Port + ", " + snapshot.Entities.Count + " entities");
            _post(() => Connected?.Invoke(snapshot));
        }
        catch (Exception e)
        {
            _log.Write("connect failed: " + e.Message);
            _post(() => Failed?.Invoke(e.Message));
            return;
        }
        using (session)
        {
            try
            {
                Loop(session);
            }
            catch (Exception e)
            {
                _log.Write("session failed: " + e.Message);
                _post(() => Failed?.Invoke(e.Message));
            }
        }
    }

    private void Loop(RemoteSession session)
    {
        while (true)
        {
            Action<RemoteSession>? work = null;
            (ViewportCamera? Camera, long InputTicks, int Count)? camera = null;
            (float Dx, float Dy, long InputTicks, int Count)? drag = null;
            lock (_lock)
            {
                // A pending drag offset waits for its BeginDrag (queued work) to have run.
                while (!_stopping && _work.Count == 0 && _pendingCamera is null && !(_pendingDrag is not null && _dragActive))
                {
                    Monitor.Wait(_lock);
                }
                if (_stopping) return;
                if (_pendingCamera is not null)
                {
                    camera = _pendingCamera;
                    _pendingCamera = null;
                }
                else if (_pendingDrag is not null && _dragActive)
                {
                    drag = _pendingDrag;
                    _pendingDrag = null;
                }
                else
                {
                    work = _work.Dequeue();
                }
            }
            if (drag is { } offset)
            {
                WriteDrag(session, offset.Dx, offset.Dy, offset.InputTicks, offset.Count);
                bool more;
                lock (_lock) more = _pendingDrag is not null;
                if (!more) CloseUnseenWrite(session);  // nothing follows to date it: ask once
            }
            if (camera is { } update)
            {
                session.Control.SetViewportCamera(update.Camera);
                double ack = Stopwatch.GetElapsedTime(update.InputTicks).TotalMilliseconds;
                double? frame = null;
                if (_measureLatency)
                {
                    // The Runtime answers between frames (main.cpp: runFrame, then poll): the set was
                    // answered after frame N, so the next status is answered after frame N+1 -- the first
                    // frame drawn from the camera (submitted for presentation).
                    _ = session.Control.Status();
                    frame = Stopwatch.GetElapsedTime(update.InputTicks).TotalMilliseconds;
                }
                _log.Write(string.Format(CultureInfo.InvariantCulture, "camera {0} coalesced {1} input->ack {2:F1} ms{3}",
                                         update.Camera is null ? "cleared" : "set", update.Count, ack,
                                         frame is double f ? string.Format(CultureInfo.InvariantCulture, " input->frame {0:F1} ms", f) : ""));
                var acked = new CameraAck(ack, frame, update.Count);
                _post(() => CameraAcked?.Invoke(acked));
            }
            work?.Invoke(session);
        }
    }

    // Plan 0066 M1: one measurement write, without perturbing the next. The submit and a status are pipelined, so both
    // are answered at the same frame boundary, after frame N (the status names N); the write is applied at the start
    // of frame N+1. The next request answered -- the next write's, sent at once, or a status when no input follows --
    // is answered after frame N+1, the first frame drawn with this write: that answer dates it. Latency counts from
    // the oldest input the write coalesced (as Phase 1's camera latency did).
    private void WriteDrag(RemoteSession session, float dx, float dy, long inputTicks, int coalesced)
    {
        Vector3 offset = _dragRight * (dx * _dragUnitsPerPixel) - _dragUp * (dy * _dragUnitsPerPixel);
        var column3 = new Vector4(_dragStart.X + offset.X, _dragStart.Y + offset.Y, _dragStart.Z + offset.Z, _dragStart.W);
        var address = new PropertyAddress(_dragEntity, W.Fields.WorldMatrix.Column3.Component, W.Fields.WorldMatrix.Column3.Id);
        ulong submit = session.Send("connection.submit",
                                    JsonValue.NewObject().Set("command", Codec.Encode(new SetProperty(address, column3))));
        ulong status = session.Send("control.status");
        session.AwaitResult(submit);
        RuntimeStatus answered = Codec.DecodeStatus(session.AwaitResult(status)) ?? throw new InvalidOperationException("a malformed status");
        long now = Stopwatch.GetTimestamp();
        DateUnseenWrite(now);
        double ack = Stopwatch.GetElapsedTime(inputTicks, now).TotalMilliseconds;
        Recorder?.Frame(now, answered.Frame);
        _unseenWrite = (inputTicks, ack, answered.Frame, coalesced);
    }

    // A status answered after the frame that applied the last write dates it; nothing to do without one.
    private void CloseUnseenWrite(RemoteSession session)
    {
        if (_unseenWrite is null) return;
        RuntimeStatus answered = session.Control.Status();
        long now = Stopwatch.GetTimestamp();
        Recorder?.Frame(now, answered.Frame);
        DateUnseenWrite(now);
    }

    private void DateUnseenWrite(long answeredTicks)
    {
        if (_unseenWrite is not { } write) return;
        _unseenWrite = null;
        double toFrame = Stopwatch.GetElapsedTime(write.InputTicks, answeredTicks).TotalMilliseconds;
        Recorder?.Write(new DragWrite(write.InputTicks, write.AckMs, toFrame, write.Frame, write.Coalesced));
        _log.Write(string.Format(CultureInfo.InvariantCulture, "drag write coalesced {0} input->ack {1:F1} ms input->frame {2:F1} ms frame {3}",
                                 write.Coalesced, write.AckMs, toFrame, write.Frame));
    }
}
