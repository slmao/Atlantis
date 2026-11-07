using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Assets;
using Command = Atlantis.Gameplay.Authoring.Command;
using CommandFailure = Atlantis.Gameplay.Authoring.CommandFailure;
using Event = Atlantis.Gameplay.Authoring.Event;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private sealed class PickerDocument : IAuthoringDocument
    {
        public readonly EntityGuid Node = FakeDocument.Guid(82);
        public readonly TypeId Component = new(8200);
        public readonly FieldId Field = new(8201);
        public PropertyValue? Value;
        public bool Refuse;
        public ApplyError? ApplyFailure;
        public int Submissions, Applies;
        private readonly List<CommandFailure> _failures = new();
        public IReadOnlyList<SchemaType> Schema => new[] { new SchemaType(Component, "test::UnexpectedComponent", TypeKind.Struct, 1,
            new[] { new SchemaField(Field, "unexpectedReference", TypeKind.Primitive, PrimitiveKind.AssetGuid, default,
                FieldFlags.Serializable | FieldFlags.AssetReference | FieldFlags.Optional) }, Array.Empty<SchemaConstant>()) };
        public DocumentInfo? Info() => new(default, Provenance.HandAuthored, "fixture", 1);
        public IReadOnlyList<NodeSummary> ListNodes() => new[] { new NodeSummary(Node, null) };
        public IReadOnlyList<TypeId> ListComponents(EntityGuid node) => node == Node ? new[] { Component } : Array.Empty<TypeId>();
        public PropertyValue? GetProperty(SceneAddress address) => Value;
        public EntityGuid? ActiveCamera() => null;
        public TransactionTicket SubmitTransaction(IReadOnlyList<Command> commands)
        {
            ++Submissions;
            if (Refuse) _failures.Add(new(new CommandTicket((ulong)Submissions), AuthoringError.UnknownNode));
            else Value = ((SetProperty)commands.Single()).Value;
            return new(new CommandTicket((ulong)Submissions), (ulong)commands.Count);
        }
        public IReadOnlyList<CommandFailure> DrainFailures() { var result = _failures.ToArray(); _failures.Clear(); return result; }
        public IReadOnlyList<Event> DrainEvents() => Array.Empty<Event>();
        public ApplyError? Apply() { ++Applies; return ApplyFailure; }
        public Outcome<ApplyReceipt, ApplyError> ApplyTracked()
        {
            var error = Apply();
            return error is ApplyError e ? Outcome<ApplyReceipt, ApplyError>.Err(e) :
                Outcome<ApplyReceipt, ApplyError>.Ok(new ApplyReceipt(new ApplyId(System.Guid.NewGuid()), 1));
        }
        public Outcome<ApplySnapshot, ApplyLookupError> ApplyStatus(ApplyId? id = null) =>
            Outcome<ApplySnapshot, ApplyLookupError>.Err(ApplyLookupError.UnknownApply);
        public SaveError? Save() => null;
    }

    private static void AssetReferencePickerChecks()
    {
        AssetGuid a = AssetGuid.Parse("00820082-0082-4082-8082-008200820001"), b = AssetGuid.Parse("00820082-0082-4082-8082-008200820002");
        var doc = new PickerDocument { Value = PropertyValue.FromAsset(a) };
        var inspector = new InspectorModel(doc);
        InspectorField Field() { inspector.Refresh(doc.Node); return inspector.Fields.Single(); }
        AssetReferencePicker Capture() => AssetReferencePicker.Capture(82, doc.Node, Field())!;
        InspectorField field = Field();
        Check(InspectorModel.CanChooseAsset(field) && !field.Field.IsEditable, "picker: serializable authoring field eligible without World Editable flag or known name");
        Check(!InspectorModel.CanChooseAsset(field with { Editable = false }), "picker: read-only Inspector field excluded");
        foreach (var schema in new[] { field.Field with { Kind = TypeKind.Struct }, field.Field with { Primitive = PrimitiveKind.EntityGuid },
                                       field.Field with { Flags = FieldFlags.Serializable } })
            Check(!InspectorModel.CanChooseAsset(field with { Field = schema }), "picker: non-asset schema shape excluded");
        Check(!InspectorModel.CanChooseAsset(field with { Value = null }), "picker: failed read is not optional absence");
        Check(InspectorModel.CanChooseAsset(field with { Value = PropertyValue.Absent }), "picker: optional absence is eligible");
        Check(!InspectorModel.CanChooseAsset(field with { Value = PropertyValue.Absent, Optional = false }), "picker: nonoptional absence not eligible");
        var picker = Capture();
        Check(picker.Confirm(inspector, doc.Node, a).Refusal == "unchanged" && doc.Submissions == 0, "picker: same-value confirmation plans no edit");
        Check(picker.Confirm(inspector, doc.Node, b).Refusal is not null, "picker: confirmation consumed once");
        picker = Capture();
        EditPlan plan = picker.Confirm(inspector, doc.Node, b);
        Check(plan.Refusal is null && plan.Commands.Count == 1 && plan.Commands[0] is SetProperty set &&
              set.Address == new SceneAddress(doc.Node, doc.Component, doc.Field) && set.Value.AsAsset() == b,
              "picker: changed candidate is one ordinary addressed SetProperty");
        var apply = new ApplyController(doc);
        Check(apply.Commit(plan.Commands).Count == 0 && doc.Value!.Value.AsAsset() == b && doc.Submissions == 1 && doc.Applies == 1,
              "picker: ordinary commit and automatic apply used");
        doc.Value = PropertyValue.FromAsset(a);
        foreach (Event e in new Event[] { new DocumentReopened(a), new NodeDestroyed(doc.Node),
                  new Atlantis.Gameplay.Authoring.ComponentRemoved(doc.Node, doc.Component),
                  new Atlantis.Gameplay.Authoring.PropertyChanged(new(doc.Node, doc.Component, doc.Field), PropertyValue.FromAsset(b)) })
        {
            picker = Capture(); picker.Observe(e);
            picker.Observe(new Atlantis.Gameplay.Authoring.PropertyChanged(new(doc.Node, doc.Component, doc.Field), PropertyValue.FromAsset(a)));
            Check(picker.Confirm(inspector, doc.Node, b).Refusal is not null, "picker: observed change/removal/reopen stays invalid after equal values restored");
        }
        picker = Capture(); picker.SelectionChanged(FakeDocument.Guid(83)); picker.SelectionChanged(doc.Node);
        Check(picker.Confirm(inspector, doc.Node, b).Refusal is not null, "picker: away and back does not revive target");
        picker = Capture(); picker.Observe(new Applied(99));
        Check(picker.Confirm(inspector, doc.Node, b).Refusal is null, "picker: Applied generation is not document reopen");
        picker = Capture(); doc.Value = PropertyValue.FromAsset(b); Field();
        Check(picker.Confirm(inspector, doc.Node, b).Refusal is not null, "picker: fresh query catches target changed before event observation");
        doc.Value = PropertyValue.FromAsset(a); picker = Capture(); doc.Value = null; Field();
        Check(picker.Confirm(inspector, doc.Node, b).Refusal is not null, "picker: unreadable target at final query refuses");
        doc.Value = PropertyValue.Absent; picker = Capture();
        Check(picker.Confirm(inspector, doc.Node, a).Refusal is null, "picker: optional absent can be assigned");
        doc.Value = PropertyValue.FromAsset(a); picker = Capture(); picker.Invalidate("cancelled");
        Check(picker.Confirm(inspector, doc.Node, b).Refusal == "cancelled", "picker: cancelled request submits nothing");
        picker = Capture(); plan = picker.Confirm(inspector, doc.Node, b);
        var refusedDoc = new PickerDocument { Value = PropertyValue.FromAsset(a), Refuse = true };
        Check(new ApplyController(refusedDoc).Commit(plan.Commands).Count == 1 && refusedDoc.Applies == 0 && refusedDoc.Value!.Value.AsAsset() == a,
              "picker: ordinary transaction refusal does not apply or mutate");
        var failedApply = new PickerDocument { Value = PropertyValue.FromAsset(a), ApplyFailure = ApplyError.NoDocument };
        var failedController = new ApplyController(failedApply);
        var tree = new DocumentTree(); tree.Rebuild(failedApply);
        var recorder = EditRecorder.Begin(failedApply, tree, plan)!;
        var failures = failedController.Commit(plan.Commands);
        var target = new AuthoringEditTarget(failedApply, tree, failedController);
        var history = new EditHistory(target);
        history.Record(recorder.Finish(failedApply.Schema), target.Committed(failures)); history.Update();
        Check(failures.Count == 0 && failedController.Status == ApplyStatus.Failed && failedApply.Value!.Value.AsAsset() == b && history.CanUndo,
              "picker: apply failure leaves accepted document edit and history intact");
        failedApply.ApplyFailure = null;
        Check(history.Undo() == HistoryResult.Submitted && failedApply.Value!.Value.AsAsset() == a,
              "picker: committed edit remains undoable after apply refusal");

        var browser = new AssetBrowser();
        var list = Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok(new[]
        { new AssetSummary(a, AssetType.Mesh, "assets:a"), new AssetSummary(b, AssetType.Material, "assets:b") });
        browser.CompleteList(browser.BeginList(), list);
        browser.Select(a);
        long cancelledDetail = browser.Select(b);
        var cancelled = new AssetDetails(b, AssetType.Material, "assets:b", 1, null, "cancelled detail", Array.Empty<AssetGuid>());
        browser.ClearSelection();
        Check(browser.Selected is null && browser.Details is null && browser.DetailState == "" && browser.State == "Ready",
              "picker: deselection clears candidate/details without discarding catalog");
        Check(!browser.CompleteDetail(cancelledDetail, b, Outcome<AssetDetails, AssetError>.Ok(cancelled)) && browser.Selected is null && browser.Details is null,
              "picker: old pending detail cannot restore a deselected candidate");
        browser.CompleteList(browser.BeginList(), list);
        Check(browser.Selected is null, "picker: reload does not revive a cancelled candidate");
        long replacementDetail = browser.Select(b);
        Check(!browser.CompleteDetail(cancelledDetail, b, Outcome<AssetDetails, AssetError>.Ok(cancelled)),
              "picker: cancelled reply remains obsolete after explicit reselection of the same GUID");
        Check(browser.CompleteDetail(replacementDetail, b, Outcome<AssetDetails, AssetError>.Ok(cancelled)),
              "picker: explicit new selection can load its own details");
        browser.ClearSelection();
        Check(browser.Selected is null && browser.Details is null && browser.DetailState == "",
              "picker: deselection also clears already completed details");
    }
}
