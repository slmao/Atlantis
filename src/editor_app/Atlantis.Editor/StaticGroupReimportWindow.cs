using System;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;

namespace Atlantis.Editor;

internal sealed class StaticGroupReimportWindow : Window
{
    private readonly StaticGroupIntent _intent;
    private readonly Action<StaticGroupReviewMode, AssetGuid, Action<Outcome<StaticGroupReview, RebuildError>?, string?>> _read;
    private readonly DispatcherTimer _poll = new() { Interval = TimeSpan.FromMilliseconds(300) };
    private readonly TextBlock _status = EditorVisuals.Secondary("Reading original and updated glTF…");
    private readonly ListBox _rows = new();
    private readonly Button _confirm = new() { Content = "Confirm reimport", IsEnabled = false };
    private StaticGroupReview? _review;
    private bool _pending, _closed, _confirmed;

    public StaticGroupReimportWindow(string name, StaticGroupIntent intent,
        Action<StaticGroupReviewMode, AssetGuid, Action<Outcome<StaticGroupReview, RebuildError>?, string?>> read,
        Action<StaticGroupReview> confirm)
    {
        _intent = intent;
        _read = read;
        Title = "Review model update";
        Width = 760;
        Height = 530;
        MinWidth = 440;
        MinHeight = 340;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        _status.TextWrapping = Avalonia.Media.TextWrapping.Wrap;
        var cancel = new Button { Content = "Cancel", IsCancel = true };
        cancel.Click += (_, _) => Close();
        _confirm.Click += (_, _) =>
        {
            if (!_intent.Current || _review is not { State: StaticGroupReviewState.Ready } ready) return;
            _confirmed = true;
            confirm(ready);
            Close();
        };
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8,
            HorizontalAlignment = HorizontalAlignment.Right, Children = { cancel, _confirm } };
        var body = new Grid { Margin = new Thickness(16), RowSpacing = 12, RowDefinitions = new("Auto,*,Auto") };
        body.Children.Add(EditorVisuals.Stack(6, EditorVisuals.Heading(name, 18), _status));
        Grid.SetRow(_rows, 1);
        body.Children.Add(_rows);
        Grid.SetRow(buttons, 2);
        body.Children.Add(buttons);
        Content = body;
        Opened += (_, _) => Request(StaticGroupReviewMode.Begin, _intent.Scene);
        Closed += (_, _) =>
        {
            _closed = true;
            _poll.Stop();
            if (!_confirmed)
            {
                _intent.Cancel();
                if (_review is { } review) _read(StaticGroupReviewMode.Cancel, review.Id, (_, _) => { });
            }
        };
        _poll.Tick += (_, _) =>
        {
            if (_review is { State: StaticGroupReviewState.Pending } pending)
                Request(StaticGroupReviewMode.Read, pending.Id);
        };
    }

    private void Request(StaticGroupReviewMode mode, AssetGuid target)
    {
        if (_pending) return;
        _pending = true;
        _read(mode, target, (outcome, diagnostic) =>
        {
            _pending = false;
            if (_closed || !_intent.Current)
            {
                if (mode == StaticGroupReviewMode.Begin && outcome is { IsOk: true, Value: { } abandoned })
                    _read(StaticGroupReviewMode.Cancel, abandoned.Id, (_, _) => { });
                _confirm.IsEnabled = false;
                _poll.Stop();
                if (!_closed) _status.Text = "The selected asset or catalog changed. Close and start a fresh review.";
                return;
            }
            if (outcome is not { IsOk: true, Value: { } review } || review.Scene != _intent.Scene ||
                (_review is { } before && (before.Id != review.Id || before.Generation != review.Generation)))
            {
                _poll.Stop();
                _status.Text = diagnostic ?? (outcome?.Error == RebuildError.Unsupported
                    ? "The connected Runtime does not support model reimport."
                    : $"Review unavailable ({outcome?.Error}). Close and check the active generation.");
                return;
            }
            _review = review;
            _confirm.IsEnabled = review.State == StaticGroupReviewState.Ready;
            if (review.State == StaticGroupReviewState.Pending) _poll.Start(); else _poll.Stop();
            int matched = review.Rows.Count(row => row.Change == StaticGroupChange.Matched);
            int added = review.Rows.Count(row => row.Change == StaticGroupChange.Added);
            int removed = review.Rows.Count(row => row.Change == StaticGroupChange.Removed);
            int conflicts = review.Rows.Count(row => row.Change == StaticGroupChange.Conflict);
            string count = review.Rows.Count == 0 ? "" :
                $" {matched} matched, {added} added, {removed} removed, {conflicts} conflicts.";
            _status.Text = review.State == StaticGroupReviewState.Ready
                ? $"Generation {review.Generation}.{count} Activation requires an explicit Runtime restart."
                : $"{review.State}.{count} {review.Diagnostic}";
            _rows.ItemsSource = review.Rows.Select(row =>
            {
                var text = new TextBlock { TextWrapping = Avalonia.Media.TextWrapping.Wrap,
                    Text = $"{row.Change} · {row.Key} · {row.Guid}\n{Describe(row.Before)} → {Describe(row.After)}" };
                return EditorVisuals.Stack(3, text, EditorVisuals.Secondary(row.Diagnostic));
            }).ToArray();
        });
    }

    private static string Describe(StaticGroupRevisionRow? revision) => revision is null ? "Absent" :
        revision.Slot is uint slot ? $"{revision.Label} [{revision.Index}], slot {slot}, material " +
            (revision.Material?.ToString() ?? "unassigned") : $"{revision.Label} [{revision.Index}]";
}
