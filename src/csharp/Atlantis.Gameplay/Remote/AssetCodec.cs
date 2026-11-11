using System;
using System.Buffers.Binary;
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
    private static A.AssetType? DecodeAssetType(JsonValue value) => value.IsString ? value.AsString switch
    {
        "mesh" => A.AssetType.Mesh, "texture" => A.AssetType.Texture, "material" => A.AssetType.Material,
        "scene" => A.AssetType.Scene, "environment" => A.AssetType.Environment, _ => null,
    } : null;
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
        A.AssetType? type = DecodeAssetType(t);
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

    private const int PreviewDimension = 256;
    private const int MaxPreviewPngBytes = 512 * 1024;
    private const int MaxPreviewBase64Chars = 4 * ((MaxPreviewPngBytes + 2) / 3);

    /// <summary>Checks the preview envelope fields and PNG header, leaving image decoding to the consumer.</summary>
    public static A.MeshPreview? DecodeMeshPreview(JsonValue value, AssetGuid requested)
    {
        if (requested.IsNil || value.Find("guid") is not JsonValue guid || DecodeAsset(guid) != requested ||
            value.Find("width") is not JsonValue w || !w.TryGetUInt64(out ulong width) || width != PreviewDimension ||
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
        return new A.MeshPreview(requested, PreviewDimension, PreviewDimension, bytes);
    }

    private static int Base64Value(char c) => c switch
    {
        >= 'A' and <= 'Z' => c - 'A',
        >= 'a' and <= 'z' => c - 'a' + 26,
        >= '0' and <= '9' => c - '0' + 52,
        '+' => 62, '/' => 63, _ => -1,
    };

    public static A.PreviewError? DecodePreviewError(JsonValue value) => RebuildToken<A.PreviewError>(value);

    private static T? RebuildToken<T>(JsonValue? value) where T : struct, Enum =>
        value is { IsString: true } && Enum.TryParse(value.AsString, out T parsed) &&
        Enum.IsDefined(parsed) && parsed.ToString() == value.AsString ? parsed : null;

    /// <summary>Compatibility response for either well-formed new rebuild operation on a pinned legacy host.</summary>
    public static bool IsLegacyAssetRebuildRefusal(JsonValue envelope) =>
        envelope.Find("result") is null && envelope.Find("error") is JsonValue error &&
        error.Find("code") is { IsString: true, AsString: "BadRequest" } &&
        error.Find("message") is { IsString: true, AsString: "unknown or malformed asset query" };

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
}
