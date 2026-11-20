using System;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Command = Atlantis.Gameplay.Authoring.Command;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void TaskIssuesChecks()
    {
        var journal = new TaskIssues();
        journal.Reset();
        var guid = new AssetGuid(Guid.NewGuid());
        var id = new TextureImportId(new AssetGuid(Guid.NewGuid()));
        var generation = new GenerationId(new AssetGuid(Guid.NewGuid()));
        long older = journal.Begin("Import texture", "paint");
        long newer = journal.Begin("Import texture", "paint");
        var published = new TextureImportJob(id, "paint", ImportMode.Register, RebuildState.Succeeded,
            guid, generation, null);
        var running = published with { State = RebuildState.Running, Generation = null };
        journal.Admit(older, AssetTaskReceipt.From(running));
        journal.Observe(newer, id, AssetTaskReceipt.From(published));
        journal.Observe(older, new RebuildId(id.Value), AssetTaskReceipt.From(published));
        Check(journal.Find(older)!.Result == TaskResult.Running && journal.Find(newer)!.Result == TaskResult.Requesting,
            "tasks: a foreign typed receipt and an ID without admission cannot settle a same-name attempt");
        journal.Observe(older, id, null, "UnknownJob");
        Check(journal.Find(older) is { Result: TaskResult.Unknown, Receipt: TextureImportId retained } && retained == id,
            "tasks: an expired lookup preserves the accepted receipt and becomes unknown");
        journal.Observe(older, id, AssetTaskReceipt.From(published));
        journal.Observe(older, id, AssetTaskReceipt.From(running));
        var row = journal.Find(older)!;
        Check(journal.Rows[0].Sequence == newer && row is { Result: TaskResult.Succeeded, Asset: { } asset } && asset == guid &&
            row.Summary(journal.ActivationKnown, journal.ActiveGeneration) == "Published · activation unconfirmed",
            "tasks: a matched publication settles its own row in place and remains proven after older running status");
        journal.ObserveActivation(null);
        string details = row.Details(journal.ActivationKnown, journal.ActiveGeneration);
        Check(details.Contains("Published · restart required", StringComparison.Ordinal) &&
              details.Contains("Active generation: unnamed", StringComparison.Ordinal) &&
              details.Contains(guid.ToString(), StringComparison.Ordinal) && details.Contains(id.ToString(), StringComparison.Ordinal),
            "tasks: clipboard projection distinguishes published and active generation and identifies the asset and job");
        journal.ObserveActivation(generation);
        Check(row.Summary(journal.ActivationKnown, journal.ActiveGeneration) == "Published · active",
            "tasks: only active generation evidence confirms activation");

        long stopped = journal.Begin("Move texture", "stale");
        journal.Admit(stopped, null, "Canceled before sending", TaskResult.NotSent);
        long denied = journal.Begin("Rebuild", "busy");
        journal.Admit(denied, null, "Busy");
        Check(journal.Find(stopped)!.Result == TaskResult.NotSent && journal.Find(denied)!.Result == TaskResult.Refused,
            "tasks: proven unsent, refusal and unknown are distinct");
        journal.Disconnect();
        Check(!journal.Connected && journal.Find(newer)!.Result == TaskResult.Unknown &&
            journal.Find(older)!.Result == TaskResult.Succeeded, "tasks: disconnect retains proven results and disables navigation");
        journal.Reset();
        Check(journal.Rows.Count == 0 && !journal.ActivationKnown, "tasks: session replacement clears retained rows");
        long outstanding = journal.Begin("Import", "outstanding");
        for (int i = 0; i < 36; ++i) journal.Admit(journal.Begin("Import", "attempt " + i), null, "Busy");
        Check(journal.Rows.Count == 33 && journal.Rows[0].Target == "attempt 35" && journal.Rows[^1].Sequence == outstanding,
            "tasks: 32 settled rows and already outstanding work in stable start order");

        journal.Reset();
        var doc = new FakeDocument();
        var node = doc.Add(93, null, LocalTransform.Identity, FakeDocument.Light);
        var address = new SceneAddress(node, FakeDocument.Light, FakeDocument.Intensity);
        var apply = new ApplyController(doc);
        long lifetime = 1;
        apply.TaskObserved += update => journal.ObserveApply(lifetime, "scene.source.txt", null, update);
        doc.ApplyTracked();
        apply.Initialize(doc.ApplyStatus().Value!);
        apply.Poll();
        Check(journal.Rows.Count == 0, "tasks: an external latest Apply never creates a local row");
        apply.SetPaused(true);
        apply.Commit(new Command[] { new SetProperty(address, PropertyValue.FromFloat32(2)) });
        var first = apply.Receipt!;
        Check(journal.Rows.Single() is { Result: TaskResult.Paused, EditCommitted: true } && doc.Applies == 2,
            "tasks: paused status is distinguishable and commit proof accompanies the exact local Apply");
        doc.FailReload(ReloadError.ScenePreparationFailed);
        apply.Poll();
        row = journal.Rows.Single();
        Check(row.Summary(false, null) == "Edit committed · Apply failed" && Equals(row.Receipt, first.Id) &&
            row.Diagnostic!.Contains(nameof(ReloadError.ScenePreparationFailed), StringComparison.Ordinal),
            "tasks: Apply failure keeps commit proof and the independently matched reload error");
        apply.Retry();
        var retry = apply.Receipt!;
        apply.Request();
        doc.UnknownStatus = true;
        apply.Poll();
        apply.Poll();
        Check(journal.Rows[0] is { Result: TaskResult.Unknown, EditCommitted: false } && Equals(journal.Rows[0].Receipt, retry.Id) &&
            journal.Rows[1] == row && doc.Applies == 3, "tasks: lost result never resends a coalesced Apply or relabels the prior failure");
        doc.UnknownStatus = false;
        apply.Retry();
        journal.RetireDocuments(++lifetime);
        Check(journal.Rows[0].DocumentLifetime == 1 && journal.Rows[0].Result == TaskResult.Unknown,
            "tasks: outstanding rows from a replaced document are retired while retained for inspection");

        journal.Reset();
        var lost = new FakeDocument();
        var loseAcceptance = new ApplyController(lost);
        loseAcceptance.TaskObserved += update => journal.ObserveApply(lifetime, "scene.source.txt", null, update);
        try
        {
            loseAcceptance.Commit(Array.Empty<Command>(), _ =>
            {
                lost.ApplyTracked();
                throw new RemoteException(RemoteError.Disconnected, "controlled lost acceptance");
            });
            Fail("tasks: the lost acceptance should propagate its original transport failure");
        }
        catch (RemoteException error) when (error.Error == RemoteError.Disconnected) { }
        loseAcceptance.Poll();
        Check(lost.Applies == 1 && journal.Rows.Single() is { Result: TaskResult.Unknown, Receipt: null, EditCommitted: false },
            "tasks: losing an admission reply yields an unknown row without borrowing an identity or resending");

        journal.Reset();
        var coalesced = new FakeDocument();
        var controller = new ApplyController(coalesced);
        controller.TaskObserved += update => journal.ObserveApply(lifetime, "scene.source.txt", null, update);
        controller.Request();
        var accepted = controller.Receipt!;
        controller.AfterLocalEdit(true);
        Check(journal.Rows.Count == 1, "tasks: coalescing does not create a row before the second send");
        coalesced.Reload();
        controller.Poll();
        Check(journal.Rows.Count == 2 && journal.Rows[0].EditCommitted && journal.Rows[1].Result == TaskResult.Succeeded &&
            Equals(journal.Rows[1].Receipt, accepted.Id), "tasks: the first Apply settles before the coalesced Apply creates a new row");
        coalesced.FailReload(ReloadError.RuntimeStopped);
        controller.Poll();
        Check(journal.Rows[0].Diagnostic!.Contains("The Runtime stopped.", StringComparison.Ordinal),
            "tasks: fatal Apply failure carries no promise of the previous Runtime projection");
    }
}
