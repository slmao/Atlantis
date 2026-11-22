using System;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.VisualTree;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private readonly Button _hideViewportNode = new() { Content = "Hide node", Focusable = false };
    private readonly Button _hideViewportSubtree = new() { Content = "Hide subtree", Focusable = false };
    private readonly Button _lockViewportPick = new() { Content = "Lock viewport pick", Focusable = false };
    private readonly Button _isolateViewport = new() { Content = "Isolate selection", Focusable = false };
    private readonly Button _restoreViewport = new() { Content = "Restore all", Focusable = false };
    private readonly TextBlock _visibilityNotice = EditorVisuals.Secondary("");
    private ViewportVisibility? _viewportVisibility;
    private ViewportMembership? _viewportMembership;
    private bool _visibilityBusy, _visibilityUnknown, _visibilityRefreshing;

    private bool ViewportVisibilityReady => !_visibilityBusy && !_visibilityUnknown &&
        (_viewportVisibility is null || _viewportVisibility is { Owner: not ViewportOwner.Other, Presented: true });

    private bool BistroVisibilityAllowed => !_visibilityUnknown && !_visibilityBusy &&
        _viewportVisibility?.Owner is not (ViewportOwner.Caller or ViewportOwner.Other);

    private Control BuildVisibilityHierarchyActions()
    {
        _hideViewportNode.Click += (_, _) => ChangeVisibility(VisibilityAction.HideNode);
        _hideViewportSubtree.Click += (_, _) => ChangeVisibility(VisibilityAction.HideSubtree);
        _lockViewportPick.Click += (_, _) => ChangeVisibility(VisibilityAction.LockPick);
        return EditorVisuals.Actions(_hideViewportNode, _hideViewportSubtree, _lockViewportPick);
    }

    private Control BuildVisibilityToolbar()
    {
        _isolateViewport.Click += (_, _) => ChangeVisibility(_viewportVisibility?.Policy?.Isolated is null
            ? VisibilityAction.Isolate : VisibilityAction.ExitIsolation);
        _restoreViewport.Click += (_, _) =>
        {
            if (_visibilityUnknown) CheckViewportVisibility();
            else ChangeVisibility(VisibilityAction.Restore);
        };
        AutomationProperties.SetName(_restoreViewport, "Restore all viewport visibility and picking");
        UpdateVisibilityActions();
        return EditorVisuals.Actions(_isolateViewport, _restoreViewport, _visibilityNotice);
    }

    private void UpdateVisibilityActions()
    {
        var policy = _viewportVisibility?.Policy;
        bool canChange = _worker is not null && !_visibilityBusy && !_visibilityUnknown &&
            _viewportVisibility is { Owner: not ViewportOwner.Other } && _bistroView is null &&
            !_leaveInProgress && !_recoveryDecision;
        bool selected = _selection.Nodes.Count > 0 && !HasHiddenSelection();
        _hideViewportNode.IsEnabled = _hideViewportSubtree.IsEnabled = _lockViewportPick.IsEnabled = canChange && selected;
        _isolateViewport.Content = policy?.Isolated is null ? "Isolate selection" : "Exit isolation";
        _isolateViewport.IsEnabled = canChange && (selected || policy?.Isolated is not null);
        _restoreViewport.Content = _visibilityUnknown ? "Check viewport state" : "Restore all";
        _restoreViewport.IsEnabled = (canChange && policy is not null) || (_visibilityUnknown && !_visibilityRefreshing);
    }

    private void ChangeVisibility(VisibilityAction action)
    {
        if (_worker is not { } worker || _viewportVisibility?.Owner == ViewportOwner.Other ||
            _visibilityBusy || _visibilityUnknown || _bistroView is not null || _leaveInProgress || _recoveryDecision)
            return;

        var rows = _hierarchyRows.Where(row => _selection.Nodes.Contains(row.Node)).ToArray();
        if (action is not (VisibilityAction.Restore or VisibilityAction.ExitIsolation) &&
            (rows.Length != _selection.Nodes.Count || rows.Length == 0 || HasHiddenSelection()))
        {
            _visibilityNotice.Text = "Select current Hierarchy rows first.";
            return;
        }
        if (CurrentSceneInput() is not null || _batchDraft is not null || _pendingInspectorBlur is not null ||
            _pendingSceneFieldEdits != 0)
        {
            _visibilityNotice.Text = "Finish the current draft before changing the viewport.";
            return;
        }
        _visibilityBusy = true;
        RetireVisibilityInteractions();
        _visibilityNotice.Text = "Updating viewport…";
        UpdateVisibilityActions();
        worker.ChangeHostedVisibility(action, rows, _meshDocumentLifetime, _selection.Generation);
    }

    private void CheckViewportVisibility()
    {
        if (_visibilityBusy || _visibilityRefreshing || _worker is not { } worker) return;
        _visibilityRefreshing = true;
        UpdateVisibilityActions();
        worker.ProbeHostedVisibility();
    }

    private void RetireVisibilityInteractions()
    {
        ++_pickRequest;
        _pressed = null;
        if (_gizmoGrab is not null)
        {
            _worker?.CancelGizmoForLeave();
            _gizmoGrab = null;
        }
        CancelGroupGesture();
        CancelAlignment("Viewport visibility changed. Pick the faces again.");
        RetireViewportOleTarget(expireRelease: true);
    }

    private void PublishViewportVisibility(ViewportVisibility? view, bool unknown, string? notice)
    {
        bool changed = _viewportVisibility?.Stamp != view?.Stamp || _viewportVisibility?.Owner != view?.Owner ||
            _visibilityUnknown != unknown;
        _viewportVisibility = view;
        _visibilityBusy = _visibilityRefreshing = false;
        _visibilityUnknown = unknown;
        if (changed)
        {
            RetireVisibilityInteractions();
            _viewportMembership = view?.Policy is { } policy ? new ViewportMembership(policy) : null;
            foreach (var label in _entities.GetVisualDescendants().OfType<TextBlock>())
            {
                if (label.DataContext is not HierarchyRow row) continue;
                label.Text = ViewportHierarchyLabel(row);
                ToolTip.SetTip(label, label.Text);
            }
        }
        _visibilityNotice.Text = notice ??
            (view?.Owner == ViewportOwner.Other ? "Another client controls this viewport." :
                view?.Policy is { } mask
                    ? (mask.Isolated is null ? "Temporary viewport settings" : "Isolation active")
                    : "");
        UpdateVisibilityActions();
        UpdateViewportNavigationActions();
    }

    private string ViewportHierarchyLabel(HierarchyRow? row)
    {
        if (row is null) return "";
        string text = _hierarchyFilter.Active && !_hierarchyProjection.Matches(row.Node)
            ? "Path · " + row.Label : row.Label;
        if (_viewportMembership is not { } membership) return text;
        if (!membership.Visible(row.Node)) text = "Hidden · " + text;
        if (membership.Locked(row.Node)) text = "Pick locked · " + text;
        return text;
    }
}
