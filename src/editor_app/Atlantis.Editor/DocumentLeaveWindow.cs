using System;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;

namespace Atlantis.Editor;

internal sealed class DocumentLeaveWindow : Window
{
    private readonly TextBlock _summary = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _notice = new() { TextWrapping = TextWrapping.Wrap, Foreground = EditorVisuals.Warning };
    private readonly TextBox _draft = new() { IsReadOnly = true, TextWrapping = TextWrapping.Wrap, MaxHeight = 84 };
    private readonly CheckBox _dropDraft = new() { Content = "Discard the unsubmitted input when continuing" };
    private readonly Button _save = new();
    private readonly Button _discard = new();
    private readonly Button _cancel = new() { Content = "Cancel", IsDefault = true };
    private readonly Button _check = new() { Content = "Check Again" };
    private TaskCompletionSource<LeaveChoice>? _choice;
    private bool _saveAvailable;
    private LeaveChoice _discardChoice;
    private bool _finish;

    public DocumentLeaveWindow(bool reopen)
    {
        Title = reopen ? "Reopen from Source" : "Close Editor";
        Background = EditorVisuals.Canvas;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Width = 510;
        MinWidth = 340;
        MaxHeight = 520;
        SizeToContent = SizeToContent.Height;
        _save.Content = reopen ? "Save and Reopen" : "Save and Close";
        var body = EditorVisuals.Stack(10, EditorVisuals.Heading(Title, 16), _summary, _draft, _dropDraft,
            _notice, EditorVisuals.Actions(_save, _discard, _check, _cancel));
        body.Margin = new Thickness(16);
        Content = EditorVisuals.Scroll(body);
        _save.Click += (_, _) => Choose(LeaveChoice.Save);
        _discard.Click += (_, _) => Choose(_discardChoice);
        _check.Click += (_, _) => Choose(LeaveChoice.Check);
        _cancel.Click += (_, _) => Choose(LeaveChoice.Cancel);
        _dropDraft.IsCheckedChanged += (_, _) => UpdateSave();
        AddHandler(KeyDownEvent, (_, e) =>
        {
            if (e.Key != Key.Escape) return;
            e.Handled = true;
            Choose(LeaveChoice.Cancel);
        }, RoutingStrategies.Tunnel);
        Closing += (_, e) =>
        {
            if (_finish) return;
            e.Cancel = true;
            Choose(LeaveChoice.Cancel);
        };
    }

    public Task<LeaveChoice> Ask(string summary, string? notice, string? draft, bool canSave, bool canDiscard,
        bool canCheck, bool reopen, bool closeAnyway)
    {
        _choice = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _summary.Text = summary;
        _notice.Text = notice ?? "";
        _notice.IsVisible = !string.IsNullOrEmpty(notice);
        _dropDraft.IsEnabled = true;
        _discardChoice = closeAnyway ? LeaveChoice.CloseAnyway : LeaveChoice.Discard;
        _discard.Content = closeAnyway ? "Close Anyway" : reopen ? "Reopen Without Saving" : "Close Without Saving";
        _discard.IsEnabled = _discard.IsVisible = canDiscard || closeAnyway;
        _check.IsEnabled = _check.IsVisible = canCheck;
        _save.IsVisible = canSave;
        _saveAvailable = canSave;
        ShowDraft(draft);
        _cancel.IsEnabled = true;
        _cancel.Focus();
        return _choice.Task;
    }

    public void ShowDraft(string? draft)
    {
        _draft.Text = draft;
        _draft.IsVisible = draft is not null;
        _dropDraft.IsVisible = _saveAvailable && draft is not null;
        _dropDraft.IsChecked = false;
        UpdateSave();
    }

    private void UpdateSave() => _save.IsEnabled = _saveAvailable && (!_dropDraft.IsVisible || _dropDraft.IsChecked == true);

    private void Choose(LeaveChoice choice)
    {
        if (_choice is not { } pending) return;
        _choice = null;
        _saveAvailable = false;
        _save.IsEnabled = _cancel.IsEnabled = _discard.IsEnabled = _check.IsEnabled = _dropDraft.IsEnabled = false;
        if (choice != LeaveChoice.Cancel)
        {
            _notice.IsVisible = true;
            _notice.Text = "Checking document…";
        }
        pending.TrySetResult(choice);
    }

    public void Finish()
    {
        _finish = true;
        Close();
    }
}
