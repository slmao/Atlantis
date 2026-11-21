using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.Controls.Primitives;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private sealed record SourceRow(ProjectSourceKey Key, ProjectSource Source);
    private sealed class AssetFolder(string label, string? path, ProjectRootKind? root)
    {
        public string Label { get; } = label;
        public string? Path { get; } = path;
        public ProjectRootKind? Root { get; } = root;
        public List<AssetFolder> Children { get; } = [];
    }

    private readonly TreeView _runtimeDirectoryTree = new() { MaxHeight = 155 };
    private readonly TreeView _projectDirectoryTree = new() { MaxHeight = 150 };
    private readonly TextBox _projectSearch = new() { PlaceholderText = "Search source names and paths" };
    private readonly ComboBox _projectKind = new() { ItemsSource = new[] { "All sources", "glTF", "PNG", "Prefab source", "Other" }, SelectedIndex = 0 };
    private readonly ListBox _projectRows = new() { Height = 224 };
    private readonly ComboBox _projectOutputs = new() { MaxWidth = 340, HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly TextBlock _projectStatus = EditorVisuals.Secondary("Choose Project to scan the authorized roots.");
    private readonly TextBlock _projectEntryState = EditorVisuals.Secondary("");
    private readonly TextBlock _projectRuntimeNotice = EditorVisuals.Secondary("");
    private readonly Button _showProjectSource = new() { Content = "Show Project Source", IsEnabled = false };
    private readonly Button _showRuntimeAsset = new() { Content = "Show in Runtime", IsEnabled = false };
    private readonly Button _openProjectImport = new() { Content = "Open Import", IsEnabled = false };
    private readonly Button _projectRefresh = new() { Content = "Refresh" };
    private readonly Button _runtimeScope = new() { Content = "Runtime", IsEnabled = false };
    private readonly Button _projectScope = new() { Content = "Project" };
    private Control? _projectPanel, _runtimePanel;
    private Expander? _projectGltfImport, _projectPngImport;
    private IReadOnlyList<AssetSummary>? _directoryCatalog;
    private AssetFolder? _runtimeFolders, _projectFolders;
    private ProjectScan? _projectScan;
    private ProjectSourceKey? _selectedProjectRow;
    private ProjectRootKind? _projectRoot;
    private string? _projectDirectory;
    private bool _projectVisible, _syncProject, _projectPending, _projectStale;
    private long _projectSession, _projectIntent;
    private string? _projectScanNotice;
    private readonly DispatcherTimer _projectPoll = new() { Interval = TimeSpan.FromMilliseconds(150) };
    private Action? _pollProject;

    private Control WrapProjectAssets(Control runtime)
    {
        _runtimePanel = runtime;
        _projectPanel = BuildProjectPanel();
        _projectPanel.IsVisible = false;
        ConfigureDirectoryTree(_runtimeDirectoryTree, "Catalog directories");
        ConfigureDirectoryTree(_projectDirectoryTree, "Project source directories");
        _runtimeDirectoryTree.SelectionChanged += (_, _) =>
        {
            if (_syncProject || _runtimeDirectoryTree.SelectedItem is not AssetFolder folder) return;
            ++_projectIntent;
            _assetBrowser.Directory = folder.Path;
            RenderAssets();
        };
        _projectDirectoryTree.SelectionChanged += (_, _) =>
        {
            if (_syncProject || _projectDirectoryTree.SelectedItem is not AssetFolder folder) return;
            ++_projectIntent;
            _projectRoot = folder.Root;
            _projectDirectory = folder.Path;
            RenderProjectRows();
        };
        _runtimeScope.Click += (_, _) => ShowProjectView(false);
        _projectScope.Click += (_, _) => { ShowProjectView(true); if (_projectScan is null) RefreshProject(); };
        _showProjectSource.Click += (_, _) => NavigateToProject();
        _projectPoll.Tick += (_, _) =>
        {
            _projectPoll.Stop();
            var poll = _pollProject;
            _pollProject = null;
            poll?.Invoke();
        };
        _taskIssues.Changed += UpdateProjectActions;
        var host = new DockPanel();
        AddDocked(host, EditorVisuals.Bar(EditorVisuals.Actions(_runtimeScope, _projectScope)), Dock.Top);
        host.Children.Add(new Panel { Children = { runtime, _projectPanel } });
        return host;
    }

    private Control BuildProjectPanel()
    {
        _projectSearch.Classes.Add("editor-search");
        AutomationProperties.SetName(_projectSearch, "Search source names and paths");
        AutomationProperties.SetName(_projectKind, "Source type");
        AutomationProperties.SetName(_projectRows, "Project source results");
        AutomationProperties.SetName(_projectOutputs, "Verified Runtime outputs");
        _projectRows.ItemsPanel = new FuncTemplate<Panel?>(() => new VirtualizingStackPanel());
        _projectRows.AutoScrollToSelectedItem = false;
        _projectRows.ItemTemplate = new FuncDataTemplate<SourceRow>((row, _) =>
        {
            var label = new TextBlock { Text = row.Key.Path, TextTrimming = TextTrimming.CharacterEllipsis };
            ToolTip.SetTip(label, SourceLabel(row.Key));
            return EditorVisuals.Columns("*,Auto", label, EditorVisuals.Secondary(row.Source.Kind.ToString()));
        });
        _projectOutputs.ItemTemplate = new FuncDataTemplate<AssetSummary>((asset, _) =>
            new TextBlock { Text = AssetBrowseProjection.AssetKind(asset) + " · " + EditorVisuals.AssetName(asset),
                TextTrimming = TextTrimming.CharacterEllipsis });
        _projectSearch.TextChanged += (_, _) => ProjectFiltersChanged();
        _projectKind.SelectionChanged += (_, _) => ProjectFiltersChanged();
        _projectRows.SelectionChanged += (_, _) =>
        {
            if (_syncProject) return;
            ++_projectIntent;
            _selectedProjectRow = (_projectRows.SelectedItem as SourceRow)?.Key;
            UpdateProjectActions();
        };
        _projectOutputs.SelectionChanged += (_, _) =>
        {
            if (_syncProject) return;
            ++_projectIntent;
            _showRuntimeAsset.IsEnabled = _projectOutputs.SelectedItem is AssetSummary && !_projectPending && !_projectStale;
        };
        _projectRefresh.Click += (_, _) => RefreshProject();
        _showRuntimeAsset.Click += (_, _) => NavigateToRuntime();
        _openProjectImport.Click += (_, _) => OpenProjectImport();
        var heading = EditorVisuals.Bar(EditorVisuals.Stack(5,
            EditorVisuals.Columns("*,Auto", EditorVisuals.Heading("Project sources"), _projectRefresh),
            _projectSearch, _projectKind, _projectStatus));
        var body = EditorVisuals.Stack(7, _projectDirectoryTree, _projectRows, _projectEntryState, _projectOutputs,
            EditorVisuals.Actions(_showRuntimeAsset, _openProjectImport));
        body.Margin = new Thickness(8);
        var panel = new DockPanel();
        AddDocked(panel, heading, Dock.Top);
        panel.Children.Add(EditorVisuals.Scroll(body));
        panel.SizeChanged += (_, e) => _projectRows.Height = e.NewSize.Height < 510 ? 150 : 224;
        return panel;
    }

    private static void ConfigureDirectoryTree(TreeView tree, string name)
    {
        AutomationProperties.SetName(tree, name);
        tree.ItemTemplate = new FuncTreeDataTemplate<AssetFolder>((item, _) =>
            new TextBlock { Text = item.Label, TextTrimming = TextTrimming.CharacterEllipsis },
            item => item.Children);
    }

    private static string SourceLabel(ProjectSourceKey key) =>
        (key.Root == ProjectRootKind.Inbox ? "inbox/" : "assets/prefabs/") + key.Path;

    private static AssetFolder Directory(AssetFolder root, string path, ProjectRootKind? key)
    {
        var at = root;
        string prefix = "";
        foreach (string part in path.Split('/'))
        {
            prefix = prefix.Length == 0 ? part : prefix + "/" + part;
            string current = prefix;
            var next = at.Children.Find(child => child.Path == current);
            if (next is null)
            {
                next = new AssetFolder(part, current, key);
                at.Children.Add(next);
            }
            at = next;
        }
        return at;
    }

    private void RenderRuntimeDirectories()
    {
        if (!ReferenceEquals(_directoryCatalog, _assetBrowser.Items))
        {
            _directoryCatalog = _assetBrowser.Items;
            _runtimeFolders = new AssetFolder("All catalog assets", null, null);
            foreach (var path in _assetBrowser.Directories.Order(StringComparer.OrdinalIgnoreCase).ThenBy(path => path, StringComparer.Ordinal))
                Directory(_runtimeFolders, path, null);
            _syncProject = true;
            try { _runtimeDirectoryTree.ItemsSource = new[] { _runtimeFolders }; }
            finally { _syncProject = false; }
        }
        if (_runtimeFolders is null) return;
        _syncProject = true;
        try
        {
            var selected = _assetBrowser.Directory is { } path ? Directory(_runtimeFolders, path, null) : _runtimeFolders;
            if (!ReferenceEquals(_runtimeDirectoryTree.SelectedItem, selected)) _runtimeDirectoryTree.SelectedItem = selected;
        }
        finally { _syncProject = false; }
    }

    private void ShowProjectView(bool project)
    {
        if (_projectVisible == project || _runtimePanel is null || _projectPanel is null) return;
        ++_projectIntent;
        _assetPress = null;
        if (project) RetireAssetDrop();
        _projectVisible = project;
        _runtimePanel.IsVisible = !project;
        _projectPanel.IsVisible = project;
        _runtimeScope.IsEnabled = project;
        _projectScope.IsEnabled = !project;
        UpdateProjectSourceAction();
        SyncPreviewSelection(invalidate: true);
        if (project) RenderProjectRows();
        else RenderAssets();
    }

    private void ResetProjectScan()
    {
        ++_projectSession;
        ++_projectIntent;
        _projectPoll.Stop();
        _pollProject = null;
        _projectPending = _projectStale = false;
        _projectScan = null;
        _selectedProjectRow = null;
        _projectRoot = null;
        _projectDirectory = null;
        _projectScanNotice = null;
        RenderProjectTree();
        RenderProjectRows();
    }

    private void RefreshProject() => RunProjectScan(scan =>
    {
        _projectScan = scan;
        _projectStale = false;
        _projectScanNotice = null;
        _projectRuntimeNotice.Text = "";
        RenderProjectTree();
        RenderProjectRows();
    });

    private void RunProjectScan(Action<ProjectScan> done)
    {
        if (_projectPending) return;
        if (_worker is not { } worker || _referenceSessionFailed)
        {
            _projectScanNotice = "The Runtime is unavailable.";
            UpdateProjectActions();
            return;
        }
        long session = _projectSession;
        long intent = _projectIntent;
        _projectPending = true;
        _projectScanNotice = null;
        UpdateProjectActions();
        void Poll(AssetGuid? id) => worker.ReadProjectScan(id, response =>
        {
            if (session != _projectSession || !ReferenceEquals(_worker, worker) || _shuttingDown || _referenceSessionFailed) return;
            if (response.IsOk && response.Value is { Pending: true } pending)
            {
                _pollProject = () => Poll(pending.Id);
                _projectPoll.Start();
                return;
            }
            _projectPending = false;
            if (intent != _projectIntent)
            {
                _projectStale = true;
                _projectScanNotice = "Selection or browse context changed. Refresh to update the source inventory.";
                if (!_projectVisible) _projectRuntimeNotice.Text = _projectScanNotice;
            }
            else if (response.IsOk && response.Value is { } scan)
                done(scan);
            else
            {
                _projectStale = true;
                _projectScanNotice = response.Error switch
                {
                    ProjectScanError.Unsupported => "Project sources are unavailable in this Runtime. Use an authorized development launch.",
                    ProjectScanError.Busy => "An asset job or source scan is running. Refresh after it settles.",
                    ProjectScanError.UnknownScan => "The scan is no longer available. Refresh.",
                    _ => "The source scan is unavailable. Refresh or check the Runtime connection."
                };
                if (!_projectVisible) _projectRuntimeNotice.Text = _projectScanNotice;
            }
            UpdateProjectActions();
        });
        Poll(null);
    }

    private void RenderProjectTree()
    {
        _projectFolders = new AssetFolder("All source roots", null, null);
        foreach (var root in _projectScan?.Roots ?? [])
        {
            var folder = new AssetFolder(root.Kind == ProjectRootKind.Inbox ? "inbox/" : "assets/prefabs/", null, root.Kind);
            foreach (var row in root.Sources.Where(source => source.Kind == ProjectSourceKind.Directory)
                .OrderBy(source => source.Path, StringComparer.OrdinalIgnoreCase).ThenBy(source => source.Path, StringComparer.Ordinal))
                Directory(folder, row.Path, root.Kind);
            _projectFolders.Children.Add(folder);
        }
        _syncProject = true;
        try
        {
            _projectDirectoryTree.ItemsSource = new[] { _projectFolders };
            SelectProjectFolder();
        }
        finally { _syncProject = false; }
    }

    private void SelectProjectFolder()
    {
        if (_projectFolders is null) return;
        var root = _projectFolders.Children.FirstOrDefault(folder => folder.Root == _projectRoot) ?? _projectFolders;
        _projectDirectoryTree.SelectedItem = _projectDirectory is { } path ? Directory(root, path, _projectRoot) : root;
    }

    private void ProjectFiltersChanged()
    {
        if (_syncProject) return;
        ++_projectIntent;
        RenderProjectRows();
    }

    private void RenderProjectRows()
    {
        var kind = _projectKind.SelectedIndex > 0 ? (ProjectSourceKind?)_projectKind.SelectedIndex : null;
        string query = (_projectSearch.Text ?? "").Trim().Replace('\\', '/');
        var rows = (_projectScan?.Roots ?? []).Where(root => _projectRoot is null || root.Kind == _projectRoot)
            .SelectMany(root => root.Sources.Where(row => row.Kind != ProjectSourceKind.Directory &&
                (kind is null || row.Kind == kind) && ProjectNavigation.InDirectory(row.Path, _projectDirectory) &&
                SourceLabel(new(root.Kind, row.Path)).Contains(query, StringComparison.OrdinalIgnoreCase))
                .Select(row => new SourceRow(new(root.Kind, row.Path), row)))
            .OrderBy(row => row.Key.Root).ThenBy(row => row.Key.Path, StringComparer.OrdinalIgnoreCase)
            .ThenBy(row => row.Key.Path, StringComparer.Ordinal).ToArray();
        _syncProject = true;
        try
        {
            _projectRows.ItemsSource = rows;
            _projectRows.SelectedItem = rows.FirstOrDefault(row => row.Key == _selectedProjectRow);
        }
        finally { _syncProject = false; }
        UpdateProjectActions();
    }

    private void UpdateProjectSourceAction()
    {
        _showProjectSource.IsVisible = _projectScan is not null;
        _showProjectSource.IsEnabled = !_projectPending && !_projectStale && !_projectVisible &&
            _assetBrowser.SelectionVisible && _assetBrowser.Selected is AssetGuid guid &&
            ProjectNavigation.SourceOf(_projectScan, guid) is not null;
    }

    private void UpdateProjectActions()
    {
        _projectRefresh.IsEnabled = !_projectPending && _worker is not null && !_referenceSessionFailed;
        var roots = (_projectScan?.Roots ?? []).Where(root => _projectRoot is null || root.Kind == _projectRoot).ToArray();
        string scopeStatus = string.Join("\n", roots.Select(root =>
            (root.Kind == ProjectRootKind.Inbox ? "Inbox" : "Prefab sources") + " · " + root.Coverage +
            (string.IsNullOrEmpty(root.Diagnostic) ? "" : " · " + root.Diagnostic)));
        if (roots.Length != 0 && roots.All(root => root.Coverage == ProjectCoverage.Complete) &&
            _projectRows.Items.Count == 0) scopeStatus += "\nNo matching source files.";
        if (_projectScan is { CatalogReady: false }) scopeStatus += "\nThe active catalog is unavailable.";
        _projectStatus.Text = _projectPending ? "Checking Project sources…" : _projectScanNotice ??
            (scopeStatus.Length == 0 ? "Refresh to read the authorized Project roots." : scopeStatus.Trim());

        SourceRow? picked = _projectRows.SelectedItem as SourceRow;
        var source = picked?.Source;
        var task = source?.ImportName is string name ? MatchingImportTask(source.Kind, name) : null;
        _projectEntryState.Text = source is null ? (_selectedProjectRow is null ? "" : "Selection hidden by the current filters.") :
            SourceLabel(picked!.Key) + "\n" + SourceState(source, task);
        var prior = (_projectOutputs.SelectedItem as AssetSummary)?.Guid;
        var outputs = source is null ? [] : _assetBrowser.Items.Where(asset => source.Outputs.Contains(asset.Guid)).ToArray();
        _syncProject = true;
        try
        {
            if (!_projectOutputs.Items.Cast<AssetSummary>().Select(row => row.Guid).SequenceEqual(outputs.Select(row => row.Guid)))
            {
                _projectOutputs.ItemsSource = outputs;
                _projectOutputs.SelectedItem = outputs.FirstOrDefault(row => row.Guid == prior) ??
                    (outputs.Length == 1 ? outputs[0] : null);
            }
            _projectOutputs.IsVisible = outputs.Length > 0;
        }
        finally { _syncProject = false; }
        _showRuntimeAsset.IsEnabled = !_projectPending && !_projectStale && picked is not null &&
            _projectOutputs.SelectedItem is AssetSummary && _assetBrowser.State == "Ready";
        _openProjectImport.IsEnabled = !_projectPending && !_projectStale && source?.ImportName is not null &&
            source.Registration != ProjectRegistration.Unknown && picked is not null &&
            task?.Result is not (TaskResult.Unknown or TaskResult.Requesting or TaskResult.Running or TaskResult.Succeeded) &&
            !_referenceSessionFailed;
        UpdateProjectSourceAction();
    }

    private TaskIssue? MatchingImportTask(ProjectSourceKind kind, string name) =>
        _taskIssues.Rows.FirstOrDefault(row => row.Target == name && (kind == ProjectSourceKind.Gltf
            ? row.Operation is "Register scene import" or "Resume scene import"
            : row.Operation == ColorTextureLabel + " Register" || row.Operation == ColorTextureLabel + " Resume"));

    private string SourceState(ProjectSource source, TaskIssue? task)
    {
        if (task?.Result == TaskResult.Succeeded && task.Published is not null && !_taskIssues.ActivationKnown)
            return "Status unavailable · verify the active generation.";
        if (task is not null && !(task.Result == TaskResult.Succeeded && task.Published == _taskIssues.ActiveGeneration))
            return task.Summary(_taskIssues.ActivationKnown, _taskIssues.ActiveGeneration) + " · " +
                task.NextStep(_taskIssues.ActivationKnown, _taskIssues.ActiveGeneration);
        if (_projectStale) return "Status unavailable · refresh sources.";
        if (source.Outputs.Count != 0)
            return source.NewerPrefabSource ? "Active frozen Prefab · use Build/Open to activate the newer checkout source." :
                "Available in this Runtime · preview and scene actions use the active build.";
        if (source.Registration == ProjectRegistration.Registered && _projectScan?.CatalogReady == true)
            return "Registered · absent from this Runtime.";
        if (source.Registration == ProjectRegistration.SourceOnly)
            return "Source only · open the existing import controls to continue.";
        return source.ImportName is null && source.Kind != ProjectSourceKind.Prefab
            ? "Browse only" : "Status unavailable · check source coverage and active generation.";
    }

    private void RecheckProject(SourceRow row, AssetGuid? output, Action<ProjectScan> complete)
    {
        RunProjectScan(scan =>
        {
            if (!ProjectNavigation.Matches(scan, row.Key, row.Source, output))
            {
                _projectStale = true;
                _projectScanNotice = "The source or active link changed. Refresh before trying again.";
                if (!_projectVisible) _projectRuntimeNotice.Text = _projectScanNotice;
                return;
            }
            _projectScan = scan;
            _projectStale = false;
            _projectRuntimeNotice.Text = "";
            complete(scan);
        });
    }

    private void NavigateToRuntime()
    {
        if (!_showRuntimeAsset.IsEnabled || _projectRows.SelectedItem is not SourceRow row ||
            _projectOutputs.SelectedItem is not AssetSummary asset) return;
        var catalog = _assetBrowser.Items;
        RecheckProject(row, asset.Guid, _ =>
        {
            if (!_projectVisible || !ReferenceEquals(catalog, _assetBrowser.Items) ||
                _assetBrowser.State != "Ready" || !_assetBrowser.Items.Any(item => item.Guid == asset.Guid)) return;
            ShowProjectView(false);
            _assetBrowser.Directory = new AssetBrowseProjection(asset).Directory;
            if (_assetBrowser.Reveal(asset.Guid)) RenderAssets();
            _revealAssetFilters = true;
            try
            {
                _assetSearch.Text = _assetBrowser.Search;
                _assetTypes.SelectedIndex = _assetBrowser.Type is AssetType type ? (int)type + 1 : 0;
            }
            finally { _revealAssetFilters = false; }
            ReadAsset(asset.Guid);
            _assetsList.ScrollIntoView(asset);
            _assetsList.Focus();
        });
    }

    private void NavigateToProject()
    {
        if (!_showProjectSource.IsEnabled || _assetBrowser.Selected is not AssetGuid guid ||
            ProjectNavigation.SourceOf(_projectScan, guid) is not { } key ||
            ProjectNavigation.Find(_projectScan, key) is not { } source) return;
        var catalog = _assetBrowser.Items;
        RecheckProject(new SourceRow(key, source), guid, _ =>
        {
            if (_projectVisible || !ReferenceEquals(catalog, _assetBrowser.Items) ||
                !_assetBrowser.SelectionVisible || _assetBrowser.Selected != guid) return;
            _selectedProjectRow = key;
            _projectRoot = key.Root;
            int slash = key.Path.LastIndexOf('/');
            _projectDirectory = slash < 0 ? null : key.Path[..slash];
            _syncProject = true;
            try
            {
                if (!SourceLabel(key).Contains((_projectSearch.Text ?? "").Trim().Replace('\\', '/'),
                    StringComparison.OrdinalIgnoreCase)) _projectSearch.Text = "";
                if (_projectKind.SelectedIndex != 0 && _projectKind.SelectedIndex != (int)source.Kind)
                    _projectKind.SelectedIndex = 0;
            }
            finally { _syncProject = false; }
            RenderProjectTree();
            ShowProjectView(true);
            if (_projectRows.SelectedItem is { } selected) _projectRows.ScrollIntoView(selected);
            _projectRows.Focus();
        });
    }

    private void OpenProjectImport()
    {
        if (!_openProjectImport.IsEnabled || _projectRows.SelectedItem is not SourceRow row ||
            row.Source.ImportName is not string name) return;
        RecheckProject(row, null, _ =>
        {
            if (!_projectVisible || _selectedProjectRow != row.Key ||
                MatchingImportTask(row.Source.Kind, name)?.Result is TaskResult.Unknown or TaskResult.Requesting or TaskResult.Running)
                return;
            var form = row.Source.Kind == ProjectSourceKind.Gltf ? _projectGltfImport : _projectPngImport;
            if (form is null) return;
            ShowProjectView(false);
            form.IsExpanded = true;
            var target = row.Source.Kind == ProjectSourceKind.Gltf ? _importName : _textureName;
            if (row.Source.Kind == ProjectSourceKind.Png) _textureKind.SelectedIndex = 0;
            target.Text = name;
            target.BringIntoView();
            target.Focus();
        });
    }
}

