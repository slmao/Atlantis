// Plan 0058 P3/P9 (Spec 0058 R6; J3): the "codec" suite. Every committed
// conformance vector (tests/csharp/conformance/codec_vectors.jsonl, written by
// the C++ codec) decodes, and re-encodes to the same bytes; a hand-written
// table pins the edge cases to exact C# values; malformed values are refused
// as the C++ decoders refuse them. Plan 0065 P6: the authoring values too --
// the scene address, commands, events (Applied included), filters, failures,
// documents, node summaries, every error name, and the authoring.* results.
using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Text;
using Atlantis.Gameplay.Remote;
using A = Atlantis.Gameplay.Authoring;

namespace Atlantis.Gameplay.Tests;

public static class CodecSuite
{
    public static void Run(TestContext t)
    {
        string path = t.RepoPath("tests", "csharp", "conformance", "codec_vectors.jsonl");
        string text = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(path));
        t.Require(!text.Contains('\r'), "the vectors file has LF line endings");
        string[] lines = text.TrimEnd('\n').Split('\n');
        t.Require(lines.Length > 50, "the vectors file has its cases (" + lines.Length + ")");

        var wires = new Dictionary<string, JsonValue>();
        foreach (string line in lines)
        {
            JsonValue parsed = Json.Parse(line);
            string name = parsed.Find("case")!.AsString;
            string type = parsed.Find("type")!.AsString;
            JsonValue wire = parsed.Find("wire")!;
            wires[name] = wire;
            t.Check(Json.Write(parsed) == line, name + ": the JSON writer reproduces the line");
            string expected = Json.Write(wire);
            string? actual = RoundTrip(type, wire);
            t.Check(actual == expected, name + ": decodes and re-encodes to the same bytes" +
                                        (actual == expected ? "" : " -- got " + (actual ?? "(not decoded)")));
        }

        Edges(t, wires);
        AuthoringEdges(t, wires);
        OverlayEdges(t, wires);
        Refusals(t);
    }

    // Plan 0066 P2: the overlay vectors decode to exact C# values; a step request without withOverlay (an older
    // client's) decodes to false; malformed overlays are refused as overlayFromJson refuses them.
    private static void OverlayEdges(TestContext t, Dictionary<string, JsonValue> wires)
    {
        var entity = EntityGuid.Parse("58005800-0000-4000-8000-0000000000b1");
        ViewportOverlay empty = Codec.DecodeViewportOverlay(wires["overlay.empty"])!;
        t.Check(empty.Selection.Count == 0 && empty.Groups.Count == 0, "overlay.empty: nothing selected, no groups");
        ViewportOverlay selected = Codec.DecodeViewportOverlay(wires["overlay.selection"])!;
        t.Check(selected.Selection.Count == 2 && selected.Selection[0] == entity && selected.Groups.Count == 0,
                "overlay.selection: two entities, no groups");
        OverlayGroup world = Codec.DecodeViewportOverlay(wires["overlay.world"])!.Groups[0];
        t.Check(world.Anchor is null, "overlay.world: a world group has no anchor");
        t.Equal(new OverlaySegment(Vector3.Zero, new Vector3(1.0f, 0.5f, -2.25f), new OverlayColor(1.0f, 0.25f, 0.0f, 1.0f),
                                   OverlayDepth.Tested, 2.0f), world.Segments[0], "overlay.world: the first segment");
        t.Equal(OverlayDepth.OnTop, world.Segments[1].Depth, "overlay.world: the second segment is on top");
        t.Equal(new OverlayTriangle(Vector3.Zero, Vector3.UnitX, Vector3.UnitY, new OverlayColor(0.2f, 0.4f, 0.6f, 0.75f)),
                world.Triangles[0], "overlay.world: the triangle");
        t.Equal(new OverlayPoint(new Vector3(0.1f, 0.2f, 0.3f), new OverlayColor(1, 1, 1), OverlayDepth.OnTop, 6.5f),
                world.Points[0], "overlay.world: the point");
        ViewportOverlay both = Codec.DecodeViewportOverlay(wires["overlay.screen_constant"])!;
        t.Equal<OverlayAnchor?>(new OverlayAnchor(new Vector3(1.5f, -2.0f, 4.0f), 96.0f), both.Groups[1].Anchor,
                                "overlay.screen_constant: the anchor");
        t.Check(both.Groups[1].Segments.Count == 3, "overlay.screen_constant: three axes");
        var gizmo = new OverlayGroup(new OverlayAnchor(new Vector3(1.5f, -2.0f, 4.0f), 96.0f),
            new[]
            {
                new OverlaySegment(Vector3.Zero, Vector3.UnitX, new OverlayColor(1, 0, 0), OverlayDepth.OnTop, 3.0f),
                new OverlaySegment(Vector3.Zero, Vector3.UnitY, new OverlayColor(0, 1, 0), OverlayDepth.OnTop, 3.0f),
                new OverlaySegment(Vector3.Zero, Vector3.UnitZ, new OverlayColor(0, 0, 1), OverlayDepth.OnTop, 3.0f),
            },
            Array.Empty<OverlayTriangle>(), Array.Empty<OverlayPoint>());
        t.Check(W(Codec.Encode(new ViewportOverlay(new[] { entity }, new[] { world, gizmo }))) == W(wires["overlay.screen_constant"]),
                "a hand-built overlay encodes to overlay.screen_constant's bytes");
        t.Equal(new StepRequest(2, "D:/shots/overlay.png", true), Codec.DecodeStepRequest(wires["step.with_overlay"]),
                "step.with_overlay: withOverlay true");
        t.Equal(new StepRequest(1), Codec.DecodeStepRequest(Json.Parse("{\"frames\":1,\"image\":null}")),
                "a step request without withOverlay: false");
        t.Check(Codec.DecodeStepRequest(Json.Parse("{\"frames\":1,\"image\":null,\"withOverlay\":1}")) is null,
                "refused a step request whose withOverlay is not a bool");

        const string group = "{\"anchor\":null,\"segments\":[],\"triangles\":[],\"points\":[]}";
        string[] bad =
        {
            "{\"selection\":[]}", "{\"groups\":[]}", "[]", "{\"selection\":[1],\"groups\":[]}",
            "{\"selection\":[\"58005800-0000-4000-8000-0000000000B1\"],\"groups\":[]}",
            "{\"selection\":[],\"groups\":[{\"segments\":[],\"triangles\":[],\"points\":[]}]}",
            "{\"selection\":[],\"groups\":[{\"anchor\":{\"position\":[0,0,0]},\"segments\":[],\"triangles\":[],\"points\":[]}]}",
            "{\"selection\":[],\"groups\":[{\"anchor\":null,\"segments\":[],\"triangles\":[]}]}",
            "{\"selection\":[],\"groups\":[{\"anchor\":null,\"segments\":[{\"a\":[0,0,0],\"b\":[1,0,0],\"color\":[1,1,1],\"depth\":\"Tested\",\"width\":1}],\"triangles\":[],\"points\":[]}]}",
            "{\"selection\":[],\"groups\":[{\"anchor\":null,\"segments\":[{\"a\":[0,0,0],\"b\":[1,0,0],\"color\":[1,1,1,1],\"depth\":\"Behind\",\"width\":1}],\"triangles\":[],\"points\":[]}]}",
            "{\"selection\":[],\"groups\":[{\"anchor\":null,\"segments\":[],\"triangles\":[],\"points\":[{\"p\":[0,0,0],\"color\":[1,1,1,1],\"depth\":\"OnTop\",\"size\":\"4\"}]}]}",
            "{\"selection\":[],\"groups\":[{\"anchor\":null,\"segments\":[],\"triangles\":[],\"points\":[{\"p\":[0,0,1e39],\"color\":[1,1,1,1],\"depth\":\"OnTop\",\"size\":4}]}]}",
        };
        foreach (string text in bad)
        {
            t.Check(Codec.DecodeViewportOverlay(Json.Parse(text)) is null, "refused overlay " + text);
        }
        t.Check(Codec.DecodeViewportOverlay(Json.Parse("{\"selection\":[],\"groups\":[" + group + "]}")) is not null,
                "an empty world group decodes");
        t.Throws<ArgumentException>(
            () => Codec.Encode(new ViewportOverlay(Array.Empty<EntityGuid>(),
                new[] { OverlayGroup.World(points: new[] { new OverlayPoint(new Vector3(float.NaN, 0, 0), new OverlayColor(1, 1, 1)) }) })),
            "a non-finite overlay float has no JSON form");
    }

    // Decodes `wire` as `type` and re-encodes it; null if it does not decode.
    private static string? RoundTrip(string type, JsonValue wire)
    {
        switch (type)
        {
            case "PropertyValue": return Codec.DecodePropertyValue(wire) is PropertyValue v ? W(Codec.Encode(v)) : null;
            case "Address": return Codec.DecodeAddress(wire) is PropertyAddress a ? W(Codec.Encode(a)) : null;
            case "Command": return Codec.DecodeCommand(wire) is Command c ? W(Codec.Encode(c)) : null;
            case "Event": return Codec.DecodeEvent(wire) is Event e ? W(Codec.Encode(e)) : null;
            case "Failure": return Codec.DecodeFailure(wire) is CommandFailure f ? W(Codec.Encode(f)) : null;
            case "TransactionTicket":
                return Codec.DecodeTransactionTicket(wire) is TransactionTicket tt ? W(Codec.Encode(tt)) : null;
            case "Filter": return Codec.DecodeFilter(wire) is EventFilter filter ? W(Codec.Encode(filter)) : null;
            case "RuntimeStatus": return Codec.DecodeStatus(wire) is RuntimeStatus s ? W(Codec.Encode(s)) : null;
            case "StepRequest": return Codec.DecodeStepRequest(wire) is StepRequest r ? W(Codec.Encode(r)) : null;
            case "FrameReport": return Codec.DecodeFrameReport(wire) is FrameReport fr ? W(Codec.Encode(fr)) : null;
            case "DiagnosticBatch":
                return Codec.DecodeDiagnosticBatch(wire) is DiagnosticBatch d ? W(Codec.Encode(d)) : null;
            case "Schema": return Codec.DecodeSchema(wire) is List<SchemaType> schema ? W(Codec.EncodeSchema(schema)) : null;
            case "ViewportCamera":
                return Codec.DecodeViewportCamera(wire) is ViewportCamera camera ? W(Codec.Encode(camera)) : null;
            case "Ray": return Codec.DecodeRay(wire) is Ray ray ? W(Codec.Encode(ray)) : null;
            case "RayHit": return Codec.DecodeRayHit(wire) is RayHit hit ? W(Codec.Encode(hit)) : null;
            // Plan 0066 P2: the Viewport overlay (section 6.6).
            case "ViewportOverlay":
                return Codec.DecodeViewportOverlay(wire) is ViewportOverlay overlay ? W(Codec.Encode(overlay)) : null;
            case "SessionFile":
                return SessionFile.TryParse(W(wire), out SessionInfo? session, out _)
                    ? SessionFile.Format(session!).TrimEnd('\n')
                    : null;
            // Plan 0065 P6: the authoring values (section 6.8).
            case "SceneAddress": return Codec.DecodeSceneAddress(wire) is A.SceneAddress sa ? W(Codec.Encode(sa)) : null;
            case "AuthoringCommand":
                return Codec.DecodeAuthoringCommand(wire) is A.Command ac ? W(Codec.Encode(ac)) : null;
            case "AuthoringEvent": return Codec.DecodeAuthoringEvent(wire) is A.Event ae ? W(Codec.Encode(ae)) : null;
            case "AuthoringFilter":
                return Codec.DecodeAuthoringFilter(wire) is A.EventFilter af ? W(Codec.Encode(af)) : null;
            case "AuthoringFailure":
                return Codec.DecodeAuthoringFailure(wire) is A.CommandFailure fail ? W(Codec.Encode(fail)) : null;
            case "Document": return Codec.DecodeDocumentInfo(wire) is A.DocumentInfo doc ? W(Codec.Encode(doc)) : null;
            case "NodeSummary": return Codec.DecodeNodeSummary(wire) is A.NodeSummary ns ? W(Codec.Encode(ns)) : null;
            case "AuthoringError":
                return Codec.DecodeAuthoringError(wire) is A.AuthoringError aerr ? W(Codec.Encode(aerr)) : null;
            case "OpenError": return Codec.DecodeOpenError(wire) is A.OpenError oe ? W(Codec.Encode(oe)) : null;
            case "SaveError": return Codec.DecodeSaveError(wire) is A.SaveError se ? W(Codec.Encode(se)) : null;
            case "ApplyError": return Codec.DecodeApplyError(wire) is A.ApplyError pe ? W(Codec.Encode(pe)) : null;
            case "AuthoringResult":
            {
                if (wire.Find("method") is not { IsString: true } method || wire.Find("result") is not JsonValue result) return null;
                return AuthoringResult(method.AsString, result) is JsonValue back
                    ? W(JsonValue.NewObject().Set("method", JsonValue.String(method.AsString)).Set("result", back))
                    : null;
            }
            case "Floats":
            {
                JsonValue output = JsonValue.NewArray();
                foreach (JsonValue item in wire.Items)
                {
                    if (Codec.DecodeFloat(item) is not float x) return null;
                    output.Push(Codec.EncodeFloat(x));
                }
                return W(output);
            }
            default: return null;
        }
    }

    private static string W(JsonValue value) => Json.Write(value);

    // An authoring.* result (section 5.1), decoded as AuthoringSession decodes it and re-encoded; null if malformed.
    private static JsonValue? AuthoringResult(string method, JsonValue result)
    {
        switch (method)
        {
            case "authoring.open": return Unit(result, v => Codec.DecodeOpenError(v) is A.OpenError e ? Codec.Encode(e) : null);
            case "authoring.apply": return Unit(result, v => Codec.DecodeApplyError(v) is A.ApplyError e ? Codec.Encode(e) : null);
            case "authoring.save": return Unit(result, v => Codec.DecodeSaveError(v) is A.SaveError e ? Codec.Encode(e) : null);
            case "authoring.document":
                return result.IsNull ? JsonValue.Null() : Codec.DecodeDocumentInfo(result) is A.DocumentInfo d ? Codec.Encode(d) : null;
            case "authoring.listNodes":
                return InBand(result, ok => Each(ok, v => Codec.DecodeNodeSummary(v) is A.NodeSummary n ? Codec.Encode(n) : null),
                              AuthoringErrorName);
            case "authoring.listComponents":
                return InBand(result, ok => Each(ok, v => Codec.DecodeTypeId(v) is TypeId c ? Codec.Encode(c) : null),
                              AuthoringErrorName);
            case "authoring.getProperty":
                return InBand(result, ok => Codec.DecodePropertyValue(ok) is PropertyValue p ? Codec.Encode(p) : null,
                              AuthoringErrorName);
            case "authoring.activeCamera":
                return InBand(result, ok => ok.IsNull ? JsonValue.Null() : Codec.DecodeEntity(ok) is EntityGuid c ? Codec.Encode(c) : null,
                              AuthoringErrorName);
            case "authoring.submit":
            case "authoring.subscribe":
                return Codec.DecodeUInt64(result) is ulong n2 ? JsonValue.Number(n2) : null;
            case "authoring.submitTransaction":
                return Codec.DecodeTransactionTicket(result) is TransactionTicket tt ? Codec.Encode(tt) : null;
            case "authoring.unsubscribe": return Unit(result, ConnectionErrorName);
            case "authoring.drainEvents":
                return InBand(result, ok => Each(ok, v => Codec.DecodeAuthoringEvent(v) is A.Event e ? Codec.Encode(e) : null),
                              ConnectionErrorName);
            case "authoring.drainFailures":
                return Each(result, v => Codec.DecodeAuthoringFailure(v) is A.CommandFailure f ? Codec.Encode(f) : null);
            default: return null;
        }
    }

    private static JsonValue? AuthoringErrorName(JsonValue v) => Codec.DecodeAuthoringError(v) is A.AuthoringError e ? Codec.Encode(e) : null;
    private static JsonValue? ConnectionErrorName(JsonValue v) => Codec.DecodeConnectionError(v) is ConnectionError e ? Codec.Encode(e) : null;

    private static JsonValue? InBand(JsonValue result, Func<JsonValue, JsonValue?> ok, Func<JsonValue, JsonValue?> err)
    {
        if (result.Find("ok") is JsonValue value) return ok(value) is JsonValue back ? Codec.Ok(back) : null;
        if (result.Find("err") is JsonValue name) return err(name) is JsonValue back ? Codec.Err(back) : null;
        return null;
    }

    private static JsonValue? Unit(JsonValue result, Func<JsonValue, JsonValue?> err) =>
        InBand(result, ok => ok.IsNull ? JsonValue.Null() : null, err);

    private static JsonValue? Each(JsonValue list, Func<JsonValue, JsonValue?> item)
    {
        if (!list.IsArray) return null;
        JsonValue output = JsonValue.NewArray();
        foreach (JsonValue element in list.Items)
        {
            if (item(element) is not JsonValue back) return null;
            output.Push(back);
        }
        return output;
    }

    // Plan 0065 P6: the authoring vectors' exact C# values.
    private static void AuthoringEdges(TestContext t, Dictionary<string, JsonValue> wires)
    {
        var node = EntityGuid.Parse("52052052-0003-4052-8052-000000000003");
        var parent = EntityGuid.Parse("52052052-0001-4052-8052-000000000001");
        var other = EntityGuid.Parse("52052052-0002-4052-8052-000000000002");
        List<SchemaType> scene = Codec.DecodeSchema(wires["schema.scene"])!;
        SchemaType light = SchemaText.FindType(scene, "Light")!;
        t.Check(light.Name == "asset_system::scene::Light", "the scene schema: Light is asset_system::scene::Light");
        t.Require(SchemaText.TryParsePath(scene, "Light.intensity", out TypeId lightId, out SchemaField? intensity, out _),
                  "the scene schema resolves Light.intensity");
        var address = new A.SceneAddress(node, lightId, intensity!.Id);
        t.Equal(address, Codec.DecodeSceneAddress(wires["authoring.address"]), "a scene address: node 3, Light, intensity");
        t.Equal<A.Command?>(new A.SetProperty(address, 4.5f), Codec.DecodeAuthoringCommand(wires["authoring.command.set"]),
                            "SetProperty");
        t.Equal<A.Command?>(
            new A.CreateNode(EntityGuid.Parse("58005800-0000-4000-8000-0000000000b1"), 2,
                             new[] { lightId, SchemaText.FindType(scene, "Camera")!.Id }),
            Codec.DecodeAuthoringCommand(wires["authoring.command.create"]), "CreateNode with a GUID, a position and two components");
        t.Equal<A.Command?>(new A.CreateNode(null, 0, Array.Empty<TypeId>()),
                            Codec.DecodeAuthoringCommand(wires["authoring.command.create.minted"]), "CreateNode, minted");
        t.Equal<A.Command?>(new A.SetParent(node, null), Codec.DecodeAuthoringCommand(wires["authoring.command.parent.root"]),
                            "SetParent to the root");
        t.Equal<A.Command?>(new A.SetActiveCamera(other), Codec.DecodeAuthoringCommand(wires["authoring.command.camera"]),
                            "SetActiveCamera");
        t.Equal<A.Command?>(new A.SetActiveCamera(null), Codec.DecodeAuthoringCommand(wires["authoring.command.camera.none"]),
                            "SetActiveCamera none");
        t.Equal<A.Event?>(new A.Applied(3), Codec.DecodeAuthoringEvent(wires["authoring.event.applied"]), "Applied{3}");
        t.Equal<A.Event?>(new A.PropertyChanged(address, PropertyValue.Absent),
                          Codec.DecodeAuthoringEvent(wires["authoring.event.changed.absent"]), "PropertyChanged to absent");
        t.Equal<A.Event?>(new A.ParentChanged(node, parent), Codec.DecodeAuthoringEvent(wires["authoring.event.parent"]),
                          "ParentChanged");
        t.Equal<A.Event?>(new A.DocumentReopened(AssetGuid.Parse("52052052-00aa-4052-8052-0000000000aa")),
                          Codec.DecodeAuthoringEvent(wires["authoring.event.reopened"]), "DocumentReopened");
        t.Equal(new A.EventFilter(), Codec.DecodeAuthoringFilter(wires["authoring.filter.all"]), "the default filter: all nine kinds");
        t.Equal(new A.EventFilter(A.EventKind.Applied), Codec.DecodeAuthoringFilter(wires["authoring.filter.applied"]),
                "an Applied-only filter");
        t.Equal(new A.EventFilter(A.EventKind.PropertyChanged | A.EventKind.ComponentAdded, node, lightId),
                Codec.DecodeAuthoringFilter(wires["authoring.filter.property"]), "a node and component filter");
        t.Equal(new A.CommandFailure(new CommandTicket(9), A.AuthoringError.ParentAcyclic),
                Codec.DecodeAuthoringFailure(wires["authoring.failure"]), "a refusal");
        t.Equal(new A.DocumentInfo(AssetGuid.Parse("52052052-00aa-4052-8052-0000000000aa"), A.Provenance.HandAuthored,
                                   "D:/Atlantis/assets/scenes/a \"b\" é.scene.txt", 5),
                Codec.DecodeDocumentInfo(wires["authoring.document"]), "a hand-authored document (escapes, non-ASCII)");
        t.Equal(A.Provenance.Imported, Codec.DecodeDocumentInfo(wires["authoring.document.imported"])?.Provenance, "an imported document");
        t.Equal(new A.NodeSummary(parent, null), Codec.DecodeNodeSummary(wires["authoring.node.root"]), "a root node");

        // Every name of every error set is a C# enumerator, in the same order.
        int index = 0;
        foreach (A.AuthoringError error in Enum.GetValues<A.AuthoringError>())
        {
            string name = "authoring.error." + error;
            t.Check(wires.ContainsKey(name) && Codec.DecodeAuthoringError(wires[name]) == error, "the refusal " + error);
            ++index;
        }
        t.Equal(24, index, "the AuthoringError set has the C++ set's 24 names");
        foreach (A.OpenError error in Enum.GetValues<A.OpenError>())
        {
            t.Check(Codec.DecodeOpenError(wires["authoring.open_error." + error]) == error, "the open error " + error);
        }
        foreach (A.SaveError error in Enum.GetValues<A.SaveError>())
        {
            t.Check(Codec.DecodeSaveError(wires["authoring.save_error." + error]) == error, "the save error " + error);
        }
        foreach (A.ApplyError error in Enum.GetValues<A.ApplyError>())
        {
            t.Check(Codec.DecodeApplyError(wires["authoring.apply_error." + error]) == error, "the apply error " + error);
        }

        // Malformed authoring values are refused as the C++ decoders refuse them.
        t.Check(Codec.DecodeAuthoringCommand(Json.Parse("{\"kind\":\"Teleport\"}")) is null, "refused an unknown authoring command");
        t.Check(Codec.DecodeAuthoringCommand(Json.Parse("{\"kind\":\"SetParent\",\"node\":\"" + node + "\"}")) is null,
                "refused a SetParent without its parent member");
        t.Check(Codec.DecodeAuthoringCommand(Json.Parse("{\"kind\":\"CreateNode\",\"node\":null,\"position\":-1,\"components\":[]}"))
                is null, "refused a negative position");
        t.Check(Codec.DecodeAuthoringFilter(Json.Parse("{\"kinds\":[\"Moved\"],\"node\":null,\"component\":null}")) is null,
                "refused an unknown authoring event kind");
        t.Check(Codec.DecodeAuthoringError(JsonValue.String("NotAnError")) is null, "refused an unknown error name");
        t.Check(Codec.DecodeDocumentInfo(Json.Parse("{\"scene\":\"52052052-00aa-4052-8052-0000000000aa\",\"provenance\":\"Copied\"," +
                                                    "\"source\":\"x\",\"nodeCount\":1}")) is null, "refused an unknown provenance");
    }

    private static PropertyValue Value(Dictionary<string, JsonValue> wires, string name) =>
        Codec.DecodePropertyValue(wires[name]) ?? throw new AssertionFailed(name + " does not decode");

    private static uint Bits(float x) => BitConverter.SingleToUInt32Bits(x);

    // The hand-written expectations for the edge cases (J3).
    private static void Edges(TestContext t, Dictionary<string, JsonValue> wires)
    {
        t.Equal(0UL, Value(wires, "value.u64.zero").AsUInt64(), "u64 zero");
        t.Equal(ulong.MaxValue, Value(wires, "value.u64.max").AsUInt64(), "u64 max is UINT64_MAX");
        t.Equal(0x80000000u, Bits(Value(wires, "value.f32.negative_zero").AsFloat32()), "f32 -0 keeps its sign bit");
        t.Equal(Bits(0.1f), Bits(Value(wires, "value.f32.tenth").AsFloat32()), "f32 0.1");
        t.Equal(Bits(1.0f / 3.0f), Bits(Value(wires, "value.f32.third").AsFloat32()), "f32 1/3");
        t.Equal(Bits(1e20f), Bits(Value(wires, "value.f32.large").AsFloat32()), "f32 1e20");
        t.Equal(Bits(float.MaxValue), Bits(Value(wires, "value.f32.max").AsFloat32()), "f32 max");
        t.Equal(Bits(float.MinValue), Bits(Value(wires, "value.f32.lowest").AsFloat32()), "f32 lowest");
        t.Equal(0x00800000u, Bits(Value(wires, "value.f32.min_normal").AsFloat32()), "f32 smallest normal");
        t.Check(float.IsNaN(Value(wires, "value.f32.nan").AsFloat32()), "f32 nan");
        t.Check(float.IsPositiveInfinity(Value(wires, "value.f32.inf").AsFloat32()), "f32 +inf");
        t.Check(float.IsNegativeInfinity(Value(wires, "value.f32.negative_inf").AsFloat32()), "f32 -inf");
        Vector3 nan3 = Value(wires, "value.vec3.nan").AsVec3();
        t.Check(float.IsNaN(nan3.X) && Bits(nan3.Y) == 0 && Bits(nan3.Z) == 0x80000000u, "vec3 [nan, 0, -0]");
        t.Equal(new Vector4(1.5f, 2.0f, -1.5f, 1.0f), Value(wires, "value.vec4").AsVec4(), "vec4");
        t.Equal(AssetGuid.Parse("52052052-00aa-4052-8052-0000000000aa"), Value(wires, "value.asset_guid").AsAsset(), "asset GUID");
        t.Check(Value(wires, "value.asset_guid.nil").AsAsset().IsNil, "the nil asset GUID");
        t.Equal(EntityGuid.Parse("58005800-0000-4000-8000-0000000000b1"), Value(wires, "value.entity_guid").AsEntity(),
                "entity GUID");
        t.Check(Value(wires, "value.entity_guid.nil").AsEntity().IsNil, "the nil entity GUID");
        t.Equal(-2L, Value(wires, "value.enum.negative").AsEnum(), "a negative enum");
        t.Equal(long.MaxValue, Value(wires, "value.enum.max").AsEnum(), "enum INT64_MAX");
        t.Equal(long.MinValue, Value(wires, "value.enum.min").AsEnum(), "enum INT64_MIN");
        t.Check(Value(wires, "value.absent").IsAbsent, "absent");

        t.Equal(new PropertyAddress(EntityGuid.Parse("58005800-0000-4000-8000-0000000000b1"), new TypeId(0x8325934757106b8dUL),
                                    new FieldId(0x000145df4f8a3ceeUL)),
                Codec.DecodeAddress(wires["address"]), "an address: entity, Light, intensity");
        t.Check(Codec.DecodeCommand(wires["command.set.nan"]) is SetProperty { Value.Kind: PropertyKind.Float32 } set &&
                float.IsNaN(set.Value.AsFloat32()), "a SetProperty carrying NaN");
        t.Equal(new TransactionTicket(new CommandTicket(7), 11), Codec.DecodeTransactionTicket(wires["ticket"]), "a ticket");
        t.Equal(new TransactionTicket(default, 0), Codec.DecodeTransactionTicket(wires["ticket.empty"]), "the empty ticket");
        t.Equal(new CommandFailure(new CommandTicket(12), AccessError.NonFiniteValue), Codec.DecodeFailure(wires["failure.non_finite"]),
                "a failure");
        t.Equal(EventKind.None, Codec.DecodeFilter(wires["filter.none"])!.Kinds, "the empty filter");
        t.Equal(new EventFilter(), Codec.DecodeFilter(wires["filter.all"]), "the default filter");
        EventFilter property = Codec.DecodeFilter(wires["filter.property"])!;
        t.Check(property.Kinds == EventKind.PropertyChanged && property.Component == new TypeId(0x8325934757106b8dUL) &&
                property.Entity == EntityGuid.Parse("52052052-0002-4052-8052-000000000002"), "a property filter");
        t.Equal(new StepRequest(3, "C:\\captures\\beacon \"k\" é.png"), Codec.DecodeStepRequest(wires["step.image"]),
                "a step request's image path (escapes, non-ASCII)");
        DiagnosticBatch batch = Codec.DecodeDiagnosticBatch(wires["diagnostics"])!;
        t.Check(batch.Entries.Count == 3 && batch.Entries[1].Message == "tab\there, quote \" and \u0001 control" &&
                batch.Entries[2].Message == "non-ASCII: 光" && batch.Entries[2].Severity == DiagnosticSeverity.Fatal &&
                batch.Dropped == 9, "diagnostics: escapes and non-ASCII text");
        List<SchemaType> schema = Codec.DecodeSchema(wires["schema.world"])!;
        SchemaType light = SchemaText.FindType(schema, "Light")!;
        t.Check(light.Name == "world::Light" && light.Fields.Count == 4 && light.Fields[0].Kind == TypeKind.Enum,
                "the World schema: Light");
        t.Equal(FieldFlags.Serializable | FieldFlags.Editable | FieldFlags.AssetReference | FieldFlags.Optional,
                SchemaText.FindType(schema, "Renderable")!.Fields[1].Flags, "Renderable.materialAsset's flags");

        // The float sweep: the C# text is the C++ text for every value.
        JsonValue sweep = wires["float.sweep"];
        int same = 0;
        foreach (JsonValue item in sweep.Items)
        {
            float x = Codec.DecodeFloat(item)!.Value;
            if (FloatText.Format(x) == item.NumberText) ++same;
            else t.Fail("float text of " + item.NumberText + " -- C# writes " + FloatText.Format(x));
        }
        t.Check(same == sweep.Items.Count, "float text: all " + sweep.Items.Count + " sweep values as std::to_chars writes them");
    }

    // What the C++ decoders refuse, C# refuses.
    private static void Refusals(TestContext t)
    {
        string[] badValues =
        {
            "{\"u64\":\"-1\"}", "{\"u64\":\"18446744073709551616\"}", "{\"u64\":1}", "{\"u64\":\"\"}", "{\"u64\":\" 1\"}",
            "{\"f32\":\"NaN\"}", "{\"f32\":1e39}", "{\"f32\":true}", "{\"vec3\":[1,2]}", "{\"vec4\":[1,2,3]}",
            "{\"enum\":1.5}", "{\"enum\":\"1\"}", "{\"enum\":9223372036854775808}",
            "{\"entityGuid\":\"58005800-0000-4000-8000-0000000000B1\"}", "{\"entityGuid\":\"{58005800-0000-4000-8000-0000000000b1}\"}",
            "{\"assetGuid\":\"5800580000004000800000000000000b1\"}", "{\"absent\":false}", "{\"f32\":1,\"u64\":\"1\"}", "{}",
            "{\"vec2\":[1,2]}", "[1]",
        };
        foreach (string bad in badValues)
        {
            t.Check(Codec.DecodePropertyValue(Json.Parse(bad)) is null, "refused PropertyValue " + bad);
        }
        foreach (string bad in new[] { "\"0x123\"", "\"0X8325934757106b8d\"", "\"0x8325934757106B8D\"", "12" })
        {
            t.Check(Codec.DecodeId(Json.Parse(bad)) is null, "refused id " + bad);
        }
        t.Check(Codec.DecodeCommand(Json.Parse("{\"kind\":\"Teleport\",\"entity\":\"58005800-0000-4000-8000-0000000000b1\"}")) is null,
                "refused an unknown command kind");
        t.Check(Codec.DecodeFilter(Json.Parse("{\"kinds\":[\"Teleported\"],\"entity\":null,\"component\":null}")) is null,
                "refused an unknown event kind in a filter");
        t.Check(Codec.DecodeStepRequest(Json.Parse("{\"frames\":4294967296,\"image\":null}")) is null,
                "refused a step over UINT32_MAX frames");
        t.Throws<FormatException>(() => Json.Parse("{\"a\":1,}"), "JSON: a trailing comma is refused");
        t.Throws<FormatException>(() => Json.Parse("{\"a\":1} x"), "JSON: text after the value is refused");
        t.Throws<FormatException>(() => Json.Parse("// c\n{}"), "JSON: comments are refused");
        t.Throws<ArgumentException>(() => JsonValue.Number(float.NaN), "a NaN has no JSON number form");
    }
}
