using System;
using System.Collections.Generic;
using A = Atlantis.Gameplay.Assets;

namespace Atlantis.Gameplay.Remote;

public static partial class Codec
{
    private static string AssetTypeText(A.AssetType type) => type switch
    {
        A.AssetType.Mesh => "mesh", A.AssetType.Texture => "texture", A.AssetType.Material => "material",
        A.AssetType.Scene => "scene", A.AssetType.Environment => "environment", _ => throw new ArgumentOutOfRangeException(nameof(type)),
    };
    public static JsonValue Encode(A.AssetSummary v) => JsonValue.NewObject().Set("guid", Encode(v.Guid))
        .Set("type", JsonValue.String(AssetTypeText(v.Type))).Set("source", JsonValue.String(v.Source));
    public static JsonValue Encode(A.AssetDetails v)
    {
        JsonValue result = Encode(new A.AssetSummary(v.Guid, v.Type, v.Source));
        JsonValue deps = JsonValue.NewArray();
        foreach (AssetGuid guid in v.Dependencies) deps.Push(Encode(guid));
        return result.Set("artifactSchema", JsonValue.Number((ulong)v.ArtifactSchema))
            .Set("sourceSchema", v.SourceSchema is uint n ? JsonValue.Number((ulong)n) : JsonValue.Null())
            .Set("tool", JsonValue.String(v.Tool)).Set("dependencies", deps);
    }
    public static A.AssetSummary? DecodeAssetSummary(JsonValue v)
    {
        if (v.Find("guid") is not JsonValue g || DecodeAsset(g) is not AssetGuid guid || guid.IsNil ||
            v.Find("type") is not { IsString: true } t || v.Find("source") is not { IsString: true } s) return null;
        A.AssetType? type = t.AsString switch { "mesh" => A.AssetType.Mesh, "texture" => A.AssetType.Texture,
            "material" => A.AssetType.Material, "scene" => A.AssetType.Scene, "environment" => A.AssetType.Environment, _ => null };
        return type is A.AssetType known ? new A.AssetSummary(guid, known, s.AsString) : null;
    }
    public static A.AssetDetails? DecodeAssetDetails(JsonValue v)
    {
        A.AssetSummary? s = DecodeAssetSummary(v);
        if (s is null || v.Find("artifactSchema") is not JsonValue a || !a.TryGetUInt64(out ulong artifact) || artifact > uint.MaxValue ||
            v.Find("sourceSchema") is not JsonValue source || v.Find("tool") is not { IsString: true } tool ||
            v.Find("dependencies") is not { IsArray: true } d) return null;
        uint? schema = null;
        if (!source.IsNull)
        {
            if (!source.TryGetUInt64(out ulong n) || n > uint.MaxValue) return null;
            schema = (uint)n;
        }
        var deps = new List<AssetGuid>();
        foreach (JsonValue item in d.Items)
        {
            if (DecodeAsset(item) is not AssetGuid guid || guid.IsNil || deps.Count > 0 &&
                string.CompareOrdinal(deps[^1].ToString(), guid.ToString()) >= 0) return null;
            deps.Add(guid);
        }
        return new A.AssetDetails(s.Guid, s.Type, s.Source, (uint)artifact, schema, tool.AsString, deps);
    }
    public static JsonValue Encode(A.AssetError e) => JsonValue.String(e.ToString());
    public static A.AssetError? DecodeAssetError(JsonValue v) => v.IsString ? v.AsString switch
    { "CatalogUnavailable" => A.AssetError.CatalogUnavailable, "UnknownAsset" => A.AssetError.UnknownAsset, _ => null } : null;
}
