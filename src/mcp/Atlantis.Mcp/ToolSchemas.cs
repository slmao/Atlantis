using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Atlantis.Mcp;

/// <summary>
/// Argument shapes from atlantis.remote/1, sections 6.2/6.3/6.6/6.8. No schema IDs, GUID/domain
/// checks, scene validation or limits are duplicated here: those decisions remain with the Runtime.
/// Declarations are built once, then read concurrently without mutation.
/// </summary>
internal static class ToolSchemas
{
    internal static readonly JsonNode Integer = Type("integer");
    private static readonly JsonNode String = Type("string");
    private static readonly JsonNode Boolean = Type("boolean");
    private static readonly JsonNode Number = Type("number");
    private static readonly JsonNode Float = Choice(Number, Enum("nan", "inf", "-inf"));
    private static readonly JsonNode Vec3 = Vector(Float, 3);
    private static readonly JsonNode Vec4 = Vector(Float, 4);
    private static readonly JsonNode Value = Choice(
        Object(("u64", String)), Object(("f32", Float)), Object(("vec3", Vec3)), Object(("vec4", Vec4)),
        Object(("assetGuid", String)), Object(("entityGuid", String)), Object(("enum", Integer)),
        Object(("absent", new JsonObject { ["const"] = true })));

    internal static readonly JsonNode WorldAddress = Object(("entity", String), ("component", String), ("field", String));
    internal static readonly JsonNode SceneAddress = Object(("node", String), ("component", String), ("field", String));
    internal static readonly JsonNode WorldCommand = Choice(
        Command("CreateEntity", ("entity", String)), Command("DestroyEntity", ("entity", String)),
        Command("AddComponent", ("entity", String), ("component", String)),
        Command("RemoveComponent", ("entity", String), ("component", String)),
        Command("SetProperty", ("address", WorldAddress), ("value", Value)));
    internal static readonly JsonNode AuthoringCommand = Choice(
        Command("SetProperty", ("address", SceneAddress), ("value", Value)),
        Command("CreateNode", ("node", Nullable(String)), ("position", Integer), ("components", Array(String))),
        Command("DestroyNode", ("node", String)),
        Command("AddComponent", ("node", String), ("component", String)),
        Command("RemoveComponent", ("node", String), ("component", String)),
        Command("SetParent", ("node", String), ("parent", Nullable(String))),
        Command("SetActiveCamera", ("camera", Nullable(String))));

    internal static readonly JsonNode StepRequest = ObjectWithOptionalOverlay();
    internal static readonly JsonNode Ray = Object(("origin", Vec3), ("direction", Vec3));
    internal static readonly JsonNode Camera = Object(("worldMatrix", Vector(Float, 16)),
        ("fovYRadians", Float), ("nearZ", Float), ("farZ", Float));
    private static readonly JsonNode Depth = Enum("Tested", "OnTop");
    private static readonly JsonNode Segment = Object(("a", Vector(Number, 3)), ("b", Vector(Number, 3)),
        ("color", Vector(Number, 4)), ("depth", Depth), ("width", Number));
    private static readonly JsonNode Triangle = Object(("a", Vector(Number, 3)), ("b", Vector(Number, 3)),
        ("c", Vector(Number, 3)), ("color", Vector(Number, 4)), ("depth", Depth));
    private static readonly JsonNode Point = Object(("p", Vector(Number, 3)), ("color", Vector(Number, 4)),
        ("depth", Depth), ("size", Number));
    private static readonly JsonNode Group = Object(
        ("anchor", Nullable(Object(("position", Vector(Number, 3)), ("pixelsPerUnit", Number)))),
        ("segments", Array(Segment)), ("triangles", Array(Triangle)), ("points", Array(Point)));
    internal static readonly JsonNode Overlay = Object(("selection", Array(String)), ("groups", Array(Group)));

    internal static JsonElement Parameters(params (string Name, JsonNode Shape)[] fields) =>
        JsonSerializer.SerializeToElement(Object(fields));

    private static JsonObject Object(params (string Name, JsonNode Shape)[] fields)
    {
        JsonObject properties = new();
        JsonArray required = new();
        foreach (var (name, shape) in fields)
        {
            properties[name] = shape.DeepClone();
            required.Add(name);
        }
        return new JsonObject
        {
            ["type"] = "object", ["properties"] = properties,
            ["required"] = required, ["additionalProperties"] = false,
        };
    }

    private static JsonNode Command(string kind, params (string Name, JsonNode Shape)[] fields) =>
        Object(new[] { ("kind", (JsonNode)new JsonObject { ["const"] = kind }) }.Concat(fields).ToArray());

    private static JsonNode ObjectWithOptionalOverlay()
    {
        JsonObject shape = Object(("frames", Integer), ("image", Nullable(String)));
        shape["properties"]!["withOverlay"] = Boolean.DeepClone();
        return shape;
    }

    private static JsonNode Type(string type) => new JsonObject { ["type"] = type };
    internal static JsonNode Array(JsonNode item) => new JsonObject { ["type"] = "array", ["items"] = item.DeepClone() };
    private static JsonNode Vector(JsonNode number, int count)
    {
        JsonNode array = Array(number);
        array["minItems"] = count;
        array["maxItems"] = count;
        return array;
    }
    internal static JsonNode Nullable(JsonNode value) => Choice(value, Type("null"));
    private static JsonNode Choice(params JsonNode[] values) => new JsonObject
    {
        ["oneOf"] = new JsonArray(values.Select(v => v.DeepClone()).ToArray()),
    };
    private static JsonNode Enum(params string[] values) => new JsonObject
    {
        ["type"] = "string", ["enum"] = new JsonArray(values.Select(v => (JsonNode?)JsonValue.Create(v)).ToArray()),
    };

    // The small structural subset used by the declarations above. Using the discovery schema here
    // keeps argument validation and discovery together; semantic refusals still cross the wire.
    internal static bool Matches(JsonElement shape, JsonElement value)
    {
        if (shape.TryGetProperty("oneOf", out JsonElement choices))
            return choices.EnumerateArray().Count(c => Matches(c, value)) == 1;
        if (shape.TryGetProperty("const", out JsonElement constant)) return JsonElement.DeepEquals(constant, value);
        if (shape.TryGetProperty("enum", out JsonElement variants)
            && !variants.EnumerateArray().Any(v => JsonElement.DeepEquals(v, value))) return false;
        switch (shape.GetProperty("type").GetString())
        {
            case "null": return value.ValueKind == JsonValueKind.Null;
            case "string": return value.ValueKind == JsonValueKind.String;
            case "boolean": return value.ValueKind is JsonValueKind.True or JsonValueKind.False;
            case "number": return value.ValueKind == JsonValueKind.Number;
            case "integer":
                return value.ValueKind == JsonValueKind.Number
                    && (value.TryGetInt64(out _) || value.TryGetUInt64(out _));
            case "array":
                if (value.ValueKind != JsonValueKind.Array) return false;
                int count = value.GetArrayLength();
                if (shape.TryGetProperty("minItems", out JsonElement min) && count < min.GetInt32()) return false;
                if (shape.TryGetProperty("maxItems", out JsonElement max) && count > max.GetInt32()) return false;
                JsonElement items = shape.GetProperty("items");
                return value.EnumerateArray().All(v => Matches(items, v));
            case "object":
                if (value.ValueKind != JsonValueKind.Object) return false;
                JsonElement properties = shape.GetProperty("properties");
                foreach (JsonElement required in shape.GetProperty("required").EnumerateArray())
                    if (!value.TryGetProperty(required.GetString()!, out _)) return false;
                return value.EnumerateObject().All(p => properties.TryGetProperty(p.Name, out JsonElement child)
                    && Matches(child, p.Value));
            default: return false;
        }
    }
}
