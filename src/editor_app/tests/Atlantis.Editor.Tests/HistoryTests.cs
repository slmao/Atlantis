// Plan 0068 M5 (Spec 0068 R8; ADR-0135 D4, ADR-0136): the editor's history
// over the authoring document -- property records (E2's, ported in M1)
// beside the structural records (create, destroy subtree, add/remove
// component, reparent, the active camera), every undo and redo one
// transaction and then an apply through the same AuthoringEditTarget the
// worker holds, the refusal rules of ADR-0136 D3 (what the document no
// longer holds refuses the step and drops the entry), the history kept
// across applies and cleared on DocumentReopened (the worker's rule), and
// the edits that cannot be recorded (an unminted node) not being undoable.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Command = Atlantis.Gameplay.Authoring.Command;
using CommandFailure = Atlantis.Gameplay.Authoring.CommandFailure;
using Event = Atlantis.Gameplay.Authoring.Event;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;
using AddComponent = Atlantis.Gameplay.Authoring.AddComponent;
using RemoveComponent = Atlantis.Gameplay.Authoring.RemoveComponent;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    // The worker's path over a fake document: the recorder reads before the commit and finishes after it, the history
    // records the settled commit, and the tree and the apply controller follow every event of every client's.
    private sealed class HistoryHarness
    {
        public readonly FakeDocument Document = new();
        public readonly DocumentTree Tree = new();
        public readonly ApplyController Apply;
        public readonly AuthoringEditTarget Target;
        public readonly EditHistory History;

        public HistoryHarness()
        {
            Apply = new ApplyController(Document);
            Target = new AuthoringEditTarget(Document, Tree, Apply);
            History = new EditHistory(Target);
        }

        /// <summary>The editor commits a planned edit; the refusal, if any. A recordable, accepted edit joins the history.</summary>
        public AuthoringError? Commit(EditPlan plan)
        {
            EditRecorder? recorder = EditRecorder.Begin(Document, Tree, plan);
            IReadOnlyList<CommandFailure> failures = Apply.Commit(plan.Commands);
            if (failures.Count == 0 && recorder is not null)
            {
                History.Record(recorder.Finish(Document.Schema), Target.Committed(failures));
                History.Update();
            }
            Settle();
            return failures.Count > 0 ? failures[0].Error : null;
        }

        /// <summary>An undo or a redo: one transaction and then an apply, as the worker's HistoryStep runs it.</summary>
        public HistoryResult Step(bool undo)
        {
            HistoryResult result = undo ? History.Undo() : History.Redo();
            History.Update();
            Settle();
            return result;
        }

        /// <summary>Another client's transaction: it changes the document, and the history does not record it.</summary>
        public void Outside(params Command[] commands)
        {
            Document.SubmitTransaction(commands);
            Settle();
        }

        /// <summary>The tree and the apply controller follow every event; a DocumentReopened clears the history (ADR-0135 D4).</summary>
        public void Settle()
        {
            bool reopened = false;
            foreach (Event e in Document.DrainEvents())
            {
                Tree.Apply(e, Document);
                Apply.Apply(e);
                reopened |= e is DocumentReopened;
            }
            if (reopened) History.Clear();
        }

        /// <summary>The Runtime's reload answer: Applied for the next generation, settling an in-flight apply.</summary>
        public void Applied()
        {
            Document.Reload();
            Settle();
        }
    }

    private static HistoryHarness Document5()
    {
        var h = new HistoryHarness();
        EntityGuid r = h.Document.Add(1, null, LocalTransform.Identity);
        h.Document.Add(2, r, Local(new(1, 0, 0), Vector3.Zero, Vector3.One));
        h.Document.Add(3, FakeDocument.Guid(2), Local(new(0, 1, 0), Vector3.Zero, Vector3.One), FakeDocument.Light);
        h.Document.Add(4, r, Local(new(0, 0, 5), new(0.1f, 0.2f, 0), Vector3.One), FakeDocument.CameraType);
        h.Document.Add(5, null, Local(new(3, 2, 1), new(0.4f, 0.1f, 0.7f), new(1, 2, 1)));
        h.Document.SetCamera(FakeDocument.Guid(4));
        h.Tree.Rebuild(h.Document);
        return h;
    }

    private static float LightIntensity(HistoryHarness h, EntityGuid node) =>
        h.Document.GetProperty(new SceneAddress(node, FakeDocument.Light, FakeDocument.Intensity))!.Value.AsFloat32();

    private static void HistoryChecks()
    {
        EntityGuid r = FakeDocument.Guid(1), a = FakeDocument.Guid(2), a1 = FakeDocument.Guid(3), b = FakeDocument.Guid(4), f = FakeDocument.Guid(5);

        // A property record (an Inspector commit): undo and redo are one transaction each, then an apply.
        {
            var h = Document5();
            var intensity = new SceneAddress(a1, FakeDocument.Light, FakeDocument.Intensity);
            Check(h.Commit(new EditPlan("light", new Command[] { new SetProperty(intensity, PropertyValue.FromFloat32(3.5f)) })) is null &&
                  LightIntensity(h, a1) == 3.5f, "history: an Inspector edit is recorded and applied");
            int submissions = h.Document.Submissions, applies = h.Document.Applies;
            h.Applied();  // the Runtime answers the first apply, so the undo's is its own
            Check(h.Step(undo: true) == HistoryResult.Submitted && LightIntensity(h, a1) == 1.0f &&
                  h.Document.Submissions == submissions + 1 && h.Document.Applies == applies + 1,
                  "history: undo of a property edit writes the Before value, one transaction and an apply");
            Check(h.Step(undo: false) == HistoryResult.Submitted && LightIntensity(h, a1) == 3.5f &&
                  h.History.UndoLabel == "light" && h.History.RedoDepth == 0,
                  "history: redo writes the After value and is undoable again");
        }

        // Create: undo removes the node, redo returns it at its position under its parent; a SetProperty and a SetParent on
        // the created node in the same transaction are folded into its entry.
        {
            var h = Document5();
            var (create, created) = HierarchyCommands.CreateChild(h.Document, h.Tree, a);
            Check(h.Commit(create) is null && h.History.UndoLabel == "create node" && h.History.CanUndo, "history: a create is recorded");
            Check(h.Step(undo: true) == HistoryResult.Submitted && !h.Tree.Contains(created), "history: undo of a create removes the node");
            Check(h.Step(undo: false) == HistoryResult.Submitted && h.Tree.Contains(created) && h.Tree.ParentOf(created) == a &&
                  h.Tree.Order[^1] == created && TransformMath.LocalOf(h.Document, created) == LocalTransform.Identity,
                  "history: redo of a create returns the node at its position under its parent");
            var folded = new EntityGuid(Guid.NewGuid());
            Check(h.Commit(new EditPlan("create placed", new Command[]
                  {
                      new CreateNode(folded, (ulong)h.Tree.Count, Array.Empty<TypeId>()),
                      new SetProperty(new SceneAddress(folded, FakeDocument.Transform, FakeDocument.Position), PropertyValue.FromVec3(new(7, 8, 9))),
                      new SetParent(folded, b),
                  })) is null &&
                  h.Step(undo: true) == HistoryResult.Submitted && !h.Tree.Contains(folded) && h.Step(undo: false) == HistoryResult.Submitted &&
                  h.Tree.ParentOf(folded) == b && TransformMath.LocalOf(h.Document, folded).Position == new Vector3(7, 8, 9),
                  "history: a create with its values and parent in one transaction is one entry, undone and redone whole");
        }

        // Destroy a subtree holding the active camera: undo re-creates every node in ascending position order with its
        // values and the camera, redo destroys it again -- each one transaction.
        {
            var h = Document5();
            h.Commit(new EditPlan("light", new Command[]
            {
                new SetProperty(new SceneAddress(a1, FakeDocument.Light, FakeDocument.Intensity), PropertyValue.FromFloat32(3.5f)),
            }));
            Check(h.Commit(HierarchyCommands.DestroySubtree(h.Document, h.Tree, r)) is null && h.Tree.Count == 1 &&
                  h.Document.ActiveCamera() is null, "history: a subtree destroy with the active camera is recorded");
            int submissions = h.Document.Submissions;
            Check(h.Step(undo: true) == HistoryResult.Submitted && h.Document.Submissions == submissions + 1 &&
                  h.Tree.Order.SequenceEqual(new[] { r, a, a1, b, f }) && h.Tree.ParentOf(a1) == a &&
                  h.Document.ActiveCamera() == b && LightIntensity(h, a1) == 3.5f,
                  "history: undo re-creates the subtree in order, its values and the active camera, in one transaction");
            Check(h.Step(undo: false) == HistoryResult.Submitted && !h.Tree.Contains(r) && h.Tree.Count == 1 &&
                  h.Document.ActiveCamera() is null,
                  "history: redo destroys the subtree again and clears the camera");
        }

        // A component's life: undo of an add removes it; undo of a remove returns it with its values, not its defaults.
        {
            var h = Document5();
            Check(h.Commit(new EditPlan("add light", new Command[] { new AddComponent(f, FakeDocument.Light) })) is null &&
                  h.Tree.ComponentsOf(f).Contains(FakeDocument.Light), "history: an added component is recorded");
            Check(h.Step(undo: true) == HistoryResult.Submitted && !h.Tree.ComponentsOf(f).Contains(FakeDocument.Light),
                  "history: undo of an add removes the component");
            Check(h.Step(undo: false) == HistoryResult.Submitted && h.Tree.ComponentsOf(f).Contains(FakeDocument.Light),
                  "history: redo adds it again");
            h.Commit(new EditPlan("light", new Command[]
            {
                new SetProperty(new SceneAddress(a1, FakeDocument.Light, FakeDocument.Intensity), PropertyValue.FromFloat32(9.0f)),
            }));
            Check(h.Commit(new EditPlan("remove light", new Command[] { new RemoveComponent(a1, FakeDocument.Light) })) is null &&
                  !h.Tree.ComponentsOf(a1).Contains(FakeDocument.Light), "history: a removed component is recorded");
            Check(h.Step(undo: true) == HistoryResult.Submitted && h.Tree.ComponentsOf(a1).Contains(FakeDocument.Light) &&
                  LightIntensity(h, a1) == 9.0f,
                  "history: undo of a remove returns the component with its values, not its defaults");
        }

        // A reparent: one record of the Parent entry and the Transform writes that kept the world placement (ADR-0136 D1);
        // undo returns the exact local values, redo re-parents keeping the world again.
        {
            var h = Document5();
            LocalTransform before = TransformMath.LocalOf(h.Document, f);
            float[] world = TransformMath.WorldOf(h.Document, h.Tree, f);
            Check(h.Commit(HierarchyCommands.Reparent(h.Document, h.Tree, f, a)) is null && h.Tree.ParentOf(f) == a,
                  "history: a reparent is recorded");
            Check(h.Step(undo: true) == HistoryResult.Submitted && h.Tree.ParentOf(f) is null &&
                  TransformMath.LocalOf(h.Document, f).Equals(before),
                  "history: undo of a reparent restores the parent and the exact local Transform");
            Check(h.Step(undo: false) == HistoryResult.Submitted && h.Tree.ParentOf(f) == a &&
                  NearMatrix(TransformMath.WorldOf(h.Document, h.Tree, f), world, 1e-5f),
                  "history: redo re-parents keeping the world placement");
        }

        // The refusals (ADR-0136 D3): what the document no longer holds refuses the step and drops the entry.
        {
            // A value changed elsewhere: E2's rule.
            var h = Document5();
            var intensity = new SceneAddress(a1, FakeDocument.Light, FakeDocument.Intensity);
            h.Commit(new EditPlan("light", new Command[] { new SetProperty(intensity, PropertyValue.FromFloat32(2.0f)) }));
            h.Outside(new SetProperty(intensity, PropertyValue.FromFloat32(5.0f)));
            Check(h.Step(undo: true) == HistoryResult.ChangedElsewhere && LightIntensity(h, a1) == 5.0f &&
                  !h.History.CanUndo && h.History.Notice.Contains("changed elsewhere"),
                  "history: an outside change to a value refuses the undo and drops the entry");

            // The node an undo of a create would destroy is gone.
            h = Document5();
            var (create, created) = HierarchyCommands.CreateChild(h.Document, h.Tree, a);
            h.Commit(create);
            h.Outside(new DestroyNode(created));
            Check(h.Step(undo: true) == HistoryResult.Invalidated && !h.History.CanUndo &&
                  h.History.Notice.Contains("what it edited is gone"),
                  "history: the node destroyed elsewhere refuses the undo of a create");

            // The node an undo of a create would destroy has another parent now.
            h = Document5();
            (create, created) = HierarchyCommands.CreateChild(h.Document, h.Tree, a);
            h.Commit(create);
            h.Outside(new SetParent(created, b));
            Check(h.Step(undo: true) == HistoryResult.ChangedElsewhere && h.Tree.ParentOf(created) == b,
                  "history: another parent refuses the undo of a create (the conservative rule)");

            // A redo of a create whose GUID exists again.
            h = Document5();
            (create, created) = HierarchyCommands.CreateChild(h.Document, h.Tree, a);
            h.Commit(create);
            h.Step(undo: true);
            h.Outside(new CreateNode(created, 0UL, Array.Empty<TypeId>()));
            Check(h.Step(undo: false) == HistoryResult.ChangedElsewhere && h.Tree.Contains(created) &&
                  !h.History.CanRedo, "history: a node with the record's GUID refuses the redo of a create");

            // A component's presence differs: the undo of a remove cannot re-add what another client re-added.
            h = Document5();
            h.Commit(new EditPlan("remove light", new Command[] { new RemoveComponent(a1, FakeDocument.Light) }));
            h.Outside(new AddComponent(a1, FakeDocument.Light));
            Check(h.Step(undo: true) == HistoryResult.ChangedElsewhere && h.Tree.ComponentsOf(a1).Contains(FakeDocument.Light),
                  "history: a component re-added elsewhere refuses the undo of a remove");

            // A Parent entry's current parent differs.
            h = Document5();
            h.Commit(HierarchyCommands.Reparent(h.Document, h.Tree, f, a));
            h.Outside(new SetParent(f, null));
            Check(h.Step(undo: true) == HistoryResult.ChangedElsewhere && h.Tree.ParentOf(f) is null,
                  "history: another parent refuses the undo of a reparent");

            // A position beyond the node count refuses a redo of a create.
            h = Document5();
            (create, created) = HierarchyCommands.CreateChild(h.Document, h.Tree, null);  // a root, at position 5
            h.Commit(create);
            h.Step(undo: true);
            h.Outside(new SetActiveCamera(null), new DestroyNode(a1), new DestroyNode(b));  // the count drops to 3
            Check(h.Step(undo: false) == HistoryResult.ChangedElsewhere && !h.History.CanRedo,
                  "history: a position now beyond the node count refuses the redo of a create");
        }

        // Lifetime: applies do not clear the history; DocumentReopened does (the worker's rule); a new edit clears redo.
        {
            var h = Document5();
            var intensity = new SceneAddress(a1, FakeDocument.Light, FakeDocument.Intensity);
            h.Commit(new EditPlan("one", new Command[] { new SetProperty(intensity, PropertyValue.FromFloat32(2.0f)) }));
            h.Applied();
            h.Commit(new EditPlan("two", new Command[] { new SetProperty(intensity, PropertyValue.FromFloat32(3.0f)) }));
            h.Applied();
            Check(h.History.UndoDepth == 2 && h.History.CanUndo && h.Apply.Status == ApplyStatus.Applied,
                  "history: applies do not clear the history");
            Check(h.Step(undo: true) == HistoryResult.Submitted && LightIntensity(h, a1) == 2.0f,
                  "history: an undo reaches across applies");
            Check(h.Commit(new EditPlan("three", new Command[] { new SetProperty(intensity, PropertyValue.FromFloat32(4.0f)) })) is null &&
                  !h.History.CanRedo && h.Step(undo: false) == HistoryResult.NothingTo,
                  "history: a new edit clears the redo stack");
            h.Document.Reopen();
            h.Settle();
            Check(!h.History.CanUndo && !h.History.CanRedo && h.History.UndoDepth == 0 && h.Tree.Count == 5,
                  "history: DocumentReopened clears the history, the tree rebuilt");

            // An edit the document refuses is not recorded, and an unminted node cannot be recorded (not undoable).
            h = Document5();
            Check(h.Commit(new EditPlan("bad", new Command[] { new DestroyNode(new EntityGuid(Guid.NewGuid())) })) is not null &&
                  h.History.UndoDepth == 0, "history: a refused edit joins no history");
            Check(h.Commit(new EditPlan("unminted", new Command[] { new CreateNode(null, 0UL, Array.Empty<TypeId>()) })) is null &&
                  h.History.UndoDepth == 0 && !h.History.CanUndo,
                  "history: an edit the recorder cannot record is not undoable");
        }

        // A transaction the document refuses (validation) drops the entry with the refusal's reason (ADR-0136 D3).
        {
            var target = new RefusingTarget();
            var history = new EditHistory(target);
            history.Record(new EditTransaction("move", new[] { new PropertyWrite(target.Address, PropertyValue.FromFloat32(1.0f),
                                                                                PropertyValue.FromFloat32(2.0f)) }),
                           new EditHandle(1, 1));
            history.Update();
            Check(history.CanUndo && history.UndoLabel == "move", "history (stub): the edit is undoable");
            HistoryResult refused = history.Undo();
            history.Update();
            Check(refused == HistoryResult.Submitted && history.Notice.Contains("refused: RefusedForTheTest") &&
                  !history.CanUndo && !history.CanRedo,
                  "history (stub): a refused undo transaction drops the entry with its reason");
        }
    }

    /// <summary>A property-only target whose own submissions are refused: the document-refusal path of a history step.</summary>
    private sealed class RefusingTarget : IEditTarget
    {
        public readonly SceneAddress Address = new(new EntityGuid(Guid.NewGuid()), new TypeId(1), new FieldId(1));
        private readonly Dictionary<ulong, Settlement> _settlements = new();

        public PropertyValue? Read(SceneAddress address) => PropertyValue.FromFloat32(2.0f);  // what the edit left there

        public EditHandle Submit(IReadOnlyList<(SceneAddress Address, PropertyValue Value)> writes)
        {
            var handle = new EditHandle((ulong)(_settlements.Count + 2), 1);
            _settlements[handle.First] = Settlement.Refused;
            return handle;
        }

        public void Track(EditHandle handle, IReadOnlyList<(SceneAddress Address, PropertyValue Value)> writes) =>
            _settlements[handle.First] = Settlement.Applied;

        public Settlement SettlementOf(EditHandle handle) => _settlements.TryGetValue(handle.First, out Settlement s) ? s : Settlement.Pending;

        public string RefusalText(EditHandle handle) => "RefusedForTheTest";

        public void Update()
        {
        }

        public IReadOnlyList<Invalidation> DrainInvalidations() => Array.Empty<Invalidation>();
    }
}
