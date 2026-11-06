using System;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Avalonia;
using Avalonia.Controls;
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
    private readonly TextBlock _current = new();
    private readonly Button _confirm = new() { Content = "Confirm", IsEnabled = false };
    private bool _sync, _closed, _invalid, _submitted, _initial = true;
    public AssetReferenceTarget Target { get; }

    public AssetReferencePickerWindow(EditorWorker worker, AssetReferenceTarget target, Action<AssetGuid> confirm, Action cancel)
    {
        _worker = worker; Target = target;
        Title = "Choose asset — " + target.Label; Width = 760; Height = 620;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        var root = new DockPanel { Margin = new Thickness(12), LastChildFill = true };
        var top = new StackPanel { Spacing = 6 };
        top.Children.Add(new TextBlock { Text = target.Path });
        _current.Text = "Current: " + (target.Before.IsAbsent ? "(absent)" : target.Before.AsAsset().ToString());
        top.Children.Add(_current);
        top.Children.Add(new TextBlock { Text = "Catalog fixed at first query; restart Runtime after a changed build or failed initial load. Reload view does not reread disk.", TextWrapping = Avalonia.Media.TextWrapping.Wrap });
        top.Children.Add(new TextBlock { Text = "Choose a GUID; catalog presence and type do not guarantee compatibility." });
        var filters = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        var types = new ComboBox { ItemsSource = new[] { "All types" }.Concat(Enum.GetNames<AssetType>()).ToList(), SelectedIndex = 0 };
        var search = new TextBox { PlaceholderText = "Source or GUID", Width = 300 };
        var reload = new Button { Content = "Reload view" };
        types.SelectionChanged += (_, _) => { _browser.Type = types.SelectedIndex <= 0 ? null : Enum.GetValues<AssetType>()[types.SelectedIndex - 1]; Render(); };
        search.TextChanged += (_, _) => { _browser.Search = search.Text ?? ""; Render(); };
        reload.Click += (_, _) => { if (!_invalid) Load(); };
        filters.Children.Add(types); filters.Children.Add(search); filters.Children.Add(reload);
        top.Children.Add(filters); top.Children.Add(_state);
        DockPanel.SetDock(top, Dock.Top); root.Children.Add(top);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Spacing = 8 };
        var back = new Button { Content = "Cancel" };
        back.Click += (_, _) => Close();
        _confirm.Click += (_, _) =>
        {
            if (!_confirm.IsEnabled || _closed || _invalid || _submitted || _browser.State != "Ready" ||
                _list.SelectedItem is not AssetSummary candidate || _browser.Selected != candidate.Guid) return;
            _submitted = true; _confirm.IsEnabled = false;
            confirm(candidate.Guid); Close();
        };
        buttons.Children.Add(_confirm); buttons.Children.Add(back);
        DockPanel.SetDock(buttons, Dock.Bottom); root.Children.Add(buttons);
        _list.ItemTemplate = new FuncDataTemplate<AssetSummary>((a, _) => new TextBlock { Text = a is null ? "" : a.Type + "  " + a.Source + "\n" + a.Guid });
        _list.SelectionChanged += (_, e) =>
        {
            if (!ReferenceEquals(e.Source, _list) || _sync || _invalid) return;
            if (_list.SelectedItem is AssetSummary a) Read(a.Guid);
            else { _browser.ClearSelection(); Render(); }
        };
        var body = new Grid { RowDefinitions = new RowDefinitions("*,Auto") };
        body.Children.Add(_list);
        var detailScroll = new ScrollViewer { Content = _details, MaxHeight = 180 };
        Grid.SetRow(detailScroll, 1); body.Children.Add(detailScroll); root.Children.Add(body);
        Content = root;
        KeyDown += (_, e) => { if (e.Key == Key.Escape) { e.Handled = true; Close(); } };
        Opened += (_, _) => Load();
        Closed += (_, _) => { _closed = true; _browser.Disconnect(); if (!_submitted) cancel(); };
    }

    public void Invalidate(string reason)
    {
        _invalid = true; _browser.Disconnect(); _state.Text = reason + "; close and reopen to choose again";
        _confirm.IsEnabled = false;
    }

    private void Load()
    {
        long request = _browser.BeginList(); Render();
        _worker.LoadAssets(result =>
        {
            if (_closed || _invalid || !_browser.CompleteList(request, result)) return;
            if (result.IsOk && _initial)
            {
                _initial = false;
                if (Target.Before.TryGetAsset(out AssetGuid current))
                {
                    if (_browser.Items.Any(a => a.Guid == current)) _browser.Select(current);
                    else _current.Text += " (outside this snapshot)";
                }
            }
            Render();
            if (result.IsOk && _browser.Selected is AssetGuid guid) Read(guid);
        });
    }

    private void Read(AssetGuid guid)
    {
        long request = _browser.Select(guid); Render();
        _worker.ReadAsset(guid, result =>
        { if (!_closed && !_invalid && _browser.CompleteDetail(request, guid, result)) Render(); });
    }

    private void Render()
    {
        if (_closed || _invalid) return;
        _state.Text = _browser.ViewState;
        var visible = _browser.Visible;
        _sync = true;
        try { _list.ItemsSource = visible; _list.SelectedItem = visible.FirstOrDefault(a => a.Guid == _browser.Selected); }
        finally { _sync = false; }
        _confirm.IsEnabled = !_submitted && _browser.State == "Ready" &&
            _list.SelectedItem is AssetSummary candidate && candidate.Guid == _browser.Selected;
        _details.Children.Clear();
        if (_browser.Details is not AssetDetails d) { _details.Children.Add(new TextBlock { Text = _browser.DetailState }); return; }
        _details.Children.Add(new TextBlock { Text = d.Type + "\n" + d.Source + "\n" + d.Guid + "\nartifact schema: " + d.ArtifactSchema + "\nsource schema: " + (d.SourceSchema?.ToString() ?? "none") + "\ntool: " + d.Tool });
        foreach (AssetGuid dependency in d.Dependencies)
        { var b = new Button { Content = dependency.ToString() }; b.Click += (_, _) => Read(dependency); _details.Children.Add(b); }
    }
}
