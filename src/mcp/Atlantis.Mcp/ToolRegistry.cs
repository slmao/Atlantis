using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text.Json;
using JsonArray = System.Text.Json.Nodes.JsonArray;
using JsonObject = System.Text.Json.Nodes.JsonObject;
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
        Declare("assets.scan_project", "assets.scanProject", "Start a bounded read of the authorized Project source roots.", AssetModel),
        Declare("assets.project_scan_status", "assets.projectScanStatus", "Read one Project scan by its exact Runtime-scoped ID.",
            AssetModel, ToolSchemas.Parameters(("id", ToolSchemas.String))),
        Declare("assets.content_metrics", "assets.contentMetrics",
            "Read on-demand cooked triangle or texture payload facts for up to 32 distinct asset GUIDs.",
            " Read-only metadata from the running asset catalog. Texture bytes describe cooked payloads.",
            ToolSchemas.Parameters(("guids", ToolSchemas.Array(ToolSchemas.String)))),
        Declare("assets.static_group", "assets.staticGroup",
            "Read-only ordered local values and renderable bindings of a managed static glTF group in the active catalog. " +
            "New publications are visible after Runtime restart.",
            AssetModel, ToolSchemas.Parameters(("guid", ToolSchemas.String))),
        Declare("assets.prefab_revision", "assets.prefabRevision", "Read the active frozen Prefab source revision.",
            AssetModel, ToolSchemas.Parameters(("guid", ToolSchemas.String))),
        Declare("assets.prefab", "assets.prefab", "Read the frozen ordered static Prefab from the saved-scene Runtime.",
            AssetModel, ToolSchemas.Parameters(("guid", ToolSchemas.String))),
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
        Declare("assets.material_read_with_metallic_roughness", "assets.materialReadWithMetallicRoughness",
            "Read the opaque PBR recipe including its metallic-roughness slot.", AssetModel, ToolSchemas.Parameters(("material", ToolSchemas.String))),
        Declare("assets.material_read_with_occlusion", "assets.materialReadWithOcclusion",
            "Read the opaque PBR recipe including its occlusion slot.", AssetModel, ToolSchemas.Parameters(("material", ToolSchemas.String))),
        Declare("assets.material_read_with_combined_maps", "assets.materialReadWithCombinedMaps",
            "Read the opaque PBR recipe including its occlusion slot.", AssetModel, ToolSchemas.Parameters(("material", ToolSchemas.String))),
        Declare("assets.material_save_with_metallic_roughness", "assets.materialSaveWithMetallicRoughness",
            "Admit one complete material recipe job. Retain the receipt and confirm settlement before retrying.",
            RebuildModel, ToolSchemas.MaterialSaveParameters(withMetallicRoughness: true)),
        Declare("assets.material_save_with_occlusion", "assets.materialSaveWithOcclusion",
            "Admit one complete material recipe job. Retain the receipt and confirm settlement before retrying.",
            RebuildModel, ToolSchemas.MaterialSaveParameters(withOcclusion: true)),
        Declare("assets.material_save_with_combined_maps", "assets.materialSaveWithCombinedMaps",
            "Admit one complete material recipe job. Retain the receipt and confirm settlement before retrying.",
            RebuildModel, ToolSchemas.MaterialSaveParameters(withOcclusion: true)),
        Declare("assets.preview_material_draft_with_metallic_roughness", "assets.previewMaterialDraftWithMetallicRoughness",
            "Render a complete draft on a cooked mesh.", AssetModel, ToolSchemas.MaterialDraftParameters(withMetallicRoughness: true)),
        Declare("assets.preview_material_draft_with_occlusion", "assets.previewMaterialDraftWithOcclusion",
            "Render a complete draft on a cooked mesh.", AssetModel, ToolSchemas.MaterialDraftParameters(withOcclusion: true)),
        Declare("assets.preview_material_draft_with_combined_maps", "assets.previewMaterialDraftWithCombinedMaps",
            "Render a complete draft on a cooked mesh.", AssetModel, ToolSchemas.MaterialDraftParameters(withOcclusion: true)),
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
        Declare("assets.normal_map_import", "assets.normalMapImport", "Register one managed linear +Y inbox PNG or resume its committed registration.",
            RebuildModel, ToolSchemas.ImportParameters()),
        Declare("assets.normal_map_import_status", "assets.normalMapImportStatus", "Read-only status for a normal map job and the independent workspace repair state.",
            RebuildModel, ToolSchemas.RebuildStatusParameters()),
        Declare("assets.metallic_roughness_import", "assets.metallicRoughnessImport", "Register one managed linear metallic-roughness inbox PNG or resume its committed registration.",
            RebuildModel, ToolSchemas.ImportParameters()),
        Declare("assets.occlusion_import", "assets.occlusionImport", "Register one managed linear occlusion inbox PNG or resume its committed registration.",
            RebuildModel, ToolSchemas.ImportParameters()),
        Declare("assets.metallic_roughness_import_status", "assets.metallicRoughnessImportStatus", "Read-only status for a metallic-roughness job and the independent workspace repair state.",
            RebuildModel, ToolSchemas.RebuildStatusParameters()),
        Declare("assets.occlusion_import_status", "assets.occlusionImportStatus", "Read-only status for a occlusion job and the independent workspace repair state.",
            RebuildModel, ToolSchemas.RebuildStatusParameters()),
        Declare("assets.rebuild", "assets.rebuild", "Admit a rebuild for an existing asset GUID. Poll rebuild_status by the returned ID.", RebuildModel,
            ToolSchemas.Parameters(("asset", ToolSchemas.String))),
        Declare("assets.rename_texture", "assets.renameTexture",
            "Publish a display rename for one registered base color texture. Query rebuild_status by the returned ID.", RebuildModel,
            ToolSchemas.Parameters(("asset", ToolSchemas.String), ("expectedGeneration", ToolSchemas.String), ("displayName", ToolSchemas.String))),
        Declare("assets.move_texture", "assets.moveTexture",
            "Publish a logical Assets move for one registered base color texture. Query rebuild_status by the returned ID.", RebuildModel,
            ToolSchemas.Parameters(("asset", ToolSchemas.String), ("expectedGeneration", ToolSchemas.String), ("expectedLogicalPath", ToolSchemas.String), ("destinationLogicalPath", ToolSchemas.String))),
        Declare("assets.check_texture_removal", "assets.checkTextureRemoval",
            "Start a project reference check for one registered base color texture. Poll texture_removal_check_status.", RebuildModel,
            ToolSchemas.TextureRemovalParameters(confirm: false)),
        Declare("assets.texture_removal_check_status", "assets.textureRemovalCheckStatus",
            "Read-only check receipt and blockers. Only a Ready receipt can be confirmed.", RebuildModel,
            ToolSchemas.Parameters(("checkId", ToolSchemas.String))),
        Declare("assets.remove_texture", "assets.removeTexture",
            "Confirm one unused texture registration removal with a Ready receipt. Keep the original inbox PNG. Poll rebuild_status by job ID.", RebuildModel,
            ToolSchemas.TextureRemovalParameters(confirm: true)),
        Declare("assets.static_group_reimport_review", "assets.staticGroupReimportReview",
            "Begin, read or cancel the structural review of one keyed static glTF. Review does not publish.",
            RebuildModel, ToolSchemas.StaticGroupReviewParameters()),
        Declare("assets.commit_static_group_reimport", "assets.commitStaticGroupReimport",
            "Confirm a Ready review once. Poll rebuild_status by job ID. If the outcome is unknown, verify settlement before retrying.",
            RebuildModel, ToolSchemas.Parameters(("id", ToolSchemas.String))),
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
        Declare("authoring.document", "authoring.document", "Read document identity, provenance, source, node count and optional guarded property support; null before open.", AuthoringModel),
        Declare("authoring.apply", "authoring.apply", "Legacy acceptance only, not reload success. Prefer apply_tracked then apply_status.", AuthoringModel),
        Declare("authoring.apply_tracked", "authoring.applyTracked", "Accept the current document snapshot and return its receipt; no implicit wait or retry. Inspect apply_status for that id.", AuthoringModel),
        Declare("authoring.apply_status", "authoring.applyStatus", "Read a retained request and current projection non-destructively; absent/null id is global newest, never proof of a lost caller receipt. Inspect state, not tool success.", AuthoringModel,
            ToolSchemas.ApplyStatusParameters()),
        Declare("authoring.save", "authoring.save", "Save a hand-authored source; imported sources are refused.", AuthoringModel),
        Declare("authoring.document_state", "authoring.documentState", "Read document identity, revision and source persistence; null before open.", AuthoringModel),
        Declare("authoring.save_if_current", "authoring.saveIfCurrent", "Save only the reviewed hand-authored document revision.", AuthoringModel,
            DocumentParameters(allowAbsent: false)),
        Declare("authoring.reopen_if_current", "authoring.reopenIfCurrent", "Reopen only the reviewed revision. Explicit null opens only while absent.", AuthoringModel,
            DocumentParameters(allowAbsent: true)),
        Declare("authoring.recovery_status", "authoring.recoveryStatus", "Read recovery coverage or preview an exact pending copy.", AuthoringModel,
            RecoveryParameters(reading: true)),
        Declare("authoring.restore_recovery", "authoring.restoreRecovery", "Restore the exact copy to the reviewed authoring document. Apply and Save remain explicit.", AuthoringModel,
            RecoveryParameters(reading: false)),
        Declare("authoring.discard_recovery", "authoring.discardRecovery", "Discard only the exact pending copy without changing the document, source or projection.", AuthoringModel,
            RecoveryParameters(reading: false)),
        Declare("authoring.save_as", "authoring.saveAs", "Create a named hand-authored copy without retargeting the document. Inspect the destination and confirm settlement before retrying a lost reply.", AuthoringModel,
            ToolSchemas.Parameters(("name", ToolSchemas.String))),
        Declare("authoring.create_scene", "authoring.createScene", "Create a camera-only scene source without opening it. Inspect both destination files and confirm settlement before retrying a lost reply.", AuthoringModel,
            ToolSchemas.Parameters(("name", ToolSchemas.String))),
        Declare("authoring.update_prefab", "authoring.updatePrefab", "Replace a Prefab source from a committed donor. Explicit Build/Open activates it.",
            AuthoringModel, ToolSchemas.Parameters(("node", ToolSchemas.String), ("prefab", ToolSchemas.String),
                ("expected", ToolSchemas.PrefabDocument), ("revision", ToolSchemas.String))),
        Declare("authoring.sync_prefab", "authoring.syncPrefab", "Synchronize one root or instantiate when prefab is supplied. Runtime Apply is separate.",
            AuthoringModel, ToolSchemas.PrefabSyncParameters()),
        Declare("authoring.replay_prefab", "authoring.replayPrefab", "Replay the exact before and after snapshots of one Prefab transaction.",
            AuthoringModel, ToolSchemas.Parameters(("node", ToolSchemas.String), ("expected", ToolSchemas.PrefabDocument),
                ("before", ToolSchemas.PrefabSubtree), ("after", ToolSchemas.PrefabSubtree))),
        Declare("authoring.create_prefab", "authoring.createPrefab", "Register a committed static subtree at the expected document revision. Inspect both files and confirm settlement before retrying a lost reply.",
            AuthoringModel, ToolSchemas.Parameters(("name", ToolSchemas.String), ("node", ToolSchemas.String),
                ("expected", ToolSchemas.ParametersNode(("documentId", ToolSchemas.String), ("documentRevision", ToolSchemas.Integer))))),
        Declare("authoring.active_camera", "authoring.activeCamera", "Read the document's active camera node or null.", AuthoringModel),
        Declare("authoring.schema", "authoring.schema", "Read scene schema types, fields, IDs, constants and declared target asset types in the wire's section 6.7 shape.", AuthoringModel),
        Declare("control.status", "control.status", "Read pause state, frame, scene and sceneGeneration.", ControlModel),
        Declare("control.pause", "control.pause", "Hold Runtime commands and pending scene reloads.", ControlModel),
        Declare("control.resume", "control.resume", "Resume Runtime command application and pending scene reloads.", ControlModel),
        Declare("control.step", "control.step", "Run the requested frames; return the frame report and optional capture path plus PNG image content.", ControlModel,
            ToolSchemas.Parameters(("request", ToolSchemas.StepRequest))),
        Declare("control.raycast", "control.raycast", "Read the nearest mesh hit or null; NoGeometry when pick geometry is unavailable.", ControlModel,
            ToolSchemas.Parameters(("ray", ToolSchemas.Ray))),
        Declare("control.viewport_visibility", "control.viewportVisibility",
            "Read the hosted viewport policy stamp and caller ownership.", ControlModel, ToolSchemas.Parameters()),
        Declare("control.set_viewport_visibility", "control.setViewportVisibility",
            "Replace the hosted viewport policy at the expected stamp. Null restores the default view.", ControlModel,
            ToolSchemas.Parameters(("sceneGeneration", ToolSchemas.Integer), ("revision", ToolSchemas.Integer),
                ("policy", ToolSchemas.VisibilityPolicy))),
        Declare("control.pick_viewport", "control.pickViewport",
            "Pick against a presented viewport revision with one selection ray or one or two alignment rays.", ControlModel,
            ToolSchemas.Parameters(("sceneGeneration", ToolSchemas.Integer), ("revision", ToolSchemas.Integer),
                ("kind", ToolSchemas.ViewportPickKind), ("rays", ToolSchemas.AlignmentRays))),
        Declare("control.pick_material_binding", "control.pickMaterialBinding",
            "Read the single explicit material binding at the nearest unambiguous opaque surface.", ControlModel,
            ToolSchemas.MaterialBindingParameters()),
        Declare("control.capture_material_diagnostic", "control.captureMaterialDiagnostic",
            "Capture normal shading and isolated base color for one verified material binding.", ControlModel,
            ToolSchemas.MaterialDiagnosticCapture()),
        Declare("control.capture_render_look", "control.captureRenderLook",
            "Capture two complete render looks at one fixed camera in the settled projection.", ControlModel,
            ToolSchemas.RenderLookCapture()),
        Declare("control.pick_alignment_faces", "control.pickAlignmentFaces",
            "Read checked rectangular first-hit faces for one or two ordered world-space rays.", ControlModel,
            ToolSchemas.Parameters(("rays", ToolSchemas.AlignmentRays))),
        Declare("control.viewport_navigation", "control.viewportNavigation", "Read the active scene camera and ordered world bounds. Pass [] for the camera only.", ControlModel,
            ToolSchemas.Parameters(("targets", ToolSchemas.NavigationTargets))),
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
            if (mapping.Method is "assets.prefabRevision" or "authoring.updatePrefab" or
                "authoring.syncPrefab" or "authoring.replayPrefab") return PrefabOperation(mapping.Method, wireParameters);
            if (mapping.Method is "assets.scanProject" or "assets.projectScanStatus")
                return ProjectScan(mapping.Method, wireParameters);
            if (mapping.Method == "assets.contentMetrics") return ContentMetrics(wireParameters);
            if (mapping.Method is "assets.staticGroup" or "assets.prefab") return StaticGroup(mapping.Method, wireParameters);
            if (mapping.Method == "assets.previewMesh") return PreviewMesh(wireParameters);
            if (mapping.Method == "assets.previewMeshMaterial") return PreviewMeshMaterial(wireParameters);
            if (mapping.Method is "assets.materialRead" or "assets.materialSave" or "assets.materialStatus" or
                "assets.previewMaterialDraft" or "assets.materialReadWithMaps" or "assets.materialSaveWithMaps" or
                "assets.previewMaterialDraftWithMaps" or "assets.materialReadWithMetallicRoughness" or
                "assets.materialSaveWithMetallicRoughness" or "assets.previewMaterialDraftWithMetallicRoughness" or
                "assets.materialReadWithOcclusion" or "assets.materialReadWithCombinedMaps" or "assets.materialSaveWithOcclusion" or "assets.materialSaveWithCombinedMaps" or "assets.previewMaterialDraftWithOcclusion" or "assets.previewMaterialDraftWithCombinedMaps")
                return MaterialOperation(mapping.Method, wireParameters);
            if (mapping.Method is "assets.textureImport" or "assets.textureImportStatus" or "assets.normalMapImport" or "assets.normalMapImportStatus" or
                "assets.metallicRoughnessImport" or "assets.metallicRoughnessImportStatus" or
                "assets.occlusionImport" or "assets.occlusionImportStatus")
                return TextureOperation(mapping.Method, wireParameters);
            if (mapping.Method == "authoring.createScene") return CreateScene(wireParameters);
            if (mapping.Method == "authoring.createPrefab") return CreatePrefab(wireParameters);
            if (mapping.Method == "authoring.saveAs") return SaveAs(wireParameters);
            if (mapping.Method is "authoring.documentState" or "authoring.saveIfCurrent" or "authoring.reopenIfCurrent")
                return DocumentOperation(mapping.Method, wireParameters);
            if (mapping.Method is "authoring.recoveryStatus" or "authoring.restoreRecovery" or "authoring.discardRecovery")
                return RecoveryOperation(mapping.Method, wireParameters);
            if (mapping.Method is "control.viewportVisibility" or "control.setViewportVisibility" or "control.pickViewport")
                return ViewportPolicyCall(mapping.Method, wireParameters);
            if (mapping.Method == "control.pickMaterialBinding") return PickMaterialBinding(wireParameters);
            if (mapping.Method == "control.captureMaterialDiagnostic") return CaptureMaterialDiagnostic(wireParameters);
            if (mapping.Method == "control.captureRenderLook") return CaptureRenderLook(wireParameters);
            if (mapping.Method == "control.pickAlignmentFaces") return PickAlignmentFaces(wireParameters);
            if (mapping.Method == "control.viewportNavigation") return ViewportNavigation(wireParameters);
            if (mapping.Method == "assets.renameTexture") return RenameTexture(wireParameters);
            if (mapping.Method == "assets.moveTexture") return MoveTexture(wireParameters);
            if (mapping.Method is "assets.checkTextureRemoval" or "assets.textureRemovalCheckStatus" or "assets.removeTexture")
                return TextureRemoval(mapping.Method, wireParameters);
            if (mapping.Method is "assets.staticGroupReimportReview" or "assets.commitStaticGroupReimport")
                return StaticGroupReimport(mapping.Method, wireParameters);
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
            if (mapping.Method == "authoring.submitTransaction" &&
                wireParameters.Find("commands")!.Items.Any(c => c.Find("kind")?.AsString switch
                {
                    "SwapAdjacentSiblings" => !_supportsSiblingOrder,
                    "MoveSiblingRelative" => !_supportsSiblingMove,
                    _ => false
                }))
                return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
            if (mapping.Method == "authoring.submitTransaction")
            {
                var commands = wireParameters.Find("commands")!.Items;
                bool property = commands.Any(c => c.Find("kind")?.AsString == "RequireDocumentRevision");
                bool transform = commands.Any(c => c.Find("kind")?.AsString == "RequireTransformBatchRevision");
                bool duplicate = commands.Any(c => c.Find("kind")?.AsString == "RequireDuplicateBatchRevision");
                bool prefab = commands.Any(c => c.Find("kind")?.AsString == "SetPrefabOrigin");
                bool names = commands.Any(c => c.Find("kind")?.AsString == "RequireNameBatchDocument");
                bool looks = commands.Any(c => c.Find("kind")?.AsString == "RequireRenderLookDocument");
                if (property || transform || duplicate || prefab || names || looks)
                {
                    JsonValue document = session.Request("authoring.document", JsonValue.NewObject());
                    if (document.Find("error") is JsonValue discoveryError) return Forward(discoveryError, true);
                    var info = document.Find("result") is JsonValue descriptor ? Codec.DecodeDocumentInfo(descriptor) : null;
                    if ((property && info?.SupportsGuardedPropertyTransactions != true) ||
                        (transform && info?.SupportsGuardedTransformTransactions != true) ||
                        (duplicate && info?.SupportsGuardedDuplicateTransactions != true) ||
                        (names && info?.SupportsGuardedNameTransactions != true) ||
                        (looks && info?.SupportsGuardedRenderLookTransactions != true) ||
                        (prefab && info?.SupportsPrefabOrigin != true))
                        return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
                }
            }
            JsonValue response;
            try { response = session.Request(mapping.Method, wireParameters); }
            catch (RemoteException transport) when (rebuildStatus && requested is AssetGuid id)
            {
                return Error(transport.Message + $" Asset job {id} outcome unknown. Retain the ID and check settlement.");
            }
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

    private CallToolResult StaticGroupReimport(string method, JsonValue parameters)
    {
        bool confirm = method == "assets.commitStaticGroupReimport";
        string? mode = parameters.Find("mode")?.AsString;
        if (parameters.Find(mode == "begin" ? "scene" : "id") is not { IsString: true } target ||
            Codec.DecodeAsset(target) is not AssetGuid guid || guid.IsNil || target.AsString != guid.ToString())
            return Error("InvalidArguments: expected a non-nil canonical GUID");
        if (!confirm && mode is not ("begin" or "read" or "cancel"))
            return Error("InvalidArguments: expected begin, read or cancel");
        bool mayHaveSent = false;
        string unknown = $"Static model reimport outcome unknown for review {guid}. Check settlement before retrying.";
        try
        {
            var response = session.Request(method, parameters, out mayHaveSent);
            if (Codec.IsUnsupportedStaticGroup(response))
                return Forward(Codec.Err(JsonValue.String("Unsupported")), true);
            if (response is { IsObject: true } && response.Members.Count == 2 && Codec.CountMember(response, "id") == 1 &&
                response.Find("result") is { IsObject: true } result && result.Members.Count == 1)
            {
                if (result.Find("err") is { } err && Codec.DecodeStaticGroupReimportError(err) is not null)
                    return Forward(result, true);
                if (result.Find("ok") is { } ok)
                {
                    if (confirm && Codec.DecodeRebuildJob(ok) is { State: A.RebuildState.Running })
                        return Forward(result, false);
                    if (!confirm && Codec.DecodeStaticGroupReview(ok) is { } review &&
                        (mode == "begin" ? review.Scene == guid && review.State == A.StaticGroupReviewState.Pending :
                            review.Id == guid && (mode != "cancel" || review.State == A.StaticGroupReviewState.Expired)))
                        return Forward(result, false);
                }
            }
            return Error(confirm ? "ProtocolError: " + unknown : "ProtocolError: malformed static model review");
        }
        catch (RemoteException error)
        {
            return Error(confirm && mayHaveSent ? error.Message + " " + unknown : error.Message);
        }
    }

    private CallToolResult TextureRemoval(string method, JsonValue parameters)
    {
        bool status = method == "assets.textureRemovalCheckStatus", confirm = method == "assets.removeTexture";
        var asset = ReadGuid("asset");
        var generation = ReadGuid("expectedGeneration");
        var id = ReadGuid("checkId");
        if ((!status && (asset is null || generation is null)) || ((status || confirm) && id is null))
            return Error("InvalidArguments: expected non-nil canonical GUIDs");
        string unknown = $"Texture removal outcome unknown for {asset} in generation {generation}. " +
            "Inspect the workspace and generations and confirm settlement before checking again.";
        bool mayHaveSent = false;
        try
        {
            var response = session.Request(method, parameters, out mayHaveSent);
            if (Codec.IsLegacyAssetRebuildRefusal(response))
                return Forward(Codec.Err(JsonValue.String("Unsupported")), true);
            if (response.IsObject && response.Members.Count == 2 && Codec.CountMember(response, "id") == 1 &&
                response.Find("result") is { IsObject: true } result && result.Members.Count == 1)
            {
                if (result.Find("err") is JsonValue err && Codec.DecodeTextureRemovalError(err) is not null)
                    return Forward(result, true);
                if (result.Find("ok") is JsonValue ok)
                {
                    if (confirm && Codec.DecodeRebuildJob(ok) is { State: A.RebuildState.Running } job && job.Asset == asset)
                        return Forward(result, false);
                    if (!confirm && Codec.DecodeTextureRemovalCheck(ok) is { } receipt &&
                        (status ? receipt.Id.Value == id : receipt.Asset == asset &&
                            receipt.ExpectedGeneration.Value == generation && receipt.State == A.TextureRemovalState.Checking))
                        return Forward(result, receipt.State is not (A.TextureRemovalState.Checking or A.TextureRemovalState.Ready));
                }
            }
            return Error(confirm ? "ProtocolError: " + unknown : "ProtocolError: malformed texture removal check");
        }
        catch (RemoteException error)
        {
            return Error(confirm && mayHaveSent ? error.Message + " " + unknown : error.Message);
        }

        AssetGuid? ReadGuid(string field) => parameters.Find(field) is { IsString: true } value &&
            Codec.DecodeAsset(value) is AssetGuid parsed && !parsed.IsNil && value.AsString == parsed.ToString()
                ? parsed : null;
    }

    private CallToolResult RenameTexture(JsonValue parameters)
    {
        if (Codec.DecodeAsset(parameters.Find("asset")!) is not AssetGuid asset || asset.IsNil ||
            Codec.DecodeAsset(parameters.Find("expectedGeneration")!) is not AssetGuid generation || generation.IsNil ||
            parameters.Find("asset")?.AsString != asset.ToString() ||
            parameters.Find("expectedGeneration")?.AsString != generation.ToString())
            return Error("InvalidArguments: expected non-nil canonical asset and generation GUIDs");
        string label = parameters.Find("displayName")!.AsString;
        string unknown = $"Texture rename outcome unknown for {asset} in generation {generation} to '{label}'. " +
            "Inspect the workspace and generations and confirm settlement before retrying.";
        bool mayHaveSent = false;
        try
        {
            var response = session.Request("assets.renameTexture", parameters, out mayHaveSent);
            if (response.IsObject && response.Members.Count == 2 && Codec.CountMember(response, "id") == 1)
            {
                if (response.Find("error") is { IsObject: true } error && error.Members.Count == 2 &&
                    Codec.CountMember(error, "code") == 1 && Codec.CountMember(error, "message") == 1 &&
                    Codec.IsLegacyAssetRebuildRefusal(response))
                    return Forward(Codec.Err(JsonValue.String("Unsupported")), true);
                if (response.Find("result") is { IsObject: true } result && result.Members.Count == 1)
                {
                    if (result.Find("ok") is JsonValue ok &&
                        Codec.DecodeRebuildJob(ok) is { State: A.RebuildState.Running } job && job.Asset == asset)
                        return Forward(result, false);
                    if (result.Find("err") is JsonValue err && Codec.DecodeRenameTextureError(err) is not null)
                        return Forward(result, true);
                }
            }
            return Error("ProtocolError: " + unknown);
        }
        catch (RemoteException error) { return Error(mayHaveSent ? error.Message + " " + unknown : error.Message); }
    }

    private CallToolResult MoveTexture(JsonValue parameters)
    {
        if (Codec.DecodeAsset(parameters.Find("asset")!) is not AssetGuid asset || asset.IsNil ||
            Codec.DecodeAsset(parameters.Find("expectedGeneration")!) is not AssetGuid generation || generation.IsNil ||
            parameters.Find("asset")?.AsString != asset.ToString() ||
            parameters.Find("expectedGeneration")?.AsString != generation.ToString())
            return Error("InvalidArguments: expected non-nil canonical asset and generation GUIDs");
        string label = parameters.Find("destinationLogicalPath")!.AsString;
        string unknown = $"Texture move outcome unknown for {asset} in generation {generation} to '{label}'. " +
            "Inspect the workspace and generations and confirm settlement before retrying.";
        bool mayHaveSent = false;
        try
        {
            var response = session.Request("assets.moveTexture", parameters, out mayHaveSent);
            if (response.IsObject && response.Members.Count == 2 && Codec.CountMember(response, "id") == 1)
            {
                if (response.Find("error") is { IsObject: true } error && error.Members.Count == 2 &&
                    Codec.CountMember(error, "code") == 1 && Codec.CountMember(error, "message") == 1 &&
                    Codec.IsLegacyAssetRebuildRefusal(response))
                    return Forward(Codec.Err(JsonValue.String("Unsupported")), true);
                if (response.Find("result") is { IsObject: true } result && result.Members.Count == 1)
                {
                    if (result.Find("ok") is JsonValue ok &&
                        Codec.DecodeRebuildJob(ok) is { State: A.RebuildState.Running } job && job.Asset == asset)
                        return Forward(result, false);
                    if (result.Find("err") is JsonValue err && Codec.DecodeMoveTextureError(err) is not null)
                        return Forward(result, true);
                }
            }
            return Error("ProtocolError: " + unknown);
        }
        catch (RemoteException error) { return Error(mayHaveSent ? error.Message + " " + unknown : error.Message); }
    }

    private CallToolResult ViewportPolicyCall(string method, JsonValue parameters)
    {
        CallToolResult Exchange<T>(Func<JsonValue,T?> decode, IReadOnlyList<Ray>? alignment = null) where T : class
        {
            var response = session.Request(method, parameters);
            if (Codec.DecodeViewportReply(response, method, decode, alignment) is not { } typed)
                return Error("ProtocolError: malformed viewport policy reply");
            if (typed.Value is not null) return Forward(response.Find("result")!, false);
            if (typed.Error is not { } error) return Error("ProtocolError: missing viewport refusal");
            if (error.Code == ViewportVisibilityError.RpcFailure) return Error(error.Detail ?? "RPC failure");
            return Forward(response.Find("result") ?? Codec.Err(JsonValue.NewObject()
                .Set("code", JsonValue.String(error.Code.ToString()))), true);
        }

        if (method == "control.viewportVisibility") return Exchange(Codec.DecodeViewportVisibility);
        if (Codec.DecodeViewportStamp(parameters) is not ViewportStamp stamp)
            return Error("InvalidArguments: expected scene generation and policy revision");
        if (method == "control.setViewportVisibility")
        {
            if (parameters.Find("policy") is not { } value) return Error("InvalidArguments: policy required");
            ViewportPolicy? policy = null;
            if (!value.IsNull && (policy = Codec.DecodeViewportPolicy(value, out var problem)) is null)
                return Forward(Codec.Err(JsonValue.NewObject().Set("code", JsonValue.String(problem.ToString()))), true);
            ViewportVisibility? Read(JsonValue result) => Codec.DecodeViewportVisibility(result) is { } state &&
                state.Acknowledges(stamp, policy) ? state : null;
            return Exchange<ViewportVisibility>(Read);
        }
        var kind = parameters.Find("kind")?.AsString switch
        {
            "Selection" => ViewportPickKind.Selection,
            "Alignment" => ViewportPickKind.Alignment,
            _ => (ViewportPickKind?)null
        };
        if (kind is not ViewportPickKind mode || parameters.Find("rays") is not { } input ||
            Codec.DecodeViewportRays(input, mode) is not { } rays)
            return Error("InvalidArguments: expected valid rays for the specified viewport pick");
        return Exchange(result => Codec.DecodeViewportPick(result, stamp, mode, rays),
            mode == ViewportPickKind.Alignment ? rays : null);
    }

    private CallToolResult PickMaterialBinding(JsonValue parameters)
    {
        if (Codec.DecodeMaterialBindingRequest(parameters) is not { } request)
            return Error("InvalidArguments: expected a finite unit ray, valid interval and current generation.");
        var response = session.Request("control.pickMaterialBinding", parameters);
        if (Codec.DecodeMaterialBindingResponse(response, request) is not { } decoded)
            return Error("ProtocolError: malformed material binding reply");
        return response.Find("result") is { } result ? Forward(result, decoded.Error is not null)
            : Error(decoded.Error?.ToString() ?? "ProtocolError: missing material binding reply");
    }

    private CallToolResult CaptureMaterialDiagnostic(JsonValue parameters)
    {
        if (Codec.DecodeMaterialDiagnosticRequest(parameters) is not { } request)
            return Error("InvalidArguments: expected a verified binding and bounded capture.");
        var status = session.Request("control.status", JsonValue.NewObject());
        if (status.Find("error") is { } failure) return Forward(failure, true);
        if (status.Find("result") is not { } value || Codec.DecodeStatus(value) is not { } runtime)
            return Error("ProtocolError: malformed control status.");
        if (!runtime.SupportsMaterialDiagnosticCapture)
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        var response = session.Request("control.captureMaterialDiagnostic", Codec.Encode(request));
        if (response.Find("error") is { } error)
            return Forward(JsonValue.NewObject().Set("error", error)
                .Set("guidance", JsonValue.String("Capture outcome unknown. Verify both output paths before retrying.")), true);
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1)
            return Error("ProtocolError: capture outcome unknown. Verify both output paths before retrying.");
        if (result.Find("err") is { } refusal && Codec.DecodeRenderLookCaptureError(refusal) is not null)
            return Forward(result, true);
        if (result.Find("ok") is { } pair && Codec.DecodeMaterialDiagnosticPair(pair, request) is not null)
            return Forward(result, false);
        return Error("ProtocolError: capture outcome unknown. Verify both output paths before retrying.");
    }

    private CallToolResult CaptureRenderLook(JsonValue parameters)
    {
        if (Codec.DecodeRenderLookCaptureRequest(parameters) is not { } request)
            return Error("InvalidArguments: expected two complete render looks and distinct output paths.");
        var status = session.Request("control.status", JsonValue.NewObject());
        if (status.Find("error") is { } failure) return Forward(failure, true);
        if (status.Find("result") is not { } statusValue || Codec.DecodeStatus(statusValue) is not { } runtime)
            return Error("ProtocolError: malformed control status.");
        if (!runtime.SupportsRenderLookCapture ||
            (request.Extent != RenderLookExtent.Default && !runtime.SupportsRenderLookCaptureExtent))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        var response = session.Request("control.captureRenderLook", Codec.Encode(request));
        if (response.Find("error") is { } error) return Forward(error, true);
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1)
            return Error("ProtocolError: malformed render look capture result.");
        if (result.Find("err") is { } refusal && Codec.DecodeRenderLookCaptureError(refusal) is not null)
            return Forward(result, true);
        if (result.Find("ok") is { } pair && Codec.DecodeRenderLookCapturedPair(pair, request) is not null)
            return Forward(result, false);
        return Error("ProtocolError: invalid render look capture result.");
    }

    private CallToolResult PickAlignmentFaces(JsonValue parameters)
    {
        if (parameters.Find("rays") is not { IsArray: true } items || items.Items.Count is < 1 or > 2)
            return Error("InvalidArguments: supply one or two rays");
        var rays = new List<Ray>();
        foreach (var item in items.Items)
        {
            if (Codec.DecodeAlignmentRay(item) is not { } ray) return Error("InvalidArguments: expected a finite unit ray");
            rays.Add(ray);
        }
        var response = session.Request("control.pickAlignmentFaces", parameters);
        if (Codec.DecodeAlignmentFacesResponse(response, rays) is not { } decoded)
            return Error("ProtocolError: malformed alignment face reply");
        if (decoded.Value is not null) return Forward(response.Find("result")!, false);
        if (decoded.Error is not { } failure) return Error("ProtocolError: missing alignment refusal");
        if (failure.Code == AlignmentFaceError.RpcFailure) return Error(failure.Detail ?? "RPC failure");
        if (response.Find("result") is { } result) return Forward(result, true);
        return Forward(Codec.Err(JsonValue.NewObject().Set("code", JsonValue.String(failure.Code.ToString()))), true);
    }

    private CallToolResult ViewportNavigation(JsonValue parameters)
    {
        var targets = new List<EntityGuid>();
        var seen = new HashSet<EntityGuid>();
        foreach (JsonValue item in parameters.Find("targets")!.Items)
        {
            if (Codec.DecodeEntity(item) is not EntityGuid target || !seen.Add(target))
                return Error("InvalidArguments: expected distinct non-nil canonical entity GUIDs");
            targets.Add(target);
        }
        JsonValue response = session.Request("control.viewportNavigation", parameters);
        ViewportNavigationResult? result = Codec.DecodeViewportNavigationResponse(response, targets);
        if (result?.Value is Atlantis.Gameplay.ViewportNavigation value) return Forward(Codec.Ok(Codec.Encode(value)), false);
        if (result?.Error is not ViewportNavigationFailure failure) return Error("ProtocolError: malformed viewport navigation reply");
        return failure.Code == ViewportNavigationError.RpcFailure
            ? Forward(response.Find("error")!, true)
            : Forward(Codec.Err(Codec.Encode(failure)), true);
    }

    private CallToolResult TextureOperation(string method, JsonValue parameters)
    {
        bool start = method is "assets.textureImport" or "assets.normalMapImport" or "assets.metallicRoughnessImport" or "assets.occlusionImport";
        string kind = method.StartsWith("assets.occlusion", StringComparison.Ordinal) ? "Occlusion map" :
            method.StartsWith("assets.metallicRoughness", StringComparison.Ordinal) ? "Metallic-roughness map" :
            method.StartsWith("assets.normalMap", StringComparison.Ordinal) ? "Normal map" : "Texture";
        string? name = parameters.Find("name")?.AsString, mode = parameters.Find("mode")?.AsString;
        if (start && !Codec.ValidImportName(name)) return Error("InvalidArguments: invalid texture import name");
        AssetGuid? id = null;
        if (parameters.Find("jobId") is { } field)
        {
            if (!field.IsString || !AssetGuid.TryParse(field.AsString, out var guid) || guid.IsNil)
                return Error("InvalidArguments: expected a non-nil canonical GUID");
            id = guid;
        }
        string unknown = $"{kind} {mode} outcome unknown for {name}. Inspect registered source and complete generations and confirm settlement before retrying.";
        string unknownStatus = id is { } retained
            ? $"{kind} job {retained} outcome unknown. Query the retained ID after reconnecting."
            : $"Invalid {kind.ToLowerInvariant()} status.";
        CallToolResult Malformed() => Error("ProtocolError: " + (start ? unknown : unknownStatus));
        JsonValue response;
        try { response = session.Request(method, parameters); }
        catch (RemoteException e) when (start || id is not null)
        {
            return Error(e.Error + ": " + (start ? unknown : unknownStatus));
        }
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
        bool withCombinedMaps = method.EndsWith("WithCombinedMaps", StringComparison.Ordinal);
        bool withOcclusion = withCombinedMaps || method.EndsWith("WithOcclusion", StringComparison.Ordinal);
        bool withMetallicRoughness = method.EndsWith("WithMetallicRoughness", StringComparison.Ordinal);
        bool read = method is "assets.materialRead" or "assets.materialReadWithMaps" or "assets.materialReadWithMetallicRoughness" or "assets.materialReadWithOcclusion" or "assets.materialReadWithCombinedMaps";
        bool save = method is "assets.materialSave" or "assets.materialSaveWithMaps" or "assets.materialSaveWithMetallicRoughness" or "assets.materialSaveWithOcclusion" or "assets.materialSaveWithCombinedMaps";
        bool preview = method is "assets.previewMaterialDraft" or "assets.previewMaterialDraftWithMaps" or "assets.previewMaterialDraftWithMetallicRoughness" or "assets.previewMaterialDraftWithOcclusion" or "assets.previewMaterialDraftWithCombinedMaps";
        var request = save ? Codec.DecodeMaterialSave(parameters, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) : null;
        var recipe = parameters.Find("recipe") is JsonValue fields ? Codec.DecodeMaterialRecipe(fields, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps) : null;
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
            return refusal is not null && (!(withMaps || withMetallicRoughness || withOcclusion) || refusal != A.MaterialError.Unsupported)
                ? Forward(result, true) : Malformed();
        }
        if (result.Find("ok") is not JsonValue ok) return Malformed();
        if (read)
            return Codec.DecodeMaterialRead(ok, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps)?.Material == material ? Forward(result, false) : Malformed();
        if (request is not null)
            return Codec.DecodeMaterialJob(ok) is { State: A.RebuildState.Running } receipt && receipt.Target == request.Target
                ? Forward(result, false) : Malformed();
        if (method == "assets.materialStatus")
            return Codec.DecodeMaterialStatus(ok) is A.MaterialStatus status && (job is null || status.Job?.Id.Value == job)
                ? Forward(result, status.Job?.State == A.RebuildState.Failed) : Malformed();
        var image = Codec.DecodeMaterialDraftPreview(ok, mesh!.Value, recipe!, withMaps, withMetallicRoughness, withOcclusion, withCombinedMaps);
        return image is null ? Malformed() : ForwardPreview(JsonValue.NewObject().Set("mesh", Codec.Encode(image.Mesh))
            .Set("recipe", Codec.EncodeMaterialRecipe(image.Recipe, withMaps, withMetallicRoughness, withOcclusion)), image.Width, image.Height, image.PngBytes);
    }

    private CallToolResult ProjectScan(string method, JsonValue parameters)
    {
        static CallToolResult Malformed() => Error("MalformedResponse: invalid Project scan result");
        bool start = method == "assets.scanProject";
        AssetGuid id = default;
        if (!start && (parameters.Find("id") is not { IsString: true } member ||
            !AssetGuid.TryParse(member.AsString, out id) || id.IsNil || id.ToString() != member.AsString))
            return Error("InvalidArguments: expected a non-nil canonical scan ID");
        var response = session.Request(method, parameters);
        if (Codec.IsUnsupportedStaticGroup(response))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1) return Malformed();
        if (result.Find("err") is JsonValue refusal)
            return Codec.DecodeProjectScanError(refusal) is not null ? Forward(result, true) : Malformed();
        return result.Find("ok") is JsonValue value && Codec.DecodeProjectScan(value) is { } scan &&
            (start ? scan.Pending : scan.Id == id) ? Forward(result, false) : Malformed();
    }

    private CallToolResult ContentMetrics(JsonValue parameters)
    {
        if (Codec.DecodeContentMetricsGuids(parameters.Find("guids")) is not { } guids)
            return Error("InvalidArguments: expected one to 32 distinct canonical asset GUIDs.");
        var response = session.Request("assets.contentMetrics", parameters);
        if (Codec.IsUnsupportedStaticGroup(response))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        if (response is { IsObject: true } && response.Members.Count == 2 && Codec.CountMember(response, "id") == 1 &&
            response.Find("result") is { IsObject: true } result && result.Members.Count == 1)
        {
            if (result.Find("err") is { } err && Codec.DecodeContentMetricsError(err) is not null) return Forward(result, true);
            if (result.Find("ok") is { } ok && Codec.DecodeContentMetrics(ok, guids) is not null) return Forward(result, false);
        }
        return Error("MalformedResponse: invalid cooked content metrics result.");
    }

    private CallToolResult StaticGroup(string method, JsonValue parameters)
    {
        static CallToolResult Malformed() => Error("MalformedResponse: invalid static group result");
        if (!AssetGuid.TryParse(parameters.Find("guid")?.AsString, out AssetGuid guid) || guid.IsNil)
            return Error("InvalidArguments: expected a non-nil canonical Scene GUID");
        var response = session.Request(method, parameters);
        if (Codec.IsUnsupportedStaticGroup(response))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        if (response.Find("result") is not { IsObject: true } result || result.Members.Count != 1)
            return Malformed();
        if (result.Find("err") is JsonValue refusal)
            return Codec.DecodeStaticGroupError(refusal) is not null ? Forward(result, true) : Malformed();
        return result.Find("ok") is JsonValue value && Codec.DecodeStaticGroup(value, guid, method == "assets.prefab") is not null
            ? Forward(result, false) : Malformed();
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

    private static JsonElement DocumentParameters(bool allowAbsent)
    {
        var token = ToolSchemas.ParametersNode(("documentId", ToolSchemas.String),
            ("documentRevision", ToolSchemas.Integer));
        return ToolSchemas.Parameters(("expected", allowAbsent ? ToolSchemas.Nullable(token) : token));
    }

    private CallToolResult DocumentOperation(string method, JsonValue parameters)
    {
        bool reading = method == "authoring.documentState", saving = method == "authoring.saveIfCurrent";
        var expected = parameters.Find("expected");
        var token = expected is null ? null : Codec.DecodeDocumentToken(expected);
        if (!reading && token is null && (saving || expected is not { IsNull: true }))
            return Error("InvalidArguments: expected a non-nil document ID and positive unsigned revision");

        string target = token is { } key ? $"{key.DocumentId}, revision {key.DocumentRevision}" : "the first open";
        string unknown = $" {method} outcome unknown for {target}. Check document state on the original Runtime before retrying.";
        JsonValue reply;
        bool mayHaveSent = true;
        try { reply = session.Request(method, parameters, out mayHaveSent); }
        catch (RemoteException error)
        {
            return Error(reading ? error.Message :
                error.Message + (mayHaveSent ? unknown : $" {method} was not sent."));
        }
        if (Codec.DocumentRpcRefusal(reply, method) is { } refusal) return Error(refusal.ToString());
        if (Codec.DocumentResult(reply) is { } result)
        {
            if (result.Find("ok") is { } ok &&
                (reading && ok.IsNull || Codec.DecodeDocumentState(ok) is { } state &&
                    (reading || Codec.ValidDocumentReceipt(state, token, saving)))) return Forward(result, false);
            if (!reading && result.Find("err") is { } err && Codec.DecodeDocumentError(err, saving) is not null)
                return Forward(result, true);
        }
        return Error("ProtocolError: malformed document reply." + (reading ? "" : unknown));
    }

    private static JsonElement RecoveryParameters(bool reading)
    {
        if (!reading) return ToolSchemas.Parameters(("copyId", ToolSchemas.String),
            ("expected", ToolSchemas.ParametersNode(("documentId", ToolSchemas.String),
                ("documentRevision", ToolSchemas.Integer))));
        var shape = (JsonObject)ToolSchemas.ParametersNode(("copyId", ToolSchemas.String));
        shape["required"] = new JsonArray();
        return JsonSerializer.SerializeToElement(shape);
    }

    private CallToolResult RecoveryOperation(string method, JsonValue parameters)
    {
        bool reading = method == "authoring.recoveryStatus";
        var copyField = parameters.Find("copyId");
        var copy = copyField is { IsString: true } && EntityGuid.TryParse(copyField.AsString, out var id) &&
            !id.IsNil && id.ToString() == copyField.AsString ? id : (EntityGuid?)null;
        var expected = parameters.Find("expected");
        var token = expected is { IsObject: true } ? Codec.DecodeDocumentToken(expected) : null;
        if ((copyField is not null && copy is null) || (!reading && (copy is null || token is null)))
            return Error("InvalidArguments: expected a canonical copy ID and, for a decision, a current document token.");
        string unknown = $" {method} outcome unknown for copy {copy}. Check recovery and document state on the original Runtime before retrying.";
        JsonValue reply;
        bool mayHaveSent = true;
        try { reply = session.Request(method, parameters, out mayHaveSent); }
        catch (RemoteException error)
        {
            return Error(reading ? error.Message : error.Message + (mayHaveSent ? unknown : $" {method} was not sent."));
        }
        if (Codec.RecoveryRpcRefusal(reply, method) is { } refusal) return Error(refusal.ToString());
        if (Codec.DocumentResult(reply) is { } result)
        {
            if (result.Find("ok") is { } ok &&
                (reading ? Codec.DecodeRecoveryStatus(ok, copy) is not null
                    : Codec.DecodeRecoveryReceipt(ok, token!.Value, copy!.Value, method == "authoring.restoreRecovery") is not null))
                return Forward(result, false);
            if (result.Find("err") is { } err && Codec.DecodeRecoveryError(err) is not null)
                return Forward(result, true);
        }
        return Error("ProtocolError: malformed recovery reply." + (reading ? "" : unknown));
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

    private CallToolResult PrefabOperation(string method, JsonValue parameters)
    {
        bool read = method == "assets.prefabRevision", update = method == "authoring.updatePrefab";
        string? revision = parameters.Find("revision")?.AsString;
        var token = parameters.Find("expected") is { } expected ? Codec.DecodeDocumentToken(expected) : null;
        var id = parameters.Find("prefab") ?? parameters.Find("guid");
        AssetGuid? guid = id is null ? null : Codec.DecodeAsset(id);
        var node = parameters.Find("node") is { } target ? Codec.DecodeEntity(target) : null;
        var before = parameters.Find("before") is { } a ? Codec.DecodePrefabSubtree(a) : null;
        var after = parameters.Find("after") is { } b ? Codec.DecodePrefabSubtree(b) : null;
        if ((id is not null && guid is null) || (read ? guid is null : token is null || node is null) ||
            (update && guid is null) || ((update || method == "authoring.syncPrefab") &&
                (revision is null || !Codec.ValidPrefabDigest(revision))) ||
            (method == "authoring.replayPrefab" && (before is null || after is null)))
            return Error("InvalidArguments: invalid Prefab identity, revision or replay snapshot.");
        string unknown = $"Prefab operation outcome unknown for {guid?.ToString() ?? node?.ToString()} at revision {revision}. " +
            (update ? "Inspect source.scene.txt and catalog-source.txt." : "Inspect the current document.") +
            " Confirm the previous request settled before retrying.";
        JsonValue reply;
        try { reply = session.Request(method, parameters); }
        catch (RemoteException error) { return Error(read ? error.Message : error.Message + " " + unknown); }
        var result = reply.Find("result");
        var failure = reply.Find("error");
        if (read && Codec.IsUnsupportedStaticGroup(reply))
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        if (result is null && failure?.Find("code") is { IsString: true, AsString: "NoAuthoring" })
            return Forward(failure, true);
        if (result is null && failure?.Find("code") is { IsString: true, AsString: "BadRequest" } &&
            failure.Find("message") is { IsString: true } message && message.AsString == $"unknown method {method}")
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        if (failure is null && result is { IsObject: true } && result.Members.Count == 1)
        {
            if (result.Find("err") is { } err &&
                (read ? Codec.DecodeStaticGroupError(err) is not null : update ? Codec.DecodeSaveAsError(err) is not null :
                    Codec.DecodePrefabEditError(err) is not null))
            {
                if (update && err.AsString == "UnknownOutcome") return Error(unknown);
                return Forward(result, true);
            }
            if (result.Find("ok") is { } value)
            {
                bool valid = read ? Codec.DecodePrefabRevision(value, guid!.Value) is not null : update
                    ? Codec.DecodePrefabUpdate(value) is { } updated && updated.Registration.Prefab == guid &&
                        updated.Registration.DocumentRevision == token!.Value.DocumentRevision && updated.PreviousRevision == revision
                    : Codec.DecodePrefabEdit(value) is { } edit && edit.Document.DocumentId == token!.Value.DocumentId &&
                        edit.Document.DocumentRevision >= token.Value.DocumentRevision &&
                        edit.Document.DocumentRevision - token.Value.DocumentRevision == (edit.Changed ? 1UL : 0UL) &&
                        (before is null || before.Matches(edit.Before)) && (after is null || after.Matches(edit.After));
                if (valid) return Forward(result, false);
            }
        }
        return Error(read ? "ProtocolError: invalid Prefab revision reply." : "ProtocolError: " + unknown);
    }

    private CallToolResult CreatePrefab(JsonValue parameters)
    {
        string? name = parameters.Find("name")?.AsString;
        if (!Codec.ValidSaveAsName(name) || parameters.Find("node") is not { } node ||
            Codec.DecodeEntity(node) is not EntityGuid entity || entity.Value == Guid.Empty ||
            parameters.Find("expected") is not { } expected || Codec.DecodeDocumentToken(expected) is not { } token)
            return Error("InvalidArguments: expected Prefab name, node GUID and document token.");
        string source = Codec.PrefabSource(name!);
        string unknown = $"Prefab registration outcome unknown at {source}. Inspect source.scene.txt and catalog-source.txt. " +
            "Confirm the previous request has settled before retrying.";
        JsonValue reply;
        try { reply = session.Request("authoring.createPrefab", parameters); }
        catch (RemoteException e) { return Error(e.Message + " " + unknown); }
        var error = reply.Find("error");
        var result = reply.Find("result");
        if (result is null && error?.Find("code") is { IsString: true, AsString: "NoAuthoring" }) return Forward(error, true);
        if (result is null && error?.Find("code") is { IsString: true, AsString: "BadRequest" } &&
            error.Find("message") is { IsString: true, AsString: "unknown method authoring.createPrefab" })
            return Forward(JsonValue.NewObject().Set("err", JsonValue.String("Unsupported")), true);
        if (error is null && result is { IsObject: true } && result.Members.Count == 1)
        {
            if (result.Find("err") is { } refusal && Codec.DecodeSaveAsError(refusal) is not null) return Forward(result, true);
            if (result.Find("ok") is { } ok && Codec.DecodePrefabRegistration(ok) is { } receipt &&
                receipt.Source == source && receipt.DocumentRevision == token.DocumentRevision) return Forward(result, false);
        }
        return Error("ProtocolError: " + unknown);
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
