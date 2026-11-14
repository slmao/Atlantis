using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using A = Atlantis.Gameplay.Assets;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;

namespace Atlantis.Mcp;

internal sealed record ToolMapping(Tool Tool, string Method, Func<JsonValue, JsonValue>? Compose = null);

/// <summary>
/// Spec 0070 R2's closed exact tier. Immutable declarations; concurrent calls are serialized by
/// IAtlantisSession. Results preserve the wire value, including in-band errors and deferred failures.
/// </summary>
internal sealed partial class ToolRegistry(IAtlantisSession session)
{
    private const string AssetModel = " Read-only catalog facts; this does not edit the authoring document. A changed build requires Runtime restart.";
    private const string RebuildModel = " The authoring document and running projection stay independent. Activation requires an explicit local Runtime restart.";
    private const string WorldModel = " Runtime World writes are disposable runtime-side tweaks; "
        + "persistent edits belong to the authoring document and land on explicit authoring.apply.";
    private const string AuthoringModel = " Edits change the authoring document; only explicit "
        + "authoring.apply_tracked accepts a snapshot; only its Applied result proves the Runtime swap. After submission, explicitly call authoring.drain_failures "
        + "and correlate tickets before applying: a ticket alone is not edit success. Apply may be held while paused; use apply_tracked/apply_status for matched terminal results, including failure and supersession.";
    private const string ControlModel = " Runtime control does not edit the authoring document; "
        + "document edits land on explicit authoring.apply, while Runtime World writes are disposable tweaks.";

    internal static ImmutableArray<ToolMapping> Mappings { get; } =
    [
        Declare("world.list_entities", "connection.listEntities", "List entities sorted by GUID.", WorldModel),
        Declare("world.get_property", "connection.getProperty", "Read a property by its wire address.", WorldModel,
            ToolSchemas.Parameters(("address", ToolSchemas.WorldAddress))),
        Declare("world.submit_transaction", "connection.submitTransaction",
            "Submit one atomic command list; returns its ticket. Refusals arrive later via world.drain_failures.", WorldModel,
            ToolSchemas.Parameters(("commands", ToolSchemas.Array(ToolSchemas.WorldCommand)))),
        Declare("world.drain_failures", "connection.drainFailures", "Return and clear this client's deferred command failures.", WorldModel),
        Declare("world.schema", "connection.schema", "Read schema types, fields, IDs and constants in the wire's section 6.7 shape.", WorldModel),
        Declare("assets.list", "assets.list", "List the fixed cooked-catalog snapshot in GUID order.", AssetModel),
        Declare("assets.get", "assets.get", "Read one catalog record and its direct dependencies.", AssetModel,
            ToolSchemas.Parameters(("guid", ToolSchemas.String))),
        Declare("assets.preview_mesh", "assets.previewMesh", "Render one cooked mesh as a fixed geometry PNG, without scene materials or lighting.",
            AssetModel, ToolSchemas.Parameters(("guid", ToolSchemas.String))),
        Declare("assets.preview_mesh_material", "assets.previewMeshMaterial",
            "Render one cooked mesh with one supported opaque PBR material under fixed preview lighting.",
            AssetModel, ToolSchemas.Parameters(("meshGuid", ToolSchemas.String), ("materialGuid", ToolSchemas.String))),
        Declare("assets.material_read", "assets.materialRead", "Read an eligible cooked material recipe and edit authority.",
            AssetModel, ToolSchemas.Parameters(("material", ToolSchemas.String))),
        Declare("assets.material_save", "assets.materialSave", "Admit Create, Copy, Edit or Resume. Keep the job ID. " +
            "On a missing receipt verify the durable target and settlement before retrying.", RebuildModel, ToolSchemas.MaterialSaveParameters()),
        Declare("assets.material_status", "assets.materialStatus", "Query a retained material job. Latest is a hint, not a receipt.",
            RebuildModel, ToolSchemas.RebuildStatusParameters()),
        Declare("assets.preview_material_draft", "assets.previewMaterialDraft", "Render an unsaved material recipe with a cooked mesh.",
            AssetModel, ToolSchemas.MaterialDraftParameters()),
        Declare("assets.material_read_with_maps", "assets.materialReadWithMaps", "Read the complete opaque PBR recipe.",
            AssetModel, ToolSchemas.Parameters(("material", ToolSchemas.String))),
        Declare("assets.material_save_with_maps", "assets.materialSaveWithMaps", "Admit a complete PBR recipe job. Keep the job ID. " +
            "On a missing receipt verify the durable target and settlement before retrying.", RebuildModel,
            ToolSchemas.MaterialSaveParameters(withMaps: true)),
        Declare("assets.preview_material_draft_with_maps", "assets.previewMaterialDraftWithMaps",
            "Render the complete draft on a cooked mesh.", AssetModel, ToolSchemas.MaterialDraftParameters(withMaps: true)),
        Declare("assets.import", "assets.import", "Register a project inbox source or resume a committed import.", RebuildModel,
            ToolSchemas.ImportParameters()),
        Declare("assets.import_status", "assets.importStatus", "Read-only status for a retained import job or the latest admission.", RebuildModel,
            ToolSchemas.RebuildStatusParameters()),
        Declare("assets.texture_import", "assets.textureImport", "Register one managed sRGB inbox PNG or resume its committed registration.",
            RebuildModel, ToolSchemas.ImportParameters()),
        Declare("assets.texture_import_status", "assets.textureImportStatus", "Read-only status for a texture job and its independent workspace repair state.",
            RebuildModel, ToolSchemas.RebuildStatusParameters()),
        Declare("assets.rebuild", "assets.rebuild", "Admit a rebuild for an existing asset GUID. Poll rebuild_status by the returned ID.", RebuildModel,
            ToolSchemas.Parameters(("asset", ToolSchemas.String))),
        Declare("assets.rebuild_status", "assets.rebuildStatus", "Read-only status for a retained rebuild or the latest admitted job.", RebuildModel,
            ToolSchemas.RebuildStatusParameters()),
        Declare("authoring.list_nodes", "authoring.listNodes", "List nodes in document order.", AuthoringModel),
        Declare("authoring.get_property", "authoring.getProperty", "Read a document property by its wire scene address.", AuthoringModel,
            ToolSchemas.Parameters(("address", ToolSchemas.SceneAddress))),
        Declare("authoring.submit_transaction", "authoring.submitTransaction",
            "Submit one atomic document command list; returns its ticket. Refusals are deferred under that ticket.", AuthoringModel,
            ToolSchemas.Parameters(("commands", ToolSchemas.Array(ToolSchemas.AuthoringCommand)))),
        Declare("authoring.drain_failures", "authoring.drainFailures",
            "Return and clear all this connection's outstanding document command failures, in order, not only the last transaction's.", AuthoringModel),
        Declare("authoring.open", "authoring.open", "Open or reopen the running scene's source document.", AuthoringModel),
        Declare("authoring.document", "authoring.document", "Read document identity, provenance, source and node count; null before open.", AuthoringModel),
        Declare("authoring.apply", "authoring.apply", "Legacy acceptance only, not reload success. Prefer apply_tracked then apply_status.", AuthoringModel),
        Declare("authoring.apply_tracked", "authoring.applyTracked", "Accept the current document snapshot and return its receipt; no implicit wait or retry. Inspect apply_status for that id.", AuthoringModel),
        Declare("authoring.apply_status", "authoring.applyStatus", "Read a retained request and current projection non-destructively; absent/null id is global newest, never proof of a lost caller receipt. Inspect state, not tool success.", AuthoringModel,
            ToolSchemas.ApplyStatusParameters()),
        Declare("authoring.save", "authoring.save", "Save a hand-authored source; imported sources are refused.", AuthoringModel),
        Declare("authoring.save_as", "authoring.saveAs", "Create a named hand-authored copy without retargeting the document. Inspect the destination and confirm settlement before retrying a lost reply.", AuthoringModel,
            ToolSchemas.Parameters(("name", ToolSchemas.String))),
        Declare("authoring.create_scene", "authoring.createScene", "Create a camera-only scene source without opening it. Inspect both destination files and confirm settlement before retrying a lost reply.", AuthoringModel,
            ToolSchemas.Parameters(("name", ToolSchemas.String))),
        Declare("authoring.active_camera", "authoring.activeCamera", "Read the document's active camera node or null.", AuthoringModel),
        Declare("authoring.schema", "authoring.schema", "Read scene schema types, fields, IDs, constants and declared target asset types in the wire's section 6.7 shape.", AuthoringModel),
        Declare("control.status", "control.status", "Read pause state, frame, scene and sceneGeneration.", ControlModel),
        Declare("control.pause", "control.pause", "Hold Runtime commands and pending scene reloads.", ControlModel),
        Declare("control.resume", "control.resume", "Resume Runtime command application and pending scene reloads.", ControlModel),
        Declare("control.step", "control.step", "Run the requested frames; return the frame report and optional capture path plus PNG image content.", ControlModel,
            ToolSchemas.Parameters(("request", ToolSchemas.StepRequest))),
        Declare("control.raycast", "control.raycast", "Read the nearest mesh hit or null; NoGeometry when pick geometry is unavailable.", ControlModel,
            ToolSchemas.Parameters(("ray", ToolSchemas.Ray))),
        Declare("control.set_viewport_camera", "control.setViewportCamera", "Set this client's viewport camera; null restores the scene camera.", ControlModel,
            ToolSchemas.Parameters(("camera", ToolSchemas.Nullable(ToolSchemas.Camera)))),
        Declare("control.set_viewport_overlay", "control.setViewportOverlay", "Replace this client's overlay; null clears it.", ControlModel,
            ToolSchemas.Parameters(("overlay", ToolSchemas.Nullable(ToolSchemas.Overlay)))),
        Declare("control.diagnostics", "control.diagnostics", "Read diagnostics after a sequence, up to max entries.", ControlModel,
            ToolSchemas.Parameters(("after", ToolSchemas.Integer), ("max", ToolSchemas.Integer))),
    ];

    internal static ImmutableArray<ToolMapping> AllMappings => Mappings.AddRange(ConvenienceMappings).AddRange(EventMappings);

    private static ToolMapping Declare(string name, string method, string description, string model, JsonElement? schema = null) =>
        new(new Tool { Name = name, Description = description + model, InputSchema = schema ?? ToolSchemas.Parameters() }, method);

    /// <summary>Sets the list and call handlers of <paramref name="options"/> over the tools.</summary>
    public void Wire(McpServerOptions options)
    {
        options.Handlers = new McpServerHandlers
        {
            ListToolsHandler = (request, token) =>
            {
                ListToolsResult result = new()
                {
                    Tools = AllMappings.Select(m => m.Tool).ToImmutableArray(),
                };
                return ValueTask.FromResult(result);
            },
            CallToolHandler = (request, token) => ValueTask.FromResult(
                ForProtocol(Invoke(request.Params, token), request.Server.NegotiatedProtocolVersion)),
        };
    }

    // Before 2026-07-28, MCP requires an object here. Only the MCP envelope changes;
    // the original Atlantis JSON text, error flag and any image content stay intact.
    internal static CallToolResult ForProtocol(CallToolResult result, string? protocolVersion)
    {
        if (protocolVersion is not null && string.CompareOrdinal(protocolVersion, "2026-07-28") < 0
            && result.StructuredContent is JsonElement structured && structured.ValueKind != JsonValueKind.Object)
            result.StructuredContent = JsonSerializer.SerializeToElement(
                new Dictionary<string, JsonElement> { ["result"] = structured });
        return result;
    }

    internal CallToolResult Invoke(CallToolRequestParams? request, CancellationToken token = default)
    {
        token.ThrowIfCancellationRequested();
        ToolMapping? mapping = AllMappings.FirstOrDefault(m => m.Tool.Name == request?.Name);
        if (mapping is null) return Error("UnknownTool: no tool is named '" + request?.Name + "'");
        JsonElement parameters = JsonSerializer.SerializeToElement(request?.Arguments ?? new Dictionary<string, JsonElement>());
        if (!ToolSchemas.Matches(mapping.Tool.InputSchema, parameters))
            return Error("InvalidArguments: arguments must match the tool's input schema");
        try
        {
            JsonValue wireParameters = Json.Parse(parameters.GetRawText());
            if (mapping.Compose is not null) wireParameters = mapping.Compose(wireParameters);
            if (mapping.Method == "assets.previewMesh") return PreviewMesh(wireParameters);
            if (mapping.Method == "assets.previewMeshMaterial") return PreviewMeshMaterial(wireParameters);
            if (mapping.Method is "assets.materialRead" or "assets.materialSave" or "assets.materialStatus" or
                "assets.previewMaterialDraft" or "assets.materialReadWithMaps" or "assets.materialSaveWithMaps" or
                "assets.previewMaterialDraftWithMaps") return MaterialOperation(mapping.Method, wireParameters);
            if (mapping.Method is "assets.textureImport" or "assets.textureImportStatus")
                return TextureOperation(mapping.Method, wireParameters);
            if (mapping.Method == "authoring.createScene") return CreateScene(wireParameters);
            if (mapping.Method == "authoring.saveAs") return SaveAs(wireParameters);
            bool rebuild = mapping.Method == "assets.rebuild", rebuildStatus = mapping.Method == "assets.rebuildStatus";
            bool import = mapping.Method == "assets.import", importStatus = mapping.Method == "assets.importStatus";
            string? importName = import ? wireParameters.Find("name")?.AsString : null;
            string? importMode = import ? wireParameters.Find("mode")?.AsString : null;
            if (import && !Codec.ValidImportName(importName))
                return Error("InvalidArguments: invalid managed import name");
            AssetGuid? requested = null;
            if ((rebuild || rebuildStatus || importStatus) && wireParameters.Find(rebuild ? "asset" : "jobId") is JsonValue key)
            {
                if (!key.IsString || !AssetGuid.TryParse(key.AsString, out AssetGuid guid) || guid.IsNil)
                    return Error("InvalidArguments: expected a non-nil canonical GUID");
                requested = guid;
            }
            if (mapping.Method is "connection.drainEvents" or "authoring.drainEvents")
            {
                JsonValue subscription = SubscriptionFor(mapping.Method);
                if (subscription.Find("error") is JsonValue subscriptionError) return Forward(subscriptionError, true);
                wireParameters = JsonValue.NewObject().Set("subscription", subscription.Find("result")!);
            }
            JsonValue response = session.Request(mapping.Method, wireParameters);
            if ((rebuild || rebuildStatus || import || importStatus) && Codec.IsLegacyAssetRebuildRefusal(response))
                return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
            if (response.Find("error") is JsonValue error) return Forward(error, true);
            if (response.Find("result") is not JsonValue result) return Error("ProtocolError: response has no result or error");
            if (rebuild || rebuildStatus)
            {
                JsonValue? ok = result.Find("ok"), err = result.Find("err");
                if ((ok is null) == (err is null)) return Error("ProtocolError: malformed rebuild outcome");
                if (err is not null)
                    return Codec.DecodeRebuildError(err, rebuild) is not null ? Forward(result, true)
                        : Error("ProtocolError: malformed rebuild refusal");
                if (rebuild)
                    return Codec.DecodeRebuildJob(ok!) is { State: Gameplay.Assets.RebuildState.Running } job && job.Asset == requested
                        ? Forward(result, false) : Error("ProtocolError: malformed rebuild receipt");
                var status = Codec.DecodeRebuildStatus(ok!);
                return status is not null && (requested is null || status.Job?.Id.Value == requested)
                    ? Forward(result, status.Job?.State == Gameplay.Assets.RebuildState.Failed)
                    : Error("ProtocolError: malformed rebuild status");
            }
            if (import || importStatus)
            {
                JsonValue? ok = result.Find("ok"), err = result.Find("err");
                if ((ok is null) == (err is null)) return Error("ProtocolError: malformed import outcome");
                if (err is not null)
                    return Codec.DecodeImportError(err, import) is not null ? Forward(result, true)
                        : Error("ProtocolError: malformed import refusal");
                if (import)
                    return Codec.DecodeImportJob(ok!) is { State: Gameplay.Assets.RebuildState.Running } job &&
                        job.Name == importName && job.Mode.ToString() == importMode
                        ? Forward(result, false) : Error("ProtocolError: malformed import receipt");
                var status = Codec.DecodeImportStatus(ok!);
                return status is not null && (requested is null || status.Job?.Id.Value == requested)
                    ? Forward(result, status.Job?.State == Gameplay.Assets.RebuildState.Failed)
                    : Error("ProtocolError: malformed import status");
            }
            bool refused = result.Find("err") is not null
                || (mapping.Method is "connection.drainFailures" or "authoring.drainFailures"
                    && result.IsArray && result.Items.Count > 0);
            CallToolResult forwarded = Forward(result, refused);
            if (!refused && mapping.Method == "control.step"
                && wireParameters.Find("request")?.Find("image") is { IsString: true })
                AddCapture(forwarded, result);
            return forwarded;
        }
        catch (RemoteException e)
        {
            return Error(e.Message);
        }
    }

    private CallToolResult TextureOperation(string method, JsonValue parameters)
    {
        bool start = method == "assets.textureImport";
        string? name = parameters.Find("name")?.AsString, mode = parameters.Find("mode")?.AsString;
        if (start && !Codec.ValidImportName(name)) return Error("InvalidArguments: invalid texture import name");
        AssetGuid? id = null;
        if (parameters.Find("jobId") is { } field)
        {
            if (!field.IsString || !AssetGuid.TryParse(field.AsString, out var guid) || guid.IsNil)
                return Error("InvalidArguments: expected a non-nil canonical GUID");
            id = guid;
        }
        string unknown = $"Texture {mode} outcome unknown for {name}. Inspect registered source and complete generations before retrying.";
        CallToolResult Malformed() => Error(start ? "ProtocolError: " + unknown : "ProtocolError: invalid texture job status");
        JsonValue response;
        try { response = session.Request(method, parameters); }
        catch (RemoteException e) when (start) { return Error(e.Error + ": " + unknown); }
        if (!response.IsObject || response.Members.Count != 2 || Codec.CountMember(response, "id") != 1)
            return Malformed();
        if (response.Find("error") is { IsObject: true } error && error.Members.Count == 2 &&
            Codec.IsLegacyAssetRebuildRefusal(response))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1) return Malformed();
        if (result.Find("err") is { } refused)
            return Codec.DecodeImportError(refused, start) is not null ? Forward(result, true) : Malformed();
        if (result.Find("ok") is not { } ok) return Malformed();
        if (start)
            return Codec.DecodeTextureImportJob(ok) is { State: Gameplay.Assets.RebuildState.Running } admitted &&
                   admitted.Name == name && admitted.Mode.ToString() == mode ? Forward(result, false) : Malformed();
        return Codec.DecodeTextureImportStatus(ok) is { } status && (id is null || status.Job?.Id.Value == id)
            ? Forward(result, status.Job?.State == Gameplay.Assets.RebuildState.Failed) : Malformed();
    }

    private CallToolResult MaterialOperation(string method, JsonValue parameters)
    {
        AssetGuid? GuidMember(string name) => parameters.Find(name) is { IsString: true } value &&
            AssetGuid.TryParse(value.AsString, out var guid) && !guid.IsNil ? guid : null;
        var material = GuidMember("material");
        var mesh = GuidMember("mesh");
        var job = GuidMember("jobId");
        bool withMaps = method.EndsWith("WithMaps", StringComparison.Ordinal);
        bool read = method is "assets.materialRead" or "assets.materialReadWithMaps";
        bool save = method is "assets.materialSave" or "assets.materialSaveWithMaps";
        bool preview = method is "assets.previewMaterialDraft" or "assets.previewMaterialDraftWithMaps";
        var request = save ? Codec.DecodeMaterialSave(parameters, withMaps) : null;
        var recipe = parameters.Find("recipe") is JsonValue fields ? Codec.DecodeMaterialRecipe(fields, withMaps) : null;
        if (read && material is null || save && request is null ||
            method == "assets.materialStatus" && parameters.Find("jobId") is not null && job is null ||
            preview && (mesh is null || recipe is null))
            return Error("InvalidArguments: invalid material operation parameters");
        string Target() => request?.Target.Name ?? request?.Target.Material?.ToString() ?? "";
        CallToolResult Malformed() => Error(request is null ? "ProtocolError: malformed material reply" :
            "ProtocolError: material save outcome unknown for " + Target() +
            ". Verify durable source and generation and confirm settlement before retrying.");
        JsonValue response;
        try { response = session.Request(method, parameters); }
        catch (RemoteException e) when (request is not null)
        {
            return Error(new RemoteException(e.Error, $"{e.Message}; material save outcome unknown for {Target()}. " +
                "Latest status is a hint. Verify durable source and generation and confirm settlement before retrying.").Message);
        }
        if (!response.IsObject || response.Members.Count != 2 || Codec.CountMember(response, "id") != 1 ||
            Codec.CountMember(response, "result") + Codec.CountMember(response, "error") != 1) return Malformed();
        if (response.Find("error") is JsonValue error)
            return error.IsObject && error.Members.Count == 2 &&
                Codec.CountMember(error, "code") == 1 && Codec.CountMember(error, "message") == 1 &&
                Codec.IsLegacyAssetRebuildRefusal(response)
                ? Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true) : Malformed();
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1) return Malformed();
        if (result.Find("err") is JsonValue refused)
        {
            var refusal = Codec.DecodeMaterialError(refused);
            return refusal is not null && (!withMaps || refusal != A.MaterialError.Unsupported)
                ? Forward(result, true) : Malformed();
        }
        if (result.Find("ok") is not JsonValue ok) return Malformed();
        if (read)
            return Codec.DecodeMaterialRead(ok, withMaps)?.Material == material ? Forward(result, false) : Malformed();
        if (request is not null)
            return Codec.DecodeMaterialJob(ok) is { State: A.RebuildState.Running } receipt && receipt.Target == request.Target
                ? Forward(result, false) : Malformed();
        if (method == "assets.materialStatus")
            return Codec.DecodeMaterialStatus(ok) is A.MaterialStatus status && (job is null || status.Job?.Id.Value == job)
                ? Forward(result, status.Job?.State == A.RebuildState.Failed) : Malformed();
        var image = Codec.DecodeMaterialDraftPreview(ok, mesh!.Value, recipe!, withMaps);
        return image is null ? Malformed() : ForwardPreview(JsonValue.NewObject().Set("mesh", Codec.Encode(image.Mesh))
            .Set("recipe", Codec.EncodeMaterialRecipe(image.Recipe, withMaps)), image.Width, image.Height, image.PngBytes);
    }

    private CallToolResult PreviewMesh(JsonValue parameters)
    {
        if (!AssetGuid.TryParse(parameters.Find("guid")?.AsString, out AssetGuid guid) || guid.IsNil)
            return Error("InvalidArguments: expected a non-nil canonical mesh GUID");
        var outcome = session.PreviewMesh(guid);
        if (!outcome.IsOk)
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String(outcome.Error.ToString())), true);
        var image = outcome.Value;
        return ForwardPreview(JsonValue.NewObject().Set("guid", Codec.Encode(image.Guid)), image.Width, image.Height, image.PngBytes);
    }

    private CallToolResult PreviewMeshMaterial(JsonValue parameters)
    {
        if (!AssetGuid.TryParse(parameters.Find("meshGuid")?.AsString, out AssetGuid meshGuid) || meshGuid.IsNil)
            return Error("InvalidArguments: expected a non-nil canonical mesh GUID");
        if (!AssetGuid.TryParse(parameters.Find("materialGuid")?.AsString, out AssetGuid materialGuid) || materialGuid.IsNil)
            return Error("InvalidArguments: expected a non-nil canonical material GUID");
        var outcome = session.PreviewMeshMaterial(meshGuid, materialGuid);
        if (!outcome.IsOk)
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String(outcome.Error.ToString())), true);
        var image = outcome.Value;
        return ForwardPreview(JsonValue.NewObject().Set("meshGuid", Codec.Encode(image.MeshGuid))
            .Set("materialGuid", Codec.Encode(image.MaterialGuid)), image.Width, image.Height, image.PngBytes);
    }

    private static CallToolResult ForwardPreview(JsonValue metadata, int width, int height, byte[] png)
    {
        const string mime = "image/png";
        metadata.Set("width", JsonValue.Number((long)width)).Set("height", JsonValue.Number((long)height))
            .Set("mimeType", JsonValue.String(mime));
        CallToolResult result = Forward(JsonValue.NewObject().Set("ok", metadata), false);
        result.Content = [.. result.Content, ImageContentBlock.FromBytes(png, mime)];
        return result;
    }

    private CallToolResult SaveAs(JsonValue parameters)
    {
        string? name = parameters.Find("name")?.AsString;
        if (!Codec.ValidSaveAsName(name))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("InvalidName")), true);
        JsonValue reply;
        try { reply = session.Request("authoring.saveAs", parameters); }
        catch (RemoteException e)
        {
            return Error("Save As outcome unknown. Inspect the destination and confirm settlement before retrying. " + e.Message);
        }
        if (Codec.IsLegacySaveAsRefusal(reply))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        var error = reply.Find("error");
        var result = reply.Find("result");
        if (error is not null && result is null && error.Find("code") is { IsString: true, AsString: "NoAuthoring" })
            return Forward(error, true);
        if (error is null && result is not null)
        {
            var ok = result.Find("ok");
            var err = result.Find("err");
            if (ok is null && err is not null && Codec.DecodeSaveAsError(err) is not null) return Forward(result, true);
            if (err is null && ok is not null && Codec.DecodeSavedScene(ok) is { } copy && copy.Source == Codec.SaveAsSource(name!))
                return Forward(result, false);
        }
        return Error("ProtocolError: malformed Save As outcome. Inspect the destination and confirm settlement before retrying.");
    }

    private CallToolResult CreateScene(JsonValue parameters)
    {
        string? name = parameters.Find("name")?.AsString;
        if (!Codec.ValidSaveAsName(name))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("InvalidName")), true);
        JsonValue reply;
        try { reply = session.Request("authoring.createScene", parameters); }
        catch (RemoteException e)
        {
            return Error("Create Scene outcome unknown for " + Codec.SaveAsSource(name!) + ". Inspect both destination files and confirm settlement before retrying. " + e.Message);
        }
        if (Codec.IsLegacyCreateSceneRefusal(reply))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        var error = reply.Find("error");
        var result = reply.Find("result");
        if (error is not null && result is null && error.Find("code") is { IsString: true, AsString: "NoAuthoring" })
            return Forward(error, true);
        if (error is null && result is not null)
        {
            var ok = result.Find("ok");
            var err = result.Find("err");
            if (ok is null && err is not null && Codec.DecodeCreateSceneError(err) is not null) return Forward(result, true);
            if (err is null && ok is not null && Codec.DecodeCreatedScene(ok) is { } copy && copy.Source == Codec.SaveAsSource(name!))
                return Forward(result, false);
        }
        return Error("ProtocolError: malformed Create Scene outcome for " + Codec.SaveAsSource(name!) + ". Inspect both destination files and confirm settlement before retrying.");
    }

    private static CallToolResult Forward(JsonValue value, bool error)
    {
        string json = Json.Write(value);
        return new CallToolResult
        {
            IsError = error,
            StructuredContent = JsonSerializer.Deserialize<JsonElement>(json),
            Content = [new TextContentBlock { Text = json }],
        };
    }

    private static CallToolResult Error(string reason) => new()
    {
        IsError = true,
        Content = [new TextContentBlock { Text = reason }],
    };
}
