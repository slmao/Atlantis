using System;
using System.IO;
using System.Linq;
using System.Numerics;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void SceneObjectCreationChecks(string conformance)
    {
        // Use the native-generated authoring schema rather than a hand-mirrored descriptor table.
        var schema = Codec.DecodeSchema(File.ReadLines(Path.Combine(conformance, "codec_vectors.jsonl"))
            .Where(line => line.Contains("\"case\":\"schema.scene\"", StringComparison.Ordinal))
            .Select(line => Json.Parse(line).Find("wire")!).Single())!;
        var node = FakeDocument.Guid(10);
        var parent = FakeDocument.Guid(1);
        foreach (var preset in Enum.GetValues<SceneObjectPreset>())
        {
            bool child = preset == SceneObjectPreset.EmptyChild;
            var plan = SceneObjectPresets.Build(schema, preset, node, 4, child ? parent : null)!;
            Check(plan.Edit.Commands.OfType<CreateNode>().Single() is { Node: var id, Position: 4 } && id == node &&
                plan.Expected.Parent == (child ? parent : (EntityGuid?)null) &&
                plan.Edit.Commands.OfType<SetParent>().Count() == (child ? 1 : 0) &&
                !plan.Edit.Commands.Any(c => c is SetActiveCamera) &&
                plan.Edit.Commands.OfType<SetNodeName>().Count() == (SceneObjectPresets.Named(preset) ? 1 : 0),
                "preset composes one node and no active-camera write: " + preset);
        }
        var camera = SceneObjectPresets.Build(schema, SceneObjectPreset.Camera, node, 4, null)!;
        PropertyValue Camera(string field) => camera.Expected.Components.SelectMany(c => c.Values)
            .Single(v => v.Address == SceneSchema.AddressOf(schema, node, "Camera", field)).Value;
        Check(Camera("fovYRadians").AsFloat32() == MathF.PI / 3 && Camera("nearZ").AsFloat32() == .1f &&
            Camera("farZ").AsFloat32() == 100 && Camera("exposureCompensationEv").AsFloat32() == 0 &&
            camera.Expected.Components.Sum(c => c.Values.Count) == 14, "camera sets a valid lens and retains the nested defaults");
        var directional = SceneObjectPresets.Build(schema, SceneObjectPreset.DirectionalLight, node, 4, null)!;
        var point = SceneObjectPresets.Build(schema, SceneObjectPreset.PointLight, node, 4, null)!;
        var angle = SceneSchema.AddressOf(schema, node, "Transform", "localEulerAnglesRadians");
        var range = SceneSchema.AddressOf(schema, node, "Light", "range");
        Check(directional.Edit.Commands.OfType<SetProperty>().Any(s => s.Address == angle && s.Value.AsVec3() == new Vector3(-MathF.PI/4, 0, 0)) &&
              point.Edit.Commands.OfType<SetProperty>().Any(s => s.Address == range && s.Value.AsFloat32() == 10),
            "directional and point presets supply a usable angle or positive range");
        var mesh = SceneObjectPresets.Mesh(SceneObjectPreset.Cube)!.Value;
        var cube = SceneObjectPresets.Build(schema, SceneObjectPreset.Cube, node, 4, null)!;
        Check(cube.Edit.Commands.OfType<SetProperty>().Single().Value.AsAsset() == mesh &&
              cube.Expected.Components.SelectMany(c => c.Values).Any(v => v.Value == PropertyValue.Absent),
            "shape binds its stock mesh and leaves material absent");
        Check(!SceneObjectPresets.MatchesMesh(mesh, null) && SceneObjectPresets.Build([], SceneObjectPreset.Camera, node, 4, null) is null &&
            SceneObjectPresets.Build(schema, SceneObjectPreset.EmptyChild, node, 4, null) is null, "missing catalog/schema or parent fails closed");
        var detail = new AssetDetails(mesh, AssetType.Mesh, "fixture", 1, null, "fixture", []);
        Check(SceneObjectPresets.MatchesMesh(mesh, detail) &&
              !SceneObjectPresets.MatchesMesh(mesh, detail with { Type = AssetType.Material }) &&
              !SceneObjectPresets.MatchesMesh(SceneObjectPresets.Mesh(SceneObjectPreset.Plane)!.Value, detail),
              "stock Mesh eligibility checks both the catalog GUID and type");
        var sameLabel = SceneObjectPresets.Build(schema, SceneObjectPreset.Cube, parent, 5, null)!;
        Check(sameLabel.Expected.Name == cube.Expected.Name && sameLabel.Expected.Node != cube.Expected.Node &&
              sameLabel.Expected.Parent is null, "repeated named presets preserve the label and use distinct root identities");
        Check(SceneObjectPresets.Available(SceneObjectPreset.Cube, true, 2) &&
             !SceneObjectPresets.Available(SceneObjectPreset.Cube, false, 0) &&
             SceneObjectPresets.Available(SceneObjectPreset.EmptyChild, false, 1) &&
             SceneObjectPresets.Available(SceneObjectPreset.UnnamedRoot, false, 0) &&
             !SceneObjectPresets.Available(SceneObjectPreset.EmptyChild, true, 2) &&
             !SceneObjectPresets.Available(SceneObjectPreset.UnnamedRoot, true, 1), "named and legacy menu eligibility follows document and selection");

        var doc = new FakeDocument();
        var apply = new ApplyController(doc);
        var target = new AuthoringEditTarget(doc, new DocumentTree(), apply);
        var history = new EditHistory(target);
        var life = new NodeLife(cube.Expected, true) { Preset = SceneObjectPreset.Cube };
        history.Record(new EditTransaction("Create Cube", [], [life]), target.Committed([]));
        history.Update();
        Check(history.Undo(preset: (entry, forward) => entry == life && !forward
                ? new(BatchReplayState.Committed, "Undo settled") : new(BatchReplayState.Drop, "wrong operation")) == HistoryResult.Submitted &&
             history.CanRedo && doc.Submissions == 0 && doc.Applies == 0,
             "preset Undo settles through its callback, not the pipelined legacy history path");
        Check(history.Redo(preset: (_, _) => new(BatchReplayState.Retry, "not sent")) == HistoryResult.Waiting && history.CanRedo,
             "pre-send replay refusal keeps the retryable entry");
        Check(history.Redo(preset: (_, _) => new(BatchReplayState.Drop, "outcome unknown")) == HistoryResult.ChangedElsewhere &&
             !history.CanRedo && !history.CanUndo && doc.Submissions == 0 && doc.Applies == 0,
             "unverified replay never moves history or automatically applies");
    }
}
