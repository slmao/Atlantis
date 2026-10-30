using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;

namespace Atlantis.Mcp;

internal sealed record ToolMapping(Tool Tool, string Method);

/// <summary>
/// Spec 0070 R2's closed exact tier. Immutable declarations; concurrent calls are serialized by
/// IAtlantisSession. Results preserve the wire value, including in-band errors and deferred failures.
/// </summary>
internal sealed class ToolRegistry(IAtlantisSession session)
{
    private const string WorldModel = " Runtime World writes are disposable runtime-side tweaks; "
        + "persistent edits belong to the authoring document and land on explicit authoring.apply.";
    private const string AuthoringModel = " Edits change the authoring document; only explicit "
        + "authoring.apply makes it the running scene. After submission, explicitly call authoring.drain_failures "
        + "and correlate tickets before applying: a ticket alone is not edit success. Apply may be held while paused; Applied events follow the reload.";
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
        Declare("authoring.apply", "authoring.apply", "Explicitly hand the document to the Runtime for reload.", AuthoringModel),
        Declare("authoring.save", "authoring.save", "Save a hand-authored source; imported sources are refused.", AuthoringModel),
        Declare("authoring.active_camera", "authoring.activeCamera", "Read the document's active camera node or null.", AuthoringModel),
        Declare("authoring.schema", "authoring.schema", "Read scene schema types, fields, IDs and constants in the wire's section 6.7 shape.", AuthoringModel),
        Declare("control.status", "control.status", "Read pause state, frame, scene and sceneGeneration.", ControlModel),
        Declare("control.pause", "control.pause", "Hold Runtime commands and pending scene reloads.", ControlModel),
        Declare("control.resume", "control.resume", "Resume Runtime command application and pending scene reloads.", ControlModel),
        Declare("control.step", "control.step", "Run the requested frames and return the frame report and optional capture path.", ControlModel,
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
                    Tools = Mappings.Select(m => m.Tool).ToImmutableArray(),
                };
                return ValueTask.FromResult(result);
            },
            CallToolHandler = (request, token) => ValueTask.FromResult(Invoke(request.Params, token)),
        };
    }

    internal CallToolResult Invoke(CallToolRequestParams? request, CancellationToken token = default)
    {
        token.ThrowIfCancellationRequested();
        ToolMapping? mapping = Mappings.FirstOrDefault(m => m.Tool.Name == request?.Name);
        if (mapping is null) return Error("UnknownTool: no tool is named '" + request?.Name + "'");
        JsonElement parameters = JsonSerializer.SerializeToElement(request?.Arguments ?? new Dictionary<string, JsonElement>());
        if (!ToolSchemas.Matches(mapping.Tool.InputSchema, parameters))
            return Error("InvalidArguments: arguments must match the tool's input schema");
        try
        {
            JsonValue response = session.Request(mapping.Method, Json.Parse(parameters.GetRawText()));
            if (response.Find("error") is JsonValue error) return Forward(error, true);
            if (response.Find("result") is not JsonValue result) return Error("ProtocolError: response has no result or error");
            bool refused = result.Find("err") is not null
                || (mapping.Method is "connection.drainFailures" or "authoring.drainFailures"
                    && result.IsArray && result.Items.Count > 0);
            return Forward(result, refused);
        }
        catch (RemoteException e)
        {
            return Error(e.Message);
        }
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
