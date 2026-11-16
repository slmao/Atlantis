// Spec 0064 Phase 1 (Plan 0064 P4, P5): what the editor reads from the World
// and asks of the Runtime, over the Atlantis C# SDK only (ADR-0125 D1/D2):
// the entity list, the scene camera, SetViewportCamera, Raycast, the
// frame-time and latency measurements of the Phase 1 investigation, and Plan
// 0066 M1's measurement drag (WorldMatrix.column3 writes, timed), and Plan
// 0066 M5's pick overlay (SetViewportOverlay: an outline and a Gizmo) and M6's
// overlay measurement (OverlayMeasurement.cs). Plan 0068 M2: the authoring
// document -- opened through the session's AuthoringSession, its events
// drained every ~50 ms into the Hierarchy's tree, the local-Transform cache
// and the apply controller; the Hierarchy's edits as one transaction each,
// then an apply (ADR-0135 D2, D3); the selection's overlay. One
// worker thread owns the RemoteSession (the SDK is not thread-safe), so the
// UI thread never waits on the Runtime's frame. UI-free.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Remote;
using W = Atlantis.Gameplay.World;
using Command = Atlantis.Gameplay.Authoring.Command;
using CommandFailure = Atlantis.Gameplay.Authoring.CommandFailure;
using Event = Atlantis.Gameplay.Authoring.Event;
using EventFilter = Atlantis.Gameplay.Authoring.EventFilter;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;
using AddComponent = Atlantis.Gameplay.Authoring.AddComponent;
using RemoveComponent = Atlantis.Gameplay.Authoring.RemoveComponent;
using PropertyChanged = Atlantis.Gameplay.Authoring.PropertyChanged;
using ComponentAdded = Atlantis.Gameplay.Authoring.ComponentAdded;
using ComponentRemoved = Atlantis.Gameplay.Authoring.ComponentRemoved;
using Atlantis.Editor.Model;

namespace Atlantis.Editor;

internal enum TextureImportKind { Color, Normal, MetallicRoughness, Occlusion }

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
public sealed record SavedSceneReceipt(string Name, AssetGuid Scene, string Source);

public sealed record PickResult(RaycastResult Result, PickRay Ray, double InputToAnswerMs)
{
    public ulong Request { get; init; }
}

/// <summary>
/// A camera update's timing: input arrival to the Runtime's acknowledgement, and (when latency is measured) to the
/// end of the first frame drawn from it.
/// </summary>
public sealed record CameraAck(double InputToAckMs, double? InputToFrameMs, int Coalesced);
public sealed record ViewportNavigationState(long Intent, bool Supported, EditorCamera? EditorCamera,
    CameraPose? SceneCamera, bool ProjectionReady, string? Message);

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
/// <summary>The selected node's world matrix and what its Gizmo can edit, as the worker last computed them.</summary>
/// <summary>Plan 0068 M5 (Spec 0068 R8): what the toolbar shows of the history.</summary>
public sealed record HistorySnapshot(bool CanUndo, bool CanRedo, string UndoLabel, string RedoLabel, string Notice);

public sealed record GroupGizmoSnapshot(long Lifetime, ulong Selection, GizmoMode Mode, bool Snap, GroupGizmo Placement);
public sealed record GizmoSnapshot(EntityGuid? Node, float[]? World, GizmoAvailability? Availability,
    GroupGizmoSnapshot? Group = null);

/// <summary>Previews: the Runtime World's WorldMatrix of each node, one transaction per batch (ADR-0135 D1). Public for
/// the tests project's live integration test, which drives real drags as EditMeasurement does.</summary>
public sealed class RuntimeWorldPreview : IWorldPreview
{
    private static readonly Field<W.WorldMatrix, Vector4>[] Columns =
    {
        W.Fields.WorldMatrix.Column0, W.Fields.WorldMatrix.Column1, W.Fields.WorldMatrix.Column2, W.Fields.WorldMatrix.Column3,
    };

    private readonly Func<RemoteSession?> _session;

    public RuntimeWorldPreview(Func<RemoteSession?> session) => _session = session;

    public void Write(IReadOnlyList<(EntityGuid Node, float[] World)> matrices)
    {
        RemoteSession? session = _session();
        if (session is null) return;
        var commands = new List<Atlantis.Gameplay.Command>(matrices.Count * 4);
        foreach (var (node, m) in matrices)
        {
            for (int k = 0; k < 4; ++k)
            {
                commands.Add(new Atlantis.Gameplay.SetProperty(new PropertyAddress(node, Columns[k].Component, Columns[k].Id),
                                                               new Vector4(m[k * 4], m[k * 4 + 1], m[k * 4 + 2], m[k * 4 + 3])));
            }
        }
        session.SubmitTransaction(commands);
    }
}

/// <summary>The Inspector as the worker last read it: the subject's fields, its components and the ones it could add.</summary>
public sealed record InspectorSnapshot(EntityGuid? Subject, IReadOnlyList<InspectorField> Fields,
                                       IReadOnlyList<(TypeId Id, string Name)> Components, IReadOnlyList<(TypeId Id, string Name)> Addable,
                                       string Name = "");

public sealed class ReparentSource
{
    internal ReparentSource(HierarchyRow row, object incarnation, long lifetime)
    {
        Node = row.Node;
        Parent = row.Parent;
        Incarnation = incarnation;
        Lifetime = lifetime;
    }

    public EntityGuid Node { get; }
    public EntityGuid? Parent { get; }
    public long Lifetime { get; }
    internal object Incarnation { get; }
}

public sealed class EditorWorker : IDisposable
{
    private readonly SessionInfo _info;
    private readonly Action<Action> _post;
    private readonly EditorLog _log;
    private readonly Thread _thread;
    private readonly string _capturePath;
    private readonly object _lock = new();
    private readonly Queue<Action<RemoteSession>> _work = new();
    private (EditorCamera Camera, long InputTicks, int Count, long Intent)? _pendingCamera;
    private readonly List<ViewportMotion> _pendingCameraMotion = new();
    private long _cameraIntent;
    private (int Width, int Height) _viewportExtent;
    private ulong _viewportExtentRevision;
    private bool _guidesEnabled, _guideRefreshQueued;
    private ulong _guideRevision;
    private sealed record GuideIntent(ulong Revision, ulong Selection, ulong Extent, long Lifetime);
    private (GuideIntent Intent, ViewportGuides.Result Value)? _guides;
    private IReadOnlyList<EntityGuid> _guideChain = Array.Empty<EntityGuid>();
    private bool _navigationSupported;
    private bool _navigationUncertain;
    private ulong _navigationGeneration;
    private CameraPose? _navigationSceneCamera;
    private EditorCamera? _confirmedEditorCamera;
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
    // Plan 0068 M2: the authoring document's view, worker-thread state.
    private const double PollMilliseconds = 50.0;
    private IAuthoringDocument? _document;
    private readonly DocumentTree _tree = new();
    private readonly LocalTransforms _transforms = new();
    private ApplyController? _apply;
    // Plan 0068 M5: the history of this editor's own edits, property and structural (ADR-0136), over the document.
    private EditHistory? _history;
    private AuthoringEditTarget? _historyTarget;
    private string _applyText = "";
    // Plan 0068 M6 (Spec 0068 R9): the Runtime's play state -- true until the first status says otherwise (it starts
    // running), polled on the document's cadence and after each transport call.
    private bool _playing = true;
    private ulong _postedGeneration = ulong.MaxValue;
    private bool _transformsChanged;
    private long _lastPoll;
    private IReadOnlyList<Marker> _markers = Array.Empty<Marker>();
    private EntityGuid? _selected;
    private IReadOnlyList<EntityGuid> _selectedNodes = Array.Empty<EntityGuid>();
    private ulong _selectionGeneration;
    private ulong _requestedSelectionGeneration;
    private bool _batchWritable;
    private bool _groupWritable;
    private volatile bool _duplicateBatchWritable;
    private bool _duplicateBatchQueued;
    private bool _sceneObjectQueued;
    private int _queuedGizmoBegins;
    public bool DuplicateBatchAvailable => _duplicateBatchWritable;
    private sealed record GroupGrab(long Id, GroupGizmoSnapshot View, GizmoAxis Axis);
    private long _nextGroupGrab;
    private GroupGrab? _requestedGroup;
    private GroupGrab? _groupGrab;
    private bool _groupSettling;
    private (GroupGrab Grab, float Amount)? _pendingGroup;
    private GroupGizmoSample? _groupPreview;
    private bool _batchSettling;
    private InspectorModel? _inspector;
    private AssetReferencePicker? _referencePicker;
    private long _referenceRequest;
    // Plan 0068 M4: the Gizmo (worker-thread state, except the pending batch under _lock).
    private GizmoDragController? _gizmo;
    private RemoteSession? _previewSession;
    private GizmoMode _gizmoMode = GizmoMode.Translate;
    private GizmoSpace _gizmoSpace = GizmoSpace.World;
    private bool _gizmoSnap;
    private (float Amount, long Ticks, int Count)? _pendingGizmo;
    private bool _gizmoBlocked;
    private long _gizmoRecoveryEpoch;
    private (bool Apply, bool Preview)? _applyControls;
    private bool _gizmoActive;  // set and cleared by the worker, read under _lock
    // One drag, pickup, or settling move per worker. State is protected by _lock.
    private ReparentSource? _reparentSource;
    private bool _reparentQueued;
    private bool _siblingMoveQueued;
    private volatile bool _siblingOrderAvailable;
    public bool SiblingOrderAvailable => _siblingOrderAvailable;
    private volatile bool _siblingMoveSupported;
    public bool SiblingMoveSupported => _siblingMoveSupported;
    // Published rows keep the identity of the node they depicted, including while the UI holds an old row.
    private readonly ConditionalWeakTable<HierarchyRow, object> _hierarchyIncarnations = new();
    private long? _commitTicks;  // the last commit's start, until its Applied dates it
    private int _previewBatches;

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
    public event Action<ViewportNavigationState>? NavigationChanged;
    public event Action<FrameTimes>? Measured;
    // Plan 0068 M2.
    public event Action<DocumentInfo?>? DocumentOpened;
    public event Action<long>? MeshDocumentReopened;
    private long _meshDocumentLifetime;
    public long MeshDocumentLifetime => Interlocked.Read(ref _meshDocumentLifetime);
    public event Action<IReadOnlyList<HierarchyRow>, IReadOnlyList<Marker>>? HierarchyChanged;
    public event Action<string?>? GuideFeedback;
    private ApplyStatus? _postedApplyStatus;
    public event Action<ApplyStatus, string>? ApplyStateChanged;
    public event Action<bool, bool>? ApplyControlsChanged;
    // Plan 0068 M6: whether the Runtime is playing (false: paused).
    public event Action<bool>? PlayStateChanged;
    public event Action<IReadOnlyList<EntityGuid>>? NodesDestroyed;
    public event Action<string>? EditNotice;
    public event Action<EntityGuid>? NodeCreatedByEditor;
    public event Action<InspectorSnapshot>? InspectorChanged;
    public event Action<BatchInspectorSnapshot>? BatchInspectorChanged;
    public event Action<long, string>? AssetReferenceInvalidated;
    public event Action<GizmoSnapshot>? GizmoChanged;
    public event Action<long>? GroupGrabEnded;
    public event Action<HistorySnapshot>? HistoryChanged;
    public event Action<double, string>? CommitApplied;

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

    // Query work stays on this worker; posted callbacks own their values.
    public void LoadAssets(Action<Outcome<IReadOnlyList<Atlantis.Gameplay.Assets.AssetSummary>, Atlantis.Gameplay.Assets.AssetError>> done) =>
        Enqueue(session => { var result = session.Assets.ListAssets(); _post(() => done(result)); });
    public void LoadReferenceAssets(long request, long listRequest,
        Action<Outcome<IReadOnlyList<Atlantis.Gameplay.Assets.AssetSummary>, Atlantis.Gameplay.Assets.AssetError>> done) =>
        Enqueue(session =>
        {
            if (_referencePicker is not { } picker || picker.Target.Request != request ||
                !picker.BeginList(listRequest)) return;
            var result = session.Assets.ListAssets();
            picker.CompleteList(listRequest, result);
            _post(() => done(result));
        });
    public void ReadAsset(AssetGuid guid, Action<Outcome<Atlantis.Gameplay.Assets.AssetDetails, Atlantis.Gameplay.Assets.AssetError>> done) =>
        Enqueue(session => { var result = session.Assets.GetAsset(guid); _post(() => done(result)); });
    public void RebuildAsset(AssetGuid guid, Action<Outcome<Atlantis.Gameplay.Assets.RebuildJob, Atlantis.Gameplay.Assets.RebuildError>> done) =>
        Enqueue(session => { var result = session.Assets.Rebuild(guid); _post(() => done(result)); });
    public void QueryRebuild(Atlantis.Gameplay.Assets.RebuildId? id,
        Action<Outcome<Atlantis.Gameplay.Assets.RebuildStatus, Atlantis.Gameplay.Assets.RebuildError>> done) =>
        Enqueue(session => { var result = session.Assets.RebuildStatus(id); _post(() => done(result)); });

    public void ImportAsset(string name, Atlantis.Gameplay.Assets.ImportMode mode,
        Action<Outcome<Atlantis.Gameplay.Assets.ImportJob, Atlantis.Gameplay.Assets.ImportError>> done) =>
        Enqueue(session => { var result = session.Assets.ImportAsset(name, mode); _post(() => done(result)); });
    public void QueryImport(Atlantis.Gameplay.Assets.ImportId? id,
        Action<Outcome<Atlantis.Gameplay.Assets.ImportStatus, Atlantis.Gameplay.Assets.ImportError>> done) =>
        Enqueue(session => { var result = session.Assets.ImportStatus(id); _post(() => done(result)); });

    public void ReadMaterial(AssetGuid guid, Action<Outcome<Atlantis.Gameplay.Assets.MaterialRead, Atlantis.Gameplay.Assets.MaterialError>, bool, bool, bool, bool> done) =>
        Enqueue(session =>
        {
            var result = session.Assets.MaterialReadWithCombinedMaps(guid);
            bool combined = result.IsOk || result.Error != Atlantis.Gameplay.Assets.MaterialError.Unsupported;
            if (combined)
            {
                _post(() => done(result, false, true, true, true));
                return;
            }
            result = session.Assets.MaterialReadWithOcclusion(guid);
            bool occlusion = result.IsOk || result.Error != Atlantis.Gameplay.Assets.MaterialError.Unsupported;
            if (!occlusion) result = session.Assets.MaterialReadWithMetallicRoughness(guid);
            bool packed = result.IsOk || result.Error != Atlantis.Gameplay.Assets.MaterialError.Unsupported;
            if (!packed) result = session.Assets.MaterialReadWithMaps(guid);
            bool basic = !result.IsOk && result.Error == Atlantis.Gameplay.Assets.MaterialError.Unsupported;
            if (basic) result = session.Assets.MaterialRead(guid);
            _post(() => done(result, basic, packed, occlusion, false));
        });

    public void SaveMaterial(Atlantis.Gameplay.Assets.MaterialSave request, bool withMaps, bool withMetallicRoughness, bool withOcclusion, bool withCombinedMaps,
        Action<Outcome<Atlantis.Gameplay.Assets.MaterialJob, Atlantis.Gameplay.Assets.MaterialError>> done) =>
        Enqueue(session =>
        {
            var result = withCombinedMaps ? session.Assets.MaterialSaveWithCombinedMaps(request) :
                withOcclusion ? session.Assets.MaterialSaveWithOcclusion(request) :
                withMetallicRoughness ? session.Assets.MaterialSaveWithMetallicRoughness(request) :
                withMaps ? session.Assets.MaterialSaveWithMaps(request) : session.Assets.MaterialSave(request);
            _post(() => done(result));
        });

    internal void ImportTexture(string name, Atlantis.Gameplay.Assets.ImportMode mode, TextureImportKind kind,
        Action<Outcome<Atlantis.Gameplay.Assets.TextureImportJob, Atlantis.Gameplay.Assets.ImportError>> done) =>
        Enqueue(session =>
        {
            var result = kind switch
            {
                TextureImportKind.Occlusion => session.Assets.OcclusionImport(name, mode),
                TextureImportKind.MetallicRoughness => session.Assets.MetallicRoughnessImport(name, mode),
                TextureImportKind.Normal => session.Assets.NormalMapImport(name, mode),
                _ => session.Assets.TextureImport(name, mode)
            };
            _post(() => done(result));
        });

    internal void QueryTexture(Atlantis.Gameplay.Assets.TextureImportId? id, TextureImportKind kind,
        Action<Outcome<Atlantis.Gameplay.Assets.TextureImportStatus, Atlantis.Gameplay.Assets.ImportError>> done) =>
        Enqueue(session =>
        {
            var result = kind switch
            {
                TextureImportKind.Occlusion => session.Assets.OcclusionImportStatus(id),
                TextureImportKind.MetallicRoughness => session.Assets.MetallicRoughnessImportStatus(id),
                TextureImportKind.Normal => session.Assets.NormalMapImportStatus(id),
                _ => session.Assets.TextureImportStatus(id)
            };
            _post(() => done(result));
        });

    public void QueryMaterial(Atlantis.Gameplay.Assets.MaterialJobId? id,
        Action<Outcome<Atlantis.Gameplay.Assets.MaterialStatus, Atlantis.Gameplay.Assets.MaterialError>> done) =>
        Enqueue(session => { var result = session.Assets.MaterialStatus(id); _post(() => done(result)); });

    public void Start() => _thread.Start();

    public void SetViewportExtent(int width, int height)
    {
        lock (_lock)
        {
            if (_viewportExtent == (width, height)) return;
            _viewportExtent = (width, height);
            ++_viewportExtentRevision;
            InvalidateGuidesLocked();
        }
    }

    public void SetGuidesEnabled(bool enabled)
    {
        lock (_lock)
        {
            if (_guidesEnabled == enabled) return;
            _guidesEnabled = enabled;
            InvalidateGuidesLocked(clear: true);
        }
    }

    private void InvalidateGuidesLocked(bool clear = false)
    {
        if (_stopping || (!_guidesEnabled && !clear)) return;
        ++_guideRevision;
        if (_guideRefreshQueued) return;
        _guideRefreshQueued = true;
        Enqueue(RefreshGuideOverlay);
    }

    private bool GuideCurrentLocked(GuideIntent intent) => !_stopping && intent.Revision == _guideRevision &&
        intent.Selection == _requestedSelectionGeneration && intent.Extent == _viewportExtentRevision &&
        intent.Lifetime == MeshDocumentLifetime;

    public long SetCamera(EditorCamera camera, long inputTicks)
    {
        lock (_lock)
        {
            int count = _pendingCamera is { } pending ? pending.Count + 1 : 1;
            long first = _pendingCamera is { } p ? p.InputTicks : inputTicks;  // latency from the oldest coalesced input
            long intent = ++_cameraIntent;
            _pendingCamera = (camera.Copy(), first, count, intent);
            Monitor.Pulse(_lock);
            return intent;
        }
    }

    public long QueueCameraMotion(ViewportMotion motion)
    {
        lock (_lock)
        {
            _pendingCameraMotion.Add(motion);
            long intent = ++_cameraIntent;
            Monitor.Pulse(_lock);
            return intent;
        }
    }

    public long Navigate(IReadOnlyList<EntityGuid>? focus, IReadOnlyList<HierarchyRow> rows,
                         ulong selection, ViewDirection? direction)
    {
        var identities = new Dictionary<EntityGuid, object>();
        if (focus is not null)
        {
            foreach (HierarchyRow row in rows)
                if (focus.Contains(row.Node) && _hierarchyIncarnations.TryGetValue(row, out object? identity))
                    identities[row.Node] = identity;
        }
        lock (_lock)
        {
            long intent = ++_cameraIntent;
            long lifetime = MeshDocumentLifetime;
            _pendingCamera = null;
            _pendingCameraMotion.Clear();
            EntityGuid[]? targets = focus?.ToArray();
            Enqueue(session => Navigate(session, intent, lifetime, selection, targets, identities, direction));
            return intent;
        }
    }

    private static CameraPose NavigationPose(ViewportCamera camera) =>
        new(camera.WorldMatrix.ToArray(), new CameraLens(camera.FovYRadians, camera.NearZ, camera.FarZ));

    private void PublishNavigation(long intent, string? message)
    {
        var state = new ViewportNavigationState(intent, _navigationSupported, _confirmedEditorCamera?.Copy(),
            _navigationSceneCamera, !_navigationUncertain && (_confirmedEditorCamera is not null || _navigationSceneCamera is not null), message);
        if (message is not null) _log.Write("viewport navigation: " + message);
        _post(() =>
        {
            lock (_lock)
                if (intent != _cameraIntent || _stopping || _pendingCameraMotion.Count != 0) return;
            NavigationChanged?.Invoke(state);
        });
    }

    private void ReadSceneNavigation(RemoteSession session, long intent, string? success = null)
    {
        ViewportNavigationResult result = session.Control.ViewportNavigation(Array.Empty<EntityGuid>());
        _navigationSupported = result.Error?.Code != ViewportNavigationError.Unsupported;
        if (result.Value is ViewportNavigation value)
        {
            _navigationGeneration = value.SceneGeneration;
            _navigationSceneCamera = NavigationPose(value.SceneCamera);
            _navigationUncertain = false;
            PublishNavigation(intent, success);
        }
        else if (_navigationSupported)
        {
            _navigationSceneCamera = null;
            _navigationUncertain = true;
            PublishNavigation(intent, (success is null ? "" : "Override cleared. ") + "Scene camera projection unavailable: " + result.Error!.Code);
        }
        else PublishNavigation(intent, success);
    }

    private void Navigate(RemoteSession session, long intent, long lifetime, ulong selection, EntityGuid[]? targets,
                          Dictionary<EntityGuid, object> identities, ViewDirection? direction)
    {
        const string set = "control.setViewportCamera";
        bool focus = targets is not null;
        bool returning = !focus && direction is null;
        bool Current()
        {
            lock (_lock)
                return !_stopping && _cameraIntent == intent && !_dragActive && !_gizmoActive && _requestedGroup is null &&
                    (!focus || (lifetime == MeshDocumentLifetime && selection == _requestedSelectionGeneration &&
                        identities.All(pair => ReferenceEquals(pair.Value, _tree.IncarnationOf(pair.Key)))));
        }
        if (!Current())
        {
            PublishNavigation(intent, "Viewport navigation canceled because its context changed.");
            return;
        }
        if (!returning && !_navigationSupported)
        {
            PublishNavigation(intent, "Focus and direction views require an updated Runtime.");
            return;
        }

        EditorCamera? candidate = null;
        ulong generation = _navigationGeneration;
        ulong extentRevision = 0;
        if (!returning)
        {
            EditorCamera? source = _confirmedEditorCamera;
            (int Width, int Height) extent;
            ViewportNavigation? sample = null;
            if (focus || source is null)
            {
                ViewportNavigationResult result = session.Control.ViewportNavigation(targets ?? Array.Empty<EntityGuid>());
                if (result.Value is not ViewportNavigation value)
                {
                    PublishNavigation(intent, "Viewport navigation refused: " + result.Error!.Code +
                        (result.Error.Target is EntityGuid entity ? " for " + entity : ""));
                    return;
                }
                sample = value;
                generation = value.SceneGeneration;
                if (generation < _navigationGeneration)
                {
                    PublishNavigation(intent, "The Runtime scene changed. Invoke navigation again.");
                    return;
                }
                _navigationGeneration = generation;
                if (source is null)
                {
                    _navigationSceneCamera = NavigationPose(value.SceneCamera);
                    _navigationUncertain = false;
                    source = new EditorCamera();
                    source.SeedNavigation(_navigationSceneCamera);
                }
            }
            if (focus) PollDocument(session, pollApply: false);
            lock (_lock)
            {
                extent = _viewportExtent;
                extentRevision = _viewportExtentRevision;
            }
            if (focus && sample is not null && sample.Bounds.Count > 0)
            {
                Vector3 min = sample.Bounds[0].Min, max = sample.Bounds[0].Max;
                foreach (ViewportBounds item in sample.Bounds)
                {
                    min = Vector3.Min(min, item.Min);
                    max = Vector3.Max(max, item.Max);
                }
                candidate = NavigationFraming.Focus(source, min, max, extent.Width, extent.Height);
            }
            else if (direction is ViewDirection selected)
                candidate = NavigationFraming.Direction(source, selected, extent.Width, extent.Height);
            if (candidate is null)
            {
                PublishNavigation(intent, focus ? "Selection cannot fit the current viewport and lens."
                                                : "Direction is unavailable for the current viewport and camera.");
                return;
            }
        }
        ulong id;
        lock (_lock)
        {
            if (!Current() || (!returning && (extentRevision != _viewportExtentRevision || generation != _navigationGeneration)))
            {
                PublishNavigation(intent, "Viewport navigation canceled because its context changed.");
                return;
            }
            JsonValue cameraValue = candidate is null ? JsonValue.Null() : Codec.Encode(SceneReader.ToViewportCamera(candidate.Pose()));
            id = session.Send(set, JsonValue.NewObject().Set("camera", cameraValue));
        }
        if (CameraWriteRefusal(session, id) is string refusal)
        {
            PublishNavigation(intent, refusal);
            return;
        }
        _confirmedEditorCamera = candidate;
        if (returning)
        {
            if (_navigationSupported)
            {
                _navigationSceneCamera = null;
                _navigationUncertain = true;
                try { ReadSceneNavigation(session, intent, "Scene camera"); }
                catch (RemoteException error)
                {
                    throw new RemoteException(error.Error, "viewport override cleared; scene camera read interrupted. " + error.Message);
                }
            }
            else PublishNavigation(intent, "Scene camera");
            return;
        }
        SettleEditorProjection(session, generation);
        PublishNavigation(intent, _navigationUncertain ? "The Runtime scene changed. Invoke navigation again."
            : !Current() ? "The navigation context changed during the camera write."
            : focus ? "Selection framed" : direction + " view");
    }

    private static string? CameraWriteRefusal(RemoteSession session, ulong id)
    {
        JsonValue reply;
        try { reply = session.AwaitResponse(id); }
        catch (RemoteException error)
        {
            throw new RemoteException(error.Error, "viewport camera write outcome unknown. " + error.Message);
        }
        if (reply.Find("error") is { IsObject: true } rpcError && reply.Find("result") is null &&
            rpcError.Find("code") is { IsString: true } code)
            return "Viewport camera refused: " + code.AsString;
        if (reply.Find("result") is not { IsNull: true } || reply.Find("error") is not null)
            throw new RemoteException(RemoteError.ProtocolError, "viewport camera write outcome unknown");
        return null;
    }

    private void SettleEditorProjection(RemoteSession session, ulong generation)
    {
        _navigationUncertain = true;
        RuntimeStatus settled = session.Control.Status();
        _navigationGeneration = settled.SceneGeneration;
        _navigationUncertain = settled.SceneGeneration != generation;
    }

    private void SettleCameraMotion(RemoteSession session)
    {
        long intent;
        ulong id;
        EditorCamera camera;
        ulong generation = _navigationGeneration;
        lock (_lock)
        {
            if (_stopping || _pendingCameraMotion.Count == 0) return;
            intent = _cameraIntent;
            if (_navigationUncertain || (_confirmedEditorCamera is null && _navigationSceneCamera is null))
            {
                _pendingCameraMotion.Clear();
                PublishNavigation(intent, "Camera projection unavailable. Invoke Scene Camera or a direction again.");
                return;
            }
            camera = _confirmedEditorCamera?.Copy() ?? new EditorCamera();
            if (_confirmedEditorCamera is null) camera.SeedNavigation(_navigationSceneCamera!);
            foreach (var motion in _pendingCameraMotion) motion.Apply(camera);
            _pendingCameraMotion.Clear();
            CameraPose pose = camera.Pose();
            if (NavigationFraming.Projection(pose, _viewportExtent.Width, _viewportExtent.Height) is null)
            {
                PublishNavigation(intent, "Camera motion is unavailable for the current viewport and camera.");
                return;
            }
            // Consume and send under the same intent lock so a newer discrete command
            // can discard unsent motion without silently losing an intermediate sample.
            id = session.Send("control.setViewportCamera",
                JsonValue.NewObject().Set("camera", Codec.Encode(SceneReader.ToViewportCamera(pose))));
        }
        if (CameraWriteRefusal(session, id) is string refusal)
        {
            PublishNavigation(intent, refusal);
            return;
        }
        _confirmedEditorCamera = camera;
        SettleEditorProjection(session, generation);
        PublishNavigation(intent, _navigationUncertain ? "The Runtime scene changed. Invoke navigation again." : null);
    }

    public void Raycast(PickRay ray, long inputTicks, ulong request = 0, long? cameraIntent = null) => Enqueue(session =>
    {
        lock (_lock)
            if (cameraIntent is long intent && _cameraIntent != intent) return;
        long lifetime = MeshDocumentLifetime;
        ulong nodesBefore = _tree.NodeLifetimeGeneration;
        RaycastResult result = session.Control.Raycast(new Ray(ray.Origin, ray.Direction));
        if (request != 0 && _document is not null)
        {
            PollDocument(session, pollApply: false);
            if (lifetime != MeshDocumentLifetime || (result.Hit is RayHit hit &&
                (nodesBefore != _tree.NodeLifetimeGeneration || !_tree.Contains(hit.Entity)))) return;
        }
        double ms = Stopwatch.GetElapsedTime(inputTicks).TotalMilliseconds;
        _log.Write(string.Format(CultureInfo.InvariantCulture, "pick {0} input->answer {1:F1} ms",
                                 result.Error?.ToString() ?? result.Hit?.Entity.ToString() ?? "miss", ms));
        // Spec 0064's north star: the CLI's `runtime raycast` for the same ray names the same entity.
        _log.Write(string.Format(CultureInfo.InvariantCulture,
                                 "  same ray: atlantis --session \"{0}\" runtime raycast --origin {1:G9} {2:G9} {3:G9} --direction {4:G9} {5:G9} {6:G9}",
                                 SessionPath ?? "<session file>", ray.Origin.X, ray.Origin.Y, ray.Origin.Z, ray.Direction.X,
                                 ray.Direction.Y, ray.Direction.Z));
        var answer = new PickResult(result, ray, ms) { Request = request };
        _post(() => { if (lifetime == MeshDocumentLifetime) Picked?.Invoke(answer); });
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
        var orbit = new EditorCamera();
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

    /// <summary>Plan 0068 M2 (Spec 0068 R3): a node with a Transform under <paramref name="parent"/> (null: the root).</summary>
    public void CreateChild(EntityGuid? parent) => Edit(document =>
    {
        var (plan, node) = HierarchyCommands.CreateChild(document, _tree, parent);
        return (plan, node);
    });

    public bool CreateSceneObject(SceneObjectPreset preset, HierarchyRow? parent, long lifetime, ulong selection,
        Action<string> report, Action<EntityGuid> select, Action finished)
    {
        lock (_lock)
        {
            if (_sceneObjectQueued || !CanSettleSceneObject(lifetime) ||
                (!SceneObjectPresets.Named(preset) && selection != _requestedSelectionGeneration)) return false;
            object? parentIdentity = null;
            if (preset == SceneObjectPreset.EmptyChild &&
                (parent is null || !_hierarchyIncarnations.TryGetValue(parent, out parentIdentity))) return false;
            _sceneObjectQueued = true;
            Enqueue(session =>
            {
                try
                {
                    var (result, life, applyRevision) = SettleSceneObject(session, preset, lifetime, replay: null, forward: true,
                        selection, parent?.Node, parentIdentity);
                    bool recorded = false;
                    string notice = result.Notice;
                    if (result.State == BatchReplayState.Committed && life is not null && lifetime == MeshDocumentLifetime)
                    {
                        if (_history is { } history && _historyTarget is { } target)
                        {
                            history.Record(new EditTransaction("Create " + SceneObjectPresets.Label(preset), [], [life]),
                                target.Committed(Array.Empty<CommandFailure>()));
                            history.Update();
                            recorded = true;
                        }
                        else
                        {
                            _history?.DiscardRedo();
                            _apply?.RequireExplicitApply("creation history could not be verified");
                            notice += " Undo unavailable. Apply the current document explicitly.";
                        }
                    }
                    PostHistory(lifetime);
                    _log.Write(notice);
                    _post(() => { if (lifetime == MeshDocumentLifetime) report(notice); });
                    if (recorded && applyRevision is ulong revision)
                    {
                        string projection = ApplyVerifiedSceneObject(session, lifetime, revision, notice);
                        if (projection != notice)
                        {
                            _log.Write(projection);
                            _post(() => { if (lifetime == MeshDocumentLifetime) report(projection); });
                        }
                    }
                    PostApplyState(lifetime);
                    // History, document outcome and Apply feedback do not depend on this refresh succeeding.
                    PollDocument(session, pollApply: false, refreshInspector: false);
                    if (recorded && life is not null && lifetime == MeshDocumentLifetime &&
                        ReferenceEquals(life.CreatedIncarnation, _tree.IncarnationOf(life.State.Node)))
                    {
                        EntityGuid created = life.State.Node;
                        _post(() => { if (lifetime == MeshDocumentLifetime) select(created); });
                    }
                    PostInspector();
                }
                finally
                {
                    lock (_lock) _sceneObjectQueued = false;
                    _post(() => { if (lifetime == MeshDocumentLifetime) finished(); });
                }
            });
            return true;
        }
    }

    // Called with the intent lock immediately before sending, after worker-side observations.
    private bool CanSettleSceneObject(long lifetime) => !_stopping && lifetime == MeshDocumentLifetime &&
        _queuedGizmoBegins == 0 && !_gizmoActive && _gizmo?.Dragging != true && _requestedGroup is null && !_groupSettling &&
        !_batchSettling && !_reparentQueued && !_siblingMoveQueued && !_duplicateBatchQueued;

    private (BatchReplayResult Result, NodeLife? Life, ulong? ApplyRevision) SettleSceneObject(RemoteSession session,
        SceneObjectPreset preset, long lifetime, NodeLife? replay, bool forward, ulong selection = 0,
        EntityGuid? parent = null, object? parentIdentity = null)
    {
        bool initial = replay is null;
        string operation = (initial ? "Create " : forward ? "Redo creation of " : "Undo creation of ") + SceneObjectPresets.Label(preset);
        (BatchReplayResult, NodeLife?, ulong?) Refuse(string reason, bool drop = false) =>
            (new(drop ? BatchReplayState.Drop : BatchReplayState.Retry, $"{operation} refused: {reason}"), null, null);
        if (_document is not RemoteAuthoringDocument document || _apply is not { } apply)
            return Refuse("open an authoring document");

        bool SameDocument() => lifetime == MeshDocumentLifetime;
        bool ContextCurrent() => CanSettleSceneObject(lifetime) &&
            (!initial || SceneObjectPresets.Named(preset) ||
                (selection == _requestedSelectionGeneration &&
                    (preset == SceneObjectPreset.EmptyChild ? _selected == parent : _selectedNodes.Count == 0)));
        bool ParentCurrent() => !initial || parent is not EntityGuid childOf ||
            ReferenceEquals(parentIdentity, _tree.IncarnationOf(childOf));
        IReadOnlyList<Event>? Observe()
        {
            if (document.ObserveEvents() is not { } events) return null;
            PollDocument(session, events, pollApply: false, allowOrderRefresh: false,
                refreshInspector: false, refreshOverlay: false, refreshHierarchy: false);
            return SameDocument() ? events : null;
        }

        EntityGuid node = replay?.State.Node ?? new EntityGuid(Guid.NewGuid());
        bool sent = false, committed = false;
        (BatchReplayResult, NodeLife?, ulong?) Unverified()
        {
            if (SameDocument())
            {
                apply.RequireExplicitApply("creation transaction or replay could not be verified");
                if (committed && initial) _history?.DiscardRedo();
            }
            return (new(BatchReplayState.Drop, committed
                ? $"{operation} committed for {node}. History unavailable. Inspect and Apply the current document explicitly."
                : $"{operation} outcome unknown for {node}. Reconnect if needed, confirm the prior request ended, and inspect the document before retrying."), null, null);
        }

        try
        {
            if (!SameDocument() || Observe() is null) return Refuse("the document changed or could not be observed");
            if (document.Info() is not { } info || (SceneObjectPresets.Named(preset) && info.Provenance != Provenance.HandAuthored))
                return Refuse("open a hand-authored scene for named presets");
            if (forward && SceneObjectPresets.Mesh(preset) is AssetGuid mesh)
            {
                var detail = session.Assets.GetAsset(mesh);
                if (!detail.IsOk || !SceneObjectPresets.MatchesMesh(mesh, detail.Value))
                    return Refuse($"mesh {mesh} is unavailable in this Runtime catalog. Check the build and restart Runtime as needed");
            }
            if (ReadOrderRevision(document) is not ulong before || before == ulong.MaxValue || Observe() is null || !_tree.OrderSynchronized)
                return Refuse("the current document revision or order could not be verified");
            if (!ParentCurrent()) return Refuse("the selected Hierarchy row was replaced");

            var schema = document.Schema;
            NodeLife life;
            IReadOnlyList<Command> commands;
            if (initial)
            {
                if (SceneObjectPresets.Build(schema, preset, node, (ulong)_tree.Count, parent) is not { } plan)
                    return Refuse("the authoring schema cannot represent this preset");
                life = new NodeLife(plan.Expected, true) { Preset = preset };
                commands = plan.Edit.Commands;
            }
            else
            {
                life = replay!;
                var step = new EditTransaction(operation, [], [life]);
                var view = new DocumentView(document, _tree);
                if (forward && StructuralSteps.Check(step, forward, view) is not null)
                    return Refuse("the node, position or parent changed elsewhere", drop: true);
                if (!forward)
                {
                    if (!view.Exists(node)) return Refuse("the node no longer exists", drop: true);
                    if (view.ReadCompleteNodes([node]) is not [NodeState undoState])
                        return Refuse("the Undo target could not be read. Refresh before retrying");
                    if (!life.State.SameAs(undoState)) return Refuse("the node or its children changed elsewhere", drop: true);
                }
                if (!forward && preset == SceneObjectPreset.Camera)
                {
                    if (!document.ObserveActiveCamera(out EntityGuid? active)) return Refuse("the active camera could not be verified");
                    if (active == node) return Refuse("the created camera is now active", drop: true);
                }
                commands = StructuralSteps.Commands(step, forward, schema);
            }

            if (Observe() is null || ReadOrderRevision(document) != before)
                return Refuse("the document changed during preflight. Refresh before retrying");
            var encoded = JsonValue.NewArray();
            foreach (Command command in commands) encoded.Push(Codec.Encode(command));
            var parameters = JsonValue.NewObject().Set("commands", encoded);
            ulong request;
            lock (_lock)
            {
                if (!ContextCurrent() || !ParentCurrent()) return Refuse("the displayed edit context changed");
                sent = true;
                request = session.Send("authoring.submitTransaction", parameters);
            }

            if (Codec.DecodeTransactionTicket(session.AwaitResult(request)) is not TransactionTicket ticket ||
                ticket.First.Value == 0 || ticket.Count != (ulong)commands.Count ||
                ticket.First.Value > ulong.MaxValue - (ticket.Count - 1)) return Unverified();
            var failures = document.DrainFailures();
            if (failures.FirstOrDefault(failure => ticket.Contains(failure.Ticket)) is { Ticket.Value: not 0 } refused)
                return Refuse(refused.Error.ToString(), drop: !initial);
            committed = true;

            if (ReadOrderRevision(document) is not ulong observing || observing <= before || Observe() is not { } authored ||
                !_tree.OrderSynchronized) return Unverified();
            object? incarnation = _tree.IncarnationOf(node);
            bool verified = forward
                ? incarnation is not null && authored.Count(e => e is NodeCreated c && c.Node == node) == 1 &&
                    authored.Any(e => e is NodeCreated c && c.Node == node && c.Position == life.State.Position) &&
                    !authored.Any(e => e is NodeDestroyed d && d.Node == node) &&
                    new DocumentView(document, _tree).ReadCompleteNodes([node]) is [var actual] &&
                    actual.Position == life.State.Position && life.State.SameAs(actual)
                : incarnation is null && authored.Count(e => e is NodeDestroyed d && d.Node == node) == 1;
            if (Observe() is not { } closing || !verified || !_tree.OrderSynchronized ||
                closing.Any(e => (e is NodeCreated c && c.Node == node) || (e is NodeDestroyed d && d.Node == node)) ||
                !ReferenceEquals(incarnation, _tree.IncarnationOf(node)) || ReadOrderRevision(document) != observing)
                return Unverified();

            if (observing != before + 1)
                apply.RequireExplicitApply("document advanced during creation or replay");
            return (new(BatchReplayState.Committed, $"{operation} committed for {node}." +
                (observing == before + 1 ? "" : " Document advanced. Apply the current document explicitly.")),
                life with { CreatedIncarnation = incarnation }, observing == before + 1 ? observing : null);
        }
        catch (Exception error) when (error is GameplayException or IOException)
        {
            return sent ? Unverified() : Refuse(error.Message);
        }
    }

    /// <summary>Plan 0068 M4 (Spec 0068 R5): the Gizmo's mode, space and snapping (Ctrl held while dragging in E1).</summary>
    public void SetGizmo(GizmoMode mode, GizmoSpace space, bool snap) => Enqueue(session =>
    {
        _gizmoMode = mode;
        _gizmoSpace = space;
        _gizmoSnap = snap;
        SendOverlay(session);
    });

    /// <summary>Plan 0068 M4: the press grabbed <paramref name="axis"/> of the selected node's Gizmo.</summary>
    public void BeginGizmoDrag(GizmoAxis axis)
    {
        lock (_lock)
        {
            if (_stopping) return;
            ++_queuedGizmoBegins;
            InvalidateGuidesLocked();
            Enqueue(session =>
            {
                try
                {
                    PollDocument(session);
                    if (_apply?.CanPreview != true) return;
                    if (_gizmo is null || _selected is not EntityGuid node) return;
                    GizmoAvailability availability = _gizmo.Availability(node, _gizmoSpace);
                    GizmoMode mode = GizmoLayout.EffectiveMode(availability, _gizmoMode);
                    GizmoSpace space = GizmoLayout.EffectiveSpace(_transforms.WorldOf(_document!, _tree, node), _gizmoSpace);
                    if (!_gizmo.Begin(node, mode, space, axis, _gizmoSnap)) return;
                    _previewBatches = 0;
                    lock (_lock) _gizmoActive = true;
                    _log.Write("gizmo begin " + mode + " " + space + " " + axis + " on " + node + " (" + _gizmo.PreviewedDescendants + " descendants previewed)");
                }
                finally
                {
                    lock (_lock)
                    {
                        --_queuedGizmoBegins;
                        if (!_gizmoActive) InvalidateGuidesLocked();
                    }
                }
            });
        }
    }

    /// <summary>Plan 0068 M4: the drag amount from the press (units, a factor or radians); batches coalesce like the camera's.</summary>
    public void GizmoDragTo(float amount, long inputTicks)
    {
        lock (_lock)
        {
            if (_gizmoBlocked) return;
            int count = _pendingGizmo is { } pending ? pending.Count + 1 : 1;
            long first = _pendingGizmo is { } p ? p.Ticks : inputTicks;
            _pendingGizmo = (amount, first, count);
            Monitor.Pulse(_lock);
        }
    }

    /// <summary>Plan 0068 M4: the release -- the last batch is written, then the final matrix commits as one authoring transaction.</summary>
    public void EndGizmoDrag(float amount, long inputTicks)
    {
        long epoch;
        lock (_lock) epoch = _gizmoRecoveryEpoch;
        GizmoDragTo(amount, inputTicks);
        Enqueue(session =>
        {
            lock (_lock) { if (epoch != _gizmoRecoveryEpoch) return; _gizmoActive = false; }
            ReleaseGizmo(session);
            lock (_lock) InvalidateGuidesLocked();
        });
    }

    public long? BeginGroupGizmo(GroupGizmoSnapshot shown, GizmoAxis axis)
    {
        lock (_lock)
        {
            if (_stopping || _gizmoActive || _batchSettling || _duplicateBatchQueued || _groupSettling || _requestedGroup is not null ||
                shown.Lifetime != MeshDocumentLifetime || shown.Selection != _requestedSelectionGeneration) return null;
            var grab = new GroupGrab(++_nextGroupGrab, shown, axis);
            _requestedGroup = grab;
            InvalidateGuidesLocked();
            Enqueue(session =>
            {
                if (!GroupCurrent(grab)) return;
                GroupGizmo? fresh = ReadGroupPlacement(session);
                if (!GroupCurrent(grab) || fresh is null || !shown.Placement.SameLayout(fresh) ||
                    shown.Mode != _gizmoMode || shown.Snap != _gizmoSnap)
                {
                    CancelGroupGizmo(grab.Id);
                    GroupNotice(shown.Lifetime, "The group placement changed. Grab the refreshed Gizmo again.");
                    return;
                }
                _groupGrab = grab;
                SendOverlay(session);
            });
            return grab.Id;
        }
    }

    public void MoveGroupGizmo(long id, float amount)
    {
        lock (_lock)
        {
            if (_groupSettling || _requestedGroup is not { } grab || grab.Id != id) return;
            _pendingGroup = (grab, amount);
            Monitor.Pulse(_lock);
        }
    }

    public bool CancelGroupGizmo(long? id = null)
    {
        lock (_lock)
        {
            if (_requestedGroup is not { } grab || (id is long specific && grab.Id != specific)) return false;
            _requestedGroup = null;
            _pendingGroup = null;
            InvalidateGuidesLocked();
            Enqueue(session =>
            {
                RetireGroup(grab);
                SendOverlay(session, retiring: grab);
            });
            _post(() => GroupGrabEnded?.Invoke(grab.Id));
            return true;
        }
    }

    public void EndGroupGizmo(long id, float amount)
    {
        lock (_lock)
        {
            if (_groupSettling || _requestedGroup is not { } grab || grab.Id != id) return;
            _groupSettling = true;
            _pendingGroup = null;
            Enqueue(session =>
            {
                try { ReleaseGroup(session, grab, amount); }
                finally
                {
                    lock (_lock)
                    {
                        if (ReferenceEquals(_requestedGroup, grab)) _requestedGroup = null;
                        _groupSettling = false;
                        InvalidateGuidesLocked();
                    }
                    _post(() => GroupGrabEnded?.Invoke(grab.Id));
                }
            });
        }
    }

    private bool GroupCurrent(GroupGrab grab)
    {
        lock (_lock)
            return ReferenceEquals(grab, _requestedGroup) && !_stopping &&
                grab.View.Lifetime == MeshDocumentLifetime && grab.View.Selection == _requestedSelectionGeneration;
    }

    private void RetireGroup(GroupGrab grab)
    {
        if (!ReferenceEquals(grab, _groupGrab)) return;
        _groupGrab = null;
        _groupPreview = null;
    }

    private void GroupNotice(long lifetime, string notice) =>
        _post(() => { if (lifetime == MeshDocumentLifetime) EditNotice?.Invoke(notice); });

    private GroupGizmo? ReadGroupPlacement(RemoteSession session)
    {
        if (!_groupWritable || _selectedNodes.Count < 2 || _document is not RemoteAuthoringDocument document ||
            ReadOrderRevision(document) is not ulong before) return null;
        long lifetime = MeshDocumentLifetime;
        var basis = GroupGizmo.Read(document, _tree, _selectedNodes);
        var events = document.ObserveEvents();
        ulong? after = ReadOrderRevision(document);
        if (events is { Count: > 0 }) PollDocument(session, events, pollApply: false, refreshOverlay: false);
        return events is not null && before == after && lifetime == MeshDocumentLifetime && basis is not null &&
            basis.IsCurrent(_tree, preview: true, checkStamps: true) &&
            basis.Targets.All(target => target.IsCurrent(_tree, checkStamp: true)) ? basis : null;
    }

    private void PreviewGroup(RemoteSession session, GroupGrab grab, float amount)
    {
        if (!ReferenceEquals(_groupGrab, grab) || !GroupCurrent(grab)) return;
        var view = grab.View;
        _groupPreview = view.Placement.Solve(view.Mode, grab.Axis, amount, view.Snap);
        string notice = _groupPreview.Refusal ?? "Group Gizmo preview · Apply updates scene geometry.";
        _post(() =>
        {
            if (GroupCurrent(grab)) EditNotice?.Invoke(notice);
        });
        SendOverlay(session);
    }

    private void ReleaseGroup(RemoteSession session, GroupGrab grab, float amount)
    {
        if (!ReferenceEquals(_groupGrab, grab) || !GroupCurrent(grab)) return;
        RetireGroup(grab);
        SendOverlay(session, retiring: grab);
        if (!GroupCurrent(grab)) return;
        var view = grab.View;
        GroupGizmoSample result = view.Placement.Solve(view.Mode, grab.Axis, amount, view.Snap);
        if (result.Refusal is string refusal)
        {
            GroupNotice(view.Lifetime, refusal);
            return;
        }
        string label = view.Mode + " group";
        var edit = new BatchPropertyEdit(label, view.Placement.Targets, result.Writes, view.Placement);
        var settled = SettlePropertyBatch(session, edit, forward: true, view.Lifetime, view.Selection, grab);
        string notice = settled.Notice;
        bool recorded = false;
        if (settled.State == BatchReplayState.Committed && result.Writes.Count > 0 &&
            view.Lifetime == MeshDocumentLifetime && _history is { } history && _historyTarget is { } target)
        {
            history.Record(new EditTransaction(label, result.Writes, new EditEntry[] { edit }),
                target.Committed(Array.Empty<CommandFailure>()));
            history.Update();
            recorded = true;
        }
        PostHistory(view.Lifetime);
        if (recorded) notice = ApplyVerifiedEdit(view.Lifetime, notice, publishStatus: false);
        GroupNotice(view.Lifetime, notice);
        PostApplyState(view.Lifetime);
        PollDocument(session, pollApply: false, refreshInspector: false, refreshOverlay: false);
        PostInspector();
        SendOverlay(session);
    }

    public void CaptureAssetReference(EntityGuid node, TypeId component, FieldId field,
                                      Action<AssetReferenceTarget?, string?> done) => Enqueue(session =>
    {
        PollDocument(session);
        if (_referencePicker is not null || _document is null || _inspector is null || _selected != node || !_tree.Contains(node))
        { _post(() => done(null, "the target is no longer available")); return; }
        _inspector.Refresh(node);
        InspectorField? current = _inspector.Fields.FirstOrDefault(f => f.Component == component && f.Field.Id == field);
        var picker = current is null ? null : AssetReferencePicker.Capture(++_referenceRequest, node, current, _inspector.Fields);
        if (picker is null) { _post(() => done(null, "the reference cannot be read or edited")); return; }
        _referencePicker = picker;
        _post(() => done(picker.Target, null));
    });

    public void CancelAssetReference(long request) => Enqueue(_ =>
    {
        if (_referencePicker?.Target.Request == request)
        { _referencePicker.Invalidate("the choice was cancelled"); _referencePicker = null; }
    });

    public void ConfirmAssetReference(long request, long listRequest, AssetGuid candidate, Action settled) => Edit(document =>
    {
        if (_referencePicker?.Target.Request != request || _inspector is null)
            return (EditPlan.Refused("choose asset", "the choice expired"), null);
        _inspector.Refresh(_selected);
        return (_referencePicker.Confirm(_inspector, _selected, candidate, listRequest), null);
    }, () =>
    {
        if (_referencePicker?.Target.Request == request) _referencePicker = null;
        _post(settled);
    });

    public void RefreshInspector(Action? settled = null) => Enqueue(_ =>
    {
        PostInspector();
        if (settled is not null) _post(settled);
    });

    private void ReferenceInvalidated(string? before)
    {
        if (before is null && _referencePicker is { Refusal: string reason } picker)
            _post(() => AssetReferenceInvalidated?.Invoke(picker.Target.Request, reason));
    }

    /// <summary>Plan 0068 M3 (Spec 0068 R4): a field's text committed as one transaction (then an apply), or its refusal shown.</summary>
    public void CommitField(string path, string text) => Edit(document => (_inspector!.Commit(path, text), null));

    /// <summary>A name draft commits only while its selection and last observed value are current.</summary>
    public void CommitName(EntityGuid node, string before, string name) =>
        Edit(document => (_selected == node
            ? HierarchyCommands.Rename(_tree, node, before, name)
            : EditPlan.Refused("rename node", "the selection changed"), null));

    /// <summary>Plan 0068 M3: an optional field cleared.</summary>
    public void ClearField(string path) => Edit(document => (_inspector!.Clear(path), null));

    public void AddComponent(TypeId component) => Edit(document => (_inspector!.AddComponent(component), null));

    public void AddMesh(AssetGuid mesh, long lifetime, Action<EntityGuid?, string> settled) => Enqueue(session =>
    {
        void Report(EntityGuid? created, string notice) => _post(() => settled(created, notice));
        bool IsCurrent()
        {
            lock (_lock) return !_stopping && MeshDocumentLifetime == lifetime;
        }
        bool Observe(RemoteAuthoringDocument document)
        {
            if (!IsCurrent() || document.ObserveEvents() is not { } events || !IsCurrent()) return false;
            // Observe reopens now, but account for a settled local edit before polling recovery.
            PollDocument(session, events, pollApply: false);
            return IsCurrent();
        }

        EntityGuid? attempted = null;
        bool committed = false, recorded = false, applying = false;
        try
        {
            if (_document is not RemoteAuthoringDocument document || _apply is not { } apply ||
                document.Info()?.Provenance != Provenance.HandAuthored)
            {
                Report(null, "Add refused: open a hand-authored scene.");
                return;
            }
            var asset = session.Assets.GetAsset(mesh);
            if (!asset.IsOk || asset.Value is not { Type: AssetType.Mesh } details || details.Guid != mesh)
            {
                Report(null, "Add refused: the selected mesh is unavailable in this catalog.");
                return;
            }
            var schema = document.Schema;
            if (!Observe(document) || _gizmo?.Dragging == true)
            {
                Report(null, "Add refused: the document changed, could not be observed, or a gesture is active.");
                return;
            }
            var node = new EntityGuid(Guid.NewGuid());
            var plan = HierarchyCommands.AddMesh(schema, (ulong)_tree.Count, node, mesh);
            if (plan.Refusal is { } refusal) { Report(null, "Add refused: " + refusal); return; }
            if (!IsCurrent()) { Report(null, "Add refused: the session or document changed."); return; }

            attempted = node;
            var ticket = document.SubmitTransaction(plan.Commands);
            var failures = document.DrainFailures().Where(failure => ticket.Contains(failure.Ticket)).ToList();
            if (ticket.Count != (ulong)plan.Commands.Count)
            {
                Report(null, $"Add outcome unknown for {node}. Inspect the document before retrying.");
                return;
            }
            if (failures.Count != 0) { Report(null, "Add refused: " + failures[0].Error); return; }
            committed = true;
            if (!Observe(document)) { Report(null, $"Added {node}. Current document or history unavailable."); return; }
            var state = new DocumentView(document, _tree).ReadNode(node);
            var edit = HierarchyCommands.AddedMeshHistory(schema, state, mesh);
            if (!Observe(document)) { Report(null, $"Added {node}. Current document or history unavailable."); return; }
            if (edit is not null && _history is not null && _historyTarget is not null)
            {
                _history.Record(edit, _historyTarget.Committed(failures));
                _history.Update();
                PostHistory(lifetime);
                recorded = true;
            }
            var selected = _tree.Contains(node) ? node : (EntityGuid?)null;
            if (!IsCurrent()) { Report(null, $"Added {node}. Session or document changed before apply."); return; }
            applying = true;
            apply.AfterLocalEdit(true);
            Report(selected, recorded ? $"Added {node}." : $"Added {node}. Undo history unavailable.");
        }
        catch (Exception e) when (e is GameplayException or IOException)
        {
            if (applying)
            {
                _apply?.LostApplyAcceptance();
                string? applyText = _apply?.Text;
                if (applyText is not null && _apply?.Status is ApplyStatus status)
                    _post(() => { if (MeshDocumentLifetime == lifetime) ApplyStateChanged?.Invoke(status, applyText); });
            }
            Report(null, committed
                ? $"Added {attempted}. {(applying ? "Apply outcome unknown." : "History unavailable.")}"
                : attempted is not null ? $"Add outcome unknown for {attempted}. Inspect the document on reconnect before retrying."
                : "Add refused: " + e.Message);
            throw;
        }
        PostApplyState(lifetime);
    });

    public void RemoveComponent(TypeId component) => Edit(document => (_inspector!.RemoveComponent(component), null));

    public void DuplicateSubtree(EntityGuid source, long lifetime, Action<EntityGuid?, string> settled) => Enqueue(session =>
    {
        void Report(EntityGuid? node, string notice)
        {
            _log.Write(notice);
            _post(() => settled(node, notice));
        }
        bool IsCurrent()
        {
            lock (_lock) return !_stopping && MeshDocumentLifetime == lifetime;
        }
        bool CanSubmit()
        {
            lock (_lock) return !_stopping && MeshDocumentLifetime == lifetime && !_gizmoActive && _gizmo?.Dragging != true;
        }
        if (_document is not RemoteAuthoringDocument document || _apply is not { } apply)
        {
            Report(null, "Duplicate refused: the authoring document is unavailable.");
            return;
        }
        bool Observe()
        {
            if (!IsCurrent() || document.ObserveEvents() is not { } events) return false;
            PollDocument(session, events, pollApply: false);
            return IsCurrent();
        }
        ulong? Revision()
        {
            var status = document.ApplyStatus();
            return status.IsOk ? status.Value!.CurrentDocumentRevision : null;
        }

        EntityGuid? attempted = null;
        EntityGuid? verified = null;
        bool committed = false;
        bool applying = false;
        string Unknown() => $"Duplicate outcome unknown for {attempted}. Inspect the document before explicitly retrying.";
        try
        {
            if (!CanSubmit() || Revision() is not ulong before || !Observe() ||
                document.Info()?.Provenance != Provenance.HandAuthored)
            {
                Report(null, "Duplicate refused: open an accessible hand-authored scene and finish any active gesture.");
                return;
            }
            var (copy, refusal) = HierarchyCommands.DuplicateSubtree(document, _tree, source);
            if (copy is null)
            {
                Report(null, "Duplicate refused: " + refusal);
                return;
            }
            var commands = StructuralSteps.Commands(copy.Edit, forward: true, document.Schema);
            if (!Observe() || Revision() != before || !CanSubmit())
            {
                Report(null, "Duplicate refused: the document or gesture changed. Retry explicitly.");
                return;
            }

            attempted = copy.Root;
            TransactionTicket ticket = document.SubmitTransaction(commands);
            var drained = document.DrainFailures();
            if (ticket.First.Value == 0 || ticket.Count != (ulong)commands.Count ||
                ticket.First.Value > ulong.MaxValue - (ticket.Count - 1))
            {
                Report(null, Unknown());
                return;
            }
            var failures = drained.Where(failure => ticket.Contains(failure.Ticket)).ToList();
            if (failures.Count != 0)
            {
                Report(null, "Duplicate refused: " + failures[0].Error);
                return;
            }
            committed = true;
            if (IsCurrent())
            {
                _history?.DiscardRedo();
                PostHistory(lifetime);
            }

            if (Revision() is not ulong observing || !Observe())
            {
                Report(null, $"Duplicated {attempted}. Undo unavailable. Apply the current document explicitly.");
                return;
            }
            var copies = copy.Edit.Structure.OfType<NodeLife>().Select(entry => entry.State.Node).ToList();
            var observed = new DocumentView(document, _tree).ReadCompleteNodes(copies);
            if (!Observe() || Revision() != observing || !IsCurrent() || observed is null || !copy.Matches(observed) ||
                _history is not { } history || _historyTarget is not { } target)
            {
                Report(null, $"Duplicated {attempted}. Undo unavailable. Apply the current document explicitly.");
                return;
            }

            history.Record(copy.Edit, target.Committed(failures));
            history.Update();
            PostHistory(lifetime);
            verified = copy.Root;
            if (!IsCurrent())
            {
                Report(null, $"Duplicated {attempted}. Session or document changed before apply.");
                return;
            }
            if (observing <= before || observing - before != 1)
            {
                Report(verified, $"Duplicated {attempted}. Document advanced. Apply the current document explicitly.");
                return;
            }
            applying = true;
            apply.AfterLocalEdit(true);
            Report(verified, $"Duplicated {attempted}.");
        }
        catch (Exception e) when (e is GameplayException or IOException)
        {
            if (applying)
            {
                apply.LostApplyAcceptance();
                string text = apply.Text;
                _post(() => { if (MeshDocumentLifetime == lifetime) ApplyStateChanged?.Invoke(ApplyStatus.Unknown, text); });
            }
            Report(verified, committed
                ? $"Duplicated {attempted}. {(applying ? "Apply outcome unknown." : "Undo unavailable.")}"
                : attempted.HasValue ? Unknown() : "Duplicate refused: " + e.Message);
            throw;
        }
        PostApplyState(lifetime);
    });

    public bool DuplicateSubtrees(IReadOnlyList<HierarchyRow> rows, long lifetime, ulong selection,
        Action<IReadOnlyList<EntityGuid>?, string> settled)
    {
        lock (_lock)
        {
            if (_stopping || !_duplicateBatchWritable || lifetime != MeshDocumentLifetime ||
                selection != _requestedSelectionGeneration || _duplicateBatchQueued || _batchSettling ||
                _groupSettling || _requestedGroup is not null || _gizmoActive || _reparentQueued || _siblingMoveQueued ||
                rows.Count < 2 || rows.Count > RequireDuplicateBatchRevision.MaxNodes) return false;
            var targets = new Dictionary<EntityGuid, object>(rows.Count);
            foreach (HierarchyRow row in rows)
            {
                if (!_hierarchyIncarnations.TryGetValue(row, out object? identity) || !targets.TryAdd(row.Node, identity))
                    return false;
            }
            _duplicateBatchQueued = true;
            Enqueue(session =>
            {
                try
                {
                    var (result, edit) = SettleDuplicateBatch(session, null, forward: true, lifetime, targets, selection);
                    IReadOnlyList<EntityGuid>? copiedRoots = null;
                    if (result.State == BatchReplayState.Committed && edit is not null && lifetime == MeshDocumentLifetime &&
                        _history is { } history && _historyTarget is { } target)
                    {
                        history.Record(new EditTransaction("duplicate subtrees", Array.Empty<PropertyWrite>(), new EditEntry[] { edit }),
                            target.Committed(Array.Empty<CommandFailure>()));
                        history.Update();
                        copiedRoots = edit.Roots;
                    }
                    PostHistory(lifetime);
                    string notice = copiedRoots is not null
                        ? ApplyVerifiedEdit(lifetime, result.Notice, publishStatus: false) : result.Notice;
                    _post(() => { if (lifetime == MeshDocumentLifetime) settled(copiedRoots, notice); });
                    PostApplyState(lifetime);
                    PollDocument(session, pollApply: false);
                    PostInspector();
                }
                finally { lock (_lock) _duplicateBatchQueued = false; }
            });
            return true;
        }
    }

    private (BatchReplayResult Result, GuardedDuplicateEdit? Edit) SettleDuplicateBatch(RemoteSession session,
        GuardedDuplicateEdit? edit, bool forward, long lifetime, IReadOnlyDictionary<EntityGuid, object>? targets = null,
        ulong? selection = null)
    {
        bool initial = edit is null;
        if (_document is not RemoteAuthoringDocument document || !_duplicateBatchWritable)
            return (new(BatchReplayState.Retry, "Guarded duplicate is unavailable."), edit);
        bool IsCurrent() => !_stopping && lifetime == MeshDocumentLifetime && !_gizmoActive &&
            _gizmo?.Dragging != true && _requestedGroup is null &&
            (selection is null || selection == _requestedSelectionGeneration);
        bool TargetsMatch() => targets is null || targets.All(target =>
            ReferenceEquals(target.Value, _tree.IncarnationOf(target.Key)));
        IReadOnlyList<Event>? Observe()
        {
            if (document.ObserveEvents() is not { } events) return null;
            PollDocument(session, events, pollApply: false, allowOrderRefresh: false,
                refreshInspector: false, refreshOverlay: false, refreshHierarchy: false);
            return lifetime == MeshDocumentLifetime ? events : null;
        }
        bool ObserveOrder(out IReadOnlyList<Event>? events)
        {
            var nodes = document.ObserveNodes();
            events = Observe();
            if (nodes is null || events is null) return false;
            return _tree.OrderSynchronized && nodes.SequenceEqual(_tree.Order.Select(node =>
                new NodeSummary(node, _tree.ParentOf(node), _tree.NameOf(node)))) || _tree.ReconcileOrder(nodes);
        }

        string operation = initial ? "Duplicate" : forward ? "Redo duplicate" : "Undo duplicate";
        (BatchReplayResult, GuardedDuplicateEdit?) Retry(string reason) =>
            (new(BatchReplayState.Retry, $"{operation} refused: {reason}"), edit);
        (BatchReplayResult, GuardedDuplicateEdit?) Conflict(string reason) =>
            (new(BatchReplayState.Drop, $"{operation} refused: {reason}"), edit);
        bool sent = false, committed = false;
        (BatchReplayResult, GuardedDuplicateEdit?) Uncertain()
        {
            if (lifetime == MeshDocumentLifetime)
            {
                _apply?.RequireExplicitApply("duplicate transaction could not be verified");
                if (initial) _history?.DiscardRedo();
            }
            string subject = string.Join(", ", edit!.Roots);
            string notice = committed
                ? $"{operation} committed for {subject}. History unavailable. Inspect and Apply the current document explicitly."
                : $"{operation} outcome unknown for {subject}. Inspect the document and confirm the prior request ended before retrying.";
            return (new(BatchReplayState.Drop, notice), edit);
        }

        try
        {
            if (!IsCurrent() || ReadOrderRevision(document) is not ulong revision || revision == ulong.MaxValue ||
                !ObserveOrder(out _)) return Retry("the document or edit context is unavailable");
            if (!TargetsMatch()) return Conflict("a selected Hierarchy row was replaced");
            if (initial)
            {
                var planned = HierarchyCommands.DuplicateSubtrees(document, _tree, targets!.Keys.ToArray());
                edit = planned.Edit;
                if (edit is null) return Retry(planned.Refusal ?? "the subtree snapshot is incomplete");
            }
            bool? matches = initial ? true : edit!.Matches(_tree, document, present: !forward, checkIncarnations: !forward);
            EntityGuid? camera = null;
            if (!forward && !document.ObserveActiveCamera(out camera)) return Retry("the active camera could not be verified");
            if (Observe() is null || ReadOrderRevision(document) != revision || !IsCurrent())
                return Retry("the document or edit context changed. Refresh before retrying");
            if (!TargetsMatch()) return Conflict("a selected Hierarchy row was replaced");
            if (matches is null) return Retry("the full copy state could not be verified");
            if (matches == false || (camera is EntityGuid active && edit!.CopyIdentities.ContainsKey(active)))
                return Conflict("the copy, its parent, the active camera or the recorded append boundary changed");

            if (document.Info() is not { Provenance: Provenance.HandAuthored, SupportsGuardedDuplicateTransactions: true })
                return Retry("the open document does not support guarded duplicate transactions");
            var commands = edit!.Commands(forward, revision, document.Schema);
            var encoded = JsonValue.NewArray();
            foreach (Command command in commands) encoded.Push(Codec.Encode(command));
            var parameters = JsonValue.NewObject().Set("commands", encoded);
            ulong request;
            lock (_lock)
            {
                if (!IsCurrent() || !TargetsMatch()) return Retry("the displayed edit context changed");
                sent = true;
                request = session.Send("authoring.submitTransaction", parameters);
            }

            if (Codec.DecodeTransactionTicket(session.AwaitResult(request)) is not TransactionTicket ticket ||
                ticket.First.Value == 0 || ticket.Count != (ulong)commands.Count ||
                ticket.First.Value > ulong.MaxValue - (ticket.Count - 1))
                return Uncertain();
            var failures = document.DrainFailures();
            if (failures.FirstOrDefault(failure => ticket.Contains(failure.Ticket)) is { Ticket.Value: not 0 } refusal)
                return initial || refusal.Error == AuthoringError.DuplicateBatchStaleRevision
                    ? Retry(refusal.Error.ToString()) : Conflict(refusal.Error.ToString());
            committed = true;
            if (ReadOrderRevision(document) != revision + 1 || !ObserveOrder(out var authored))
                return Uncertain();
            bool verified = edit.Matches(_tree, document, present: forward, checkIncarnations: false) == true;
            var closingEvents = Observe();
            if (!verified || closingEvents is null || ReadOrderRevision(document) != revision + 1 ||
                !authored!.Concat(closingEvents).Where(e => e is not Applied)
                    .SequenceEqual(GuardedDuplicateEdit.ExpectedEvents(commands)))
                return Uncertain();
            if (forward) edit.CaptureCopyIdentities(_tree);
            string notice = initial ? $"Duplicated {edit.Copies.Count} nodes." : $"{operation}: {edit.Copies.Count} nodes.";
            return (new(BatchReplayState.Committed, notice), edit);
        }
        catch (Exception error) when (error is GameplayException or IOException)
        {
            return sent ? Uncertain() : Retry(error.Message);
        }
    }

    /// <summary>Plan 0068 M2: the node and its whole subtree, one transaction.</summary>
    public void DestroySubtree(EntityGuid node) => Edit(document => (HierarchyCommands.DestroySubtree(document, _tree, node), null));

    public ReparentSource? CaptureReparent(HierarchyRow row, long lifetime)
    {
        lock (_lock)
        {
            if (_stopping || lifetime != MeshDocumentLifetime || _gizmoActive || _reparentQueued || _siblingMoveQueued ||
                !_hierarchyIncarnations.TryGetValue(row, out object? incarnation)) return null;
            _reparentSource = new ReparentSource(row, incarnation, lifetime);
            return _reparentSource;
        }
    }

    public bool MoveSibling(HierarchyRow row, HierarchyRow sibling, SiblingDirection direction,
        long lifetime, Action<string> settled)
    {
        lock (_lock)
        {
            if (_stopping || lifetime != MeshDocumentLifetime || !_siblingOrderAvailable || _gizmoActive ||
                _reparentSource is not null || _siblingMoveQueued || row.Parent != sibling.Parent ||
                !_hierarchyIncarnations.TryGetValue(row, out object? nodeIdentity) ||
                !_hierarchyIncarnations.TryGetValue(sibling, out object? siblingIdentity)) return false;
            var order = new SiblingOrderEntry(row.Node, sibling.Node, row.Parent, nodeIdentity, siblingIdentity, direction);
            QueueSiblingOrder(order, lifetime, settled);
            return true;
        }
    }

    public bool SameHierarchyIncarnation(HierarchyRow first, HierarchyRow second) =>
        first.Node == second.Node && _hierarchyIncarnations.TryGetValue(first, out object? identity) &&
        _hierarchyIncarnations.TryGetValue(second, out object? other) && ReferenceEquals(identity, other);

    public bool MoveSiblingRelative(ReparentSource source, HierarchyRow anchor, IReadOnlyList<HierarchyRow> interval,
        SiblingPlacement placement, Action<string> settled)
    {
        lock (_lock)
        {
            if (_stopping || source.Lifetime != MeshDocumentLifetime || !_siblingMoveSupported || !_siblingOrderAvailable ||
                _gizmoActive || _reparentQueued || _siblingMoveQueued || !ReferenceEquals(source, _reparentSource))
                return false;
            var displayed = new List<(EntityGuid Node, object Identity)>(interval.Count);
            foreach (HierarchyRow row in interval)
            {
                if (row.Parent != source.Parent || !_hierarchyIncarnations.TryGetValue(row, out object? identity) ||
                    (row.Node == source.Node && !ReferenceEquals(identity, source.Incarnation))) return false;
                displayed.Add((row.Node, identity));
            }
            if (SiblingMoveEntry.Capture(source.Node, anchor.Node, source.Parent, placement, displayed) is not { } order)
                return false;
            _reparentSource = null;
            QueueSiblingOrder(order, source.Lifetime, settled);
            return true;
        }
    }

    private void QueueSiblingOrder(SiblingEditEntry order, long lifetime, Action<string> settled)
    {
        _siblingMoveQueued = true;
        Enqueue(session =>
        {
            string notice;
            try
            {
                var result = SettleSiblingOrder(session, order, forward: true, lifetime);
                notice = result.Notice;
                if (result.Verified && _history is { } history && _historyTarget is { } target)
                {
                    var edit = new EditTransaction("Move sibling", Array.Empty<PropertyWrite>(), new EditEntry[] { order });
                    history.Record(edit, target.Committed(Array.Empty<CommandFailure>()));
                    history.Update();
                    notice = ApplyVerifiedEdit(lifetime, notice);
                }
                PostHistory(lifetime);
            }
            finally
            {
                lock (_lock) _siblingMoveQueued = false;
            }
            _log.Write(notice);
            _post(() => settled(notice));
        });
    }

    private (bool Verified, string Notice) SettleSiblingOrder(RemoteSession session, SiblingEditEntry order,
        bool forward, long lifetime)
    {
        bool relative = order is SiblingMoveEntry;
        if (_document is not RemoteAuthoringDocument document || !(relative ? document.SupportsSiblingMove : document.SupportsSiblingOrder))
            return (false, "Sibling order is unavailable for this connection.");
        bool Current()
        {
            lock (_lock) return !_stopping && lifetime == MeshDocumentLifetime;
        }
        (Command Command, Event Delta)? Plan(ulong revision)
        {
            lock (_lock)
                return Current() && !_gizmoActive && _gizmo?.Dragging != true && !_reparentQueued && _tree.OrderSynchronized
                    ? order.Plan(_tree, forward, revision) : null;
        }
        bool Observe()
        {
            if (document.ObserveEvents() is not { } events) return false;
            PollDocument(session, events, pollApply: false);
            return Current();
        }
        bool sent = false, committed = false;
        string operation = relative ? "Sibling move" : "Sibling swap";
        string pair = $"{order.Node} and {order.Anchor}";
        string unknown = $"{operation} outcome unknown for {pair}. Refresh and inspect before retrying.";
        string unverified = $"{operation} committed for {pair}. Undo is unavailable for this edit. Inspect and Apply explicitly.";
        string stale = $"{operation} refused: the document, rows or sibling order changed. Refresh before retrying.";
        try
        {
            if (!Current() || ReadOrderRevision(document) is not ulong expected || expected == ulong.MaxValue ||
                !Observe() || Plan(expected) is null || document.Info()?.Provenance != Provenance.HandAuthored ||
                !Observe() || ReadOrderRevision(document) != expected || Plan(expected) is not { } planned)
                return (false, stale);

            var delta = planned.Delta;
            if (planned.Command is MoveSiblingRelative move) pair = $"{move.Node} and {move.Anchor}";
            unknown = $"{operation} outcome unknown for {pair}. Refresh and inspect before retrying.";
            unverified = $"{operation} committed for {pair}. Undo is unavailable for this edit. Inspect and Apply explicitly.";
            sent = true;
            var ticket = document.SubmitTransaction(new[] { planned.Command });
            var failures = document.DrainFailures();
            if (ticket.First.Value == 0 || ticket.Count != 1)
            {
                _apply?.RequireExplicitApplyAfterUnverifiedSwap();
                _history?.InvalidatePositional();
                Observe();
                return (false, unknown);
            }
            if (failures.FirstOrDefault(f => f.Ticket == ticket.First) is { Ticket.Value: not 0 } failure)
            {
                return (false, $"{operation} refused: {failure.Error}.");
            }
            committed = true;
            // Read after draining so a newer edit/reopen racing with observation cannot certify this commit.
            var observed = document.ObserveEvents();
            bool verified = observed is not null && observed.Where(IsSiblingOrderEvent).SequenceEqual(new[] { delta }) &&
                !observed.Any(e => e is DocumentReopened) && ReadOrderRevision(document) == expected + 1 &&
                Current() && Plan(expected)?.Delta == delta;
            if (observed is not null) PollDocument(session, observed, pollApply: false, localOrder: verified ? delta : null);
            if (!verified || !Current() || !_tree.OrderSynchronized ||
                !order.Verified(_tree, forward, delta))
            {
                if (Current())
                {
                    _apply?.RequireExplicitApplyAfterUnverifiedSwap();
                    _history?.DiscardRedo();
                    _history?.InvalidatePositional();
                }
                return (false, unverified);
            }
            return (true, relative ? $"Moved sibling {pair}." : $"Swapped siblings {pair}.");
        }
        catch (Exception e) when (e is GameplayException or IOException)
        {
            if (sent && Current())
            {
                _apply?.RequireExplicitApplyAfterUnverifiedSwap();
                _history?.InvalidatePositional();
                if (committed) _history?.DiscardRedo();
            }
            return (false, committed ? unverified : sent ? unknown : $"{operation} refused: {e.Message}");
        }
    }

    private string ApplyVerifiedEdit(long lifetime, string notice, bool publishStatus = true)
    {
        if (lifetime != MeshDocumentLifetime || _apply is not { } apply) return notice;
        try
        {
            apply.AfterLocalEdit(true);
            if (publishStatus) PostApplyState(lifetime);
            return notice;
        }
        catch (Exception e) when (e is GameplayException or IOException)
        {
            apply.LostApplyAcceptance();
            string status = apply.Text;
            _post(() => { if (lifetime == MeshDocumentLifetime) ApplyStateChanged?.Invoke(ApplyStatus.Unknown, status); });
            return notice + " Apply outcome unknown. Inspect the current document and projection before retrying.";
        }
    }

    private string ApplyVerifiedSceneObject(RemoteSession session, long lifetime, ulong expected, string notice)
    {
        if (lifetime != MeshDocumentLifetime || _document is not RemoteAuthoringDocument document ||
            _apply is not { } apply) return notice;
        // Document history has already settled. Before asking the projection controller to act,
        // detect any observable new revision/reopen, including changes while that history was published.
        try
        {
            if (document.ObserveEvents() is { } events)
            {
                PollDocument(session, events, pollApply: false, allowOrderRefresh: false,
                    refreshInspector: false, refreshOverlay: false, refreshHierarchy: false);
                if (lifetime != MeshDocumentLifetime) return notice;
                if (ReadOrderRevision(document) == expected)
                    return ApplyVerifiedEdit(lifetime, notice, publishStatus: false);
            }
        }
        catch (Exception error) when (error is GameplayException or IOException)
        {
            // No new Apply was sent by the revision check.
        }
        if (lifetime == MeshDocumentLifetime)
            apply.RequireExplicitApply("the creation or replay revision is no longer verified");
        return notice + " Verify and Apply the current document explicitly.";
    }

    private static ulong? ReadOrderRevision(RemoteAuthoringDocument document)
    {
        var status = document.ApplyStatus();
        return status.IsOk ? status.Value!.CurrentDocumentRevision : null;
    }

    public void CancelReparent(ReparentSource source)
    {
        lock (_lock)
        {
            if (ReferenceEquals(source, _reparentSource) && !_reparentQueued) _reparentSource = null;
        }
    }

    public bool Reparent(ReparentSource source, HierarchyRow? target, bool drag, Action<bool, string> settled)
    {
        lock (_lock)
        {
            object? targetIncarnation = null;
            if (_stopping || source.Lifetime != MeshDocumentLifetime || _gizmoActive ||
                !ReferenceEquals(source, _reparentSource) || _reparentQueued ||
                (target is not null && !_hierarchyIncarnations.TryGetValue(target, out targetIncarnation)))
                return false;
            _reparentQueued = true;
            Enqueue(session => SettleReparent(session, source, target?.Node, targetIncarnation, drag, settled));
            return true;
        }
    }

    private void SettleReparent(RemoteSession session, ReparentSource source, EntityGuid? parent, object? targetIncarnation,
        bool drag, Action<bool, string> settled)
    {
        long lifetime = source.Lifetime;
        void Report(bool verified, string notice)
        {
            lock (_lock)
            {
                _reparentQueued = false;
                _reparentSource = null;
            }
            _log.Write(notice);
            _post(() => settled(verified, notice));
        }
        bool IsCurrent()
        {
            lock (_lock) return !_stopping && lifetime == MeshDocumentLifetime;
        }
        bool CanSubmit()
        {
            lock (_lock)
                return !_stopping && lifetime == MeshDocumentLifetime && !_gizmoActive && _gizmo?.Dragging != true &&
                       ReferenceEquals(source, _reparentSource) &&
                       ReferenceEquals(source.Incarnation, _tree.IncarnationOf(source.Node)) &&
                       (parent is not EntityGuid target || ReferenceEquals(targetIncarnation, _tree.IncarnationOf(target)));
        }
        if (_document is not RemoteAuthoringDocument document || _apply is not { } apply)
        {
            Report(false, "Reparent refused: the authoring document is unavailable.");
            return;
        }
        bool Observe()
        {
            if (!IsCurrent() || document.ObserveEvents() is not { } events) return false;
            PollDocument(session, events, pollApply: false);
            return IsCurrent();
        }
        ulong? Revision()
        {
            var status = document.ApplyStatus();
            return status.IsOk ? status.Value!.CurrentDocumentRevision : null;
        }

        string destination = parent?.ToString() ?? "scene root";
        string known = $"Reparented {source.Node} under {destination}.";
        const string unavailable = "Undo unavailable for this move. Apply the current document explicitly.";
        string unknown = $"Reparent outcome unknown for {source.Node} under {destination}. Inspect the document before retrying.";
        bool sent = false, committed = false, applying = false;
        try
        {
            if (!CanSubmit() || Revision() is not ulong before || !Observe() || !CanSubmit() ||
                document.Info() is not { } info || (drag && info.Provenance != Provenance.HandAuthored))
            {
                Report(false, "Reparent refused: the document, source, destination or gesture is unavailable.");
                return;
            }
            var (edit, refusal) = HierarchyCommands.PlanReparent(document, _tree, source.Node, source.Parent, parent);
            if (edit is null)
            {
                Report(false, "Reparent refused: " + refusal);
                return;
            }
            var commands = StructuralSteps.Commands(edit, forward: true, document.Schema);
            if (!Observe() || Revision() != before || !CanSubmit())
            {
                Report(false, "Reparent refused: the document or gesture changed. Retry explicitly.");
                return;
            }

            sent = true;
            TransactionTicket ticket = document.SubmitTransaction(commands);
            var drained = document.DrainFailures();
            if (ticket.First.Value == 0 || ticket.Count != (ulong)commands.Count ||
                ticket.First.Value > ulong.MaxValue - (ticket.Count - 1))
            {
                Report(false, unknown);
                return;
            }
            var failures = drained.Where(failure => ticket.Contains(failure.Ticket)).ToList();
            if (failures.Count != 0)
            {
                Report(false, "Reparent refused: " + failures[0].Error);
                return;
            }
            committed = true;
            if (IsCurrent())
            {
                _history?.DiscardRedo();
                PostHistory(lifetime);
            }

            ulong? observing = Revision();
            if (!Observe() || observing is not ulong verifiedRevision || verifiedRevision <= before || verifiedRevision - before != 1 ||
                !_tree.Contains(source.Node) || _tree.ParentOf(source.Node) != parent)
            {
                Report(false, $"{known} {unavailable}");
                return;
            }

            var locals = document.GetProperties(edit.Writes.Select(write => write.Address).ToList());
            if (!Observe() || Revision() != verifiedRevision || !IsCurrent() ||
                locals.Count != edit.Writes.Count || !locals.Zip(edit.Writes).All(pair => pair.First == pair.Second.After) ||
                _history is not { } history || _historyTarget is not { } target)
            {
                Report(false, $"{known} {unavailable}");
                return;
            }
            history.Record(edit, target.Committed(failures));
            history.Update();
            PostHistory(lifetime);
            applying = true;
            apply.AfterLocalEdit(true);
            Report(true, known);
        }
        catch (Exception e) when (e is GameplayException or IOException)
        {
            if (applying)
            {
                apply.LostApplyAcceptance();
                string status = apply.Text;
                _post(() => { if (lifetime == MeshDocumentLifetime) ApplyStateChanged?.Invoke(ApplyStatus.Unknown, status); });
            }
            Report(applying, committed ? $"{known} {(applying ? "Apply outcome unknown." : unavailable)}"
                : sent ? unknown : "Reparent refused: " + e.Message);
            throw;
        }
        PostApplyState(lifetime);
    }

    /// <summary>
    /// Plan 0068 M2 (Spec 0068 R6): the overlay for the selection -- the node outlined, a translate Gizmo at its world
    /// position (from the document), and the light and camera markers.
    /// </summary>
    public void ShowSelection(EntityGuid? node)
    {
        ulong generation;
        lock (_lock) generation = ++_requestedSelectionGeneration;
        ShowSelection(node is EntityGuid guid ? new[] { guid } : Array.Empty<EntityGuid>(), generation);
    }

    public void ShowSelection(IReadOnlyList<EntityGuid> nodes, ulong generation)
    {
        lock (_lock)
        {
            _requestedSelectionGeneration = generation;
            if (_requestedGroup is { } group && group.View.Selection != generation) CancelGroupGizmo(group.Id);
            Enqueue(session =>
            {
                lock (_lock)
                {
                    if (_requestedSelectionGeneration != generation) return;
                }
                EntityGuid? node = nodes.Count == 1 ? nodes[0] : null;
                string? refusal = _referencePicker?.Refusal;
                _referencePicker?.SelectionChanged(node);
                ReferenceInvalidated(refusal);
                _selectionGeneration = generation;
                _selectedNodes = nodes;
                _selected = node;
                if (nodes.Count > 1 && _gizmo?.Dragging == true)
                {
                    _gizmo.Revert();
                    lock (_lock) _gizmoActive = false;
                }
                lock (_lock) InvalidateGuidesLocked();
                SendOverlay(session);
                PostInspector();
            });
        }
    }

    public bool CommitBatch(BatchInspectorSnapshot snapshot, CommonInspectorField field, PropertyValue value)
    {
        lock (_lock)
        {
            if (_stopping || _batchSettling || _duplicateBatchQueued || _requestedGroup is not null || _groupSettling || snapshot.Lifetime != MeshDocumentLifetime ||
                snapshot.SelectionGeneration != _requestedSelectionGeneration ||
                snapshot.Revision is null || !snapshot.Writable || !field.Field.Editable) return false;
            _batchSettling = true;
            Enqueue(session =>
            {
                try
                {
                    var writes = field.Targets.Where(target => target.Value is PropertyValue before && before != value)
                        .Select(target => new PropertyWrite(target.Address, target.Value!.Value, value)).ToArray();
                    var edit = new BatchPropertyEdit(field.Field.Path, field.Targets, writes);
                    var result = SettlePropertyBatch(session, edit, true, snapshot.Lifetime, snapshot.SelectionGeneration);
                    string notice = result.Notice;
                    bool recorded = false;
                    if (result.State == BatchReplayState.Committed && writes.Length != 0 &&
                        snapshot.Lifetime == MeshDocumentLifetime && _history is { } history && _historyTarget is { } target)
                    {
                        var changed = field.Targets.Where(item => writes.Any(write => write.Address == item.Address)).ToArray();
                        var record = edit with { Targets = changed };
                        history.Record(new EditTransaction("Set " + field.Field.Path, writes, new EditEntry[] { record }),
                            target.Committed(Array.Empty<CommandFailure>()));
                        history.Update();
                        recorded = true;
                    }
                    PostHistory(snapshot.Lifetime);
                    if (recorded) notice = ApplyVerifiedEdit(snapshot.Lifetime, notice, publishStatus: false);
                    _post(() => { if (snapshot.Lifetime == MeshDocumentLifetime) EditNotice?.Invoke(notice); });
                    PostApplyState(snapshot.Lifetime);
                    PollDocument(session, pollApply: false, refreshInspector: false);
                    PostInspector();
                }
                finally { lock (_lock) _batchSettling = false; }
            });
            return true;
        }
    }

    private BatchReplayResult SettlePropertyBatch(RemoteSession session, BatchPropertyEdit edit, bool forward,
        long lifetime, ulong? selection = null, GroupGrab? grab = null)
    {
        bool group = edit.Group is not null;
        if (_document is not RemoteAuthoringDocument document || !(group ? _groupWritable : _batchWritable))
            return new(BatchReplayState.Retry, "Guarded editing is unavailable.");
        bool IsCurrent() => !_stopping && lifetime == MeshDocumentLifetime &&
            (selection is null || selection == _requestedSelectionGeneration) &&
            (grab is null || ReferenceEquals(grab, _requestedGroup));
        bool Current()
        {
            lock (_lock) return IsCurrent();
        }
        bool SameDocument() => lifetime == MeshDocumentLifetime;
        bool Observe()
        {
            if (document.ObserveEvents() is not { } events) return false;
            PollDocument(session, events, pollApply: false, refreshInspector: false);
            return true;
        }
        bool TargetsCurrent(bool stamps) =>
            (edit.Group?.IsCurrent(_tree, preview: grab is not null, checkStamps: stamps) ?? true) &&
            edit.Targets.All(target => target.IsCurrent(_tree, stamps));
        var addresses = edit.Targets.Select(target => target.Address).ToArray();
        var writes = edit.Writes;
        var expectedValues = edit.Targets.Select(target => target.Value);
        if (selection is null)
        {
            var valuesToCompare = writes.ToDictionary(write => write.Address, write => forward ? write.Before : write.After);
            expectedValues = edit.Targets.Select(target => valuesToCompare.TryGetValue(target.Address, out var expected)
                ? expected : target.Value);
        }
        var changed = writes.Select(write => new SetProperty(write.Address, forward ? write.After : write.Before)).ToArray();
        string subject = edit.Field + " on " + string.Join(", ", edit.Group?.Roots ?? addresses.Select(address => address.Node));
        BatchReplayResult Conflict() => new(BatchReplayState.Drop, subject + " changed elsewhere. Refresh and review the values.");
        BatchReplayResult Stale() => new(BatchReplayState.Retry,
            (selection is null ? "Document revision changed." : "Selection or document revision changed.") +
            " Refresh before retrying " + edit.Field + ".");
        bool sent = false, committed = false;
        BatchReplayResult Uncertain()
        {
            if (SameDocument())
            {
                _apply?.RequireExplicitApply("property batch could not be verified");
                if (selection is not null) _history?.DiscardRedo();
            }
            return new(BatchReplayState.Drop, committed
                ? "Committed " + subject + ". History unavailable. Refresh, inspect and Apply explicitly."
                : "Edit outcome unknown for " + subject + ". Refresh and inspect before retrying.");
        }
        try
        {
            if (!Current() || ReadOrderRevision(document) is not ulong revision || revision == ulong.MaxValue || !Observe())
                return Stale();
            if (!TargetsCurrent(selection is not null)) return Conflict();
            IReadOnlyList<PropertyValue?> prior = document.GetProperties(addresses);
            if (!Observe() || !Current()) return Stale();
            if (!TargetsCurrent(selection is not null)) return Conflict();
            if (ReadOrderRevision(document) != revision) return Stale();
            if (!prior.SequenceEqual(expectedValues)) return Conflict();
            if (changed.Length == 0) return new(BatchReplayState.Committed, "The selected values are unchanged.");

            if (!Current()) return Stale();
            DocumentInfo? info = document.Info();
            if (!(group ? info?.SupportsGuardedTransformTransactions == true : info?.SupportsGuardedPropertyTransactions == true))
                return new(BatchReplayState.Retry, "Guarded editing is unavailable.");
            Command guard = group ? new RequireTransformBatchRevision(revision) : new RequireDocumentRevision(revision);
            var commands = JsonValue.NewArray().Push(Codec.Encode(guard));
            foreach (var command in changed) commands.Push(Codec.Encode(command));
            var parameters = JsonValue.NewObject().Set("commands", commands);
            ulong request;
            // Selection changes share this lock with sending. Capability discovery and receipt waits stay outside it.
            lock (_lock)
            {
                if (!IsCurrent()) return Stale();
                sent = true;
                request = session.Send("authoring.submitTransaction", parameters);
            }
            if (Codec.DecodeTransactionTicket(session.AwaitResult(request)) is not TransactionTicket ticket)
            {
                session.Dispose();
                throw new RemoteException(RemoteError.ProtocolError, "a malformed authoring.submitTransaction result");
            }
            var failures = document.DrainFailures();
            if (ticket.First.Value == 0 || ticket.Count != (ulong)changed.Length + 1) return Uncertain();
            if (failures.FirstOrDefault(failure => ticket.Contains(failure.Ticket)) is { Ticket.Value: not 0 } refusal)
                return new(refusal.Error is AuthoringError.UnknownNode or AuthoringError.ComponentMissing
                    ? BatchReplayState.Drop : BatchReplayState.Retry, "Batch refused: " + refusal.Error);
            committed = true;
            ulong? before = ReadOrderRevision(document);
            IReadOnlyList<PropertyValue?> values = document.GetProperties(changed.Select(write => write.Address).ToArray());
            IReadOnlyList<Event>? observed = document.ObserveEvents();
            ulong? after = ReadOrderRevision(document);
            // Consume events for identity checks. The caller publishes history before any view refresh.
            if (observed is not null)
                PollDocument(session, observed, pollApply: false, allowOrderRefresh: false,
                    refreshInspector: false, refreshHierarchy: false);
            if (!SameDocument() || before != revision + 1 || after != before || observed is null ||
                !observed.OfType<PropertyChanged>().SequenceEqual(changed.Select(write => new PropertyChanged(write.Address, write.Value))) ||
                !values.SequenceEqual(changed.Select(write => (PropertyValue?)write.Value)) ||
                !TargetsCurrent(stamps: false))
                return Uncertain();
            return new(BatchReplayState.Committed, (selection is not null ? "Set " : forward ? "Redid " : "Undid ") + subject + ".");
        }
        catch (Exception error) when (error is GameplayException or IOException)
        {
            return sent ? Uncertain() : new(BatchReplayState.Retry, "Batch refused before submission: " + error.Message);
        }
    }

    // The release: one authoring transaction from the final matrix, then an apply; a refusal reverts the preview.
    private void ReleaseGizmo(RemoteSession session)
    {
        if (_document is null || _apply is null || _gizmo is null || !_apply.CanPreview) return;
        EntityGuid? node = _selected;
        EditPlan plan = _gizmo.Release();
        if (plan.Refusal is string reason)
        {
            if (reason != "unchanged")
            {
                _log.Write("gizmo " + plan.Label + " refused: " + reason);
                _post(() => EditNotice?.Invoke(plan.Label + " refused: " + reason));
            }
            SendOverlay(session);
            return;
        }
        long start = Stopwatch.GetTimestamp();
        EditRecorder? recorder = EditRecorder.Begin(_document, _tree, plan);
        IReadOnlyList<CommandFailure> failures = _apply.Commit(plan.Commands);
        double ms = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        if (failures.Count > 0)
        {
            if (node is EntityGuid n) _gizmo.RevertRelease(n);
            string why = plan.Label + " refused by the document: " + failures[0].Error;
            _log.Write("gizmo " + why);
            _post(() => EditNotice?.Invoke(why));
            PollDocument(session);
            SendOverlay(session);
            return;
        }
        _log.Write(string.Format(CultureInfo.InvariantCulture, "gizmo {0}: committed in {1:F1} ms after {2} preview batch(es)", plan.Label, ms, _previewBatches));
        _commitTicks = start;
        RecordEdit(recorder, failures);
        PollDocument(session);
        SendOverlay(session);
        PostInspector();
        _post(() => EditNotice?.Invoke(plan.Label));
    }

    // One batch of the drag: the node (and its descendants) previewed on the Runtime World, the overlay following.
    private void PreviewGizmo(RemoteSession session, float amount, long inputTicks, int coalesced)
    {
        if (_gizmo is null || _apply?.CanPreview != true) return;
        _gizmo.Preview(amount);
        ++_previewBatches;
        SendOverlay(session);
        _log.Write(string.Format(CultureInfo.InvariantCulture, "gizmo batch coalesced {0} input->write {1:F1} ms", coalesced,
                                 Stopwatch.GetElapsedTime(inputTicks).TotalMilliseconds));
    }

    // One edit: planned on this thread against the tree as of now, submitted as one transaction, then an apply.
    private void Edit(Func<IAuthoringDocument, (EditPlan Plan, EntityGuid? Created)> plan, Action? settled = null) => Enqueue(session =>
    {
        try
        {
            if (_document is null || _apply is null)
            {
                _post(() => EditNotice?.Invoke("no authoring document"));
                return;
            }
            PollDocument(session);
            var (edit, created) = plan(_document);
            if (edit.Refusal is string reason)
            {
                _log.Write("edit " + edit.Label + " refused: " + reason);
                _post(() => EditNotice?.Invoke(edit.Label + " refused: " + reason));
                PostInspector();  // the field reverts to the document's value
                return;
            }
            long start = Stopwatch.GetTimestamp();
            EditRecorder? recorder = EditRecorder.Begin(_document, _tree, edit);
            IReadOnlyList<CommandFailure> failures = _apply.Commit(edit.Commands);
            double ms = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            if (failures.Count == 0) _commitTicks = start;
            if (failures.Count > 0)
            {
                string why = edit.Label + " refused by the document: " + failures[0].Error;
                _log.Write("edit " + why);
                _post(() => EditNotice?.Invoke(why));
                PollDocument(session);
                PostInspector();
                return;
            }
            _log.Write(string.Format(CultureInfo.InvariantCulture, "edit {0}: {1} command(s) committed in {2:F1} ms", edit.Label,
                                     edit.Commands.Count, ms));
            RecordEdit(recorder, failures);
            PollDocument(session);
            PostInspector();
            _post(() => EditNotice?.Invoke(edit.Label));
            if (created is EntityGuid node) _post(() => NodeCreatedByEditor?.Invoke(node));
        }
        finally { settled?.Invoke(); }
    });

    // A committed edit joins the history (settled at once: the commit was applied, or refused, when submitted).
    private void RecordEdit(EditRecorder? recorder, IReadOnlyList<CommandFailure> failures)
    {
        if (_history is null || _historyTarget is null || _document is null || failures.Count > 0) return;
        if (recorder is null)
        {
            _log.Write("history: this edit cannot be recorded, so it is not undoable");
            return;
        }
        _history.Record(recorder.Finish(_document.Schema), _historyTarget.Committed(failures));
        _history.Update();
        PostHistory();
    }

    private void PostHistory(long? lifetime = null)
    {
        if (_history is null) return;
        var snapshot = new HistorySnapshot(_history.CanUndo, _history.CanRedo, _history.UndoLabel, _history.RedoLabel, _history.Notice);
        _post(() => { if (lifetime is null || lifetime == MeshDocumentLifetime) HistoryChanged?.Invoke(snapshot); });
    }

    /// <summary>Plan 0068 M5 (Spec 0068 R8): undo the last edit -- one transaction, then an apply -- or say why not.</summary>
    public void Undo() => Enqueue(session => HistoryStep(session, undo: true));

    public void Redo() => Enqueue(session => HistoryStep(session, undo: false));

    private void HistoryStep(RemoteSession session, bool undo)
    {
        lock (_lock)
            if (_history is null || _requestedGroup is not null || _groupSettling) return;
        PollDocument(session);
        long start = Stopwatch.GetTimestamp();
        long lifetime = MeshDocumentLifetime;
        bool siblingReplay = false, reordered = false, batchReplay = false;
        (bool Verified, string Notice) Replay(SiblingEditEntry order, bool forward)
        {
            siblingReplay = true;
            var settlement = SettleSiblingOrder(session, order, forward, lifetime);
            reordered = settlement.Verified;
            return settlement;
        }
        BatchReplayResult ReplayBatch(BatchPropertyEdit edit, bool forward)
        {
            batchReplay = true;
            var settlement = SettlePropertyBatch(session, edit, forward, lifetime);
            reordered = settlement.State == BatchReplayState.Committed;
            return settlement;
        }
        BatchReplayResult ReplayDuplicate(GuardedDuplicateEdit edit, bool forward)
        {
            batchReplay = true;
            var (settlement, _) = SettleDuplicateBatch(session, edit, forward, lifetime);
            reordered = settlement.State == BatchReplayState.Committed;
            return settlement;
        }
        ulong? presetApplyRevision = null;
        BatchReplayResult ReplayPreset(NodeLife edit, bool forward)
        {
            batchReplay = true;
            var (settlement, _, applyRevision) = SettleSceneObject(session, edit.Preset!.Value, lifetime, edit, forward);
            presetApplyRevision = applyRevision;
            return settlement;
        }
        HistoryResult result = undo ? _history.Undo(Replay, ReplayBatch, ReplayDuplicate, ReplayPreset) :
            _history.Redo(Replay, ReplayBatch, ReplayDuplicate, ReplayPreset);
        siblingReplay |= batchReplay;
        _history.Update();
        _log.Write(string.Format(CultureInfo.InvariantCulture, "history {0}: {1} ({2}) in {3:F1} ms", undo ? "undo" : "redo", result,
                                 _history.Notice, Stopwatch.GetElapsedTime(start).TotalMilliseconds));
        if (result == HistoryResult.Submitted) _commitTicks = start;
        if (batchReplay) PostHistory(lifetime);
        string notice = presetApplyRevision is ulong revision
            ? ApplyVerifiedSceneObject(session, lifetime, revision, _history.Notice)
            : reordered ? ApplyVerifiedEdit(lifetime, _history.Notice, publishStatus: !batchReplay) : _history.Notice;
        if (siblingReplay)
        {
            if (!batchReplay) PostHistory(lifetime);
            _post(() => { if (lifetime == MeshDocumentLifetime) EditNotice?.Invoke(notice); });
        }
        if (batchReplay) PostApplyState(lifetime);
        PollDocument(session);
        if (!siblingReplay) PostHistory();
        PostInspector();
        SendOverlay(session);
        if (!siblingReplay) _post(() => EditNotice?.Invoke(notice));
    }

    /// <summary>Plan 0068 M6 (Spec 0068 R9): the Runtime's transport, through RuntimeControl. Play resumes the frames
    /// (and a held apply goes with them); Pause holds both; Step runs one frame while paused.</summary>
    public void Play() => Enqueue(session =>
    {
        session.Control.Resume();
        PollStatus(session);
    });

    public void Pause() => Enqueue(session =>
    {
        session.Control.Pause();
        PollStatus(session);
    });

    public void StepFrame() => Enqueue(session =>
    {
        StepResult stepped = session.Control.Step(new StepRequest(1));
        _log.Write(stepped.Report is FrameReport report
                       ? string.Format(CultureInfo.InvariantCulture, "step: frame {0}", report.Frame)
                       : "step: " + stepped.Error);
        PollStatus(session);
        PollDocument(session);  // a held apply went with the frame: take its Applied now
    });

    /// <summary>Writes the document to its source; an imported scene is refused with the reason (Spec 0068 R9).</summary>
    public void RetryApply() => Enqueue(session =>
    {
        PollDocument(session);
        _apply?.Retry();
        _applyControls = null; // settle the button even when a retry is refused identically
        PostApplyState();
    });

    public void Save() => Enqueue(session =>
    {
        if (_document is null) return;
        long start = Stopwatch.GetTimestamp();
        SaveError? error = _document.Save();
        double ms = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        DocumentInfo? info = _document.Info();
        if (error is SaveError refused)
        {
            // The refusal states what an imported scene's edits are for (Spec 0068, the risk "Imported scenes").
            string reason = refused == SaveError.ImportedSceneNotSavable
                ? "an imported scene is not savable; its edits are for trying things out"
                : refused.ToString();
            _log.Write(string.Format(CultureInfo.InvariantCulture, "save refused ({0}) in {1:F1} ms", refused, ms));
            _post(() => EditNotice?.Invoke("save refused (" + refused + "): " + reason));
            return;
        }
        _log.Write(string.Format(CultureInfo.InvariantCulture, "saved {0} in {1:F1} ms", info?.SourcePath, ms));
        _post(() => EditNotice?.Invoke("saved " + info?.SourcePath));
    });

    public void SaveAs(string name, long lifetime, Action<string, SavedSceneReceipt?> settled) => Enqueue(session =>
    {
        void Report(string text, SavedSceneReceipt? receipt = null) => _post(() => settled(text, receipt));
        bool IsCurrent()
        {
            lock (_lock) return !_stopping && MeshDocumentLifetime == lifetime;
        }
        bool sent = false;
        try
        {
            if (!IsCurrent() || _document is not RemoteAuthoringDocument document ||
                document.ObserveEvents() is not { } events)
            { Report("Save As refused: the document or session is unavailable."); return; }
            PollDocument(session, events, pollApply: false);
            if (!IsCurrent()) { Report("Save As refused: the document or session changed."); return; }
            sent = true;
            var result = session.Authoring.SaveAs(name);
            if (result.IsOk && result.Value is { } copy)
                Report($"Saved copy {copy.Source} ({copy.Scene}), revision {copy.DocumentRevision}. Configure a separate build with ATLANTIS_SAVED_SCENE={name}, then launch --scene saved_scene.",
                    new(name, copy.Scene, copy.Source));
            else
                Report("Save As refused: " + result.Error);
        }
        catch (GameplayException e)
        {
            Report(sent ? $"Save As outcome unknown for {name}. Inspect the destination and confirm settlement before retrying."
                : "Save As did not send a request: " + e.Message);
            throw;
        }
    });

    public void CreateScene(string name, Action<string, SavedSceneReceipt?> settled) => Enqueue(session =>
    {
        void Report(string text, SavedSceneReceipt? receipt = null) => _post(() => settled(text, receipt));
        lock (_lock)
        {
            if (_stopping)
            {
                Report("New Scene was not sent: the session changed.");
                return;
            }
        }
        try
        {
            var result = session.Authoring.CreateScene(name);
            if (result.IsOk && result.Value is { } created)
            {
                Report($"Created {name}: {created.Source} ({created.Scene}). The new scene is not open. " +
                    $"In this checkout, configure a separate build with -DATLANTIS_SAVED_SCENE={name} and build atlantis_runtime. " +
                    "Keep this Editor open. Start a second Atlantis.Editor --runtime <new-build-runtime> --scene saved_scene.",
                    new(name, created.Scene, created.Source));
                return;
            }
            string next = result.Error switch
            {
                CreateSceneError.Busy => "Retry explicitly after contention clears.",
                CreateSceneError.NameConflict => "Choose another name or inspect the existing unit.",
                CreateSceneError.InvalidName => "Use 1–48 lowercase letters, digits or underscores, starting with a letter.",
                CreateSceneError.Unavailable or CreateSceneError.Unsupported => "Use a supported development Runtime with --allow-scene-save-as.",
                CreateSceneError.NoAuthoring => "This Runtime has no authoring session.",
                CreateSceneError.UnsafeTarget => "Check the development checkout and its ordinary assets/scenes/saved directory.",
                _ => "Check the Runtime log and destination before trying again.",
            };
            Report($"New Scene refused ({result.Error}). {next}");
        }
        catch (GameplayException)
        {
            Report($"New Scene outcome unknown for {name} at {Codec.SaveAsSource(name)}. Inspect scene.scene.txt and " +
                "catalog-source.txt. If the unit is absent, confirm the previous request can no longer commit before retrying.");
            throw;
        }
    });

    // Opens the Runtime's authoring document (Spec 0065 R2) and posts the first tree.
    private void OpenDocument(RemoteSession session)
    {
        try
        {
            // A read-only probe precedes automatic open and editable UI. An old
            // Runtime fails through the existing protocol rules without a document write.
            var readiness = session.Authoring.ApplyStatus();
            if (!readiness.IsOk) throw new InvalidOperationException("authoring apply tracking unavailable");
            if (session.Authoring.Document() is null && session.Authoring.Open() is OpenError error)
            {
                _log.Write("authoring: open refused: " + error);
                _post(() => EditNotice?.Invoke("the authoring document cannot be opened: " + error));
                return;
            }
            var document = new RemoteAuthoringDocument(session.Authoring, session);
            long start = Stopwatch.GetTimestamp();
            _tree.Rebuild(document);
            _document = document;
            _apply = new ApplyController(document);
            _apply.Initialize(readiness.Value!);
            _inspector = new InspectorModel(document);
            _historyTarget = new AuthoringEditTarget(document, _tree, _apply);
            _history = new EditHistory(_historyTarget);
            _gizmo = new GizmoDragController(document, _tree, _transforms, new RuntimeWorldPreview(() => _previewSession));
            _previewSession = session;
            PostApplyState();
            DocumentInfo? info = document.Info();
            _batchWritable = info is { Provenance: Provenance.HandAuthored, SupportsGuardedPropertyTransactions: true };
            _groupWritable = info is { Provenance: Provenance.HandAuthored, SupportsGuardedTransformTransactions: true };
            _duplicateBatchWritable = info is { Provenance: Provenance.HandAuthored, SupportsGuardedDuplicateTransactions: true };
            PostHierarchy(session);
            _log.Write(string.Format(CultureInfo.InvariantCulture, "authoring: {0} nodes, the Hierarchy built in {1:F0} ms ({2})",
                                     _tree.Count, Stopwatch.GetElapsedTime(start).TotalMilliseconds, info?.Provenance));
            _post(() => DocumentOpened?.Invoke(info));
        }
        catch (Exception e) when (e is GameplayException or InvalidOperationException or IOException)
        {
            _log.Write("authoring: " + e.Message);
            _post(() => EditNotice?.Invoke("no authoring document: " + e.Message));
        }
    }

    // Drains the authoring events into the tree, the transform cache and the apply controller; posts what changed.
    private static bool IsSiblingOrderEvent(Event e) => e is NodeOrderChanged or NodeMovedInSiblingOrder;

    private void PollDocument(RemoteSession session, IReadOnlyList<Event>? observed = null, bool pollApply = true,
        Event? localOrder = null, bool allowOrderRefresh = true, bool refreshInspector = true, bool refreshOverlay = true,
        bool refreshHierarchy = true)
    {
        _lastPoll = Stopwatch.GetTimestamp();
        if (_document is null || _apply is null) return;
        IReadOnlyList<Event> events = observed ?? _document.DrainEvents();
        if (events.Count > 0)
        {
            lock (_lock)
            {
                if (_guidesEnabled && events.Any(GuideInputChanged)) InvalidateGuidesLocked();
            }
        }
        var destroyed = new List<EntityGuid>();
        bool historyChanged = false;
        foreach (Event e in events)
        {
            string? refusal = _referencePicker?.Refusal;
            _referencePicker?.Observe(e);
            ReferenceInvalidated(refusal);
            _tree.Apply(e, _document);
            if (IsSiblingOrderEvent(e) && (e != localOrder || !_tree.OrderSynchronized))
                historyChanged |= _history?.InvalidatePositional() == true;
            _transforms.Apply(e);
            _apply.Apply(e);
            if (e is NodeDestroyed gone) destroyed.Add(gone.Node);
            if (e is PropertyChanged or ParentChanged or DocumentReopened) _transformsChanged = true;
        }
        if (events.Any(e => e is DocumentReopened))
        {
            long lifetime = Interlocked.Increment(ref _meshDocumentLifetime);
            if (_selectedNodes.Count > 1)
            {
                _selectedNodes = Array.Empty<EntityGuid>();
                _selected = null;
            }
            _batchWritable = false;
            _groupWritable = false;
            _duplicateBatchWritable = false;
            CancelGroupGizmo();
            _post(() => MeshDocumentReopened?.Invoke(lifetime));
            _history?.Clear();  // ADR-0135 D4: the history clears on DocumentReopened, not on an apply
            PostHistory();
            DocumentInfo? reopened = _document.Info();
            _batchWritable = reopened is { Provenance: Provenance.HandAuthored, SupportsGuardedPropertyTransactions: true };
            _groupWritable = reopened is { Provenance: Provenance.HandAuthored, SupportsGuardedTransformTransactions: true };
            _duplicateBatchWritable = reopened is { Provenance: Provenance.HandAuthored, SupportsGuardedDuplicateTransactions: true };
        }
        else if (historyChanged) PostHistory();
        if (destroyed.Count > 0)
        {
            _selectedNodes = _selectedNodes.Where(node => !destroyed.Contains(node)).ToArray();
            _selected = _selectedNodes.Count == 1 ? _selectedNodes[0] : null;
            _post(() => NodesDestroyed?.Invoke(destroyed));
        }
        // Another client's change to the selected node (or the node going) refreshes the Inspector.
        if (refreshInspector && (destroyed.Count > 0 || events.Any(e => e switch
        {
            PropertyChanged changed => _selectedNodes.Contains(changed.Address.Node),
            ComponentAdded added => _selectedNodes.Contains(added.Node),
            ComponentRemoved removed => _selectedNodes.Contains(removed.Node),
            NodeNameChanged named => _selected == named.Node,
            DocumentReopened => true,
            _ => false
        })))
        {
            PostInspector();
        }
        if (allowOrderRefresh && !_tree.OrderSynchronized && _document is RemoteAuthoringDocument document)
        {
            long lifetime = MeshDocumentLifetime;
            ulong? before = ReadOrderRevision(document);
            IReadOnlyList<NodeSummary> snapshot = document.ListNodes();
            var queued = document.ObserveEvents();
            ulong? after = ReadOrderRevision(document);
            if (queued is not null && queued.Count > 0)
                PollDocument(session, queued, pollApply: false, allowOrderRefresh: false);
            if (queued is not null && before is not null && before == after && lifetime == MeshDocumentLifetime &&
                _tree.ReconcileOrder(snapshot))
                _log.Write("Hierarchy order refreshed at document revision " + before);
        }
        if (_groupGrab is { } group && (!group.View.Placement.IsCurrent(_tree, preview: true, checkStamps: true) ||
            group.View.Placement.Targets.Any(target => !target.IsCurrent(_tree, checkStamp: true))))
        {
            CancelGroupGizmo(group.Id);
            RetireGroup(group);
            GroupNotice(group.View.Lifetime, "The group placement changed elsewhere. Grab the refreshed Gizmo again.");
        }
        if (refreshHierarchy && (_tree.Generation != _postedGeneration || _transformsChanged))
            PostHierarchy(session, refreshOverlay);
        if (!pollApply) return;
        _apply.Poll();
        if (_apply.Status is ApplyStatus.Failed or ApplyStatus.Superseded or ApplyStatus.Unknown or ApplyStatus.NeedsApply) _commitTicks = null;
        if (_commitTicks is long committed && _apply.Status == ApplyStatus.Applied)
        {
            double applied = Stopwatch.GetElapsedTime(committed).TotalMilliseconds;
            _commitTicks = null;
            _log.Write(string.Format(CultureInfo.InvariantCulture, "commit->applied {0:F1} ms", applied));
            _post(() => CommitApplied?.Invoke(applied, _applyText));
        }
        PostApplyState();
    }

    // Plan 0068 M6 (Spec 0068 R9): the Runtime's play state, from control.status. A pause also holds applies, which
    // the apply controller then shows as pending; while it plays, an apply replaces the scene -- gameplay clients'
    // runtime-side changes included (ADR-0131 D7) -- which the apply state names: the wire carries no client list, so
    // playing is that note's signal.
    private void PollStatus(RemoteSession session)
    {
        RuntimeStatus status = session.Control.Status();
        if (status.SceneGeneration > _navigationGeneration)
        {
            _navigationGeneration = status.SceneGeneration;
            if (_navigationSupported && _confirmedEditorCamera is null)
            {
                long intent;
                lock (_lock) intent = _cameraIntent;
                _navigationSceneCamera = null;
                _navigationUncertain = true;
                ReadSceneNavigation(session, intent);
            }
        }
        _apply?.SetPaused(status.Paused);
        bool playing = !status.Paused;
        if (playing == _playing)
        {
            PostApplyState();
            return;
        }
        _playing = playing;
        _log.Write(playing ? "runtime: playing" : "runtime: paused");
        _post(() => PlayStateChanged?.Invoke(playing));
        PostApplyState();
    }

    // The apply state line: the controller's text, with the play note appended while the Runtime plays.
    private const string PlayNote = " · playing: an apply replaces the scene, gameplay clients' changes included";

    private void PostApplyState(long? lifetime = null)
    {
        if (_apply is null) return;
        bool canApply = _apply.CanApply, canPreview = _apply.CanPreview;
        bool controlsChanged = _applyControls != (canApply, canPreview);
        if (controlsChanged)
        {
            lock (_lock)
            {
                if (!canPreview && !_gizmoBlocked)
                {
                    if (_gizmoActive) InvalidateGuidesLocked();
                    ++_gizmoRecoveryEpoch; _pendingGizmo = null; _gizmoActive = false; _gizmo?.Abandon();
                }
                _gizmoBlocked = !canPreview;
            }
            _applyControls = (canApply, canPreview);
            _post(() => { if (lifetime is null || lifetime == MeshDocumentLifetime) ApplyControlsChanged?.Invoke(canApply, canPreview); });
        }
        string text = _apply.Text + (_playing ? PlayNote : "");
        ApplyStatus status = _apply.Status;
        if (text != _applyText || status != _postedApplyStatus)
        {
            _postedApplyStatus = status;
            _applyText = text;
            _log.Write("apply: " + text);
            _post(() => { if (lifetime is null || lifetime == MeshDocumentLifetime) ApplyStateChanged?.Invoke(status, text); });
        }
        if (controlsChanged && _previewSession is RemoteSession session) SendOverlay(session);
    }

    // Re-reads the selected node's components and fields (on this thread) and posts them.
    private void PostInspector()
    {
        if (_inspector is null || _document is null) return;
        if (_selectedNodes.Count > 1 && _document is RemoteAuthoringDocument remote)
        {
            ulong generation = _selectionGeneration;
            long lifetime = MeshDocumentLifetime;
            ulong? before = ReadOrderRevision(remote);
            var fields = _inspector.ReadCommon(_selectedNodes, _tree);
            bool stable = before is not null && ReadOrderRevision(remote) == before && fields is not null;
            var view = new BatchInspectorSnapshot(generation, lifetime, stable ? before : null, _selectedNodes.Count,
                stable && _batchWritable, stable ? fields! : Array.Empty<CommonInspectorField>(),
                stable ? _batchWritable ? null : "Batch editing is unavailable for this document." : "Common properties are unavailable. Refresh to retry.");
            _post(() => { if (lifetime == MeshDocumentLifetime) BatchInspectorChanged?.Invoke(view); });
            return;
        }
        EntityGuid? subject = _selected is EntityGuid s && _tree.Contains(s) ? s : null;
        _inspector.Refresh(subject);
        IReadOnlyList<SchemaType> schema = _document.Schema;
        var snapshot = new InspectorSnapshot(subject, _inspector.Fields,
                                             _inspector.Components.Select(c => (c, SceneSchema.NameOf(schema, c))).ToList(),
                                             _inspector.Addable().Select(t => (t.Id, t.ShortName)).ToList(),
                                             subject is EntityGuid named ? _tree.NameOf(named) : "");
        _post(() => InspectorChanged?.Invoke(snapshot));
    }

    private void PostHierarchy(RemoteSession session, bool refreshOverlay = true)
    {
        if (_document is null) return;
        _siblingOrderAvailable = _tree.OrderSynchronized &&
            _document is RemoteAuthoringDocument { SupportsSiblingOrder: true };
        _siblingMoveSupported = _document is RemoteAuthoringDocument { SupportsSiblingMove: true };
        _postedGeneration = _tree.Generation;
        _transformsChanged = false;
        IReadOnlyList<HierarchyRow> rows = SceneMarkers.Rows(_tree, _document.Schema);
        foreach (HierarchyRow row in rows)
            _hierarchyIncarnations.Add(row, _tree.IncarnationOf(row.Node)!);
        _markers = SceneMarkers.Of(_document, _tree, _transforms);
        IReadOnlyList<Marker> markers = _markers;
        _post(() => HierarchyChanged?.Invoke(rows, markers));
        if (refreshOverlay) SendOverlay(session);
    }

    private bool GuideInputChanged(Event e)
    {
        bool OnChain(EntityGuid node) => node == _selected || _guideChain.Contains(node);
        return e switch
        {
            DocumentReopened => true,
            NodeCreated n => OnChain(n.Node),
            NodeDestroyed n => OnChain(n.Node),
            ParentChanged n => OnChain(n.Node),
            ComponentAdded n => OnChain(n.Node),
            ComponentRemoved n => OnChain(n.Node),
            PropertyChanged n => OnChain(n.Address.Node),
            _ => false
        };
    }

    private void RefreshGuideOverlay(RemoteSession session)
    {
        GuideIntent intent;
        (int Width, int Height) extent;
        bool eligible;
        lock (_lock)
        {
            _guideRefreshQueued = false;
            extent = _viewportExtent;
            intent = new(_guideRevision, _selectionGeneration, _viewportExtentRevision, MeshDocumentLifetime);
            eligible = _guidesEnabled && _queuedGizmoBegins == 0 && !_gizmoActive &&
                _requestedGroup is null && !_groupSettling && _gizmo?.Dragging != true;
        }

        var value = ViewportGuides.Result.Empty;
        _guideChain = Array.Empty<EntityGuid>();
        if (eligible && extent.Width > 0 && extent.Height > 0 && _selected is EntityGuid node &&
            _selectedNodes.Count == 1 && _document is RemoteAuthoringDocument document &&
            _tree.IncarnationOf(node) is object incarnation)
        {
            _guideChain = ViewportGuides.Chain(_tree, node, out bool complete);
            if (ReadOrderRevision(document) is ulong before)
            {
                var start = document.ObserveEvents();
                if (start is { Count: > 0 })
                    PollDocument(session, start, pollApply: false, allowOrderRefresh: false, refreshInspector: false,
                        refreshOverlay: false, refreshHierarchy: false);
                lock (_lock) if (!GuideCurrentLocked(intent)) return;
                if (start is not null)
                {
                    value = ViewportGuides.Read(document, node, _guideChain, complete, extent.Width, extent.Height);
                    var end = document.ObserveEvents();
                    ulong? after = ReadOrderRevision(document);
                    if (end is { Count: > 0 })
                        PollDocument(session, end, pollApply: false, allowOrderRefresh: false, refreshInspector: false,
                            refreshOverlay: false, refreshHierarchy: false);
                    if (end is null || after != before || !ReferenceEquals(incarnation, _tree.IncarnationOf(node)))
                        value = new([], "Guide inputs changed or could not be verified.");
                }
                else value = new([], "Guide inputs could not be verified.");
            }
            else value = new([], "Guide revision could not be verified.");
        }

        lock (_lock)
        {
            if (!GuideCurrentLocked(intent)) return;
            _guides = (intent, value);
        }
        SendOverlay(session);
    }

    // Admit the send under the intent lock, then allow UI input while the shared reply is pending.
    private bool SendGuideOverlay(RemoteSession session, ViewportOverlay? overlay, GuideIntent intent, out ControlError? refusal)
    {
        var parameters = JsonValue.NewObject().Set("overlay", overlay is null ? JsonValue.Null() : Codec.Encode(overlay));
        ulong request;
        refusal = null;
        lock (_lock)
        {
            if (!GuideCurrentLocked(intent)) return false;
            request = session.Send("control.setViewportOverlay", parameters);
        }
        var result = session.AwaitResult(request);
        if (result.Find("ok") is { IsNull: true }) return true;
        if (result.Find("err") is JsonValue error && Codec.DecodeControlError(error) is ControlError typed)
        {
            refusal = typed;
            return true;
        }
        throw new RemoteException(RemoteError.ProtocolError, "Malformed guide overlay reply.");
    }

    private void SendOverlay(RemoteSession session, GroupGrab? retiring = null)
    {
        OverlayGroup? gizmo = null;
        GizmoSnapshot snapshot = new(null, null, null);
        ulong selection = _selectionGeneration;
        long lifetime = MeshDocumentLifetime;
        if (_document is not null && _gizmo is not null && _apply?.CanPreview == true && _selected is EntityGuid node && _tree.Contains(node))
        {
            float[] world = _gizmo.Dragging && _gizmo.LatestWorld is { } latest ? latest : _transforms.WorldOf(_document, _tree, node);
            GizmoAvailability availability = _gizmo.Availability(node, _gizmoSpace);
            GizmoMode mode = GizmoLayout.EffectiveMode(availability, _gizmoMode);
            GizmoSpace space = GizmoLayout.EffectiveSpace(world, _gizmoSpace);
            // While dragging, the axes stay where the drag began (the overlay's handles do not swing with the previewed rotation).
            Vector3[] directions = GizmoLayout.Directions(_gizmo.Dragging ? _transforms.WorldOf(_document, _tree, node) : world, mode, space);
            gizmo = GizmoOverlay.ModeGizmo(new Vector3(world[12], world[13], world[14]), mode, directions, _gizmo.Axis is GizmoAxis axis ? (int)axis : null);
            snapshot = new GizmoSnapshot(node, world, availability);
        }
        GroupGizmoSample? preview = null;
        if (_selectedNodes.Count > 1 && _groupWritable)
        {
            GroupGrab? grab = _groupGrab is { } active && GroupCurrent(active) ? active : null;
            GroupGizmo? placement = grab?.View.Placement ?? ReadGroupPlacement(session);
            if (placement is not null && lifetime == MeshDocumentLifetime)
            {
                var shown = grab?.View ?? new GroupGizmoSnapshot(lifetime, selection, _gizmoMode, _gizmoSnap, placement);
                gizmo = GizmoOverlay.GroupHandle(placement.Pivot, shown.Mode, grab?.Axis);
                preview = grab is not null ? _groupPreview : null;
                snapshot = new GizmoSnapshot(null, null, null, shown);
            }
        }
        ViewportOverlay overlay = _selectedNodes.Count > 1
            ? GizmoOverlay.ForMultiselection(_selectedNodes, _markers, gizmo, preview)
            : GizmoOverlay.ForSelection(_selected, gizmo, _markers);
        var guides = _guides;
        lock (_lock)
        {
            if (guides is { } proposed && !GuideCurrentLocked(proposed.Intent)) guides = null;
        }
        if (guides is { } guide && guide.Value.Groups.Count > 0)
        {
            if (!SendGuideOverlay(session, ViewportGuides.Compose(overlay, guide.Value), guide.Intent, out ControlError? refusal))
                return;
            if (refusal is ControlError error)
            {
                string notice = "Guide overlay refused: " + error;
                _log.Write(notice);
                if (!SendGuideOverlay(session, overlay, guide.Intent, out ControlError? fallback)) return;
                if (fallback is not null && !SendGuideOverlay(session, null, guide.Intent, out _)) return;
                PostGuideFeedback(guide.Intent, notice);
            }
            else PostGuideFeedback(guide.Intent, guide.Value.Unavailable);
        }
        else if (session.Control.SetViewportOverlay(overlay) is ControlError refused)
        {
            _log.Write("overlay refused: " + refused);
            // A failed placement read can leave no Group snapshot while retiring guides.
            if (retiring is not null || snapshot.Group is not null)
            {
                CancelGroupGizmo(retiring?.Id);
                snapshot = new GizmoSnapshot(null, null, null);
                GroupNotice(lifetime, "The group Gizmo overlay was refused.");
                if (retiring is not null ||
                    session.Control.SetViewportOverlay(GizmoOverlay.ForMultiselection(_selectedNodes, _markers)) is not null)
                    session.Control.SetViewportOverlay(null);
            }
        }
        else if (guides is { } empty) PostGuideFeedback(empty.Intent, empty.Value.Unavailable);
        _post(() =>
        {
            bool current;
            lock (_lock) current = lifetime == MeshDocumentLifetime && selection == _requestedSelectionGeneration;
            if (current) GizmoChanged?.Invoke(snapshot);
        });
    }

    private void PostGuideFeedback(GuideIntent intent, string? message) => _post(() =>
    {
        bool current;
        lock (_lock) current = GuideCurrentLocked(intent);
        if (current) GuideFeedback?.Invoke(message);
    });

    public void Measure(int frames) => Enqueue(session =>
    {
        FrameTimes times = FrameSampler.Measure(session.Control, frames, _capturePath);
        _log.Write("frametimes " + times.ToJson());
        _post(() => Measured?.Invoke(times));
    });

    public void Stop()
    {
        lock (_lock)
        {
            _stopping = true;
            Monitor.Pulse(_lock);
        }
    }

    public void Dispose()
    {
        Stop();
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
        RemoteSession? session = null;
        try
        {
            session = RemoteSession.Connect(_info);
            var world = new GameplayWorld(session);
            _world = world;
            SceneSnapshot snapshot = SceneReader.Read(world);
            _navigationSceneCamera = snapshot.ScenePose;
            _log.Write("connected: port " + _info.Port + ", " + snapshot.Entities.Count + " entities");
            _post(() => Connected?.Invoke(snapshot));
            ReadSceneNavigation(session, 0);
            OpenDocument(session);
        }
        catch (Exception e)
        {
            session?.Dispose();
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
            (EditorCamera Camera, long InputTicks, int Count, long Intent)? camera = null;
            (float Dx, float Dy, long InputTicks, int Count)? drag = null;
            (float Amount, long Ticks, int Count)? gizmoBatch = null;
            (GroupGrab Grab, float Amount)? groupSample = null;
            bool interacting;  // a pointer interaction is using the wire: its latency wins over the status poll
            lock (_lock)
            {
                // A pending drag offset waits for its BeginDrag (queued work) to have run.
                while (!_stopping && _work.Count == 0 && _pendingCamera is null && _pendingCameraMotion.Count == 0 &&
                       !(_pendingDrag is not null && _dragActive) &&
                       !(_pendingGizmo is not null && _gizmoActive) && _pendingGroup is null)
                {
                    if (!Monitor.Wait(_lock, TimeSpan.FromMilliseconds(PollMilliseconds))) break;  // Plan 0068 M2: time to poll
                }
                interacting = _dragActive || _gizmoActive || _requestedGroup is not null;
                if (_stopping) return;
                if (_pendingCameraMotion.Count != 0)
                    work = SettleCameraMotion;
                else if (_pendingCamera is not null)
                {
                    camera = _pendingCamera;
                    _pendingCamera = null;
                }
                else if (_pendingDrag is not null && _dragActive)
                {
                    drag = _pendingDrag;
                    _pendingDrag = null;
                }
                else if (_pendingGizmo is not null && _gizmoActive)
                {
                    gizmoBatch = _pendingGizmo;
                    _pendingGizmo = null;
                }
                else if (_work.Count > 0)
                {
                    work = _work.Dequeue();
                }
                else if (_pendingGroup is not null)
                {
                    groupSample = _pendingGroup;
                    _pendingGroup = null;
                }
            }
            if (groupSample is { } sample) PreviewGroup(session, sample.Grab, sample.Amount);
            if (gizmoBatch is { } batch) PreviewGizmo(session, batch.Amount, batch.Ticks, batch.Count);
            if (drag is { } offset)
            {
                WriteDrag(session, offset.Dx, offset.Dy, offset.InputTicks, offset.Count);
                bool more;
                lock (_lock) more = _pendingDrag is not null;
                if (!more) CloseUnseenWrite(session);  // nothing follows to date it: ask once
            }
            if (camera is { } update)
            {
                ulong? request;
                lock (_lock)
                    request = update.Intent == _cameraIntent ? session.Send("control.setViewportCamera",
                        JsonValue.NewObject().Set("camera", Codec.Encode(SceneReader.ToViewportCamera(update.Camera.Pose())))) : null;
                if (request is null) continue;
                if (session.AwaitResult(request.Value) is not { IsNull: true })
                    throw new RemoteException(RemoteError.ProtocolError, "viewport camera write outcome unknown");
                _confirmedEditorCamera = update.Camera;
                _navigationUncertain = false;
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
                _log.Write(string.Format(CultureInfo.InvariantCulture, "camera set coalesced {0} input->ack {1:F1} ms{2}",
                                         update.Count, ack,
                                         frame is double f ? string.Format(CultureInfo.InvariantCulture, " input->frame {0:F1} ms", f) : ""));
                var acked = new CameraAck(ack, frame, update.Count);
                _post(() => CameraAcked?.Invoke(acked));
            }
            work?.Invoke(session);
            if (Stopwatch.GetElapsedTime(_lastPoll).TotalMilliseconds >= PollMilliseconds)
            {
                // Plan 0068 M6: the play state rides the document's poll cadence, but never a pointer interaction's wire.
                if (!interacting) PollStatus(session);
                PollDocument(session);
            }
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
                                    JsonValue.NewObject().Set("command", Codec.Encode(new Atlantis.Gameplay.SetProperty(address, column3))));
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
