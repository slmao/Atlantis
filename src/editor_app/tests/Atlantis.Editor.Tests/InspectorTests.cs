// Plan 0068 M3 (Spec 0068 R4; ADR-0135 D2): the Inspector's model against the
// fake document -- reflection from the schema (nested structs expanded,
// descriptor order, Transform first), the editor kind of each field, an
// optional field, one transaction per commit, refusals with their reasons,
// outside changes, and add / remove component.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Command = Atlantis.Gameplay.Authoring.Command;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;
using AddComponent = Atlantis.Gameplay.Authoring.AddComponent;
using RemoveComponent = Atlantis.Gameplay.Authoring.RemoveComponent;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void InspectorChecks()
    {
        var document = new FakeDocument();
        EntityGuid light = document.Add(1, null, LocalTransform.Identity, FakeDocument.Light);
        EntityGuid camera = document.Add(2, null, LocalTransform.Identity, FakeDocument.CameraType);
        var tree = new DocumentTree();
        tree.Rebuild(document);
        var selection = new Selection();
        var inspector = new InspectorModel(document);

        inspector.Refresh(light);
        Check(inspector.Fields.Select(f => f.Path).SequenceEqual(new[]
              {
                  "Transform.localPosition", "Transform.localEulerAnglesRadians", "Transform.localScale",
                  "Light.intensity", "Light.color", "Light.kind", "Light.range",
              }), "inspector: every field of every component, Transform first, in descriptor order");
        Check(inspector.Find("Light.intensity") is { Control: FieldControl.Float, Text: "1" } &&
              inspector.Find("Light.color") is { Control: FieldControl.Float3, Text: "1 1 1" } &&
              inspector.Find("Light.kind") is { Control: FieldControl.Enum, Text: "Directional" } &&
              inspector.Find("Transform.localScale") is { Control: FieldControl.Float3, Text: "1 1 1" },
              "inspector: each field's editor kind and the value as it shows it (enums by name)");
        Check(inspector.Find("Light.range") is { Optional: true, Present: false, Text: "" }, "inspector: an absent optional field shows empty");

        inspector.Refresh(camera);
        Check(inspector.Find("Camera.fog.density") is { Label: "fog.density", Text: "0.5" } && inspector.Find("Camera.fog") is null,
              "inspector: a nested struct is expanded in place, its leaf labelled by its path");
        Check(inspector.Find("Camera.exposureCompensationEv") is { Editable: false }, "inspector: a read-only field is not editable");

        // Commits: one transaction (one SetProperty) each.
        inspector.Refresh(light);
        EditPlan set = inspector.Commit("Light.intensity", " 2.5 ");
        Check(set.Refusal is null && set.Commands.Count == 1 && set.Commands[0] is SetProperty { Value: var v } && v.Equals(PropertyValue.FromFloat32(2.5f)),
              "inspector: a commit is one SetProperty of the parsed value");
        Check(Submit(document, tree, selection, set) is null, "inspector: the document accepts it");
        inspector.Refresh(light);
        Check(inspector.Find("Light.intensity")!.Text == "2.5", "inspector: the refreshed field shows the committed value");
        Check(inspector.Commit("Light.intensity", "2.5").Refusal == "unchanged", "inspector: a value equal to the current one is not a commit");
        Check(inspector.Commit("Light.intensity", "abc").Refusal == "not a number" && inspector.Commit("Light.intensity", "").Refusal == "not a number" &&
              inspector.Commit("Light.intensity", "NaN").Refusal == "not a number", "inspector: text that is not a number is refused with its reason");
        Check(inspector.Commit("Light.color", "1 2").Refusal == "expected three numbers", "inspector: a vector with the wrong count is refused");
        Check(Submit(document, tree, selection, inspector.Commit("Light.color", "0.5, 0.25 1")) is null &&
              document.GetProperty(new SceneAddress(light, FakeDocument.Light, FakeDocument.Color)) is { } color && color.Equals(PropertyValue.FromVec3(new Vector3(0.5f, 0.25f, 1))),
              "inspector: a vector commits (spaces or commas)");
        Check(Submit(document, tree, selection, inspector.Commit("Light.kind", "Point")) is null &&
              document.GetProperty(new SceneAddress(light, FakeDocument.Light, FakeDocument.Kind)) is { } kind && kind.Equals(PropertyValue.FromEnum(1)),
              "inspector: an enum commits by name");
        inspector.Refresh(light);
        Check(inspector.Find("Light.kind")!.Text == "Point" && inspector.Commit("Light.kind", "1").Refusal == "unchanged" &&
              inspector.Commit("Light.kind", "Sun").Refusal == "one of Directional, Point", "inspector: an enum shows its name, accepts its number, and lists its choices on a bad name");
        inspector.Refresh(camera);
        Check(inspector.Commit("Camera.exposureCompensationEv", "1").Refusal == "the field is read-only", "inspector: a read-only field cannot be committed");
        Check(inspector.Commit("Light.missing", "1").Refusal == "no such field", "inspector: an unknown field is refused");

        // An optional field: set, then cleared; empty text clears it.
        inspector.Refresh(light);
        Check(Submit(document, tree, selection, inspector.Commit("Light.range", "12")) is null, "inspector: an optional field is set");
        inspector.Refresh(light);
        Check(inspector.Find("Light.range") is { Present: true, Text: "12" } && inspector.Clear("Light.range").Commands.Count == 1,
              "inspector: it shows present, and offers its clear");
        Check(Submit(document, tree, selection, inspector.Commit("Light.range", "")) is null, "inspector: empty text clears an optional field");
        inspector.Refresh(light);
        Check(inspector.Find("Light.range") is { Present: false } && inspector.Clear("Light.range").Refusal == "unchanged" &&
              inspector.Clear("Light.intensity").Refusal == "the field is not optional", "inspector: cleared, and a clear of a non-optional field is refused");

        // An outside change (another client) is seen by a refresh: the field follows the document.
        document.SubmitTransaction(new Command[] { new SetProperty(new SceneAddress(light, FakeDocument.Light, FakeDocument.Intensity), PropertyValue.FromFloat32(9.0f)) });
        document.DrainEvents();
        inspector.Refresh(light);
        Check(inspector.Find("Light.intensity")!.Text == "9", "inspector: a value changed elsewhere shows after a refresh (the field reverts to the document)");

        // Components: addable ones exclude what the node has and nested structs; add and remove are commands.
        Check(inspector.Addable().Select(t => t.ShortName).SequenceEqual(new[] { "Camera" }), "inspector: the addable components are the schema's top-level ones the node lacks");
        EditPlan add = inspector.AddComponent(FakeDocument.CameraType);
        Check(add.Commands.Single() is AddComponent && Submit(document, tree, selection, add) is null, "inspector: add component is one command");
        inspector.Refresh(light);
        Check(inspector.Find("Camera.fog.density") is not null && inspector.Addable().Count == 0 && inspector.AddComponent(FakeDocument.CameraType).Refusal == "the node has it",
              "inspector: the new component's fields appear");
        Check(inspector.RemoveComponent(FakeDocument.Transform).Refusal == "the Transform is required", "inspector: the Transform cannot be removed");
        Check(Submit(document, tree, selection, inspector.RemoveComponent(FakeDocument.Light)) is null, "inspector: remove component is one command");
        inspector.Refresh(light);
        Check(inspector.Find("Light.intensity") is null && inspector.RemoveComponent(FakeDocument.Light).Refusal == "the node does not have it",
              "inspector: the removed component's fields are gone");
        inspector.Refresh(null);
        Check(inspector.Fields.Count == 0 && inspector.Commit("Light.intensity", "1").Refusal == "no such field", "inspector: nothing selected shows nothing");
    }
}
