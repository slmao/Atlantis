using System;
using System.Collections.Generic;
using A = Atlantis.Gameplay.Assets;

namespace Atlantis.Gameplay.Remote;

/// <summary>
/// Cooked catalog, preview and asset management. Not thread-safe; the session's
/// thread. Owned copies; in-band refusals preserve the session, transport or
/// malformed typed results throw RemoteException. No connect-time request.
/// </summary>
public sealed class AssetSession
{
    private readonly RemoteSession _session;
    internal AssetSession(RemoteSession session) => _session = session;
    public Outcome<IReadOnlyList<A.AssetSummary>, A.AssetError> ListAssets()
    {
        JsonValue value = _session.Call("assets.list");
        CheckEnvelope(value, "assets.list");
        return _session.InBand<IReadOnlyList<A.AssetSummary>, A.AssetError>(value, "assets.list", v =>
        {
            if (!v.IsArray) return null;
            var list = new List<A.AssetSummary>();
            foreach (JsonValue item in v.Items)
            {
                A.AssetSummary? s = Codec.DecodeAssetSummary(item);
                if (s is null || list.Count > 0 && string.CompareOrdinal(list[^1].Guid.ToString(), s.Guid.ToString()) >= 0) return null;
                list.Add(s);
            }
            return list;
        }, Codec.DecodeAssetError);
    }
    public Outcome<A.AssetDetails, A.AssetError> GetAsset(AssetGuid guid)
    {
        if (guid == default) throw new ArgumentException("a non-nil asset GUID is required", nameof(guid));
        JsonValue value = _session.Call("assets.get", JsonValue.NewObject().Set("guid", Codec.Encode(guid)));
        CheckEnvelope(value, "assets.get");
        return _session.InBand<A.AssetDetails, A.AssetError>(value, "assets.get", v =>
            Codec.DecodeAssetDetails(v) is A.AssetDetails d && d.Guid == guid ? d : null, Codec.DecodeAssetError);
    }

    public Outcome<A.StaticGroup, A.StaticGroupError> StaticGroup(AssetGuid guid)
    {
        if (guid.IsNil) throw new ArgumentException("a non-nil Scene GUID is required", nameof(guid));
        const string method = "assets.staticGroup";
        JsonValue response = _session.AwaitResponse(_session.Send(method, JsonValue.NewObject().Set("guid", Codec.Encode(guid))));
        if (!response.IsObject || response.Members.Count != 2 || Codec.CountMember(response, "id") != 1)
            throw _session.Malformed(method);
        if (Codec.IsUnsupportedStaticGroup(response))
            return Outcome<A.StaticGroup, A.StaticGroupError>.Err(A.StaticGroupError.Unsupported);
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1)
            throw _session.Malformed(method);
        return _session.InBand<A.StaticGroup, A.StaticGroupError>(result, method,
            value => Codec.DecodeStaticGroup(value, guid), Codec.DecodeStaticGroupError);
    }

    /// <summary>Requests one still of a cooked mesh. No scene state changes and no automatic retry.</summary>
    public Outcome<A.MeshPreview, A.PreviewError> PreviewMesh(AssetGuid guid)
    {
        if (guid.IsNil) throw new ArgumentException("a non-nil asset GUID is required", nameof(guid));
        return AssetRequest("assets.previewMesh", JsonValue.NewObject().Set("guid", Codec.Encode(guid)),
            A.PreviewError.Unsupported, Codec.DecodePreviewError, value => Codec.DecodeMeshPreview(value, guid));
    }

    /// <summary>Requests one read-only still for a cooked mesh and material. In-band refusals preserve the session.</summary>
    public Outcome<A.MeshMaterialPreview, A.MeshMaterialPreviewError> PreviewMeshMaterial(AssetGuid meshGuid, AssetGuid materialGuid)
    {
        if (meshGuid.IsNil) throw new ArgumentException("a non-nil mesh GUID is required", nameof(meshGuid));
        if (materialGuid.IsNil) throw new ArgumentException("a non-nil material GUID is required", nameof(materialGuid));
        const string method = "assets.previewMeshMaterial";
        var parameters = JsonValue.NewObject().Set("meshGuid", Codec.Encode(meshGuid)).Set("materialGuid", Codec.Encode(materialGuid));
        JsonValue response = _session.AwaitResponse(_session.Send(method, parameters));
        if (!response.IsObject || response.Members.Count != 2 || Codec.CountMember(response, "id") != 1 || Codec.CountMember(response, "result") + Codec.CountMember(response, "error") != 1)
            throw _session.Malformed(method);
        if (response.Find("error") is JsonValue error)
        {
            if (error.IsObject && error.Members.Count == 2 && Codec.CountMember(error, "code") == 1 && Codec.CountMember(error, "message") == 1 &&
                Codec.IsLegacyAssetRebuildRefusal(response))
                return Outcome<A.MeshMaterialPreview, A.MeshMaterialPreviewError>.Err(A.MeshMaterialPreviewError.Unsupported);
            throw _session.Malformed(method);
        }
        if (response.Find("result") is not JsonValue result || Codec.CountMember(result, "ok") + Codec.CountMember(result, "err") != 1)
            throw _session.Malformed(method);
        return _session.InBand<A.MeshMaterialPreview, A.MeshMaterialPreviewError>(result, method,
            value => Codec.DecodeMeshMaterialPreview(value, meshGuid, materialGuid), Codec.DecodeMeshMaterialPreviewError);
    }

    public Outcome<A.RebuildJob, A.RebuildError> Rebuild(AssetGuid asset)
    {
        if (asset.IsNil) throw new ArgumentException("a non-nil asset GUID is required", nameof(asset));
        return AssetRequest("assets.rebuild", JsonValue.NewObject().Set("asset", Codec.Encode(asset)),
            A.RebuildError.Unsupported, v => Codec.DecodeRebuildError(v, true),
            value => Codec.DecodeRebuildJob(value) is { State: A.RebuildState.Running } job && job.Asset == asset ? job : null);
    }
    /// <summary>Admits a display rename; query the returned ID using RebuildStatus. Never retries.</summary>
    public Outcome<A.RebuildJob, A.RenameTextureError> RenameTexture(AssetGuid asset, A.GenerationId expectedGeneration, string displayName)
    {
        if (asset.IsNil || expectedGeneration.Value.IsNil) throw new ArgumentException("non-nil asset and generation GUIDs are required");
        ArgumentNullException.ThrowIfNull(displayName);
        _session.ThrowIfFailed(); // Known not sent.
        const string method = "assets.renameTexture";
        var request = JsonValue.NewObject().Set("asset", Codec.Encode(asset))
            .Set("expectedGeneration", Codec.Encode(expectedGeneration.Value)).Set("displayName", JsonValue.String(displayName));
        try
        {
            var response = _session.AwaitResponse(_session.Send(method, request));
            if (!response.IsObject || response.Members.Count != 2 || Codec.CountMember(response, "id") != 1 ||
                Codec.CountMember(response, "error") + Codec.CountMember(response, "result") != 1)
                throw _session.Malformed(method);
            if (response.Find("error") is { IsObject: true } error && error.Members.Count == 2 &&
                Codec.CountMember(error, "code") == 1 && Codec.CountMember(error, "message") == 1 &&
                Codec.IsLegacyAssetRebuildRefusal(response))
                return Outcome<A.RebuildJob, A.RenameTextureError>.Err(A.RenameTextureError.Unsupported);
            if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1 ||
                Codec.CountMember(result, "ok") + Codec.CountMember(result, "err") != 1)
                throw _session.Malformed(method);
            return _session.InBand<A.RebuildJob, A.RenameTextureError>(result, method,
                value => Codec.DecodeRebuildJob(value) is { State: A.RebuildState.Running } job && job.Asset == asset ? job : null,
                Codec.DecodeRenameTextureError);
        }
        catch (RemoteException error)
        {
            throw new RemoteException(error, $"Texture rename outcome unknown for {asset} in generation {expectedGeneration.Value} to '{displayName}'. " +
                "Inspect the workspace and generations, and confirm the previous request settled before retrying.");
        }
    }

    public Outcome<A.RebuildJob, A.MoveTextureError> MoveTexture(AssetGuid asset, A.GenerationId expectedGeneration, string expectedLogicalPath, string destinationLogicalPath)
    {
        if (asset.IsNil || expectedGeneration.Value.IsNil) throw new ArgumentException("non-nil asset and generation GUIDs are required");
        ArgumentNullException.ThrowIfNull(expectedLogicalPath);
        ArgumentNullException.ThrowIfNull(destinationLogicalPath);
        _session.ThrowIfFailed(); // Known not sent.
        const string method = "assets.moveTexture";
        var request = JsonValue.NewObject().Set("asset", Codec.Encode(asset))
            .Set("expectedGeneration", Codec.Encode(expectedGeneration.Value)).Set("expectedLogicalPath", JsonValue.String(expectedLogicalPath))
            .Set("destinationLogicalPath", JsonValue.String(destinationLogicalPath));
        try
        {
            var response = _session.AwaitResponse(_session.Send(method, request));
            if (!response.IsObject || response.Members.Count != 2 || Codec.CountMember(response, "id") != 1 ||
                Codec.CountMember(response, "error") + Codec.CountMember(response, "result") != 1)
                throw _session.Malformed(method);
            if (response.Find("error") is { IsObject: true } error && error.Members.Count == 2 &&
                Codec.CountMember(error, "code") == 1 && Codec.CountMember(error, "message") == 1 &&
                Codec.IsLegacyAssetRebuildRefusal(response))
                return Outcome<A.RebuildJob, A.MoveTextureError>.Err(A.MoveTextureError.Unsupported);
            if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1 ||
                Codec.CountMember(result, "ok") + Codec.CountMember(result, "err") != 1)
                throw _session.Malformed(method);
            return _session.InBand<A.RebuildJob, A.MoveTextureError>(result, method,
                value => Codec.DecodeRebuildJob(value) is { State: A.RebuildState.Running } job && job.Asset == asset ? job : null,
                Codec.DecodeMoveTextureError);
        }
        catch (RemoteException error)
        {
            throw new RemoteException(error, $"Texture move outcome unknown for {asset} in generation {expectedGeneration.Value} to '{destinationLogicalPath}'. " +
                "Inspect the workspace and generations, and confirm the previous request settled before retrying.");
        }
    }

    public Outcome<A.TextureRemovalCheck, A.TextureRemovalError> CheckTextureRemoval(
        AssetGuid asset, A.GenerationId expectedGeneration) =>
        AssetRequest("assets.checkTextureRemoval", RemovalTarget(asset, expectedGeneration),
            A.TextureRemovalError.Unsupported, Codec.DecodeTextureRemovalError,
            v => Codec.DecodeTextureRemovalCheck(v) is { State: A.TextureRemovalState.Checking } check &&
                 check.Asset == asset && check.ExpectedGeneration == expectedGeneration ? check : null);

    public Outcome<A.TextureRemovalCheck, A.TextureRemovalError> TextureRemovalCheckStatus(A.TextureRemovalCheckId checkId) =>
        AssetRequest("assets.textureRemovalCheckStatus", JsonValue.NewObject().Set("checkId", EncodeCheckId(checkId)),
            A.TextureRemovalError.Unsupported, Codec.DecodeTextureRemovalError,
            v => Codec.DecodeTextureRemovalCheck(v) is { } check && check.Id == checkId ? check : null);

    public Outcome<A.RebuildJob, A.TextureRemovalError> RemoveTexture(
        AssetGuid asset, A.GenerationId expectedGeneration, A.TextureRemovalCheckId checkId)
    {
        var request = RemovalTarget(asset, expectedGeneration).Set("checkId", EncodeCheckId(checkId));
        _session.ThrowIfFailed();
        try
        {
            return AssetRequest("assets.removeTexture", request, A.TextureRemovalError.Unsupported,
                Codec.DecodeTextureRemovalError,
                v => Codec.DecodeRebuildJob(v) is { State: A.RebuildState.Running } job && job.Asset == asset ? job : null);
        }
        catch (RemoteException error)
        {
            throw new RemoteException(error, $"Texture removal outcome unknown for {asset} in generation {expectedGeneration.Value}. " +
                "Inspect the workspace and published generations and confirm the request settled before checking again.");
        }
    }

    private static JsonValue RemovalTarget(AssetGuid asset, A.GenerationId generation)
    {
        if (asset.IsNil || generation.Value.IsNil) throw new ArgumentException("non-nil asset and generation GUIDs are required");
        return JsonValue.NewObject().Set("asset", Codec.Encode(asset)).Set("expectedGeneration", Codec.Encode(generation.Value));
    }

    private static JsonValue EncodeCheckId(A.TextureRemovalCheckId id)
    {
        if (id.Value.IsNil) throw new ArgumentException("a non-nil check GUID is required", nameof(id));
        return Codec.Encode(id.Value);
    }

    public Outcome<A.RebuildStatus, A.RebuildError> RebuildStatus(A.RebuildId? jobId = null)
    {
        if (jobId is { Value.IsNil: true }) throw new ArgumentException("a non-nil job GUID is required", nameof(jobId));
        JsonValue parameters = JsonValue.NewObject();
        if (jobId is A.RebuildId id) parameters.Set("jobId", Codec.Encode(id.Value));
        try
        {
            return AssetRequest("assets.rebuildStatus", parameters, A.RebuildError.Unsupported,
                v => Codec.DecodeRebuildError(v, false), value =>
                Codec.DecodeRebuildStatus(value) is A.RebuildStatus status && (jobId is null || status.Job?.Id == jobId)
                    ? status : null);
        }
        catch (RemoteException error) when (jobId is A.RebuildId retained)
        {
            throw new RemoteException(error, $"Asset job {retained.Value} outcome unknown. Retain the ID and check settlement before retrying.");
        }
    }
    public Outcome<A.ImportJob, A.ImportError> ImportAsset(string name, A.ImportMode mode = A.ImportMode.Register)
    {
        if (!Codec.ValidImportName(name)) throw new ArgumentException("invalid managed import name", nameof(name));
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        JsonValue parameters = JsonValue.NewObject().Set("name", JsonValue.String(name)).Set("mode", JsonValue.String(mode.ToString()));
        return AssetRequest("assets.import", parameters, A.ImportError.Unsupported,
            v => Codec.DecodeImportError(v, true), value =>
                Codec.DecodeImportJob(value) is { State: A.RebuildState.Running } job && job.Name == name && job.Mode == mode
                    ? job : null);
    }
    public Outcome<A.ImportStatus, A.ImportError> ImportStatus(A.ImportId? jobId = null)
    {
        if (jobId is { Value.IsNil: true }) throw new ArgumentException("a non-nil job GUID is required", nameof(jobId));
        JsonValue parameters = JsonValue.NewObject();
        if (jobId is A.ImportId id) parameters.Set("jobId", Codec.Encode(id.Value));
        return AssetRequest("assets.importStatus", parameters, A.ImportError.Unsupported,
            v => Codec.DecodeImportError(v, false), value =>
                Codec.DecodeImportStatus(value) is A.ImportStatus status && (jobId is null || status.Job?.Id == jobId)
                    ? status : null);
    }
    public Outcome<A.MaterialRead, A.MaterialError> MaterialRead(AssetGuid material) => ReadMaterial(material, false);
    public Outcome<A.MaterialRead, A.MaterialError> MaterialReadWithMaps(AssetGuid material) => ReadMaterial(material, true);

    public Outcome<A.MaterialRead, A.MaterialError> MaterialReadWithMetallicRoughness(AssetGuid material) =>
        ReadMaterial(material, false, withMetallicRoughness: true);

    public Outcome<A.MaterialRead, A.MaterialError> MaterialReadWithOcclusion(AssetGuid material) =>
        ReadMaterial(material, false, withOcclusion: true);

    public Outcome<A.MaterialRead, A.MaterialError> MaterialReadWithCombinedMaps(AssetGuid material) =>
        ReadMaterial(material, false, withOcclusion: true, withCombinedMaps: true);

    private static string MaterialMethod(string member, bool withMaps, bool withMetallicRoughness, bool withOcclusion, bool withCombinedMaps) =>
        member + (withCombinedMaps ? "WithCombinedMaps" : withOcclusion ? "WithOcclusion" : withMetallicRoughness ? "WithMetallicRoughness" : withMaps ? "WithMaps" : "");

    private Outcome<A.MaterialRead, A.MaterialError> ReadMaterial(AssetGuid material, bool withMaps, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false)
    {
        if (material.IsNil) throw new ArgumentException("a non-nil material GUID is required", nameof(material));
        return MaterialRequest(MaterialMethod("assets.materialRead", withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps), JsonValue.NewObject().Set("material", Codec.Encode(material)),
            value => Codec.DecodeMaterialRead(value, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) is { } read && read.Material == material ? read : null);
    }

    /// <summary>Admits a managed PNG job. A missing admission receipt does not determine the durable outcome.</summary>
    public Outcome<A.TextureImportJob, A.ImportError> TextureImport(string name, A.ImportMode mode = A.ImportMode.Register) =>
        ImportTexture(name, mode, "assets.textureImport", "Texture");

    /// <summary>Admits a managed linear +Y PNG job. Verify durable registration and settlement if the receipt is lost.</summary>
    public Outcome<A.TextureImportJob, A.ImportError> NormalMapImport(string name, A.ImportMode mode = A.ImportMode.Register) =>
        ImportTexture(name, mode, "assets.normalMapImport", "Normal map");

    /// <summary>Admits a managed linear PNG with G roughness and B metallic.</summary>
    public Outcome<A.TextureImportJob, A.ImportError> MetallicRoughnessImport(string name, A.ImportMode mode = A.ImportMode.Register) =>
        ImportTexture(name, mode, "assets.metallicRoughnessImport", "Metallic-roughness map");

    public Outcome<A.TextureImportJob, A.ImportError> OcclusionImport(string name, A.ImportMode mode = A.ImportMode.Register) =>
        ImportTexture(name, mode, "assets.occlusionImport", "Occlusion map");

    private Outcome<A.TextureImportJob, A.ImportError> ImportTexture(string name, A.ImportMode mode, string method, string kind)
    {
        if (!Codec.ValidImportName(name)) throw new ArgumentException("invalid texture import name", nameof(name));
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        _session.ThrowIfFailed();
        var parameters = JsonValue.NewObject().Set("name", JsonValue.String(name)).Set("mode", JsonValue.String(mode.ToString()));
        try
        {
            return TextureRequest(method, parameters, true, value =>
                Codec.DecodeTextureImportJob(value) is { State: A.RebuildState.Running } job && job.Name == name &&
                job.Mode == mode ? job : null);
        }
        catch (RemoteException error)
        {
            throw new RemoteException(error, $"{kind} {mode} outcome unknown for {name}. " +
                "Inspect the registered texture and complete generations and confirm settlement before retrying.");
        }
    }

    public Outcome<A.TextureImportStatus, A.ImportError> TextureImportStatus(A.TextureImportId? jobId = null) =>
        ReadTextureStatus(jobId, "assets.textureImportStatus");

    /// <summary>Reads a normal map job by ID, or the latest normal map job, and the shared repair flag.</summary>
    public Outcome<A.TextureImportStatus, A.ImportError> NormalMapImportStatus(A.TextureImportId? jobId = null) =>
        ReadTextureStatus(jobId, "assets.normalMapImportStatus", "Normal map");

    public Outcome<A.TextureImportStatus, A.ImportError> MetallicRoughnessImportStatus(A.TextureImportId? jobId = null) =>
        ReadTextureStatus(jobId, "assets.metallicRoughnessImportStatus", "Metallic-roughness map");

    public Outcome<A.TextureImportStatus, A.ImportError> OcclusionImportStatus(A.TextureImportId? jobId = null) =>
        ReadTextureStatus(jobId, "assets.occlusionImportStatus", "Occlusion map");

    private Outcome<A.TextureImportStatus, A.ImportError> ReadTextureStatus(A.TextureImportId? jobId, string method, string? kind = null)
    {
        if (jobId is { Value.IsNil: true }) throw new ArgumentException("a non-nil job GUID is required", nameof(jobId));
        var parameters = JsonValue.NewObject();
        if (jobId is A.TextureImportId id) parameters.Set("jobId", Codec.Encode(id.Value));
        try
        {
            return TextureRequest(method, parameters, false, value =>
                Codec.DecodeTextureImportStatus(value) is { } status && (jobId is null || status.Job?.Id == jobId) ? status : null);
        }
        catch (RemoteException error) when (kind is not null && jobId is A.TextureImportId)
        {
            throw new RemoteException(error, $"{kind} job {jobId.Value.Value} outcome unknown. Query the retained ID after reconnecting.");
        }
    }

    private Outcome<T, A.ImportError> TextureRequest<T>(string method, JsonValue parameters, bool start,
        Func<JsonValue, T?> decode) where T : class
    {
        var response = _session.AwaitResponse(_session.Send(method, parameters));
        if (!response.IsObject || response.Members.Count != 2 || Codec.CountMember(response, "id") != 1)
            throw _session.Malformed(method);
        if (response.Find("error") is { IsObject: true } error && error.Members.Count == 2 &&
            Codec.CountMember(error, "code") == 1 && Codec.CountMember(error, "message") == 1 &&
            Codec.IsLegacyAssetRebuildRefusal(response))
            return Outcome<T, A.ImportError>.Err(A.ImportError.Unsupported);
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1)
            throw _session.Malformed(method);
        return _session.InBand<T, A.ImportError>(result, method, decode, v => Codec.DecodeImportError(v, start));
    }

    /// <summary>Admits one job without waiting or retrying. A lost receipt retains its transport or protocol category.</summary>
    public Outcome<A.MaterialJob, A.MaterialError> MaterialSave(A.MaterialSave request) => SaveMaterial(request, false);
    public Outcome<A.MaterialJob, A.MaterialError> MaterialSaveWithMaps(A.MaterialSave request) => SaveMaterial(request, true);

    public Outcome<A.MaterialJob, A.MaterialError> MaterialSaveWithMetallicRoughness(A.MaterialSave request) =>
        SaveMaterial(request, false, withMetallicRoughness: true);

    public Outcome<A.MaterialJob, A.MaterialError> MaterialSaveWithOcclusion(A.MaterialSave request) =>
        SaveMaterial(request, false, withOcclusion: true);

    public Outcome<A.MaterialJob, A.MaterialError> MaterialSaveWithCombinedMaps(A.MaterialSave request) =>
        SaveMaterial(request, false, withOcclusion: true, withCombinedMaps: true);

    private Outcome<A.MaterialJob, A.MaterialError> SaveMaterial(A.MaterialSave request, bool withMaps, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false)
    {
        ArgumentNullException.ThrowIfNull(request);
        var parameters = Codec.EncodeMaterialSave(request, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps);
        if (parameters is null) return Outcome<A.MaterialJob, A.MaterialError>.Err(A.MaterialError.InvalidRecipe);
        _session.ThrowIfFailed();
        var target = request.Target;
        try
        {
            return MaterialRequest(MaterialMethod("assets.materialSave", withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps), parameters,
                value => Codec.DecodeMaterialJob(value) is { State: A.RebuildState.Running } job &&
                    job.Target == target ? job : null);
        }
        catch (RemoteException error)
        {
            throw new RemoteException(error, $"Material {target.Mode} outcome unknown for {target.Name ?? target.Material?.ToString()}. " +
                "Use the job ID if known. Latest status is only a hint; verify the durable source and generation " +
                "and confirm the previous request has settled before explicitly retrying.");
        }
    }

    public Outcome<A.MaterialStatus, A.MaterialError> MaterialStatus(A.MaterialJobId? jobId = null)
    {
        if (jobId is { Value.IsNil: true }) throw new ArgumentException("a non-nil job GUID is required", nameof(jobId));
        var parameters = JsonValue.NewObject();
        if (jobId is A.MaterialJobId id) parameters.Set("jobId", Codec.Encode(id.Value));
        return MaterialRequest("assets.materialStatus", parameters, value =>
            Codec.DecodeMaterialStatus(value) is { } status && (jobId is null || status.Job?.Id == jobId) ? status : null);
    }

    public Outcome<A.MaterialDraftPreview, A.MaterialError> PreviewMaterialDraft(AssetGuid mesh, A.MaterialRecipe recipe) =>
        PreviewDraft(mesh, recipe, false);
    public Outcome<A.MaterialDraftPreview, A.MaterialError> PreviewMaterialDraftWithMaps(AssetGuid mesh, A.MaterialRecipe recipe) =>
        PreviewDraft(mesh, recipe, true);

    public Outcome<A.MaterialDraftPreview, A.MaterialError> PreviewMaterialDraftWithMetallicRoughness(AssetGuid mesh, A.MaterialRecipe recipe) =>
        PreviewDraft(mesh, recipe, false, withMetallicRoughness: true);

    public Outcome<A.MaterialDraftPreview, A.MaterialError> PreviewMaterialDraftWithOcclusion(AssetGuid mesh, A.MaterialRecipe recipe) =>
        PreviewDraft(mesh, recipe, false, withOcclusion: true);

    public Outcome<A.MaterialDraftPreview, A.MaterialError> PreviewMaterialDraftWithCombinedMaps(AssetGuid mesh, A.MaterialRecipe recipe) =>
        PreviewDraft(mesh, recipe, false, withOcclusion: true, withCombinedMaps: true);

    private Outcome<A.MaterialDraftPreview, A.MaterialError> PreviewDraft(AssetGuid mesh, A.MaterialRecipe recipe, bool withMaps, bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false)
    {
        ArgumentNullException.ThrowIfNull(recipe);
        if (mesh.IsNil || !Codec.ValidMaterialValues(recipe.Values, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) || !Enum.IsDefined(recipe.Filter) ||
            !Enum.IsDefined(recipe.Address))
            return Outcome<A.MaterialDraftPreview, A.MaterialError>.Err(A.MaterialError.InvalidRecipe);
        return MaterialRequest(MaterialMethod("assets.previewMaterialDraft", withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps),
            JsonValue.NewObject().Set("mesh", Codec.Encode(mesh)).Set("recipe", Codec.EncodeMaterialRecipe(recipe, withMaps, withMetallicRoughness, withOcclusion)),
            value => Codec.DecodeMaterialDraftPreview(value, mesh, recipe, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps));
    }

    private Outcome<T, A.MaterialError> MaterialRequest<T>(string method, JsonValue parameters, Func<JsonValue, T?> decode)
        where T : class
    {
        var response = _session.AwaitResponse(_session.Send(method, parameters));
        if (!response.IsObject || response.Members.Count != 2 || Codec.CountMember(response, "id") != 1 ||
            Codec.CountMember(response, "result") + Codec.CountMember(response, "error") != 1)
            throw _session.Malformed(method);
        if (response.Find("error") is JsonValue error)
        {
            if (error.IsObject && error.Members.Count == 2 && Codec.CountMember(error, "code") == 1 && Codec.CountMember(error, "message") == 1 &&
                Codec.IsLegacyAssetRebuildRefusal(response))
                return Outcome<T, A.MaterialError>.Err(A.MaterialError.Unsupported);
            throw _session.Malformed(method);
        }
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1)
            throw _session.Malformed(method);
        return _session.InBand<T, A.MaterialError>(result, method, decode, value =>
        {
            var error = Codec.DecodeMaterialError(value);
            return error == A.MaterialError.Unsupported && (method.EndsWith("WithMaps", StringComparison.Ordinal) ||
                method.EndsWith("WithMetallicRoughness", StringComparison.Ordinal) ||
                method.EndsWith("WithOcclusion", StringComparison.Ordinal) ||
                method.EndsWith("WithCombinedMaps", StringComparison.Ordinal))
                ? null : error;
        });
    }

    private Outcome<T, E> AssetRequest<T, E>(string method, JsonValue parameters, E unsupported,
        Func<JsonValue, E?> decodeError, Func<JsonValue, T?> decode) where T : class where E : struct, Enum
    {
        JsonValue response = _session.AwaitResponse(_session.Send(method, parameters));
        if (Codec.IsLegacyAssetRebuildRefusal(response))
            return Outcome<T, E>.Err(unsupported);
        if (response.Find("error") is not null || response.Find("result") is not JsonValue result)
            throw _session.Malformed(method);
        CheckEnvelope(result, method);
        return _session.InBand<T, E>(result, method, decode, decodeError);
    }
    private void CheckEnvelope(JsonValue value, string method)
    {
        if ((value.Find("ok") is null) == (value.Find("err") is null)) throw _session.Malformed(method);
    }
}
