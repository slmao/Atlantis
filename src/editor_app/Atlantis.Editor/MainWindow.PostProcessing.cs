using System;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Media;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private readonly Button _postProcessingMode = new() { Content = "Post Processing", Focusable = false };
    private readonly TextBlock _postProcessingTarget = EditorVisuals.Heading("Active scene camera", 15);
    private readonly TextBlock _postProcessingGuid = EditorVisuals.Details("");
    private readonly TextBlock _postProcessingNotice = EditorVisuals.Secondary("");
    private readonly StackPanel _postProcessingRows = new() { Spacing = 9, Margin = new Thickness(12, 4, 12, 12) };
    private Control? _selectionInspector, _postProcessingInspector;
    private bool _postProcessingVisible;
    private PostProcessingView? _postProcessingView;
    private PostProcessingDraft? _postProcessingDraft, _postProcessingStale, _postProcessingPending;
    private TextBox? _postProcessingDraftBox;

    private Control BuildInspectorModes()
    {
        _selectionInspector = EditorVisuals.Scroll(_inspectorPanel);
        var copyGuid = new Button { Content = "Copy GUID", Focusable = false };
        copyGuid.Click += async (_, _) =>
        {
            if (_postProcessingView is { } view && Clipboard is { } clipboard)
                await clipboard.SetTextAsync(view.Camera.ToString());
        };
        var refresh = new Button { Content = "Refresh", Focusable = false };
        refresh.Click += (_, _) =>
        {
            if (_postProcessingDraft is not null || _postProcessingPending is not null)
            {
                _postProcessingNotice.Text = "Finish or cancel the field edit before refreshing.";
                return;
            }
            _worker?.ShowPostProcessing(true);
        };
        _postProcessingTarget.TextWrapping = _postProcessingGuid.TextWrapping = _postProcessingNotice.TextWrapping = TextWrapping.Wrap;
        var header = EditorVisuals.Stack(4, EditorVisuals.Secondary("Active scene camera"), _postProcessingTarget,
            _postProcessingGuid, EditorVisuals.Actions(refresh, copyGuid), _postProcessingNotice);
        header.Margin = new Thickness(12, 8, 12, 4);
        var pane = new DockPanel { IsVisible = false };
        DockPanel.SetDock(header, Dock.Top);
        pane.Children.Add(header);
        pane.Children.Add(EditorVisuals.Scroll(_postProcessingRows));
        _postProcessingInspector = pane;
        _postProcessingMode.Click += (_, _) => TogglePostProcessing();
        _postProcessingMode.Margin = new Thickness(12, 8, 12, 0);
        DockPanel.SetDock(_postProcessingMode, Dock.Top);
        return new DockPanel { Children = { _postProcessingMode, new Grid { Children = { _selectionInspector, pane } } } };
    }

    private void TogglePostProcessing()
    {
        if (_leaveInProgress || _shuttingDown) return;
        if (CurrentSceneInput() is not null || _pendingInspectorBlur is not null || _pendingSceneFieldEdits != 0 ||
            _postProcessingPending is not null)
        {
            var notice = "Finish or cancel the current field edit before switching Inspector modes.";
            if (_postProcessingVisible) _postProcessingNotice.Text = notice;
            else _editLine.Text = notice;
            return;
        }
        _postProcessingVisible = !_postProcessingVisible;
        _selectionInspector!.IsVisible = !_postProcessingVisible;
        _postProcessingInspector!.IsVisible = _postProcessingVisible;
        _postProcessingMode.Content = _postProcessingVisible ? "Selection Inspector" : "Post Processing";
        _worker?.ShowPostProcessing(_postProcessingVisible);
        ShowPostProcessing(null, _worker is null ? "No authoring session." : "Reading the active scene camera…");
    }

    private void ShowPostProcessing(PostProcessingView? view, string notice)
    {
        if (!ReferenceEquals(view, _postProcessingView)) RetirePostProcessingDraft();
        _postProcessingView = view;
        _postProcessingTarget.Text = view?.Label ?? "Camera unavailable";
        _postProcessingGuid.Text = view?.Camera.ToString() ?? "";
        _postProcessingGuid.IsVisible = view is not null;
        _postProcessingNotice.Text = notice;
        BuildPostProcessingFields();
        if (_postProcessingVisible) ShowDocumentState(_documentState);
    }

    private void RetirePostProcessingDraft()
    {
        if (_postProcessingDraft is not { } draft) return;
        _postProcessingStale = draft;
        if (_leaveInput?.Box == _postProcessingDraftBox)
        {
            _leaveInputStale = true;
            _leaveDialog?.ShowDraft(LeaveDraftSummary());
        }
        _postProcessingDraft = null;
        _postProcessingDraftBox = null;
    }

    private SceneInput? PostProcessingInput()
    {
        if (_postProcessingDraft is not { } draft || _postProcessingDraftBox is not { } box) return null;
        return new(box, draft.LeaveDraft, box.CaretIndex, box.SelectionStart, box.SelectionEnd);
    }

    private void ClearPostProcessingInput()
    {
        _postProcessingDraft = _postProcessingStale = _postProcessingPending = null;
        _postProcessingDraftBox = null;
        BuildPostProcessingFields();
    }

    private void BuildPostProcessingFields()
    {
        _postProcessingRows.Children.Clear();
        if (_postProcessingStale is { } stale)
        {
            var copy = new Button { Content = "Copy and dismiss", Focusable = false };
            var discard = new Button { Content = "Discard stale input", Focusable = false };
            void Dismiss()
            {
                if (!ReferenceEquals(_postProcessingStale, stale)) return;
                _postProcessingStale = null;
                BuildPostProcessingFields();
                ShowDocumentState(_documentState);
            }
            copy.Click += async (_, _) =>
            {
                if (Clipboard is not { } clipboard) return;
                await clipboard.SetTextAsync(stale.Text);
                Dismiss();
            };
            discard.Click += (_, _) => Dismiss();
            var context = EditorVisuals.Secondary($"{stale.View.Label} · {stale.View.Camera}\n{stale.Input.Field.Path}");
            context.TextWrapping = TextWrapping.Wrap;
            _postProcessingRows.Children.Add(EditorVisuals.Group(EditorVisuals.Heading("Stale input", 13), EditorVisuals.Stack(4, context,
                new TextBox { Text = stale.Text, IsReadOnly = true }, EditorVisuals.Actions(copy, discard))));
        }
        if (_postProcessingView is not { } view) return;
        foreach (var group in view.Fields.GroupBy(field => field.Group))
        {
            var fields = EditorVisuals.Stack(6);
            foreach (var input in group) fields.Children.Add(BuildPostProcessingField(view, input));
            _postProcessingRows.Children.Add(EditorVisuals.Group(EditorVisuals.Heading(group.Key, 14), fields));
        }
    }

    private Control BuildPostProcessingField(PostProcessingView view, PostProcessingField input)
    {
        var field = input.Field;
        string label = field.Path == "Camera.exposureCompensationEv" ? "Compensation (EV)" :
            field.Label[(field.Label.LastIndexOf('.') + 1)..];
        var box = new TextBox
        {
            Text = field.Text,
            MinWidth = 0,
            IsReadOnly = !view.Writable || !field.Editable || !field.Present || _postProcessingStale is not null,
            PlaceholderText = field.Present ? null : "Unavailable"
        };
        AutomationProperties.SetName(box, field.Path);
        var set = new Button { Content = "Set", Focusable = false, IsEnabled = false };
        AutomationProperties.SetName(set, "Set " + field.Path);
        void Reset()
        {
            if (_postProcessingDraftBox != box) return;
            _postProcessingDraft = null;
            _postProcessingDraftBox = null;
            box.Text = field.Text;
            ShowDocumentState(_documentState);
        }
        box.PropertyChanged += (_, change) =>
        {
            if (change.Property != TextBox.TextProperty || box.IsReadOnly || !ReferenceEquals(view, _postProcessingView) ||
                _leaveInProgress || _postProcessingPending?.View == view) return;
            if (_postProcessingDraftBox is { } other && other != box)
            {
                if (box.Text != field.Text) box.Text = field.Text;
                _postProcessingNotice.Text = "Finish or cancel the other field edit first.";
                other.Focus();
                return;
            }
            string text = box.Text ?? "";
            _postProcessingDraft = text == field.Text ? null : new(view, input, text);
            _postProcessingDraftBox = _postProcessingDraft is not null ? box : null;
            set.IsEnabled = _postProcessingDraft is not null;
            ShowDocumentState(_documentState);
        };
        void Submit()
        {
            if (_postProcessingDraft is not { } draft || _postProcessingDraftBox != box ||
                !draft.CanSubmit(_postProcessingView) || _postProcessingPending is not null || _leaveInProgress ||
                _worker is not { } worker) return;
            if (!InspectorModel.TryParse(field, draft.Text, out PropertyValue value, out string error))
            {
                _postProcessingNotice.Text = error;
                return;
            }
            if (value == input.Target.Value) { Reset(); return; }
            if (!worker.CommitPostProcessing(draft, value, result => SettlePostProcessing(worker, draft, result)))
            {
                _postProcessingNotice.Text = "The camera or edit changed. Refresh before retrying.";
                return;
            }
            _postProcessingPending = draft;
            box.IsReadOnly = true;
            set.IsEnabled = false;
            _postProcessingNotice.Text = "Committing " + field.Path + "…";
        }
        set.Click += (_, _) => Submit();
        box.KeyDown += (_, e) =>
        {
            if (_postProcessingPending?.View == view || _postProcessingDraftBox != box) return;
            if (e.Key == Key.Escape) { Reset(); e.Handled = true; }
            else if (e.Key == Key.Enter) { Submit(); e.Handled = true; }
        };
        var row = EditorVisuals.Columns("*,*,Auto", EditorVisuals.Secondary(label), box, set);
        row.Tag = field.Path;
        return row;
    }

    private void SettlePostProcessing(EditorWorker worker, PostProcessingDraft draft, BatchReplayResult result)
    {
        if (!ReferenceEquals(_worker, worker) || !ReferenceEquals(_postProcessingPending, draft)) return;
        _postProcessingPending = null;
        bool current = ReferenceEquals(_postProcessingView, draft.View);
        if (result.State == BatchReplayState.Committed)
        {
            if (ReferenceEquals(_postProcessingDraft, draft))
            {
                _postProcessingDraft = null;
                _postProcessingDraftBox = null;
            }
            if (ReferenceEquals(_postProcessingStale, draft))
            {
                _postProcessingStale = null;
                if (_postProcessingDraft is null) BuildPostProcessingFields();
            }
        }
        else if (ReferenceEquals(_postProcessingDraft, draft))
        {
            RetirePostProcessingDraft();
            BuildPostProcessingFields();
        }
        if (current)
        {
            _postProcessingNotice.Text = result.Notice;
            if (result.State != BatchReplayState.Committed) worker.ShowPostProcessing(true);
        }
        if (current && _postProcessingVisible && _postProcessingDraft is null) _editLine.Text = result.Notice;
        _log.Write("post processing: " + result.Notice);
        ShowDocumentState(_documentState);
    }
}
