using System;
using System.Collections.Immutable;
using System.Text.Json;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Mcp;

internal sealed partial class ToolRegistry
{
    private static ImmutableArray<ToolMapping> ConvenienceMappings { get; } =
    [
        Composed("create_node", "Create a node with a minted GUID, insertion position and component IDs (Transform is implicit). Read NodeCreated via authoring.drain_events for its GUID.",
            ToolSchemas.Parameters(("position", ToolSchemas.Integer), ("components", ToolSchemas.Array(ToolSchemas.String))),
            p => OneCommand("CreateNode").Set("node", JsonValue.String(Guid.NewGuid().ToString("D")))
                .Set("position", p.Find("position")!).Set("components", p.Find("components")!)),
        Composed("destroy_node", "Destroy a document node by GUID.",
            ToolSchemas.Parameters(("node", ToolSchemas.String)), p => WithNode("DestroyNode", p)),
        new(Declare("set_properties", "authoring.submitTransaction",
            "Set document properties in order, addressed by node/component/field IDs from authoring.schema; one transaction, including an empty list.",
            AuthoringModel, ToolSchemas.Parameters(("properties", ToolSchemas.Array(ToolSchemas.ParametersNode(
                ("address", ToolSchemas.SceneAddress), ("value", ToolSchemas.Value)))))).Tool,
            "authoring.submitTransaction", p =>
            {
                JsonValue commands = JsonValue.NewArray();
                foreach (JsonValue property in p.Find("properties")!.Items)
                    commands.Push(OneCommand("SetProperty").Set("address", property.Find("address")!)
                        .Set("value", property.Find("value")!));
                return JsonValue.NewObject().Set("commands", commands);
            }),
        Composed("add_component", "Add a component by schema type ID to a document node.",
            ToolSchemas.Parameters(("node", ToolSchemas.String), ("component", ToolSchemas.String)),
            p => WithNode("AddComponent", p).Set("component", p.Find("component")!)),
        Composed("remove_component", "Remove a component by schema type ID from a document node.",
            ToolSchemas.Parameters(("node", ToolSchemas.String), ("component", ToolSchemas.String)),
            p => WithNode("RemoveComponent", p).Set("component", p.Find("component")!)),
        Composed("set_parent", "Set a document node's parent by GUID; null makes it a root.",
            ToolSchemas.Parameters(("node", ToolSchemas.String), ("parent", ToolSchemas.Nullable(ToolSchemas.String))),
            p => WithNode("SetParent", p).Set("parent", p.Find("parent")!)),
        Composed("set_active_camera", "Set the document's active camera node by GUID, or null.",
            ToolSchemas.Parameters(("camera", ToolSchemas.Nullable(ToolSchemas.String))),
            p => OneCommand("SetActiveCamera").Set("camera", p.Find("camera")!)),
    ];

    private static ToolMapping Composed(string name, string description, JsonElement schema, Func<JsonValue, JsonValue> command) =>
        new(Declare(name, "authoring.submitTransaction", description + " Submits exactly one authoring transaction; returns its wire ticket.",
            AuthoringModel, schema).Tool, "authoring.submitTransaction",
            p => JsonValue.NewObject().Set("commands", JsonValue.NewArray().Push(command(p))));

    private static JsonValue OneCommand(string kind) => JsonValue.NewObject().Set("kind", JsonValue.String(kind));
    private static JsonValue WithNode(string kind, JsonValue parameters) => OneCommand(kind).Set("node", parameters.Find("node")!);
}
