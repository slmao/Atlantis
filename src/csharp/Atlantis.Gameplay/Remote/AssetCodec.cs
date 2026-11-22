using System;
using System.Linq;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Numerics;
using System.Text;
using A = Atlantis.Gameplay.Assets;

namespace Atlantis.Gameplay.Remote;

public static partial class Codec
{
    public static AssetGuid[]? DecodeContentMetricsGuids(JsonValue? input)
    {
        if (input is not { IsArray: true } || input.Items.Count is 0 or > A.ContentMetrics.Limit) return null;
        var guids = new List<AssetGuid>(input.Items.Count);
        foreach (var item in input.Items)
        {
            if (DecodeProjectGuid(item) is not AssetGuid guid || guids.Contains(guid)) return null;
            guids.Add(guid);
        }
        return guids.ToArray();
    }

    public static A.ContentMetricsError? DecodeContentMetricsError(JsonValue value) =>
        value is { IsString: true } ? value.AsString switch
        {
            "Unsupported" => A.ContentMetricsError.Unsupported,
            "CatalogUnavailable" => A.ContentMetricsError.CatalogUnavailable,
            _ => null
        } : null;

    public static A.ContentMetrics? DecodeContentMetrics(JsonValue value, IReadOnlyList<AssetGuid> requested)
    {
        if (!Exact(value, 1) || value.Find("rows") is not { IsArray: true } rows ||
            rows.Items.Count != requested.Count || requested.Count is 0 or > A.ContentMetrics.Limit ||
            Encoding.UTF8.GetByteCount(Json.Write(value)) > 16 * 1024) return null;
        var decoded = new List<A.ContentMetricRow>(requested.Count);
        foreach (var row in rows.Items)
        {
            if (!Exact(row, 2) || row.Find("guid") is not { } id ||
                DecodeProjectGuid(id) is not AssetGuid guid || requested[decoded.Count] != guid) return null;
            A.ContentMetric result;
            if (row.Find("mesh") is { } mesh && Exact(mesh, 2) &&
                ContentU32(mesh, "artifactSchema") is uint schema && schema is 4 or 5 && ContentU32(mesh, "indexedTriangles") is uint triangles &&
                triangles <= uint.MaxValue / 3)
            {
                result = new A.MeshContentMetrics(schema, triangles);
            }
            else if (row.Find("texture") is { } texture && Exact(texture, 6) && ContentU32(texture, "artifactSchema") == 3 &&
                ContentU32(texture, "width") is uint width && width <= 8192 &&
                ContentU32(texture, "height") is uint height && height <= 8192 &&
                RebuildToken<A.ContentTextureLayout>(texture.Find("layout")) is { } layout &&
                ContentU32(texture, "mipCount") is uint count && count <= 14 &&
                ContentU32(texture, "pixelPayloadBytes") is uint bytes &&
                ValidContentTexture(width, height, layout, count, bytes))
            {
                result = new A.TextureContentMetrics(3, width, height, layout, count, bytes);
            }
            else if (RebuildToken<A.ContentMetricError>(row.Find("err")) is { } reason)
            {
                result = new A.RefusedContentMetric(reason);
            }
            else return null;
            decoded.Add(new(guid, result));
        }
        return new(decoded.AsReadOnly());
    }

    private static bool ValidContentTexture(uint width, uint height, A.ContentTextureLayout layout, uint count, uint bytes)
    {
        bool bc7 = layout == A.ContentTextureLayout.Bc7;
        if (count > BitOperations.Log2(Math.Max(width, height)) + 1 ||
            bc7 && (width % 4 != 0 || height % 4 != 0)) return false;
        ulong expected = 0;
        for (int level = 0; level < count; ++level)
        {
            uint w = Math.Max(1u, width >> level), h = Math.Max(1u, height >> level);
            expected += bc7 ? (ulong)((w + 3) / 4) * ((h + 3) / 4) * 16 : (ulong)w * h * 4;
        }
        return expected == bytes;
    }

    private static uint? ContentU32(JsonValue value, string name) =>
        value.Find(name) is { } member && member.TryGetUInt64(out ulong number) && number is > 0 and <= uint.MaxValue
            ? (uint)number : null;

    public static A.ProjectScanError? DecodeProjectScanError(JsonValue value) => RebuildToken<A.ProjectScanError>(value);

    public static A.ProjectScan? DecodeProjectScan(JsonValue value)
    {
        if (!Exact(value, 4) || value.Find("id") is not JsonValue idValue ||
            DecodeProjectGuid(idValue) is not AssetGuid id ||
            value.Find("pending") is not { IsBool: true } pending ||
            value.Find("catalogReady") is not { IsBool: true } catalog ||
            value.Find("roots") is not { IsArray: true } roots ||
            (pending.AsBool ? catalog.AsBool || roots.Items.Count != 0 : roots.Items.Count != 2) ||
            Encoding.UTF8.GetByteCount(Json.Write(value)) > 2 * 1024 * 1024) return null;

        var parsed = new List<A.ProjectRoot>(2);
        var usedRoots = new HashSet<A.ProjectRootKind>();
        var links = new HashSet<AssetGuid>();
        int entries = 0;
        foreach (var root in roots.Items)
        {
            if (!Exact(root, 4) || ProjectToken<A.ProjectRootKind>(root.Find("kind")) is not A.ProjectRootKind kind ||
                !usedRoots.Add(kind) ||
                ProjectToken<A.ProjectCoverage>(root.Find("coverage")) is not A.ProjectCoverage coverage ||
                root.Find("diagnostic") is not { IsString: true } diagnostic ||
                Encoding.UTF8.GetByteCount(diagnostic.AsString) > 256 ||
                root.Find("sources") is not { IsArray: true } rows ||
                coverage == A.ProjectCoverage.Unavailable && rows.Items.Count != 0) return null;
            var paths = new HashSet<string>(StringComparer.Ordinal);
            var sources = new List<A.ProjectSource>();
            foreach (var row in rows.Items)
            {
                if (++entries > 2048 || !Exact(row, 8) ||
                    row.Find("path") is not { IsString: true } path || !ValidProjectPath(path.AsString) ||
                    !paths.Add(path.AsString) ||
                    ProjectToken<A.ProjectSourceKind>(row.Find("kind")) is not A.ProjectSourceKind rowKind ||
                    ProjectToken<A.ProjectRegistration>(row.Find("registration")) is not A.ProjectRegistration registration ||
                    row.Find("importName") is not JsonValue name ||
                    row.Find("evidence") is not { IsString: true } proof || !ValidPrefabDigest(proof.AsString) ||
                    row.Find("producer") is not JsonValue producer ||
                    row.Find("outputs") is not { IsArray: true } outputs ||
                    row.Find("newerPrefabSource") is not { IsBool: true } newer)
                    return null;

                AssetGuid? producerId = DecodeProjectGuid(producer);
                if (!producer.IsNull && !producerId.HasValue ||
                    (registration == A.ProjectRegistration.Registered) != producerId.HasValue ||
                    coverage != A.ProjectCoverage.Complete && registration != A.ProjectRegistration.Unknown ||
                    (!producerId.HasValue || !catalog.AsBool) && outputs.Items.Count != 0 ||
                    newer.AsBool && (rowKind != A.ProjectSourceKind.Prefab || outputs.Items.Count == 0)) return null;
                string? importName = null;
                if (!name.IsNull)
                {
                    if (!name.IsString || !ValidImportName(name.AsString) || kind != A.ProjectRootKind.Inbox ||
                        !((rowKind == A.ProjectSourceKind.Gltf && path.AsString == name.AsString + ".gltf") ||
                          (rowKind == A.ProjectSourceKind.Png && path.AsString == name.AsString + "/source.png"))) return null;
                    importName = name.AsString;
                }
                var linked = new List<AssetGuid>();
                foreach (var output in outputs.Items)
                {
                    if (DecodeProjectGuid(output) is not AssetGuid guid || !links.Add(guid) || links.Count > 4096)
                        return null;
                    linked.Add(guid);
                }
                if (kind == A.ProjectRootKind.Prefabs && rowKind is not (A.ProjectSourceKind.Directory or A.ProjectSourceKind.Prefab) ||
                    kind == A.ProjectRootKind.Inbox && rowKind == A.ProjectSourceKind.Prefab ||
                    rowKind == A.ProjectSourceKind.Directory && path.AsString.Count(c => c == '/') >= 4 ||
                    registration != A.ProjectRegistration.Unknown && importName is null && rowKind != A.ProjectSourceKind.Prefab ||
                    (rowKind is A.ProjectSourceKind.Png or A.ProjectSourceKind.Prefab) &&
                    (linked.Count > 1 || linked.Count == 1 && linked[0] != producerId))
                    return null;
                sources.Add(new(path.AsString, rowKind, importName, proof.AsString, registration, producerId,
                    linked.AsReadOnly(), newer.AsBool));
            }
            parsed.Add(new(kind, coverage, diagnostic.AsString, sources.AsReadOnly()));
        }
        return new(id, pending.AsBool, catalog.AsBool, parsed.AsReadOnly());
    }

    private static AssetGuid? DecodeProjectGuid(JsonValue value) =>
        DecodeAsset(value) is AssetGuid guid && !guid.IsNil && guid.ToString() == value.AsString ? guid : null;

    private static T? ProjectToken<T>(JsonValue? value) where T : struct, Enum =>
        value is { IsString: true } ? RebuildToken<T>(value) : null;

    private static bool ValidProjectPath(string path)
    {
        if (path.Length == 0 || Encoding.UTF8.GetByteCount(path) > 512) return false;
        string[] parts = path.Split('/');
        return parts.Length <= 5 && parts.All(part => part.Length != 0 && part[0] != '.' &&
            part[^1] is not ('.' or ' ') && !part.Any(c => c < 32 || c == 127 || "\\:*?\"<>|".Contains(c)));
    }

    public static A.StaticGroupError? DecodeStaticGroupError(JsonValue value) => RebuildToken<A.StaticGroupError>(value);

    public static bool IsUnsupportedStaticGroup(JsonValue envelope) =>
        IsLegacyAssetRebuildRefusal(envelope) || envelope.Find("result") is null &&
        envelope.Find("error")?.Find("code") is { IsString: true, AsString: "UnknownMethod" };

    public static A.StaticGroup? DecodeStaticGroup(JsonValue value, AssetGuid requested, bool prefab = false)
    {
        int min = prefab ? 1 : 2;
        if (requested.IsNil || !Exact(value, 3) || value.Find("scene") is not JsonValue identity ||
            DecodeAsset(identity) != requested || value.Find("name") is not { IsString: true } name ||
            !(prefab ? ValidSaveAsName(name.AsString) : ValidImportName(name.AsString)) ||
            value.Find("nodes") is not { IsArray: true } nodes ||
            nodes.Items.Count < min || nodes.Items.Count > (prefab ? 64 : 63) ||
            Encoding.UTF8.GetByteCount(Json.Write(value)) > 64 * 1024)
            return null;
        var parsed = new List<A.StaticGroupNode>(nodes.Items.Count);
        int renderables = 0;
        foreach (var node in nodes.Items)
        {
            if (!Exact(node, prefab ? 6 : 5) || node.Find("parent") is not JsonValue parent ||
                node.Find("renderable") is not JsonValue renderable ||
                GroupVector(node.Find("position")) is not Vector3 position ||
                GroupVector(node.Find("eulerRadians")) is not Vector3 euler ||
                GroupVector(node.Find("scale")) is not Vector3 scale ||
                !prefab && (scale.X == 0 || scale.Y == 0 || scale.Z == 0))
                return null;
            var label = node.Find("name");
            if (prefab && (label is not { IsString: true } || !Atlantis.Gameplay.Authoring.NodeNames.IsValid(label.AsString)))
                return null;
            int? parentIndex = null;
            if (!parent.IsNull)
            {
                if (!parent.TryGetUInt64(out ulong index) || index >= (ulong)nodes.Items.Count) return null;
                parentIndex = (int)index;
            }
            A.StaticGroupRenderable? binding = null;
            if (!renderable.IsNull)
            {
                if (!Exact(renderable, 2) || renderable.Find("mesh") is not JsonValue mesh ||
                    DecodeAsset(mesh) is not AssetGuid meshId || meshId.IsNil ||
                    renderable.Find("material") is not JsonValue material)
                    return null;
                AssetGuid? materialId = material.IsNull ? null : DecodeAsset(material);
                if (!material.IsNull && (materialId is null || materialId.Value.IsNil)) return null;
                binding = new(meshId, materialId);
                ++renderables;
            }
            parsed.Add(new(parentIndex, position, euler, scale, binding, label?.AsString ?? ""));
        }
        if (renderables < min || prefab && parsed.Count(node => node.Parent is null) != 1) return null;
        foreach (var node in parsed)
        {
            int depth = 0;
            for (int? parent = node.Parent; parent is int index; parent = parsed[index].Parent)
                if (++depth >= parsed.Count) return null;
        }
        return new A.StaticGroup(requested, name.AsString, parsed.AsReadOnly());
    }

    private static Vector3? GroupVector(JsonValue? value) =>
        value is { IsArray: true } && value.Items.Count == 3 &&
        value.Items[0].TryGetFloat(out float x) && value.Items[1].TryGetFloat(out float y) &&
        value.Items[2].TryGetFloat(out float z) && float.IsFinite(x) && float.IsFinite(y) && float.IsFinite(z)
            ? new Vector3(x, y, z) : null;

    private static string AssetTypeText(A.AssetType type) => type switch
    {
        A.AssetType.Mesh => "mesh", A.AssetType.Texture => "texture", A.AssetType.Material => "material",
        A.AssetType.Scene => "scene", A.AssetType.Environment => "environment", _ => throw new ArgumentOutOfRangeException(nameof(type)),
    };
    private static A.AssetType? DecodeAssetType(JsonValue value) => value.IsString ? value.AsString switch
    {
        "mesh" => A.AssetType.Mesh, "texture" => A.AssetType.Texture, "material" => A.AssetType.Material,
        "scene" => A.AssetType.Scene, "environment" => A.AssetType.Environment, _ => null,
    } : null;
    public static JsonValue Encode(A.AssetSummary v)
    {
        var result = JsonValue.NewObject().Set("guid", Encode(v.Guid))
            .Set("type", JsonValue.String(AssetTypeText(v.Type))).Set("source", JsonValue.String(v.Source));
        if (v.DisplayName is string name) result.Set("displayName", JsonValue.String(name));
        if (v.LogicalPath is string path) result.Set("logicalPath", JsonValue.String(path));
        if (v.IsPrefab) result.Set("isPrefab", JsonValue.Bool(true));
        return result;
    }
    public static JsonValue Encode(A.AssetDetails v)
    {
        JsonValue result = Encode(new A.AssetSummary(v.Guid, v.Type, v.Source)
            { DisplayName = v.DisplayName, LogicalPath = v.LogicalPath, IsPrefab = v.IsPrefab });
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
        A.AssetType? type = DecodeAssetType(t);
        if (type is not A.AssetType known || CountMember(v, "displayName") > 1) return null;
        var optional = v.Find("displayName");
        if (optional is not null && (known != A.AssetType.Texture || !optional.IsString || !ValidImportName(optional.AsString)))
            return null;
        if (CountMember(v, "logicalPath") > 1) return null;
        var path = v.Find("logicalPath");
        if (path is not null && (known != A.AssetType.Texture || !path.IsString ||
            !ValidTextureLogicalPath(path.AsString) || RegisteredColorToken(s.AsString) is null)) return null;
        var prefab = v.Find("isPrefab");
        if (CountMember(v, "isPrefab") > 1 || prefab is not null &&
            (known != A.AssetType.Scene || prefab.Kind != JsonKind.Bool)) return null;
        return new A.AssetSummary(guid, known, s.AsString)
            { DisplayName = optional?.AsString, LogicalPath = path?.AsString, IsPrefab = prefab?.AsBool ?? false };
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
        return new A.AssetDetails(s.Guid, s.Type, s.Source, (uint)artifact, schema, tool.AsString, deps)
            { DisplayName = s.DisplayName, LogicalPath = s.LogicalPath, IsPrefab = s.IsPrefab };
    }
    public static JsonValue Encode(A.AssetError e) => JsonValue.String(e.ToString());
    public static A.AssetError? DecodeAssetError(JsonValue v) => v.IsString ? v.AsString switch
    { "CatalogUnavailable" => A.AssetError.CatalogUnavailable, "UnknownAsset" => A.AssetError.UnknownAsset, _ => null } : null;

    private const int PreviewDimension = 256;
    private const int MaxPreviewPngBytes = 512 * 1024;
    private const int MaxPreviewBase64Chars = 4 * ((MaxPreviewPngBytes + 2) / 3);

    /// <summary>Checks the preview envelope fields and PNG header, leaving image decoding to the consumer.</summary>
    public static A.MeshPreview? DecodeMeshPreview(JsonValue value, AssetGuid requested)
    {
        if (requested.IsNil || value.Find("guid") is not JsonValue guid || DecodeAsset(guid) != requested) return null;
        byte[]? bytes = DecodePreviewPng(value);
        return bytes is null ? null : new A.MeshPreview(requested, PreviewDimension, PreviewDimension, bytes);
    }

    /// <summary>Checks both requested roles and the bounded PNG header. Each successful call returns owned bytes.</summary>
    public static A.MeshMaterialPreview? DecodeMeshMaterialPreview(JsonValue value, AssetGuid meshGuid, AssetGuid materialGuid)
    {
        if (meshGuid.IsNil || materialGuid.IsNil ||
            CountMember(value, "meshGuid") != 1 || value.Find("meshGuid") is not JsonValue mesh || DecodeAsset(mesh) != meshGuid ||
            CountMember(value, "materialGuid") != 1 || value.Find("materialGuid") is not JsonValue material ||
            DecodeAsset(material) != materialGuid || CountMember(value, "width") != 1 || CountMember(value, "height") != 1 ||
            CountMember(value, "mimeType") != 1 || CountMember(value, "pngBase64") != 1) return null;
        byte[]? bytes = DecodePreviewPng(value);
        return bytes is null ? null : new A.MeshMaterialPreview(meshGuid, materialGuid, PreviewDimension, PreviewDimension, bytes);
    }

    // The protocol reader keeps duplicate JSON members. The pair operation checks
    // exact counts without changing the existing first-member behavior for other calls.
    public static int CountMember(JsonValue value, string key)
    {
        int count = 0;
        if (value.IsObject)
            foreach (var member in value.Members)
                if (member.Key == key) ++count;
        return count;
    }

    private static byte[]? DecodePreviewPng(JsonValue value)
    {
        if (value.Find("width") is not JsonValue w || !w.TryGetUInt64(out ulong width) || width != PreviewDimension ||
            value.Find("height") is not JsonValue h || !h.TryGetUInt64(out ulong height) || height != PreviewDimension ||
            value.Find("mimeType") is not { IsString: true, AsString: "image/png" } ||
            value.Find("pngBase64") is not { IsString: true } png) return null;

        ReadOnlySpan<char> encoded = png.AsString.AsSpan();
        if (encoded.Length < 4 || encoded.Length > MaxPreviewBase64Chars || encoded.Length % 4 != 0) return null;
        int padding = encoded[^1] != '=' ? 0 : encoded[^2] == '=' ? 2 : 1;
        int length = encoded.Length / 4 * 3 - padding;
        // Signature plus a complete IHDR chunk (length, type, body and CRC).
        if (length is < 33 or > MaxPreviewPngBytes) return null;
        ReadOnlySpan<char> symbols = encoded[..(encoded.Length - padding)];
        foreach (char symbol in symbols)
            if (Base64Value(symbol) < 0) return null;
        // Convert accepts unused nonzero bits, which would allow a second spelling of the same bytes.
        if (padding != 0 && (Base64Value(symbols[^1]) & (padding == 1 ? 3 : 15)) != 0) return null;

        byte[] bytes = new byte[length];
        if (!Convert.TryFromBase64Chars(encoded, bytes, out int written) || written != length) return null;
        ReadOnlySpan<byte> header = bytes;
        ReadOnlySpan<byte> signature = [137, 80, 78, 71, 13, 10, 26, 10];
        if (!header[..8].SequenceEqual(signature) ||
            BinaryPrimitives.ReadUInt32BigEndian(header[8..12]) != 13 || !header[12..16].SequenceEqual("IHDR"u8) ||
            BinaryPrimitives.ReadUInt32BigEndian(header[16..20]) != PreviewDimension ||
            BinaryPrimitives.ReadUInt32BigEndian(header[20..24]) != PreviewDimension) return null;
        return bytes;
    }

    private static int Base64Value(char c) => c switch
    {
        >= 'A' and <= 'Z' => c - 'A',
        >= 'a' and <= 'z' => c - 'a' + 26,
        >= '0' and <= '9' => c - '0' + 52,
        '+' => 62, '/' => 63, _ => -1,
    };

    public static A.PreviewError? DecodePreviewError(JsonValue value) => RebuildToken<A.PreviewError>(value);
    public static A.MeshMaterialPreviewError? DecodeMeshMaterialPreviewError(JsonValue value) =>
        RebuildToken<A.MeshMaterialPreviewError>(value);

    private static T? RebuildToken<T>(JsonValue? value) where T : struct, Enum =>
        value is { IsString: true } && Enum.TryParse(value.AsString, out T parsed) &&
        Enum.IsDefined(parsed) && parsed.ToString() == value.AsString ? parsed : null;

    /// <summary>Compatibility response for either well-formed new rebuild operation on a pinned legacy host.</summary>
    public static bool IsLegacyAssetRebuildRefusal(JsonValue envelope) =>
        envelope.Find("result") is null && envelope.Find("error") is JsonValue error &&
        error.Find("code") is { IsString: true, AsString: "BadRequest" } &&
        error.Find("message") is { IsString: true, AsString: "unknown or malformed asset query" };

    public static bool ValidTextureLogicalPath(string path)
    {
        if (!path.StartsWith("textures/", StringComparison.Ordinal) || !path.EndsWith(".png", StringComparison.Ordinal)) return false;
        var parts = path[9..^4].Split('/');
        return parts.Length <= 5 && parts.All(ValidImportName);
    }

    public static string? RegisteredColorToken(string source)
    {
        const string prefix = "content:texture_", suffix = "/source.png";
        if (!source.StartsWith(prefix, StringComparison.Ordinal) || !source.EndsWith(suffix, StringComparison.Ordinal) ||
            source.Length <= prefix.Length + suffix.Length) return null;
        string token = source[prefix.Length..^suffix.Length];
        return ValidImportName(token) ? token : null;
    }

    public static A.MoveTextureError? DecodeMoveTextureError(JsonValue value) => RebuildToken<A.MoveTextureError>(value);
    public static A.RenameTextureError? DecodeRenameTextureError(JsonValue value) => RebuildToken<A.RenameTextureError>(value);
    public static A.TextureRemovalError? DecodeTextureRemovalError(JsonValue value) => RebuildToken<A.TextureRemovalError>(value);

    public static A.TextureRemovalCheck? DecodeTextureRemovalCheck(JsonValue value)
    {
        if (!value.IsObject || value.Members.Count != 8 ||
            value.Find("id") is not JsonValue idValue || DecodeAsset(idValue) is not AssetGuid id || id.IsNil ||
            value.Find("asset") is not JsonValue assetValue || DecodeAsset(assetValue) is not AssetGuid asset || asset.IsNil ||
            value.Find("expectedGeneration") is not JsonValue generationValue ||
            DecodeAsset(generationValue) is not AssetGuid generation || generation.IsNil ||
            value.Find("state") is not JsonValue stateValue ||
            RebuildToken<A.TextureRemovalState>(stateValue) is not { } state ||
            value.Find("blockers") is not { IsArray: true } blockers || blockers.Items.Count > 16 ||
            value.Find("truncated") is not { IsBool: true } truncated ||
            value.Find("incomplete") is not { IsBool: true } incomplete ||
            value.Find("diagnostic") is not { IsString: true } diagnostic ||
            System.Text.Encoding.UTF8.GetByteCount(diagnostic.AsString) > 4096) return null;
        if ((state == A.TextureRemovalState.Referenced) != (blockers.Items.Count != 0) ||
            (truncated.AsBool && blockers.Items.Count != 16) ||
            (state is A.TextureRemovalState.Checking or A.TextureRemovalState.Ready &&
                (incomplete.AsBool || diagnostic.AsString.Length != 0)) ||
            (state == A.TextureRemovalState.CheckIncomplete && !incomplete.AsBool)) return null;
        var entries = new List<A.TextureRemovalBlocker>();
        foreach (var item in blockers.Items)
        {
            if (!item.IsObject || item.Members.Count != 3 ||
                item.Find("asset") is not JsonValue assetItem || DecodeAsset(assetItem) is not AssetGuid dependent || dependent.IsNil ||
                item.Find("source") is not { IsString: true } source ||
                System.Text.Encoding.UTF8.GetByteCount(source.AsString) is 0 or > 4096 ||
                item.Find("slot") is not JsonValue slotValue) return null;
            A.TextureReferenceSlot? slot = slotValue.IsNull ? null : RebuildToken<A.TextureReferenceSlot>(slotValue);
            if (slot is null && !slotValue.IsNull) return null;
            var entry = new A.TextureRemovalBlocker(dependent, source.AsString, slot);
            if (entries.Contains(entry)) return null;
            entries.Add(entry);
        }
        return new(new(id), asset, new(generation), state, entries.AsReadOnly(), truncated.AsBool, incomplete.AsBool,
            diagnostic.AsString);
    }

    public static JsonValue Encode(A.TextureRemovalCheck check)
    {
        var blockers = JsonValue.NewArray();
        foreach (var blocker in check.Blockers)
            blockers.Push(JsonValue.NewObject().Set("asset", Encode(blocker.Asset))
                .Set("source", JsonValue.String(blocker.Source))
                .Set("slot", blocker.Slot is { } slot ? JsonValue.String(slot.ToString()) : JsonValue.Null()));
        return JsonValue.NewObject().Set("id", Encode(check.Id.Value)).Set("asset", Encode(check.Asset))
            .Set("expectedGeneration", Encode(check.ExpectedGeneration.Value))
            .Set("state", JsonValue.String(check.State.ToString())).Set("blockers", blockers)
            .Set("truncated", JsonValue.Bool(check.Truncated)).Set("incomplete", JsonValue.Bool(check.Incomplete))
            .Set("diagnostic", JsonValue.String(check.Diagnostic));
    }

    public static A.RebuildError? DecodeStaticGroupReimportError(JsonValue value) =>
        value is { IsString: true } && value.AsString == "UnknownJob"
            ? A.RebuildError.UnknownJob : DecodeRebuildError(value, true);

    public static A.StaticGroupReview? DecodeStaticGroupReview(JsonValue value)
    {
        if (!value.IsObject || value.Members.Count != 7 || Encoding.UTF8.GetByteCount(Json.Write(value)) > 64 * 1024 ||
            RebuildGuid(value.Find("id")) is not { } id || RebuildGuid(value.Find("scene")) is not { } scene ||
            RebuildGuid(value.Find("generation")) is not { } generation ||
            value.Find("name") is not { IsString: true } name || !ValidImportName(name.AsString) ||
            Text(value.Find("diagnostic"), 1024) is not { } diagnostic ||
            RebuildToken<A.StaticGroupReviewState>(value.Find("state")) is not { } state ||
            value.Find("rows") is not { IsArray: true } rows || rows.Items.Count > 4 * 63) return null;
        bool ready = state == A.StaticGroupReviewState.Ready;
        if (ready && rows.Items.Count == 0 ||
            !ready && state != A.StaticGroupReviewState.Blocked && rows.Items.Count != 0) return null;
        var keys = new HashSet<string>(StringComparer.Ordinal);
        var parsed = new List<A.StaticGroupChangeRow>(rows.Items.Count);
        foreach (var entry in rows.Items)
        {
            if (!entry.IsObject || entry.Members.Count != 6 || Text(entry.Find("key"), 75) is not { } key ||
                !keys.Add(key) || RebuildGuid(entry.Find("guid")) is not { } guid ||
                RebuildToken<A.StaticGroupChange>(entry.Find("change")) is not { } change ||
                Text(entry.Find("diagnostic"), 160) is not { } note ||
                !Revision(entry.Find("before"), out var before) || !Revision(entry.Find("after"), out var after)) return null;
            bool mesh = key.StartsWith("mesh-key/", StringComparison.Ordinal);
            if ((!mesh && !key.StartsWith("material-key/", StringComparison.Ordinal)) ||
                (before is not null && before.Slot.HasValue != mesh) || (after is not null && after.Slot.HasValue != mesh) ||
                (before is null && after is null) || (change == A.StaticGroupChange.Added && (before is not null || after is null)) ||
                (change == A.StaticGroupChange.Removed && (before is null || after is not null)) ||
                (change == A.StaticGroupChange.Matched && (before is null || after is null || before.Material != after.Material)) ||
                (ready && change is A.StaticGroupChange.Conflict or A.StaticGroupChange.Removed)) return null;
            parsed.Add(new(key, guid, change, before, after, note));
        }
        return new(id, scene, new(generation), name.AsString, state, diagnostic, parsed.AsReadOnly());

        static string? Text(JsonValue? item, int limit) =>
            item is { IsString: true } && Encoding.UTF8.GetByteCount(item.AsString) <= limit ? item.AsString : null;

        static bool Revision(JsonValue? item, out A.StaticGroupRevisionRow? row)
        {
            row = null;
            if (item is null) return false;
            if (item.IsNull) return true;
            if (!item.IsObject || item.Members.Count != 4 || Text(item.Find("label"), 128) is not { } label ||
                item.Find("index") is not { } rawIndex || !rawIndex.TryGetUInt64(out ulong index) || index >= 63 ||
                item.Find("slot") is not { } rawSlot || item.Find("material") is not { } rawMaterial) return false;
            uint? slot = null;
            if (!rawSlot.IsNull)
            {
                if (!rawSlot.TryGetUInt64(out ulong number) || number >= 63) return false;
                slot = (uint)number;
            }
            var material = RebuildGuid(rawMaterial);
            if (!rawMaterial.IsNull && (material is null || slot is null)) return false;
            row = new(label, (uint)index, slot, material);
            return true;
        }
    }

    public static JsonValue Encode(A.StaticGroupReview review)
    {
        var rows = JsonValue.NewArray();
        foreach (var row in review.Rows)
            rows.Push(JsonValue.NewObject().Set("key", JsonValue.String(row.Key)).Set("guid", Encode(row.Guid))
                .Set("change", JsonValue.String(row.Change.ToString())).Set("before", Revision(row.Before))
                .Set("after", Revision(row.After)).Set("diagnostic", JsonValue.String(row.Diagnostic)));
        return JsonValue.NewObject().Set("id", Encode(review.Id)).Set("scene", Encode(review.Scene))
            .Set("generation", Encode(review.Generation.Value)).Set("name", JsonValue.String(review.Name))
            .Set("state", JsonValue.String(review.State.ToString())).Set("diagnostic", JsonValue.String(review.Diagnostic))
            .Set("rows", rows);

        static JsonValue Revision(A.StaticGroupRevisionRow? row) => row is null ? JsonValue.Null() :
            JsonValue.NewObject().Set("label", JsonValue.String(row.Label)).Set("index", JsonValue.Number((ulong)row.Index))
                .Set("slot", row.Slot is uint slot ? JsonValue.Number((ulong)slot) : JsonValue.Null())
                .Set("material", row.Material is AssetGuid material ? Encode(material) : JsonValue.Null());
    }

    public static A.RebuildError? DecodeRebuildError(JsonValue value, bool start)
    {
        A.RebuildError? error = RebuildToken<A.RebuildError>(value);
        if (error is null or A.RebuildError.Unsupported) return null;
        return error == A.RebuildError.Unavailable || (error == A.RebuildError.UnknownJob) != start ? error : null;
    }

    private static AssetGuid? RebuildGuid(JsonValue? value) =>
        value is not null && DecodeAsset(value) is AssetGuid guid && !guid.IsNil ? guid : null;

    public static A.RebuildJob? DecodeRebuildJob(JsonValue value)
    {
        if (RebuildGuid(value.Find("id")) is not AssetGuid id || RebuildGuid(value.Find("asset")) is not AssetGuid asset ||
            RebuildToken<A.RebuildState>(value.Find("state")) is not A.RebuildState state) return null;
        JsonValue? g = value.Find("generation"), f = value.Find("failure");
        if ((g is not null) != (state == A.RebuildState.Succeeded) ||
            (f is not null) != (state == A.RebuildState.Failed)) return null;
        A.GenerationId? generation = null;
        A.RebuildFailure? failure = null;
        if (g is not null)
        {
            if (RebuildGuid(g) is not AssetGuid guid) return null;
            generation = new(guid);
        }
        if (f is not null)
        {
            if (RebuildToken<A.RebuildStage>(f.Find("stage")) is not A.RebuildStage stage ||
                RebuildToken<A.RebuildFailureCode>(f.Find("code")) is not A.RebuildFailureCode code ||
                f.Find("diagnostic") is not { IsString: true } diagnostic ||
                System.Text.Encoding.UTF8.GetByteCount(diagnostic.AsString) > 4096) return null;
            bool valid = code switch
            {
                A.RebuildFailureCode.LaunchFailed => stage == A.RebuildStage.Launch,
                A.RebuildFailureCode.BuildFailed => stage == A.RebuildStage.Pipeline,
                A.RebuildFailureCode.InvalidOutput or A.RebuildFailureCode.IdentityChanged => stage == A.RebuildStage.Validation,
                A.RebuildFailureCode.PublishFailed => stage == A.RebuildStage.Publication,
                A.RebuildFailureCode.TimedOut => true,
                _ => false,
            };
            if (!valid) return null;
            uint? exit = null;
            if (f.Find("exitCode") is JsonValue e)
            {
                if (!e.TryGetUInt64(out ulong n) || n > uint.MaxValue) return null;
                exit = (uint)n;
            }
            failure = new(stage, code, diagnostic.AsString, exit);
        }
        return new(new(id), asset, state, generation, failure);
    }

    public static A.RebuildStatus? DecodeRebuildStatus(JsonValue value)
    {
        if (value.Find("activeGeneration") is not JsonValue a || value.Find("job") is not JsonValue j) return null;
        AssetGuid? active = a.IsNull ? null : RebuildGuid(a);
        A.RebuildJob? job = j.IsNull ? null : DecodeRebuildJob(j);
        if ((!a.IsNull && active is null) || (!j.IsNull && job is null)) return null;
        return new(active is AssetGuid id ? new A.GenerationId(id) : null, job);
    }

    public static JsonValue Encode(A.RebuildJob job)
    {
        JsonValue value = JsonValue.NewObject().Set("id", Encode(job.Id.Value)).Set("asset", Encode(job.Asset))
            .Set("state", JsonValue.String(job.State.ToString()));
        if (job.Generation is A.GenerationId id) value.Set("generation", Encode(id.Value));
        if (job.Failure is A.RebuildFailure failure)
        {
            JsonValue f = JsonValue.NewObject().Set("stage", JsonValue.String(failure.Stage.ToString()))
                .Set("code", JsonValue.String(failure.Code.ToString())).Set("diagnostic", JsonValue.String(failure.Diagnostic));
            if (failure.ExitCode is uint code) f.Set("exitCode", JsonValue.Number((ulong)code));
            value.Set("failure", f);
        }
        return value;
    }

    public static JsonValue Encode(A.RebuildStatus status) => JsonValue.NewObject()
        .Set("activeGeneration", status.ActiveGeneration is A.GenerationId id ? Encode(id.Value) : JsonValue.Null())
        .Set("job", status.Job is A.RebuildJob job ? Encode(job) : JsonValue.Null());

    public static bool ValidImportName(string? name)
    {
        if (name is null || name.Length is < 1 or > 48 || name[0] is < 'a' or > 'z') return false;
        foreach (char c in name)
            if (c is not (>= 'a' and <= 'z' or >= '0' and <= '9' or '_')) return false;
        if (name is "con" or "prn" or "aux" or "nul") return false;
        return !(name.Length == 4 && name[3] is >= '0' and <= '9' &&
            (name.StartsWith("com", StringComparison.Ordinal) || name.StartsWith("lpt", StringComparison.Ordinal)));
    }

    public static A.ImportError? DecodeImportError(JsonValue value, bool start)
    {
        var error = RebuildToken<A.ImportError>(value);
        if (error is null or A.ImportError.Unsupported) return null;
        return error == A.ImportError.Unavailable || (error == A.ImportError.UnknownJob) != start ? error : null;
    }

    private static A.ImportFailure? DecodeImportFailure(JsonValue value)
    {
        if (RebuildToken<A.ImportStage>(value.Find("stage")) is not A.ImportStage stage ||
            RebuildToken<A.ImportFailureCode>(value.Find("code")) is not A.ImportFailureCode code ||
            RebuildToken<A.RegistrationState>(value.Find("registration")) is not A.RegistrationState registration ||
            value.Find("diagnostic") is not { IsString: true } diagnostic ||
            System.Text.Encoding.UTF8.GetByteCount(diagnostic.AsString) > 4096) return null;
        bool valid = code switch
        {
            A.ImportFailureCode.InvalidSource => stage == A.ImportStage.Source,
            A.ImportFailureCode.LaunchFailed or A.ImportFailureCode.BuildFailed => stage == A.ImportStage.Pipeline,
            A.ImportFailureCode.InvalidOutput or A.ImportFailureCode.IdentityConflict => stage == A.ImportStage.Validation,
            A.ImportFailureCode.RegisterFailed => stage == A.ImportStage.Registration,
            A.ImportFailureCode.PublishFailed => stage == A.ImportStage.Publication,
            A.ImportFailureCode.TimedOut => true,
            _ => false,
        };
        JsonValue? rootField = value.Find("registeredRootGuid");
        AssetGuid? root = RebuildGuid(rootField);
        if (!valid || (rootField is not null) != (registration == A.RegistrationState.Committed) ||
            (rootField is not null && root is null)) return null;
        uint? exit = null;
        if (value.Find("exitCode") is JsonValue raw)
        {
            if (!raw.TryGetUInt64(out ulong number) || number > uint.MaxValue) return null;
            exit = (uint)number;
        }
        return new(stage, code, diagnostic.AsString, exit, registration, root);
    }

    public static A.ImportJob? DecodeImportJob(JsonValue value)
    {
        if (RebuildGuid(value.Find("id")) is not AssetGuid id ||
            value.Find("name") is not { IsString: true } name || !ValidImportName(name.AsString) ||
            RebuildToken<A.ImportMode>(value.Find("mode")) is not A.ImportMode mode ||
            RebuildToken<A.RebuildState>(value.Find("state")) is not A.RebuildState state) return null;
        JsonValue? root = value.Find("rootGuid"), generation = value.Find("generation"), failure = value.Find("failure");
        bool success = state == A.RebuildState.Succeeded;
        if ((root is not null) != success || (generation is not null) != success ||
            (failure is not null) != (state == A.RebuildState.Failed)) return null;
        AssetGuid? guid = RebuildGuid(root), selected = RebuildGuid(generation);
        A.ImportFailure? detail = failure is null ? null : DecodeImportFailure(failure);
        if ((success && (guid is null || selected is null)) || (failure is not null && detail is null)) return null;
        return new(new(id), name.AsString, mode, state, guid, selected is AssetGuid gen ? new A.GenerationId(gen) : null, detail);
    }

    public static A.ImportStatus? DecodeImportStatus(JsonValue value)
    {
        if (value.Find("activeGeneration") is not JsonValue active || value.Find("job") is not JsonValue job) return null;
        AssetGuid? id = active.IsNull ? null : RebuildGuid(active);
        A.ImportJob? decoded = job.IsNull ? null : DecodeImportJob(job);
        if ((!active.IsNull && id is null) || (!job.IsNull && decoded is null)) return null;
        return new(id is AssetGuid guid ? new A.GenerationId(guid) : null, decoded);
    }

    public static A.TextureImportJob? DecodeTextureImportJob(JsonValue value)
    {
        if (DecodeImportJob(value) is not { } job || !Exact(value,
            job.State == A.RebuildState.Running ? 4 : job.State == A.RebuildState.Succeeded ? 6 : 5)) return null;
        if (job.Failure is { } failure &&
            !Exact(value.Find("failure")!, 4 + (failure.ExitCode.HasValue ? 1 : 0) +
                (failure.RegisteredRootGuid.HasValue ? 1 : 0))) return null;
        return new(new(job.Id.Value), job.Name, job.Mode, job.State, job.RootGuid, job.Generation, job.Failure);
    }

    public static A.TextureImportStatus? DecodeTextureImportStatus(JsonValue value)
    {
        if (!Exact(value, 3) || value.Find("workspaceNeedsRepair") is not { IsBool: true } repair ||
            value.Find("activeGeneration") is not JsonValue active || value.Find("job") is not JsonValue job) return null;
        AssetGuid? id = active.IsNull ? null : RebuildGuid(active);
        var decoded = job.IsNull ? null : DecodeTextureImportJob(job);
        if ((!active.IsNull && id is null) || (!job.IsNull && decoded is null)) return null;
        return new(id is AssetGuid guid ? new A.GenerationId(guid) : null, decoded, repair.AsBool);
    }

    public static JsonValue Encode(A.ImportJob job)
    {
        JsonValue value = JsonValue.NewObject().Set("id", Encode(job.Id.Value)).Set("name", JsonValue.String(job.Name))
            .Set("mode", JsonValue.String(job.Mode.ToString())).Set("state", JsonValue.String(job.State.ToString()));
        if (job.RootGuid is AssetGuid root) value.Set("rootGuid", Encode(root));
        if (job.Generation is A.GenerationId gen) value.Set("generation", Encode(gen.Value));
        if (job.Failure is A.ImportFailure failure)
        {
            JsonValue detail = JsonValue.NewObject().Set("stage", JsonValue.String(failure.Stage.ToString()))
                .Set("code", JsonValue.String(failure.Code.ToString())).Set("diagnostic", JsonValue.String(failure.Diagnostic))
                .Set("registration", JsonValue.String(failure.Registration.ToString()));
            if (failure.ExitCode is uint exit) detail.Set("exitCode", JsonValue.Number((ulong)exit));
            if (failure.RegisteredRootGuid is AssetGuid guid) detail.Set("registeredRootGuid", Encode(guid));
            value.Set("failure", detail);
        }
        return value;
    }

    public static JsonValue Encode(A.ImportStatus status) => JsonValue.NewObject()
        .Set("activeGeneration", status.ActiveGeneration is A.GenerationId gen ? Encode(gen.Value) : JsonValue.Null())
        .Set("job", status.Job is A.ImportJob job ? Encode(job) : JsonValue.Null());

    private static bool Exact(JsonValue value, int count) => value.IsObject && value.Members.Count == count;

    internal static bool ValidMaterialValues(A.MaterialValues values, bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false) =>
        !values.Texture.IsNil && Factor(values.Red) && Factor(values.Green) && Factor(values.Blue) &&
        Factor(values.Metallic) && Factor(values.Roughness) &&
        (withOcclusion ? values.OcclusionMap is null || (values.OcclusionMap is { IsNil: false } &&
            (withCombinedMaps || (values.NormalMap is null && values.MetallicRoughnessMap is null))) : values.OcclusionMap is null) &&
        (withMetallicRoughness || withOcclusion ? values.MetallicRoughnessMap is not { IsNil: true } : values.MetallicRoughnessMap is null) &&
        (withMaps || withMetallicRoughness || withOcclusion ? values.NormalMap is not { IsNil: true } && values.EmissiveTexture is not { IsNil: true } &&
            EmissiveComponent(values.EmissiveRed) && EmissiveComponent(values.EmissiveGreen) &&
            EmissiveComponent(values.EmissiveBlue) :
            values.NormalMap is null && values.EmissiveTexture is null &&
            values.EmissiveRed == 0 && values.EmissiveGreen == 0 && values.EmissiveBlue == 0);

    private static bool EmissiveComponent(float value) => float.IsFinite(value) && value is >= 0f and <= 65504f;

    private static bool Factor(float value) => float.IsFinite(value) && value >= 0f && value <= 1f;
    private static float? ReadFactor(JsonValue? value) =>
        value is not null && value.TryGetFloat(out float number) && Factor(number) ? number : null;

    private static JsonValue EncodeMaterialValues(A.MaterialValues values, bool withMaps, bool withMetallicRoughness, bool withOcclusion)
    {
        var encoded = JsonValue.NewObject().Set("texture", Encode(values.Texture))
            .Set("color", JsonValue.NewArray().Push(JsonValue.Number(values.Red)).Push(JsonValue.Number(values.Green))
                .Push(JsonValue.Number(values.Blue)))
            .Set("metallic", JsonValue.Number(values.Metallic)).Set("roughness", JsonValue.Number(values.Roughness));
        if (withMaps || withMetallicRoughness || withOcclusion)
            encoded.Set("normalMap", values.NormalMap is AssetGuid normal ? Encode(normal) : JsonValue.Null())
                .Set("emissiveTexture", values.EmissiveTexture is AssetGuid emissive ? Encode(emissive) : JsonValue.Null())
                .Set("emissiveFactor", JsonValue.NewArray().Push(JsonValue.Number(values.EmissiveRed))
                    .Push(JsonValue.Number(values.EmissiveGreen)).Push(JsonValue.Number(values.EmissiveBlue)));
        if (withMetallicRoughness || withOcclusion)
            encoded.Set("metallicRoughnessMap", values.MetallicRoughnessMap is AssetGuid packed ? Encode(packed) : JsonValue.Null());
        if (withOcclusion)
            encoded.Set("occlusionMap", values.OcclusionMap is AssetGuid occlusion ? Encode(occlusion) : JsonValue.Null());
        return encoded;
    }

    private static bool MaterialSlot(JsonValue? value, out AssetGuid? slot)
    {
        slot = null;
        if (value is null) return false;
        if (value.IsNull) return true;
        slot = RebuildGuid(value);
        return slot is not null;
    }

    private static A.MaterialValues? DecodeMaterialValues(JsonValue value, bool withMaps, bool withMetallicRoughness, bool withOcclusion, bool withCombinedMaps)
    {
        if (!Exact(value, withOcclusion ? 9 : withMetallicRoughness ? 8 : withMaps ? 7 : 4) || RebuildGuid(value.Find("texture")) is not AssetGuid texture ||
            value.Find("color") is not { IsArray: true } color || color.Items.Count != 3 ||
            ReadFactor(color.Items[0]) is not float red || ReadFactor(color.Items[1]) is not float green ||
            ReadFactor(color.Items[2]) is not float blue || ReadFactor(value.Find("metallic")) is not float metallic ||
            ReadFactor(value.Find("roughness")) is not float roughness) return null;
        var values = new A.MaterialValues(texture, red, green, blue, metallic, roughness);
        if (!withMaps && !withMetallicRoughness && !withOcclusion) return values;
        if (!MaterialSlot(value.Find("normalMap"), out var normal) ||
            !MaterialSlot(value.Find("emissiveTexture"), out var emissive) ||
            value.Find("emissiveFactor") is not { IsArray: true } factor || factor.Items.Count != 3 ||
            !factor.Items[0].TryGetFloat(out float er) || !factor.Items[1].TryGetFloat(out float eg) ||
            !factor.Items[2].TryGetFloat(out float eb)) return null;
        values = values with { NormalMap = normal, EmissiveTexture = emissive,
            EmissiveRed = er, EmissiveGreen = eg, EmissiveBlue = eb };
        if (withMetallicRoughness || withOcclusion)
        {
            if (!MaterialSlot(value.Find("metallicRoughnessMap"), out var packed)) return null;
            values = values with { MetallicRoughnessMap = packed };
        }
        if (withOcclusion)
        {
            if (!MaterialSlot(value.Find("occlusionMap"), out var occlusion)) return null;
            values = values with { OcclusionMap = occlusion };
        }
        return ValidMaterialValues(values, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) ? values : null;
    }

    public static JsonValue EncodeMaterialRecipe(A.MaterialRecipe recipe, bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false) => JsonValue.NewObject()
        .Set("values", EncodeMaterialValues(recipe.Values, withMaps, withMetallicRoughness, withOcclusion)).Set("filter", JsonValue.String(recipe.Filter.ToString()))
        .Set("address", JsonValue.String(recipe.Address.ToString()));

    public static A.MaterialRecipe? DecodeMaterialRecipe(JsonValue value, bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false)
    {
        if (!Exact(value, 3) || value.Find("values") is not JsonValue fields ||
            DecodeMaterialValues(fields, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) is not A.MaterialValues values ||
            RebuildToken<A.MaterialFilter>(value.Find("filter")) is not A.MaterialFilter filter ||
            RebuildToken<A.MaterialAddress>(value.Find("address")) is not A.MaterialAddress address) return null;
        return new(values, filter, address);
    }

    internal static JsonValue? EncodeMaterialSave(A.MaterialSave request, bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false)
    {
        var target = request.Target;
        if (target.Name is string name && !ValidImportName(name) || target.Material is { IsNil: true }) return null;
        if ((target.Mode is A.MaterialMode.Create or A.MaterialMode.Copy) && target.Name is null) return null;
        var parameters = JsonValue.NewObject().Set("mode", JsonValue.String(target.Mode.ToString()));
        if (target.Name is string validName) parameters.Set("name", JsonValue.String(validName));
        if (target.Material is AssetGuid material) parameters.Set("material", Encode(material));
        A.MaterialValues values;
        switch (request)
        {
            case A.MaterialSave.Create create: values = create.Values; break;
            case A.MaterialSave.Copy copy: values = copy.Values; break;
            case A.MaterialSave.Edit edit:
                if (edit.ActiveGeneration is { Value.IsNil: true } || !ValidMaterialValues(edit.Expected, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps)) return null;
                parameters.Set("activeGeneration", edit.ActiveGeneration is A.GenerationId generation
                    ? Encode(generation.Value) : JsonValue.Null());
                parameters.Set("expected", EncodeMaterialValues(edit.Expected, withMaps, withMetallicRoughness, withOcclusion));
                values = edit.Values;
                break;
            case A.MaterialSave.Resume: return parameters;
            default: return null;
        }
        return ValidMaterialValues(values, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) ? parameters.Set("values", EncodeMaterialValues(values, withMaps, withMetallicRoughness, withOcclusion)) : null;
    }

    public static A.MaterialSave? DecodeMaterialSave(JsonValue parameters, bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false)
    {
        var mode = RebuildToken<A.MaterialMode>(parameters.Find("mode"));
        var name = parameters.Find("name");
        var material = RebuildGuid(parameters.Find("material"));
        var fields = parameters.Find("values");
        var values = fields is null ? null : DecodeMaterialValues(fields, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps);
        if (mode == A.MaterialMode.Resume && Exact(parameters, 2) && material is AssetGuid resume)
            return new A.MaterialSave.Resume(resume);
        if (values is not A.MaterialValues replacement) return null;
        if (mode == A.MaterialMode.Create && Exact(parameters, 3) && name is { IsString: true } && ValidImportName(name.AsString))
            return new A.MaterialSave.Create(name.AsString, replacement);
        if (mode == A.MaterialMode.Copy && Exact(parameters, 4) && name is { IsString: true } &&
            ValidImportName(name.AsString) && material is AssetGuid copy)
            return new A.MaterialSave.Copy(name.AsString, copy, replacement);
        if (mode == A.MaterialMode.Edit && Exact(parameters, 5) && material is AssetGuid edit &&
            DecodeMaterialGeneration(parameters.Find("activeGeneration"), out var generation) &&
            parameters.Find("expected") is JsonValue expected && DecodeMaterialValues(expected, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) is A.MaterialValues original)
            return new A.MaterialSave.Edit(edit, generation, original, replacement);
        return null;
    }

    private static bool DecodeMaterialGeneration(JsonValue? value, out A.GenerationId? generation)
    {
        generation = null;
        if (value is null) return false;
        if (value.IsNull) return true;
        if (RebuildGuid(value) is not AssetGuid guid) return false;
        generation = new(guid);
        return true;
    }

    public static A.MaterialRead? DecodeMaterialRead(JsonValue value, bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false)
    {
        if (!Exact(value, 4) || RebuildGuid(value.Find("material")) is not AssetGuid material ||
            value.Find("recipe") is not JsonValue recipe || DecodeMaterialRecipe(recipe, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) is not A.MaterialRecipe decoded ||
            value.Find("editable") is not { IsBool: true } editable ||
            !DecodeMaterialGeneration(value.Find("activeGeneration"), out var generation)) return null;
        return new(material, decoded, editable.AsBool, generation);
    }

    public static A.MaterialJob? DecodeMaterialJob(JsonValue value)
    {
        if (!value.IsObject || RebuildGuid(value.Find("jobId")) is not AssetGuid id ||
            RebuildToken<A.MaterialMode>(value.Find("mode")) is not A.MaterialMode mode ||
            RebuildToken<A.RebuildState>(value.Find("state")) is not A.RebuildState state) return null;
        bool named = mode is A.MaterialMode.Create or A.MaterialMode.Copy;
        bool addressed = mode is not A.MaterialMode.Create;
        var name = value.Find("name");
        var material = value.Find("material");
        var guid = RebuildGuid(material);
        if (named ? name is not { IsString: true } || !ValidImportName(name.AsString) : name is not null) return null;
        if (addressed ? guid is null : material is not null) return null;
        var target = new A.MaterialTarget(mode, name?.AsString, guid);
        int count = 3 + (named ? 1 : 0) + (addressed ? 1 : 0);
        if (state == A.RebuildState.Running) return Exact(value, count) ? new(new(id), target, state) : null;
        if (state == A.RebuildState.Succeeded)
        {
            var saved = RebuildGuid(value.Find("savedMaterial"));
            if (!Exact(value, count + 2) || saved is null ||
                !DecodeMaterialGeneration(value.Find("generation"), out var generation) || generation is null ||
                (mode is A.MaterialMode.Edit or A.MaterialMode.Resume) && saved != guid) return null;
            return new(new(id), target, state, saved, generation);
        }
        if (!Exact(value, count + 1) || value.Find("failure") is not { IsObject: true } failure ||
            RebuildToken<A.MaterialStage>(failure.Find("stage")) is not A.MaterialStage stage ||
            RebuildToken<A.MaterialFailureCode>(failure.Find("code")) is not A.MaterialFailureCode code ||
            RebuildToken<A.RegistrationState>(failure.Find("sourceState")) is not A.RegistrationState source ||
            failure.Find("diagnostic") is not { IsString: true } diagnostic ||
            System.Text.Encoding.UTF8.GetByteCount(diagnostic.AsString) > 4096) return null;
        var exit = failure.Find("exitCode");
        uint? exitCode = null;
        if (exit is not null)
        {
            if (!exit.TryGetUInt64(out ulong parsed) || parsed > uint.MaxValue) return null;
            exitCode = (uint)parsed;
        }
        var savedValue = failure.Find("savedMaterial");
        var durable = RebuildGuid(savedValue);
        if (!Exact(failure, 4 + (exit is null ? 0 : 1) + (savedValue is null ? 0 : 1)) ||
            savedValue is not null && durable is null || (durable is not null) != (source == A.RegistrationState.Committed) ||
            durable is not null && (mode is A.MaterialMode.Edit or A.MaterialMode.Resume) && durable != guid) return null;
        return new(new(id), target, state, Failure: new(stage, code, diagnostic.AsString, exitCode, source, durable));
    }

    public static A.MaterialStatus? DecodeMaterialStatus(JsonValue value)
    {
        var job = value.Find("job");
        if (!Exact(value, job is null ? 2 : 3) || value.Find("workspaceNeedsRepair") is not { IsBool: true } repair ||
            !DecodeMaterialGeneration(value.Find("activeGeneration"), out var generation)) return null;
        var decoded = job is null ? null : DecodeMaterialJob(job);
        if (job is not null && decoded is null) return null;
        return new(generation, decoded, repair.AsBool);
    }

    public static A.MaterialDraftPreview? DecodeMaterialDraftPreview(JsonValue value, AssetGuid mesh, A.MaterialRecipe recipe, bool withMaps = false, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false)
    {
        if (!Exact(value, 6) || RebuildGuid(value.Find("mesh")) != mesh ||
            value.Find("recipe") is not JsonValue fields || DecodeMaterialRecipe(fields, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) != recipe) return null;
        var bytes = DecodePreviewPng(value);
        return bytes is null ? null : new(mesh, recipe, PreviewDimension, PreviewDimension, bytes);
    }

    public static A.MaterialError? DecodeMaterialError(JsonValue value) => RebuildToken<A.MaterialError>(value);
}
