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
using Avalonia.Controls.Primitives;
using Avalonia.Automation;
using Avalonia.Media;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.VisualTree;
using Avalonia.Layout;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Atlantis.Editor.Model;
using Atlantis.Gameplay.Assets;
using Doc = Atlantis.Gameplay.Authoring;
using SceneAddress = Atlantis.Gameplay.Authoring.SceneAddress;
using SiblingDirection = Atlantis.Gameplay.Authoring.SiblingDirection;
using SiblingPlacement = Atlantis.Gameplay.Authoring.SiblingPlacement;
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
    private SavedSceneBuild? _savedFresh;
    private System.Threading.CancellationTokenSource? _savedCheck;
    private readonly SavedSceneWorkspace? _savedWorkspace;
    private readonly ListBox _savedList = new() { Height = 140, SelectionMode = SelectionMode.Single };
    private readonly Button _savedRefresh = new() { Content = "Refresh" };
    private readonly Button _savedBuild = new() { Content = "Build", IsEnabled = false };
    private readonly Button _savedOpen = new() { Content = "Open", IsEnabled = false };
    private readonly TextBox _savedManaged = new() { PlaceholderText = "Absolute committed managed import root (optional)" };
    private readonly TextBlock _savedDiscovery = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly TextBlock _savedHint = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly TextBox _savedBuildResult = new() { IsReadOnly = true, AcceptsReturn = true, TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly TextBlock _savedOpenResult = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _savedBuildSummary = new() { TextWrapping = TextWrapping.Wrap };

    // Plan 0068 M2: the Hierarchy over the authoring document, the one shared selection, and the edit controls.
    private readonly ListBox _entities = new();
    private readonly TextBox _hierarchySearch = new() { PlaceholderText = "Search by name" };
    private readonly ComboBox _hierarchyComponent = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly Button _clearHierarchyFilter = new() { Content = "Clear filters" };
    private readonly Button _locateHierarchySelection = new() { Content = "Locate selection" };
    private readonly TextBlock _hierarchyFilterStatus = EditorVisuals.Secondary("");
    private readonly TextBlock _hiddenBatchStatus = EditorVisuals.Secondary("Selected nodes are hidden. Locate selection to edit them.");
    private readonly List<Action> _batchButtons = new();
    private readonly List<Button> _inspectorStructureButtons = new();
    private static readonly HierarchyComponent AllHierarchyComponents = new(null, "All components");
    private const string FilteredStructureReason = "Clear filters to change scene structure.";
    private const string HiddenBatchReason = "Selected nodes are hidden. Locate selection to edit them.";
    private HierarchyFilter _hierarchyFilter = HierarchyFilter.Empty;
    private HierarchyProjection _hierarchyProjection = HierarchyFilter.Empty.Project(Array.Empty<HierarchyRow>());
    private HierarchyScrollAnchor? _hierarchyBeforeFilter;
    private ulong? _hierarchyViewGeneration;
    private long _hierarchyScrollIntent;
    private bool _updatingHierarchyControls;
    private readonly Selection _selection = new();
    private readonly Button _create = new() { Content = "Create" };
    private readonly Dictionary<SceneObjectPreset, MenuItem> _createItems = new();
    private long _createRequest;
    private bool _creatingObject;
    private readonly Button _duplicate = new() { Content = "Duplicate subtree", IsEnabled = false };
    private readonly Button _destroy = new() { Content = "Destroy subtree" };
    private readonly TextBlock _duplicateState = EditorVisuals.Secondary("");
    private bool _duplicating;
    private long _duplicateRequest;
    private readonly Button _toRoot = new() { Content = "Move to root" };
    private readonly Button _moveUp = new() { Content = "Move up", IsEnabled = false };
    private readonly Button _moveDown = new() { Content = "Move down", IsEnabled = false };
    private readonly Button _pickUp = new() { Content = "Pick up" };
    private readonly Button _moveHere = new() { Content = "Move under selection", IsEnabled = false };
    // Plan 0068 M5: the history's Undo and Redo, labelled with the edit they would step over.
    private readonly Button _undo = new() { Content = "Undo", IsEnabled = false };
    private readonly Button _redo = new() { Content = "Redo", IsEnabled = false };
    // Plan 0068 M6 (Spec 0068 R9): the document's save and the Runtime's transport. The Runtime starts playing, so
    // Pause is the enabled action until the first play state says otherwise.
    private readonly Button _save = new() { Content = "Save" };
    private readonly Button _reopen = new() { Content = "Reopen from Source", IsEnabled = false };
    private readonly TextBlock _documentLine = EditorVisuals.Secondary("Document is opening");
    private DocumentObservation _documentState = new(DocumentAvailability.Starting);
    private readonly Button _reviewRecovery = new() { Content = "Review Recovery Copy", IsVisible = false };
    private readonly TextBlock _recoveryLine = EditorVisuals.Secondary("");
    private RecoveryObservation? _recoveryView;
    private SceneRecoveryWindow? _recoveryWindow;
    private (EntityGuid Document, EntityGuid Copy)? _offeredRecovery;
    private bool _recoveryDecision, _recoveryUnknown;

    private sealed record SceneInput(TextBox Box, DocumentDraft Draft, int Caret, int Start, int End);
    private readonly DocumentDraftProtection _draftProtection = new();
    private SceneInput? _sceneInput;
    private SceneInput? _leaveInput;
    private TextBox? _invalidatedInput;
    private DocumentLeaveWindow? _leaveDialog;
    private Action? _pendingInspectorBlur;
    private bool _leaveInputStale, _leaveInProgress, _leaveArmed, _leaveCloseApproved, _shuttingDown;
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
    private readonly TaskIssues _taskIssues = new();
    private TaskIssuesPanel? _tasksPanel;
    private long _importTask, _rebuildTask, _materialTask;
    private readonly Button _retryApply = new() { Content = "Apply current document / Retry", IsEnabled = false };
    private bool _allowGizmoPreview;
    private readonly Button _play = new() { Content = "Play", IsEnabled = false };
    private readonly Button _pause = new() { Content = "Pause" };
    private readonly Button _step = new() { Content = "Step", IsEnabled = false };
    private readonly TextBlock _applyLine = EditorVisuals.Ellipsis("authoring: connecting...");
    private readonly TextBlock _editLine = EditorVisuals.Ellipsis("");
    private readonly TextBlock _connectionLine = EditorVisuals.Ellipsis("");
    private readonly TextBlock _assetTitle = EditorVisuals.Ellipsis("Selected asset");
    private IReadOnlyList<HierarchyRow> _hierarchyRows = Array.Empty<HierarchyRow>();
    private IReadOnlyList<Marker> _markers = Array.Empty<Marker>();
    private ReparentSource? _pickedUp;
    private bool _reparentDocumentOpen;
    private bool _reparentPending;
    private bool _siblingMovePending;
    private long _reparentRequest;
    private readonly TextBlock _reparentState = EditorVisuals.Secondary("");
    private (HierarchyRow Row, PointerPressedEventArgs Event, Point Position)? _hierarchyPress;
    private (ReparentSource Source, string Token, bool Relative)? _hierarchyDrag;
    private (HierarchyRow Row, SiblingPlacement? Placement)? _advertisedHierarchyDrop;
    private Point? _hierarchyDropPointer;
    private readonly Canvas _hierarchyDropFeedback = new() { IsHitTestVisible = false, ClipToBounds = true };
    private readonly Border _hierarchyDropLine = new() { Height = 2, Background = EditorVisuals.Focus, IsVisible = false };
    private readonly Border _hierarchyDropLabel = new() { Background = EditorVisuals.Pane, Padding = new Thickness(6, 2), IsVisible = false };
    private ListBoxItem? _reparentTargetRow;
    private static readonly DataFormat<string> ReparentDragFormat = DataFormat.CreateStringApplicationFormat("atlantis-reparent");
    private bool _syncingSelection;
    private ulong _pickRequest;
    private BatchInspectorSnapshot? _batchView;
    private (BatchInspectorSnapshot View, CommonInspectorField Field, string Text)? _batchDraft;
    private readonly Dictionary<string, TextBox> _batchInputs = new();
    private bool _resettingBatchInput;
    // Plan 0068 M4: the Gizmo's settings, the selected node's matrix and availability, and the grab in progress.
    private readonly ComboBox _gizmoMode = new() { ItemsSource = new[] { "Move", "Rotate", "Scale" }, SelectedIndex = 0, MinWidth = 80 };
    private readonly ComboBox _gizmoSpace = new() { ItemsSource = new[] { "World", "Local" }, SelectedIndex = 0, MinWidth = 80 };
    private readonly CheckBox _gizmoSnap = new() { Content = "Snap" };
    private GizmoSnapshot? _gizmoShown;
    private GizmoGrab? _gizmoGrab;
    private GizmoMode _gizmoGrabMode;
    private (long Id, GizmoMode Mode, GizmoGrab Handle)? _groupGrab;
    private (int Mode, int Space, bool? Snap)? _singleGizmoSettings;
    private bool _syncingGizmoSettings;
    // Plan 0068 M3: the Inspector panel, rebuilt from the worker's snapshots (not while a field is being typed in).
    private readonly StackPanel _inspectorPanel = new() { Spacing = 10, Margin = new Thickness(12) };
    private InspectorSnapshot? _inspectorShown;
    private InspectorSnapshot? _inspectorDeferred;
    private BatchInspectorSnapshot? _leaveBatchDeferred;
    private readonly List<Action> _refreshReferenceLabels = new();
    private readonly AssetBrowser _assetBrowser = new();
    private const string ColorTextureLabel = "sRGB color";
    private readonly ComboBox _textureKind = new() { ItemsSource = new[] { ColorTextureLabel, "Normal map", "Metallic-roughness", "Occlusion map" }, SelectedIndex = 0 };
    private readonly SelectableTextBlock _textureFormat = EditorVisuals.Details("");
    private readonly TextBox _textureName = new() { PlaceholderText = "Inbox texture name" };
    private readonly Button _textureRegister = new() { Content = "Register", IsEnabled = false };
    private readonly Button _textureResume = new() { Content = "Resume", IsEnabled = false };
    private readonly Button _textureRefresh = new() { Content = "Refresh", IsEnabled = false };
    private readonly SelectableTextBlock _textureState = EditorVisuals.Details("");
    private readonly DispatcherTimer _texturePoll = new() { Interval = TimeSpan.FromSeconds(1) };
    private sealed class TextureImportView(TextureImportKind kind)
    {
        public TextureImportKind Mode { get; } = kind;
        public string Kind => Mode switch
        {
            TextureImportKind.MetallicRoughness => "Metallic-roughness map",
            TextureImportKind.Occlusion => "Occlusion map",
            TextureImportKind.Normal => "Normal map",
            _ => ColorTextureLabel
        };
        public TextureImportJob? Job;
        public long Task;
        public (string Name, ImportMode Mode)? Target;
        public bool Admission, Polling, Unknown, Writable;
        public ImportError? Error;
        public bool Committed => Job?.Failure?.Registration == RegistrationState.Committed;
        public bool Running => Job?.State == RebuildState.Running;
        public bool BlocksOtherWrites => Admission || Unknown || Running || Committed ||
            Job?.State == RebuildState.Succeeded;
    }
    private readonly TextureImportView[] _textureImports =
        [new(TextureImportKind.Color), new(TextureImportKind.Normal), new(TextureImportKind.MetallicRoughness), new(TextureImportKind.Occlusion)];
    private TextureImportView SelectedTextureImport => _textureImports[Math.Max(0, _textureKind.SelectedIndex)];
    private bool _textureRepair;
    private readonly ListBox _assetsList = new() { Height = 224 };
    private static readonly DataFormat<string> AssetDragFormat = DataFormat.CreateStringApplicationFormat("atlantis-asset-drag");
    private (AssetGuid Guid, PointerPressedEventArgs Event, Point Position)? _assetPress;
    private AssetDragIntent? _assetDrag;
    private bool _assetDropPending;
    private int _pendingSceneFieldEdits;
    private ulong _assetDropRequest, _assetDropInteraction;
    private Border? _assetDropHighlight;
    private readonly Dictionary<SceneAddress, AssetDropField> _assetDropFields = new();
    private (Control Surface, AssetDropField? Field)? _advertisedAssetDrop;
    private readonly TextBlock _rootDropLabel = EditorVisuals.Secondary("Drop mesh to add at scene root");
    private readonly TextBox _assetSearch = new() { PlaceholderText = "Search name, path or GUID" };
    private readonly ComboBox _assetTypes = new() { ItemsSource = new[] { "All types", "Mesh", "Texture", "Material", "Scene", "Environment" }, SelectedIndex = 0 };
    private readonly ComboBox _assetSort = new() { ItemsSource = new[] { "Name A–Z", "Name Z–A" }, SelectedIndex = 0 };
    private readonly ComboBox _assetGroup = new() { ItemsSource = new[] { "No grouping", "By type", "By source" }, SelectedIndex = 0 };
    private readonly Button _assetClearFilters = new() { Content = "Clear filters" };
    private readonly TextBlock _assetsState = new();
    private readonly AssetPreviewRequests _previewRequests = new();
    private readonly Button _previewMesh = new() { Content = "Preview geometry", IsEnabled = false };
    private readonly ComboBox _previewMaterial = new() { PlaceholderText = "Select a material", IsVisible = false };
    private readonly Button _previewPair = new() { Content = "Preview with material", IsEnabled = false, IsVisible = false };
    private readonly TextBlock _previewState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly Image _previewImage = new() { Height = 192, Stretch = Stretch.Uniform, IsVisible = false };
    private IReadOnlyList<AssetSummary>? _previewCatalog;
    private SessionInfo? _previewSession;
    private bool _previewRetry, _previewUnavailable, _previewWithMaterial;
    private readonly Button _newMaterial = new() { Content = "New material", IsEnabled = false };
    private readonly Button _copyMaterial = new() { Content = "Duplicate", IsEnabled = false };
    private readonly Button _editMaterial = new() { Content = "Edit material", IsEnabled = false };
    private readonly Button _materialRefresh = new() { Content = "Refresh material status", IsEnabled = false };
    private readonly Button _materialResume = new() { Content = "Resume publication", IsVisible = false };
    private readonly SelectableTextBlock _materialState = EditorVisuals.Details("");
    private readonly DispatcherTimer _materialPoll = new() { Interval = TimeSpan.FromSeconds(1) };
    private MaterialSettlement _materialSettlement = new();
    private MaterialEditorWindow? _materialWindow;
    private MaterialRead? _selectedMaterial;
    private AssetGuid? _materialReadTarget;
    private bool _materialPolling, _materialCanWrite, _materialSupported, _materialLegacy;
    private bool? _materialSupportsMetallicRoughness;
    private bool _materialResumeWithMetallicRoughness;
    private bool? _materialSupportsOcclusion;
    private bool _materialResumeWithOcclusion;
    private bool? _materialSupportsCombinedMaps;
    private bool _materialResumeWithCombinedMaps;
    private long _materialReadEpoch, _materialSurface;
    private readonly TextBlock _rebuildState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly Button _renameTexture = new() { Content = "Rename…", IsVisible = false };
    private readonly Button _renamePublish = new() { Content = "Publish rename" };
    private readonly Button _renameCancel = new() { Content = "Cancel" };
    private readonly TextBox _renameLabel = new() { PlaceholderText = "Display name", MaxLength = 48 };
    private readonly SelectableTextBlock _renamePath = EditorVisuals.Details("");
    private StackPanel? _renameEditor;
    private GenerationId? _renameDraftGeneration;
    private string? _renameRequestContext;
    private bool _renameUnknown;
    private string _assetWriteLabel = "Rename";
    private readonly Button _moveTexture = new() { Content = "Move…", IsVisible = false };
    private readonly Button _movePublish = new() { Content = "Publish move" };
    private readonly Button _moveCancel = new() { Content = "Cancel" };
    private readonly TextBox _moveDestination = new() { PlaceholderText = "textures/folder/name.png", MaxLength = 257 };
    private readonly SelectableTextBlock _movePaths = EditorVisuals.Details("");
    private StackPanel? _moveEditor;
    private GenerationId? _moveDraftGeneration;
    private readonly Button _removeRegistration = new() { Content = "Remove registration…", IsVisible = false };
    private readonly Button _confirmRemoval = new() { Content = "Confirm removal", IsVisible = false };
    private readonly Button _cancelRemoval = new() { Content = "Cancel check", IsVisible = false };
    private readonly TextBlock _removalState = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    private readonly DispatcherTimer _removalPoll = new() { Interval = TimeSpan.FromSeconds(1) };
    private TextureRemovalCheck? _removalReceipt;
    private bool _removalQueryPending;

    private readonly Button _rebuildStart = new() { Content = "Rebuild selected", IsEnabled = false };
    private readonly TextBlock _reimportInput = EditorVisuals.Secondary("");
    private readonly Button _viewReferences = new() { Content = "View References", IsVisible = false };
    private readonly Button _cancelReferences = new() { Content = "Cancel", IsVisible = false };
    private readonly ListBox _referenceResults = new() { MaxHeight = 170, IsVisible = false };
    private readonly SelectableTextBlock _referenceStatus = EditorVisuals.Details("");
    private readonly TextBlock _referenceScope = EditorVisuals.Secondary("Current Runtime catalog · direct material references");
    private readonly StackPanel _locatedSlots = EditorVisuals.Stack(4);
    private LocatedAssetReference? _shownReference;
    private bool _revealAssetFilters;
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
    private Button? _assetCopyGuid;
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
    private readonly Button _focusSelection = new() { Content = "Focus selection" };
    private readonly ToggleButton _viewportGuides = new() { Content = "Guides", IsEnabled = false };
    private string? _navigationFeedback, _guideFeedback;
    private readonly ComboBox _viewDirection = new()
    {
        ItemsSource = new[] { "View direction", "Front", "Back", "Left", "Right", "Top", "Bottom" }, SelectedIndex = 0,
    };
    private readonly ComboBox _navigationSpeed = new()
    {
        ItemsSource = new[] { "Slow 0.5×", "Normal 1×", "Fast 2×" }, SelectedIndex = 1,
    };
    private readonly TextBlock _viewportNavFeedback = new() { TextTrimming = TextTrimming.CharacterEllipsis, Height = 18 };
    private readonly TextBox _sizeBox = new() { PlaceholderText = "WxH, e.g. 1280x720", Width = 150 };
    private readonly Button _applySize = new() { Content = "Set viewport size" };
    private (int Width, int Height)? _targetSize;
    private int _sizeCorrections;
    private EditorCamera _camera = new();
    private readonly DispatcherTimer _uiTicker = new() { Interval = TimeSpan.FromMilliseconds(16) };
    private readonly List<double> _uiGaps = new();
    private long _lastUiTick = Stopwatch.GetTimestamp();
    private long _uiWindowStart = Stopwatch.GetTimestamp();

    private RuntimeProcess? _runtime;
    private EditorWorker? _worker;
    private SceneSnapshot? _scene;
    private bool _cameraInitialized;
    private bool _navigationSupported, _navigationReady, _navigationPending, _navigationToolbarPointer;
    private long _viewportCameraIntent;
    private CameraPose? _sceneProjection;
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
        Title = "Atlantis Editor · " + options.Scene;
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
        _entities.SelectionMode = SelectionMode.Multiple;
        _entities.AddHandler(KeyDownEvent, OnHierarchyKeyDown, RoutingStrategies.Tunnel);
        _entities.ItemTemplate = new FuncDataTemplate<HierarchyRow>((row, _) =>
        {
            string text = row?.Label ?? "";
            if (row is not null && _hierarchyFilter.Active && !_hierarchyProjection.Matches(row.Node))
                text = "Path · " + text;
            var label = EditorVisuals.Ellipsis(text);
            label.Margin = new Thickness(Math.Min(row?.Depth ?? 0, 7) * 12, 0, 0, 0);
            ToolTip.SetTip(label, text);
            return label;
        });
        _entities.SelectionChanged += (_, _) => OnHierarchySelected();
        _entities.AddHandler(PointerPressedEvent, OnHierarchyPointerPressed, RoutingStrategies.Tunnel);
        _entities.AddHandler(PointerMovedEvent, OnHierarchyPointerMoved, RoutingStrategies.Tunnel);
        _entities.AddHandler(PointerReleasedEvent, (_, _) => _hierarchyPress = null, RoutingStrategies.Tunnel);
        DragDrop.SetAllowDrop(_entities, true);
        DragDrop.AddDragOverHandler(_entities, OnHierarchyDragOver);
        DragDrop.AddDragLeaveHandler(_entities, (_, _) => ClearHierarchyDropFeedback());
        DragDrop.AddDropHandler(_entities, OnHierarchyDrop);

        Content = BuildWorkspace();

        _viewport.WindowCreated += OnViewportWindow;
        _viewport.WindowDestroyed += () =>
        {
            _worker?.SetViewportExtent(0, 0);
            ++_pickRequest;
            _log.Write("viewport host window destroyed");
        };
        _measureFrames.Click += (_, _) =>
        {
            _measureLine = "measuring 600 frames...";
            UpdateStatus();
            _worker?.Measure(600);
        };
        _resetCamera.Click += (_, _) => ResetToSceneCamera();
        _focusSelection.Click += (_, _) => NavigateViewport(_selection.Nodes);
        _viewportGuides.IsCheckedChanged += (_, _) =>
        {
            ShowGuideFeedback(null);
            _worker?.SetGuidesEnabled(_viewportGuides.IsChecked == true);
        };
        foreach (Control control in new Control[] { _resetCamera, _focusSelection, _viewDirection, _navigationSpeed, _viewportGuides })
            control.AddHandler(PointerPressedEvent, (_, _) =>
            {
                _navigationToolbarPointer = true;
                Dispatcher.UIThread.Post(() => _navigationToolbarPointer = false, DispatcherPriority.Input);
            }, RoutingStrategies.Tunnel);
        _viewDirection.SelectionChanged += (_, _) =>
        {
            int selected = _viewDirection.SelectedIndex;
            if (selected <= 0) return;
            _viewDirection.SelectedIndex = 0;
            NavigateViewport(direction: (ViewDirection)(selected - 1));
        };
        UpdateViewportNavigationActions();
        _gizmoMode.SelectionChanged += (_, _) => SendGizmoSettings();
        _gizmoSpace.SelectionChanged += (_, _) => SendGizmoSettings();
        _gizmoSnap.IsCheckedChanged += (_, _) => SendGizmoSettings();
        SetupSceneObjectMenu();
        _duplicate.Click += (_, _) => DuplicateSelectedSubtree();
        ToolTip.SetShowOnDisabled(_duplicate, true);
        _undo.Click += (_, _) => _worker?.Undo();
        _redo.Click += (_, _) => _worker?.Redo();
        _save.Click += (_, _) =>
        {
            SettleInspectorBlur();
            _worker?.Save();
        };
        _reopen.AddHandler(PointerPressedEvent, (_, _) => _leaveArmed = true, RoutingStrategies.Tunnel);
        _reopen.AddHandler(PointerReleasedEvent, (_, _) => Dispatcher.UIThread.Post(() => _leaveArmed = false),
            RoutingStrategies.Tunnel);
        _reopen.Click += async (_, _) => await AskToLeave(reopen: true);
        _reviewRecovery.Click += async (_, _) =>
        {
            if (_recoveryUnknown && _worker is { } worker)
            {
                var checkedState = await worker.CheckOriginalRecovery();
                if (!ReferenceEquals(worker, _worker) || _shuttingDown) return;
                _recoveryLine.Text = checkedState.Diagnostic ?? (checkedState.Status?.Copy?.Pending == true
                    ? "The original Runtime still has a pending copy. Review it again from a live Editor connection."
                    : "No pending recovery copy was observed on the original Runtime. Inspect the current document.");
                return;
            }
            await ReviewRecovery();
        };
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
        Deactivated += (_, _) => CancelGroupGesture();
        AddHandler(GotFocusEvent, (_, e) =>
        {
            if (e.Source is Control next && !ReferenceEquals(next, _reopen) && TopLevel.GetTopLevel(next) == this)
                SettleInspectorBlur();
        }, RoutingStrategies.Bubble);
        AddHandler(KeyDownEvent, (_, e) =>
        {
            if (e.Key == Key.R && e.KeyModifiers == (KeyModifiers.Control | KeyModifiers.Shift) && _reopen.IsEnabled)
            {
                e.Handled = true;
                _ = AskToLeave(reopen: true);
                return;
            }
            if (e.Key == Key.Escape && CancelGroupGesture()) e.Handled = true;
        }, RoutingStrategies.Tunnel);
        KeyDown += (_, e) =>
        {
            if (_leaveInProgress || _recoveryDecision) return;
            // Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z): not while a text box is being edited, which has its own.
            if ((e.KeyModifiers & KeyModifiers.Control) == 0 || FocusManager?.GetFocusedElement() is TextBox) return;
            if (e.Key == Key.Z && (e.KeyModifiers & KeyModifiers.Shift) == 0) { _worker?.Undo(); e.Handled = true; }
            else if (e.Key == Key.Y || e.Key == Key.Z) { _worker?.Redo(); e.Handled = true; }
        };
        _destroy.Click += (_, _) =>
        {
            if (!_hierarchyFilter.Active && SelectedReparentRow() is { } row) _worker?.DestroySubtree(row.Node, row);
        };
        _toRoot.Click += (_, _) =>
        {
            if (CaptureSelectedReparent() is { } source) SubmitReparent(source, null, drag: false);
        };
        _moveUp.Click += (_, _) => MoveSelectedSibling(SiblingDirection.Up);
        _moveDown.Click += (_, _) => MoveSelectedSibling(SiblingDirection.Down);
        _pickUp.Click += (_, _) =>
        {
            _pickedUp = CaptureSelectedReparent();
            if (_pickedUp is { } source) ShowReparentState($"Picked up {source.Node}. Select its new parent.");
            UpdateHierarchyActions();
        };
        _moveHere.Click += (_, _) =>
        {
            if (_pickedUp is { } source && SelectedReparentRow() is { } target)
            {
                _pickedUp = null;
                SubmitReparent(source, target, drag: false);
            }
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
            if (_leaveCloseApproved)
            {
                _leaveCloseApproved = false;
                _shuttingDown = true;
                RetireAssetDrop();
                Shutdown();
                return;
            }
            e.Cancel = true;
            if (_savedAttempts.Busy)
            {
                _savedOpenResult.Text = "Wait for the saved scene operation to finish, then close again.";
                _assetTabs.SelectedIndex = 2;
                return;
            }
            _ = AskToLeave(reopen: false);
        };
        UpdateStatus();
    }

    private Control BuildWorkspace()
    {
        bool diagnosticMode = _options.MeasureDrag || _options.MeasureOverlay;
        EditorVisuals.Glyph(_save, EditorVisuals.Save);
        EditorVisuals.Glyph(_undo, EditorVisuals.Undo, "Undo");
        EditorVisuals.Glyph(_redo, EditorVisuals.Redo, "Redo");
        EditorVisuals.Glyph(_play, EditorVisuals.Play);
        EditorVisuals.Glyph(_pause, EditorVisuals.Pause);
        EditorVisuals.Glyph(_step, EditorVisuals.Step);
        EditorVisuals.Glyph(_create, EditorVisuals.Plus);
        var transport = new Border
        {
            Background = EditorVisuals.Inset, CornerRadius = new CornerRadius(6), Padding = new Thickness(2),
            Child = EditorVisuals.Actions(_play, _pause, _step)
        };
        var commands = EditorVisuals.Bar(EditorVisuals.Columns("*,Auto,*",
            EditorVisuals.Actions(_save, _undo, _redo), transport));

        var sceneFiles = EditorVisuals.Scroll(EditorVisuals.Stack(4,
            EditorVisuals.Actions(_reopen, _reviewRecovery), _recoveryLine,
            EditorVisuals.Actions(_newSceneName, _newScene), _newSceneState,
            EditorVisuals.Actions(_saveAsName, _saveAs), _saveAsState));
        sceneFiles.MaxHeight = 164;
        _newSceneState.Text = NewSceneHint();
        var sceneSection = EditorVisuals.Disclosure("Scene files", sceneFiles);
        _duplicateState.TextWrapping = Avalonia.Media.TextWrapping.Wrap;
        _duplicateState.IsVisible = false;
        var structuralButtons = EditorVisuals.Scroll(EditorVisuals.Actions(
            _duplicate, _destroy, _toRoot, _pickUp, _moveHere, _moveUp, _moveDown));
        structuralButtons.MaxHeight = 130;
        var structural = EditorVisuals.Stack(4, structuralButtons, _duplicateState);
        var actions = new ToggleButton { Content = "Actions" };
        structural.IsVisible = false;
        actions.IsCheckedChanged += (_, _) => structural.IsVisible = actions.IsChecked == true;
        var hierarchy = new DockPanel { Background = EditorVisuals.Pane };
        AddDocked(hierarchy, EditorVisuals.Bar(EditorVisuals.Heading("Hierarchy")), Avalonia.Controls.Dock.Top);
        AddDocked(hierarchy, BuildHierarchyFilters(), Dock.Top);
        var hierarchyCommands = EditorVisuals.Columns("Auto,Auto", _create, actions);
        hierarchyCommands.Margin = new Thickness(4);
        actions.Margin = new Thickness(4, 0, 0, 0);
        AddDocked(hierarchy, hierarchyCommands, Avalonia.Controls.Dock.Top);
        AddDocked(hierarchy, structural, Avalonia.Controls.Dock.Top);
        var assetDropZone = new Border
        {
            Background = EditorVisuals.Inset, BorderBrush = EditorVisuals.Line, BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(4), Child = _rootDropLabel, Padding = new Thickness(4, 2),
        };
        _rootDropLabel.TextWrapping = TextWrapping.NoWrap;
        _rootDropLabel.TextTrimming = TextTrimming.CharacterEllipsis;
        ConfigureAssetDropTarget(assetDropZone, assetDropZone, null);
        AddDocked(hierarchy, assetDropZone, Dock.Top);

        _reparentState.TextWrapping = TextWrapping.Wrap;
        ShowReparentState("");
        AddDocked(hierarchy, _reparentState, Avalonia.Controls.Dock.Top);
        AddDocked(hierarchy, sceneSection, Avalonia.Controls.Dock.Bottom);
        _hierarchyDropFeedback.Children.Add(_hierarchyDropLine);
        _hierarchyDropFeedback.Children.Add(_hierarchyDropLabel);
        hierarchy.Children.Add(new Grid { Children = { _entities, _hierarchyDropFeedback } });
        AutomationProperties.SetName(_entities, "Scene hierarchy");

        var viewCommands = EditorVisuals.Actions(_gizmoMode, _gizmoSpace, _gizmoSnap, _focusSelection,
            _viewDirection, _navigationSpeed, _resetCamera, _viewportGuides);
        viewCommands.HorizontalAlignment = HorizontalAlignment.Right;
        var viewHeader = EditorVisuals.Bar(EditorVisuals.Columns("Auto,*", EditorVisuals.Heading("Viewport"), viewCommands));
        var view = new DockPanel { Background = EditorVisuals.Inset };
        AddDocked(view, viewHeader, Avalonia.Controls.Dock.Top);
        AddDocked(view, _viewportNavFeedback, Avalonia.Controls.Dock.Top);
        view.Children.Add(_viewport);
        AutomationProperties.SetName(_gizmoMode, "Transform mode");
        AutomationProperties.SetName(_gizmoSpace, "Coordinate space");
        AutomationProperties.SetName(_resetCamera, "Reset to scene camera");
        AutomationProperties.SetName(_focusSelection, "Focus selection");
        AutomationProperties.SetName(_viewDirection, "View direction");
        AutomationProperties.SetName(_navigationSpeed, "Navigation speed");
        AutomationProperties.SetName(_viewportGuides, "Authoring camera and light guides");
        ToolTip.SetTip(_viewportGuides, "Show read-only authoring camera and light guides for one selected node.");

        var tabs = new List<TabItem>
        {
            new() { Header = "Inspector", HeaderTemplate = EditorVisuals.LabelTemplate(EditorVisuals.Sliders),
                    Content = EditorVisuals.Scroll(_inspectorPanel) },
            new() { Header = "Assets", HeaderTemplate = EditorVisuals.LabelTemplate(EditorVisuals.Catalog),
                    Content = BuildAssetsPanel() }
        };
        // Assets and Inspector share this pane. A valid local drag can reveal
        // Inspector via its header without making the tab itself a drop target.
        DragDrop.SetAllowDrop(tabs[0], true);
        DragDrop.AddDragOverHandler(tabs[0], (_, e) =>
        {
            if (_assetDrag is not { } drag || !drag.Matches(e.DataTransfer.TryGetValue(AssetDragFormat))) return;
            _assetTabs.SelectedIndex = 0;
            e.DragEffects = DragDropEffects.None;
            e.Handled = true;
        });
        if (_savedWorkspace is not null)
            tabs.Add(new TabItem { Header = "Saved Scenes", HeaderTemplate = EditorVisuals.LabelTemplate(EditorVisuals.File),
                Content = BuildSavedScenePanel() });
        _assetTabs.Background = EditorVisuals.Pane;
        _assetTabs.ItemsSource = tabs;
        _assetTabs.SelectionChanged += (_, e) =>
        {
            // Lists and the type filters also raise SelectionChanged.
            if (!ReferenceEquals(e.Source, _assetTabs)) return;
            SyncPreviewSelection();
            if (_assetTabs.SelectedIndex == 1)
            {
                if (_assetBrowser.State is "Not loaded" or "Disconnected") LoadAssets();
                QueryMaterial();
                foreach (var state in _textureImports) QueryTexture(state);
            }
            else
            {
                _assetBrowser.CancelPendingDependents();
                _assetBrowser.CancelRename();
                _assetBrowser.CancelMove();
                _assetBrowser.CancelRemoval();
                UpdateRebuildButtons();
            }
            if (_assetTabs.SelectedIndex == 2) RefreshSavedScenes();
        };

        var grid = EditorVisuals.Columns("220,5,*,5,368", hierarchy,
            new GridSplitter { ResizeDirection = GridResizeDirection.Columns, Background = EditorVisuals.Line },
            view,
            new GridSplitter { ResizeDirection = GridResizeDirection.Columns, Background = EditorVisuals.Line }, _assetTabs);
        if (!diagnosticMode)
        {
            MinWidth = 960;
            MinHeight = 600;
            grid.ColumnDefinitions[0].MinWidth = 173;
            grid.ColumnDefinitions[2].MinWidth = 280;
            grid.ColumnDefinitions[4].MinWidth = 298;
            bool customized = false;
            foreach (var splitter in grid.Children.OfType<GridSplitter>())
                splitter.AddHandler(Thumb.DragCompletedEvent, (_, _) => customized = true, handledEventsToo: true);
            grid.SizeChanged += (_, e) =>
            {
                if (customized || _targetSize is not null) return;
                bool compact = e.NewSize.Width < 1100;
                grid.ColumnDefinitions[0].Width = new GridLength(compact ? 173 : 220);
                grid.ColumnDefinitions[4].Width = new GridLength(compact ? 308 : 368);
            };
        }

        _retryApply.Content = "Apply";
        AutomationProperties.SetName(_retryApply, "Apply current document or retry");
        var statusToggle = new ToggleButton { Content = "Status details" };
        var diagnosticsToggle = new ToggleButton { Content = "Diagnostics", IsChecked = diagnosticMode };
        var applyDetail = EditorVisuals.Details("");
        applyDetail.Bind(TextBlock.TextProperty, _applyLine.GetObservable(TextBlock.TextProperty));
        var editDetail = EditorVisuals.Details("");
        editDetail.Bind(TextBlock.TextProperty, _editLine.GetObservable(TextBlock.TextProperty));
        var runtimeDetail = EditorVisuals.Details("");
        runtimeDetail.Bind(TextBlock.TextProperty, _connectionLine.GetObservable(TextBlock.TextProperty));
        var statusDetails = EditorVisuals.Scroll(EditorVisuals.Stack(3, runtimeDetail, editDetail, applyDetail));
        statusDetails.MaxHeight = 130;
        statusDetails.IsVisible = false;
        statusToggle.IsCheckedChanged += (_, _) => statusDetails.IsVisible = statusToggle.IsChecked == true;

        _status.MaxHeight = 105;
        var diagnosticPanel = EditorVisuals.Scroll(EditorVisuals.Stack(4,
            EditorVisuals.Actions(_notes, _measureLatency, _measureFrames, _sizeBox, _applySize), _status));
        diagnosticPanel.MaxHeight = 162;
        diagnosticPanel.IsVisible = diagnosticMode;
        diagnosticsToggle.IsCheckedChanged += (_, _) => diagnosticPanel.IsVisible = diagnosticsToggle.IsChecked == true;
        var tasksToggle = new ToggleButton { Content = "Tasks & Issues" };
        _tasksPanel = new TaskIssuesPanel(_taskIssues, CanLocateTask, LocateTask);
        tasksToggle.IsCheckedChanged += (_, _) =>
        {
            _tasksPanel.IsVisible = tasksToggle.IsChecked == true;
            _tasksPanel.Refresh();
        };
        var status = EditorVisuals.Bar(EditorVisuals.Columns("*,*,2*,*,Auto,Auto,Auto,Auto",
            _connectionLine, _editLine, _documentLine, _applyLine, _retryApply, tasksToggle, statusToggle, diagnosticsToggle));

        _connectionLine.Margin = _editLine.Margin = _applyLine.Margin = new Thickness(0, 0, 8, 0);
        var root = new Grid { RowDefinitions = new RowDefinitions("Auto,*,Auto,Auto,Auto,Auto") };
        foreach (Control control in new Control[] { commands, grid, status, _tasksPanel, statusDetails, diagnosticPanel })
        {
            Grid.SetRow(control, root.Children.Count);
            root.Children.Add(control);
        }
        return root;
    }

    private static void AddDocked(DockPanel parent, Control child, Avalonia.Controls.Dock side)
    {
        DockPanel.SetDock(child, side);
        parent.Children.Add(child);
    }

    private void OnViewportWindow(ViewportWindow window)
    {
        _worker?.SetViewportExtent(window.Width, window.Height);
        _log.Write("viewport host window 0x" + window.Handle.ToString("x") + " " + window.Width + "x" + window.Height);
        window.Resized += (w, h) =>
        {
            _worker?.SetViewportExtent(w, h);
            ++_pickRequest;
            CancelGroupGesture();
            _log.Write("viewport resized " + w + "x" + h);
            CorrectViewportSize();
            UpdateStatus();
        };
        window.Pointer += OnPointer;
        window.CaptureLost += () =>
        {
            if (_groupGrab is not null) CancelGroupGesture();
        };
        window.Wheel += OnWheel;
        window.Key += (key, down, _) =>
        {
            if (key == 0x1b && down && CancelGroupGesture()) return;
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
            Dispatcher.UIThread.Post(() =>
            {
                if (_shuttingDown) runtime.Dispose();
                else _runtime = runtime;
            });
            long started = Stopwatch.GetTimestamp();
            SessionInfo session = runtime.WaitForSession(TimeSpan.FromSeconds(120));
            double ms = Stopwatch.GetElapsedTime(started).TotalMilliseconds;
            Dispatcher.UIThread.Post(() => OnSession(runtime, session, ms, runtime.SessionPath));
        }).ContinueWith(task =>
        {
            if (task.Exception is null) return;
            string message = task.Exception.GetBaseException().Message;
            Dispatcher.UIThread.Post(() =>
            {
                if (_shuttingDown) return;
                _log.Write("runtime start failed: " + message);
                ShowDocumentState(new(DocumentAvailability.Unavailable, Diagnostic: message));
                _runtimeLine = "Runtime failed: " + message;
                UpdateStatus();
            });
        }, TaskScheduler.Default);
    }

    private void OnSession(RuntimeProcess? runtime, SessionInfo session, double startMs, string? sessionPath)
    {
        if (_shuttingDown) return;
        _recoveryWindow?.Close();
        _recoveryView = null;
        _offeredRecovery = null;
        _recoveryUnknown = false;
        _reviewRecovery.IsVisible = false;
        _recoveryLine.Text = "";
        _taskIssues.Reset();
        _importTask = _rebuildTask = _materialTask = 0;
        foreach (var texture in _textureImports) texture.Task = 0;
        EndPreviewSession();
        LoseTextureConnection();
        _materialPoll.Stop(); _materialWindow?.Close(); _materialWindow = null;
        _materialSettlement.LoseReceipt();
        _materialPolling = _materialCanWrite = _materialSupported = _materialLegacy = false; _materialSupportsMetallicRoughness = null; _materialSupportsOcclusion = null; _materialSupportsCombinedMaps = null;
        _materialReadTarget = null; _selectedMaterial = null; ++_materialReadEpoch;
        ResetReparent("");
        _reparentDocumentOpen = false;
        RetireAssetDrop();
        _worker?.Stop();
        _viewportCameraIntent = 0;
        _cameraInitialized = _navigationSupported = _navigationReady = _navigationPending = false;
        _sceneProjection = null;
        ShowNavigationMessage(null);
        _selection.Select(null);
        ResetHierarchyView();
        ++_pickRequest;
        ClearBatchInspector();
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
        ++_createRequest;
        _creatingObject = false;
        ++_duplicateRequest;
        _duplicating = false;
        ShowDuplicateState("");
        UpdateHierarchyActions();
        ++_referenceLaunch;
        _referenceWindow?.Close(); _referenceWindow = null;
        _referenceBusy = false; _referenceSessionFailed = false;
        _importPoll.Stop(); _importJob = null; _importPending = _importRunning = false;
        _importState.Text = "";
        UpdateImportButtons();
        _rebuildPoll.Stop(); _rebuildJob = null;
        _activeAssetGeneration = runtime?.Launch.AssetGeneration is { } named ? new GenerationId(AssetGuid.Parse(named)) : null;
        _renameDraftGeneration = null; _renameRequestContext = null; _renameUnknown = false;
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
        worker.PersistenceChanged += state =>
        {
            if (ReferenceEquals(_worker, worker) && !_shuttingDown) ShowDocumentState(state);
        };
        worker.RecoveryChanged += view =>
        {
            if (ReferenceEquals(_worker, worker) && !_shuttingDown) ShowRecovery(view);
        };
        worker.LeaveDocumentEvents += events =>
        {
            if (ReferenceEquals(_worker, worker)) ObserveDraftChanges(events);
        };
        ShowGuideFeedback(null);
        worker.SetGuidesEnabled(_viewportGuides.IsChecked == true);
        worker.GuideFeedback += message =>
        {
            if (ReferenceEquals(_worker, worker) && !_referenceSessionFailed) ShowGuideFeedback(message);
        };
        if (window is not null) worker.SetViewportExtent(window.Width, window.Height);
        worker.NavigationChanged += state =>
        {
            if (!ReferenceEquals(_worker, worker) || state.Intent != _viewportCameraIntent) return;
            _navigationPending = false;
            _navigationSupported = state.Supported;
            _navigationReady = state.ProjectionReady;
            _sceneProjection = state.EditorCamera is null ? state.SceneCamera : null;
            if (state.EditorCamera is EditorCamera current) _camera = current;
            else if (_sceneProjection is CameraPose scenePose) _camera.SeedNavigation(scenePose);
            ++_pickRequest;
            ShowNavigationMessage(state.Message);
            UpdateViewportNavigationActions();
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
            if (_assetTabs.SelectedIndex == 1) { QueryMaterial(); QueryTexture(); LoadAssets(); }
        };
        worker.Failed += message =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            ShowDocumentState(new(DocumentAvailability.Unavailable, Diagnostic: message));
            _cameraInitialized = _navigationReady = _navigationPending = false;
            ShowNavigationMessage(message);
            UpdateViewportNavigationActions();
            EndPreviewSession();
            _referenceSessionFailed = true;
            _taskIssues.Disconnect();
            ++_hierarchyScrollIntent;
            ShowGuideFeedback("Session unavailable.");
            ResetReparent(_reparentPending ? "Reparent interrupted. Inspect the document before retrying." : null);
            _reparentDocumentOpen = false;
            LoseTextureConnection();
            _materialSettlement.LoseReceipt();
            _materialPolling = _materialCanWrite = _materialSupported = _materialLegacy = false; _materialSupportsMetallicRoughness = null; _materialSupportsOcclusion = null; _materialSupportsCombinedMaps = null; _materialPoll.Stop();
            ShowMaterialState();
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
            RetireAssetDrop();
            _addingMesh = false;
            ++_meshRequest;
            if (_duplicating) ShowDuplicateState("Duplicate interrupted. Inspect the document on reconnect before retrying.");
            _duplicating = false;
            ++_duplicateRequest;
            UpdateHierarchyActions();
            _importPoll.Stop();
            if (_importPending || _importRunning)
                _importState.Text = _importJob is ImportId importId ? $"Import {importId} outcome unknown." : "Import admission unknown.";
            _importPending = _importRunning = false;
            UpdateImportButtons();
            _rebuildPoll.Stop();
            if (_rebuildPending || _rebuildRunning)
            {
                _renameUnknown = _renameRequestContext is not null;
                _rebuildState.Text = _renameRequestContext is string target
                    ? (_rebuildJob is RebuildId renameJob
                        ? $"{_assetWriteLabel} {target}, job {renameJob}: outcome unknown. Retain the ID and query after reconnecting."
                        : $"{_assetWriteLabel} {target}: outcome unknown. Inspect the workspace and generations; confirm settlement before retrying.")
                    : _rebuildJob is RebuildId job
                        ? $"Rebuild {job} outcome unknown. Check its status before retrying."
                        : "Rebuild admission unknown. Inspect the latest job before retrying; it may belong to another client.";
            }
            _rebuildPending = _rebuildRunning = false;
            _assetBrowser.CancelRemoval();
            UpdateRebuildButtons();
            _retryApply.IsEnabled = false; _allowGizmoPreview = false; _gizmoGrab = null;
            _groupGrab = null;
            _gizmoShown = null;
            SetGroupControls(false);
            _referenceWindow?.Invalidate("the session failed");
            _referenceBusy = false; ++_referenceLaunch; UpdateReferenceButtons();
            _selection.Select(null);
            ++_pickRequest;
            SyncHierarchySelection(scroll: false);
            ClearBatchInspector();
            _inspectorPanel.Children.Clear();
            if (!_assetBrowser.InterruptReferences()) _assetBrowser.Disconnect();
            RenderAssets();
            _runtimeLine = "session failed: " + message;
            UpdateStatus();
        };
        worker.Picked += pick => { if (ReferenceEquals(_worker, worker)) OnPicked(pick); };
        worker.HierarchyChanged += snapshot =>
        {
            if (ReferenceEquals(_worker, worker) && !_referenceSessionFailed &&
                snapshot.Lifetime == worker.MeshDocumentLifetime) OnHierarchyChanged(snapshot);
        };
        worker.InspectorChanged += snapshot =>
        {
            if (ReferenceEquals(_worker, worker)) OnInspectorChanged(snapshot);
        };
        worker.AssetReferenceInvalidated += (request, reason) =>
        {
            if (ReferenceEquals(_worker, worker) && _referenceWindow?.Target.Request == request)
                _referenceWindow.Invalidate(reason);
        };
        worker.BatchInspectorChanged += snapshot =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            if (_leaveInProgress && _leaveInput is not null && !_leaveInputStale)
                _leaveBatchDeferred = snapshot;
            else OnBatchInspectorChanged(snapshot);
        };
        worker.HistoryChanged += history =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            _undo.IsEnabled = history.CanUndo;
            _redo.IsEnabled = history.CanRedo;
            _undo.Content = history.CanUndo ? "Undo " + history.UndoLabel : "Undo";
            _redo.Content = history.CanRedo ? "Redo " + history.RedoLabel : "Redo";
            AutomationProperties.SetName(_undo, _undo.Content?.ToString());
            AutomationProperties.SetName(_redo, _redo.Content?.ToString());
            ToolTip.SetTip(_undo, _undo.Content);
            ToolTip.SetTip(_redo, _redo.Content);
        };
        worker.GizmoChanged += snapshot =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed) return;
            _gizmoShown = snapshot;
            UpdateHierarchyActions();
        };
        worker.GroupGrabEnded += id =>
        {
            if (!ReferenceEquals(_worker, worker) || _groupGrab is not { } grab || grab.Id != id) return;
            _groupGrab = null;
            _pressed = null;
        };
        worker.CommitApplied += (ms, state) => _editLine.Text = string.Format(CultureInfo.InvariantCulture, "{0} -- committed, applied in {1:F0} ms", _editLine.Text, ms);
        worker.ApplyTaskObserved += (lifetime, target, update) =>
        {
            if (ReferenceEquals(_worker, worker))
                _taskIssues.ObserveApply(lifetime, target?.SourcePath ?? "Current authoring document", target?.Scene, update);
        };
        worker.MeshDocumentReopened += lifetime =>
        {
            if (ReferenceEquals(_worker, worker)) _taskIssues.RetireDocuments(lifetime);
        };
        worker.ApplyStateChanged += (status, text) =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            _applyLine.Text = "apply: " + text;
            _applyLine.Foreground = status switch
            {
                ApplyStatus.Applied => EditorVisuals.Success,
                ApplyStatus.Failed => EditorVisuals.Error,
                ApplyStatus.Superseded or ApplyStatus.Unknown or ApplyStatus.NeedsApply => EditorVisuals.Warning,
                _ => EditorVisuals.Muted,
            };
        };
        worker.ApplyControlsChanged += (canApply, canPreview) =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            _retryApply.IsEnabled = canApply; _allowGizmoPreview = canPreview;
            if (!canPreview)
            {
                _gizmoGrab = null;
                if (_selection.Nodes.Count < 2) _gizmoShown = null;
            }
            UpdateAddMeshButton();
            UpdateHierarchyActions();
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
            _reparentDocumentOpen = info is not null;
            _viewportGuides.IsEnabled = _reparentDocumentOpen;
            _canAddMeshToDocument = info?.Provenance == Atlantis.Gameplay.Authoring.Provenance.HandAuthored;
            UpdateAddMeshButton();
            UpdateHierarchyActions();
            UpdateSaveAsButton();
            _applyLine.Text = info is null ? "authoring: no document" :
                "authoring: " + info.NodeCount + " nodes (" + info.Provenance + (info.Provenance == Atlantis.Gameplay.Authoring.Provenance.Imported ? ", edits apply but cannot be saved" : "") + ")";
        };
        worker.MeshDocumentReopened += lifetime =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            if (_creatingObject) _editLine.Text = "Scene reopened. Inspect the document before retrying creation.";
            ++_createRequest;
            _creatingObject = false;
            ++_meshRequest;
            _meshDocumentLifetime = lifetime;
            ++_pickRequest;
            ClearBatchInspector();
            if (_selection.Nodes.Count > 0) SetSelection(null, false);
            ResetHierarchyView();
            ResetReparent(_reparentPending || _siblingMovePending || _pickedUp is not null || _hierarchyDrag is not null
                ? "Scene reopened. Inspect the document before retrying the move." : null);
            if (_saveAsPending)
            {
                ++_saveAsRequest;
                _saveAsPending = false;
                _saveAsState.Text = "Document reopened. Inspect the destination if the copy was already sent.";
                UpdateSaveAsButton();
            }
            RetireAssetDrop();
            _addingMesh = false;
            _addMeshState.Text = "Scene reopened.";
            UpdateAddMeshButton();
            if (_duplicating) ShowDuplicateState("Scene reopened. Inspect the document before retrying.");
            ++_duplicateRequest;
            _duplicating = false;
            UpdateHierarchyActions();
        };
        worker.NodesDestroyed += nodes =>
        {
            if (!ReferenceEquals(_worker, worker)) return;
            if (_selection.Nodes.Any(nodes.Contains))
            {
                _selection.Set(_selection.Nodes.Where(node => !nodes.Contains(node)));
                PublishSelection(scroll: false);
            }
            if (!_reparentPending && (_pickedUp ?? _hierarchyDrag?.Source) is { } source && nodes.Contains(source.Node))
            {
                ResetReparent("Reparent cancelled: the source was deleted.");
                UpdateHierarchyActions();
            }
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
        UpdateViewportNavigationActions();
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

    private void ResetToSceneCamera() => NavigateViewport();

    private void NavigateViewport(IReadOnlyList<EntityGuid>? focus = null, ViewDirection? direction = null)
    {
        if (_worker is not { } worker || !_cameraInitialized || _gizmoGrab is not null || _groupGrab is not null) return;
        _navigationPending = true;
        ++_pickRequest;
        ShowNavigationMessage("Updating viewport camera…");
        _viewportCameraIntent = worker.Navigate(focus, _hierarchyRows, _selection.Generation, direction);
    }

    private void ShowNavigationMessage(string? message)
    {
        _navigationFeedback = message;
        UpdateViewportFeedback();
    }

    private void UpdateViewportNavigationActions()
    {
        bool camera = _cameraInitialized && _worker is not null && _gizmoGrab is null && _groupGrab is null;
        _resetCamera.IsEnabled = camera;
        _navigationSpeed.IsEnabled = camera;
        _focusSelection.IsEnabled = camera && _navigationSupported &&
            _selection.Nodes.Count is > 0 and <= ViewportNavigation.MaxTargets;
        _viewDirection.IsEnabled = camera && _navigationSupported;
        _viewportGuides.IsEnabled = _worker is not null && _reparentDocumentOpen && !_referenceSessionFailed;
        string? tip = camera && !_navigationSupported ? "Focus and direction views require an updated Runtime." : null;
        ToolTip.SetTip(_focusSelection, tip ?? (_selection.Nodes.Count > ViewportNavigation.MaxTargets
            ? "Focus supports up to 64 selected nodes." : null));
        ToolTip.SetTip(_viewDirection, tip);
    }

    private void ShowGuideFeedback(string? message)
    {
        _guideFeedback = message;
        UpdateViewportFeedback();
    }

    private void UpdateViewportFeedback()
    {
        string? guide = _viewportGuides.IsChecked == true
            ? (_guideFeedback is null ? "Authoring guides" : "Authoring guides · " + _guideFeedback)
            : null;
        string? text = guide is null || _navigationFeedback is null
            ? guide ?? _navigationFeedback : guide + " · " + _navigationFeedback;
        _viewportNavFeedback.Text = text;
        ToolTip.SetTip(_viewportNavFeedback, text);
    }

    private bool MovingFocusToNavigation()
    {
        if (_navigationToolbarPointer) return true;
        var focused = FocusManager?.GetFocusedElement();
        return IsNavigationTarget(_resetCamera) || IsNavigationTarget(_focusSelection) ||
            IsNavigationTarget(_viewDirection) || IsNavigationTarget(_navigationSpeed) || IsNavigationTarget(_viewportGuides);

        bool IsNavigationTarget(Control control) =>
            control.IsPointerOver || control.IsKeyboardFocusWithin || ReferenceEquals(focused, control) ||
            (focused is Visual child && child.GetVisualAncestors().Contains(control));
    }

    private CameraMatrices? ViewportProjection(ViewportWindow window) =>
        !_navigationReady || _navigationPending ? null :
            NavigationFraming.Projection(_sceneProjection ?? _camera.Pose(), window.Width, window.Height);

    private float NavigationSpeed => _navigationSpeed.SelectedIndex switch { 0 => 0.5f, 2 => 2f, _ => 1f };

    private void MoveViewport(ViewportMotion motion, long arrivalTicks)
    {
        if (_worker is not { } worker || _viewport.Window is not { } window) return;
        if (_navigationPending)
        {
            ++_pickRequest;
            _viewportCameraIntent = worker.QueueCameraMotion(motion);
            return;
        }
        EditorCamera camera = _camera.Copy();
        motion.Apply(camera);
        if (NavigationFraming.Projection(camera.Pose(), window.Width, window.Height) is null) return;
        _camera = camera;
        _sceneProjection = null;
        ++_pickRequest;
        _viewportCameraIntent = worker.SetCamera(camera, arrivalTicks);
        ShowNavigationMessage(null);
    }

    private GizmoMode ModeUi => _gizmoMode.SelectedIndex switch { 1 => GizmoMode.Rotate, 2 => GizmoMode.Scale, _ => GizmoMode.Translate };

    private GizmoSpace SpaceUi => _gizmoSpace.SelectedIndex == 1 ? GizmoSpace.Local : GizmoSpace.World;

    private void SendGizmoSettings()
    {
        if (_syncingGizmoSettings) return;
        if (_selection.Nodes.Count > 1) CancelGroupGesture();
        _worker?.SetGizmo(ModeUi, SpaceUi, _gizmoSnap.IsChecked == true);
    }

    private void SetGroupControls(bool group)
    {
        if (group == _singleGizmoSettings.HasValue) return;
        _syncingGizmoSettings = true;
        try
        {
            var settings = _singleGizmoSettings ?? (_gizmoMode.SelectedIndex, _gizmoSpace.SelectedIndex, _gizmoSnap.IsChecked);
            _singleGizmoSettings = group ? settings : null;
            _gizmoMode.ItemsSource = group ? new[] { "Move", "Rotate", "Uniform scale" } : new[] { "Move", "Rotate", "Scale" };
            _gizmoMode.SelectedIndex = settings.Mode;
            _gizmoSpace.SelectedIndex = group ? 0 : settings.Space;
            _gizmoSnap.IsChecked = settings.Snap;
        }
        finally { _syncingGizmoSettings = false; }
        SendGizmoSettings();
    }

    private bool CancelGroupGesture()
    {
        bool wasGrabbed = _groupGrab is not null;
        bool canceled = _worker?.CancelGroupGizmo() == true;
        _groupGrab = null;
        if (wasGrabbed) UpdateViewportNavigationActions();
        if (wasGrabbed || canceled) _pressed = null;
        if (wasGrabbed && _editLine.Text?.StartsWith("Group Gizmo", StringComparison.Ordinal) == true)
            _editLine.Text = "Group Gizmo canceled.";
        return wasGrabbed || canceled;
    }

    private bool GroupGizmoPointer(ViewportPointerEvent e)
    {
        if (_groupGrab is { } active)
        {
            if (e.Kind is ViewportPointer.Move or ViewportPointer.LeftUp)
            {
                var point = new Vector2(e.X, e.Y);
                float amount = Gizmo.DragAmount(active.Mode, active.Handle, point);
                if (e.Kind == ViewportPointer.Move) _worker?.MoveGroupGizmo(active.Id, amount);
                else
                {
                    if (_viewport.Window is { } w && e.X >= 0 && e.X < w.Width && e.Y >= 0 && e.Y < w.Height)
                        _worker?.EndGroupGizmo(active.Id, amount);
                    else _worker?.CancelGroupGizmo(active.Id);
                    _groupGrab = null;
                    UpdateViewportNavigationActions();
                }
            }
            return true;
        }
        if (e.Kind != ViewportPointer.LeftDown || _selection.Nodes.Count < 2 || HasHiddenSelection() || !_cameraInitialized ||
            _worker is not { } worker || _viewport.Window is not { Width: > 0, Height: > 0 } window ||
            _gizmoShown?.Group is not { } shown || shown.Selection != _selection.Generation ||
            shown.Mode != ModeUi || shown.Snap != (_gizmoSnap.IsChecked == true)) return false;
        if (ViewportProjection(window) is not CameraMatrices camera) return false;
        var rect = new Atlantis.Editor.Model.PixelRect(0, 0, window.Width, window.Height);
        var pointer = new Vector2(e.X, e.Y);
        Vector3 pivot = shown.Placement.Pivot;
        GizmoGrab handle;
        if (shown.Mode == GizmoMode.Scale)
        {
            if (Picking.PixelOf(camera, rect, pivot) is not Vector2 center ||
                Vector2.Distance(center, pointer) > Gizmo.PickPixels) return false;
            handle = new GizmoGrab(GizmoAxis.X, pointer,
                new GizmoHandle(center, Vector2.UnitX, center, 1), 0, false);
        }
        else
        {
            Vector3[] axes = [Vector3.UnitX, Vector3.UnitY, Vector3.UnitZ];
            if (Gizmo.Handles(camera, rect, pivot, axes) is not { } handles ||
                Gizmo.Grab(handles, axes, camera, shown.Mode, pointer) is not GizmoGrab grabbed) return false;
            handle = grabbed;
        }
        _pressed = null;
        if (worker.BeginGroupGizmo(shown, handle.Axis) is long id)
        {
            _groupGrab = (id, shown.Mode, handle);
            UpdateViewportNavigationActions();
            _editLine.Text = "Group Gizmo · World · " + (shown.Mode == GizmoMode.Scale ? "Uniform scale" : shown.Mode.ToString());
        }
        return true;
    }

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
                CameraMatrices? camera = ViewportProjection(window);
                if (camera is null) return false;
                GizmoMode mode = GizmoLayout.EffectiveMode(availability, ModeUi);
                GizmoSpace space = GizmoLayout.EffectiveSpace(world, SpaceUi);
                Vector3[] directions = GizmoLayout.Directions(world, mode, space);
                var rect = new Atlantis.Editor.Model.PixelRect(0, 0, window.Width, window.Height);
                GizmoHandle?[]? handles = Gizmo.Handles(camera, rect, new Vector3(world[12], world[13], world[14]), directions);
                if (handles is null || Gizmo.Grab(handles, directions, camera, mode, new Vector2(e.X, e.Y)) is not GizmoGrab grab) return false;
                _gizmoGrab = grab;
                UpdateAddMeshButton();
                UpdateHierarchyActions();
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
                UpdateHierarchyActions();
                return true;
            default:
                return false;
        }
    }

    private void OnPointer(ViewportPointerEvent e)
    {
        if (_leaveInProgress || _recoveryDecision) return;
        if (_recorder is not null && MeasureDrag(e)) return;
        if (GroupGizmoPointer(e)) return;
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
                if (_dragging && (_navigationReady || _navigationPending))
                    MoveViewport(ViewportMotion.Orbit((e.X - _last.X) * NavigationSpeed, (e.Y - _last.Y) * NavigationSpeed),
                        e.ArrivalTicks);
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
        if (_leaveInProgress || _recoveryDecision) return;
        if (_groupGrab is not null || _gizmoGrab is not null || !(_navigationReady || _navigationPending)) return;
        MoveViewport(ViewportMotion.Dolly(delta / 120.0f * NavigationSpeed), arrivalTicks);
        _inputLine = "forwarded wheel " + delta;
    }

    private void Pick(int x, int y, long arrivalTicks)
    {
        ViewportWindow? window = _viewport.Window;
        if (window is null || _worker is null || window.Width <= 0 || window.Height <= 0) return;
        CameraMatrices? matrices = ViewportProjection(window);
        if (matrices is null) return;
        ulong request = ++_pickRequest;
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
        _worker.Raycast(CameraConvention.RayThroughNdc(matrices, ndcX, ndcY), arrivalTicks, request, _viewportCameraIntent);
    }

    private void OnPicked(PickResult pick)
    {
        if (pick.Request != 0 && pick.Request != _pickRequest) return;
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

    private void SetSelection(EntityGuid? node, bool scroll)
    {
        ++_pickRequest;
        _selection.Select(node);
        PublishSelection(scroll);
    }

    private void SyncHierarchySelection(bool scroll)
    {
        bool wasSyncing = _syncingSelection;
        _syncingSelection = true;
        try
        {
            var selected = _entities.SelectedItems!;
            selected.Clear();
            foreach (HierarchyRow row in _hierarchyProjection.Rows.Where(row => _selection.Nodes.Contains(row.Node)))
                selected.Add(row);
            if (scroll && selected.Count == 1 && selected[0] is object item) _entities.ScrollIntoView(item);
        }
        finally { _syncingSelection = wasSyncing; }
    }

    private void PublishSelection(bool scroll)
    {
        if (_advertisedAssetDrop?.Field is { } field && field.Selection != _selection.Generation)
            ClearAssetDropFeedback();
        if (_gizmoShown?.Group?.Selection != _selection.Generation) CancelGroupGesture();
        SetGroupControls(_selection.Nodes.Count > 1);
        if (_batchView?.SelectionGeneration != _selection.Generation) ClearBatchInspector();
        SyncHierarchySelection(scroll);
        if (_selection.Nodes.Count > 1)
        {
            _gizmoGrab = null;
            _gizmoShown = null;
            _dragPress = null;
            if (_pickedUp is not null || _hierarchyDrag is not null) ResetReparent(null);
            if (_batchView is null)
            {
                _inspectorPanel.Children.Clear();
                _inspectorPanel.Children.Add(EditorVisuals.Secondary($"Reading {_selection.Nodes.Count} selected nodes…"));
            }
        }
        _worker?.ShowSelection(_selection.Nodes, _selection.Generation);
        UpdateHierarchyActions();
    }

    private void OnHierarchySelected()
    {
        if (_syncingSelection) return;
        var nodes = _entities.SelectedItems!.OfType<HierarchyRow>().Select(row => row.Node).ToArray();
        ++_pickRequest;
        if (_selection.Set(nodes)) PublishSelection(scroll: false);
        else
        {
            SyncHierarchySelection(scroll: false);
            _editLine.Text = $"Select up to {Atlantis.Gameplay.Authoring.RequireDocumentRevision.MaxWrites} nodes.";
        }
    }

    private void ToggleHierarchyRow(HierarchyRow row)
    {
        if (!_hierarchyProjection.Rows.Any(item => ReferenceEquals(item, row))) return;
        ++_pickRequest;
        if (_selection.Toggle(row.Node)) PublishSelection(scroll: false);
        else _editLine.Text = $"Select up to {Atlantis.Gameplay.Authoring.RequireDocumentRevision.MaxWrites} nodes.";
    }

    private void OnHierarchyKeyDown(object? sender, KeyEventArgs e)
    {
        if ((e.KeyModifiers & KeyModifiers.Control) != 0 && e.Key == Key.Space &&
            HierarchyItem(e.Source)?.DataContext is HierarchyRow row)
        {
            ToggleHierarchyRow(row);
            e.Handled = true;
        }
        else if ((e.KeyModifiers & KeyModifiers.Shift) != 0 && e.Key is Key.Up or Key.Down)
        {
            e.Handled = true;
        }
    }

    private void SetupSceneObjectMenu()
    {
        var flyout = new MenuFlyout { Placement = PlacementMode.BottomEdgeAlignedLeft };
        void Add(params SceneObjectPreset[] presets)
        {
            if (flyout.Items.Count != 0) flyout.Items.Add(new Separator());
            foreach (SceneObjectPreset preset in presets)
            {
                var item = new MenuItem { Header = SceneObjectPresets.Label(preset) };
                ToolTip.SetShowOnDisabled(item, true);
                item.Click += (_, _) => CreateSceneObject(preset);
                _createItems.Add(preset, item);
                flyout.Items.Add(item);
            }
        }
        Add(SceneObjectPreset.Empty, SceneObjectPreset.Camera, SceneObjectPreset.UnnamedRoot, SceneObjectPreset.EmptyChild);
        Add(SceneObjectPreset.DirectionalLight, SceneObjectPreset.PointLight);
        Add(SceneObjectPreset.Cube, SceneObjectPreset.Sphere, SceneObjectPreset.Plane);
        flyout.Opening += (_, _) => UpdateSceneObjectMenu();
        _create.Flyout = flyout;
        ToolTip.SetShowOnDisabled(_create, true);
    }

    private string? SceneObjectMenuUnavailable() => _worker is null || _referenceSessionFailed || !_reparentDocumentOpen
        ? "Open an authoring document."
        : _creatingObject ? "Creation is settling."
        : _gizmoGrab is not null || _groupGrab is not null ? "Finish the Gizmo gesture first."
        : _reparentPending || _siblingMovePending || _duplicating || _addingMesh || _hierarchyDrag is not null
            ? "Finish the current scene edit first." : null;

    private void UpdateSceneObjectMenu()
    {
        string? reason = SceneObjectMenuUnavailable();
        _create.IsEnabled = reason is null;
        ToolTip.SetTip(_create, reason);
        foreach (var (preset, item) in _createItems)
        {
            bool allowed = SceneObjectPresets.Available(preset, _canAddMeshToDocument, _selection.Nodes.Count) &&
                (preset != SceneObjectPreset.EmptyChild || (!_hierarchyFilter.Active && SelectedReparentRow() is not null));
            item.IsEnabled = reason is null && allowed;
            ToolTip.SetTip(item, item.IsEnabled ? null : reason ?? (preset switch
            {
                SceneObjectPreset.EmptyChild => _hierarchyFilter.Active ? FilteredStructureReason : "Select exactly one live Hierarchy row.",
                SceneObjectPreset.UnnamedRoot => "Clear the Hierarchy selection.",
                _ => "Open a hand-authored scene to use named presets."
            }));
        }
    }

    private void CreateSceneObject(SceneObjectPreset preset)
    {
        UpdateSceneObjectMenu();
        if (!_createItems[preset].IsEnabled || _worker is not { } worker) return;
        long request = _createRequest + 1, lifetime = _meshDocumentLifetime;
        ulong selection = _selection.Generation;
        HierarchyRow? parent = preset == SceneObjectPreset.EmptyChild ? SelectedReparentRow() : null;
        bool Current() => ReferenceEquals(_worker, worker) && request == _createRequest &&
            !_referenceSessionFailed && lifetime == worker.MeshDocumentLifetime;
        bool accepted = worker.CreateSceneObject(preset, parent, lifetime, selection, notice =>
        {
            if (Current()) _editLine.Text = notice;
        }, created =>
        {
            if (Current() && _selection.Generation == selection && _hierarchyRows.Any(row => row.Node == created))
                SetSelection(created, true);
        }, () =>
        {
            if (!Current()) return;
            _creatingObject = false;
            UpdateHierarchyActions();
        });
        if (accepted)
        {
            _createRequest = request;
            _creatingObject = true;
        }
        _editLine.Text = accepted ? $"Creating {SceneObjectPresets.Label(preset)}…" : "Create refused. Refresh and try again.";
        UpdateHierarchyActions();
    }

    private void UpdateHierarchyActions()
    {
        UpdateViewportNavigationActions();
        UpdateHierarchyFilterStatus();
        bool multiple = _selection.Nodes.Count > 1;
        _duplicate.Content = multiple ? "Duplicate subtrees" : "Duplicate subtree";
        string? reason = _worker is null || _referenceSessionFailed ? "Connect to an authoring session."
            : _hierarchyFilter.Active ? FilteredStructureReason
            : !_canAddMeshToDocument ? "Open a hand-authored scene to duplicate nodes."
            : _duplicating ? "A duplicate is settling."
            : _gizmoGrab is not null || _groupGrab is not null ? "Finish the active Gizmo gesture."
            : multiple && _worker.DuplicateBatchAvailable != true ? "The open document does not support guarded duplicate."
            : _selection.Nodes.Count == 0 || _selection.Nodes.Any(node => !_hierarchyRows.Any(row => row.Node == node))
                ? "Select live nodes in the Hierarchy."
                : null;
        _duplicate.IsEnabled = reason is null;
        UpdateSceneObjectMenu();
        _destroy.IsEnabled = !_hierarchyFilter.Active && _selection.Node is not null;
        bool gizmoTools = _allowGizmoPreview && _selection.Nodes.Count < 2;
        bool groupTools = _selection.Nodes.Count > 1 && !HasHiddenSelection() && _gizmoShown?.Group is { } group &&
            group.Selection == _selection.Generation;
        _gizmoMode.IsEnabled = _gizmoSnap.IsEnabled = gizmoTools || groupTools;
        _gizmoSpace.IsEnabled = gizmoTools;
        foreach (Control control in new Control[] { _gizmoMode, _gizmoSnap })
            ToolTip.SetTip(control, multiple && !groupTools ? HasHiddenSelection() ? HiddenBatchReason :
                "Group placement requires a current editable selection and guarded Transform support." : null);
        ToolTip.SetTip(_duplicate, reason);
        bool canCapture = CanCaptureReparent(drag: false) && SelectedReparentRow() is not null;
        _pickUp.IsEnabled = canCapture;
        _toRoot.IsEnabled = canCapture && SelectedReparentRow()?.Parent is not null;
        _moveHere.IsEnabled = canCapture && _pickedUp is { } pickup && _selection.Node != pickup.Node;
        bool canReorder = _worker?.SiblingOrderAvailable == true && _canAddMeshToDocument && canCapture &&
            _pickedUp is null && _hierarchyDrag is null;
        _moveUp.IsEnabled = canReorder && DisplayedSibling(SiblingDirection.Up) is not null;
        _moveDown.IsEnabled = canReorder && DisplayedSibling(SiblingDirection.Down) is not null;
        foreach (Button button in new[] { _destroy, _pickUp, _moveHere, _toRoot, _moveUp, _moveDown }.Concat(_inspectorStructureButtons))
        {
            if (_inspectorStructureButtons.Contains(button)) button.IsEnabled = !_hierarchyFilter.Active;
            ToolTip.SetTip(button, _hierarchyFilter.Active ? FilteredStructureReason : null);
        }
        foreach (Action update in _batchButtons) update();
        _hiddenBatchStatus.IsVisible = multiple && HasHiddenSelection();
    }

    private HierarchyRow? DisplayedSibling(SiblingDirection direction)
    {
        if (SelectedReparentRow() is not { } row) return null;
        var siblings = _hierarchyRows.Where(r => r.Parent == row.Parent).ToList();
        int index = siblings.FindIndex(candidate => ReferenceEquals(candidate, row));
        int adjacent = index + (direction == SiblingDirection.Up ? -1 : 1);
        return index >= 0 && adjacent >= 0 && adjacent < siblings.Count ? siblings[adjacent] : null;
    }

    private void MoveSelectedSibling(SiblingDirection direction)
    {
        Button button = direction == SiblingDirection.Up ? _moveUp : _moveDown;
        if (!button.IsEnabled || _worker is not { } worker || SelectedReparentRow() is not { } row ||
            DisplayedSibling(direction) is not { } sibling) return;
        long lifetime = _meshDocumentLifetime;
        _siblingMovePending = worker.MoveSibling(row, sibling, direction, lifetime, notice =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed || lifetime != worker.MeshDocumentLifetime) return;
            _siblingMovePending = false;
            ShowReparentState(notice);
            UpdateHierarchyActions();
        });
        ShowReparentState(_siblingMovePending ? "Moving sibling…" : "Sibling swap refused. Refresh before retrying.");
        UpdateHierarchyActions();
    }

    private void ShowDuplicateState(string notice)
    {
        _duplicateState.Text = notice;
        _duplicateState.IsVisible = notice.Length > 0;
    }

    private void DuplicateSelectedSubtree()
    {
        if (!_duplicate.IsEnabled || _worker is not { } worker) return;
        if (_selection.Nodes.Count > 1)
        {
            DuplicateSelectedSubtrees(worker);
            return;
        }
        if (SelectedReparentRow() is not { } sourceRow) return;
        EntityGuid source = sourceRow.Node;
        long request = ++_duplicateRequest;
        long lifetime = _meshDocumentLifetime;
        ulong selection = _selection.Generation;
        _duplicating = true;
        ShowDuplicateState("Duplicating subtree…");
        UpdateHierarchyActions();
        worker.DuplicateSubtree(source, lifetime, (node, notice) =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed || request != _duplicateRequest ||
                lifetime != worker.MeshDocumentLifetime) return;
            _duplicating = false;
            ShowDuplicateState(notice);
            if (_selection.Generation == selection && node is EntityGuid created &&
                _hierarchyRows.Any(row => row.Node == created))
                SetSelection(created, true);
            UpdateHierarchyActions();
        }, sourceRow);
    }

    private void DuplicateSelectedSubtrees(EditorWorker worker)
    {
        long request = ++_duplicateRequest;
        long lifetime = _meshDocumentLifetime;
        ulong selection = _selection.Generation;
        ulong pick = _pickRequest;
        var rows = _hierarchyRows.Where(row => _selection.Nodes.Contains(row.Node)).ToArray();
        _duplicating = worker.DuplicateSubtrees(rows, lifetime, selection, (roots, notice) =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed || request != _duplicateRequest ||
                lifetime != worker.MeshDocumentLifetime) return;
            _duplicating = false;
            ShowDuplicateState(notice);
            if (roots is not null && _selection.Generation == selection && _pickRequest == pick)
            {
                ++_pickRequest;
                _selection.Set(roots);
                PublishSelection(scroll: true);
            }
            UpdateHierarchyActions();
        });
        ShowDuplicateState(_duplicating ? "Duplicating subtrees…" : "Duplicate refused. Refresh the selection and retry.");
        UpdateHierarchyActions();
    }

    private bool CanCaptureReparent(bool drag) =>
        !_hierarchyFilter.Active && _worker is not null && !_referenceSessionFailed && _reparentDocumentOpen && _selection.Nodes.Count == 1 &&
        (!drag || _canAddMeshToDocument) && !_reparentPending && !_siblingMovePending && _gizmoGrab is null;

    private HierarchyRow? SelectedReparentRow() =>
        _entities.SelectedItem is HierarchyRow row && row.Node == _selection.Node ? row : null;

    private ReparentSource? CaptureSelectedReparent()
    {
        if (!CanCaptureReparent(drag: false) || SelectedReparentRow() is not { } row) return null;
        _pickedUp = null;
        return _worker?.CaptureReparent(row, _meshDocumentLifetime);
    }

    private static ListBoxItem? HierarchyItem(object? source) =>
        source is ListBoxItem item ? item : (source as Visual)?.FindAncestorOfType<ListBoxItem>();

    private void OnHierarchyPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        _hierarchyPress = null;
        if (!e.GetCurrentPoint(_entities).Properties.IsLeftButtonPressed ||
            HierarchyItem(e.Source) is not { DataContext: HierarchyRow row } item) return;
        if ((e.KeyModifiers & KeyModifiers.Control) != 0) ToggleHierarchyRow(row);
        else
        {
            SetSelection(row.Node, scroll: false);
            if (CanCaptureReparent(drag: true)) _hierarchyPress = (row, e, e.GetPosition(_entities));
        }
        item.Focus();
        e.Handled = true;
    }

    private async void OnHierarchyPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_hierarchyPress is not { } press) return;
        if (e.Pointer != press.Event.Pointer || !e.GetCurrentPoint(_entities).Properties.IsLeftButtonPressed)
        {
            _hierarchyPress = null;
            return;
        }
        var displacement = e.GetPosition(_entities) - press.Position;
        if (Math.Max(Math.Abs(displacement.X), Math.Abs(displacement.Y)) < 5) return;
        _hierarchyPress = null;
        if (!CanCaptureReparent(drag: true) || !Equals(SelectedReparentRow(), press.Row) || _worker is not { } worker ||
            worker.CaptureReparent(press.Row, _meshDocumentLifetime) is not { } source) return;

        _pickedUp = null;
        string token = Guid.NewGuid().ToString("N");
        _hierarchyDropPointer = null;
        _hierarchyDrag = (source, token, worker.SiblingMoveSupported);
        var data = new DataTransfer();
        data.Add(DataTransferItem.Create(ReparentDragFormat, token));
        try
        {
            await DragDrop.DoDragDropAsync(press.Event, data, DragDropEffects.Move);
        }
        finally
        {
            worker.CancelReparent(source);
            if (_hierarchyDrag?.Source == source)
            {
                _hierarchyDrag = null;
                _hierarchyDropPointer = null;
                ClearHierarchyDropFeedback();
                UpdateHierarchyActions();
            }
        }
    }

    private (ListBoxItem Item, HierarchyRow Row, SiblingPlacement? Placement)? HierarchyDropTarget(DragEventArgs e)
    {
        if (_hierarchyDrag is not { } drag || !CanCaptureReparent(drag: true) ||
            _worker is not { } worker || drag.Source.Lifetime != worker.MeshDocumentLifetime ||
            drag.Relative != worker.SiblingMoveSupported ||
            e.DataTransfer.TryGetValue(ReparentDragFormat) != drag.Token ||
            HierarchyItem(e.Source) is not { DataContext: HierarchyRow target } item ||
            !_hierarchyRows.Any(row => row.Node == drag.Source.Node))
            return null;

        double height = item.Bounds.Height;
        double pointer = e.GetPosition(item).Y;
        if (drag.Relative && height > 0 && (pointer < height / 4 || pointer >= 3 * height / 4))
        {
            SiblingPlacement placement = pointer < height / 4 ? SiblingPlacement.Before : SiblingPlacement.After;
            if (!worker.SiblingOrderAvailable || target.Node == drag.Source.Node || target.Parent != drag.Source.Parent)
                return null;
            var interval = DisplayedDropInterval(drag.Source, target);
            bool noChange = interval.Count == 2 && (interval[0].Node == drag.Source.Node) == (placement == SiblingPlacement.Before);
            return interval.Count < 2 || noChange ? null : (item, target, placement);
        }

        if (target.Node == drag.Source.Parent) return null;
        for (HierarchyRow? current = target; current is not null;)
        {
            if (current.Node == drag.Source.Node) return null;
            current = current.Parent is EntityGuid parent ? _hierarchyRows.FirstOrDefault(row => row.Node == parent) : null;
        }
        return (item, target, null);
    }

    private IReadOnlyList<HierarchyRow> DisplayedDropInterval(ReparentSource source, HierarchyRow anchor)
    {
        var siblings = _hierarchyRows.Where(row => row.Parent == source.Parent).ToList();
        int from = siblings.FindIndex(row => row.Node == source.Node);
        int to = siblings.FindIndex(row => ReferenceEquals(row, anchor));
        return from < 0 || to < 0 ? Array.Empty<HierarchyRow>() :
            siblings.GetRange(Math.Min(from, to), Math.Abs(from - to) + 1);
    }

    private void ClearHierarchyDropFeedback()
    {
        _advertisedHierarchyDrop = null;
        _hierarchyDropLine.IsVisible = false;
        _hierarchyDropLabel.IsVisible = false;
        HighlightReparentTarget(null);
    }

    private void ShowHierarchyInsertion(ListBoxItem item, HierarchyRow anchor, SiblingPlacement placement)
    {
        ListBoxItem? endpoint = item;
        if (placement == SiblingPlacement.After)
        {
            int index = _hierarchyRows.ToList().FindIndex(row => ReferenceEquals(row, anchor));
            if (index >= 0)
            {
                while (index + 1 < _hierarchyRows.Count && _hierarchyRows[index + 1].Depth > anchor.Depth) ++index;
                endpoint = _entities.GetVisualDescendants().OfType<ListBoxItem>()
                    .FirstOrDefault(row => ReferenceEquals(row.DataContext, _hierarchyRows[index]));
            }
        }
        double y = placement == SiblingPlacement.Before ? 0 : endpoint?.Bounds.Height ?? 0;
        var point = endpoint?.TranslatePoint(new Point(0, y), _hierarchyDropFeedback);
        if (point is { } screen && screen.Y >= 1 && screen.Y < _hierarchyDropFeedback.Bounds.Height)
        {
            _hierarchyDropLine.Width = Math.Max(0, item.Bounds.Width - 8);
            Canvas.SetLeft(_hierarchyDropLine, screen.X + 4);
            Canvas.SetTop(_hierarchyDropLine, screen.Y - 1);
            _hierarchyDropLine.IsVisible = true;
            return;
        }
        _hierarchyDropLabel.Child = EditorVisuals.Ellipsis($"{placement} {anchor.Label}");
        _hierarchyDropLabel.MaxWidth = _hierarchyDropFeedback.Bounds.Width;
        Canvas.SetLeft(_hierarchyDropLabel, 4);
        Canvas.SetTop(_hierarchyDropLabel, 2);
        _hierarchyDropLabel.IsVisible = true;
    }

    private void HighlightReparentTarget(ListBoxItem? item)
    {
        if (ReferenceEquals(_reparentTargetRow, item)) return;
        _reparentTargetRow?.ClearValue(TemplatedControl.BackgroundProperty);
        _reparentTargetRow?.ClearValue(TemplatedControl.BorderBrushProperty);
        _reparentTargetRow?.ClearValue(TemplatedControl.BorderThicknessProperty);
        _reparentTargetRow = item;
        if (item is null) return;
        item.Background = EditorVisuals.Selected;
        item.BorderBrush = EditorVisuals.Focus;
        item.BorderThickness = new Thickness(1);
    }

    private bool IsAdvertisedHierarchyDrop(HierarchyRow row, SiblingPlacement? placement) =>
        _advertisedHierarchyDrop is { } advertised && advertised.Placement == placement &&
        _worker?.SameHierarchyIncarnation(advertised.Row, row) == true;

    private void OnHierarchyDragOver(object? sender, DragEventArgs e)
    {
        var target = HierarchyDropTarget(e);
        Point pointer = e.GetPosition(_entities);
        // The native drag loop can send a final DragOver at release even when the pointer stayed still.
        // After a row refresh, require pointer movement before advertising the changed target.
        if (_hierarchyDrag is { Relative: true } && _hierarchyDropPointer == pointer &&
            target is { } stationary && !IsAdvertisedHierarchyDrop(stationary.Row, stationary.Placement))
            target = null;
        _hierarchyDropPointer = pointer;
        ClearHierarchyDropFeedback();
        if (target is { } drop)
        {
            _advertisedHierarchyDrop = (drop.Row, drop.Placement);
            if (drop.Placement is SiblingPlacement placement) ShowHierarchyInsertion(drop.Item, drop.Row, placement);
            else HighlightReparentTarget(drop.Item);
        }
        e.DragEffects = target is null ? DragDropEffects.None : DragDropEffects.Move;
        e.Handled = true;
    }

    private void OnHierarchyDrop(object? sender, DragEventArgs e)
    {
        bool submitted = false;
        if (HierarchyDropTarget(e) is { } target && IsAdvertisedHierarchyDrop(target.Row, target.Placement) &&
            _hierarchyDrag is { } drag && _worker is { } worker)
        {
            if (target.Placement is SiblingPlacement placement)
            {
                _siblingMovePending = worker.MoveSiblingRelative(drag.Source, target.Row,
                    DisplayedDropInterval(drag.Source, target.Row), placement, notice =>
                    {
                        if (!ReferenceEquals(_worker, worker) || drag.Source.Lifetime != worker.MeshDocumentLifetime) return;
                        _siblingMovePending = false;
                        ShowReparentState(notice);
                        UpdateHierarchyActions();
                    });
                submitted = _siblingMovePending;
                if (submitted) UpdateHierarchyActions();
            }
            else submitted = SubmitReparent(drag.Source, target.Row, drag: true);
        }
        ClearHierarchyDropFeedback();
        e.DragEffects = submitted ? DragDropEffects.Move : DragDropEffects.None;
        e.Handled = true;
    }

    private void ShowReparentState(string notice)
    {
        _reparentState.Text = notice;
        _reparentState.IsVisible = notice.Length > 0;
    }

    private void ResetReparent(string? notice)
    {
        _siblingMovePending = false;
        if ((_pickedUp ?? _hierarchyDrag?.Source) is { } source) _worker?.CancelReparent(source);
        ++_reparentRequest;
        _reparentPending = false;
        _pickedUp = null;
        _hierarchyPress = null;
        _hierarchyDrag = null;
        _hierarchyDropPointer = null;
        ClearHierarchyDropFeedback();
        if (notice is not null) ShowReparentState(notice);
    }

    private bool SubmitReparent(ReparentSource source, HierarchyRow? target, bool drag)
    {
        if (_worker is not { } worker || !CanCaptureReparent(drag)) return false;
        long request = ++_reparentRequest;
        ulong selection = _selection.Generation;
        _reparentPending = worker.Reparent(source, target, drag, (verified, notice) =>
        {
            if (!ReferenceEquals(_worker, worker) || request != _reparentRequest ||
                source.Lifetime != worker.MeshDocumentLifetime) return;
            _reparentPending = false;
            ShowReparentState(notice);
            if (verified && drag && _selection.Generation == selection && _hierarchyRows.Any(row => row.Node == source.Node))
                SetSelection(source.Node, true);
            UpdateHierarchyActions();
        });
        if (!_reparentPending) worker.CancelReparent(source);
        ShowReparentState(_reparentPending ? "Moving subtree…" : "Reparent refused: the document, source or gesture changed.");
        UpdateHierarchyActions();
        return _reparentPending;
    }

    // Plan 0068 M3 (Spec 0068 R4): one typed editor per field -- a text box committed on Enter or loss of focus (one
    // transaction each), a combo box for an enum, a clear button for an optional field, a note for a read-only one --
    // and Add / Remove component. The panel is not rebuilt under a field being typed in: the snapshot waits.
    private Control BuildAssetsPanel()
    {
        var reload = new Button { Content = "Reload view" };
        reload.Click += (_, _) => LoadAssets();
        var types = _assetTypes;
        types.SelectionChanged += (_, _) =>
        {
            if (_revealAssetFilters) return;
            _assetBrowser.Type = types.SelectedIndex <= 0 ? null : (AssetType)(types.SelectedIndex - 1);
            RenderAssets();
        };
        AutomationProperties.SetName(types, "Asset type");
        var search = _assetSearch;
        search.Classes.Add("editor-search");
        AutomationProperties.SetName(search, "Search name, path or GUID");
        search.PropertyChanged += (_, change) =>
        {
            if (change.Property != TextBox.TextProperty || _revealAssetFilters) return;
            _assetBrowser.Search = search.Text ?? "";
            RenderAssets();
        };
        AutomationProperties.SetName(_assetSort, "Sort assets");
        AutomationProperties.SetName(_assetGroup, "Group assets");
        _assetSort.SelectionChanged += (_, _) => { _assetBrowser.Sort = (AssetBrowseSort)Math.Max(0, _assetSort.SelectedIndex); RenderAssets(); };
        _assetGroup.SelectionChanged += (_, _) => { _assetBrowser.Group = (AssetBrowseGroup)Math.Max(0, _assetGroup.SelectedIndex); RenderAssets(); };
        _assetClearFilters.Click += (_, _) =>
        {
            _assetSearch.Text = "";
            _assetTypes.SelectedIndex = 0;
            _assetSearch.Focus();
        };
        _assetsState.TextWrapping = TextWrapping.Wrap;
        var heading = EditorVisuals.Bar(EditorVisuals.Stack(5,
            EditorVisuals.Columns("*,Auto", EditorVisuals.Heading("Asset catalog"), reload),
            search, EditorVisuals.Actions(types, _assetSort, _assetGroup, _assetClearFilters),
            _assetsState));

        EditorVisuals.ConfigureAssetList(_assetsList);
        AutomationProperties.SetName(_assetsList, "Asset results");
        _assetsList.AddHandler(PointerPressedEvent, OnAssetPointerPressed, RoutingStrategies.Tunnel);
        _assetsList.AddHandler(PointerMovedEvent, OnAssetPointerMoved, RoutingStrategies.Tunnel);
        _assetsList.AddHandler(PointerReleasedEvent, (_, _) => _assetPress = null, RoutingStrategies.Tunnel);
        _assetsList.AddHandler(PointerCaptureLostEvent, (_, _) => _assetPress = null, RoutingStrategies.Bubble, true);
        AddHandler(PointerPressedEvent, (_, _) => ++_assetDropInteraction, RoutingStrategies.Tunnel);
        AddHandler(KeyDownEvent, (_, _) => ++_assetDropInteraction, RoutingStrategies.Tunnel);
        _assetsList.SelectionChanged += (_, _) =>
        {
            if (_assetsSync || _assetsList.SelectedItem is AssetGroupHeader) return;
            if (_assetsList.SelectedItem is AssetSummary asset) ReadAsset(asset.Guid);
            else { _assetBrowser.ClearSelection(); RenderAssets(); }
        };
        var copy = new Button { Content = "Copy GUID" };
        copy.Click += async (_, _) =>
        {
            if (_assetBrowser.SelectionVisible && _assetBrowser.Selected is AssetGuid guid && TopLevel.GetTopLevel(this)?.Clipboard is { } clipboard)
                await clipboard.SetTextAsync(guid.ToString());
        };
        _previewMesh.Click += (_, _) => RequestPreview(withMaterial: false);
        _previewMaterial.ItemTemplate = new FuncDataTemplate<AssetSummary>((asset, _) =>
            asset is null ? new TextBlock() : EditorVisuals.AssetRow(asset));
        _previewMaterial.SelectionChanged += (_, _) => { if (!_assetsSync) SyncPreviewSelection(); };
        AutomationProperties.SetName(_previewMaterial, "Material for mesh preview");
        _previewPair.Click += (_, _) => RequestPreview(withMaterial: true);
        ToolTip.SetShowOnDisabled(_addMesh, true);
        _addMesh.Click += (_, _) => AddMeshToScene();
        copy.IsEnabled = false;
        _assetCopyGuid = copy;
        _renameTexture.Click += (_, _) => BeginTextureRename();
        _renamePublish.Click += (_, _) => PublishTextureRename();
        _renameCancel.Click += (_, _) => { _assetBrowser.CancelRename(); UpdateRebuildButtons(); _renameTexture.Focus(); };
        _renameLabel.TextChanged += (_, _) => UpdateRebuildButtons();
        _moveTexture.Click += (_, _) => BeginTextureMove();
        _movePublish.Click += (_, _) => PublishTextureMove();
        _moveCancel.Click += (_, _) => { _assetBrowser.CancelMove(); UpdateRebuildButtons(); _moveTexture.Focus(); };
        _moveDestination.TextChanged += (_, _) => UpdateRebuildButtons();
        AutomationProperties.SetName(_moveDestination, "Destination texture path");
        _moveEditor = EditorVisuals.Stack(4, _movePaths, _moveDestination, EditorVisuals.Actions(_movePublish, _moveCancel));
        _moveEditor.IsVisible = false;
        _removeRegistration.Click += (_, _) => BeginTextureRemoval();
        _confirmRemoval.Click += (_, _) => ConfirmTextureRemoval();
        _cancelRemoval.Click += (_, _) =>
        {
            _assetBrowser.CancelRemoval();
            UpdateRebuildButtons();
            _removeRegistration.Focus();
        };
        _removalPoll.Tick += (_, _) =>
        {
            if (_worker is { } worker && _assetBrowser.RemovalIntent is { } intent &&
                _removalReceipt is { State: TextureRemovalState.Checking } check)
                ReadTextureRemoval(worker, intent, check.ExpectedGeneration, check.Id);
        };
        AutomationProperties.SetName(_renameLabel, "Texture display name");
        _renameEditor = EditorVisuals.Stack(4, _renameLabel, _renamePath,
            EditorVisuals.Actions(_renamePublish, _renameCancel));
        _renameEditor.IsVisible = false;
        _viewReferences.Click += (_, _) => ViewReferences();
        _cancelReferences.Click += (_, _) =>
        {
            _assetBrowser.CancelPendingDependents();
            UpdateReferences();
            _viewReferences.Focus();
        };
        EditorVisuals.ConfigureReferenceResults(_referenceResults, LocateReference);
        AutomationProperties.SetName(_referenceResults, "Direct material references");
        AutomationProperties.SetName(_referenceStatus, "Reference check status");
        AutomationProperties.SetLiveSetting(_referenceStatus, AutomationLiveSetting.Polite);
        var selected = EditorVisuals.Group(EditorVisuals.Heading("Selected asset"),
            EditorVisuals.Stack(5, _assetTitle, EditorVisuals.Actions(_renameTexture, _moveTexture, _removeRegistration),
                _renameEditor, _moveEditor, _removalState, EditorVisuals.Actions(_confirmRemoval, _cancelRemoval),
                EditorVisuals.Actions(_viewReferences, _cancelReferences), _referenceScope, _referenceStatus,
                _referenceResults, _locatedSlots,
                _addMesh, _addMeshState, _previewState,
                _previewImage, _previewMaterial, EditorVisuals.Actions(_previewMesh, _previewPair),
                EditorVisuals.Disclosure("Asset details", EditorVisuals.Stack(4, copy, _assetDetails))));

        _rebuildStart.Click += (_, _) => StartAssetRebuild();
        _rebuildRefresh.Click += (_, _) => ReadRebuildStatus();
        _rebuildPoll.Tick += (_, _) => ReadRebuildStatus();
        _importName.TextChanged += (_, _) => UpdateImportButtons();
        _importRegister.Click += (_, _) => StartImport(ImportMode.Register);
        _importResume.Click += (_, _) => StartImport(ImportMode.Resume);
        _importRefresh.Click += (_, _) => ReadImportStatus();
        _importPoll.Tick += (_, _) => ReadImportStatus();
        _textureName.TextChanged += (_, _) => UpdateTextureButtons();
        _textureRegister.Click += (_, _) => StartTexture(ImportMode.Register);
        _textureResume.Click += (_, _) => StartTexture(ImportMode.Resume);
        _textureRefresh.Click += (_, _) => QueryTexture();
        _texturePoll.Tick += (_, _) =>
        {
            foreach (var state in _textureImports)
                if (state.Running && !state.Unknown) QueryTexture(state);
        };
        _textureKind.SelectionChanged += (_, _) => { RenderTextureStatus(); QueryTexture(); };
        AutomationProperties.SetName(_textureKind, "Texture import kind");
        RenderTextureStatus();
        AutomationProperties.SetName(_textureName, "Texture inbox name");

        _newMaterial.Click += (_, _) => OpenMaterial(MaterialMode.Create, _newMaterial);
        _copyMaterial.Click += (_, _) => OpenMaterial(MaterialMode.Copy, _copyMaterial);
        _editMaterial.Click += (_, _) => OpenMaterial(MaterialMode.Edit, _editMaterial);
        _materialRefresh.Click += (_, _) => QueryMaterial();
        _materialResume.Click += (_, _) => ResumeMaterial();
        _materialPoll.Tick += (_, _) => QueryMaterial();
        var material = EditorVisuals.Disclosure("Materials", EditorVisuals.Stack(5,
            EditorVisuals.Actions(_newMaterial, _copyMaterial, _editMaterial),
            EditorVisuals.Actions(_materialRefresh, _materialResume), _materialState), expanded: true);
        var body = EditorVisuals.Stack(7, _assetsList, selected, material,
            EditorVisuals.Disclosure("Rebuild / Reimport", EditorVisuals.Stack(5,
                EditorVisuals.Actions(_rebuildStart, _rebuildRefresh), _reimportInput)),
            _rebuildState,
            EditorVisuals.Disclosure("Import", EditorVisuals.Stack(4, _importName,
                EditorVisuals.Actions(_importRegister, _importResume, _importRefresh))),
            _importState,
            EditorVisuals.Disclosure("Import Texture", EditorVisuals.Stack(4,
                EditorVisuals.Secondary("Place one PNG in inbox/<name>/source.png."),
                _textureKind, _textureFormat, _textureName,
                EditorVisuals.Actions(_textureRegister, _textureResume, _textureRefresh),
                _textureState)),
            EditorVisuals.Secondary("Catalog fixed at first query. Changed build or initial load failure: restart Runtime. Reload view does not reread disk."));
        body.Margin = new Thickness(8);
        var panel = new DockPanel();
        AddDocked(panel, heading, Dock.Top);
        panel.Children.Add(EditorVisuals.Scroll(body));
        panel.SizeChanged += (_, e) =>
        {
            bool compact = e.NewSize.Width < 330 || e.NewSize.Height < 510;
            _assetsList.Height = compact ? 180 : 256;
            _previewImage.Height = compact ? 156 : 192;
        };
        return panel;
    }

    private void OnAssetPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        _assetPress = null;
        if (_assetDropPending || _assetDrag is not null || !e.GetCurrentPoint(_assetsList).Properties.IsLeftButtonPressed ||
            e.ClickCount != 1 || HierarchyItem(e.Source) is not { DataContext: AssetSummary row }) return;
        _assetPress = (row.Guid, e, e.GetPosition(_assetsList));
    }

    private async void OnAssetPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_assetPress is not { } press) return;
        if (e.Pointer != press.Event.Pointer || !e.GetCurrentPoint(_assetsList).Properties.IsLeftButtonPressed)
        {
            _assetPress = null;
            return;
        }
        var move = e.GetPosition(_assetsList) - press.Position;
        if (Math.Max(Math.Abs(move.X), Math.Abs(move.Y)) < 5) return;
        _assetPress = null;
        if (_assetDropPending || _assetDrag is not null || _referenceSessionFailed || _worker is null ||
            !_assetBrowser.SelectionVisible || _assetsList.SelectedItem is not AssetSummary asset || asset.Guid != press.Guid) return;

        SettleInspectorBlur();
        var drag = new AssetDragIntent(asset, _assetBrowser.Items, _meshDocumentLifetime);
        _assetDrag = drag;
        var data = new DataTransfer();
        data.Add(DataTransferItem.Create(AssetDragFormat, drag.Token));
        try
        {
            await DragDrop.DoDragDropAsync(press.Event, data, DragDropEffects.Copy);
        }
        finally
        {
            drag.Cancel(onlyUndropped: true);
            ClearAssetDropFeedback();
            _assetDropFields.Clear();
            if (!_assetDropPending && ReferenceEquals(_assetDrag, drag)) _assetDrag = null;
        }
    }

    private void RetireAssetDrop()
    {
        _assetPress = null;
        _assetDrag?.Cancel();
        _assetDrag = null;
        _assetDropPending = false;
        _pendingSceneFieldEdits = 0;
        ++_assetDropRequest;
        ClearAssetDropFeedback();
    }

    private void ClearAssetDropFeedback()
    {
        if (_assetDropHighlight is { } border) border.BorderBrush = EditorVisuals.Line;
        _assetDropHighlight = null;
        _advertisedAssetDrop = null;
        _rootDropLabel.Text = "Drop mesh to add at scene root";
        _rootDropLabel.Foreground = EditorVisuals.Muted;
    }

    private string? AssetDropRefusal(AssetDragIntent drag, AssetDropField? field)
    {
        if (_worker is null || drag.Lifetime != _worker.MeshDocumentLifetime || _referenceSessionFailed ||
            !_assetBrowser.SelectionVisible || _assetBrowser.Selected != drag.Asset.Guid ||
            !ReferenceEquals(drag.Catalog, _assetBrowser.Items)) return "Asset or document is no longer current.";
        if (_pendingInspectorBlur is not null || _pendingSceneFieldEdits != 0 || CurrentSceneInput() is not null)
            return "Finish or cancel the field edit first.";
        if (_assetDropPending || _leaveInProgress || _referenceBusy || _materialWindow is not null ||
            _gizmoGrab is not null || _hierarchyDrag is not null) return "Finish the current action first.";
        if (field is null)
        {
            if (!_canAddMeshToDocument) return "Open a hand-authored scene.";
            if (_addingMesh) return "Mesh addition is already in progress.";
            return drag.Asset.Type == AssetType.Mesh ? null : "Only meshes can be added at scene root.";
        }
        if (_selection.Nodes.Count != 1 || _selection.Generation != field.Selection ||
            field.Lifetime != drag.Lifetime || _inspectorShown?.AssetDropFields.Contains(field) != true)
            return "The displayed reference field changed.";
        return field.Accepts(drag.Asset.Type) ? null : "This field requires " + field.Field.TargetAssetType + ".";
    }

    private void ConfigureAssetDropTarget(Control surface, Border border, AssetDropField? field)
    {
        AutomationProperties.SetName(surface, field is null ? "Drop mesh to add at scene root" : "Drop asset on " + field.Field.Label);
        DragDrop.SetAllowDrop(surface, true);
        DragDrop.AddDragOverHandler(surface, (_, e) =>
        {
            if (_assetDrag is not { } drag || !drag.Matches(e.DataTransfer.TryGetValue(AssetDragFormat))) return;
            ClearAssetDropFeedback();
            string? refusal = AssetDropRefusal(drag, field);
            if (field is not null && !_assetDropFields.TryAdd(field.Field.Address, field) &&
                _assetDropFields[field.Field.Address] != field)
                refusal = "The displayed reference field changed.";
            string description = refusal ?? EditorVisuals.AssetName(drag.Asset) + " → " +
                (field is null ? "Add mesh at scene root" : field.Field.Label);
            _assetDropHighlight = border;
            border.BorderBrush = refusal is null ? EditorVisuals.Focus : EditorVisuals.Error;
            // Keep feedback in the fixed root-zone strip so it cannot move the drop surface.
            string? compatibility = refusal is null ? field?.CompatibilityNotice : null;
            _rootDropLabel.Text = compatibility is null ? description : "Compatibility unverified · " + description;
            _rootDropLabel.Foreground = refusal is null ? EditorVisuals.Accent : EditorVisuals.Error;
            AutomationProperties.SetHelpText(surface, compatibility is null ? description : compatibility + " " + description);
            e.DragEffects = refusal is null ? DragDropEffects.Copy : DragDropEffects.None;
            if (refusal is null) _advertisedAssetDrop = (surface, field);
            e.Handled = true;
        });
        DragDrop.AddDragLeaveHandler(surface, (_, _) => { if (_assetDropHighlight == border) ClearAssetDropFeedback(); });
        DragDrop.AddDropHandler(surface, (_, e) =>
        {
            if (_assetDrag is not { } drag || !drag.Matches(e.DataTransfer.TryGetValue(AssetDragFormat))) return;
            e.Handled = true;
            e.DragEffects = DragDropEffects.None;
            bool advertised = _advertisedAssetDrop is { } target && target.Surface == surface && target.Field == field;
            ClearAssetDropFeedback();
            if (!advertised || AssetDropRefusal(drag, field) is not null || _worker is not { } worker || !drag.Queue()) return;
            e.DragEffects = DragDropEffects.Copy;
            StartAssetDrop(worker, drag, field);
        });
    }

    private void StartAssetDrop(EditorWorker worker, AssetDragIntent drag, AssetDropField? field)
    {
        _assetDropPending = true;
        ulong request = ++_assetDropRequest, interaction = _assetDropInteraction, selection = _selection.Generation;
        var focused = FocusManager?.GetFocusedElement();
        bool root = field is null;
        if (root)
        {
            _addingMesh = true;
            _addMeshState.Text = "Adding mesh…";
            UpdateAddMeshButton();
        }
        worker.DropAsset(drag, field, (node, edited, notice, finished) =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed || request != _assetDropRequest ||
                drag.Lifetime != worker.MeshDocumentLifetime) return;
            bool currentInteraction = interaction == _assetDropInteraction && selection == _selection.Generation;
            if (root) _addMeshState.Text = notice;
            else if (currentInteraction) _editLine.Text = notice;
            if (!finished) return;
            _assetDropPending = false;
            if (ReferenceEquals(drag, _assetDrag)) _assetDrag = null;
            if (root)
            {
                _addingMesh = false;
                UpdateAddMeshButton();
                if (node is EntityGuid created && currentInteraction &&
                    _hierarchyRows.Any(row => row.Node == created)) SetSelection(created, true);
            }
            if (edited && field is not null && currentInteraction &&
                !_referenceBusy && _materialWindow is null && ReferenceEquals(focused, FocusManager?.GetFocusedElement()) &&
                _inspectorShown?.AssetDropFields.Any(current => current.Field.Address == field.Field.Address &&
                    ReferenceEquals(current.Identity.NodeIdentity, field.Identity.NodeIdentity) &&
                    ReferenceEquals(current.Identity.ComponentIdentity, field.Identity.ComponentIdentity)) == true)
                FocusReferenceLauncher(field.Field.Address);
        });
    }

    private void LoadAssets()
    {
        var worker = _worker; if (worker is null || _referenceSessionFailed) return;
        long request = _assetBrowser.BeginList(); RenderAssets();
        worker.LoadAssets(result =>
        {
            if (!ReferenceEquals(worker, _worker) || !_assetBrowser.CompleteList(request, result)) return;
            RenderAssets();
        });
    }
    private void ReadAsset(AssetGuid guid, LocatedAssetReference? located = null)
    {
        var worker = _worker; if (worker is null) return;
        if (_assetBrowser.State != "Ready" || _referenceSessionFailed ||
            !_assetBrowser.Visible.Any(asset => asset.Guid == guid)) return;
        long request = _assetBrowser.Select(guid, located);
        AssetDetailIntent? intent = _assetBrowser.DetailIntent;
        RenderAssets();
        worker.ReadAsset(guid, result =>
        {
            if (ReferenceEquals(worker, _worker) && _assetBrowser.CompleteDetail(request, guid, result)) RenderAssets();
        }, intent, _assetBrowser);
    }
    private void RenderAssets()
    {
        if (_assetDrag is { } drag && (!_assetBrowser.SelectionVisible || _assetBrowser.Selected != drag.Asset.Guid ||
            !ReferenceEquals(drag.Catalog, _assetBrowser.Items)))
        {
            drag.Cancel();
            ClearAssetDropFeedback();
        }
        bool catalogFailed = _assetBrowser.State == nameof(AssetError.CatalogUnavailable);
        _assetsState.Text = catalogFailed
            ? "Catalog unavailable. Restart Runtime after repairing the catalog."
            : _assetBrowser.ViewState;
        _assetsState.Foreground = catalogFailed ? EditorVisuals.Error : EditorVisuals.Muted;
        var visible = _assetBrowser.Visible;
        _assetsList.IsEnabled = _assetBrowser.State == "Ready" && !_referenceSessionFailed;
        _assetsSync = true;
        try
        {
            EditorVisuals.BindAssetList(_assetsList, _assetBrowser.Rows,
                _assetBrowser.SelectionVisible ? _assetBrowser.Selected : null);
            if (_assetBrowser.State == "Ready" && !ReferenceEquals(_previewCatalog, _assetBrowser.Items))
            {
                _previewCatalog = _assetBrowser.Items;
                var previous = (_previewMaterial.SelectedItem as AssetSummary)?.Guid;
                var materials = _previewCatalog.Where(a => a.Type == AssetType.Material).ToArray();
                _previewMaterial.ItemsSource = materials;
                _previewMaterial.SelectedItem = materials.FirstOrDefault(a => a.Guid == previous);
            }
        }
        finally { _assetsSync = false; }
        bool hidden = _assetBrowser.State == "Ready" && _assetBrowser.Selected is not null && !_assetBrowser.SelectionVisible;
        if (hidden) _assetsState.Text += " · Selected asset is hidden";
        if (_assetCopyGuid is not null) _assetCopyGuid.IsEnabled = _assetBrowser.SelectionVisible;
        _assetClearFilters.IsEnabled = !string.IsNullOrEmpty(_assetSearch.Text) || _assetBrowser.Type is not null;
        _assetTitle.Text = _assetsList.SelectedItem is AssetSummary asset
            ? asset.Type == AssetType.Material ? EditorVisuals.ReferenceMaterialName(asset) : EditorVisuals.AssetName(asset)
            : hidden ? "Selected asset is hidden" : "Select an asset";
        ToolTip.SetTip(_assetTitle, _assetTitle.Text);
        foreach (Action refresh in _refreshReferenceLabels) refresh();
        SyncPreviewSelection();
        UpdateAddMeshButton();
        UpdateRebuildButtons();
        UpdateImportButtons();
        ReadSelectedMaterial();
        _assetDetails.Children.Clear();
        if (_assetBrowser.NeedsDetail && _assetBrowser.Selected is AssetGuid pending)
        {
            ReadAsset(pending);
            return;
        }
        if (!_assetBrowser.SelectionVisible) return;
        if (_assetsList.SelectedItem is AssetSummary summary)
            _assetDetails.Children.Add(EditorVisuals.Details(summary.Type + "\n" + summary.Source + "\n" + summary.Guid +
                (AssetBrowseProjection.EffectiveLogicalPath(summary) is string location ? "\nAssets path: " + location : "")));
        if (_assetBrowser.Details is not Atlantis.Gameplay.Assets.AssetDetails d)
        { _assetDetails.Children.Add(new TextBlock { Text = _assetBrowser.DetailState }); return; }
        _assetDetails.Children.Add(EditorVisuals.Details("artifact schema: " + d.ArtifactSchema + "\nsource schema: " + (d.SourceSchema?.ToString() ?? "none") + "\ntool: " + d.Tool));
        foreach (AssetGuid guid in d.Dependencies)
        {
            if (!_assetBrowser.Items.Any(asset => asset.Guid == guid))
            { _assetDetails.Children.Add(EditorVisuals.Details(guid.ToString())); continue; }
            var button = new Button { Content = guid.ToString() };
            button.Click += (_, _) => RevealAsset(guid);
            _assetDetails.Children.Add(button);
        }
    }

    private bool CanLocateTask(TaskIssue row) => _taskIssues.Connected && !_referenceSessionFailed &&
        _worker is { } worker && (row.DocumentLifetime is not long lifetime ||
            (worker.MeshDocumentLifetime == lifetime && _reparentDocumentOpen));

    private string? LocateTask(TaskIssue row)
    {
        if (!CanLocateTask(row)) return "The original target is no longer available.";
        if (row.DocumentLifetime is not null)
        {
            if (_retryApply.IsEnabled) _retryApply.Focus();
            else
            {
                _applyLine.Focusable = true;
                _applyLine.Focus();
            }
            return null;
        }
        if (row.Asset is not AssetGuid guid) return "The operation has no confirmed asset GUID.";
        if (_assetBrowser.State != "Ready") return "The current Assets catalog is unavailable.";
        if (!_assetBrowser.Items.Any(asset => asset.Guid == guid))
            return row.Published is not null && (!_taskIssues.ActivationKnown || row.Published != _taskIssues.ActiveGeneration)
                ? "This asset is absent from the active catalog. Check the generation and explicitly restart to activate it."
                : "The asset is no longer in the active catalog.";
        RevealAsset(guid);
        _assetTabs.SelectedIndex = 1;
        _assetsList.Focus();
        return null;
    }

    private void ObserveImportTask<T>(long attempt, object? requestedId, Outcome<T, ImportError> result,
        Func<T, GenerationId?> active, Func<T, AssetTaskReceipt?> receipt)
    {
        if (result.IsOk) _taskIssues.ObserveActivation(active(result.Value!));
        if (requestedId is not null)
            _taskIssues.Observe(attempt, requestedId, result.IsOk ? receipt(result.Value!) : null,
                result.IsOk ? null : result.Error.ToString());
    }

    private void RevealAsset(AssetGuid guid, LocatedAssetReference? located = null)
    {
        if (_referenceSessionFailed) return;
        if (located is null)
        {
            _assetBrowser.Search = "";
            _assetBrowser.Type = null;
        }
        if (!_assetBrowser.Reveal(guid)) return;
        _revealAssetFilters = true;
        try
        {
            _assetSearch.Text = _assetBrowser.Search;
            _assetTypes.SelectedIndex = _assetBrowser.Type is AssetType type ? (int)type + 1 : 0;
        }
        finally { _revealAssetFilters = false; }
        ReadAsset(guid, located);
        if (_assetsList.SelectedItem is AssetSummary asset) _assetsList.ScrollIntoView(asset);
    }

    private void ReadSelectedMaterial()
    {
        var target = _assetBrowser.State == "Ready" && _assetsList.SelectedItem is AssetSummary { Type: AssetType.Material } row
            ? row.Guid : (AssetGuid?)null;
        if (!_materialSupported || _materialReadTarget == target) { UpdateMaterialButtons(); return; }
        _materialReadTarget = target; _selectedMaterial = null;
        long epoch = ++_materialReadEpoch;
        UpdateMaterialButtons();
        if (target is not AssetGuid guid || _worker is not { } worker || _referenceSessionFailed) return;
        worker.ReadMaterial(guid, (result, legacy, packed, occlusion, combined) =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed || epoch != _materialReadEpoch ||
                _materialReadTarget != guid) return;
            _materialLegacy = legacy;
            _materialSupportsMetallicRoughness = packed;
            _materialSupportsOcclusion = occlusion;
            _materialSupportsCombinedMaps = combined;
            if (result.IsOk) _selectedMaterial = result.Value;
            else if (result.Error != MaterialError.Unsupported && !_materialSettlement.BlocksSave)
                ShowMaterialState("Selected material: " + result.Error);
            UpdateMaterialButtons();
        });
    }

    private void UpdateMaterialButtons()
    {
        bool connected = _worker is not null && !_referenceSessionFailed;
        bool open = connected && _materialWindow is null && _referenceWindow is null && _assetBrowser.State == "Ready";
        _newMaterial.IsEnabled = open && _materialCanWrite && !_materialSettlement.BlocksSave;
        _copyMaterial.IsEnabled = open && _materialCanWrite && !_materialSettlement.BlocksSave && _selectedMaterial is not null;
        _editMaterial.IsEnabled = _copyMaterial.IsEnabled && _selectedMaterial?.Editable == true;
        _materialRefresh.IsEnabled = connected && !_materialPolling && !_materialSettlement.AwaitingAdmission;
        _materialResume.IsVisible = _materialCanWrite && _materialSettlement.ResumableMaterial is not null;
        _materialResume.IsEnabled = _materialResume.IsVisible && !_materialSettlement.InFlight;
    }

    private void OpenMaterial(MaterialMode mode, Button origin)
    {
        if (!origin.IsEnabled || _materialWindow is not null) return;
        _assetDrag?.Cancel();
        var original = mode == MaterialMode.Create ? null : _selectedMaterial;
        if (mode != MaterialMode.Create && original is null) return;
        var name = _assetsList.SelectedItem is AssetSummary row ? EditorVisuals.AssetName(row) : "";
        var dialog = new MaterialEditorWindow(--_materialSurface, new MaterialDraft(mode, original), name, _assetBrowser.Items,
            !_materialLegacy, _materialSupportsMetallicRoughness != false, _materialSupportsOcclusion != false,
            _materialSupportsCombinedMaps != false);
        _materialWindow = dialog;
        dialog.DraftChanged += () => SyncPreviewSelection(invalidate: true);
        dialog.PreviewRequested += () =>
        {
            if (!_previewRequests.RequestDraft()) return;
            dialog.ShowPreview(null, "Rendering draft preview...");
            UpdatePreviewButton();
            StartNextPreview();
        };
        dialog.SaveRequested += request => SubmitMaterial(request, dialog.WithMaps, dialog.WithMetallicRoughness, dialog.WithOcclusion, dialog.WithCombinedMaps);
        dialog.ResumeRequested += ResumeMaterial;
        dialog.Closed += (_, _) =>
        {
            if (_materialWindow != dialog) return;
            _materialWindow = null; SyncPreviewSelection(invalidate: true);
            UpdateMaterialButtons(); origin.Focus();
        };
        dialog.ShowSettlement("", false, false, false);
        SyncPreviewSelection(invalidate: true); UpdateMaterialButtons();
        _ = dialog.ShowDialog(this);
    }

    private void LoseTextureConnection()
    {
        foreach (var state in _textureImports)
        {
            if (state.Admission || state.Running) state.Unknown = true;
            state.Admission = state.Polling = state.Writable = false;
        }
        _texturePoll.Stop();
        RenderTextureStatus();
    }

    private void UpdateTextureButtons()
    {
        var state = SelectedTextureImport;
        var other = _textureImports.FirstOrDefault(entry => entry != state && entry.BlocksOtherWrites);
        bool connected = _worker is not null && !_referenceSessionFailed;
        bool ready = connected && state.Writable && !_textureRepair && other is null &&
            !state.Admission && !state.Polling && !state.Unknown && !state.Running &&
            state.Job?.State != RebuildState.Succeeded;
        _textureRegister.IsEnabled = ready && !state.Committed && Codec.ValidImportName(_textureName.Text);
        _textureResume.IsEnabled = ready && (state.Committed || Codec.ValidImportName(_textureName.Text));
        _textureRefresh.IsEnabled = connected && !state.Admission && !state.Polling;
    }

    private void StartTexture(ImportMode mode)
    {
        if (_worker is not { } worker || !(mode == ImportMode.Resume ? _textureResume : _textureRegister).IsEnabled)
            return;
        var state = SelectedTextureImport;
        string name = mode == ImportMode.Resume && state.Committed ? state.Job!.Name : _textureName.Text!;
        state.Target = (name, mode);
        long task = state.Task = _taskIssues.Begin(state.Kind + " " + mode, name);
        state.Admission = true;
        state.Error = null;
        RenderTextureStatus();
        worker.ImportTexture(name, mode, state.Mode, result =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed || state.Target != (name, mode)) return;
            state.Admission = false;
            if (result.IsOk && result.Value is { } job && (job.Name, job.Mode) == (name, mode))
                _taskIssues.Admit(task, AssetTaskReceipt.From(job));
            else
                _taskIssues.Admit(task, null, result.IsOk ? "The returned job did not match the import." : result.Error.ToString(),
                    result.IsOk ? TaskResult.Unknown : TaskResult.Refused);
            if (!result.IsOk)
            {
                _textureRepair |= result.Error == ImportError.WorkspaceNeedsRepair;
                state.Writable = result.Error is not (ImportError.Unsupported or ImportError.Unavailable);
                state.Error = result.Error;
            }
            else
            {
                state.Job = result.Value;
                _texturePoll.Start();
            }
            RenderTextureStatus();
        });
    }

    private void QueryTexture() => QueryTexture(SelectedTextureImport);

    private void QueryTexture(TextureImportView state)
    {
        if (_worker is not { } worker || _referenceSessionFailed || state.Polling || state.Admission) return;
        // Latest status can confirm active generation, but only an explicit ID can settle a known running job.
        var id = state.Running ? state.Job?.Id : null;
        long task = state.Task;
        var target = state.Target;
        state.Polling = true;
        UpdateTextureButtons();
        worker.QueryTexture(id, state.Mode, result =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed) return;
            state.Polling = false;
            if (state.Admission) { UpdateTextureButtons(); return; }
            ObserveImportTask(task, id, result, status => status.ActiveGeneration, status =>
                status.Job is { } job && target == (job.Name, job.Mode) ? AssetTaskReceipt.From(job) : null);
            if (result.IsOk && result.Value.WorkspaceNeedsRepair && state.Task == task)
                _taskIssues.NeedsRepair(task);
            if (!result.IsOk)
            {
                if (id is not null) state.Unknown = true;
                state.Writable = false;
                state.Error = result.Error;
            }
            else
            {
                var status = result.Value;
                _textureRepair = status.WorkspaceNeedsRepair;
                state.Writable = true;
                state.Error = null;
                if (id is not null)
                {
                    if (status.Job is { } job && job.Id == id && state.Job is { } previous &&
                        (job.Name, job.Mode) == (previous.Name, previous.Mode))
                    {
                        state.Job = job;
                        state.Unknown = false;
                    }
                    else
                    {
                        state.Unknown = true;
                    }
                }
                if (!state.Unknown && state.Job?.Generation is { } generation && generation == status.ActiveGeneration)
                {
                    state.Job = null;
                    state.Target = null;
                }
            }
            if (!_textureImports.Any(entry => entry.Running && !entry.Unknown))
                _texturePoll.Stop();
            RenderTextureStatus();
        });
    }

    private void RenderTextureStatus()
    {
        var state = SelectedTextureImport;
        var other = _textureImports.FirstOrDefault(entry => entry != state && entry.BlocksOtherWrites);
        _textureFormat.Text = state.Mode switch
        {
            TextureImportKind.MetallicRoughness => "PNG \u00b7 linear RGBA8 \u00b7 G roughness, B metallic",
            TextureImportKind.Occlusion => "PNG · linear RGBA8 · R occlusion for indirect light",
            TextureImportKind.Normal => "PNG \u00b7 linear RGBA8 \u00b7 tangent space +Y",
            _ => "PNG · sRGB · Base texture or Emissive texture"
        };
        string text = state.Error is { } error ? $"{state.Kind} import: {error}. " : "";
        if (state.Unknown)
        {
            text += $"{state.Kind} {state.Target?.Mode} outcome unknown for {state.Target?.Name}. " +
                    "Inspect the registered texture and complete generations and verify settlement before retrying.";
            if (state.Job is { } known) text += $" Retained job: {known.Id}.";
        }
        else if (state.Admission)
        {
            text += $"Requesting {state.Kind} {state.Target?.Mode} for {state.Target?.Name}…";
        }
        else if (state.Job is { } job)
        {
            text += $"{state.Kind} {job.Name} · job {job.Id} · {job.State}.";
            if (job.Generation is { } generation)
                text += $" Texture {job.RootGuid}. Restart Runtime with --asset-generation {generation}.";
            if (job.Failure is { } failure)
                text += $" {failure.Stage}/{failure.Code}: {failure.Diagnostic} Registration: {failure.Registration}." +
                        (failure.RegisteredRootGuid is { } guid ? $" Texture {guid}. Resume after correction." : "");
        }
        if (other is not null) text += $" View {other.Kind} to check its pending import or recovery.";
        if (_textureRepair) text += " Workspace needs repair before any new write.";
        _textureState.Text = text;
        _textureState.Foreground = state.Error is not null || state.Unknown || _textureRepair || state.Job?.Failure is not null
            ? EditorVisuals.Error : EditorVisuals.Muted;
        UpdateTextureButtons();
    }

    private void SubmitMaterial(MaterialSave request, bool withMaps, bool withMetallicRoughness, bool withOcclusion, bool withCombinedMaps)
    {
        if (_worker is not { } worker || _referenceSessionFailed || !_materialCanWrite ||
            (_materialSettlement.BlocksSave && request is not MaterialSave.Resume)) return;
        _materialSettlement.Begin(request.Target);
        var target = request.Target;
        long task = _materialTask = _taskIssues.Begin(target.Mode + " material",
            target.Name ?? target.Material?.ToString() ?? "Material", target.Mode == MaterialMode.Copy ? null : target.Material);
        ShowMaterialState();
        worker.SaveMaterial(request, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps, result =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed || _materialSettlement.Target != request.Target) return;
            if (result.IsOk && result.Value is { } job && job.Target == target)
                _taskIssues.Admit(task, AssetTaskReceipt.From(job));
            else
                _taskIssues.Admit(task, null, result.IsOk ? "The returned job did not match the material." : result.Error.ToString(),
                    result.IsOk ? TaskResult.Unknown : TaskResult.Refused);
            if (!result.IsOk)
            {
                _materialSettlement.Refused();
                if (withMaps && result.Error == MaterialError.Unsupported)
                {
                    if (withCombinedMaps) _materialSupportsCombinedMaps = false;
                    else if (withOcclusion) _materialSupportsOcclusion = false;
                    else if (withMetallicRoughness) _materialSupportsMetallicRoughness = false;
                    else _materialLegacy = true;
                    _materialWindow?.OfferLegacyEditing();
                    ShowMaterialState(withCombinedMaps
                        ? "This Runtime does not support combined map editing. Choose occlusion editing explicitly or reconnect."
                        : withOcclusion
                        ? "This Runtime does not support occlusion editing. Choose metallic-roughness editing explicitly or reconnect."
                        : withMetallicRoughness
                        ? "This Runtime does not support the metallic-roughness slot. Choose normal and emissive editing explicitly or reconnect."
                        : "This Runtime supports basic material editing. Choose basic editing explicitly or reconnect.");
                    return;
                }
                if (result.Error is MaterialError.Unsupported or MaterialError.Unavailable) _materialCanWrite = false;
                ShowMaterialState("Material save refused: " + result.Error);
                return;
            }
            if (!_materialSettlement.Admit(result.Value)) return;
            _materialResumeWithMetallicRoughness = withMetallicRoughness;
            _materialResumeWithOcclusion = withOcclusion;
            _materialResumeWithCombinedMaps = withCombinedMaps;
            ShowMaterialState(); _materialPoll.Start();
        });
    }

    private void ResumeMaterial()
    {
        if (_materialResume.IsEnabled && _materialSettlement.ResumableMaterial is AssetGuid saved)
            SubmitMaterial(new MaterialSave.Resume(saved), !_materialLegacy,
                _materialResumeWithMetallicRoughness || _materialSupportsMetallicRoughness != false,
                _materialResumeWithOcclusion || _materialSupportsOcclusion != false,
                _materialResumeWithCombinedMaps || _materialSupportsCombinedMaps != false);
    }

    private void QueryMaterial()
    {
        if (_materialPolling || _materialSettlement.AwaitingAdmission || _referenceSessionFailed || _worker is not { } worker) return;
        _materialPolling = true;
        var id = _materialSettlement.Id;
        long task = _materialTask;
        var target = _materialSettlement.Target;
        UpdateMaterialButtons();
        worker.QueryMaterial(id, result =>
        {
            if (!ReferenceEquals(_worker, worker) || _referenceSessionFailed) return;
            _materialPolling = false;
            if (id != _materialSettlement.Id) { UpdateMaterialButtons(); return; }
            if (result.IsOk) _taskIssues.ObserveActivation(result.Value.ActiveGeneration);
            if (id is MaterialJobId expected)
                _taskIssues.Observe(task, expected,
                    result.IsOk && result.Value.Job is { } job && job.Target == target ? AssetTaskReceipt.From(job) : null,
                    result.IsOk ? null : result.Error.ToString());
            if (result.IsOk && result.Value.WorkspaceNeedsRepair && task == _materialTask)
                _taskIssues.NeedsRepair(task);
            if (!result.IsOk)
            {
                _materialPoll.Stop();
                if (id is not null) _materialSettlement.LoseReceipt();
                _materialSupported = result.Error != MaterialError.Unsupported;
                _materialCanWrite = result.Error is not (MaterialError.Unavailable or MaterialError.Unsupported);
                ShowMaterialState(_materialSettlement.Unknown ? null : "Material status: " + result.Error);
                return;
            }
            var status = result.Value;
            _materialSupported = true;
            _materialCanWrite = !status.WorkspaceNeedsRepair;
            if (id is not null) _materialSettlement.Observe(status);
            if (_materialSettlement.Job?.Generation is GenerationId published && status.ActiveGeneration == published)
                _materialSettlement = new();
            if (_materialSettlement.InFlight) _materialPoll.Start(); else _materialPoll.Stop();
            if (id is null && _materialSettlement.Unknown && status.Job is { } latest)
            {
                ShowMaterialState($"Latest job {latest.Id} ({latest.State}) is only a hint. " +
                    "Verify the intended durable target and confirm settlement before retrying.");
                return;
            }
            ShowMaterialState(status.WorkspaceNeedsRepair && id is null ? "Material workspace needs repair." : null);
            ReadSelectedMaterial();
        });
    }

    private void ShowMaterialState(string? message = null)
    {
        var job = _materialSettlement.Job;
        bool error = _materialSettlement.Unknown || _materialSettlement.NeedsRepair || job?.Failure is not null || message is not null;
        string target = _materialSettlement.Target is { } subject ? subject.Name ?? subject.Material?.ToString() ?? "" : "";
        string text = message ?? (_materialSettlement.Unknown
            ? $"Material outcome unknown for {target}. " +
                (_materialSettlement.Id is MaterialJobId id ? $"Keep and query job {id}. " : "") +
                "Inspect durable source and generation and confirm settlement before retrying."
            : _materialSettlement.AwaitingAdmission ? $"Requesting material save for {target}..."
            : job is null ? "" : $"Material job {job.Id}: {job.State}.");
        if (message is null && !_materialSettlement.Unknown)
        {
            if (job?.Generation is GenerationId generation)
                text += $" Material {job.SavedMaterial}. Restart Runtime explicitly with --asset-generation {generation}.";
            if (job?.Failure is { } failure)
                text += $" {failure.Stage}/{failure.Code}: {failure.Diagnostic} Source: {failure.SourceState}." +
                    (failure.SavedMaterial is AssetGuid guid ? $" Material {guid}. Resume publication after correction." : "");
            if (job?.Failure?.SourceState == RegistrationState.NotCommitted &&
                _materialSettlement.ResumableMaterial is AssetGuid previous)
                text += $" The previous material {previous} is still committed. Resume publication after correction.";
            if (_materialSettlement.NeedsRepair) text += " Workspace needs repair before any further writes.";
        }
        _materialState.Text = text;
        _materialState.Foreground = error ? EditorVisuals.Error : EditorVisuals.Muted;
        UpdateMaterialButtons();
        _materialWindow?.ShowSettlement(text, _materialSettlement.BlocksSave || !_materialCanWrite,
            _materialResume.IsEnabled, error);
    }

    private void SyncPreviewSelection(bool invalidate = false)
    {
        var visibleMesh = _assetBrowser.State == "Ready" &&
            _assetsList.SelectedItem is AssetSummary { Type: AssetType.Mesh };
        _previewMaterial.IsVisible = _previewPair.IsVisible = visibleMesh;
        _previewMaterial.IsEnabled = visibleMesh && _referenceWindow is null && _previewSession is not null;
        AssetPreviewInputs? inputs = null;
        if (_previewSession is not null)
        {
            if (_materialWindow is { } materialWindow) inputs = materialWindow.Inputs;
            else if (_referenceWindow is { } chooser) inputs = chooser.PreviewInputs;
            else if (_assetTabs.SelectedIndex == 1 && visibleMesh && _assetsList.SelectedItem is AssetSummary mesh)
                inputs = new(mesh.Guid, (_previewMaterial.SelectedItem as AssetSummary)?.Guid);
        }
        if (_previewRequests.SetInputs(inputs, invalidate)) ClearPreview();
        UpdatePreviewButton();
    }

    private void EndPreviewSession()
    {
        _previewSession = null;
        _previewMaterial.SelectedItem = null;
        SyncPreviewSelection(invalidate: true);
    }

    private void ClearPreview()
    {
        static void Clear(Image image, TextBlock state)
        {
            var previous = image.Source as Bitmap;
            image.Source = null;
            image.IsVisible = false;
            previous?.Dispose();
            state.Text = "";
        }
        _materialWindow?.ClearPreview();
        Clear(_previewImage, _previewState);
        if (_referenceWindow is { } chooser) Clear(chooser.PreviewImage, chooser.PreviewState);
        _previewRetry = _previewUnavailable = false;
    }

    private void UpdatePreviewButton()
    {
        void Update(Button button, bool withMaterial, bool surfaceCurrent, string label)
        {
            bool currentMode = withMaterial == _previewWithMaterial;
            button.Content = currentMode && _previewRetry ? "Retry preview" : label;
            button.IsEnabled = surfaceCurrent && (!currentMode || !_previewUnavailable) &&
                _previewRequests.CanRequest(withMaterial);
        }
        Update(_previewMesh, false, _referenceWindow is null, "Preview geometry");
        Update(_previewPair, true, _referenceWindow is null, "Preview with material");
        if (_referenceWindow is { } chooser) Update(chooser.PreviewButton, true, true, "Preview with material");
        _materialWindow?.SetPreviewEnabled(_previewRequests.CanRequestDraft());
    }

    private void RequestPreview(bool withMaterial)
    {
        var chooser = _referenceWindow;
        var button = withMaterial ? chooser?.PreviewButton ?? _previewPair : _previewMesh;
        if (!button.IsEnabled || !_previewRequests.Request(withMaterial)) return;
        ClearPreview();
        _previewWithMaterial = withMaterial;
        var state = chooser?.PreviewState ?? _previewState;
        state.Text = withMaterial ? "Loading material preview…" : "Loading geometry preview…";
        UpdatePreviewButton();
        StartNextPreview();
    }

    private async void StartNextPreview()
    {
        if (_previewSession is not { } session || _previewRequests.TakeNext() is not { } intent) return;
        // One worker owns each ordinary SDK connection from setup through disposal.
        // Window closure invalidates its intent without waiting on that worker.
        var result = await Task.Run(() => ReadPreview(session, intent));
        var chooser = _referenceWindow;
        if (_previewRequests.Settle(intent) && ReferenceEquals(_previewSession, session) &&
            intent.Surface == (_materialWindow?.Surface ?? chooser?.Target.Request ?? 0))
        {
            if (_materialWindow is { } draftWindow)
            {
                draftWindow.ShowPreview(result.Png, result.Message);
                UpdatePreviewButton();
                StartNextPreview();
                return;
            }
            var image = chooser?.PreviewImage ?? _previewImage;
            var state = chooser?.PreviewState ?? _previewState;
            if (result.Png is { } png)
            {
                try
                {
                    using var stream = new MemoryStream(png, writable: false);
                    image.Source = new Bitmap(stream);
                    image.IsVisible = true;
                    state.Text = intent.Material is AssetGuid material
                        ? $"Material preview · fixed lighting\nMesh: {intent.Guid}\nMaterial: {material}"
                        : "Geometry preview · unlit vertex colors";
                }
                catch (Exception error)
                {
                    ClearPreview();
                    state.Text = "Could not decode the preview. " + error.Message;
                    _previewRetry = true;
                }
            }
            else
            {
                state.Text = result.Message;
                _previewRetry = result.Retry;
                _previewUnavailable = !result.Retry;
            }
            UpdatePreviewButton();
        }
        StartNextPreview();
    }

    private (byte[]? Png, string? Message, bool Retry) ReadPreview(SessionInfo session, AssetPreviewIntent intent)
    {
        try
        {
            if (!_previewRequests.CanSend(intent)) return default;
            using var preview = RemoteSession.Connect(session);
            if (!_previewRequests.CanSend(intent)) return default;
            if (intent.Draft is MaterialRecipe recipe)
            {
                var draft = intent.WithCombinedMaps ? preview.Assets.PreviewMaterialDraftWithCombinedMaps(intent.Guid, recipe) :
                    intent.WithOcclusion ? preview.Assets.PreviewMaterialDraftWithOcclusion(intent.Guid, recipe) :
                    intent.WithMetallicRoughness ? preview.Assets.PreviewMaterialDraftWithMetallicRoughness(intent.Guid, recipe) :
                    intent.WithMaps ? preview.Assets.PreviewMaterialDraftWithMaps(intent.Guid, recipe)
                    : preview.Assets.PreviewMaterialDraft(intent.Guid, recipe);
                return draft.IsOk ? (draft.Value.PngBytes, null, false) :
                    (null, "Draft preview failed: " + draft.Error + ". Check the mesh and base texture or retry.", true);
            }
            if (intent.Material is AssetGuid material)
            {
                var pair = preview.Assets.PreviewMeshMaterial(intent.Guid, material);
                if (pair.IsOk) return (pair.Value.PngBytes, null, false);
                return pair.Error switch
                {
                    MeshMaterialPreviewError.Unsupported => (null, "Material preview requires an updated Runtime.", false),
                    MeshMaterialPreviewError.Unavailable => (null, "Runtime has no usable material preview context.", false),
                    MeshMaterialPreviewError.CatalogUnavailable => (null, "Catalog unavailable. Restart Runtime after repairing the build.", false),
                    MeshMaterialPreviewError.Busy => (null, "Runtime is busy with another preview. Retry when ready.", true),
                    MeshMaterialPreviewError.UnsupportedMaterial => (null, "UnsupportedMaterial. Choose an opaque base PBR material with one sRGB RGBA8 texture.", false),
                    MeshMaterialPreviewError.DependencyUnavailable => (null, "DependencyUnavailable. Repair the material's cooked base texture and retry.", true),
                    MeshMaterialPreviewError.TooLarge => (null, "TooLarge. Choose a smaller mesh or material texture.", false),
                    MeshMaterialPreviewError.UnknownMesh or MeshMaterialPreviewError.NotMesh => (null, "Select a mesh from this Runtime's catalog.", false),
                    MeshMaterialPreviewError.UnknownMaterial or MeshMaterialPreviewError.NotMaterial => (null, "Select a material from this Runtime's catalog.", false),
                    MeshMaterialPreviewError.InvalidMesh or MeshMaterialPreviewError.InvalidMaterial =>
                        (null, $"{pair.Error}. Repair the cooked asset and retry.", true),
                    MeshMaterialPreviewError.IncompatiblePair => (null, "IncompatiblePair. Choose a mesh that can be framed with its cooked UVs and normals.", false),
                    _ => (null, "Material preview failed: " + pair.Error + ". See the Runtime log and retry.", true),
                };
            }
            var result = preview.Assets.PreviewMesh(intent.Guid);
            if (result.IsOk) return (result.Value.PngBytes, null, false);
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
        var header = EditorVisuals.Bar(EditorVisuals.Stack(2,
            EditorVisuals.Columns("*,Auto", EditorVisuals.Heading("Saved scene units"), _savedRefresh),
            EditorVisuals.Secondary("Visual Studio 18 2026 · x64 · Debug")));
        var checkout = EditorVisuals.Disclosure("Checkout details",
            EditorVisuals.Details(_options.SavedSceneCheckout ?? ""));
        _savedHint.Foreground = EditorVisuals.Muted;
        _savedBuildResult.MaxHeight = 170;
        _savedOpen.Classes.Add("editor-primary");
        var operations = EditorVisuals.Group(EditorVisuals.Heading("Build and open"),
            EditorVisuals.Stack(6, EditorVisuals.Secondary("Committed managed import root (optional)"),
                _savedManaged, _savedBuildSummary, _savedOpenResult, EditorVisuals.Actions(_savedBuild, _savedOpen),
                EditorVisuals.Disclosure("Build details", _savedBuildResult)));
        var body = EditorVisuals.Stack(6, checkout, _savedHint, _savedDiscovery, _savedList, operations);
        body.Margin = new Thickness(8);
        var panel = new DockPanel();
        AddDocked(panel, header, Dock.Top);
        panel.Children.Add(EditorVisuals.Scroll(body));
        panel.SizeChanged += (_, e) => _savedList.Height = e.NewSize.Height < 510 ? 100 : 126;

        AutomationProperties.SetName(_savedList, "Local saved scenes");
        AutomationProperties.SetName(_savedManaged, "Committed managed import root");
        _savedRefresh.Click += (_, _) => RefreshSavedScenes();
        _savedList.SelectionChanged += async (_, _) => await CheckSavedSelection();
        _savedManaged.TextChanged += async (_, _) => await CheckSavedSelection();
        _savedBuild.Click += async (_, _) => await BuildSelectedSavedScene();
        _savedOpen.Click += async (_, _) => await OpenSelectedSavedScene();
        return panel;
    }

    private void ShowSavedSceneReceipt(SavedSceneReceipt? receipt)
    {
        if (_savedWorkspace is null || receipt is null) return;
        _savedHint.Text = $"Receipt for {receipt.Name}: {receipt.Source} ({receipt.Scene}). " +
            "Refresh and select the local unit explicitly. A matching name does not verify the checkout.";
    }

    private SavedSceneInput? SelectedSavedInput()
    {
        if (_savedList.SelectedItem is not string name) return null;
        string? text = string.IsNullOrWhiteSpace(_savedManaged.Text) ? null : _savedManaged.Text.Trim();
        if (text is not null && Path.IsPathFullyQualified(text))
        {
            try { text = Path.TrimEndingDirectorySeparator(Path.GetFullPath(text)); }
            catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { }
        }
        return new(name, text);
    }

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
        _savedOpenResult.Foreground = EditorVisuals.Warning;
    }

    private async Task CheckSavedSelection()
    {
        if (_savedWorkspace is not { } workspace || _savedAttempts.Busy || _shuttingDown) return;
        _savedCheck?.Cancel();
        long check = _savedAttempts.BeginCheck();
        UpdateSavedSceneButtons();
        if (SelectedSavedInput() is not { } input)
        {
            _savedOpenResult.Text = "Select a saved scene to check the previous build.";
            _savedOpenResult.Foreground = EditorVisuals.Muted;
            return;
        }

        _savedOpenResult.Text = "Checking the previous build for " + input.Name + "…";
        _savedOpenResult.Foreground = EditorVisuals.Muted;
        var fresh = _savedFresh is { Recorded: false } current && current.Input == input ? current : null;
        using var cancel = new System.Threading.CancellationTokenSource();
        _savedCheck = cancel;
        SavedSceneEligibility eligibility;
        try
        {
            eligibility = await Task.Run(() => fresh is null ? workspace.CheckStored(input, cancel.Token) :
                workspace.IsCurrent(fresh) ? new(fresh, "Ready to open the build from this session.") :
                new SavedSceneEligibility(null, "The saved unit or executable changed. Build again."));
        }
        catch (OperationCanceledException) when (cancel.IsCancellationRequested) { return; }
        finally { if (ReferenceEquals(_savedCheck, cancel)) _savedCheck = null; }
        if (_shuttingDown || input != SelectedSavedInput() || !_savedAttempts.FinishCheck(check, eligibility.Build)) return;

        _savedOpenResult.Text = eligibility.Detail;
        _savedOpenResult.Foreground = eligibility.Build is null ? EditorVisuals.Warning : EditorVisuals.Success;
        if (eligibility.Build is { Recorded: true } recorded)
        {
            ShowSavedBuild(input, new("Ready to open", "Verified recorded build", recorded.Directory,
                Path.Combine(recorded.Directory, "build.log"), recorded));
        }
        else if (fresh is null)
        {
            _savedBuildSummary.Text = "Build required";
            _savedBuildSummary.Foreground = EditorVisuals.Muted;
            _savedBuildResult.Text = eligibility.Detail;
        }
        UpdateSavedSceneButtons();
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
        if (_savedFresh is { } fresh && !names.Contains(fresh.Input.Name, StringComparer.Ordinal)) _savedFresh = null;
        if (_savedAttempts.Result is { } result && !names.Contains(result.Input.Name, StringComparer.Ordinal))
            InvalidateSavedResult("The built unit is no longer listed. Build again after restoring it.");
        _ = CheckSavedSelection();
    }

    private void ShowSavedBuild(SavedSceneInput input, SavedSceneBuildReport report)
    {
        _savedBuildSummary.Text = $"{report.Phase}: {report.Detail}";
        _savedBuildSummary.Foreground = report.Result is not null ? EditorVisuals.Success :
            report.Phase == "Failed" ? EditorVisuals.Error : EditorVisuals.Muted;
        _savedBuildResult.Text = $"{report.Phase}: {report.Detail}\nScene: {input.Name}\n" +
            $"Visual Studio 18 2026 · x64 · Debug\nManaged root: {input.ManagedRoot ?? "(none)"}\n" +
            $"Build directory: {report.Directory}\nLog: {(report.Log.Length == 0 ? "(unavailable)" : report.Log)}" +
            (report.Result is { } built ? "\nRuntime: " + built.Executable : "");
    }

    private async Task BuildSelectedSavedScene()
    {
        if (_savedWorkspace is not { } workspace || SelectedSavedInput() is not { } input ||
            !_savedAttempts.BeginBuild(input)) return;
        _savedCheck?.Cancel();
        _savedFresh = null;
        _savedOpenResult.Text = "";
        _savedBuildResult.Text = _savedBuildSummary.Text = "Preparing a fresh build for " + input.Name;
        _savedBuildSummary.Foreground = EditorVisuals.Muted;
        UpdateSavedSceneButtons();
        var progress = new Progress<SavedSceneBuildReport>(report =>
        {
            if (_savedAttempts.Building) ShowSavedBuild(input, report);
        });
        var result = await Task.Run(() => workspace.BuildAsync(input, ((IProgress<SavedSceneBuildReport>)progress).Report));
        _savedAttempts.FinishBuild(result.Result);
        _savedFresh = result.Result;
        ShowSavedBuild(input, result);
        UpdateSavedSceneButtons();
    }

    private async Task OpenSelectedSavedScene()
    {
        if (_savedWorkspace is not { } workspace || SelectedSavedInput() is not { } input ||
            _savedAttempts.BeginOpen(input) is not { } result) return;
        _savedOpenResult.Text = "Starting a separate Editor for " + input.Name;
        _savedOpenResult.Foreground = EditorVisuals.Muted;
        UpdateSavedSceneButtons();
        Process? child = null;
        try
        {
            string? refusal;
            (child, refusal) = await Task.Run(() => workspace.Open(result, _options.SavedSceneAppHost!));
            if (refusal is not null)
            {
                _savedFresh = null;
                InvalidateSavedResult(refusal);
            }
            else
                _savedOpenResult.Text = $"Editor started for {input.Name}. Check its window for Runtime and document readiness.";
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e))
        {
            _savedOpenResult.Text = "Editor start failed. Retry Open if the build is still current. " + e.Message;
            _savedOpenResult.Foreground = EditorVisuals.Error;
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
            if (ReferenceEquals(_savedAttempts.Result, result))
            {
                bool current = await Task.Run(() => result.Recorded ? workspace.CheckStored(input).Build == result : workspace.IsCurrent(result));
                if (!current && ReferenceEquals(_savedAttempts.Result, result))
                {
                    _savedFresh = null;
                    InvalidateSavedResult("The child closed and the saved unit or build inputs changed. Build again.");
                }
            }
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
        _rebuildStart.IsEnabled = available && !_rebuildRunning && !_renameUnknown && _assetBrowser.State == "Ready" &&
                                  _assetsList.SelectedItem is AssetSummary;
        _rebuildRefresh.IsEnabled = available && (!_renameUnknown || _rebuildJob is not null);
        string? name = _assetBrowser.SelectionVisible
            ? AssetBrowser.RegisteredColorName(_assetsList.SelectedItem as AssetSummary) : null;
        _rebuildStart.Content = name is null ? "Rebuild selected" : "Reimport PNG";
        _reimportInput.IsVisible = name is not null;
        _reimportInput.Text = name is null ? "" : "Update inbox/" + name +
            "/source.png in the configured project. Use the active named generation.";
        UpdateReferences();
        bool editing = _assetBrowser.RenameIntent is not null && _renameDraftGeneration is not null;
        bool checkingRemoval = _assetBrowser.RemovalIntent is not null && !_referenceSessionFailed;
        if (!checkingRemoval)
        {
            _removalPoll.Stop();
            _removalReceipt = null;
            _removalQueryPending = false;
            _removalState.Text = "";
        }
        _removalState.IsVisible = checkingRemoval;
        bool moving = _assetBrowser.MoveIntent is not null;
        if (_moveEditor is not null) _moveEditor.IsVisible = moving && !_referenceSessionFailed;
        _moveTexture.IsVisible = name is not null && !editing && !checkingRemoval && !moving;
        _moveTexture.IsEnabled = available && !_rebuildRunning && !_renameUnknown && _moveTexture.IsVisible;
        _movePublish.IsEnabled = moving && _moveDraftGeneration is not null && available && !_rebuildRunning && !_renameUnknown &&
            _moveDestination.Text is string destination && Codec.ValidTextureLogicalPath(destination);
        _moveCancel.IsEnabled = moving && !_rebuildPending;
        _removeRegistration.IsVisible = name is not null && !editing && !checkingRemoval && !moving;
        _removeRegistration.IsEnabled = available && !_rebuildRunning && !_renameUnknown;
        _confirmRemoval.IsVisible = checkingRemoval && _removalReceipt?.State == TextureRemovalState.Ready;
        _confirmRemoval.IsEnabled = _confirmRemoval.IsVisible && available && !_rebuildRunning && !_renameUnknown;
        _cancelRemoval.IsVisible = checkingRemoval;
        _renameTexture.IsVisible = name is not null && !editing && !moving;
        _renameTexture.IsEnabled = available && !_rebuildRunning && !_renameUnknown && name is not null && !checkingRemoval && !moving;
        if (_renameEditor is not null) _renameEditor.IsVisible = editing && !_referenceSessionFailed;
        _renamePublish.IsEnabled = editing && available && !_rebuildRunning && !_renameUnknown && Codec.ValidImportName(_renameLabel.Text);
        _renameCancel.IsEnabled = editing;
    }

    private void BeginTextureRemoval()
    {
        if (!_removeRegistration.IsEnabled || _worker is not { } worker ||
            _assetBrowser.BeginRemoval() is not { } intent) return;
        _removalState.Text = "Checking project references…";
        UpdateRebuildButtons();
        worker.QueryRebuild(null, status =>
        {
            if (!ReferenceEquals(worker, _worker) || !intent.Intent.Current || _referenceSessionFailed) return;
            if (!status.IsOk || status.Value!.ActiveGeneration is not GenerationId generation)
            {
                _removalState.Text = "Removal requires a compatible Runtime and an active named generation.";
                return;
            }
            _activeAssetGeneration = generation;
            ReadTextureRemoval(worker, intent, generation, null);
        });
    }

    private void ReadTextureRemoval(EditorWorker worker, TextureRemovalIntent intent, GenerationId generation,
        TextureRemovalCheckId? id)
    {
        if (_removalQueryPending) return;
        _removalQueryPending = true;
        worker.CheckTextureRemoval(intent, generation, id, (result, diagnostic) =>
        {
            if (!ReferenceEquals(worker, _worker) || !intent.Intent.Current || _referenceSessionFailed) return;
            _removalQueryPending = false;
            if (result is not { IsOk: true } accepted || accepted.Value is not { } check ||
                check.Asset != intent.Asset.Guid || check.ExpectedGeneration != generation ||
                generation != _activeAssetGeneration || id is { } expected && check.Id != expected)
            {
                _removalReceipt = null;
                _removalPoll.Stop();
                _removalState.Text = diagnostic ?? "Cannot verify removal eligibility. Check again. " + result?.Error;
                UpdateRebuildButtons();
                return;
            }
            _removalReceipt = check;
            if (check.State == TextureRemovalState.Checking) _removalPoll.Start(); else _removalPoll.Stop();
            string png = "inbox/" + AssetBrowser.RegisteredColorName(intent.Asset) + "/source.png";
            _removalState.Text = check.State switch
            {
                TextureRemovalState.Checking => "Checking project references…",
                TextureRemovalState.Ready => $"Remove {EditorVisuals.AssetName(intent.Asset)} from the registry? The original {png} is kept.",
                TextureRemovalState.Referenced => "Removal refused. Referenced by:\n" +
                    string.Join("\n", check.Blockers.Select(blocker =>
                        blocker.Source + (blocker.Slot is { } slot ? $" ({slot})" : ""))) +
                    (check.Truncated ? "\nAdditional references omitted." : "") +
                    (check.Incomplete ? "\nThe check was incomplete." : ""),
                _ => "Removal refused. " + check.State + ". " + check.Diagnostic,
            };
            UpdateRebuildButtons();
        });
    }

    private void ConfirmTextureRemoval()
    {
        if (!_confirmRemoval.IsEnabled || _worker is not { } worker || _assetBrowser.RemovalIntent is not { } intent ||
            _removalReceipt is not { State: TextureRemovalState.Ready } check || check.Asset != intent.Asset.Guid ||
            check.ExpectedGeneration != _activeAssetGeneration) return;
        _rebuildPending = true;
        _rebuildJob = null;
        _assetWriteLabel = "Remove registration";
        _renameRequestContext = $"{check.Asset} in generation {check.ExpectedGeneration}";
        _rebuildState.Text = "Requesting removal…";
        long task = _rebuildTask = _taskIssues.Begin("Remove registration", EditorVisuals.AssetName(intent.Asset), check.Asset);
        UpdateRebuildButtons();
        worker.RemoveTexture(intent, check, (result, diagnostic, refusal, remoteError) =>
        {
            if (!ReferenceEquals(worker, _worker)) return;
            CompleteAssetMutation(task, check.Asset, result, remoteError?.ToString() ?? diagnostic, refusal);
            _rebuildPending = false;
            _assetBrowser.CancelRemoval();
            if (result is { IsOk: true } accepted) ShowRebuild(accepted.Value!);
            else
            {
                _renameUnknown = diagnostic?.Contains("outcome unknown", StringComparison.Ordinal) == true;
                _rebuildState.Text = diagnostic ?? "Removal refused. Check again. " + result?.Error;
                if (!_renameUnknown) _renameRequestContext = null;
            }
            UpdateRebuildButtons();
        });
    }

    private void BeginTextureRename()
    {
        if (!_renameTexture.IsEnabled || _worker is not { } worker || _assetBrowser.BeginRename() is not { } intent) return;
        _renameDraftGeneration = null;
        _rebuildPending = true;
        // Discovery and status come from the same worker connection.
        UpdateRebuildButtons();
        worker.QueryRebuild(null, status =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _rebuildPending = false;
            if (!intent.Intent.Current) { UpdateRebuildButtons(); return; }
            if (!status.IsOk || status.Value!.ActiveGeneration is not GenerationId generation)
            {
                _assetBrowser.CancelRename();
                _rebuildState.Text = status.IsOk ? "Rename requires an active named generation." :
                    "Rename requires a compatible managed Runtime: " + status.Error;
            }
            else if (status.Value.Job?.State == RebuildState.Running)
            {
                _assetBrowser.CancelRename();
                _rebuildState.Text = "A managed asset job is running. Check its status before renaming.";
            }
            else
            {
                _activeAssetGeneration = _renameDraftGeneration = generation;
                _renamePath.Text = "Original input: inbox/" + AssetBrowser.RegisteredColorName(intent.Asset) + "/source.png";
                _renameLabel.Text = EditorVisuals.AssetName(intent.Asset);
            }
            UpdateRebuildButtons();
            if (_renameEditor?.IsVisible == true) { _renameLabel.Focus(); _renameLabel.SelectAll(); }
        });
    }

    private void PublishTextureRename()
    {
        if (!_renamePublish.IsEnabled || _worker is not { } worker || _assetBrowser.RenameIntent is not { } intent ||
            _renameDraftGeneration is not GenerationId generation || _renameLabel.Text is not string label) return;
        _rebuildPending = true; _rebuildJob = null;
        _assetWriteLabel = "Rename";
        _renameRequestContext = $"{intent.Asset.Guid} in generation {generation} to '{label}'";
        _rebuildState.Text = "Requesting rename…";
        long task = _rebuildTask = _taskIssues.Begin("Rename texture", EditorVisuals.AssetName(intent.Asset) + " → " + label, intent.Asset.Guid);
        UpdateRebuildButtons();
        worker.RenameTexture(intent, generation, label, (result, diagnostic, refusal, remoteError) =>
        {
            if (!ReferenceEquals(worker, _worker)) return;
            CompleteAssetMutation(task, intent.Asset.Guid, result, remoteError?.ToString() ?? diagnostic, refusal);
            _rebuildPending = false;
            _assetBrowser.CancelRename();
            _renameDraftGeneration = null;
            if (result is { IsOk: true } accepted) ShowRebuild(accepted.Value!);
            else
            {
                _renameUnknown = diagnostic?.Contains("outcome unknown", StringComparison.Ordinal) == true;
                _rebuildState.Text = diagnostic ?? (result?.Error switch
                {
                    RenameTextureError.NameConflict => "Rename refused: NameConflict. Choose an unused display name.",
                    RenameTextureError.StaleSnapshot => "Rename refused: StaleSnapshot. Restart into the committed generation and check the asset.",
                    RenameTextureError.Unsupported => "Rename is unsupported by the connected Runtime.",
                    _ => "Rename refused: " + result?.Error,
                });
                if (!_renameUnknown) _renameRequestContext = null;
            }
            UpdateRebuildButtons();
        });
    }

    private void BeginTextureMove()
    {
        if (!_moveTexture.IsEnabled || _worker is not { } worker || _assetBrowser.BeginMove() is not { } intent) return;
        _moveDraftGeneration = null;
        _rebuildPending = true;
        // Discovery and status come from the same worker connection.
        UpdateRebuildButtons();
        worker.QueryRebuild(null, status =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _rebuildPending = false;
            if (!intent.Intent.Current) { UpdateRebuildButtons(); return; }
            if (!status.IsOk || status.Value!.ActiveGeneration is not GenerationId generation)
            {
                _assetBrowser.CancelMove();
                _rebuildState.Text = status.IsOk ? "Move requires an active named generation." :
                    "Move requires a compatible managed Runtime: " + status.Error;
            }
            else if (status.Value.Job?.State == RebuildState.Running)
            {
                _assetBrowser.CancelMove();
                _rebuildState.Text = "A managed asset job is running. Check its status before renaming.";
            }
            else
            {
                _activeAssetGeneration = _moveDraftGeneration = generation;
                _movePaths.Text = "Current Assets path: " + AssetBrowseProjection.EffectiveLogicalPath(intent.Asset) + "\nOriginal input: inbox/" + AssetBrowser.RegisteredColorName(intent.Asset) + "/source.png";
                _moveDestination.Text = AssetBrowseProjection.EffectiveLogicalPath(intent.Asset);
            }
            UpdateRebuildButtons();
            if (_moveEditor?.IsVisible == true) { _moveDestination.Focus(); _moveDestination.SelectAll(); }
        });
    }

    private void PublishTextureMove()
    {
        if (!_movePublish.IsEnabled || _worker is not { } worker || _assetBrowser.MoveIntent is not { } intent ||
            _moveDraftGeneration is not GenerationId generation || _moveDestination.Text is not string destination ||
            AssetBrowseProjection.EffectiveLogicalPath(intent.Asset) is not string expectedPath) return;
        _rebuildPending = true; _rebuildJob = null;
        _assetWriteLabel = "Move";
        _renameRequestContext = $"{intent.Asset.Guid} in generation {generation} to '{destination}'";
        _rebuildState.Text = "Requesting move…";
        long task = _rebuildTask = _taskIssues.Begin("Move texture", expectedPath + " → " + destination, intent.Asset.Guid);
        UpdateRebuildButtons();
        worker.MoveTexture(intent, generation, expectedPath, destination, (result, diagnostic, refusal, remoteError) =>
        {
            if (!ReferenceEquals(worker, _worker)) return;
            CompleteAssetMutation(task, intent.Asset.Guid, result, remoteError?.ToString() ?? diagnostic, refusal);
            _rebuildPending = false;
            _assetBrowser.CancelMove();
            _moveDraftGeneration = null;
            if (result is { IsOk: true } accepted) ShowRebuild(accepted.Value!);
            else
            {
                _renameUnknown = diagnostic?.Contains("outcome unknown", StringComparison.Ordinal) == true;
                _rebuildState.Text = diagnostic ?? (result?.Error switch
                {
                    MoveTextureError.PathConflict => "Move refused: PathConflict. Choose a vacant Assets path.",
                    MoveTextureError.StaleSnapshot => "Move refused: StaleSnapshot. Restart into the committed generation and check the asset.",
                    MoveTextureError.Unsupported => "Move is unsupported by the connected Runtime.",
                    _ => "Move refused: " + result?.Error,
                });
                if (!_renameUnknown) _renameRequestContext = null;
            }
            UpdateRebuildButtons();
        });
    }

    private void UpdateReferences()
    {
        bool eligible = _assetBrowser.CanScanReferences;
        bool scanning = _assetBrowser.DependentScan is not null;
        bool locating = _assetBrowser.ReferenceLocate is not null;
        bool available = _worker is not null && !_referenceSessionFailed;
        var result = _assetBrowser.DependentResult;
        _viewReferences.IsVisible = eligible;
        _viewReferences.Content = result is null ? "View References" : "Refresh References";
        _viewReferences.IsEnabled = eligible && available && !scanning;
        _cancelReferences.IsVisible = eligible && (scanning || locating);
        _referenceScope.IsVisible = eligible || result is not null;
        _referenceStatus.Text = _assetBrowser.ReferenceNotice ?? (scanning ? "Checking direct material references…" :
            locating ? "Verifying the material slot…" : result switch
            {
                { Complete: true, Matches.Count: 0 } => "No direct material references in this catalog.",
                { Complete: true } => "Check complete.",
                { Complete: false } => "Check incomplete. " + (result.Diagnostic ?? "Some material references could not be verified."),
                _ => "",
            });
        _referenceStatus.IsVisible = !string.IsNullOrEmpty(_referenceStatus.Text);
        _referenceResults.IsEnabled = eligible && available && !scanning;
        _referenceResults.IsVisible = result?.Matches.Count > 0;
        if (!ReferenceEquals(_referenceResults.ItemsSource, result?.Matches)) _referenceResults.ItemsSource = result?.Matches;

        var located = _assetBrowser.LocatedReference;
        if (!ReferenceEquals(_shownReference, located))
        {
            _shownReference = located;
            _locatedSlots.Children.Clear();
            if (located is not null) ShowLocatedSlots(located);
        }
    }

    private void ShowLocatedSlots(LocatedAssetReference located)
    {
        _locatedSlots.Children.Add(EditorVisuals.Heading("Material texture slots"));
        TextBox? target = null;
        foreach (var (slot, texture) in MaterialTextureReferences.Slots(located.Values))
        {
            if (texture is not AssetGuid guid || guid.IsNil) continue;
            var asset = _assetBrowser.Items.FirstOrDefault(item => item.Guid == guid);
            var row = EditorVisuals.ReadOnlyTextureSlot(slot, asset is null ? guid.ToString() : EditorVisuals.AssetName(asset), guid);
            _locatedSlots.Children.Add(row);
            if (slot == located.Slot) target = row;
        }
        if (target is null) return;
        Dispatcher.UIThread.Post(() =>
        {
            if (_shuttingDown || _assetTabs.SelectedIndex != 1 || !ReferenceEquals(_assetBrowser.LocatedReference, located)) return;
            target.BringIntoView();
            target.Focus();
        }, DispatcherPriority.Loaded);
    }

    private void ViewReferences()
    {
        if (!_viewReferences.IsEnabled || _worker is not { } worker ||
            _assetBrowser.BeginDependents(_activeAssetGeneration) is not { } scan) return;
        UpdateReferences();
        worker.ReadAffectedMaterials(scan, result =>
        {
            if (!_shuttingDown && ReferenceEquals(worker, _worker) && _assetBrowser.CompleteDependents(scan, result))
                UpdateReferences();
        });
    }

    private void LocateReference(AssetReferenceRow row)
    {
        if (_referenceSessionFailed || _worker is not { } worker ||
            _assetBrowser.BeginReferenceLocate(row, _activeAssetGeneration) is not { } locate) return;
        UpdateReferences();
        worker.LocateAssetReference(locate, read =>
        {
            if (_shuttingDown || !ReferenceEquals(worker, _worker) || _referenceSessionFailed ||
                !_assetBrowser.CompleteReferenceLocate(locate, read, out var located)) return;
            if (located is not null) RevealAsset(located.Material, located);
            else UpdateReferences();
        });
    }

    private void CompleteAssetMutation<TError>(long task, AssetGuid target, Outcome<RebuildJob, TError>? result,
        string? diagnostic, TaskResult refusal) where TError : struct, Enum
    {
        if (result is { IsOk: true, Value: { } job })
        {
            if (job.Asset == target) _taskIssues.Admit(task, AssetTaskReceipt.From(job));
            else _taskIssues.Admit(task, null, "The returned job did not match the asset.", TaskResult.Unknown);
        }
        else _taskIssues.Admit(task, null, diagnostic ?? result?.Error.ToString(), refusal);
    }

    private void StartAssetRebuild()
    {
        if (!_rebuildStart.IsEnabled || _worker is not { } worker ||
            _assetsList.SelectedItem is not AssetSummary selected) return;
        _assetBrowser.CancelRename();
        _renameRequestContext = null;
        _rebuildPending = true; _rebuildJob = null;
        long task = _rebuildTask = _taskIssues.Begin("Rebuild asset", EditorVisuals.AssetName(selected), selected.Guid);
        _rebuildState.Text = "Requesting rebuild…";
        UpdateRebuildButtons();
        worker.RebuildAsset(selected.Guid, result =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _rebuildPending = false;
            CompleteAssetMutation(task, selected.Guid, (Outcome<RebuildJob, RebuildError>?)result, null, TaskResult.Refused);
            if (result.IsOk) ShowRebuild(result.Value!);
            else
            {
                string guidance = AssetBrowser.RegisteredColorName(selected) is null ? "" : result.Error switch
                {
                    RebuildError.Unavailable => " Check the managed profile and activate the latest committed generation.",
                    RebuildError.UnsupportedProducer => " Use an active named generation and a Runtime that supports this registered color texture.",
                    RebuildError.WorkspaceNeedsRepair => " Keep using the previous generation and inspect the workspace offline.",
                    _ => "",
                };
                _rebuildState.Text = "Rebuild refused: " + result.Error + "." + guidance;
            }
            UpdateRebuildButtons();
        });
    }

    private void ReadRebuildStatus()
    {
        if (!_rebuildRefresh.IsEnabled || _worker is not { } worker) return;
        _rebuildPending = true;
        UpdateRebuildButtons();
        var id = _rebuildJob;
        long task = _rebuildTask;
        worker.QueryRebuild(id, result =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _rebuildPending = false;
            if (result.IsOk) _taskIssues.ObserveActivation(result.Value.ActiveGeneration);
            if (id is RebuildId expected)
                _taskIssues.Observe(task, expected, result.IsOk && result.Value.Job is { } observed &&
                    _taskIssues.Find(task)?.Asset == observed.Asset ? AssetTaskReceipt.From(observed) : null,
                    result.IsOk ? null : result.Error.ToString());
            if (result.IsOk)
            {
                _activeAssetGeneration = result.Value!.ActiveGeneration;
                _renameUnknown = false;
                if (_assetBrowser.RenameIntent is not null && _renameDraftGeneration != _activeAssetGeneration)
                    _assetBrowser.CancelRename();
                ShowRebuild(result.Value.Job);
            }
            else
            {
                _rebuildPoll.Stop(); _rebuildRunning = false;
                if (result.Error == RebuildError.UnknownJob && _renameRequestContext is string target && _rebuildJob is RebuildId id)
                {
                    _renameUnknown = true;
                    _rebuildState.Text = $"{_assetWriteLabel} {target}, job {id}: outcome unknown (record expired). Inspect the workspace before retrying.";
                }
                else
                {
                    if (result.Error == RebuildError.UnknownJob) _rebuildJob = null;
                    _rebuildState.Text = "Rebuild status: " + result.Error;
                }
            }
            UpdateRebuildButtons();
        });
    }

    private void ShowRebuild(RebuildJob? job)
    {
        _rebuildJob = job?.Id;
        _rebuildRunning = job?.State == RebuildState.Running;
        if (_rebuildRunning) _rebuildPoll.Start(); else _rebuildPoll.Stop();
        string action = _renameRequestContext is null ? "Rebuild" : _assetWriteLabel;
        string state = job is null ? "No admitted job" : $"{action} {job.Id} · {job.State}";
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
        long task = _importTask = _taskIssues.Begin(mode + " scene import", name);
        _importState.Text = "Requesting import…";
        UpdateImportButtons();
        worker.ImportAsset(name, mode, result =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _importPending = false;
            if (result.IsOk && result.Value is { } job && (job.Name, job.Mode) == (name, mode))
                _taskIssues.Admit(task, AssetTaskReceipt.From(job));
            else
                _taskIssues.Admit(task, null, result.IsOk ? "The returned job did not match the import." : result.Error.ToString(),
                    result.IsOk ? TaskResult.Unknown : TaskResult.Refused);
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
        var id = _importJob;
        long task = _importTask;
        worker.QueryImport(id, result =>
        {
            if (!ReferenceEquals(worker, _worker) || _referenceSessionFailed) return;
            _importPending = false;
            ObserveImportTask(task, id, result, status => status.ActiveGeneration, status =>
                status.Job is { } job && _taskIssues.Find(task) is { } row &&
                row.Target == job.Name && row.Operation == job.Mode + " scene import" ? AssetTaskReceipt.From(job) : null);
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

    private void ClearBatchInspector()
    {
        _batchView = null;
        _batchDraft = null;
        _batchInputs.Clear();
        _batchButtons.Clear();
    }

    private void DiscardBatchDraft(BatchInspectorSnapshot snapshot)
    {
        if (!ReferenceEquals(_batchView, snapshot) || _batchDraft is not { } draft) return;
        _batchDraft = null;
        string path = draft.Field.Field.Path;
        if (!_batchInputs.TryGetValue(path, out TextBox? input)) return;
        _resettingBatchInput = true;
        try
        {
            input.Text = snapshot.Fields.FirstOrDefault(field => field.Field.Path == path)?.Field.Text ?? "";
        }
        finally { _resettingBatchInput = false; }
    }

    private void OnBatchInspectorChanged(BatchInspectorSnapshot snapshot)
    {
        if (_selection.Nodes.Count < 2 || snapshot.SelectionGeneration != _selection.Generation ||
            snapshot.Lifetime != _meshDocumentLifetime) return;
        if (_batchDraft is { } draft && snapshot.Revision is not null)
        {
            var fresh = snapshot.Fields.FirstOrDefault(field => field.Field.Path == draft.Field.Field.Path);
            if (fresh is null || !fresh.Field.Editable || !fresh.Targets.SequenceEqual(draft.Field.Targets))
            {
                _batchDraft = null;
                _editLine.Text = "The property changed elsewhere. Review the new values before editing.";
            }
        }
        _batchView = snapshot;
        BuildBatchInspector(snapshot);
    }

    private void BuildBatchInspector(BatchInspectorSnapshot snapshot)
    {
        var focused = _batchInputs.FirstOrDefault(pair => pair.Value.IsFocused);
        int caret = focused.Value?.CaretIndex ?? 0;
        _batchInputs.Clear();
        _batchButtons.Clear();
        _inspectorStructureButtons.Clear();
        _nameBox = null;
        _inspectorShown = _inspectorDeferred = null;
        _refreshReferenceLabels.Clear();
        _inspectorPanel.Children.Clear();
        _invalidatedInput = null;
        _inspectorPanel.Children.Add(EditorVisuals.Heading($"{snapshot.Count} selected nodes", 17));
        _hiddenBatchStatus.IsVisible = HasHiddenSelection();
        _inspectorPanel.Children.Add(_hiddenBatchStatus);
        if (snapshot.Notice is string note)
        {
            _inspectorPanel.Children.Add(EditorVisuals.Secondary(note));
            if (snapshot.Revision is null)
            {
                var refresh = new Button { Content = "Refresh properties" };
                refresh.Click += (_, _) => _worker?.RefreshInspector();
                _inspectorPanel.Children.Add(refresh);
            }
        }
        foreach (var component in snapshot.Fields.GroupBy(field => field.Field.ComponentName))
        {
            _inspectorPanel.Children.Add(EditorVisuals.Heading(component.Key, 14));
            foreach (CommonInspectorField field in component)
            {
                InspectorField definition = field.Field;
                string display = field.State switch
                {
                    CommonValueState.Mixed => "Mixed values",
                    CommonValueState.Absent => "Absent",
                    CommonValueState.Unavailable => "Unavailable",
                    _ => definition.Text
                };
                var row = new Grid { ColumnDefinitions = new ColumnDefinitions("120,*,Auto"), Margin = new Thickness(0, 2) };
                row.Children.Add(EditorVisuals.Ellipsis(definition.Label));
                _inspectorPanel.Children.Add(row);
                if (!snapshot.Writable || !definition.Editable)
                {
                    var value = EditorVisuals.Ellipsis(display);
                    Grid.SetColumn(value, 1);
                    row.Children.Add(value);
                    continue;
                }

                var box = new TextBox
                {
                    Text = _batchDraft is { } saved && saved.Field.Field.Path == definition.Path ? saved.Text : definition.Text,
                    PlaceholderText = field.State == CommonValueState.Mixed ? display : ""
                };
                _batchInputs[definition.Path] = box;
                AutomationProperties.SetName(box, definition.Path);
                var set = new Button { Content = "Set" };
                AutomationProperties.SetName(set, "Set " + definition.Path);
                Grid.SetColumn(box, 1);
                Grid.SetColumn(set, 2);
                row.Children.Add(box);
                row.Children.Add(set);
                bool ReadValue(out PropertyValue value) => InspectorModel.TryParse(definition, box.Text ?? "", out value, out _);
                void UpdateSet() => set.IsEnabled = box.IsEnabled && !box.IsReadOnly && !HasHiddenSelection() && ReadValue(out _);
                _batchButtons.Add(UpdateSet);
                UpdateSet();
                box.PropertyChanged += (sender, change) =>
                {
                    if (change.Property == TextBox.IsReadOnlyProperty)
                    {
                        UpdateSet();
                        return;
                    }
                    if (change.Property != TextBox.TextProperty || box.IsReadOnly) return;
                    UpdateSet();
                    if (_resettingBatchInput || !ReferenceEquals(_batchView, snapshot)) return;
                    if (_batchDraft is { } previous && previous.Field.Field.Path != definition.Path)
                        DiscardBatchDraft(snapshot);
                    string text = box.Text ?? "";
                    _batchDraft = (snapshot, field, text);
                    if (text != definition.Text) _assetDrag?.Cancel();
                    ShowDocumentState(_documentState);
                };
                void Submit()
                {
                    if (!set.IsEnabled || box.IsReadOnly || HasHiddenSelection() ||
                        !ReferenceEquals(_batchView, snapshot) || !ReadValue(out PropertyValue value)) return;
                    var basis = _batchDraft is { } current && current.Field.Field.Path == definition.Path
                        ? (current.View, current.Field) : (snapshot, field);
                    if (_worker?.CommitBatch(basis.Item1, basis.Item2, value) == true)
                    {
                        _batchDraft = null;
                        ShowDocumentState(_documentState);
                        set.IsEnabled = false;
                        box.IsEnabled = false;
                    }
                    else _editLine.Text = "Batch editing is busy or the selection changed. Refresh and retry.";
                }
                set.Click += (_, _) => Submit();
                box.KeyDown += (_, e) =>
                {
                    if (box.IsReadOnly) return;
                    if (e.Key == Key.Enter) { Submit(); e.Handled = true; }
                    if (e.Key == Key.Escape)
                    {
                        DiscardBatchDraft(snapshot);
                        e.Handled = true;
                    }
                };
            }
        }
        if (focused.Key is string path && _batchInputs.TryGetValue(path, out TextBox? next))
        {
            next.Focus();
            next.CaretIndex = Math.Min(caret, next.Text?.Length ?? 0);
        }
    }

    private void OnInspectorChanged(InspectorSnapshot snapshot)
    {
        if (_selection.Nodes.Count > 1 || snapshot.Subject != _selection.Node) return;
        if (_referenceBusy) return; // the worker owns the captured target; refresh after settlement/cancel
        if (_leaveInProgress && _leaveInput is not null && !_leaveInputStale)
        {
            _inspectorDeferred = snapshot;
            return;
        }
        // An observed external rename cancels a name draft immediately. Other
        // field refreshes retain the existing deferral while a user is typing.
        if (_nameBox?.IsFocused == true && _inspectorShown is InspectorSnapshot shown &&
            (snapshot.Subject != shown.Subject || snapshot.Name != shown.Name))
        {
            BuildInspector(snapshot);
            return;
        }
        if (_inspectorShown is not null && snapshot.Subject == _inspectorShown.Subject && TopLevel.GetTopLevel(this)?.FocusManager?.GetFocusedElement() is TextBox focused &&
            !ReferenceEquals(focused, _invalidatedInput) && _inspectorPanel.GetVisualDescendants().Contains(focused))
        {
            _inspectorDeferred = snapshot;
            return;
        }
        BuildInspector(snapshot);
    }

    private void BuildInspector(InspectorSnapshot snapshot)
    {
        if (!_leaveInProgress) _sceneInput = null;
        var focused = TopLevel.GetTopLevel(this)?.FocusManager?.GetFocusedElement();
        var focusedAddress = focused is Button { Tag: "asset chooser", DataContext: SceneAddress address }
            ? address : (SceneAddress?)null;
        _inspectorShown = snapshot;
        _inspectorDeferred = null;
        _nameBox = null;
        _refreshReferenceLabels.Clear();
        _inspectorPanel.Children.Clear();
        _inspectorStructureButtons.Clear();
        _invalidatedInput = null;
        if (snapshot.Subject is not EntityGuid subject)
        {
            _inspectorPanel.Children.Add(new TextBlock { Text = "Select a node in the Hierarchy.", Opacity = 0.6 });
            return;
        }
        var title = EditorVisuals.Heading(string.IsNullOrEmpty(snapshot.Name) ? "Unnamed node" : snapshot.Name, 17);
        title.TextWrapping = TextWrapping.Wrap;
        _inspectorPanel.Children.Add(EditorVisuals.Stack(1, title, EditorVisuals.Secondary("Scene node")));
        _inspectorPanel.Children.Add(EditorVisuals.Disclosure("Node details", EditorVisuals.Details(subject.ToString())));
        var nameRow = new Grid { ColumnDefinitions = new ColumnDefinitions("90,*"), Margin = new Thickness(0, 2) };
        nameRow.Children.Add(new TextBlock { Text = "Name", VerticalAlignment = VerticalAlignment.Center });
        var nameBox = new TextBox { Text = snapshot.Name, PlaceholderText = "Unnamed", MinWidth = 80 };
        _nameBox = nameBox;
        TrackSceneInput(nameBox, "Name", null, null, () => snapshot.Name ?? "");
        AutomationProperties.SetName(nameBox, "Node name");
        Grid.SetColumn(nameBox, 1);
        nameRow.Children.Add(nameBox);
        _inspectorPanel.Children.Add(nameRow);
        nameBox.KeyDown += (_, e) =>
        {
            if (nameBox.IsReadOnly) return;
            if (e.Key == Key.Enter)
            {
                string name = nameBox.Text ?? "";
                if (name != snapshot.Name) _worker?.CommitName(subject, snapshot.Name, name);
                _sceneInput = null;
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
            if (MovingFocusToNavigation()) return;
            DeferInspectorBlur(nameBox, () =>
            {
                if (!nameBox.IsReadOnly) nameBox.Text = snapshot.Name;
                if (_inspectorDeferred is InspectorSnapshot waiting) BuildInspector(waiting);
            });
        };
        foreach (var (component, name) in snapshot.Components)
        {
            var header = new DockPanel();
            if (name != "Transform")
            {
                var remove = new Button { Content = "Remove" };
                AutomationProperties.SetName(remove, "Remove " + name + " component");
                TypeId id = component;
                AddInspectorStructureButton(remove);
                remove.Click += (_, _) =>
                {
                    if (!_hierarchyFilter.Active && SelectedReparentRow() is { } row && row.Node == subject)
                        _worker?.RemoveComponent(id, row);
                };
                AddDocked(header, remove, Dock.Right);
            }
            header.Children.Add(EditorVisuals.Heading(name));
            var body = new StackPanel { Spacing = 4 };
            var fields = snapshot.Fields.Where(f => f.Component == component).ToArray();
            ToggleButton? referenceDetails = fields.Any(InspectorModel.CanChooseAsset)
                ? new ToggleButton { Content = "Reference details" }
                : null;
            if (name == "Transform" && fields.Any(IsTransformVector))
                body.Children.Add(EditorVisuals.Columns("76,*,*,*", new TextBlock(),
                    AxisHeading("X"), AxisHeading("Y"), AxisHeading("Z")));
            foreach (InspectorField field in fields)
                body.Children.Add(FieldRow(field, referenceDetails));
            if (referenceDetails is not null) body.Children.Add(referenceDetails);
            _inspectorPanel.Children.Add(EditorVisuals.Group(header, body));
        }
        if (snapshot.Addable.Count > 0)
        {
            var choice = new ComboBox
            {
                ItemsSource = snapshot.Addable.Select(a => a.Name).ToList(), SelectedIndex = 0, MinWidth = 115,
            };
            AutomationProperties.SetName(choice, "Component type");
            var button = new Button { Content = "Add component" };
            AddInspectorStructureButton(button);
            button.Click += (_, _) =>
            {
                if (!_hierarchyFilter.Active && choice.SelectedIndex >= 0 && SelectedReparentRow() is { } row &&
                    row.Node == subject)
                    _worker?.AddComponent(snapshot.Addable[choice.SelectedIndex].Id, row);
            };
            _inspectorPanel.Children.Add(EditorVisuals.Actions(choice, button));
        }
        if (focusedAddress is SceneAddress previous) FocusReferenceLauncher(previous);
    }

    private void FocusReferenceLauncher(SceneAddress address)
    {
        if (_referenceBusy || _referenceSessionFailed || _selection.Node != address.Node ||
            _inspectorShown?.Subject != address.Node) return;
        _inspectorPanel.GetVisualDescendants().OfType<Button>()
            .FirstOrDefault(b => b.Tag as string == "asset chooser" &&
                b.DataContext is SceneAddress current && current == address)?.Focus();
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
        _assetDrag?.Cancel();
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
                worker.ConfirmAssetReference(target.Request, listRequest, guid, () =>
                { if (ReferenceEquals(worker, _worker) && launch == _referenceLaunch) FinishAssetReference(); });
            }, () =>
            {
                worker.CancelAssetReference(target.Request);
                if (ReferenceEquals(worker, _worker) && launch == _referenceLaunch)
                { FinishAssetReference(); }
            });
            _referenceWindow = chooser;
            chooser.PreviewInputsChanged += () => { if (ReferenceEquals(_referenceWindow, chooser)) SyncPreviewSelection(); };
            chooser.PreviewButton.Click += (_, _) => RequestPreview(withMaterial: true);
            chooser.Closed += (_, _) =>
            {
                if (!ReferenceEquals(_referenceWindow, chooser)) return;
                _referenceWindow = null;
                SyncPreviewSelection();
            };
            SyncPreviewSelection();
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
            var manager = TopLevel.GetTopLevel(this)?.FocusManager;
            var focusBeforeRefresh = manager?.GetFocusedElement();
            focus?.Focus(); // return to the launcher, so an obsolete focused TextBox cannot defer the fresh snapshot
            _inspectorDeferred = null;
            worker.RefreshInspector(() =>
            {
                if (!ReferenceEquals(worker, _worker) || launch != _referenceLaunch ||
                    focus?.DataContext is not SceneAddress address) return;
                var current = manager?.GetFocusedElement();
                if (current is null || ReferenceEquals(current, this) || ReferenceEquals(current, focus) ||
                    ReferenceEquals(current, focusBeforeRefresh))
                    FocusReferenceLauncher(address);
            });
        });
    }

    private static bool IsTransformVector(InspectorField field) =>
        field.Control == FieldControl.Float3 && field.Path is
            "Transform.localPosition" or "Transform.localEulerAnglesRadians" or "Transform.localScale";

    private static TextBlock AxisHeading(string axis)
    {
        var text = EditorVisuals.Secondary(axis);
        text.TextAlignment = TextAlignment.Right;
        text.Margin = new Thickness(2, 0, 6, 0);
        return text;
    }

    private Control FieldRow(InspectorField field, ToggleButton? referenceDetails)
    {
        EntityGuid? sourceSubject = _inspectorShown?.Subject;
        bool vector = IsTransformVector(field);
        string name = field.Path switch
        {
            "Transform.localPosition" => "Position",
            "Transform.localEulerAnglesRadians" => "Rotation (rad)",
            "Transform.localScale" => "Scale",
            "Renderable.meshAsset" => "Mesh",
            "Renderable.materialAsset" => "Material",
            _ => field.Label,
        };
        var label = EditorVisuals.Secondary(name);
        label.VerticalAlignment = VerticalAlignment.Center;
        var row = EditorVisuals.Columns(vector ? "76,*" : "76,*,Auto,Auto", label);
        row.Tag = field.Path;
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
            var box = new TextBox { Text = field.Text, IsReadOnly = !field.Editable, MinWidth = 0 };
            if (field.Optional && !field.Present) box.PlaceholderText = "(absent)";
            if (vector)
                AutomationProperties.SetName(box, name + ", X Y Z");
            string original = field.Text;
            if (field.Editable) TrackSceneInput(box, field.Path, field.Component, field.Field.Id, () => original);
            void ReplaceDraft() { box.Text = field.Text; original = field.Text; }
            if (InspectorModel.CanChooseAsset(field) && referenceDetails is not null)
            {
                var choose = new Button { Content = "Choose…", Tag = "asset chooser", IsEnabled = !_referenceBusy && !_referenceSessionFailed };
                AutomationProperties.SetName(choose, "Choose " + name);
                if (sourceSubject is EntityGuid subject)
                    choose.DataContext = new SceneAddress(subject, field.Component, field.Field.Id);
                // The ancestor tunnel runs before Button's class focus handler.
                row.AddHandler(InputElement.PointerPressedEvent, (_, e) =>
                {
                    if (e.Source is Visual source && (ReferenceEquals(source, choose) || source.GetVisualAncestors().Contains(choose)) &&
                        e.GetCurrentPoint(choose).Properties.IsLeftButtonPressed) ReplaceDraft();
                }, RoutingStrategies.Tunnel);
                choose.Click += (_, _) => BeginAssetReference(field, choose, sourceSubject);
                box.KeyDown += (_, e) =>
                { if (e.Key == Key.F4 && e.KeyModifiers == KeyModifiers.None) { e.Handled = true; ReplaceDraft(); BeginAssetReference(field, choose, sourceSubject); } };

                row.RowDefinitions = new RowDefinitions("Auto,Auto,Auto,Auto");
                var value = EditorVisuals.Ellipsis("");
                var sourcePath = EditorVisuals.Details("");
                var valueWell = new Border
                {
                    Child = value, Padding = new Thickness(6, 4), Background = EditorVisuals.Inset,
                    CornerRadius = new CornerRadius(4), Margin = new Thickness(0, 0, 4, 0),
                };
                Grid.SetColumn(valueWell, 1);
                row.Children.Add(valueWell);
                Grid.SetColumn(sourcePath, 1);
                Grid.SetColumnSpan(sourcePath, 3);
                Grid.SetRow(sourcePath, 2);
                row.Children.Add(sourcePath);
                Grid.SetColumn(choose, 2);
                row.Children.Add(choose);
                Grid.SetRow(box, 3);
                Grid.SetColumnSpan(box, 3);

                void ReferenceLabel()
                {
                    AssetSummary? asset = field.Value is PropertyValue v && v.TryGetAsset(out AssetGuid guid)
                        ? _assetBrowser.Items.FirstOrDefault(a => a.Guid == guid)
                        : null;
                    value.Text = asset is not null ? EditorVisuals.AssetName(asset) :
                        field.Present ? "Asset " + field.Text.Split('-')[0] : "(absent)";
                    ToolTip.SetTip(value, asset is not null ? asset.Source + "\n" + field.Text : field.Text);
                    sourcePath.Text = asset?.Source ?? "";
                    sourcePath.IsVisible = referenceDetails.IsChecked == true && asset is not null;
                }

                _refreshReferenceLabels.Add(ReferenceLabel);
                void DetailsChanged()
                {
                    box.IsVisible = referenceDetails.IsChecked == true;
                    ReferenceLabel();
                }

                referenceDetails.IsCheckedChanged += (_, _) => DetailsChanged();
                DetailsChanged();
                AutomationProperties.SetName(box, name + " GUID");
                row.SizeChanged += (_, e) =>
                {
                    bool compact = e.NewSize.Width < 285;
                    Grid.SetColumnSpan(valueWell, compact ? 3 : 1);
                    Grid.SetRow(choose, compact ? 1 : 0);
                    if (row.Children.OfType<Button>().FirstOrDefault(b => b.Tag as string == "clear field") is { } clear)
                        Grid.SetRow(clear, compact ? 1 : 0);
                };
            }
            void Commit()
            {
                if (box.IsReadOnly || !field.Editable || (box.Text ?? "") == original) return;
                original = box.Text ?? "";
                if (ReferenceEquals(_sceneInput?.Box, box)) _sceneInput = null;
                if (_worker is { } worker)
                {
                    ++_pendingSceneFieldEdits;
                    long lifetime = worker.MeshDocumentLifetime;
                    worker.CommitField(field.Path, original, () =>
                    {
                        if (ReferenceEquals(_worker, worker) && lifetime == worker.MeshDocumentLifetime && _pendingSceneFieldEdits > 0)
                            --_pendingSceneFieldEdits;
                    });
                }
                ShowDocumentState(_documentState);
            }
            box.KeyDown += (_, e) =>
            {
                if (box.IsReadOnly) return;
                if (e.Key == Key.Enter) Commit();
                if (e.Key == Key.Escape) box.Text = original;
            };
            box.LostFocus += (_, _) =>
            {
                if (MovingFocusToNavigation()) return;
                DeferInspectorBlur(box, () =>
                {
                    Commit();
                    if (_inspectorDeferred is InspectorSnapshot waiting) BuildInspector(waiting);
                });
            };
            if (vector)
            {
                var axes = new[] { AxisHeading(""), AxisHeading(""), AxisHeading("") };
                var summary = new Border
                {
                    Background = EditorVisuals.Inset, CornerRadius = new CornerRadius(4),
                    Child = EditorVisuals.Columns("*,*,*", axes), IsHitTestVisible = false,
                    Padding = new Thickness(0, 4), VerticalAlignment = VerticalAlignment.Center,
                };
                void ShowVector()
                {
                    string text = InspectorModel.TryParse(field, box.Text ?? "", out PropertyValue value, out _)
                        ? InspectorModel.Format(field, value) : field.Text;
                    string[] values = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    for (int axis = 0; axis < axes.Length; ++axis)
                        axes[axis].Text = axis < values.Length ? values[axis] : "";
                    summary.IsVisible = !box.IsFocused;
                    box.Opacity = box.IsFocused ? 1 : 0;
                }

                box.GotFocus += (_, _) => ShowVector();
                box.LostFocus += (_, _) => ShowVector();
                ShowVector();
                var layer = new Grid { Children = { box, summary } };
                editor = layer;
            }
            else editor = box;
        }
        if (!vector && AutomationProperties.GetName(editor) is null)
            AutomationProperties.SetName(editor, name);
        Grid.SetColumn(editor, 1);
        row.Children.Add(editor);
        if (field.Optional && field.Present && field.Editable)
        {
            var clear = new Button { Content = "Clear", Tag = "clear field" };
            AutomationProperties.SetName(clear, "Clear " + name);
            clear.Click += (_, _) => _worker?.ClearField(field.Path);
            Grid.SetColumn(clear, InspectorModel.CanChooseAsset(field) ? 3 : 2);
            row.Children.Add(clear);
        }
        var drop = _inspectorShown?.AssetDropFields.FirstOrDefault(target =>
            target.Field.Address.Component == field.Component && target.Field.Address.Field == field.Field.Id);
        if (drop is not null)
        {
            var outline = new Border { BorderBrush = EditorVisuals.Line, BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(4), IsHitTestVisible = false };
            outline.ZIndex = 1;
            Grid.SetColumnSpan(outline, row.ColumnDefinitions.Count);
            Grid.SetRowSpan(outline, row.RowDefinitions.Count);
            row.Children.Add(outline);
            row.Background = Brushes.Transparent;
            ConfigureAssetDropTarget(row, outline, drop);
        }
        return row;
    }

    private Control BuildHierarchyFilters()
    {
        AutomationProperties.SetName(_hierarchySearch, "Search by name");
        AutomationProperties.SetName(_hierarchyComponent, "Filter by component");
        _hierarchyComponent.ItemTemplate = new FuncDataTemplate<HierarchyComponent>((item, _) =>
        {
            var label = EditorVisuals.Ellipsis(item?.Label ?? "");
            ToolTip.SetTip(label, item?.Label);
            return label;
        });
        _hierarchyComponent.ItemsSource = new[] { AllHierarchyComponents };
        _hierarchyComponent.SelectedIndex = 0;
        _hierarchySearch.PropertyChanged += (_, change) =>
        {
            if (change.Property == TextBox.TextProperty) ApplyHierarchyFilters();
        };
        _hierarchyComponent.SelectionChanged += (_, _) => ApplyHierarchyFilters();
        _clearHierarchyFilter.Click += (_, _) =>
        {
            ClearHierarchyFilters();
            _hierarchySearch.Focus();
        };
        _locateHierarchySelection.Click += (_, _) => LocateHierarchySelection();
        _hierarchyFilterStatus.TextWrapping = TextWrapping.Wrap;
        _hiddenBatchStatus.TextWrapping = TextWrapping.Wrap;
        foreach (Control control in new Control[] { _gizmoMode, _gizmoSnap, _destroy, _pickUp, _moveHere, _toRoot, _moveUp, _moveDown })
            ToolTip.SetShowOnDisabled(control, true);
        var filterBar = EditorVisuals.Stack(4, _hierarchySearch, _hierarchyComponent,
            EditorVisuals.Actions(_clearHierarchyFilter, _locateHierarchySelection), _hierarchyFilterStatus);
        filterBar.Margin = new Thickness(4);
        UpdateHierarchyFilterStatus();
        return filterBar;
    }

    private bool HasHiddenSelection() =>
        _hierarchyFilter.Active && _selection.Nodes.Any(node => !_hierarchyProjection.Visible(node));

    private void UpdateHierarchyFilterStatus()
    {
        int total = _selection.Nodes.Count;
        int hidden = _hierarchyFilter.Active ? _selection.Nodes.Count(node => !_hierarchyProjection.Visible(node)) : 0;
        _hierarchyFilterStatus.Text = (total == 0 ? "No selection" : $"{total} selected") + (hidden > 0 ? $" · {hidden} hidden" : "");
        if (_hierarchyFilter.Active)
        {
            string matches = _hierarchyProjection.MatchCount switch
            {
                0 => "No matching nodes",
                1 => "1 match",
                _ => $"{_hierarchyProjection.MatchCount} matches"
            };
            _hierarchyFilterStatus.Text = matches + " · " + _hierarchyFilterStatus.Text;
        }
        _clearHierarchyFilter.IsEnabled = _hierarchyFilter.Active || !string.IsNullOrEmpty(_hierarchySearch.Text);
        _locateHierarchySelection.IsEnabled = !_referenceSessionFailed &&
            _hierarchyRows.Any(row => _selection.Nodes.Contains(row.Node));
    }

    private void AddInspectorStructureButton(Button button)
    {
        _inspectorStructureButtons.Add(button);
        button.IsEnabled = !_hierarchyFilter.Active;
        ToolTip.SetShowOnDisabled(button, true);
        ToolTip.SetTip(button, _hierarchyFilter.Active ? FilteredStructureReason : null);
    }

    private ScrollViewer? HierarchyScroll() => _entities.GetVisualDescendants().OfType<ScrollViewer>().FirstOrDefault();

    private HierarchyScrollAnchor? CurrentHierarchyAnchor()
    {
        if (HierarchyScroll() is not { } scroll) return null;
        var top = _entities.GetVisualDescendants().OfType<ListBoxItem>()
            .Where(item => item.DataContext is HierarchyRow)
            .Select(item => (Row: (HierarchyRow)item.DataContext!, Y: item.TranslatePoint(default, scroll)?.Y, item.Bounds.Height))
            .Where(item => item.Y is double y && y + item.Height > 0 && y < scroll.Viewport.Height)
            .OrderBy(item => item.Y).FirstOrDefault().Row;
        return top is null ? null : new HierarchyScrollAnchor(_hierarchyProjection.Rows, top);
    }

    private void ProjectHierarchy(HierarchyScrollAnchor? anchor = null, HierarchyRow? reveal = null)
    {
        long intent = ++_hierarchyScrollIntent;
        _hierarchyProjection = _hierarchyFilter.Project(_hierarchyRows);
        _syncingSelection = true;
        try
        {
            _entities.ItemsSource = _hierarchyProjection.Rows;
            SyncHierarchySelection(scroll: false);
        }
        finally { _syncingSelection = false; }
        if (HasHiddenSelection() && _groupGrab is not null) CancelGroupGesture();
        UpdateHierarchyActions();
        HierarchyRow? target = reveal ?? anchor?.Restore(_hierarchyProjection.Rows);
        if (target is not null) ScheduleHierarchyScroll(target, intent, reveal is not null);
    }

    private void ScheduleHierarchyScroll(HierarchyRow target, long intent, bool focus)
    {
        // Layout can realize a different set of rows after changing ItemsSource.
        Dispatcher.UIThread.Post(() =>
        {
            if (intent != _hierarchyScrollIntent || !_hierarchyProjection.Rows.Any(row => ReferenceEquals(row, target))) return;
            if (focus && TopLevel.GetTopLevel(this)?.FocusManager?.GetFocusedElement() is Control current &&
                !ReferenceEquals(current, _locateHierarchySelection) && !ReferenceEquals(current, _entities) &&
                HierarchyItem(current) is null) return;
            _entities.ScrollIntoView(target);
            _entities.UpdateLayout();
            if (_entities.ContainerFromItem(target) is not ListBoxItem item) return;
            if (focus) item.Focus();
            else if (HierarchyScroll() is { } scroll && item.TranslatePoint(default, scroll) is Point point)
                scroll.Offset = new Avalonia.Vector(scroll.Offset.X, scroll.Offset.Y + point.Y);
        }, DispatcherPriority.Loaded);
    }

    private void ApplyHierarchyFilters(HierarchyRow? reveal = null)
    {
        if (_updatingHierarchyControls) return;
        var filter = new HierarchyFilter(_hierarchySearch.Text, (_hierarchyComponent.SelectedItem as HierarchyComponent)?.Id);
        if (_hierarchyFilter.EquivalentTo(filter))
        {
            UpdateHierarchyFilterStatus();
            if (reveal is not null) ScheduleHierarchyScroll(reveal, ++_hierarchyScrollIntent, focus: true);
            return;
        }
        HierarchyScrollAnchor? anchor = CurrentHierarchyAnchor();
        if (!_hierarchyFilter.Active && filter.Active) _hierarchyBeforeFilter = anchor;
        if (_hierarchyFilter.Active && !filter.Active)
        {
            anchor = _hierarchyBeforeFilter;
            _hierarchyBeforeFilter = null;
        }
        _hierarchyFilter = filter;
        _worker?.SetHierarchyFilter(filter);
        if ((_pickedUp ?? _hierarchyDrag?.Source) is { } source) _worker?.CancelReparent(source);
        _pickedUp = null;
        _hierarchyPress = null;
        _hierarchyDrag = null;
        _hierarchyDropPointer = null;
        ClearHierarchyDropFeedback();
        if (!_reparentPending && !_siblingMovePending) ShowReparentState("");
        ProjectHierarchy(anchor, reveal);
    }

    private void ClearHierarchyFilters(HierarchyRow? reveal = null)
    {
        _updatingHierarchyControls = true;
        try
        {
            _hierarchySearch.Text = "";
            _hierarchyComponent.SelectedIndex = 0;
        }
        finally { _updatingHierarchyControls = false; }
        ApplyHierarchyFilters(reveal);
    }

    private void LocateHierarchySelection()
    {
        if (!_locateHierarchySelection.IsEnabled || _hierarchyProjection.Locate(_hierarchyRows, _selection.Nodes) is not { } row) return;
        _locateHierarchySelection.Focus();
        if (HasHiddenSelection()) ClearHierarchyFilters(row);
        else ScheduleHierarchyScroll(row, ++_hierarchyScrollIntent, focus: true);
    }

    private void ResetHierarchyView()
    {
        _hierarchyBeforeFilter = null;
        _hierarchyViewGeneration = null;
        _hierarchyRows = Array.Empty<HierarchyRow>();
        _updatingHierarchyControls = true;
        try { _hierarchyComponent.ItemsSource = new[] { AllHierarchyComponents }; }
        finally { _updatingHierarchyControls = false; }
        ClearHierarchyFilters();
        ProjectHierarchy();
    }

    private void OnHierarchyChanged(HierarchySnapshot snapshot)
    {
        if (_hierarchyViewGeneration is ulong generation && snapshot.Generation < generation) return;
        _markers = snapshot.Markers;
        if (_hierarchyViewGeneration == snapshot.Generation) return;
        ClearHierarchyDropFeedback();
        HierarchyScrollAnchor? anchor = CurrentHierarchyAnchor();
        _hierarchyViewGeneration = snapshot.Generation;
        _hierarchyRows = snapshot.Rows;
        var options = snapshot.Components.Prepend(AllHierarchyComponents).ToArray();
        if (_hierarchyComponent.ItemsSource?.OfType<HierarchyComponent>().SequenceEqual(options) != true)
        {
            _updatingHierarchyControls = true;
            try
            {
                _hierarchyComponent.ItemsSource = options;
                _hierarchyComponent.SelectedItem = options.FirstOrDefault(option => option.Id == _hierarchyFilter.Component) ?? options[0];
            }
            finally { _updatingHierarchyControls = false; }
            ApplyHierarchyFilters();
        }
        ProjectHierarchy(anchor);
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

    private void ShowRecovery(RecoveryObservation view)
    {
        _recoveryView = view;
        if (_recoveryUnknown) return;
        var status = view.Status;
        var pending = status?.Copy is { Pending: true };
        _reviewRecovery.Content = "Review Recovery Copy";
        _reviewRecovery.IsVisible = pending;
        _recoveryLine.Text = pending ? "An earlier recovery copy awaits a decision. New edits cannot replace it."
            : status?.Capture switch
            {
                Doc.RecoveryCapture.Writing => status.ProtectedRevision is ulong saved
                    ? $"Writing recovery copy · revision {saved} protected" : "Writing recovery copy",
                Doc.RecoveryCapture.Failed => status.ProtectedRevision is ulong durable
                    ? $"Recovery write failed · revision {durable} protected" : "Recovery copy unavailable: storage or file error.",
                Doc.RecoveryCapture.Unavailable => "Recovery storage unavailable on this Runtime.",
                _ => status?.ProtectedRevision is ulong revision ? $"Recovery copy protects revision {revision}"
                    : view.Refusal == Doc.RecoveryError.Unsupported ? "Recovery copies are unsupported on this Runtime." : "",
            };
        if (pending) Dispatcher.UIThread.Post(async () => await ReviewRecovery(automatic: true));
    }

    private async Task ReviewRecovery(bool automatic = false)
    {
        if (_leaveInProgress || _recoveryDecision || _shuttingDown || _savedAttempts.Busy || _referenceBusy ||
            _materialWindow is not null || _worker is not { LeaveEditingAvailable: true } worker ||
            _recoveryView is not { Document: { } document, Status.Copy: { Pending: true } copy } ||
            document.Token != _documentState.State?.Token) return;
        if (CurrentSceneInput() is not null || _batchDraft is not null || _gizmoGrab is not null || _groupGrab is not null)
        {
            if (!automatic) _recoveryLine.Text = "Finish or cancel the active scene edit before reviewing recovery.";
            return;
        }
        if (automatic && _offeredRecovery == (document.Token.DocumentId, copy.Id)) return;
        _offeredRecovery = (document.Token.DocumentId, copy.Id);
        _recoveryDecision = true;
        _pendingInspectorBlur = null;
        _assetDrag?.Cancel();
        var origin = FocusManager?.GetFocusedElement() as Control;
        var content = Content as Control;
        var lifetime = worker.MeshDocumentLifetime;
        bool Current() => ReferenceEquals(worker, _worker) && !_shuttingDown;
        bool SameDocument() => Current() && lifetime == worker.MeshDocumentLifetime &&
            _documentState.State?.Token == document.Token;
        try
        {
            var dialog = new SceneRecoveryWindow(document, copy, () => SameDocument()
                ? worker.PreviewRecovery(document.Token, copy.Id)
                : Task.FromResult(new RecoveryObservation(Refusal: Doc.RecoveryError.StaleDocument)));
            _recoveryWindow = dialog;
            var choice = await dialog.ShowDialog<RecoveryChoice>(this);
            if (choice == RecoveryChoice.Later || !Current()) return;
            if (!SameDocument())
            {
                _recoveryLine.Text = "The document changed. Review the current recovery copy before choosing again.";
                return;
            }
            _recoveryLine.Text = "Settling the recovery decision…";
            _reviewRecovery.IsEnabled = false;
            if (content is not null) content.IsEnabled = false;
            var result = await worker.ResolveRecovery(document, copy.Id, choice == RecoveryChoice.Restore);
            if (!Current()) return;
            if (result.Receipt is not null)
            {
                _recoveryLine.Text = choice == RecoveryChoice.Restore
                    ? "Recovery restored. Use Apply or Save explicitly."
                    : "Recovery copy discarded. Reopen from Source if the official file changed after this document opened.";
                _reviewRecovery.IsVisible = false;
                _editLine.Text = _recoveryLine.Text;
            }
            else
            {
                _recoveryUnknown = result.Unknown;
                _reviewRecovery.IsVisible = true;
                _reviewRecovery.Content = result.Unknown ? "Check Original Recovery" : "Review Recovery Copy";
                _recoveryLine.Text = result.Diagnostic ?? $"Recovery refused ({result.Refusal}). Review the current document and copy.";
            }
        }
        finally
        {
            _recoveryWindow = null;
            _recoveryDecision = false;
            _reviewRecovery.IsEnabled = true;
            if (content is not null) content.IsEnabled = true;
            if (Current() && origin is { IsEffectivelyEnabled: true } && TopLevel.GetTopLevel(origin) == this) origin.Focus();
        }
    }

    private void ShowDocumentState(DocumentObservation state)
    {
        _documentState = state;
        bool draft = _sceneInput is not null || _batchDraft is not null || _leaveInput is not null && !_leaveInputStale;
        _documentLine.Text = state.Title + (draft ? " · Unsubmitted input" : "") +
            (_draftProtection.Stale is not null ? " · Stale input" : "");
        ToolTip.SetTip(_documentLine, state.State?.Source ?? state.Diagnostic);
        _reopen.Content = state.State is null && state.Diagnostic is null && state.Availability == DocumentAvailability.Known
            ? "Open Source" : "Reopen from Source";
        _reopen.IsEnabled = !_leaveInProgress && state.Availability == DocumentAvailability.Known &&
            state.Diagnostic is null && _worker?.LeaveEditingAvailable == true;
        ToolTip.SetTip(_reopen, state.Availability == DocumentAvailability.Legacy ? state.Diagnostic : "Ctrl+Shift+R");
    }

    private SceneInput CaptureInput(TextBox box, string key, TypeId? component, FieldId? field, string text) =>
        new(box, new DocumentDraft(key, text, _selection.Nodes.ToArray(), component, field), box.CaretIndex,
            box.SelectionStart, box.SelectionEnd);

    private SceneInput? CurrentSceneInput()
    {
        if (_batchDraft is { } batch && batch.Text != batch.Field.Field.Text &&
            _batchInputs.TryGetValue(batch.Field.Field.Path, out var box))
        {
            var field = batch.Field.Field;
            return CaptureInput(box, field.Path, field.Component, field.Field.Id, batch.Text);
        }
        return _sceneInput;
    }

    private void TrackSceneInput(TextBox box, string key, TypeId? component, FieldId? field, Func<string> baseline)
    {
        box.PropertyChanged += (_, change) =>
        {
            if (change.Property != TextBox.TextProperty || box.IsReadOnly || _leaveInProgress ||
                (!box.IsFocused && _sceneInput?.Box != box)) return;
            string text = box.Text ?? "";
            _sceneInput = text == baseline() ? null : CaptureInput(box, key, component, field, text);
            if (_sceneInput is not null) _assetDrag?.Cancel();
            ShowDocumentState(_documentState);
        };
    }

    private void DeferInspectorBlur(TextBox box, Action settle)
    {
        if (_leaveArmed || _leaveInProgress || _recoveryDecision || _shuttingDown) return;
        void SettleIfCurrent()
        {
            if (_leaveArmed || _leaveInProgress || _recoveryDecision || _shuttingDown || !IsActive ||
                (_reopen.IsFocused || _reviewRecovery.IsFocused) && _sceneInput?.Box == box) return;
            if (TopLevel.GetTopLevel(box) != this) return;
            if (ReferenceEquals(_sceneInput?.Box, box)) _sceneInput = null;
            settle();
            ShowDocumentState(_documentState);
        }
        // An ordinary focus destination settles this before its action. An
        // ambiguous native blur waits for the Closing route to claim the draft.
        Action pending = SettleIfCurrent;
        _pendingInspectorBlur = pending;
        Dispatcher.UIThread.Post(() =>
        {
            if (ReferenceEquals(_pendingInspectorBlur, pending)) SettleInspectorBlur();
        }, DispatcherPriority.Background);
    }

    private void SettleInspectorBlur()
    {
        var pending = _pendingInspectorBlur;
        _pendingInspectorBlur = null;
        pending?.Invoke();
    }

    private void ObserveDraftChanges(IReadOnlyList<Doc.Event> events)
    {
        var input = _leaveInProgress ? (_leaveInputStale ? null : _leaveInput) : CurrentSceneInput();
        if (input is null || !_draftProtection.Observe(input.Draft, events)) return;
        _invalidatedInput = input.Box;
        input.Box.IsReadOnly = true;
        _pendingInspectorBlur = null;
        _sceneInput = null;
        _batchDraft = null;
        if (_leaveInput is not null) _leaveInputStale = true;
        _leaveDialog?.ShowDraft(LeaveDraftSummary());
        ShowDocumentState(_documentState);
    }

    private string? LeaveDraftSummary()
    {
        string? stale = _draftProtection.Stale is { } copy ? $"Stale input (copy only) · {copy.Key}\n{copy.Text}" : null;
        if (_leaveInputStale || _leaveInput is not { } active) return stale;
        string current = $"Unsubmitted input · {active.Draft.Key}\n{active.Draft.Text}";
        return stale is null ? current : stale + "\n\n" + current;
    }

    private async Task AskToLeave(bool reopen)
    {
        if (_leaveInProgress || _recoveryDecision || _shuttingDown || _savedAttempts.Busy) return;
        _leaveArmed = false;
        _assetDrag?.Cancel();
        _leaveInProgress = true;
        _pendingInspectorBlur = null;
        var worker = _worker;
        var originalFocus = FocusManager?.GetFocusedElement();
        _leaveInput = CurrentSceneInput();
        if (_leaveInput is { } input)
            _leaveInput = input with { Caret = input.Box.CaretIndex, Start = input.Box.SelectionStart, End = input.Box.SelectionEnd };
        _leaveInputStale = false;
        if (_groupGrab is not null) CancelGroupGesture();
        if (_gizmoGrab is not null)
        {
            worker?.CancelGizmoForLeave();
            _gizmoGrab = null;
        }
        _pressed = null;
        var content = Content as Control;
        if (content is not null) content.IsEnabled = false;
        ShowDocumentState(_documentState);
        DocumentLeaveWindow? dialog = null;
        Task? dialogExit = null;
        bool closed = false, reopened = false, mustConfirm = false;
        bool unknown = false, checkedUnknown = false;
        Doc.DocumentState? attempted = null;
        bool attemptedSave = false;
        string? notice = null;

        bool Current() => !_shuttingDown && ReferenceEquals(_worker, worker);
        async Task<DocumentObservation> Read()
        {
            if (worker is null) return _documentState;
            return worker.LeaveEditingAvailable ? await worker.ReviewDocument() : await worker.CheckOriginalDocument();
        }

        try
        {
            var reviewed = await Read();
            while (Current())
            {
                if (reopen && reviewed.Availability == DocumentAvailability.Legacy) break;
                bool prompt = mustConfirm || _draftProtection.RequiresConfirmation(reviewed, _leaveInput is not null && !_leaveInputStale);
                var choice = LeaveChoice.Discard;
                if (prompt)
                {
                    if (dialog is null)
                    {
                        dialog = new DocumentLeaveWindow(reopen);
                        _leaveDialog = dialog;
                        dialogExit = dialog.ShowDialog(this);
                    }
                    bool unavailable = reviewed.Availability != DocumentAvailability.Known;
                    bool live = worker?.LeaveEditingAvailable == true;
                    bool canWrite = live && (!unknown || checkedUnknown);
                    bool canContinue = !unavailable && (reopen ? reviewed.State is not null && canWrite : !unknown || checkedUnknown);
                    bool anyway = !reopen && (unavailable || unknown && !live && !checkedUnknown);
                    string consequence = reopen ? "Reopen replaces the current document and clears its undo history."
                        : _options.AttachSession is null ? "Closing stops this Editor’s Runtime."
                        : "Closing leaves the attached Runtime and shared document running.";
                    string summary = (reviewed.State?.Source is string source ? source + "\n" : "") + reviewed.Title + "\n" + consequence;
                    choice = await dialog.Ask(summary, notice ?? reviewed.Diagnostic, LeaveDraftSummary(),
                        canWrite && reviewed.Writable && reviewed.State?.Dirty == true, canContinue,
                        worker is not null, reopen, anyway);
                    if (!Current() || choice == LeaveChoice.Cancel) break;
                    if (choice == LeaveChoice.Check)
                    {
                        reviewed = await Read();
                        checkedUnknown = reviewed.Availability == DocumentAvailability.Known;
                        if (unknown && attempted is { } target && checkedUnknown)
                        {
                            notice = attemptedSave && reviewed.ProvesSaved(target.Token)
                                ? "The reviewed content is saved. Confirm again to continue."
                                : !attemptedSave && reviewed.State is { } opened && opened.Token.DocumentId != target.Token.DocumentId
                                    ? "The document was replaced. Confirm again against the current document."
                                    : "The previous operation remains unconfirmed. Review the current state before continuing.";
                        }
                        else notice = reviewed.Diagnostic;
                        continue;
                    }
                }

                var latest = await Read();
                if (!Current()) break;
                if (!latest.Matches(reviewed))
                {
                    reviewed = latest;
                    mustConfirm = true;
                    checkedUnknown = false;
                    notice = "The document changed. Review the current state and choose again.";
                    continue;
                }
                if (choice == LeaveChoice.CloseAnyway)
                {
                    closed = true;
                    break;
                }

                if (choice == LeaveChoice.Save)
                {
                    if (worker is null || latest.State is not { } target) break;
                    var save = await worker.SaveOrReopen(target, saving: true);
                    if (!Current()) break;
                    if (save.State is null)
                    {
                        (unknown, checkedUnknown, attempted, attemptedSave) = (save.Unknown, false, target, true);
                        notice = save.Diagnostic ?? "Save refused: " + save.Refusal;
                        reviewed = await Read();
                        mustConfirm = true;
                        continue;
                    }
                    reviewed = new(DocumentAvailability.Known, save.State);
                    if (!reopen)
                    {
                        latest = await Read();
                        if (!Current()) break;
                        if (!latest.Matches(reviewed))
                        {
                            reviewed = latest;
                            notice = "Saved the reviewed changes. The document changed before closing.";
                            mustConfirm = true;
                            continue;
                        }
                    }
                }

                if (!reopen)
                {
                    closed = true;
                    break;
                }
                if (worker is null || reviewed.Availability != DocumentAvailability.Known || reviewed.Diagnostic is not null) break;
                var reopenTarget = reviewed.State;
                var open = await worker.SaveOrReopen(reopenTarget, saving: false);
                if (!Current()) break;
                if (open.State is not null)
                {
                    reopened = true;
                    SetSelection(null, false);
                    _editLine.Text = "Reopened " + open.State.Source;
                    break;
                }
                (unknown, checkedUnknown, attempted, attemptedSave) = (open.Unknown, false, reopenTarget, false);
                notice = (choice == LeaveChoice.Save ? "Saved the reviewed changes. " : "") +
                    (open.Diagnostic ?? "Reopen refused: " + open.Refusal);
                reviewed = await Read();
                mustConfirm = true;
            }
        }
        finally
        {
            _leaveDialog = null;
            dialog?.Finish();
            if (!closed && !reopened && dialogExit is not null) await dialogExit;
            if (content is not null) content.IsEnabled = true;
            var captured = _leaveInput;
            var batchDeferred = _leaveBatchDeferred;
            _leaveBatchDeferred = null;
            _leaveInput = null;
            if (!closed && !reopened && !_leaveInputStale && captured is { } valid &&
                TopLevel.GetTopLevel(valid.Box) == this && valid.Box.IsEnabled)
            {
                Activate();
                valid.Box.Text = valid.Draft.Text;
                valid.Box.Focus();
                valid.Box.CaretIndex = valid.Caret;
                valid.Box.SelectionStart = valid.Start;
                valid.Box.SelectionEnd = valid.End;
            }
            else if (!closed && !reopened && !_leaveInputStale && originalFocus is Control control &&
                control.IsEnabled && TopLevel.GetTopLevel(control) == this)
                control.Focus();
            else if (!closed && !reopened) _save.Focus();
            if (closed || reopened)
            {
                _draftProtection.ConfirmLeave();
                _sceneInput = null;
                _batchDraft = null;
            }
            if (!closed && !reopened && (_leaveInputStale || captured is null) && batchDeferred is not null)
                OnBatchInspectorChanged(batchDeferred);
            _leaveInProgress = false;
            if (!closed)
            {
                ShowDocumentState(_documentState);
                if (reopened) Dispatcher.UIThread.Post(async () => await ReviewRecovery(automatic: true));
            }
        }
        if (!closed || !Current()) return;
        _leaveCloseApproved = true;
        Close();
    }

    private void UpdateStatus()
    {
        string size = _viewport.Window is { } w ? "viewport " + w.Width + "x" + w.Height + " px   " : "";
        _connectionLine.Text = _runtimeLine;
        _status.Text = string.Join("\n", new[] { _runtimeLine, _pickLine + "   " + _cameraLine, size + _inputLine + "   " + _uiLine, _measureLine }
                                         .Where(line => !string.IsNullOrWhiteSpace(line)));
    }

    private void Shutdown()
    {
        _assetBrowser.CancelPendingDependents();
        EndPreviewSession();
        _uiTicker.Stop();
        _texturePoll.Stop();
        _materialPoll.Stop(); _materialWindow?.Close(); _materialWindow = null;
        _importPoll.Stop();
        _rebuildPoll.Stop();
        ++_referenceLaunch; _referenceWindow?.Close(); _referenceWindow = null;
        if (!_driving) WriteDragReport();  // a manual run's report (a driven run writes its own)
        ResetReparent(null);
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
