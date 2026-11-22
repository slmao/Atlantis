using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Editor.Model;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Media;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private readonly Button _batchRename = new() { Content = "Batch Rename…" };
    private BatchRenameWindow? _batchRenameWindow;

    private string? BatchRenameRefusal() =>
        GroupEditRefusal() ?? (_worker is null || _referenceSessionFailed
            ? "Connect to a hand-authored scene."
            : !_worker.BatchRenameAvailable ? "The open document does not support guarded batch rename."
            : _selection.Nodes.Count < 2 ? "Select at least two nodes in the Hierarchy." : null);

    private void UpdateBatchRename()
    {
        string? reason = BatchRenameRefusal();
        _batchRename.IsEnabled = reason is null && _batchRenameWindow is null;
        ToolTip.SetTip(_batchRename, reason);
    }

    private async void OpenBatchRename()
    {
        if (_batchRenameWindow is not null || BatchRenameRefusal() is not null || _worker is not { } worker) return;
        var dialog = new BatchRenameWindow();
        _batchRenameWindow = dialog;
        BatchRenameIntent? intent = null;
        UpdateBatchRename();

        bool SameSession() => ReferenceEquals(worker, _worker) && !_referenceSessionFailed;
        void Refresh()
        {
            intent?.Cancel();
            int hidden = _selection.Nodes.Count(node => !_hierarchyProjection.Visible(node));
            if (!SameSession() || BatchRenameRefusal() is not null)
            {
                dialog.ShowPreview(null, "The scene or edit context changed. Check the selection and retry.");
                return;
            }
            if (hidden > 0)
            {
                dialog.RequireReveal(hidden);
                return;
            }
            var request = new BatchRenameIntent(worker.MeshDocumentLifetime, _selection.Generation, _selection.Nodes);
            intent = request;
            dialog.ShowPreview(null, "Reading the current names…");
            worker.PreviewBatchRename(request, (preview, reason) =>
            {
                if (ReferenceEquals(_batchRenameWindow, dialog) && ReferenceEquals(intent, request) && request.Current)
                {
                    bool current = SameSession() && _selection.Generation == request.SelectionGeneration &&
                        worker.MeshDocumentLifetime == request.Lifetime;
                    dialog.ShowPreview(current ? preview : null,
                        current ? reason : "The selection or scene changed. Refresh the preview.");
                }
            });
        }

        dialog.RefreshRequested += Refresh;
        dialog.RevealRequested += () =>
        {
            if (!SameSession() || BatchRenameRefusal() is not null) return;
            LocateHierarchySelection();
            Refresh();
        };
        dialog.Confirmed += (preview, rows) =>
        {
            if (!SameSession() || BatchRenameRefusal() is not null || HasHiddenSelection() ||
                intent is not { Current: true } request || _selection.Generation != request.SelectionGeneration)
            {
                dialog.ShowPreview(null, "The selection or edit context changed. Refresh the preview.");
                return;
            }
            dialog.Settle(true);
            if (worker.CommitBatchRename(request, preview, rows, result =>
            {
                if (!ReferenceEquals(_batchRenameWindow, dialog)) return;
                dialog.Settle(false);
                if (result.State == BatchReplayState.Committed) dialog.Close();
                else dialog.ShowPreview(null, result.Notice);
            })) return;
            dialog.Settle(false);
            dialog.ShowPreview(null, "Rename not sent. Refresh the selection and retry.");
        };

        try
        {
            var closed = dialog.ShowDialog(this);
            Refresh();
            await closed;
        }
        finally
        {
            intent?.Cancel();
            _batchRenameWindow = null;
            UpdateBatchRename();
            if (_batchRename.IsEnabled) _batchRename.Focus();
        }
    }
}

internal sealed class BatchRenameWindow : Window
{
    private readonly TextBox _prefix = new() { PlaceholderText = "Name prefix" };
    private readonly Button _refresh = new() { Content = "Refresh preview" };
    private readonly Button _reveal = new() { Content = "Locate selection", IsVisible = false };
    private readonly Button _confirm = new() { Content = "Confirm", IsEnabled = false };
    private readonly Button _cancel = new() { Content = "Cancel" };
    private readonly TextBlock _notice = EditorVisuals.Secondary("");
    private readonly ListBox _rows = new()
    {
        ItemTemplate = new FuncDataTemplate<BatchRenameRow>((row, _) =>
            new TextBlock { Text = row?.ToString(), TextWrapping = TextWrapping.Wrap }),
    };
    private BatchRenamePreview? _preview;
    private IReadOnlyList<BatchRenameRow>? _proposed;
    private bool _busy;

    public event Action? RefreshRequested;
    public event Action? RevealRequested;
    public event Action<BatchRenamePreview, IReadOnlyList<BatchRenameRow>>? Confirmed;

    public BatchRenameWindow()
    {
        Title = "Batch Rename";
        Width = 700;
        Height = 540;
        MinWidth = 460;
        MinHeight = 350;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        _notice.TextWrapping = TextWrapping.Wrap;
        var layout = new Grid { Margin = new Thickness(16), RowSpacing = 12, RowDefinitions = new("Auto,*,Auto") };
        layout.Children.Add(EditorVisuals.Stack(6, EditorVisuals.Secondary("Name prefix"), _prefix,
            EditorVisuals.Actions(_refresh, _reveal), _notice));
        Grid.SetRow(_rows, 1);
        layout.Children.Add(_rows);
        var actions = EditorVisuals.Actions(_confirm, _cancel);
        Grid.SetRow(actions, 2);
        layout.Children.Add(actions);
        Content = layout;

        Opened += (_, _) => _prefix.Focus();
        _refresh.Click += (_, _) => RefreshRequested?.Invoke();
        _reveal.Click += (_, _) => RevealRequested?.Invoke();
        _prefix.TextChanged += (_, _) => UpdateRows();
        _confirm.Click += (_, _) =>
        {
            if (!_busy && _confirm.IsEnabled && _preview is { } preview && _proposed is { } rows)
                Confirmed?.Invoke(preview, rows);
        };
        _cancel.Click += (_, _) => Close();
        KeyDown += (_, e) =>
        {
            if (e.Key == Key.Escape && !_busy) { e.Handled = true; Close(); }
        };
        Closing += (_, e) => { if (_busy) e.Cancel = true; };
    }

    public void ShowPreview(BatchRenamePreview? preview, string notice)
    {
        _preview = preview;
        _reveal.IsVisible = false;
        _notice.Text = notice;
        UpdateRows();
    }

    public void RequireReveal(int hidden)
    {
        ShowPreview(null, $"{hidden} selected {(hidden == 1 ? "node is" : "nodes are")} hidden. Locate selection before reviewing the names.");
        _reveal.IsVisible = true;
        _reveal.Focus();
    }

    public void Settle(bool busy)
    {
        _busy = busy;
        _prefix.IsEnabled = _refresh.IsEnabled = _reveal.IsEnabled = _cancel.IsEnabled = !busy;
        UpdateRows();
        if (busy) _notice.Text = "Settling batch rename…";
    }

    private void UpdateRows()
    {
        _proposed = _preview?.Propose(_prefix.Text);
        _rows.ItemsSource = _proposed;
        _confirm.IsEnabled = !_busy && _proposed?.Any(row => row.Changed) == true;
        if (_preview is not null && !_busy)
            _notice.Text = _proposed is null ? "Enter a valid prefix. Each complete name can use up to 255 UTF-8 bytes."
                : _confirm.IsEnabled ? $"Review {_proposed.Count} old and new names before confirming."
                : "All proposed names are unchanged.";
    }
}
