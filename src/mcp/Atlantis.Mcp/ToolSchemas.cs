using Atlantis.Gameplay;
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
    internal static readonly JsonNode String = Type("string");
    private static readonly JsonNode Boolean = Type("boolean");
    private static readonly JsonNode Number = Type("number");
    private static readonly JsonNode Float = Choice(Number, Enum("nan", "inf", "-inf"));
    private static readonly JsonNode Vec3 = Vector(Float, 3);
    private static readonly JsonNode Vec4 = Vector(Float, 4);
    internal static readonly JsonNode Value = Choice(
        Object(("u64", String)), Object(("f32", Float)), Object(("vec3", Vec3)), Object(("vec4", Vec4)),
        Object(("assetGuid", String)), Object(("entityGuid", String)), Object(("enum", Integer)),
        Object(("absent", new JsonObject { ["const"] = true })));

    internal static readonly JsonNode WorldAddress = Object(("entity", String), ("component", String), ("field", String));
    internal static readonly JsonNode SceneAddress = Object(("node", String), ("component", String), ("field", String));
    private static readonly JsonNode RenderLookGuard = Command("RequireRenderLookDocument",
        ("expectedDocument", Object(("documentId", String), ("documentRevision", Integer))),
        ("activeCamera", String), ("lightRoster", Array(String)), ("placementSignature", String));
    private static readonly JsonNode RenderLookWrite = Command("SetProperty", ("address", SceneAddress), ("value", Value));
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
        Command("SwapAdjacentSiblings", ("node", String), ("sibling", String), ("parent", Nullable(String)),
            ("direction", Enum("Up", "Down")), ("expectedRevision", Integer)),
        Command("MoveSiblingRelative", ("node", String), ("anchor", String), ("parent", Nullable(String)),
            ("placement", Enum("Before", "After")), ("expectedRevision", Integer)),
        Command("RequireDocumentRevision", ("expectedRevision", Integer)),
        Command("RequireTransformBatchRevision", ("expectedRevision", Integer)),
        Command("RequireDuplicateBatchRevision", ("expectedRevision", Integer)),
        Command("RequireNameBatchDocument", ("expectedDocument", Object(("documentId", String), ("documentRevision", Integer)))),
        RenderLookGuard,
        Command("SetActiveCamera", ("camera", Nullable(String))),
        Command("SetNodeName", ("node", String), ("name", String)),
        Command("SetPrefabOrigin", ("node", String), ("prefab", String)));

    internal static readonly JsonNode StepRequest = ObjectWithOptionalOverlay();
    internal static readonly JsonNode VisibilityPolicy = Nullable(Object(
        ("hidden", Array(String)), ("locked", Array(String)), ("isolated", Nullable(Array(String)))));
    internal static readonly JsonNode ViewportPickKind = Enum("Selection", "Alignment");
    internal static readonly JsonNode AlignmentRays = new JsonObject
    {
        ["type"] = "array", ["items"] = Object(("origin", Vector(Number, 3)), ("direction", Vector(Number, 3))),
        ["minItems"] = 1, ["maxItems"] = 2
    };
    internal static readonly JsonNode Ray = Object(("origin", Vec3), ("direction", Vec3));
    internal static readonly JsonNode NavigationTargets = new JsonObject
    {
        ["type"] = "array", ["items"] = String.DeepClone(), ["maxItems"] = ViewportNavigation.MaxTargets,
    };
    internal static readonly JsonNode Camera = Object(("worldMatrix", Vector(Float, 16)),
        ("fovYRadians", Float), ("nearZ", Float), ("farZ", Float));
    internal static JsonElement MaterialDiagnosticCapture()
    {
        var fields = Object(("document", Object(("documentId", String), ("documentRevision", Integer))),
            ("applyId", String), ("generation", Integer), ("camera", Camera),
            ("extent", Object(("width", Integer), ("height", Integer))),
            ("target", Object(("entity", String), ("mesh", String), ("material", String))),
            ("outputs", Vector(String, 2)), ("viewport", Object(("sceneGeneration", Integer), ("revision", Integer))));
        fields["required"] = new JsonArray("document", "applyId", "generation", "camera", "extent", "target", "outputs");
        return JsonSerializer.SerializeToElement(fields);
    }

    internal static JsonElement RenderLookCapture()
    {
        var fields = Object(("guard", RenderLookGuard), ("applyId", String), ("generation", Integer), ("camera", Camera),
            ("looks", Vector(Array(RenderLookWrite), 2)), ("outputs", Vector(String, 2)),
            ("extent", Object(("width", Integer), ("height", Integer))));
        fields["required"] = new JsonArray("guard", "applyId", "generation", "camera", "looks", "outputs");
        return JsonSerializer.SerializeToElement(fields);
    }
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

    internal static JsonNode ParametersNode(params (string Name, JsonNode Shape)[] fields) => Object(fields);

    internal static readonly JsonNode PrefabDocument = Object(("documentId", String), ("documentRevision", Integer));
    internal static readonly JsonNode PrefabSubtree = Object(("source", String), ("parent", Nullable(String)),
        ("slots", Array(Integer)), ("order", String));

    internal static JsonElement MaterialBindingParameters()
    {
        var fields = Object(("ray", Object(("origin", Vector(Number, 3)), ("direction", Vector(Number, 3)))),
            ("minDistance", Number), ("maxDistance", Number), ("sceneGeneration", Integer),
            ("viewport", Object(("sceneGeneration", Integer), ("revision", Integer))));
        fields["required"] = new JsonArray("ray", "minDistance", "maxDistance", "sceneGeneration");
        return JsonSerializer.SerializeToElement(fields);
    }

    internal static JsonElement PrefabSyncParameters()
    {
        JsonObject shape = Object(("node", String), ("expected", PrefabDocument), ("revision", String), ("prefab", String));
        shape["required"] = new JsonArray("node", "expected", "revision");
        return JsonSerializer.SerializeToElement(shape);
    }

    internal static JsonElement ApplyStatusParameters()
    {
        JsonObject shape = Object(("id", Nullable(String))); shape["required"] = new JsonArray();
        return JsonSerializer.SerializeToElement(shape);
    }
    internal static JsonElement ImportParameters() =>
        Parameters(("name", String), ("mode", Enum("Register", "Resume")));

    private static readonly JsonNode MaterialValues = Object(("texture", String), ("color", Vector(Number, 3)),
        ("metallic", Number), ("roughness", Number));

    private static readonly JsonNode MaterialValuesWithMaps = Object(("texture", String), ("color", Vector(Number, 3)),
        ("metallic", Number), ("roughness", Number), ("normalMap", Nullable(String)),
        ("emissiveTexture", Nullable(String)), ("emissiveFactor", Vector(Number, 3)));

    private static readonly JsonNode MaterialValuesWithMetallicRoughness = Object(("texture", String), ("color", Vector(Number, 3)),
        ("metallic", Number), ("roughness", Number), ("normalMap", Nullable(String)),
        ("emissiveTexture", Nullable(String)), ("emissiveFactor", Vector(Number, 3)), ("metallicRoughnessMap", Nullable(String)));

    private static readonly JsonNode MaterialValuesWithOcclusion = Object(("texture", String), ("color", Vector(Number, 3)),
        ("metallic", Number), ("roughness", Number), ("normalMap", Nullable(String)),
        ("emissiveTexture", Nullable(String)), ("emissiveFactor", Vector(Number, 3)),
        ("metallicRoughnessMap", Nullable(String)), ("occlusionMap", Nullable(String)));

    internal static JsonElement MaterialDraftParameters(bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false) => Parameters(("mesh", String),
        ("recipe", Object(("values", withOcclusion ? MaterialValuesWithOcclusion : withMetallicRoughness ? MaterialValuesWithMetallicRoughness : withMaps ? MaterialValuesWithMaps : MaterialValues), ("filter", Enum("Nearest", "Linear")),
                          ("address", Enum("Repeat", "ClampToEdge")))));

    internal static JsonElement MaterialSaveParameters(bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false)
    {
        var values = withOcclusion ? MaterialValuesWithOcclusion : withMetallicRoughness ? MaterialValuesWithMetallicRoughness : withMaps ? MaterialValuesWithMaps : MaterialValues;
        return JsonSerializer.SerializeToElement(new JsonObject { ["type"] = "object", ["oneOf"] = Choice(
            Object(("mode", Enum("Create")), ("name", String), ("values", values)),
            Object(("mode", Enum("Copy")), ("name", String), ("material", String), ("values", values)),
            Object(("mode", Enum("Edit")), ("material", String), ("activeGeneration", Nullable(String)),
                ("expected", values), ("values", values)),
            Object(("mode", Enum("Resume")), ("material", String)))["oneOf"]!.DeepClone() });
    }

    internal static JsonElement TextureRemovalParameters(bool confirm) => confirm
        ? Parameters(("asset", String), ("expectedGeneration", String), ("checkId", String))
        : Parameters(("asset", String), ("expectedGeneration", String));

    internal static JsonElement StaticGroupReviewParameters() => JsonSerializer.SerializeToElement(new JsonObject
    {
        ["type"] = "object",
        ["oneOf"] = Choice(Object(("mode", Enum("begin")), ("scene", String)),
            Object(("mode", Enum("read", "cancel")), ("id", String)))["oneOf"]!.DeepClone()
    });

    internal static JsonElement RebuildStatusParameters()
    {
        JsonObject shape = Object(("jobId", String));
        shape["required"] = new JsonArray();
        return JsonSerializer.SerializeToElement(shape);
    }

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
