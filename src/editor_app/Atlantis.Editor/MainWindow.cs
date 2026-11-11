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
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.VisualTree;
using Avalonia.Layout;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Atlantis.Editor.Model;
using Atlantis.Gameplay.Assets;
using PixelRect = Avalonia.PixelRect;  // the window's pixels; the model's PixelRect is the Viewport image's

namespace Atlantis.Editor;

public sealed record EditorOptions(string RuntimePath, string Scene)
{
    public string? AssetBuildProfile { get; init; }
    public string? AssetGeneration { get; init; }
    public bool AllowSceneSaveAs { get; init; }
    public string? SavedSceneCheckout { get; init; }
    public string? SavedSceneCMake { get; init; }
    public string? SavedSceneAppHost { get; init; }
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
    private readonly SavedSceneAttempts _savedAttempts = new();
    private readonly SavedSceneWorkspace? _savedWorkspace;
    private readonly ListBox _savedList = new() { Height = 140, SelectionMode = SelectionMode.Single };
    private readonly Button _savedRefresh = new() { Content = "Refresh" };
    private readonly Button _savedBuild = new() { Content = "Build", IsEnabled = false };
    private readonly Button _savedOpen = new() { Content = "Open", IsEnabled = false };
    private readonly TextBox _savedManaged = new() { PlaceholderText = "Absolute committed managed import root (optional)" };
    private readonly TextBlock _savedDiscovery = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly TextBlock _savedHint = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly TextBox _savedBuildResult = new() { IsReadOnly = true, AcceptsReturn = true, TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly TextBlock _savedOpenResult = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };

    // Plan 0068 M2: the Hierarchy over the authoring document, the one shared selection, and the edit controls.
    private readonly ListBox _entities = new();
    private readonly Selection _selection = new();
    private readonly Button _create = new() { Content = "Create child" };
    private readonly Button _destroy = new() { Content = "Destroy subtree" };
    private readonly Button _toRoot = new() { Content = "Move to root" };
    private readonly Button _pickUp = new() { Content = "Pick up" };
    private readonly Button _moveHere = new() { Content = "Move under selection", IsEnabled = false };
    // Plan 0068 M5: the history's Undo and Redo, labelled with the edit they would step over.
    private readonly Button _undo = new() { Content = "Undo", IsEnabled = false };
    private readonly Button _redo = new() { Content = "Redo", IsEnabled = false };
    // Plan 0068 M6 (Spec 0068 R9): the document's save and the Runtime's transport. The Runtime starts playing, so
    // Pause is the enabled action until the first play state says otherwise.
    private readonly Button _save = new() { Content = "Save" };
    private readonly Button _saveAs = new() { Content = "Save As", IsEnabled = false };
    private readonly TextBox _saveAsName = new() { PlaceholderText = "Copy name", Width = 115, MaxLength = 48 };
    private readonly TextBlock _saveAsState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private long _saveAsRequest;
    private bool _saveAsPending;
    private readonly Button _newScene = new() { Content = "New Scene", IsEnabled = false };
    private readonly TextBox _newSceneName = new() { PlaceholderText = "New scene name", Width = 135, MaxLength = 48 };
    private readonly TextBlock _newSceneState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private long _newSceneRequest;
    private string? _newScenePendingName;
    private bool _newSceneSessionReady;
    private readonly Button _retryApply = new() { Content = "Apply current document / Retry", IsEnabled = false };
    private bool _allowGizmoPreview;
    private readonly Button _play = new() { Content = "Play", IsEnabled = false };
    private readonly Button _pause = new() { Content = "Pause" };
    private readonly Button _step = new() { Content = "Step", IsEnabled = false };
    private readonly TextBlock _applyLine = new() { Text = "authoring: connecting..." };
    private readonly TextBlock _editLine = new();
    private IReadOnlyList<HierarchyRow> _hierarchyRows = Array.Empty<HierarchyRow>();
    private IReadOnlyList<Marker> _markers = Array.Empty<Marker>();
    private EntityGuid? _pickedUp;
    private bool _syncingSelection;
    // Plan 0068 M4: the Gizmo's settings, the selected node's matrix and availability, and the grab in progress.
    private readonly ComboBox _gizmoMode = new() { ItemsSource = new[] { "Move", "Rotate", "Scale" }, SelectedIndex = 0, MinWidth = 80 };
    private readonly ComboBox _gizmoSpace = new() { ItemsSource = new[] { "World", "Local" }, SelectedIndex = 0, MinWidth = 80 };
    private readonly CheckBox _gizmoSnap = new() { Content = "Snap" };
    private GizmoSnapshot? _gizmoShown;
    private GizmoGrab? _gizmoGrab;
    private GizmoMode _gizmoGrabMode;
    // Plan 0068 M3: the Inspector panel, rebuilt from the worker's snapshots (not while a field is being typed in).
    private readonly StackPanel _inspectorPanel = new() { Spacing = 4, Margin = new Thickness(6) };
    private InspectorSnapshot? _inspectorShown;
    private InspectorSnapshot? _inspectorDeferred;
    private readonly AssetBrowser _assetBrowser = new();
    private readonly ListBox _assetsList = new() { Height = 230 };
    private readonly TextBlock _assetsState = new();
    private readonly AssetPreviewRequests _previewRequests = new();
    private readonly Button _previewMesh = new() { Content = "Preview geometry", IsEnabled = false };
    private readonly TextBlock _previewState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly Image _previewImage = new() { Width = 256, Height = 256, IsVisible = false };
    private SessionInfo? _previewSession;
    private bool _previewRetry, _previewUnavailable;
    private readonly TextBlock _rebuildState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly Button _rebuildStart = new() { Content = "Rebuild selected", IsEnabled = false };
    private readonly Button _rebuildRefresh = new() { Content = "Refresh rebuild status", IsEnabled = false };
    private readonly TextBox _importName = new() { PlaceholderText = "Inbox name (without .gltf)" };
    private readonly Button _importRegister = new() { Content = "Register and publish", IsEnabled = false };
    private readonly Button _importResume = new() { Content = "Resume publication", IsEnabled = false };
    private readonly Button _importRefresh = new() { Content = "Refresh import status", IsEnabled = false };
    private readonly TextBlock _importState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly Button _addMesh = new() { Content = "Add to Scene", IsEnabled = false };
    private readonly TextBlock _addMeshState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private bool _canAddMeshToDocument, _addingMesh;
    private long _meshRequest, _meshDocumentLifetime;
    private readonly DispatcherTimer _importPoll = new() { Interval = TimeSpan.FromSeconds(1) };
    private ImportId? _importJob;
    private bool _importPending, _importRunning;
    private readonly DispatcherTimer _rebuildPoll = new() { Interval = TimeSpan.FromSeconds(1) };
    private RebuildId? _rebuildJob;
    private GenerationId? _activeAssetGeneration;
    private bool _rebuildPending, _rebuildRunning;
    private readonly StackPanel _assetDetails = new() { Spacing = 4 };
    private bool _assetsSync;
    private readonly TabControl _assetTabs = new();
    private TextBox? _nameBox;
    private AssetReferencePickerWindow? _referenceWindow;
    private bool _referenceBusy, _referenceSessionFailed;
    private long _referenceLaunch;
    private Button? _referenceFocus;
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
        if (options.SavedSceneCheckout is { } checkout && options.SavedSceneCMake is { } cmake &&
            options.SavedSceneAppHost is not null &&
            options.Scene != "saved_scene" && options.AttachSession is null && !options.MeasureDrag && !options.MeasureOverlay)
            _savedWorkspace = new(checkout, cmake, _workDirectory);
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
            new TextBlock { Text = row?.Label ?? "", Margin = new Thickness((row?.Depth ?? 0) * 14, 0, 0, 0) });
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
        foreach (Button b in new[] { _undo, _redo, _create, _destroy, _toRoot, _pickUp, _moveHere, _save, _retryApply, _play, _pause, _step })
        { b.Margin = new Thickness(0, 0, 4, 4); edits.Children.Add(b); }
        edits.Children.Add(_saveAsName);
        edits.Children.Add(_saveAs);
        edits.Children.Add(_newSceneName);
        edits.Children.Add(_newScene);
        _newSceneState.Text = NewSceneHint();
        foreach (Control c in new Control[] { _gizmoMode, _gizmoSpace, _gizmoSnap }) { c.Margin = new Thickness(0, 0, 4, 4); edits.Children.Add(c); }
        var state = new StackPanel { Spacing = 2, Margin = new Thickness(0, 4, 0, 0) };
        state.Children.Add(_applyLine);
        state.Children.Add(_editLine);
        state.Children.Add(_saveAsState);
        state.Children.Add(_newSceneState);
        DockPanel.SetDock(edits, Dock.Top);
        DockPanel.SetDock(state, Dock.Bottom);
        left.Children.Add(heading);
        left.Children.Add(edits);
        left.Children.Add(tools);
        left.Children.Add(state);
        left.Children.Add(_entities);

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("320,5,*,5,320"),
            RowDefinitions = new RowDefinitions("*,Auto"),
        };
        Grid.SetColumn(left, 0);
        var splitter = new GridSplitter { ResizeDirection = GridResizeDirection.Columns };
        Grid.SetColumn(splitter, 1);
        Grid.SetColumn(_viewport, 2);
        var inspectorSplitter = new GridSplitter { ResizeDirection = GridResizeDirection.Columns };
        Grid.SetColumn(inspectorSplitter, 3);
        var inspectorScroll = new ScrollViewer { Content = _inspectorPanel, HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled };
        var tabs = new List<TabItem> {
            new() { Header = "Inspector", Content = inspectorScroll },
            new() { Header = "Assets", Content = BuildAssetsPanel() } };
        if (_savedWorkspace is not null)
            tabs.Add(new TabItem { Header = "Saved Scenes", Content = BuildSavedScenePanel() });
        _assetTabs.ItemsSource = tabs;
        _assetTabs.SelectionChanged += (_, e) =>
        {
            // SelectionChanged also bubbles from the list and filter ComboBox.
            if (!ReferenceEquals(e.Source, _assetTabs)) return;
            SyncPreviewSelection();
            if (_assetTabs.SelectedIndex == 1 &&
                _assetBrowser.State is "Not loaded" or "Disconnected") LoadAssets();
            if (_assetTabs.SelectedIndex == 2)
                RefreshSavedScenes();
        };
        Grid.SetColumn(_assetTabs, 4);
        Grid.SetRow(_status, 1);
        Grid.SetColumnSpan(_status, 5);
        grid.Children.Add(left);
        grid.Children.Add(splitter);
        grid.Children.Add(_viewport);
        grid.Children.Add(inspectorSplitter);
        grid.Children.Add(_assetTabs);
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
        _gizmoMode.SelectionChanged += (_, _) => SendGizmoSettings();
        _gizmoSpace.SelectionChanged += (_, _) => SendGizmoSettings();
        _gizmoSnap.IsCheckedChanged += (_, _) => SendGizmoSettings();
        _create.Click += (_, _) => _worker?.CreateChild(_selection.Node);
        _undo.Click += (_, _) => _worker?.Undo();
        _redo.Click += (_, _) => _worker?.Redo();
        _save.Click += (_, _) => _worker?.Save();
        _saveAsName.TextChanged += (_, _) => UpdateSaveAsButton();
        _saveAs.Click += (_, _) => SaveSceneCopy();
        _newSceneName.TextChanged += (_, _) => UpdateNewSceneButton();
        _newScene.Click += (_, _) => CreateScene();
        _retryApply.Click += (_, _) =>
        {
            if (!_retryApply.IsEnabled || _worker is not EditorWorker worker) return;
            _retryApply.IsEnabled = false;
            worker.RetryApply();
        };
        _play.Click += (_, _) => _worker?.Play();
        _pause.Click += (_, _) => _worker?.Pause();
        _step.Click += (_, _) => _worker?.StepFrame();
        KeyDown += (_, e) =>
        {
            // Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z): not while a text box is being edited, which has its own.
            if ((e.KeyModifiers & KeyModifiers.Control) == 0 || FocusManager?.GetFocusedElement() is TextBox) return;
            if (e.Key == Key.Z && (e.KeyModifiers & KeyModifiers.Shift) == 0) { _worker?.Undo(); e.Handled = true; }
            else if (e.Key == Key.Y || e.Key == Key.Z) { _worker?.Redo(); e.Handled = true; }
        };
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
        Closing += (_, e) =>
        {
            if (_savedAttempts.Busy)
            {
                e.Cancel = true;
                _savedOpenResult.Text = "Wait for the saved scene operation to finish, then close again.";
                _assetTabs.SelectedIndex = 2;
                return;
            }
            Shutdown();
        };
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
        var launch = new RuntimeLaunch(_options.RuntimePath, _options.Scene, parent, _workDirectory)
        {
            AssetBuildProfile = _options.AssetBuildProfile, AssetGeneration = _options.AssetGeneration,
            AllowSceneSaveAs = _options.AllowSceneSaveAs,
        };
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
        EndPreviewSession();
        _worker?.Stop();
        ++_saveAsRequest;
        _saveAsPending = false;
        _saveAs.IsEnabled = false;
        _saveAsState.Text = "";
        ++_newSceneRequest;
        _newScenePendingName = null;
        _newScene.IsEnabled = false;
        _newSceneState.Text = NewSceneHint();
        ++_meshRequest;
        _meshDocumentLifetime = 0;
        _canAddMeshToDocument = _addingMesh = false;
        _addMeshState.Text = "";
        ++_referenceLaunch;
        _referenceWindow?.Close(); _referenceWindow = null;
        _referenceBusy = false; _referenceSessionFailed = false;
        _importPoll.Stop(); _importJob = null; _importPending = _importRunning = false;
        _importState.Text = "";
        UpdateImportButtons();
        _rebuildPoll.Stop(); _rebuildJob = null; _activeAssetGeneration = null;
        _rebuildPending = _rebuildRunning = false; _rebuildState.Text = "";
        _assetBrowser.Disconnect();
        RenderAssets();
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
        worker.Connected += snapshot =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            _previewSession = session;
            OnConnected(snapshot);
            SyncPreviewSelection();
            _newSceneSessionReady = true;
            UpdateNewSceneButton();
            UpdateRebuildButtons();
            if (_assetTabs.SelectedIndex == 1) LoadAssets();
        };
        worker.Failed += message =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            EndPreviewSession();
            _referenceSessionFailed = true;
            if (_newScenePendingName is not null)
            {
                _newSceneState.Text = "New Scene was not sent: the authoring session disconnected.";
                _newScenePendingName = null;
            }
            UpdateNewSceneButton();
            if (_saveAsPending) _saveAsState.Text = "Save As interrupted. Inspect the destination and confirm settlement before retrying.";
            _saveAsPending = false;
            ++_saveAsRequest;
            UpdateSaveAsButton();
            if (_addingMesh) _addMeshState.Text = "Add interrupted. Inspect the document on reconnect before retrying.";
            _addingMesh = false;
            ++_meshRequest;
            _importPoll.Stop();
            if (_importPending || _importRunning)
                _importState.Text = _importJob is ImportId importId ? $"Import {importId} outcome unknown." : "Import admission unknown.";
            _importPending = _importRunning = false;
            UpdateImportButtons();
            _rebuildPoll.Stop();
            if (_rebuildPending || _rebuildRunning)
                _rebuildState.Text = _rebuildJob is RebuildId job ? $"Rebuild {job} outcome unknown." : "Rebuild admission unknown.";
            _rebuildPending = _rebuildRunning = false;
            UpdateRebuildButtons();
            _retryApply.IsEnabled = false; _allowGizmoPreview = false; _gizmoGrab = null;
            _referenceWindow?.Invalidate("the session failed");
            _referenceBusy = false; ++_referenceLaunch; UpdateReferenceButtons();
            _assetBrowser.Disconnect(); RenderAssets();
            _runtimeLine = "session failed: " + message;
            UpdateStatus();
        };
        worker.Picked += OnPicked;
        worker.HierarchyChanged += OnHierarchyChanged;
        worker.InspectorChanged += OnInspectorChanged;
        worker.AssetReferenceInvalidated += (request, reason) =>
        {
            if (ReferenceEquals(_worker, worker) && _referenceWindow?.Target.Request == request)
                _referenceWindow.Invalidate(reason);
        };
        worker.HistoryChanged += history =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            _undo.IsEnabled = history.CanUndo;
            _redo.IsEnabled = history.CanRedo;
            _undo.Content = history.CanUndo ? "Undo " + history.UndoLabel : "Undo";
            _redo.Content = history.CanRedo ? "Redo " + history.RedoLabel : "Redo";
        };
        worker.GizmoChanged += snapshot => _gizmoShown = snapshot;
        worker.CommitApplied += (ms, state) => _editLine.Text = string.Format(CultureInfo.InvariantCulture, "{0} -- committed, applied in {1:F0} ms", _editLine.Text, ms);
        worker.ApplyStateChanged += text => { if (ReferenceEquals(_worker, worker)) _applyLine.Text = "apply: " + text; };
        worker.ApplyControlsChanged += (canApply, canPreview) =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            _retryApply.IsEnabled = canApply; _allowGizmoPreview = canPreview;
            _gizmoMode.IsEnabled = _gizmoSpace.IsEnabled = _gizmoSnap.IsEnabled = canPreview;
            if (!canPreview) { _gizmoGrab = null; _gizmoShown = null; }
            UpdateAddMeshButton();
        };
        // Plan 0068 M6 (Spec 0068 R9): the transport follows the play state -- Step runs a frame while paused.
        worker.PlayStateChanged += playing =>
        {
            _play.IsEnabled = !playing;
            _pause.IsEnabled = playing;
            _step.IsEnabled = !playing;
        };
        worker.EditNotice += text => _editLine.Text = text;
        worker.DocumentOpened += info =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            _canAddMeshToDocument = info?.Provenance == Atlantis.Gameplay.Authoring.Provenance.HandAuthored;
            UpdateAddMeshButton();
            UpdateSaveAsButton();
            _applyLine.Text = info is null ? "authoring: no document" :
                "authoring: " + info.NodeCount + " nodes (" + info.Provenance + (info.Provenance == Atlantis.Gameplay.Authoring.Provenance.Imported ? ", edits apply but cannot be saved" : "") + ")";
        };
        worker.MeshDocumentReopened += lifetime =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            ++_meshRequest;
            _meshDocumentLifetime = lifetime;
            if (_saveAsPending)
            {
                ++_saveAsRequest;
                _saveAsPending = false;
                _saveAsState.Text = "Document reopened. Inspect the destination if the copy was already sent.";
                UpdateSaveAsButton();
            }
            _addingMesh = false;
            _addMeshState.Text = "Scene reopened.";
            UpdateAddMeshButton();
        };
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

    private GizmoMode ModeUi => _gizmoMode.SelectedIndex switch { 1 => GizmoMode.Rotate, 2 => GizmoMode.Scale, _ => GizmoMode.Translate };

    private GizmoSpace SpaceUi => _gizmoSpace.SelectedIndex == 1 ? GizmoSpace.Local : GizmoSpace.World;

    private void SendGizmoSettings() => _worker?.SetGizmo(ModeUi, SpaceUi, _gizmoSnap.IsChecked == true);

    // Plan 0068 M4 (Spec 0068 R5): a press on a handle of the selected node's Gizmo grabs it -- E1's handle layout and its
    // 8-pixel pick (gizmo.h's math, ported and held by vectors) -- a move drags it (previews on the Runtime World, one
    // coalesced batch per worker turn), the release commits. Returns whether it took the event.
    private bool GizmoPointer(ViewportPointerEvent e)
    {
        if (!_allowGizmoPreview) return false;
        ViewportWindow? window = _viewport.Window;
        switch (e.Kind)
        {
            case ViewportPointer.LeftDown when _gizmoShown is { World: { } world, Availability: { } availability } && _worker is not null &&
                                               _cameraInitialized && window is { Width: > 0, Height: > 0 }:
            {
                CameraMatrices? camera = _camera.Matrices((float)window.Width / window.Height);
                if (camera is null) return false;
                GizmoMode mode = GizmoLayout.EffectiveMode(availability, ModeUi);
                GizmoSpace space = GizmoLayout.EffectiveSpace(world, SpaceUi);
                Vector3[] directions = GizmoLayout.Directions(world, mode, space);
                var rect = new Atlantis.Editor.Model.PixelRect(0, 0, window.Width, window.Height);
                GizmoHandle?[]? handles = Gizmo.Handles(camera, rect, new Vector3(world[12], world[13], world[14]), directions);
                if (handles is null || Gizmo.Grab(handles, directions, camera, mode, new Vector2(e.X, e.Y)) is not GizmoGrab grab) return false;
                _gizmoGrab = grab;
                UpdateAddMeshButton();
                _gizmoGrabMode = mode;
                _pressed = null;
                _worker.BeginGizmoDrag(grab.Axis);
                _inputLine = "gizmo " + mode + " " + grab.Axis;
                return true;
            }
            case ViewportPointer.Move when _gizmoGrab is { } grab:
                _worker?.GizmoDragTo(Gizmo.DragAmount(_gizmoGrabMode, grab, new Vector2(e.X, e.Y)), e.ArrivalTicks);
                return true;
            case ViewportPointer.LeftUp when _gizmoGrab is { } grab:
                _worker?.EndGizmoDrag(Gizmo.DragAmount(_gizmoGrabMode, grab, new Vector2(e.X, e.Y)), e.ArrivalTicks);
                _gizmoGrab = null;
                UpdateAddMeshButton();
                return true;
            default:
                return false;
        }
    }

    private void OnPointer(ViewportPointerEvent e)
    {
        if (_recorder is not null && MeasureDrag(e)) return;
        if (GizmoPointer(e)) return;
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

    // Plan 0068 M3 (Spec 0068 R4): one typed editor per field -- a text box committed on Enter or loss of focus (one
    // transaction each), a combo box for an enum, a clear button for an optional field, a note for a read-only one --
    // and Add / Remove component. The panel is not rebuilt under a field being typed in: the snapshot waits.
    private Control BuildAssetsPanel()
    {
        var panel = new StackPanel { Spacing = 6, Margin = new Thickness(6) };
        panel.Children.Add(new TextBlock { Text = "Catalog fixed at first query. Changed build or initial load failure: restart Runtime. Reload view does not reread disk.", TextWrapping = Avalonia.Media.TextWrapping.Wrap });
        var reload = new Button { Content = "Reload view" };
        reload.Click += (_, _) => LoadAssets(); panel.Children.Add(reload);
        var types = new ComboBox { ItemsSource = new[] { "All types", "Mesh", "Texture", "Material", "Scene", "Environment" }, SelectedIndex = 0 };
        types.SelectionChanged += (_, _) => { _assetBrowser.Type = types.SelectedIndex <= 0 ? null : (Atlantis.Gameplay.Assets.AssetType)(types.SelectedIndex - 1); RenderAssets(); };
        panel.Children.Add(types);
        var search = new TextBox { PlaceholderText = "Source or GUID" };
        search.TextChanged += (_, _) => { _assetBrowser.Search = search.Text ?? ""; RenderAssets(); };
        panel.Children.Add(search); panel.Children.Add(_assetsState);
        _assetsList.ItemTemplate = new FuncDataTemplate<Atlantis.Gameplay.Assets.AssetSummary>((v, _) => new TextBlock {
            Text = v is null ? "" : v.Type + " · " + v.Source + "\n" + v.Guid,
            TextWrapping = Avalonia.Media.TextWrapping.Wrap });
        _assetsList.SelectionChanged += (_, _) =>
        {
            if (_assetsSync) return;
            if (_assetsList.SelectedItem is AssetSummary asset) ReadAsset(asset.Guid);
            else { _assetBrowser.ClearSelection(); RenderAssets(); }
        };
        panel.Children.Add(_assetsList);
        var copy = new Button { Content = "Copy GUID" };
        copy.Click += async (_, _) => { if (_assetBrowser.Selected is AssetGuid guid && TopLevel.GetTopLevel(this)?.Clipboard is { } clipboard) await clipboard.SetTextAsync(guid.ToString()); };
        panel.Children.Add(copy);
        _previewMesh.Click += (_, _) => RequestMeshPreview();
        panel.Children.Add(_previewMesh);
        panel.Children.Add(_previewState);
        panel.Children.Add(_previewImage);
        ToolTip.SetShowOnDisabled(_addMesh, true);
        _addMesh.Click += (_, _) => AddMeshToScene();
        panel.Children.Add(_addMesh);
        panel.Children.Add(_addMeshState);
        _rebuildStart.Click += (_, _) => StartAssetRebuild();
        _rebuildRefresh.Click += (_, _) => ReadRebuildStatus();
        _rebuildPoll.Tick += (_, _) => ReadRebuildStatus();
        panel.Children.Add(_rebuildStart);
        panel.Children.Add(_rebuildRefresh);
        panel.Children.Add(_rebuildState);
        panel.Children.Add(_assetDetails);
        _importName.TextChanged += (_, _) => UpdateImportButtons();
        _importRegister.Click += (_, _) => StartImport(ImportMode.Register);
        _importResume.Click += (_, _) => StartImport(ImportMode.Resume);
        _importRefresh.Click += (_, _) => ReadImportStatus();
        _importPoll.Tick += (_, _) => ReadImportStatus();
        panel.Children.Add(_importName);
        panel.Children.Add(_importRegister);
        panel.Children.Add(_importResume);
        panel.Children.Add(_importRefresh);
        panel.Children.Add(_importState);
        return new ScrollViewer { Content = panel };
    }
    private void LoadAssets()
    {
        var worker = _worker; if (worker is null) return;
        long request = _assetBrowser.BeginList(); RenderAssets();
        worker.LoadAssets(result =>
        {
            if (!ReferenceEquals(worker, _worker) || !_assetBrowser.CompleteList(request, result)) return;
            RenderAssets();
            // BeginList invalidated pending details. Selection is restored under
            // _assetsSync, so its event cannot request the replacement for us.
            if (result.IsOk && _assetBrowser.Selected is AssetGuid guid) ReadAsset(guid);
        });
    }
    private void ReadAsset(AssetGuid guid)
    {
        var worker = _worker; if (worker is null) return;
        long request = _assetBrowser.Select(guid); RenderAssets();
        worker.ReadAsset(guid, result => { if (ReferenceEquals(worker, _worker) && _assetBrowser.CompleteDetail(request, guid, result)) RenderAssets(); });
    }
    private void RenderAssets()
    {
        _assetsState.Text = _assetBrowser.ViewState;
        var visible = _assetBrowser.Visible;
        _assetsSync = true;
        try { _assetsList.ItemsSource = visible; _assetsList.SelectedItem = visible.FirstOrDefault(v => v.Guid == _assetBrowser.Selected); }
        finally { _assetsSync = false; }
        SyncPreviewSelection();
        UpdateAddMeshButton();
        UpdateRebuildButtons();
        UpdateImportButtons();
        _assetDetails.Children.Clear();
        if (_assetBrowser.Details is not Atlantis.Gameplay.Assets.AssetDetails d)
        { _assetDetails.Children.Add(new TextBlock { Text = _assetBrowser.DetailState }); return; }
        _assetDetails.Children.Add(new TextBlock { Text = d.Type + "\n" + d.Source + "\n" + d.Guid + "\nartifact schema: " + d.ArtifactSchema + "\nsource schema: " + (d.SourceSchema?.ToString() ?? "none") + "\ntool: " + d.Tool, TextWrapping = Avalonia.Media.TextWrapping.Wrap });
        foreach (AssetGuid guid in d.Dependencies)
        { var button = new Button { Content = guid.ToString() }; button.Click += (_, _) => ReadAsset(guid); _assetDetails.Children.Add(button); }
    }

    private void SyncPreviewSelection(bool invalidate = false)
    {
        var mesh = _previewSession is not null && _assetTabs.SelectedIndex == 1 &&
            _assetBrowser.State == "Ready" && _assetsList.SelectedItem is AssetSummary { Type: AssetType.Mesh } item
            ? item.Guid : (AssetGuid?)null;
        if (_previewRequests.SetSelection(mesh, invalidate)) ClearPreview();
        UpdatePreviewButton();
    }

    private void EndPreviewSession()
    {
        _previewSession = null;
        SyncPreviewSelection(invalidate: true);
    }

    private void ClearPreview()
    {
        var previous = _previewImage.Source as Bitmap;
        _previewImage.Source = null;
        _previewImage.IsVisible = false;
        previous?.Dispose();
        _previewState.Text = "";
        _previewRetry = _previewUnavailable = false;
    }

    private void UpdatePreviewButton()
    {
        _previewMesh.Content = _previewRetry ? "Retry preview" : "Preview geometry";
        _previewMesh.IsEnabled = !_previewUnavailable && _previewRequests.CanRequest;
    }

    private void RequestMeshPreview()
    {
        if (!_previewMesh.IsEnabled || !_previewRequests.Request()) return;
        ClearPreview();
        _previewState.Text = "Loading geometry preview…";
        UpdatePreviewButton();
        StartNextPreview();
    }

    private async void StartNextPreview()
    {
        if (_previewSession is not { } session || _previewRequests.TakeNext() is not { } intent) return;
        // One worker owns each ordinary SDK connection from setup through disposal.
        // Window closure invalidates its intent without waiting on that worker.
        var result = await Task.Run(() => ReadMeshPreview(session, intent));
        if (_previewRequests.Settle(intent) && ReferenceEquals(_previewSession, session))
        {
            if (result.Image is { } image)
            {
                try
                {
                    using var stream = new MemoryStream(image.PngBytes, writable: false);
                    _previewImage.Source = new Bitmap(stream);
                    _previewImage.IsVisible = true;
                    _previewState.Text = "Geometry preview · unlit vertex colors";
                }
                catch (Exception error)
                {
                    ClearPreview();
                    _previewState.Text = "Could not decode the geometry preview. " + error.Message;
                    _previewRetry = true;
                }
            }
            else
            {
                _previewState.Text = result.Message;
                _previewRetry = result.Retry;
                _previewUnavailable = !result.Retry;
            }
            UpdatePreviewButton();
        }
        StartNextPreview();
    }

    private (MeshPreview? Image, string? Message, bool Retry) ReadMeshPreview(SessionInfo session, AssetPreviewIntent intent)
    {
        try
        {
            if (!_previewRequests.CanSend(intent)) return default;
            using var preview = RemoteSession.Connect(session);
            if (!_previewRequests.CanSend(intent)) return default;
            var result = preview.Assets.PreviewMesh(intent.Guid);
            if (result.IsOk) return (result.Value, null, false);
            return result.Error switch
            {
                PreviewError.Unsupported => (null, "Geometry preview requires an updated Runtime.", false),
                PreviewError.Unavailable => (null, "Runtime has no usable geometry preview context.", false),
                PreviewError.CatalogUnavailable => (null, "Catalog unavailable. Restart Runtime after repairing the build.", false),
                PreviewError.Busy => (null, "Runtime is busy with another preview. Retry when ready.", true),
                PreviewError.TooLarge => (null, "This mesh exceeds the geometry preview size limit.", false),
                _ => (null, "Geometry preview failed: " + result.Error + ". See the Runtime log for details.", true),
            };
        }
        catch (RemoteException error)
        {
            return (null, "Geometry preview connection failed: " + error.Message, true);
        }
    }

    private Control BuildSavedScenePanel()
    {
        var panel = new StackPanel { Margin = new Thickness(8), Spacing = 8 };
        panel.Children.Add(new TextBlock
        {
            Text = "Checkout: " + _options.SavedSceneCheckout + "\nVisual Studio 18 2026 · x64 · Debug",
            TextWrapping = Avalonia.Media.TextWrapping.Wrap,
        });
        panel.Children.Add(_savedHint);
        panel.Children.Add(_savedRefresh);
        panel.Children.Add(_savedDiscovery);
        panel.Children.Add(_savedList);
        panel.Children.Add(new TextBlock { Text = "Committed managed import root (optional)" });
        panel.Children.Add(_savedManaged);
        panel.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { _savedBuild, _savedOpen } });
        panel.Children.Add(_savedBuildResult);
        panel.Children.Add(_savedOpenResult);
        _savedRefresh.Click += (_, _) => RefreshSavedScenes();
        _savedList.SelectionChanged += (_, _) => UpdateSavedSceneButtons();
        _savedManaged.TextChanged += (_, _) =>
        {
            if (_savedAttempts.Busy) return;
            if (_savedAttempts.Result is not null)
                InvalidateSavedResult("Managed input changed. Build again.");
            UpdateSavedSceneButtons();
        };
        _savedBuild.Click += async (_, _) => await BuildSelectedSavedScene();
        _savedOpen.Click += async (_, _) => await OpenSelectedSavedScene();
        return new ScrollViewer { Content = panel };
    }

    private void ShowSavedSceneReceipt(SavedSceneReceipt? receipt)
    {
        if (_savedWorkspace is null || receipt is null) return;
        _savedHint.Text = $"Receipt for {receipt.Name}: {receipt.Source} ({receipt.Scene}). " +
            "Refresh and select the local unit explicitly. A matching name does not verify the checkout.";
    }

    private SavedSceneInput? SelectedSavedInput() => _savedList.SelectedItem is string name
        ? new(name, string.IsNullOrWhiteSpace(_savedManaged.Text) ? null : _savedManaged.Text.Trim())
        : null;

    private void UpdateSavedSceneButtons()
    {
        bool idle = !_savedAttempts.Busy;
        SavedSceneInput? input = SelectedSavedInput();
        _savedList.IsEnabled = _savedManaged.IsEnabled = _savedRefresh.IsEnabled = idle;
        _savedBuild.IsEnabled = _savedAttempts.CanBuild(input);
        _savedOpen.IsEnabled = _savedAttempts.CanOpen(input);
    }

    private void InvalidateSavedResult(string why)
    {
        _savedAttempts.Invalidate();
        _savedOpenResult.Text = "Stale. " + why;
    }

    private void RefreshSavedScenes()
    {
        if (_savedWorkspace is null || _savedAttempts.Busy) return;
        string? selected = _savedList.SelectedItem as string;
        string[] names;
        try
        {
            bool missing;
            (names, missing) = _savedWorkspace.Discover();
            _savedDiscovery.Text = missing ? "The checkout has no assets/scenes/saved directory." :
                names.Length == 0 ? "No complete saved scene units." : "Select a local saved scene.";
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e))
        {
            names = [];
            _savedDiscovery.Text = "Cannot read saved scenes: " + e.Message;
        }
        _savedList.ItemsSource = names;
        _savedList.SelectedItem = names.Contains(selected, StringComparer.Ordinal) ? selected : null;
        if (_savedAttempts.Result is { } result && !names.Contains(result.Input.Name, StringComparer.Ordinal))
            InvalidateSavedResult("The built unit is no longer listed. Build again after restoring it.");
        UpdateSavedSceneButtons();
    }

    private void ShowSavedBuild(SavedSceneInput input, SavedSceneBuildReport report)
    {
        _savedBuildResult.Text = $"{report.Phase}: {report.Detail}\nScene: {input.Name}\n" +
            $"Visual Studio 18 2026 · x64 · Debug\nManaged root: {input.ManagedRoot ?? "(none)"}\n" +
            $"Build directory: {report.Directory}\nLog: {(report.Log.Length == 0 ? "(unavailable)" : report.Log)}" +
            (report.Result is { } built ? "\nRuntime: " + built.Executable : "");
    }

    private async Task BuildSelectedSavedScene()
    {
        if (_savedWorkspace is not { } workspace || SelectedSavedInput() is not { } input ||
            !_savedAttempts.BeginBuild(input)) return;
        _savedOpenResult.Text = "";
        _savedBuildResult.Text = "Preparing a fresh build for " + input.Name;
        UpdateSavedSceneButtons();
        var progress = new Progress<SavedSceneBuildReport>(report =>
        {
            if (_savedAttempts.Building) ShowSavedBuild(input, report);
        });
        var result = await Task.Run(() => workspace.BuildAsync(input, ((IProgress<SavedSceneBuildReport>)progress).Report));
        _savedAttempts.FinishBuild(result.Result);
        ShowSavedBuild(input, result);
        UpdateSavedSceneButtons();
    }

    private async Task OpenSelectedSavedScene()
    {
        if (_savedWorkspace is not { } workspace || SelectedSavedInput() is not { } input ||
            _savedAttempts.BeginOpen(input) is not { } result) return;
        _savedOpenResult.Text = "Starting a separate Editor for " + input.Name;
        UpdateSavedSceneButtons();
        Process? child = null;
        try
        {
            child = await Task.Run(() =>
            {
                if (!workspace.IsCurrent(result)) return null;
                return Process.Start(SavedSceneWorkspace.ChildStart(_options.SavedSceneAppHost!, result.Executable))
                    ?? throw new IOException("The Editor process did not start.");
            });
            if (child is null)
                InvalidateSavedResult("The saved unit or executable changed or is unavailable. Build again.");
            else
                _savedOpenResult.Text = $"Editor started for {input.Name}. Check its window for Runtime and document readiness.";
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e))
        {
            _savedOpenResult.Text = "Editor start failed. Retry Open if the build is still current. " + e.Message;
        }
        finally
        {
            _savedAttempts.FinishOpen(input.Name, child is not null);
            UpdateSavedSceneButtons();
        }
        if (child is not null)
        {
            using (child)
                await child.WaitForExitAsync();
            if (ReferenceEquals(_savedAttempts.Result, result) &&
                !await Task.Run(() => workspace.IsCurrent(result)) &&
                ReferenceEquals(_savedAttempts.Result, result))
                InvalidateSavedResult("The child closed and the saved unit or executable changed. Build again.");
            _savedAttempts.ChildExited(input.Name);
            UpdateSavedSceneButtons();
        }
    }

    private string NewSceneHint() => _options.AllowSceneSaveAs
        ? "New Scene saves a camera-only scene and leaves the current scene open."
        : "New Scene requires an Editor launch with --allow-scene-save-as.";

    private void UpdateNewSceneButton() => _newScene.IsEnabled = _options.AllowSceneSaveAs &&
        _worker is not null && _newSceneSessionReady && !_referenceSessionFailed && _newScenePendingName is null &&
        Codec.ValidSaveAsName(_newSceneName.Text);

    private void CreateScene()
    {
        if (!_newScene.IsEnabled || _worker is not { } worker || _newSceneName.Text is not { } name ||
            !Codec.ValidSaveAsName(name)) return;
        long request = ++_newSceneRequest;
        _newScenePendingName = name;
        _newSceneState.Text = $"Creating {name}…";
        UpdateNewSceneButton();
        worker.CreateScene(name, (text, receipt) =>
        {
            if (!ReferenceEquals(_worker, worker) || request != _newSceneRequest) return;
            _newScenePendingName = null;
            _newSceneState.Text = text;
            ShowSavedSceneReceipt(receipt);
            UpdateNewSceneButton();
        });
    }

    private void UpdateSaveAsButton() => _saveAs.IsEnabled = _worker is not null && _canAddMeshToDocument &&
        !_referenceSessionFailed && !_saveAsPending && Codec.ValidSaveAsName(_saveAsName.Text);

    private void SaveSceneCopy()
    {
        if (!_saveAs.IsEnabled || _worker is not { } worker || _saveAsName.Text is not { } name || !Codec.ValidSaveAsName(name)) return;
        long request = ++_saveAsRequest, lifetime = _meshDocumentLifetime;
        _saveAsPending = true;
        _saveAsState.Text = "Saving copy…";
        UpdateSaveAsButton();
        worker.SaveAs(name, lifetime, (text, receipt) =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed || request != _saveAsRequest ||
                lifetime != worker.MeshDocumentLifetime) return;
            _saveAsPending = false;
            _saveAsState.Text = text;
            ShowSavedSceneReceipt(receipt);
            UpdateSaveAsButton();
        });
    }

    private AssetSummary? SelectedMesh() => _assetBrowser.State == "Ready" &&
        _assetsList.SelectedItem is AssetSummary { Type: AssetType.Mesh } asset ? asset : null;

    private void UpdateAddMeshButton()
    {
        bool meshSelected = SelectedMesh() is not null;
        _addMesh.IsEnabled = meshSelected && _worker is not null && _canAddMeshToDocument &&
            !_referenceSessionFailed && !_addingMesh && _gizmoGrab is null;
        ToolTip.SetTip(_addMesh, meshSelected && !_canAddMeshToDocument
            ? "Open a hand-authored scene to add a mesh." : null);
    }

    private void AddMeshToScene()
    {
        if (!_addMesh.IsEnabled || _worker is not { } worker || SelectedMesh() is not { } mesh) return;
        long request = ++_meshRequest;
        long lifetime = _meshDocumentLifetime;
        _addingMesh = true;
        _addMeshState.Text = "Adding mesh…";
        UpdateAddMeshButton();
        worker.AddMesh(mesh.Guid, lifetime, (node, notice) =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed || request != _meshRequest ||
                lifetime != worker.MeshDocumentLifetime) return;
            _addingMesh = false;
            _addMeshState.Text = notice;
            if (node is EntityGuid created && _hierarchyRows.Any(row => row.Node == created)) SetSelection(created, true);
            UpdateAddMeshButton();
        });
    }

    private void UpdateRebuildButtons()
    {
        bool available = _worker is not null && !_referenceSessionFailed && !_rebuildPending;
        _rebuildStart.IsEnabled = available && !_rebuildRunning && _assetBrowser.State == "Ready" &&
                                  _assetsList.SelectedItem is AssetSummary;
        _rebuildRefresh.IsEnabled = available;
    }

    private void StartAssetRebuild()
    {
        if (!_rebuildStart.IsEnabled || _worker is not { } worker ||
            _assetsList.SelectedItem is not AssetSummary selected) return;
        _rebuildPending = true; _rebuildJob = null;
        _rebuildState.Text = "Requesting rebuild…";
        UpdateRebuildButtons();
        worker.RebuildAsset(selected.Guid, result =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _rebuildPending = false;
            if (result.IsOk) ShowRebuild(result.Value!);
            else _rebuildState.Text = "Rebuild refused: " + result.Error;
            UpdateRebuildButtons();
        });
    }

    private void ReadRebuildStatus()
    {
        if (!_rebuildRefresh.IsEnabled || _worker is not { } worker) return;
        _rebuildPending = true;
        UpdateRebuildButtons();
        worker.QueryRebuild(_rebuildJob, result =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _rebuildPending = false;
            if (result.IsOk)
            {
                _activeAssetGeneration = result.Value!.ActiveGeneration;
                ShowRebuild(result.Value.Job);
            }
            else
            {
                _rebuildPoll.Stop(); _rebuildRunning = false;
                if (result.Error == RebuildError.UnknownJob) _rebuildJob = null;
                _rebuildState.Text = "Rebuild status: " + result.Error;
            }
            UpdateRebuildButtons();
        });
    }

    private void ShowRebuild(RebuildJob? job)
    {
        _rebuildJob = job?.Id;
        _rebuildRunning = job?.State == RebuildState.Running;
        if (_rebuildRunning) _rebuildPoll.Start(); else _rebuildPoll.Stop();
        string state = job is null ? "No admitted job" : $"Rebuild {job.Id} · {job.State}";
        if (job?.Failure is RebuildFailure failure)
            state += $" · {failure.Stage}/{failure.Code}: {failure.Diagnostic}";
        if (job?.Generation is GenerationId published)
            state += $" · published {published}. Restart Runtime explicitly with --asset-generation {published}.";
        if (_activeAssetGeneration is GenerationId active) state += $" Active generation: {active}.";
        _rebuildState.Text = state;
    }

    private void UpdateImportButtons()
    {
        bool available = _worker is not null && !_referenceSessionFailed && !_importPending;
        _importRefresh.IsEnabled = available;
        _importRegister.IsEnabled = _importResume.IsEnabled = available && !_importRunning && Codec.ValidImportName(_importName.Text);
    }

    private void StartImport(ImportMode mode)
    {
        if (!_importRegister.IsEnabled || _worker is not { } worker || _importName.Text is not { } name) return;
        _importPending = true;
        _importJob = null;
        _importState.Text = "Requesting import…";
        UpdateImportButtons();
        worker.ImportAsset(name, mode, result =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _importPending = false;
            if (result.IsOk) ShowImport(result.Value!);
            else _importState.Text = "Import refused: " + result.Error;
            UpdateImportButtons();
        });
    }

    private void ReadImportStatus()
    {
        if (!_importRefresh.IsEnabled || _worker is not { } worker) return;
        _importPending = true;
        UpdateImportButtons();
        worker.QueryImport(_importJob, result =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _importPending = false;
            if (result.IsOk) ShowImport(result.Value!.Job);
            else
            {
                _importPoll.Stop();
                _importRunning = false;
                _importState.Text = result.Error == ImportError.UnknownJob ? $"Import {_importJob} outcome unknown." :
                    "Import status refused: " + result.Error;
                if (result.Error == ImportError.UnknownJob) _importJob = null;
            }
            UpdateImportButtons();
        });
    }

    private void ShowImport(ImportJob? job)
    {
        _importJob = job?.Id;
        _importRunning = job?.State == RebuildState.Running;
        if (_importRunning) _importPoll.Start(); else _importPoll.Stop();
        string text = job is null ? "No admitted import" : $"Import {job.Name} ({job.Id}) · {job.State}";
        if (job?.Failure is ImportFailure failure)
        {
            text += $" · {failure.Stage}/{failure.Code}: {failure.Diagnostic} · registration {failure.Registration}";
            if (failure.RegisteredRootGuid is AssetGuid root) text += $" ({root}). Resume publication after correction.";
        }
        if (job?.Generation is GenerationId generation)
            text += $" · published {generation}. Restart Runtime explicitly with --asset-generation {generation}.";
        _importState.Text = text;
    }

    private void OnInspectorChanged(InspectorSnapshot snapshot)
    {
        if (_referenceBusy) return; // the worker owns the captured target; refresh after settlement/cancel
        // An observed external rename cancels a name draft immediately. Other
        // field refreshes retain the existing deferral while a user is typing.
        if (_nameBox?.IsFocused == true && _inspectorShown is InspectorSnapshot shown &&
            (snapshot.Subject != shown.Subject || snapshot.Name != shown.Name))
        {
            BuildInspector(snapshot);
            return;
        }
        if (_inspectorShown is not null && snapshot.Subject == _inspectorShown.Subject && TopLevel.GetTopLevel(this)?.FocusManager?.GetFocusedElement() is TextBox focused &&
            _inspectorPanel.GetVisualDescendants().Contains(focused))
        {
            _inspectorDeferred = snapshot;
            return;
        }
        BuildInspector(snapshot);
    }

    private void BuildInspector(InspectorSnapshot snapshot)
    {
        _inspectorShown = snapshot;
        _inspectorDeferred = null;
        _nameBox = null;
        _inspectorPanel.Children.Clear();
        if (snapshot.Subject is not EntityGuid subject)
        {
            _inspectorPanel.Children.Add(new TextBlock { Text = "Select a node in the Hierarchy.", Opacity = 0.6 });
            return;
        }
        _inspectorPanel.Children.Add(new TextBlock { Text = "Node " + subject, FontWeight = Avalonia.Media.FontWeight.SemiBold, TextWrapping = Avalonia.Media.TextWrapping.Wrap });
        var nameRow = new Grid { ColumnDefinitions = new ColumnDefinitions("90,*"), Margin = new Thickness(0, 2) };
        nameRow.Children.Add(new TextBlock { Text = "Name", VerticalAlignment = VerticalAlignment.Center });
        var nameBox = new TextBox { Text = snapshot.Name, PlaceholderText = "Unnamed", MinWidth = 80 };
        _nameBox = nameBox;
        Grid.SetColumn(nameBox, 1);
        nameRow.Children.Add(nameBox);
        _inspectorPanel.Children.Add(nameRow);
        nameBox.KeyDown += (_, e) =>
        {
            if (e.Key == Key.Enter)
            {
                string name = nameBox.Text ?? "";
                if (name != snapshot.Name) _worker?.CommitName(subject, snapshot.Name, name);
                nameBox.Text = snapshot.Name;
                Focus();
                e.Handled = true;
            }
            else if (e.Key == Key.Escape)
            {
                nameBox.Text = snapshot.Name;
                Focus();
                e.Handled = true;
            }
        };
        nameBox.LostFocus += (_, _) =>
        {
            nameBox.Text = snapshot.Name;
            if (_inspectorDeferred is InspectorSnapshot waiting) Dispatcher.UIThread.Post(() =>
            {
                if (ReferenceEquals(_inspectorDeferred, waiting)) BuildInspector(waiting);
            });
        };
        foreach (var (component, name) in snapshot.Components)
        {
            var header = new DockPanel { Margin = new Thickness(0, 8, 0, 0) };
            if (name != "Transform")
            {
                var remove = new Button { Content = "Remove", Padding = new Thickness(6, 0) };
                TypeId id = component;
                remove.Click += (_, _) => _worker?.RemoveComponent(id);
                DockPanel.SetDock(remove, Dock.Right);
                header.Children.Add(remove);
            }
            header.Children.Add(new TextBlock { Text = name, FontWeight = Avalonia.Media.FontWeight.SemiBold, VerticalAlignment = VerticalAlignment.Center });
            _inspectorPanel.Children.Add(header);
            foreach (InspectorField field in snapshot.Fields.Where(f => f.Component == component)) _inspectorPanel.Children.Add(FieldRow(field));
        }
        if (snapshot.Addable.Count > 0)
        {
            var add = new DockPanel { Margin = new Thickness(0, 10, 0, 0) };
            var choice = new ComboBox { ItemsSource = snapshot.Addable.Select(a => a.Name).ToList(), SelectedIndex = 0, MinWidth = 140 };
            var button = new Button { Content = "Add component", Margin = new Thickness(6, 0, 0, 0) };
            button.Click += (_, _) =>
            {
                if (choice.SelectedIndex >= 0) _worker?.AddComponent(snapshot.Addable[choice.SelectedIndex].Id);
            };
            DockPanel.SetDock(button, Dock.Right);
            add.Children.Add(button);
            add.Children.Add(choice);
            _inspectorPanel.Children.Add(add);
        }
    }

    private void UpdateReferenceButtons()
    {
        foreach (Button b in _inspectorPanel.GetVisualDescendants().OfType<Button>().Where(b => b.Tag as string == "asset chooser"))
            b.IsEnabled = !_referenceBusy && !_referenceSessionFailed;
    }

    private void BeginAssetReference(InspectorField field, Button launchControl, EntityGuid? sourceSubject)
    {
        var worker = _worker;
        if (_referenceBusy || _referenceSessionFailed || worker is null || _selection.Node is not EntityGuid node) return;
        if (sourceSubject != node || _inspectorShown?.Subject != node)
        { _editLine.Text = "choose asset refused: the Inspector target changed"; return; }
        _referenceBusy = true; _referenceFocus = launchControl; long launch = ++_referenceLaunch;
        _inspectorDeferred = null;
        UpdateReferenceButtons();
        worker.CaptureAssetReference(node, field.Component, field.Field.Id, (target, error) =>
        {
            if (!ReferenceEquals(worker, _worker) || launch != _referenceLaunch)
            { if (target is not null) worker.CancelAssetReference(target.Request); return; }
            if (target is null) { _editLine.Text = error; FinishAssetReference(); return; }
            var chooser = new AssetReferencePickerWindow(worker, target, (listRequest, guid) =>
            {
                _referenceWindow = null;
                worker.ConfirmAssetReference(target.Request, listRequest, guid, () =>
                { if (ReferenceEquals(worker, _worker) && launch == _referenceLaunch) FinishAssetReference(); });
            }, () =>
            {
                worker.CancelAssetReference(target.Request);
                if (ReferenceEquals(worker, _worker) && launch == _referenceLaunch)
                { _referenceWindow = null; FinishAssetReference(); }
            });
            _referenceWindow = chooser;
            _ = chooser.ShowDialog(this);
        });
    }

    private void FinishAssetReference()
    {
        _referenceBusy = false; _inspectorDeferred = null;
        UpdateReferenceButtons();
        var worker = _worker; long launch = _referenceLaunch; var focus = _referenceFocus;
        Dispatcher.UIThread.Post(() =>
        {
            if (worker is null || !ReferenceEquals(worker, _worker) || launch != _referenceLaunch || _referenceSessionFailed) return;
            focus?.Focus(); // return to the launcher, so an obsolete focused TextBox cannot defer the fresh snapshot
            _inspectorDeferred = null;
            worker.RefreshInspector();
        });
    }

    private Control FieldRow(InspectorField field)
    {
        EntityGuid? sourceSubject = _inspectorShown?.Subject;
        var row = new Grid { ColumnDefinitions = new ColumnDefinitions("110,*,Auto") };
        row.Children.Add(new TextBlock { Text = field.Label, VerticalAlignment = VerticalAlignment.Center, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis });
        Control editor;
        if (field.Control == FieldControl.Enum && field.Editable)
        {
            var combo = new ComboBox { ItemsSource = field.Choices.Select(c => c.Name).ToList(), SelectedItem = field.Text, HorizontalAlignment = HorizontalAlignment.Stretch };
            combo.SelectionChanged += (_, _) =>
            {
                if (combo.SelectedItem is string chosen && chosen != field.Text) _worker?.CommitField(field.Path, chosen);
            };
            editor = combo;
        }
        else
        {
            var box = new TextBox { Text = field.Text, IsReadOnly = !field.Editable, Opacity = field.Editable ? 1.0 : 0.6 };
            if (field.Optional && !field.Present) box.PlaceholderText = "(absent)";
            string original = field.Text;
            void ReplaceDraft() { box.Text = field.Text; original = field.Text; }
            if (InspectorModel.CanChooseAsset(field))
            {
                row.ColumnDefinitions = new ColumnDefinitions("110,*,Auto,Auto");
                var choose = new Button { Content = "Choose…", Tag = "asset chooser", IsEnabled = !_referenceBusy && !_referenceSessionFailed };
                // The ancestor tunnel runs before Button's class focus handler.
                row.AddHandler(InputElement.PointerPressedEvent, (_, e) =>
                {
                    if (e.Source is Visual source && (ReferenceEquals(source, choose) || source.GetVisualAncestors().Contains(choose)) &&
                        e.GetCurrentPoint(choose).Properties.IsLeftButtonPressed) ReplaceDraft();
                }, RoutingStrategies.Tunnel);
                choose.Click += (_, _) => BeginAssetReference(field, choose, sourceSubject);
                Grid.SetColumn(choose, 2); row.Children.Add(choose);
                box.KeyDown += (_, e) =>
                { if (e.Key == Key.F4 && e.KeyModifiers == KeyModifiers.None) { e.Handled = true; ReplaceDraft(); BeginAssetReference(field, choose, sourceSubject); } };
            }
            void Commit()
            {
                if (!field.Editable || (box.Text ?? "") == original) return;
                original = box.Text ?? "";
                _worker?.CommitField(field.Path, original);
            }
            box.KeyDown += (_, e) =>
            {
                if (e.Key == Avalonia.Input.Key.Enter) Commit();
                if (e.Key == Avalonia.Input.Key.Escape) box.Text = original;
            };
            box.LostFocus += (_, _) =>
            {
                Commit();
                if (_inspectorDeferred is InspectorSnapshot waiting) Dispatcher.UIThread.Post(() =>
                {
                    if (ReferenceEquals(_inspectorDeferred, waiting)) BuildInspector(waiting);
                });
            };
            editor = box;
        }
        Grid.SetColumn(editor, 1);
        row.Children.Add(editor);
        if (field.Optional && field.Present && field.Editable)
        {
            var clear = new Button { Content = "×", Padding = new Thickness(6, 0), Margin = new Thickness(4, 0, 0, 0) };
            clear.Click += (_, _) => _worker?.ClearField(field.Path);
            Grid.SetColumn(clear, InspectorModel.CanChooseAsset(field) ? 3 : 2);
            row.Children.Add(clear);
        }
        return row;
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
        EndPreviewSession();
        _uiTicker.Stop();
        _importPoll.Stop();
        _rebuildPoll.Stop();
        ++_referenceLaunch; _referenceWindow?.Close(); _referenceWindow = null;
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
