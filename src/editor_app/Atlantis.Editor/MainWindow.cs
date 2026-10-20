// Spec 0064 Phase 1 (Plan 0064 P4): the editor shell -- two panes, the
// entity list and the embedded Viewport, and a status line. The Viewport's
// host window is handed to atlantis_runtime as --parent-window; the Runtime's
// forwarded input orbits and dollies the editor camera (SetViewportCamera)
// and a click ray-casts (Raycast) and highlights the hit in the list. The
// status line and editor.log carry the Phase 1 measurements (P5).
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
using Avalonia.Layout;
using Avalonia.Threading;

namespace Atlantis.Editor;

public sealed record EditorOptions(string RuntimePath, string Scene);

public sealed class MainWindow : Window
{
    private const float DragThresholdPixels = 3.0f;

    private readonly EditorOptions _options;
    private readonly string _workDirectory = RuntimeProcess.NewWorkDirectory();
    private readonly EditorLog _log;
    private readonly ListBox _entities = new();
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
    private readonly OrbitCamera _camera = new();
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

    public MainWindow(EditorOptions options)
    {
        _options = options;
        _log = new EditorLog(Path.Combine(_workDirectory, "editor.log"));
        _log.Write("editor: runtime " + options.RuntimePath + ", scene " + options.Scene);
        Title = "Atlantis Editor (Phase 1 prototype) - " + options.Scene;
        Width = 1400;
        Height = 860;

        _status.IsReadOnly = true;
        _status.AcceptsReturn = true;
        _status.TextWrapping = Avalonia.Media.TextWrapping.Wrap;
        _status.Height = 96;
        _notes.PlaceholderText = "Type here (keyboard focus check)";
        _entities.SelectionMode = SelectionMode.Single;

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
        var heading = new TextBlock { Text = "Entities", Margin = new Thickness(0, 0, 0, 4) };
        DockPanel.SetDock(heading, Dock.Top);
        DockPanel.SetDock(tools, Dock.Bottom);
        left.Children.Add(heading);
        left.Children.Add(tools);
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
        if (_runtime is not null) return;  // a re-created host window: the Runtime went with the old one
        ulong parent = (ulong)window.Handle.ToInt64();
        var launch = new RuntimeLaunch(_options.RuntimePath, _options.Scene, parent, _workDirectory);
        Task.Run(() =>
        {
            RuntimeProcess runtime = RuntimeProcess.Start(launch);
            Dispatcher.UIThread.Post(() => _runtime = runtime);
            long started = Stopwatch.GetTimestamp();
            SessionInfo session = runtime.WaitForSession(TimeSpan.FromSeconds(120));
            double ms = Stopwatch.GetElapsedTime(started).TotalMilliseconds;
            Dispatcher.UIThread.Post(() => OnSession(runtime, session, ms));
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

    private void OnSession(RuntimeProcess runtime, SessionInfo session, double startMs)
    {
        ViewportWindow? window = _viewport.Window;
        if (window is not null && session.Window is ulong child)
        {
            window.RuntimeChild = new IntPtr((long)child);
            window.FitRuntimeChild();
        }
        bool hosted = window?.RuntimeChildIsHosted() ?? false;
        _runtimeLine = string.Format(CultureInfo.InvariantCulture,
                                     "Runtime pid {0}, port {1}, child 0x{2:x} {3} ({4:F0} ms to listen); logs in {5}",
                                     runtime.Pid, session.Port, session.Window ?? 0, hosted ? "hosted" : "NOT hosted",
                                     startMs, _workDirectory);
        _log.Write(_runtimeLine);
        var worker = new EditorWorker(session, action => Dispatcher.UIThread.Post(action), _log)
        {
            MeasureLatency = _measureLatency.IsChecked == true,
            SessionPath = runtime.SessionPath,
        };
        worker.Connected += OnConnected;
        worker.Failed += message =>
        {
            _runtimeLine = "session failed: " + message;
            UpdateStatus();
        };
        worker.Picked += OnPicked;
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
        _entities.ItemsSource = scene.Entities;
        if (scene.ScenePose is CameraPose pose) _camera.Reset(pose);
        else _camera.Reset(new CameraPose(new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 2, 10, 1 }, CameraLens.Default));
        _cameraInitialized = true;
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
            EntityRow? row = _scene?.Entities.FirstOrDefault(r => r.Guid == hit.Entity);
            _entities.SelectedItem = row;
            if (row is not null) _entities.ScrollIntoView(row);
            _pickLine = string.Format(CultureInfo.InvariantCulture, "pick: {0} at {1:F3} ({2:F1} ms)", hit.Entity, hit.Distance,
                                      pick.InputToAnswerMs);
        }
        else
        {
            _entities.SelectedItem = null;
            _pickLine = string.Format(CultureInfo.InvariantCulture, "pick: no hit ({0:F1} ms)", pick.InputToAnswerMs);
        }
        UpdateStatus();
    }

    // The UI thread's responsiveness (P5 U2, input-queue coupling): the gaps between 16 ms timer ticks.
    private void OnUiTick()
    {
        long now = Stopwatch.GetTimestamp();
        _uiGaps.Add(Stopwatch.GetElapsedTime(_lastUiTick, now).TotalMilliseconds);
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
