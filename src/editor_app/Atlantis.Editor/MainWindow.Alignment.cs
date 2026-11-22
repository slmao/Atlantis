using System;
using System.Globalization;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Media;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private readonly Button _alignSurfaces = new() { Content = "Align surfaces", Focusable = false, IsEnabled = false };
    private readonly Button _alignConfirm = new() { Content = "Confirm", IsEnabled = false };
    private readonly Button _alignCancel = new() { Content = "Cancel" };
    private readonly TextBox _alignGap = new() { Text = "0", Width = 86 };
    private readonly TextBlock _alignStatus = EditorVisuals.Secondary("");
    private Control? _alignInputs;
    private long? _alignmentId;
    private ulong _alignmentSelection;
    private bool _alignmentBusy, _alignmentConfirming, _alignmentGapDirty;

    private Control BuildAlignmentControls()
    {
        AutomationProperties.SetName(_alignGap, "Gap in world units");
        AutomationProperties.SetName(_alignConfirm, "Confirm surface alignment");
        AutomationProperties.SetName(_alignCancel, "Cancel surface alignment");
        _alignStatus.TextWrapping = TextWrapping.Wrap;
        _alignSurfaces.Click += (_, _) => StartAlignment();
        _alignConfirm.Click += (_, _) =>
        {
            if (_alignmentId is not long id || _alignmentBusy || !_alignConfirm.IsEnabled) return;
            if (!AlignmentContextAllows())
            {
                CancelAlignment("The editing context changed. Start alignment again.");
                return;
            }
            if (_worker?.ConfirmAlignment(id) == true)
            {
                _alignmentConfirming = true;
                AlignmentWait("Rechecking both faces…");
            }
        };
        _alignCancel.Click += (_, _) => CancelAlignment();
        _alignGap.PropertyChanged += (_, e) =>
        {
            if (e.Property != TextBox.TextProperty || _alignmentId is null || _alignmentConfirming) return;
            _alignmentGapDirty = true;
            SubmitAlignmentGap();
        };
        _alignInputs = EditorVisuals.Actions(EditorVisuals.Secondary("Gap · world units"),_alignGap,_alignConfirm,_alignCancel);
        _alignInputs.IsVisible = false;
        _alignStatus.IsVisible = false;
        return EditorVisuals.Stack(3,_alignInputs,_alignStatus);
    }

    private bool AlignmentContextAllows()
    {
        return !_shuttingDown && !_referenceSessionFailed && _worker is not null && _canAddMeshToDocument &&
            _navigationReady && !_navigationPending && ViewportVisibilityReady &&
            _selection.Nodes.Count == 1 && !HasHiddenSelection() &&
            (_selection.Node is not EntityGuid node || _viewportMembership?.Pickable(node) != false) &&
            _viewport.Window is { Width: > 0, Height: > 0 } && !_leaveInProgress && !_recoveryDecision &&
            _pendingInspectorBlur is null && _pendingSceneFieldEdits == 0 && CurrentSceneInput() is null &&
            _batchDraft is null && _referenceWindow is null && !_referenceBusy && _materialWindow is null &&
            _gizmoGrab is null && _groupGrab is null && _hierarchyDrag is null && _assetDrag is null &&
            !_assetDropPending && !_addingMesh && _bistroView is null;
    }

    private void UpdateAlignmentControls()
    {
        _alignSurfaces.IsEnabled = _alignmentId is null && _worker is not null && _canAddMeshToDocument &&
            _selection.Nodes.Count == 1 && !_referenceSessionFailed;
        if (_alignInputs is not null) _alignInputs.IsVisible = _alignmentId is not null;
        _alignGap.IsEnabled = _alignmentId is not null && !_alignmentConfirming;
        if (_alignmentId is null || _alignmentBusy) _alignConfirm.IsEnabled = false;
    }

    private void SubmitAlignmentGap()
    {
        if (!_alignmentGapDirty || _alignmentBusy || _alignmentId is not long id) return;
        if (_worker?.SetAlignmentGap(id,AlignmentGap()) != true) return;
        _alignmentGapDirty = false;
        AlignmentWait("Checking the gap…");
    }

    private double AlignmentGap() =>
        double.TryParse(_alignGap.Text,NumberStyles.Float,CultureInfo.InvariantCulture,out double gap)
            ? gap : double.NaN;

    private void AlignmentStatus(string text)
    {
        _alignStatus.Text = text;
        _alignStatus.IsVisible = text.Length != 0;
    }

    private void StartAlignment()
    {
        if (_alignmentId is not null || _worker is not { } worker || _selection.Node is not EntityGuid node ||
            !AlignmentContextAllows())
        {
            AlignmentStatus("Select one visible object and finish the active edit or navigation first.");
            return;
        }
        _pressed = null;
        ++_pickRequest;
        _alignGap.Text = "0";
        _alignmentSelection = _selection.Generation;
        if (worker.BeginAlignment(node,_meshDocumentLifetime,_selection.Generation,view =>
        {
            if (!ReferenceEquals(worker,_worker) || _alignmentId != view.Id) return;
            if (!view.Active)
            {
                _alignmentId = null;
                _alignmentBusy = _alignmentConfirming = _alignmentGapDirty = false;
                AlignmentStatus(view.Notice);
                UpdateAlignmentControls();
                return;
            }
            _alignmentBusy = view.Busy;
            string Name(EntityGuid guid) => _hierarchyRows.FirstOrDefault(r => r.Node == guid) is { } row &&
                !string.IsNullOrWhiteSpace(row.Name) ? row.Name : guid.ToString();
            string prefix = view.Target is EntityGuid target ? Name(node) + " → " + Name(target) + " · " : "";
            AlignmentStatus(prefix + view.Notice);
            UpdateAlignmentControls();
            _alignConfirm.IsEnabled = view.CanConfirm;
            SubmitAlignmentGap();
        }) is not long id)
        {
            AlignmentStatus("Alignment could not start in the current editing context.");
            return;
        }
        _alignmentId = id;
        AlignmentWait("Checking the current projection…");
    }

    private void AlignmentWait(string notice)
    {
        _alignmentBusy = true;
        AlignmentStatus(notice);
        UpdateAlignmentControls();
    }

    private bool CancelAlignment(string notice = "Surface alignment canceled.")
    {
        if (_alignmentId is not long id) return false;
        _alignmentId = null;
        _alignmentBusy = _alignmentConfirming = _alignmentGapDirty = false;
        _pressed = null;
        _worker?.CancelAlignment(id);
        AlignmentStatus(notice);
        UpdateAlignmentControls();
        return true;
    }

    private void PickAlignmentFace(ViewportWindow window, CameraMatrices matrices, int x, int y)
    {
        if (_alignmentId is not long id || _alignmentBusy) return;
        if (!AlignmentContextAllows() || x < 0 || y < 0 || x >= window.Width || y >= window.Height)
        {
            CancelAlignment("The editing context changed. Start alignment again.");
            return;
        }
        float ndcX = (float)x / window.Width * 2 - 1;
        float ndcY = (float)y / window.Height * 2 - 1;
        var sample = new PlacementRay(x,y,window.Width,window.Height,_viewportCameraIntent,
            _sceneProjection ?? _camera.Pose(), CameraConvention.RayThroughNdc(matrices,ndcX,ndcY))
            { VisibilityIntent = _worker?.VisibilityIntent ?? 0 };
        if (_worker?.PickAlignment(id,sample,AlignmentGap()) == true) AlignmentWait("Checking the picked face…");
    }
}
