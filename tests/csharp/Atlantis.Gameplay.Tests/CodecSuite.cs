// Plan 0058 P3/P9 (Spec 0058 R6; J3): the "codec" suite. Every committed
// conformance vector (tests/csharp/conformance/codec_vectors.jsonl, written by
// the C++ codec) decodes, and re-encodes to the same bytes; a hand-written
// table pins the edge cases to exact C# values; malformed values are refused
// as the C++ decoders refuse them.
using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Text;
using Atlantis.Gameplay.Remote;

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
        Refusals(t);
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
            case "SessionFile":
                return SessionFile.TryParse(W(wire), out SessionInfo? session, out _)
                    ? SessionFile.Format(session!).TrimEnd('\n')
                    : null;
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
