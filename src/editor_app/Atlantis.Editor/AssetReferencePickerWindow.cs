using System;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Automation;
using Avalonia.Media;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Layout;

namespace Atlantis.Editor;

public sealed class AssetReferencePickerWindow : Window
{
    private readonly EditorWorker _worker;
    private readonly AssetBrowser _browser = new();
    private readonly ListBox _list = new();
    private readonly StackPanel _details = new() { Spacing = 4 };
    private readonly TextBlock _state = new();
    private readonly TextBlock _current = EditorVisuals.Secondary("");
    private readonly TextBlock _candidate = EditorVisuals.Ellipsis("No candidate selected");
    private readonly TextBlock _catalogRecovery = EditorVisuals.Secondary("Restart Runtime after repairing the catalog.");
    private readonly SelectableTextBlock _openingMeshDetail = EditorVisuals.Details("");
    private readonly Button _confirm = new() { Content = "Confirm", IsEnabled = false };
    private readonly TextBlock _openingMesh = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    internal Button PreviewButton { get; } = new() { Content = "Preview with material", IsEnabled = false };
    internal TextBlock PreviewState { get; } = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    internal Image PreviewImage { get; } = new() { Height = 256, Stretch = Stretch.Uniform, IsVisible = false };
    internal event Action? PreviewInputsChanged;
    private bool _sync, _closed, _invalid, _submitted, _initial = true;
    private long _displayedListRequest;
    public AssetReferenceTarget Target { get; }

    internal AssetPreviewInputs? PreviewInputs =>
        Target.SupportsMaterialPreview && !_closed && !_invalid && !_submitted && _browser.State == "Ready" &&
        Target.OpeningMesh is AssetGuid mesh && _browser.Items.Any(a => a.Guid == mesh && a.Type == AssetType.Mesh) &&
        _list.SelectedItem is AssetSummary { Type: AssetType.Material } material && _browser.Selected == material.Guid
            ? new AssetPreviewInputs(mesh, material.Guid, Target.Request) : null;

    public AssetReferencePickerWindow(EditorWorker worker, AssetReferenceTarget target,
                                      Action<long, AssetGuid> confirm, Action cancel)
    {
        _worker = worker; Target = target;
        bool material = target.SupportsMaterialPreview;
        string title = material ? "Choose material" : "Choose asset";
        Title = title + " — " + target.Label;
        Width = material ? 900 : 760;
        Height = 630;
        MinWidth = 560;
        MinHeight = 520;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        var root = new DockPanel { LastChildFill = true, Background = EditorVisuals.Pane };
        _state.TextWrapping = TextWrapping.Wrap;
        _catalogRecovery.IsVisible = false;
        _openingMesh.TextWrapping = TextWrapping.Wrap;
        _current.Text = "Current: " + (target.Before.IsAbsent ? "(absent)" : target.Before.AsAsset().ToString());
        var search = new TextBox { PlaceholderText = "Source or GUID", MinWidth = 100 };
        search.Classes.Add("editor-search");
        AutomationProperties.SetName(search, "Search assets by source or GUID");
        var reload = new Button { Content = "Reload view" };
        _browser.Type = target.TargetAssetType;
        Control filters;
        if (target.TargetAssetType is null)
        {
            var types = new ComboBox { ItemsSource = new[] { "All types" }.Concat(Enum.GetNames<AssetType>()).ToList(), SelectedIndex = 0 };
            AutomationProperties.SetName(types, "Asset type");
            types.SelectionChanged += (_, _) =>
            {
                _browser.Type = types.SelectedIndex <= 0 ? null : Enum.GetValues<AssetType>()[types.SelectedIndex - 1];
                Render();
            };
            filters = EditorVisuals.Columns("Auto,*,Auto", types, search, reload);
        }
        else filters = EditorVisuals.Columns("*,Auto", search, reload);
        search.TextChanged += (_, _) => { _browser.Search = search.Text ?? ""; Render(); };
        reload.Click += (_, _) => { if (!_invalid) Load(); };
        var top = EditorVisuals.Bar(EditorVisuals.Stack(5,
            EditorVisuals.Heading(title, 22), EditorVisuals.Secondary(target.Label), _current, filters, _state, _catalogRecovery));
        DockPanel.SetDock(top, Dock.Top);
        root.Children.Add(top);

        var back = new Button { Content = "Cancel" };
        back.Click += (_, _) => Close();
        _confirm.Classes.Add("editor-primary");
        _confirm.MinWidth = 96;
        _confirm.Click += (_, _) =>
        {
            if (!_confirm.IsEnabled || _closed || _invalid || _submitted || _browser.State != "Ready" ||
                _list.SelectedItem is not AssetSummary candidate || _browser.Selected != candidate.Guid) return;
            _submitted = true; _confirm.IsEnabled = false;
            PreviewInputsChanged?.Invoke();
            confirm(_displayedListRequest, candidate.Guid); Close();
        };
        var buttons = EditorVisuals.Bar(EditorVisuals.Columns("*,Auto,Auto", _candidate, back, _confirm));
        DockPanel.SetDock(buttons, Dock.Bottom);
        root.Children.Add(buttons);
        _list.Height = 224;
        AutomationProperties.SetName(_list, "Matching assets");
        _list.ItemTemplate = new FuncDataTemplate<AssetSummary>((asset, _) =>
            asset is null ? new TextBlock() : EditorVisuals.AssetRow(asset));
        _list.SelectionChanged += (_, e) =>
        {
            if (!ReferenceEquals(e.Source, _list) || _sync || _invalid) return;
            if (_list.SelectedItem is AssetSummary a) Read(a.Guid);
            else { _browser.ClearSelection(); Render(); }
        };
        var guidance = EditorVisuals.Secondary(target.TargetAssetType is AssetType required
            ? "Target type: " + required + ". Type matches can still fail during Runtime apply."
            : "Target type unspecified. Catalog presence does not guarantee compatibility.");
        var referenceDetails = EditorVisuals.Disclosure("Asset details", EditorVisuals.Stack(4,
            EditorVisuals.Details("Target: " + target.Path + "\nCurrent value: " +
                (target.Before.IsAbsent ? "(absent)" : target.Before.AsAsset().ToString())),
            _openingMeshDetail,
            EditorVisuals.Secondary("Catalog fixed at first query. Restart Runtime after a changed build or failed initial load. Reload view does not reread disk."),
            _details));
        var results = EditorVisuals.Stack(6, EditorVisuals.Heading(material ? "Materials" : "Assets"),
            _list, guidance, referenceDetails);
        var body = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions(material ? "*,1.08*" : "*"),
            RowDefinitions = new RowDefinitions("Auto,Auto"),
            Margin = new Thickness(14, 10),
        };
        body.Children.Add(results);
        if (material)
        {
            var preview = EditorVisuals.Group(EditorVisuals.Heading("Combination preview"),
                EditorVisuals.Stack(6, _openingMesh, PreviewButton, PreviewState, PreviewImage));
            preview.Margin = new Thickness(10, 0, 0, 0);
            preview.VerticalAlignment = VerticalAlignment.Top;
            Grid.SetColumn(preview, 1);
            body.Children.Add(preview);
            root.SizeChanged += (_, e) =>
            {
                bool compact = e.NewSize.Width < 720;
                body.ColumnDefinitions[1].Width = compact ? new GridLength(0) : new GridLength(1.08, GridUnitType.Star);
                Grid.SetColumn(preview, compact ? 0 : 1);
                Grid.SetRow(preview, compact ? 1 : 0);
                preview.Margin = compact ? new Thickness(0, 10, 0, 0) : new Thickness(10, 0, 0, 0);
                _list.Height = compact ? 192 : 224;
                PreviewImage.Height = compact ? 216 : 256;
            };
        }
        root.Children.Add(EditorVisuals.Scroll(body));
        Content = root;
        KeyDown += (_, e) => { if (e.Key == Key.Escape) { e.Handled = true; Close(); } };
        Opened += (_, _) => Load();
        Closed += (_, _) =>
        {
            _closed = true; _browser.Disconnect();
            PreviewInputsChanged?.Invoke();
            if (!_submitted) cancel();
        };
    }

    public void Invalidate(string reason)
    {
        _invalid = true; _browser.Disconnect(); _state.Text = reason + "; close and reopen to choose again";
        _confirm.IsEnabled = false;
        _state.Foreground = EditorVisuals.Error;
        _catalogRecovery.IsVisible = false;
        _candidate.Text = "Reference target unavailable";
        PreviewInputsChanged?.Invoke();
    }

    private void Load()
    {
        long request = _browser.BeginList(); Render();
        _worker.LoadReferenceAssets(Target.Request, request, result =>
        {
            if (_closed || _invalid || !_browser.CompleteList(request, result)) return;
            if (result.IsOk) _displayedListRequest = request;
            if (result.IsOk && _initial)
            {
                _initial = false;
                if (Target.Before.TryGetAsset(out AssetGuid current))
                {
                    AssetSummary? listed = _browser.Items.FirstOrDefault(a => a.Guid == current);
                    if (listed is null) _current.Text += " (outside this snapshot)";
                    else if (Target.TargetAssetType is AssetType type && listed.Type != type)
                        _current.Text += " (listed as " + listed.Type + ")";
                    else
                    {
                        _current.Text = "Current: " + EditorVisuals.AssetName(listed);
                        if (_browser.Visible.Any(a => a.Guid == current)) _browser.Select(current);
                    }
                }
            }
            Render();
            if (result.IsOk && _browser.Selected is AssetGuid guid) Read(guid);
        });
    }

    private void Read(AssetGuid guid)
    {
        if (Target.TargetAssetType is not null && !_browser.Visible.Any(a => a.Guid == guid)) return;
        long request = _browser.Select(guid); Render();
        _worker.ReadAsset(guid, result =>
        { if (!_closed && !_invalid && _browser.CompleteDetail(request, guid, result)) Render(); });
    }

    private void Render()
    {
        if (_closed || _invalid) return;
        _state.Text = _browser.State == "Ready" && Target.TargetAssetType is AssetType type &&
            _browser.Items.Count != 0 && !_browser.Items.Any(a => a.Type == type)
            ? "No assets of type " + type : _browser.ViewState;
        _catalogRecovery.IsVisible = _browser.State == nameof(AssetError.CatalogUnavailable);
        _state.Foreground = _catalogRecovery.IsVisible ? EditorVisuals.Error : EditorVisuals.Muted;
        var visible = _browser.Visible;
        _sync = true;
        try { _list.ItemsSource = visible; _list.SelectedItem = visible.FirstOrDefault(a => a.Guid == _browser.Selected); }
        finally { _sync = false; }
        _confirm.IsEnabled = !_submitted && _browser.State == "Ready" &&
            _list.SelectedItem is AssetSummary candidate && candidate.Guid == _browser.Selected;
        _candidate.Text = _list.SelectedItem is AssetSummary selected
            ? "Selected: " + EditorVisuals.AssetName(selected) : "No candidate selected";
        ToolTip.SetTip(_candidate, _candidate.Text);
        if (Target.SupportsMaterialPreview)
        {
            var mesh = _browser.Items.FirstOrDefault(a => a.Guid == Target.OpeningMesh && a.Type == AssetType.Mesh);
            _openingMesh.Text = Target.OpeningMesh is not AssetGuid opening
                ? "Preview unavailable. No mesh was readable when the chooser opened."
                : mesh is not null ? "Mesh at opening: " + EditorVisuals.AssetName(mesh)
                : "Mesh at opening: " + opening + (_browser.State == "Ready" ? "\nNo mesh record in this catalog." : "");
            _openingMeshDetail.Text = Target.OpeningMesh is AssetGuid openingGuid
                ? "Opening mesh GUID: " + openingGuid + (mesh is null ? "" : "\n" + mesh.Source) : "";
            ToolTip.SetTip(_openingMesh, _openingMeshDetail.Text);
        }
        PreviewInputsChanged?.Invoke();
        _details.Children.Clear();
        if (_browser.Details is not AssetDetails d) { _details.Children.Add(new TextBlock { Text = _browser.DetailState }); return; }
        _details.Children.Add(EditorVisuals.Details(d.Type + "\n" + d.Source + "\n" + d.Guid +
            "\nartifact schema: " + d.ArtifactSchema + "\nsource schema: " + (d.SourceSchema?.ToString() ?? "none") + "\ntool: " + d.Tool));
        foreach (AssetGuid dependency in d.Dependencies)
        {
            if (Target.TargetAssetType is not null && !visible.Any(a => a.Guid == dependency))
                _details.Children.Add(new TextBlock { Text = dependency.ToString() });
            else
            {
                var button = new Button { Content = dependency.ToString() };
                button.Click += (_, _) => Read(dependency);
                _details.Children.Add(button);
            }
        }
    }
}
