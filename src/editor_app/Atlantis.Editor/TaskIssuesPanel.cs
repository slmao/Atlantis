using System;
using System.Linq;
using System.Collections.ObjectModel;
using System.ComponentModel;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Input.Platform;
using Avalonia.Media;
using Atlantis.Editor.Model;

namespace Atlantis.Editor;

internal sealed class TaskIssuesPanel : Border
{
    private readonly TaskIssues _tasks;
    private readonly ListBox _list = new() { MinHeight = 90 };
    private readonly TextBlock _empty = EditorVisuals.Secondary("No operations in this session.");
    private readonly SelectableTextBlock _details = EditorVisuals.Details("");
    private readonly TextBlock _nextStep = EditorVisuals.Secondary("");
    private readonly TextBlock _notice = EditorVisuals.Secondary("");
    private readonly Button _locate = new() { Content = "Locate" };
    private readonly Button _copy = new() { Content = "Copy diagnostics" };
    private readonly Func<TaskIssue, string?> _locateTarget;
    private readonly Func<TaskIssue, bool> _canLocate;
    private bool _updating;
    private readonly ObservableCollection<TaskIssueItem> _items = new();

    public TaskIssuesPanel(TaskIssues tasks, Func<TaskIssue, bool> canLocate, Func<TaskIssue, string?> locateTarget)
    {
        _tasks = tasks;
        _canLocate = canLocate;
        _locateTarget = locateTarget;
        IsVisible = false;
        Background = EditorVisuals.Pane;
        BorderBrush = EditorVisuals.Line;
        BorderThickness = new Thickness(0, 1, 0, 0);
        Padding = new Thickness(9, 5);

        AutomationProperties.SetName(_list, "Session operations");
        AutomationProperties.SetName(_details, "Selected operation diagnostics");
        AutomationProperties.SetName(_locate, "Locate operation target");
        _list.ItemsSource = _items;
        _list.ItemTemplate = new FuncDataTemplate<TaskIssueItem>((row, _) =>
            row is null ? null : EditorVisuals.TaskIssueLine(), supportsRecycling: false);
        _list.SelectionChanged += (_, _) =>
        {
            if (_updating) return;
            _notice.Text = "";
            DisplaySelection();
        };
        _locate.Click += (_, _) =>
        {
            if (_list.SelectedItem is TaskIssueItem { Row: { } row } && _tasks.Find(row.Sequence) is { } current)
                _notice.Text = _locateTarget(current) ?? "";
        };
        _copy.Click += async (_, _) =>
        {
            if (_list.SelectedItem is not TaskIssueItem { Row: { } row } || _tasks.Find(row.Sequence) is not { } current) return;
            try
            {
                var clipboard = TopLevel.GetTopLevel(this)?.Clipboard ?? throw new InvalidOperationException();
                await clipboard.SetTextAsync(current.Details(_tasks.ActivationKnown, _tasks.ActiveGeneration));
                if (_list.SelectedItem is TaskIssueItem { Row: { } selected } && selected.Sequence == current.Sequence)
                    _notice.Text = "Diagnostics copied.";
            }
            catch (Exception)
            {
                if (_list.SelectedItem is TaskIssueItem { Row: { } selected } && selected.Sequence == current.Sequence)
                    _notice.Text = "Could not copy diagnostics.";
            }
        };

        var detailsScroll = EditorVisuals.Scroll(_details);
        detailsScroll.MaxHeight = 105;
        var details = EditorVisuals.Stack(4, _nextStep, detailsScroll, EditorVisuals.Actions(_locate, _copy), _notice);
        details.Margin = new Thickness(12, 0, 0, 0);
        var left = new Panel();
        left.Children.Add(_list);
        left.Children.Add(_empty);
        _list.MaxHeight = 174;
        Child = EditorVisuals.Stack(4, EditorVisuals.Heading("Tasks & Issues"), EditorVisuals.Columns("*,*", left, details));
        _tasks.Changed += Refresh;
        Refresh();
    }

    public void Refresh()
    {
        if (!IsVisible) return;
        var selected = _list.SelectedItem as TaskIssueItem;
        var rows = _tasks.Rows;
        _updating = true;
        for (int index = _items.Count - 1; index >= 0; --index)
            if (_tasks.Find(_items[index].Row.Sequence) is null) _items.RemoveAt(index);
        for (int index = 0; index < rows.Count; ++index)
        {
            var row = rows[index];
            if (index == _items.Count || _items[index].Row.Sequence != row.Sequence)
                _items.Insert(index, new TaskIssueItem(row));
            _items[index].Update(row, _tasks.ActivationKnown, _tasks.ActiveGeneration);
        }
        if (selected is null || !_items.Contains(selected))
        {
            _notice.Text = "";
            _list.SelectedItem = _items.FirstOrDefault();
        }
        _updating = false;
        _empty.IsVisible = rows.Count == 0;
        _list.IsVisible = rows.Count != 0;
        DisplaySelection();
    }

    private void DisplaySelection()
    {
        var row = (_list.SelectedItem as TaskIssueItem)?.Row;
        _nextStep.Text = row?.NextStep(_tasks.ActivationKnown, _tasks.ActiveGeneration) ?? "";
        _details.Text = row?.Details(_tasks.ActivationKnown, _tasks.ActiveGeneration, includeGuidance: false) ?? "";
        _locate.IsEnabled = row is not null && _canLocate(row);
        _copy.IsEnabled = row is not null;
    }
}

internal sealed class TaskIssueItem(TaskIssue row) : INotifyPropertyChanged
{
    public TaskIssue Row { get; private set; } = row;
    public string Label => Row.Operation + " · " + Row.Target;
    public string Outcome { get; private set; } = "";
    public IBrush Tone { get; private set; } = EditorVisuals.Muted;
    public event PropertyChangedEventHandler? PropertyChanged;

    public void Update(TaskIssue current, bool activationKnown, Atlantis.Gameplay.Assets.GenerationId? active)
    {
        Row = current;
        string outcome = current.Summary(activationKnown, active);
        IBrush tone = current.Result switch
        {
            TaskResult.Failed or TaskResult.Refused => EditorVisuals.Error,
            TaskResult.Unknown or TaskResult.Superseded => EditorVisuals.Warning,
            TaskResult.Succeeded when current.Published is not null && (!activationKnown || current.Published != active) => EditorVisuals.Warning,
            TaskResult.Succeeded => EditorVisuals.Success,
            _ => EditorVisuals.Muted,
        };
        if (Outcome == outcome && ReferenceEquals(Tone, tone)) return;
        Outcome = outcome;
        Tone = tone;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Outcome)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Tone)));
    }
}
