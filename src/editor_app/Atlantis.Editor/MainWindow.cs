// Spec 0064 Phase 1 (Plan 0064 P4): the editor shell -- two panes, the
// entity list and the embedded Viewport, and a status line. The Viewport's
// host window is handed to atlantis_runtime as --parent-window; the Runtime's
// forwarded input orbits and dollies the editor camera (SetViewportCamera)
// and a click ray-casts (Raycast) and highlights the hit in the list -- and,
// Plan 0066 M5 (Spec 0066 R7), outlines it in the Viewport with a translate
// Gizmo (SetViewportOverlay; a miss clears both, a camera change sends
// nothing). The
// status line and editor.log carry the Phase 1 measurements (P5).
//
// Plan 0066 M1 (Spec 0066 R8): with MeasureDrag, a Shift + left drag on the
// selected entity is the input investigation's measurement drag (no orbit, no
// pick), recorded by a DragRecorder with the UI thread's tick gaps; with
// AttachSession, the editor drives an already running, unembedded Runtime (the
// no-attachment control), taking input in its own Viewport pane; with
// DriveDrags, the protocol's drags are replayed with real OS input
// (InputDriver), and drag-report.json is written when they end.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Threading.Tasks;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Threading;
using Atlantis.Editor.Model;
using PixelRect = Avalonia.PixelRect;  // the window's pixels; the model's PixelRect is the Viewport image's

namespace Atlantis.Editor;

public sealed record EditorOptions(string RuntimePath, string Scene)
{
    // Plan 0066 M1: the input investigation's options (measurement-only).
    public bool MeasureDrag { get; init; }
    public string? AttachSession { get; init; }
    public int DriveDrags { get; init; }
    public bool ExitAfterDrive { get; init; }
    public int SettleSeconds { get; init; } = 5;
    // Plan 0066 M6: the overlay measurement's options (measurement-only).
    public bool MeasureOverlay { get; init; }
    public int OverlaySends { get; init; } = 50;
    public int OverlayPhasePairs { get; init; } = 3;
    public double OverlayPhaseSeconds { get; init; } = 15.0;
    public (float X, float Y) PickNdc { get; init; }
    public EntityGuid? OverlayEntity { get; init; }
    public (int Width, int Height)? ViewportSize { get; init; }
}

public sealed class MainWindow : Window
{
    private const float DragThresholdPixels = 3.0f;

    private readonly EditorOptions _options;
    private readonly string _workDirectory = RuntimeProcess.NewWorkDirectory();
    private readonly EditorLog _log;
    // Plan 0068 M2: the Hierarchy over the authoring document, the one shared selection, and the edit controls.
    private readonly ListBox _entities = new();
    private readonly Selection _selection = new();
    private readonly Button _create = new() { Content = "Create child" };
    private readonly Button _destroy = new() { Content = "Destroy subtree" };
    private readonly Button _toRoot = new() { Content = "Move to root" };
    private readonly Button _pickUp = new() { Content = "Pick up" };
    private readonly Button _moveHere = new() { Content = "Move under selection", IsEnabled = false };
    private readonly TextBlock _applyLine = new() { Text = "authoring: connecting..." };
    private readonly TextBlock _editLine = new();
    private IReadOnlyList<HierarchyRow> _hierarchyRows = Array.Empty<HierarchyRow>();
    private IReadOnlyList<Marker> _markers = Array.Empty<Marker>();
    private EntityGuid? _pickedUp;
    private bool _syncingSelection;
    private readonly ViewportHost _viewport = new();
    private readonly TextBox _status = new();
    private readonly TextBox _notes = new();
    private readonly CheckBox _measureLatency = new() { Content = "Measure input latency (one more frame per update)" };
    private readonly Button _measureFrames = new() { Content = "Measure 600 frames" };
    private readonly Button _resetCamera = new() { Content = "Scene camera" };
    private readonly TextBox _sizeBox = new() { PlaceholderText = "WxH, e.g. 1280x720", Width = 150 };
    private readonly Button _applySize = new() { Content = "Set viewport size" };
    private (int Width, int Height)? _targetSize;
    private int _sizeCorrections;
    private readonly EditorCamera _camera = new();
    private readonly DispatcherTimer _uiTicker = new() { Interval = TimeSpan.FromMilliseconds(16) };
    private readonly List<double> _uiGaps = new();
    private long _lastUiTick = Stopwatch.GetTimestamp();
    private long _uiWindowStart = Stopwatch.GetTimestamp();

    private RuntimeProcess? _runtime;
    private EditorWorker? _worker;
    private SceneSnapshot? _scene;
    private bool _cameraInitialized;
    private (int X, int Y)? _pressed;
    private (int X, int Y) _last;
    private bool _dragging;
    private string _runtimeLine = "starting the Runtime...";
    private string _pickLine = "";
    private string _cameraLine = "";
    private string _inputLine = "";
    private string _uiLine = "";
    private string _measureLine = "";
    // Plan 0066 M1.
    private readonly DragRecorder? _recorder;
    private (int X, int Y)? _dragPress;
    private bool _driving;

    public MainWindow(EditorOptions options)
    {
        _options = options;
        _log = new EditorLog(Path.Combine(_workDirectory, "editor.log"));
        _log.Write("editor: runtime " + options.RuntimePath + ", scene " + options.Scene);
        Title = "Atlantis Editor (Phase 1 prototype) - " + options.Scene;
        if (options.MeasureDrag)
        {
            _recorder = new DragRecorder();
            if (options.AttachSession is null)
            {
                WindowState = WindowState.Maximized;  // Phase 1 measured maximized (Viewport 2374x1320 on the reference machine)
            }
            else
            {
                // The control: the left half of the screen, leaving the Runtime's own window visible beside it.
                Opened += (_, _) =>
                {
                    if (Screens.Primary is not { } screen) return;
                    PixelRect area = screen.WorkingArea;
                    Position = area.Position;
                    Width = area.Width / 2.0 / RenderScaling;
                    Height = area.Height / RenderScaling;
                };
            }
            Title +=options.AttachSession is null ? " - measure drag (embedded)" : " - measure drag (control, unembedded)";
            _log.Write("measure drag: " + (options.AttachSession is null ? "embedded" : "attached to " + options.AttachSession));
        }
        Width = 1400;
        Height = 860;

        _status.IsReadOnly = true;
        _status.AcceptsReturn = true;
        _status.TextWrapping = Avalonia.Media.TextWrapping.Wrap;
        _status.Height = 96;
        _notes.PlaceholderText = "Type here (keyboard focus check)";
        _entities.SelectionMode = SelectionMode.Single;
        _entities.ItemTemplate = new FuncDataTemplate<HierarchyRow>((row, _) =>
            new TextBlock { Text = row.Label, Margin = new Thickness(row.Depth * 14, 0, 0, 0) });
        _entities.SelectionChanged += (_, _) => OnHierarchySelected();

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        buttons.Children.Add(_measureFrames);
        buttons.Children.Add(_resetCamera);
        var tools = new StackPanel { Spacing = 6, Margin = new Thickness(0, 6, 0, 0) };
        tools.Children.Add(_notes);
        tools.Children.Add(_measureLatency);
        tools.Children.Add(buttons);
        var sizing = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        sizing.Children.Add(_sizeBox);
        sizing.Children.Add(_applySize);
        tools.Children.Add(sizing);
        var left = new DockPanel { Margin = new Thickness(6) };
        var heading = new TextBlock { Text = "Hierarchy", Margin = new Thickness(0, 0, 0, 4) };
        DockPanel.SetDock(heading, Dock.Top);
        DockPanel.SetDock(tools, Dock.Bottom);
        var edits = new WrapPanel { Margin = new Thickness(0, 0, 0, 4) };
        foreach (Button b in new[] { _create, _destroy, _toRoot, _pickUp, _moveHere }) { b.Margin = new Thickness(0, 0, 4, 4); edits.Children.Add(b); }
        var state = new StackPanel { Spacing = 2, Margin = new Thickness(0, 4, 0, 0) };
        state.Children.Add(_applyLine);
        state.Children.Add(_editLine);
        DockPanel.SetDock(edits, Dock.Top);
        DockPanel.SetDock(state, Dock.Bottom);
        left.Children.Add(heading);
        left.Children.Add(edits);
        left.Children.Add(tools);
        left.Children.Add(state);
        left.Children.Add(_entities);

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("320,5,*"),
            RowDefinitions = new RowDefinitions("*,Auto"),
        };
        Grid.SetColumn(left, 0);
        var splitter = new GridSplitter { ResizeDirection = GridResizeDirection.Columns };
        Grid.SetColumn(splitter, 1);
        Grid.SetColumn(_viewport, 2);
        Grid.SetRow(_status, 1);
        Grid.SetColumnSpan(_status, 3);
        grid.Children.Add(left);
        grid.Children.Add(splitter);
        grid.Children.Add(_viewport);
        grid.Children.Add(_status);
        Content = grid;

        _viewport.WindowCreated += OnViewportWindow;
        _viewport.WindowDestroyed += () => _log.Write("viewport host window destroyed");
        _measureFrames.Click += (_, _) =>
        {
            _measureLine = "measuring 600 frames...";
            UpdateStatus();
            _worker?.Measure(600);
        };
        _resetCamera.Click += (_, _) => ResetToSceneCamera();
        _create.Click += (_, _) => _worker?.CreateChild(_selection.Node);
        _destroy.Click += (_, _) =>
        {
            if (_selection.Node is EntityGuid node) _worker?.DestroySubtree(node);
        };
        _toRoot.Click += (_, _) =>
        {
            if (_selection.Node is EntityGuid node) _worker?.Reparent(node, null);
        };
        _pickUp.Click += (_, _) =>
        {
            _pickedUp = _selection.Node;
            _moveHere.IsEnabled = _pickedUp is not null;
            _editLine.Text = _pickedUp is EntityGuid n ? "picked up " + n.Value.ToString("N")[..8] + ": select its new parent" : "";
        };
        _moveHere.Click += (_, _) =>
        {
            if (_pickedUp is EntityGuid node && _selection.Node is EntityGuid parent) _worker?.Reparent(node, parent);
            _pickedUp = null;
            _moveHere.IsEnabled = false;
        };
        _applySize.Click += (_, _) => ApplyViewportSize();
        _measureLatency.IsCheckedChanged += (_, _) =>
        {
            if (_worker is not null) _worker.MeasureLatency = _measureLatency.IsChecked == true;
        };
        _uiTicker.Tick += (_, _) => OnUiTick();
        _uiTicker.Start();
        Closing += (_, _) => Shutdown();
        UpdateStatus();
    }

    private void OnViewportWindow(ViewportWindow window)
    {
        _log.Write("viewport host window 0x" + window.Handle.ToString("x") + " " + window.Width + "x" + window.Height);
        window.Resized += (w, h) =>
        {
            _log.Write("viewport resized " + w + "x" + h);
            CorrectViewportSize();
            UpdateStatus();
        };
        window.Pointer += OnPointer;
        window.Wheel += OnWheel;
        window.Key += (key, down, _) =>
        {
            _inputLine = "forwarded key 0x" + key.ToString("x2", CultureInfo.InvariantCulture) + (down ? " down" : " up");
            UpdateStatus();
        };
        if (_runtime is not null || _worker is not null) return;  // a re-created host window: the Runtime went with the old one
        if (_options.AttachSession is string attach)
        {
            // Plan 0066 M1's control: an unembedded Runtime, already running, attached over the wire only.
            OnSession(null, SessionFile.Read(attach), 0.0, attach);
            return;
        }
        ulong parent = (ulong)window.Handle.ToInt64();
        var launch = new RuntimeLaunch(_options.RuntimePath, _options.Scene, parent, _workDirectory);
        Task.Run(() =>
        {
            RuntimeProcess runtime = RuntimeProcess.Start(launch);
            Dispatcher.UIThread.Post(() => _runtime = runtime);
            long started = Stopwatch.GetTimestamp();
            SessionInfo session = runtime.WaitForSession(TimeSpan.FromSeconds(120));
            double ms = Stopwatch.GetElapsedTime(started).TotalMilliseconds;
            Dispatcher.UIThread.Post(() => OnSession(runtime, session, ms, runtime.SessionPath));
        }).ContinueWith(task =>
        {
            if (task.Exception is null) return;
            string message = task.Exception.GetBaseException().Message;
            _log.Write("runtime start failed: " + message);
            Dispatcher.UIThread.Post(() =>
            {
                _runtimeLine = "Runtime failed: " + message;
                UpdateStatus();
            });
        }, TaskScheduler.Default);
    }

    private void OnSession(RuntimeProcess? runtime, SessionInfo session, double startMs, string? sessionPath)
    {
        ViewportWindow? window = _viewport.Window;
        if (runtime is not null && window is not null && session.Window is ulong child)
        {
            window.RuntimeChild = new IntPtr((long)child);
            window.FitRuntimeChild();
        }
        bool hosted = window?.RuntimeChildIsHosted() ?? false;
        _runtimeLine = runtime is null
            ? string.Format(CultureInfo.InvariantCulture, "attached to port {0} (unembedded control); logs in {1}", session.Port,
                            _workDirectory)
            : string.Format(CultureInfo.InvariantCulture,
                            "Runtime pid {0}, port {1}, child 0x{2:x} {3} ({4:F0} ms to listen); logs in {5}",
                            runtime.Pid, session.Port, session.Window ?? 0, hosted ? "hosted" : "NOT hosted",
                            startMs, _workDirectory);
        _log.Write(_runtimeLine);
        var worker = new EditorWorker(session, action => Dispatcher.UIThread.Post(action), _log)
        {
            MeasureLatency = _measureLatency.IsChecked == true,
            SessionPath = sessionPath,
            Recorder = _recorder,
        };
        worker.Connected += OnConnected;
        worker.Failed += message =>
        {
            _runtimeLine = "session failed: " + message;
            UpdateStatus();
        };
        worker.Picked += OnPicked;
        worker.HierarchyChanged += OnHierarchyChanged;
        worker.ApplyStateChanged += text => _applyLine.Text = "apply: " + text;
        worker.EditNotice += text => _editLine.Text = text;
        worker.DocumentOpened += info => _applyLine.Text = info is null ? "authoring: no document" :
            "authoring: " + info.NodeCount + " nodes (" + info.Provenance + (info.Provenance == Atlantis.Gameplay.Authoring.Provenance.Imported ? ", edits apply but cannot be saved" : "") + ")";
        worker.NodesDestroyed += nodes =>
        {
            if (_selection.Node is EntityGuid s && nodes.Contains(s)) SetSelection(null, true);
        };
        worker.NodeCreatedByEditor += node => SetSelection(node, true);
        worker.CameraAcked += ack =>
        {
            _cameraLine = string.Format(CultureInfo.InvariantCulture, "camera: input->ack {0:F1} ms{1} ({2} coalesced)",
                                        ack.InputToAckMs,
                                        ack.InputToFrameMs is double f ? string.Format(CultureInfo.InvariantCulture, ", input->frame {0:F1} ms", f) : "",
                                        ack.Coalesced);
            UpdateStatus();
        };
        worker.Measured += times =>
        {
            _measureLine = "frame times: " + times;
            UpdateStatus();
        };
        _worker = worker;
        worker.Start();
        UpdateStatus();
    }

    private void OnConnected(SceneSnapshot scene)
    {
        _scene = scene;
        if (scene.ScenePose is CameraPose pose) _camera.Reset(pose);
        else _camera.Reset(new CameraPose(new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 2, 10, 1 }, CameraLens.Default));
        _cameraInitialized = true;
        if (_recorder is not null && _selection.Node is null && scene.FirstRenderable is EntityGuid first)
        {
            SetSelection(first, true);  // the drag's default subject
        }
        if (_options.DriveDrags > 0) StartDriver();
        if (_options.MeasureOverlay) StartOverlayMeasurement();
        UpdateStatus();
    }

    // Plan 0066 M6: the Viewport sized (--viewport, else maximized), then, once the scene has settled, the overlay
    // measurement of the entity under PickNdc from the scene camera.
    private void StartOverlayMeasurement()
    {
        if (_options.ViewportSize is { } size)
        {
            _sizeBox.Text = size.Width + "x" + size.Height;
            ApplyViewportSize();
        }
        else
        {
            WindowState = WindowState.Maximized;
        }
        _measureLine = "measuring the overlay in " + _options.SettleSeconds + " s...";
        UpdateStatus();
        DispatcherTimer.RunOnce(() =>
        {
            ViewportWindow? window = _viewport.Window;
            if (window is null || _worker is null || _scene?.ScenePose is not CameraPose pose) return;
            CameraMatrices? matrices = CameraConvention.Matrices(pose, (float)window.Width / window.Height);
            if (matrices is null) return;
            PickRay ray = CameraConvention.RayThroughNdc(matrices, _options.PickNdc.X, _options.PickNdc.Y);
            var request = new OverlayMeasureRequest(pose, ray, _options.OverlayEntity, _options.OverlaySends, _options.OverlayPhasePairs,
                                                    _options.OverlayPhaseSeconds);
            string label = string.Format(CultureInfo.InvariantCulture, "{0} viewport {1}x{2} {3}", _options.Scene, window.Width, window.Height,
                                         _options.OverlayEntity is EntityGuid e
                                             ? "entity " + e
                                             : string.Format(CultureInfo.InvariantCulture, "pick ndc {0:F2},{1:F2}", _options.PickNdc.X, _options.PickNdc.Y));
            _log.Write("overlay measurement: " + label);
            _worker.MeasureOverlay(request, label, report =>
            {
                string path = OverlayReportFile.Write(_workDirectory, report);
                _log.Write("overlay report written to " + path);
                _measureLine = report.ToString();
                UpdateStatus();
                if (_options.ExitAfterDrive) Close();
            });
        }, TimeSpan.FromSeconds(_options.SettleSeconds));
    }

    // Plan 0066 M1: the protocol's drags, replayed with real OS input once the scene has settled.
    private void StartDriver()
    {
        if (_driving || _viewport.Window is null) return;
        _driving = true;
        _measureLine = "driving " + _options.DriveDrags + " drags in " + _options.SettleSeconds + " s (the mouse is taken over)...";
        UpdateStatus();
        Topmost = true;  // the injected input lands on this window, never on a Runtime window beside it (the control)
        DispatcherTimer.RunOnce(() =>
        {
            Activate();
            ViewportWindow window = _viewport.Window!;
            InputDriver.Run(window.ScreenRect, _options.DriveDrags, _log.Write, () => Dispatcher.UIThread.Post(OnDriveFinished));
        }, TimeSpan.FromSeconds(_options.SettleSeconds));
    }

    private void OnDriveFinished()
    {
        WriteDragReport();
        if (_options.ExitAfterDrive) Close();
    }

    private void WriteDragReport()
    {
        if (_recorder is null || _recorder.Drags == 0) return;
        DragReport report = _recorder.Report(_options.Scene + (_options.AttachSession is null ? " embedded" : " control"));
        string path = DragReportFile.Write(_workDirectory, report);
        _log.Write("drag report " + report);
        _log.Write("drag report written to " + path);
        _measureLine = report.ToString();
        UpdateStatus();
    }

    // P5 U1: frame times are compared at equal pixel sizes. Resizes the window so the Viewport pane is the
    // requested size in pixels (a couple of corrections absorb rounding at fractional DPI scales).
    private void ApplyViewportSize()
    {
        string[] parts = (_sizeBox.Text ?? "").Trim().ToLowerInvariant().Split('x');
        if (parts.Length != 2 || !int.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out int w) ||
            !int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out int h) || w < 64 || h < 64)
        {
            _measureLine = "viewport size: expected WxH";
            UpdateStatus();
            return;
        }
        WindowState = WindowState.Normal;
        _targetSize = (w, h);
        _sizeCorrections = 0;
        CorrectViewportSize();
    }

    private void CorrectViewportSize()
    {
        ViewportWindow? window = _viewport.Window;
        if (_targetSize is not { } target || window is null) return;
        int dw = target.Width - window.Width;
        int dh = target.Height - window.Height;
        if ((dw == 0 && dh == 0) || ++_sizeCorrections > 4)
        {
            _measureLine = "viewport " + window.Width + "x" + window.Height + (dw == 0 && dh == 0 ? " (as requested)" : " (closest reached)");
            _targetSize = null;
            UpdateStatus();
            return;
        }
        double scale = RenderScaling;
        Width += dw / scale;
        Height += dh / scale;
    }

    private void ResetToSceneCamera()
    {
        if (_scene?.ScenePose is CameraPose pose) _camera.Reset(pose);
        _worker?.SetCamera(null, Stopwatch.GetTimestamp());
    }

    private void OnPointer(ViewportPointerEvent e)
    {
        if (_recorder is not null && MeasureDrag(e)) return;
        switch (e.Kind)
        {
            case ViewportPointer.LeftDown:
            case ViewportPointer.RightDown:
                _pressed = (e.X, e.Y);
                _last = (e.X, e.Y);
                _dragging = false;
                break;
            case ViewportPointer.Move when _pressed is { } start:
                if (!_dragging && MathF.Max(MathF.Abs(e.X - start.X), MathF.Abs(e.Y - start.Y)) > DragThresholdPixels) _dragging = true;
                if (_dragging && _cameraInitialized)
                {
                    _camera.Orbit(e.X - _last.X, e.Y - _last.Y);
                    _worker?.SetCamera(SceneReader.ToViewportCamera(_camera.Pose()), e.ArrivalTicks);
                }
                _last = (e.X, e.Y);
                break;
            case ViewportPointer.LeftUp:
                if (_pressed is not null && !_dragging) Pick(e.X, e.Y, e.ArrivalTicks);
                _pressed = null;
                break;
            case ViewportPointer.RightUp:
                _pressed = null;
                break;
        }
        _inputLine = "forwarded " + e.Kind + " at " + e.X + "," + e.Y;
    }

    // Plan 0066 M1: Shift + left drag on the selected entity is the measurement drag (any left drag in a driven run,
    // whose injected input cannot rely on keyboard focus). Returns whether it took the event.
    private bool MeasureDrag(ViewportPointerEvent e)
    {
        switch (e.Kind)
        {
            case ViewportPointer.LeftDown when (e.Shift || _options.DriveDrags > 0) && _worker is not null && _cameraInitialized &&
                                               _selection.Node is EntityGuid selected && _viewport.Window is { } window:
                _dragPress = (e.X, e.Y);
                _recorder!.Press(e.ArrivalTicks);
                _log.Write("ui press at " + e.X + "," + e.Y);
                _worker.BeginDrag(selected, _camera.Pose(), window.Height);
                _inputLine = "measurement drag of " + selected.Value.ToString("N")[..8];
                return true;
            case ViewportPointer.Move when _dragPress is { } press:
                _worker?.DragTo(e.X - press.X, e.Y - press.Y, e.ArrivalTicks);
                return true;
            case ViewportPointer.LeftUp when _dragPress is { } press:
                _worker?.EndDrag(e.X - press.X, e.Y - press.Y, e.ArrivalTicks);
                _recorder!.Release(e.ArrivalTicks);
                _log.Write("ui release at " + e.X + "," + e.Y);
                _dragPress = null;
                return true;
            default:
                return false;
        }
    }

    private void OnWheel(int delta, long arrivalTicks)
    {
        if (!_cameraInitialized) return;
        _camera.Dolly(delta / 120.0f);
        _worker?.SetCamera(SceneReader.ToViewportCamera(_camera.Pose()), arrivalTicks);
        _inputLine = "forwarded wheel " + delta;
    }

    private void Pick(int x, int y, long arrivalTicks)
    {
        ViewportWindow? window = _viewport.Window;
        if (window is null || _worker is null || window.Width <= 0 || window.Height <= 0) return;
        CameraMatrices? matrices = _camera.Matrices((float)window.Width / window.Height);
        if (matrices is null) return;
        // E1's order: a light or camera marker under the pointer wins, else the geometry pick (the Runtime's raycast).
        var rect = new Atlantis.Editor.Model.PixelRect(0, 0, window.Width, window.Height);
        if (Picking.PickMarker(_markers, matrices, rect, x, y) is EntityGuid marker)
        {
            SetSelection(marker, true);
            _pickLine = "pick: marker " + marker.Value.ToString("N")[..8];
            UpdateStatus();
            return;
        }
        float ndcX = (float)x / window.Width * 2.0f - 1.0f;
        float ndcY = (float)y / window.Height * 2.0f - 1.0f;
        _worker.Raycast(CameraConvention.RayThroughNdc(matrices, ndcX, ndcY), arrivalTicks);
    }

    private void OnPicked(PickResult pick)
    {
        if (pick.Result.Error is ControlError error)
        {
            _pickLine = "pick: " + error;
        }
        else if (pick.Result.Hit is RayHit hit)
        {
            SetSelection(hit.Entity, true);
            _pickLine = string.Format(CultureInfo.InvariantCulture, "pick: {0} at {1:F3} ({2:F1} ms)", hit.Entity, hit.Distance,
                                      pick.InputToAnswerMs);
        }
        else
        {
            SetSelection(null, true);
            _pickLine = string.Format(CultureInfo.InvariantCulture, "pick: no hit ({0:F1} ms)", pick.InputToAnswerMs);
        }
        UpdateStatus();
    }

    // Plan 0068 M2 (Spec 0068 R6, Q6): the one selection. A change anywhere -- the Viewport pick, the Hierarchy, a
    // node destroyed -- updates the list's highlight and the overlay (outline, Gizmo, markers).
    private void SetSelection(EntityGuid? node, bool scroll)
    {
        _selection.Select(node);
        _syncingSelection = true;
        try
        {
            HierarchyRow? row = node is EntityGuid n ? _hierarchyRows.FirstOrDefault(r => r.Node == n) : null;
            _entities.SelectedItem = row;
            if (scroll && row is not null) _entities.ScrollIntoView(row);
        }
        finally
        {
            _syncingSelection = false;
        }
        _worker?.ShowSelection(node);
    }

    private void OnHierarchySelected()
    {
        if (_syncingSelection) return;
        EntityGuid? node = (_entities.SelectedItem as HierarchyRow)?.Node;
        if (node == _selection.Node) return;
        _selection.Select(node);
        _worker?.ShowSelection(node);
    }

    private void OnHierarchyChanged(IReadOnlyList<HierarchyRow> rows, IReadOnlyList<Marker> markers)
    {
        _hierarchyRows = rows;
        _markers = markers;
        _syncingSelection = true;
        try
        {
            _entities.ItemsSource = rows;
            _entities.SelectedItem = _selection.Node is EntityGuid n ? rows.FirstOrDefault(r => r.Node == n) : null;
        }
        finally
        {
            _syncingSelection = false;
        }
    }

    // The UI thread's responsiveness (P5 U2, input-queue coupling): the gaps between 16 ms timer ticks.
    private void OnUiTick()
    {
        long now = Stopwatch.GetTimestamp();
        double gap = Stopwatch.GetElapsedTime(_lastUiTick, now).TotalMilliseconds;
        _uiGaps.Add(gap);
        if (_scene is not null) _recorder?.UiGap(now, gap);  // from the connection on: startup is not idle
        _lastUiTick = now;
        if (Stopwatch.GetElapsedTime(_uiWindowStart, now).TotalSeconds < 2.0) return;
        _uiWindowStart = now;
        FrameTimes gaps = FrameSampler.Summarize(_uiGaps, 0);
        _uiGaps.Clear();
        _uiLine = string.Format(CultureInfo.InvariantCulture, "UI tick gaps (2 s): p95 {0:F1} ms, max {1:F1} ms", gaps.P95Ms, gaps.MaxMs);
        _log.Write(string.Format(CultureInfo.InvariantCulture, "ui ticks {0} p95 {1:F1} ms max {2:F1} ms", gaps.Frames, gaps.P95Ms, gaps.MaxMs));
        UpdateStatus();
    }

    private void UpdateStatus()
    {
        string size = _viewport.Window is { } w ? "viewport " + w.Width + "x" + w.Height + " px   " : "";
        _status.Text = string.Join("\n", new[] { _runtimeLine, _pickLine + "   " + _cameraLine, size + _inputLine + "   " + _uiLine, _measureLine }
                                         .Where(line => !string.IsNullOrWhiteSpace(line)));
    }

    private void Shutdown()
    {
        _uiTicker.Stop();
        if (!_driving) WriteDragReport();  // a manual run's report (a driven run writes its own)
        _worker?.Dispose();
        _worker = null;
        if (_runtime is not null)
        {
            // Destroying the host window destroys the Runtime's child: the Runtime sees its parent go and exits.
            long start = Stopwatch.GetTimestamp();
            _viewport.Window?.Dispose();
            int? code = _runtime.WaitForExit(TimeSpan.FromSeconds(10));
            _log.Write(string.Format(CultureInfo.InvariantCulture, "runtime {0} after {1:F0} ms",
                                     code is int c ? "exited with " + c : "did not exit: killed",
                                     Stopwatch.GetElapsedTime(start).TotalMilliseconds));
            _runtime.Dispose();
            _runtime = null;
        }
        _log.Dispose();
    }
}
