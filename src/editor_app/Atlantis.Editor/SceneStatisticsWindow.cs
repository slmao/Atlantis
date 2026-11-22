using System;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;

namespace Atlantis.Editor;

internal sealed record SceneStatisticsFrame(AssetGuid Scene, ulong Generation, ulong Frame, ulong DrawItems);

public sealed partial class MainWindow
{
    private readonly Button _sceneStatisticsButton = new() { Content = "Scene statistics", Focusable = false };
    private SceneStatisticsWindow? _sceneStatistics;
    private SceneStatisticsIntent? _sceneStatisticsIntent;
    private SceneStatisticsReport? _sceneStatisticsReport;
    private EditorWorker? _sceneStatisticsWorker, _sceneStatisticsFrameWorker;
    private SceneStatisticsFrame? _sceneStatisticsFrame;

    private void OpenSceneStatistics()
    {
        if (BistroLocatorProtected())
        {
            ShowNavigationMessage("Finish the current draft or gesture before opening Scene Statistics.");
            return;
        }
        if (_sceneStatistics is { } present) { present.Activate(); return; }
        var view = new SceneStatisticsWindow();
        _sceneStatistics = view;
        view.RefreshRequested += RefreshSceneStatistics;
        view.ScopeChanged += () => InvalidateSceneStatistics("Scope changed. Refresh to read it.");
        view.LocateRequested += missing => LocateSceneStatisticsBinding(view, missing);
        view.Closed += (_, _) =>
        {
            _sceneStatisticsIntent?.Cancel();
            _sceneStatisticsIntent = null;
            _sceneStatisticsReport = null;
            _sceneStatisticsWorker = null;
            _sceneStatistics = null;
        };
        view.Show(this);
        view.Display(null, SceneStatisticsFrameForWorker(), SceneStatisticsCatalogLabel());
    }

    private string SceneStatisticsCatalogLabel() => _activeAssetGeneration is { } named
        ? $"Running Runtime catalog · generation {named}"
        : _runtime?.Launch.AssetGeneration is { } launched ? $"Running Runtime catalog · generation {launched}"
        : "Running Runtime catalog";

    private SceneStatisticsFrame? SceneStatisticsFrameForWorker() =>
        _worker is not null && ReferenceEquals(_worker, _sceneStatisticsFrameWorker) ? _sceneStatisticsFrame : null;

    private void RememberSceneStatisticsFrame(EditorWorker worker, BistroCapture capture)
    {
        _sceneStatisticsFrameWorker = worker;
        _sceneStatisticsFrame = new(capture.Status.Scene, capture.Status.SceneGeneration,
            capture.Report.Frame, capture.Report.Data.DrawItemCount);
        _sceneStatistics?.SetFrame(_sceneStatisticsReport, SceneStatisticsFrameForWorker(), false);
    }

    private void InvalidateSceneStatistics(string reason)
    {
        _sceneStatisticsIntent?.Cancel();
        _sceneStatistics?.Invalidate(reason, _sceneStatisticsReport, SceneStatisticsFrameForWorker());
    }

    private void ObserveSceneStatistics()
    {
        if (_sceneStatistics is not { } view || _sceneStatisticsIntent is not { Current: true } intent) return;
        if (!ReferenceEquals(_worker, _sceneStatisticsWorker) || _referenceSessionFailed ||
            intent.Lifetime != _worker?.MeshDocumentLifetime ||
            (view.SelectedSubtree && (_selection.Node != intent.Root ||
              !_hierarchyRows.Any(row => row.Node == intent.Root && ReferenceEquals(row.Incarnation, intent.RootIdentity)))) ||
            (_sceneStatisticsReport?.Document is { } token && _documentState.State?.Token != token))
            InvalidateSceneStatistics("The scene or selection changed. Refresh to inspect it.");
    }

    private void RefreshSceneStatistics()
    {
        if (_sceneStatistics is not { } view) return;
        _sceneStatisticsIntent?.Cancel();
        _sceneStatisticsReport = null;
        _sceneStatisticsWorker = _worker;
        view.Display(null, SceneStatisticsFrameForWorker(), SceneStatisticsCatalogLabel());
        if (_worker is not { } worker || _referenceSessionFailed)
        {
            view.Invalidate("Runtime connection unavailable.", null, SceneStatisticsFrameForWorker());
            return;
        }
        HierarchyRow? root = view.SelectedSubtree && _selection.Node is { } selected
            ? _hierarchyRows.FirstOrDefault(row => row.Node == selected) : null;
        if (view.SelectedSubtree && root is null)
        {
            view.Invalidate("Select one current Hierarchy node to inspect its subtree.", null, SceneStatisticsFrameForWorker());
            return;
        }
        var intent = new SceneStatisticsIntent(root?.Node, root?.Incarnation, worker.MeshDocumentLifetime);
        _sceneStatisticsIntent = intent;
        view.BeginRead(() =>
        {
            if (ReferenceEquals(_sceneStatisticsIntent, intent) && intent.Current)
                view.Invalidate("Read budget expired. Refresh to try again.", null, SceneStatisticsFrameForWorker());
        });
        worker.ReadSceneStatistics(intent, (report, stale) =>
        {
            if (!ReferenceEquals(_worker, worker) || !ReferenceEquals(_sceneStatisticsIntent, intent) ||
                !ReferenceEquals(_sceneStatistics, view) || !intent.Current) return;
            _sceneStatisticsReport = report;
            view.Display(report, SceneStatisticsFrameForWorker(), SceneStatisticsCatalogLabel());
            if (stale) InvalidateSceneStatistics("These results could not be settled. Refresh to inspect the current document. " +
                string.Join(" ", report.Reasons));
        });
    }

    private void LocateSceneStatisticsBinding(SceneStatisticsWindow view, MissingSceneBinding missing)
    {
        if (_worker is not { } worker || _sceneStatisticsIntent is not { Current: true } intent ||
            _sceneStatisticsReport is not { } report || !report.Missing.Contains(missing)) return;
        if (BistroLocatorProtected())
        {
            view.ShowNotice("Finish or cancel the current draft or gesture, then locate again.");
            return;
        }
        ulong selection = _selection.Generation;
        view.SetLocateBusy(true);
        worker.LocateSceneStatistic(intent, report, missing, selection, accepted =>
        {
            if (!ReferenceEquals(_sceneStatistics, view) || !ReferenceEquals(_sceneStatisticsIntent, intent)) return;
            view.SetLocateBusy(false);
            if (!accepted || !ReferenceEquals(_worker, worker) || !worker.SceneStatisticsCurrent(intent, report.Document) ||
                selection != _selection.Generation || BistroLocatorProtected() ||
                !_hierarchyRows.Any(row => row.Node == missing.Address.Node && ReferenceEquals(row.Incarnation, missing.NodeIdentity)))
            {
                view.ShowNotice("The node, field or edit context changed. Refresh and locate again.");
                return;
            }
            _locateMaterialFocus = missing.Address;
            if (!_hierarchyProjection.Rows.Any(row => row.Node == missing.Address.Node)) ClearHierarchyFilters();
            Activate();
            SetSelection(missing.Address.Node, scroll: true);
            FocusReferenceLauncher(missing.Address);
            view.ShowNotice($"Located Renderable.{missing.FieldName}.");
        });
    }
}

internal sealed class SceneStatisticsWindow : Window
{
    private readonly ComboBox _scope = new() { ItemsSource = new[] { "Whole scene", "Selected subtree" }, SelectedIndex = 0 };
    private readonly Button _refresh = new() { Content = "Refresh" };
    private readonly Button _locate = new() { Content = "Locate in Inspector", IsEnabled = false };
    private readonly TextBlock _context = EditorVisuals.Secondary("Choose a scope and refresh.");
    private readonly TextBlock _numbers = EditorVisuals.Secondary("");
    private readonly TextBlock _frame = EditorVisuals.Secondary("");
    private readonly TextBlock _references = EditorVisuals.Secondary("");
    private readonly TextBlock _notice = EditorVisuals.Secondary("");
    private readonly ListBox _missing = new();
    private readonly DispatcherTimer _expiry = new() { Interval = TimeSpan.FromSeconds(30) };
    private Action? _expired;
    private bool _current, _locating;
    public bool SelectedSubtree => _scope.SelectedIndex == 1;
    public event Action? ScopeChanged;
    public event Action? RefreshRequested;
    public event Action<MissingSceneBinding>? LocateRequested;

    public SceneStatisticsWindow()
    {
        Title = "Scene Statistics";
        Width = 720;
        Height = 570;
        MinWidth = 460;
        MinHeight = 380;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ShowActivated = false;
        foreach (var line in new[] { _context, _numbers, _frame, _references, _notice }) line.TextWrapping = TextWrapping.Wrap;
        var root = new Grid { Margin = new Thickness(16), RowSpacing = 12, RowDefinitions = new("Auto,*,Auto") };
        root.Children.Add(EditorVisuals.Stack(8, EditorVisuals.Actions(_scope, _refresh), _context, _numbers, _frame, _references));
        Grid.SetRow(_missing, 1);
        root.Children.Add(_missing);
        var footer = EditorVisuals.Stack(6, _notice, EditorVisuals.Actions(_locate));
        Grid.SetRow(footer, 2);
        root.Children.Add(footer);
        Content = root;

        _scope.SelectionChanged += (_, _) => ScopeChanged?.Invoke();
        _refresh.Click += (_, _) => RefreshRequested?.Invoke();
        _missing.SelectionChanged += (_, _) => UpdateLocate();
        _locate.Click += (_, _) =>
        {
            if (_locate.IsEnabled && _missing.SelectedItem is MissingSceneBinding missing) LocateRequested?.Invoke(missing);
        };
        _expiry.Tick += (_, _) => { _expiry.Stop(); _expired?.Invoke(); };
        Closed += (_, _) => _expiry.Stop();
    }

    public void BeginRead(Action expired)
    {
        _expired = expired;
        _expiry.Start();
        _notice.Text = "Reading the document and running catalog…";
    }

    public void Display(SceneStatisticsReport? report, SceneStatisticsFrame? frame, string catalog)
    {
        _expiry.Stop();
        _current = report?.Document is not null;
        _locating = false;
        _notice.Text = report is null ? "" : string.Join(" ", report.Reasons);
        _context.Text = report is null ? "Choose a scope and refresh." :
            $"{report.Scope} · {report.Scene?.ToString() ?? "scene unavailable"} · " +
            $"document revision {report.Document?.DocumentRevision.ToString() ?? "unavailable"}\n{catalog}";
        _numbers.Text = report is null ? "" : $"Nodes · {report.Nodes?.ToString("N0") ?? "unavailable"}\n" +
            $"Estimated content draw items · {Metric(report.DrawItems, report.DrawComplete)}\n" +
            $"Indexed triangles · {Metric(report.Triangles, report.TrianglesComplete)}\n" +
            $"Unique cooked texture payload · {Metric(report.TextureBytes, report.TexturesComplete, " bytes")} " +
            $"({report.Textures:N0} measured texture GUIDs)";
        _references.Text = report is null ? "" : !report.ReferencesComplete
            ? $"Check incomplete · {report.Missing.Count:N0} confirmed missing direct references"
            : report.Missing.Count == 0 ? "No missing direct references"
            : $"{report.Missing.Count:N0} missing in running catalog";
        _missing.ItemsSource = report?.Missing;
        if (_missing.ItemCount > 0) _missing.SelectedIndex = 0;
        SetFrame(report, frame, false);
        UpdateLocate();
    }

    public void SetFrame(SceneStatisticsReport? report, SceneStatisticsFrame? frame, bool stale)
    {
        _frame.Text = frame is null ? "Runtime draw items unavailable · use an existing Bistro capture." :
            $"Full-scene frame report · {frame.DrawItems:N0} draw items · frame {frame.Frame:N0} · " +
            $"projection {frame.Generation} · {frame.Scene} · " +
            (!stale && report is { Projection: { } generation } && generation != frame.Generation
                ? "earlier projection" : "historical sample");
    }

    public void Invalidate(string reason, SceneStatisticsReport? report, SceneStatisticsFrame? frame)
    {
        _expiry.Stop();
        _current = _locating = false;
        _notice.Text = reason;
        if (report is not null) _references.Text = "Stale · previous results";
        SetFrame(report, frame, true);
        UpdateLocate();
    }

    public void ShowNotice(string reason) => _notice.Text = reason;
    public void SetLocateBusy(bool busy) { _locating = busy; UpdateLocate(); }
    private void UpdateLocate() => _locate.IsEnabled = _current && !_locating && _missing.SelectedItem is MissingSceneBinding { CanLocate: true };
    private static string Metric(ulong value, bool complete, string suffix = "") =>
        complete ? value.ToString("N0") + suffix : value == 0 ? "unavailable · incomplete" :
            $"{value:N0}{suffix} known subtotal · incomplete";
}
