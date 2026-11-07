using System;
using System.Collections.Generic;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Command = Atlantis.Gameplay.Authoring.Command;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;
public static partial class Program
{
    private sealed class ApplyPreview : IWorldPreview
    {
        public int Writes;
        public void Write(IReadOnlyList<(EntityGuid Node, float[] World)> matrices) => ++Writes;
    }
    private static void ApplyOutcomeChecks()
    {
        (FakeDocument Doc, ApplyController Apply, SceneAddress Address) Setup()
        {
            var d = new FakeDocument(); var node = d.Add(83, null, LocalTransform.Identity, FakeDocument.Light);
            return (d, new ApplyController(d), new SceneAddress(node, FakeDocument.Light, FakeDocument.Intensity));
        }
        var (doc, apply, addr) = Setup(); var tree = new DocumentTree(); tree.Rebuild(doc);
        var target = new AuthoringEditTarget(doc, tree, apply); var history = new EditHistory(target);
        var failures = apply.Commit(new Command[] { new SetProperty(addr, PropertyValue.FromFloat32(2)) });
        history.Record(new EditTransaction("intensity", new[] { new PropertyWrite(addr, PropertyValue.FromFloat32(1), PropertyValue.FromFloat32(2)) }), target.Committed(failures)); history.Update();
        ApplyReceipt first = apply.Receipt!;
        Check(history.CanUndo && doc.Applies == 1 && apply.Status == ApplyStatus.Applying, "outcome: document/history settles before reload");
        doc.DrainEvents(); doc.FailReload(ReloadError.ScenePreparationFailed);
        Check(doc.DrainEvents().Count == 0, "outcome: Runtime failure has no authoring event");
        apply.Poll();
        Check(apply.Status == ApplyStatus.Failed && apply.Suspended && !apply.CanPreview && apply.CanApply && history.CanUndo,
            "outcome: event-independent failure releases waiter and retains committed history");
        Check(history.Undo() == HistoryResult.Submitted && doc.GetProperty(addr)!.Value.AsFloat32() == 1 && doc.Applies == 1,
            "outcome: suspended Undo submits document only");
        history.Update();
        Check(history.Redo() == HistoryResult.Submitted && doc.GetProperty(addr)!.Value.AsFloat32() == 2 && doc.Applies == 1,
            "outcome: suspended Redo submits document only");
        failures = apply.Commit(new Command[] { new SetProperty(addr, PropertyValue.FromFloat32(3)) });
        history.Record(new EditTransaction("repair", new[] { new PropertyWrite(addr, PropertyValue.FromFloat32(2), PropertyValue.FromFloat32(3)) }), target.Committed(failures)); history.Update();
        Check(doc.Applies == 1 && apply.Suspended, "outcome: ordinary repair cannot bypass suspension through CommitAndApply");
        apply.SetPaused(true); apply.Retry(); ApplyReceipt retried = apply.Receipt!; apply.Retry();
        Check(doc.Applies == 2 && retried.Id != first.Id && apply.Status == ApplyStatus.PendingPaused && !apply.CanPreview,
            "outcome: explicit retry snapshots latest once, duplicates disabled, pause retained");
        doc.Reload(); apply.Poll();
        Check(apply.Status == ApplyStatus.Applied && !apply.Suspended && apply.CanPreview && history.CanUndo,
            "outcome: matching current projection restores preview without clearing history");
        doc.NextApplyError = ApplyError.CookFailed; apply.Retry();
        Check(apply.Status == ApplyStatus.Failed && apply.Receipt is null && history.CanUndo,
            "outcome: synchronous refusal cannot borrow the previous apply identity or erase history");

        var (d2, a2, p2) = Setup(); a2.Request();
        a2.Commit(new Command[] { new SetProperty(p2, PropertyValue.FromFloat32(4)) });
        d2.FailReload(ReloadError.RenderPreparationFailed); a2.Poll();
        Check(d2.Applies == 1 && a2.Suspended, "outcome: failed apply drops automatic coalesced retry");
        a2.SetPaused(true); a2.Retry();
        a2.Commit(new Command[] { new SetProperty(p2, PropertyValue.FromFloat32(5)) });
        d2.Reload(); a2.Poll();
        Check(d2.Applies == 3 && a2.Status == ApplyStatus.PendingPaused && !a2.CanPreview,
            "outcome: recovery success issues one local follow-up requiring another Step");
        d2.Reload(); a2.Poll();
        Check(a2.CanPreview && a2.Status == ApplyStatus.Applied, "outcome: final current follow-up exits recovery");

        var (d3, a3, p3) = Setup(); a3.Request(); d3.FailReload(ReloadError.ScenePreparationFailed); a3.Poll();
        a3.Retry(); var owned = a3.Receipt!; d3.Reload();
        d3.SubmitTransaction(new Command[] { new SetProperty(p3, PropertyValue.FromFloat32(6)) }); d3.DrainFailures();
        d3.ApplyTracked(); d3.Reload(); int before = d3.Applies; a3.Poll();
        Check(a3.Status == ApplyStatus.NeedsApply && a3.Suspended && !a3.CanPreview && d3.Applies == before,
            "outcome: historical Applied cannot end recovery after external projection/revision moved");
        Check(d3.ApplyStatus(owned.Id).Value!.Request!.State == ApplyState.Applied, "outcome: historical success remains immutable");

        foreach (bool externalProjection in new[] { false, true })
        {
            var (shared, recovery, address) = Setup();
            recovery.Request();
            shared.FailReload(ReloadError.ScenePreparationFailed);
            recovery.Poll();
            recovery.SetPaused(true);
            recovery.Retry();
            recovery.Commit(new Command[] { new SetProperty(address, PropertyValue.FromFloat32(4)) });
            shared.Reload();
            if (externalProjection)
            {
                shared.ApplyTracked();
                shared.Reload();
            }
            else
            {
                shared.SubmitTransaction(new Command[] { new SetProperty(address, PropertyValue.FromFloat32(5)) });
                shared.DrainFailures();
            }
            foreach (var change in shared.DrainEvents()) recovery.Apply(change);
            int issued = recovery.Issued;
            recovery.Poll();
            Check(recovery.Status == ApplyStatus.NeedsApply && recovery.Suspended &&
                !recovery.CanPreview && recovery.CanApply && recovery.Issued == issued,
                "outcome: queued local recovery does not auto-apply after an external " +
                (externalProjection ? "projection change" : "revision advance"));
            recovery.Retry();
            Check(recovery.Issued == issued + 1 && recovery.Status == ApplyStatus.PendingPaused,
                "outcome: explicit retry is available after rejecting the stale follow-up");
        }

        var (d4, a4, _) = Setup(); a4.Request(); var old = a4.Receipt!; var other = d4.ApplyTracked().Value!; a4.Poll();
        Check(a4.Status == ApplyStatus.Superseded && a4.Receipt == old && a4.Suspended,
            "outcome: another client's same-revision apply supersedes, never becomes this caller's receipt");
        a4.Retry(); d4.UnknownStatus = true; a4.Poll();
        Check(a4.Status == ApplyStatus.Unknown && a4.CanApply && a4.Suspended && !a4.CanPreview,
            "outcome: unknown is not failure/success or a permanently busy waiter");
        d4.UnknownStatus = false; var unknownId = a4.Receipt; a4.Poll();
        Check(a4.Receipt == unknownId && a4.Status == ApplyStatus.Unknown,
            "outcome: global latest cannot silently recover ownership or resubmit unknown work");

        var (d5, a5, p5) = Setup(); a5.Request(); var pending = a5.Receipt!;
        d5.Reopen(); foreach (var e in d5.DrainEvents()) a5.Apply(e); a5.Poll(); d5.Reload();
        foreach (var e in d5.DrainEvents()) a5.Apply(e); a5.Poll();
        Check(a5.Suspended && a5.Status == ApplyStatus.NeedsApply && a5.Receipt is null && d5.ApplyStatus(pending.Id).Value!.Request!.State == ApplyState.Applied,
            "outcome: Reopen does not cancel old snapshot or let its late success settle reopened document");
        var (batchDoc, batchApply, batchAddr) = Setup(); batchApply.Request();
        batchApply.Commit(new Command[] { new SetProperty(batchAddr, PropertyValue.FromFloat32(8)) });
        batchDoc.Reload(); batchDoc.Reopen();
        foreach (var e in batchDoc.DrainEvents()) batchApply.Apply(e);
        batchApply.Poll();
        Check(batchDoc.Applies == 1 && batchApply.Status == ApplyStatus.NeedsApply,
            "outcome: Applied before Reopen in one batch cannot issue a coalesced request for the reopened document");
        var (d6, a6, _) = Setup(); a6.Request(); d6.FailReload(ReloadError.RuntimeStopped); a6.Poll(); a6.Retry();
        Check(!a6.CanApply && !a6.CanPreview && d6.Applies == 1, "outcome: RuntimeStopped requires a new Runtime, no in-place retry");
        var observed = new ApplyController(d6); int submissions = d6.Submissions;
        observed.Initialize(d6.ApplyStatus().Value!);
        Check(observed.Receipt is null && observed.Suspended && d6.Submissions == submissions,
            "outcome: readiness observes shared failure without adopting identity or changing document");

        var preview = new ApplyPreview(); var transforms = new LocalTransforms(); var g = new GizmoDragController(doc, tree, transforms, preview);
        Check(g.Begin(addr.Node, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.X, false), "outcome: uncommitted gesture starts on valid basis");
        g.Preview(1); int writes = preview.Writes; g.Abandon(); var release = g.Release();
        Check(!g.Dragging && release.Refusal is not null && preview.Writes == writes,
            "outcome: recovery abandons active gesture and late release without stale-World rollback");
    }
}
