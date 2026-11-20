using System;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay.Authoring;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;

namespace Atlantis.Editor;

internal enum RecoveryChoice { Later, Restore, Discard }

internal sealed class SceneRecoveryWindow : Window
{
    private readonly Func<Task<RecoveryObservation>> _preview;
    private readonly TextBlock _notice = EditorVisuals.Secondary("Preview the copy to check the current source.");
    private readonly TextBox _before = new() { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MaxHeight = 136 };
    private readonly TextBox _after = new() { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MaxHeight = 136 };
    private readonly StackPanel _comparison;
    private readonly Button _load = new() { Content = "Preview" };
    private readonly Button _restore = new() { Content = "Restore to Document", IsEnabled = false };
    private readonly Button _discard = new() { Content = "Discard Recovery Copy" };
    private readonly Button _later = new() { Content = "Decide Later", IsDefault = true };
    private bool _confirmDiscard, _closed;

    public SceneRecoveryWindow(DocumentState document, RecoveryCopy copy, Func<Task<RecoveryObservation>> preview)
    {
        _preview = preview;
        Title = "Scene Recovery";
        Background = EditorVisuals.Canvas;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Width = 645;
        MinWidth = 400;
        MaxHeight = 650;
        SizeToContent = SizeToContent.Height;
        var captured = DateTimeOffset.FromUnixTimeSeconds(checked((long)copy.CapturedUnixSeconds)).ToLocalTime();
        var summary = EditorVisuals.Secondary($"{document.Source}\nRecovery revision {copy.Revision} · {captured:g}");
        _comparison = EditorVisuals.Stack(4, EditorVisuals.Heading("Current source"), _before,
            EditorVisuals.Heading("Recovery copy"), _after);
        _comparison.IsVisible = false;
        var body = EditorVisuals.Stack(10, EditorVisuals.Heading("Unsaved scene edits are available", 16), summary,
            _notice, _comparison, EditorVisuals.Actions(_load, _restore, _discard, _later));
        body.Margin = new Thickness(16);
        Content = EditorVisuals.Scroll(body);
        Opened += (_, _) => _later.Focus();
        Closed += (_, _) => _closed = true;
        _load.Click += async (_, _) => await LoadPreview();
        _later.Click += (_, _) => Close(RecoveryChoice.Later);
        _restore.Click += (_, _) => { if (_restore.IsEnabled) Close(RecoveryChoice.Restore); };
        _discard.Click += (_, _) =>
        {
            if (!_discard.IsEnabled) return;
            if (_confirmDiscard) { Close(RecoveryChoice.Discard); return; }
            _confirmDiscard = true;
            _discard.Content = "Confirm Discard";
            _restore.IsEnabled = false;
            _notice.Text = "Discard the reviewed recovery copy? The official source and current document will stay as they are.";
            _later.Focus();
        };
        AddHandler(KeyDownEvent, (_, e) =>
        {
            if (e.Key != Key.Escape) return;
            e.Handled = true;
            Close(RecoveryChoice.Later);
        }, RoutingStrategies.Tunnel);
    }

    private async Task LoadPreview()
    {
        _confirmDiscard = false;
        _discard.Content = "Discard Recovery Copy";
        _load.IsEnabled = _restore.IsEnabled = _discard.IsEnabled = false;
        _notice.Text = "Checking the recovery copy and current source…";
        var result = await _preview();
        if (_closed) return;
        _load.IsEnabled = true;
        if (result.Status?.Preview is not { } view)
        {
            _comparison.IsVisible = false;
            _notice.Text = result.Diagnostic ?? $"Recovery preview unavailable ({result.Refusal}).";
            _discard.IsEnabled = result.Refusal == RecoveryError.InvalidCopy;
            return;
        }
        _discard.IsEnabled = true;
        _restore.IsEnabled = view.Source == RecoverySource.Matches;
        _notice.Text = (view.Source switch
        {
            RecoverySource.Matches => "The source matches the recovery base. Restore edits the document. Apply and Save stay explicit.",
            RecoverySource.Changed => "The official source changed. Restore is unavailable. You can keep or discard the copy.",
            RecoverySource.Unreadable => "The official source could not be read or validated. Restore is unavailable.",
            RecoverySource.AlreadySaved => "The official source already contains this copy. You can discard it.",
            RecoverySource.DirtyDocument => "The open document has unsaved edits. Save or reopen before restoring.",
            _ => "",
        }) + (view.Truncated ? " The preview is truncated." : "");
        _before.Text = Excerpt(view.SourceNodes, view.SourceLines);
        _after.Text = Excerpt(view.CopyNodes, view.CopyLines);
        _comparison.IsVisible = true;
    }

    private static string Excerpt(ulong? nodes, System.Collections.Generic.IReadOnlyList<string> lines) =>
        (nodes is ulong count ? $"{count} nodes" : "Source unavailable") +
        (lines.Count == 0 ? "" : "\n" + string.Join("\n", lines));
}
