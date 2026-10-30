using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using ModelContextProtocol.Client;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;
using A = Atlantis.Gameplay.Authoring;

namespace Atlantis.Mcp.Tests;

internal static partial class Program
{
    private const string Entity = "11111111-2222-4333-8444-555555555555";
    private const string TypeId = "0x123456789abcdef0";
    private const string FieldId = "0xfedcba9876543210";
    private static readonly string WorldAddress = $$$"""{"entity":"{{{Entity}}}","component":"{{{TypeId}}}","field":"{{{FieldId}}}"}""";
    private static readonly string SceneAddress = $$$"""{"node":"{{{Entity}}}","component":"{{{TypeId}}}","field":"{{{FieldId}}}"}""";
    private static readonly string Camera = """{"worldMatrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"fovYRadians":1,"nearZ":0.1,"farZ":100}""";
    private static int _passed;

    // Independent protocol examples, not derived from the implementation table. Each case tests
    // the name, method, exact parameters, one round trip and both forms of MCP result content.
    private static readonly (string Tool, string Method, string Args, string Result)[] Cases =
    [
        ("world.list_entities", "connection.listEntities", "{}", $$$"""["{{{Entity}}}"]"""),
        ("world.get_property", "connection.getProperty", "{\"address\":" + WorldAddress + "}", """{"ok":{"u64":"18446744073709551615"}}"""),
        ("world.submit_transaction", "connection.submitTransaction", """{"commands":[]}""", """{"first":18446744073709551615,"count":0}"""),
        ("world.drain_failures", "connection.drainFailures", "{}", "[]"),
        ("world.schema", "connection.schema", "{}", """[{"id":"0x123456789abcdef0","name":"world::Light","kind":"Struct","version":1,"fields":[{"id":"0xfedcba9876543210","name":"intensity","kind":"Primitive","primitive":"Float32","type":"0x0000000000000000","flags":3,"offset":8}],"constants":[]}]"""),
        ("authoring.list_nodes", "authoring.listNodes", "{}", $$$"""{"ok":[{"node":"{{{Entity}}}","parent":null}]}"""),
        ("authoring.get_property", "authoring.getProperty", "{\"address\":" + SceneAddress + "}", """{"ok":{"f32":-0}}"""),
        ("authoring.submit_transaction", "authoring.submitTransaction", """{"commands":[]}""", """{"first":1,"count":0}"""),
        ("authoring.drain_failures", "authoring.drainFailures", "{}", "[]"),
        ("authoring.open", "authoring.open", "{}", """{"ok":null}"""),
        ("authoring.document", "authoring.document", "{}", $$$"""{"scene":"{{{Entity}}}","provenance":"Imported","source":"C:\\源\\scene.txt","nodeCount":3}"""),
        ("authoring.apply", "authoring.apply", "{}", """{"ok":null}"""),
        ("authoring.save", "authoring.save", "{}", """{"ok":null}"""),
        ("authoring.active_camera", "authoring.activeCamera", "{}", """{"ok":null}"""),
        ("authoring.schema", "authoring.schema", "{}", """[{"id":"0x123456789abcdef0","name":"scene::Light","kind":"Enum","version":2,"fields":[],"constants":[{"name":"Point","value":-1}]}]"""),
        ("control.status", "control.status", "{}", $$$"""{"paused":false,"frame":18446744073709551615,"scene":"{{{Entity}}}","sceneGeneration":2}"""),
        ("control.pause", "control.pause", "{}", "null"),
        ("control.resume", "control.resume", "{}", "null"),
        ("control.step", "control.step", """{"request":{"frames":2,"image":null,"withOverlay":true}}""", """{"ok":{"frame":2,"applied":true,"data":{"directionalLights":[],"pointLights":[],"view":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"projection":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"drawItemCount":3},"image":null}}"""),
        ("control.raycast", "control.raycast", """{"ray":{"origin":[0,1,2],"direction":[0,0,-1]}}""", $$$"""{"ok":{"entity":"{{{Entity}}}","distance":1e-05}}"""),
        ("control.set_viewport_camera", "control.setViewportCamera", "{\"camera\":" + Camera + "}", "null"),
        ("control.set_viewport_overlay", "control.setViewportOverlay", """{"overlay":{"selection":[],"groups":[]}}""", """{"ok":null}"""),
        ("control.diagnostics", "control.diagnostics", """{"after":18446744073709551615,"max":20}""", """{"entries":[{"sequence":2,"severity":"warning","message":"原样\n<&>"}],"latest":2,"dropped":0}"""),
    ];

    public static async Task<int> Main(string[] args)
    {
        if (args.Contains("--fake-host")) return await FakeHost(false);
        if (args.Contains("--result-host")) return await FakeHost(true);
        if (args.Contains("--m3-host")) return await FakeHost(false, new QueueSession { RejectCommands = true });
        try
        {
            MappingChecks();
            ArgumentChecks();
            RefusalChecks();
            await McpChecks();
            int at = Array.IndexOf(args, "--repo");
            string repo = at >= 0 ? args[at + 1] : Directory.GetCurrentDirectory();
            CompositionChecks(repo);
            EventAndFailureChecks();
            CaptureChecks();
            await M3McpChecks();
            await SessionChecks.Run(Check);
            await SessionChecks.RunHost(Check);
            Console.WriteLine($"MCP M3: {_passed} checks passed, 0 failed");
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"MCP M3: {_passed} checks passed, FAILED: {e}");
            return 1;
        }
    }

    private static void Check(bool passed, string context)
    {
        if (!passed) throw new InvalidOperationException(context);
        ++_passed;
    }

    private static CallToolRequestParams Request(string name, string arguments) => new()
    {
        Name = name,
        Arguments = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(arguments),
    };

    private static void MappingChecks()
    {
        Check(ToolRegistry.Mappings.Select(m => m.Tool.Name).Order().SequenceEqual(Cases.Select(c => c.Tool).Order()),
            "exactly the 23 approved exact-tier names, without subscription management");
        Check(ToolRegistry.Mappings.Select(m => m.Method).Distinct().Count() == Cases.Length, "one distinct wire method per tool");
        foreach (var c in Cases)
        {
            FakeSession session = new() { Response = "{\"id\":7,\"result\":" + c.Result + "}" };
            CallToolResult result = new ToolRegistry(session).Invoke(Request(c.Tool, c.Args));
            Check(result.IsError != true, c.Tool + " succeeds");
            Check(session.Calls.Count == 1 && session.Calls[0].Method == c.Method, c.Tool + " exact method and one round trip");
            Check(Json.Write(session.Calls[0].Parameters) == Json.Write(Json.Parse(c.Args)), c.Tool + " arguments unchanged");
            CheckForward(result, c.Result, c.Tool);
            string description = ToolRegistry.Mappings.Single(m => m.Tool.Name == c.Tool).Tool.Description ?? "";
            Check(description.Contains("authoring.apply") && (c.Tool.StartsWith("authoring.")
                ? description.Contains("document") : description.Contains("disposable")), c.Tool + " editing model stated");
        }
        CheckCall("authoring.document", "{}", "null");
        CheckCall("control.raycast", Cases.Single(c => c.Tool == "control.raycast").Args, """{"ok":null}""");
        CheckCall("control.set_viewport_camera", """{"camera":null}""", "null");
        CheckCall("control.set_viewport_overlay", """{"overlay":null}""", """{"ok":null}""");
        CheckCall("control.step", """{"request":{"frames":0,"image":null}}""", """{"ok":null}""");
    }

    private static void CheckForward(CallToolResult result, string expected, string context)
    {
        Check(result.Content.Count == 1 && result.Content[0] is TextContentBlock text && text.Text == expected,
            context + " verbatim JSON text");
        // The MCP SDK deserializes JSON null to nullable JsonElement's null; arrays and objects
        // must retain their exact wire value, with no result wrapper or SDK object model.
        Check(result.StructuredContent is JsonElement structured
            ? Json.Write(Json.Parse(structured.GetRawText())) == expected : expected == "null",
            context + " verbatim structured JSON (no SDK type serialization or wrapper)");
    }

    private static void CheckCall(string name, string arguments, string response)
    {
        FakeSession session = new() { Response = "{\"result\":" + response + "}" };
        CallToolResult result = new ToolRegistry(session).Invoke(Request(name, arguments));
        Check(result.IsError != true && session.Calls.Count == 1, name + " valid shape reaches wire");
        Check(Json.Write(session.Calls[0].Parameters) == Json.Write(Json.Parse(arguments)), name + " preserves every supplied argument");
        CheckForward(result, response, name);
    }

    private static void ArgumentChecks()
    {
        string[] values = ["""{"u64":"18446744073709551615"}""", """{"f32":"nan"}""", """{"f32":"inf"}""",
            """{"f32":"-inf"}""", """{"vec3":[-0,1e-05,3.4028235e+38]}""", """{"vec4":[1,2,3,4]}""",
            $$$"""{"assetGuid":"{{{Entity}}}"}""", $$$"""{"entityGuid":"{{{Entity}}}"}""", """{"enum":-1}""", """{"absent":true}"""];
        foreach (string value in values)
        {
            CheckCall("world.submit_transaction", "{\"commands\":[{\"kind\":\"SetProperty\",\"address\":"
                + WorldAddress + ",\"value\":" + value + "}]}", """{"first":1,"count":1}""");
            CheckCall("authoring.submit_transaction", "{\"commands\":[{\"kind\":\"SetProperty\",\"address\":"
                + SceneAddress + ",\"value\":" + value + "}]}", """{"first":1,"count":1}""");
        }
        string worldCommands = $$$"""{"commands":[{"kind":"CreateEntity","entity":"{{{Entity}}}"},{"kind":"DestroyEntity","entity":"{{{Entity}}}"},{"kind":"AddComponent","entity":"{{{Entity}}}","component":"{{{TypeId}}}"},{"kind":"RemoveComponent","entity":"{{{Entity}}}","component":"{{{TypeId}}}"}]}""";
        CheckCall("world.submit_transaction", worldCommands, """{"first":1,"count":4}""");
        string authoringCommands = $$$"""{"commands":[{"kind":"CreateNode","node":null,"position":0,"components":["{{{TypeId}}}"]},{"kind":"CreateNode","node":"{{{Entity}}}","position":1,"components":[]},{"kind":"DestroyNode","node":"{{{Entity}}}"},{"kind":"AddComponent","node":"{{{Entity}}}","component":"{{{TypeId}}}"},{"kind":"RemoveComponent","node":"{{{Entity}}}","component":"{{{TypeId}}}"},{"kind":"SetParent","node":"{{{Entity}}}","parent":null},{"kind":"SetParent","node":"{{{Entity}}}","parent":"{{{Entity}}}"},{"kind":"SetActiveCamera","camera":null},{"kind":"SetActiveCamera","camera":"{{{Entity}}}"}]}""";
        CheckCall("authoring.submit_transaction", authoringCommands, """{"first":1,"count":9}""");
        string overlay = $$$"""{"overlay":{"selection":["{{{Entity}}}"],"groups":[{"anchor":null,"segments":[{"a":[0,0,0],"b":[1,1,1],"color":[1,0,0,1],"depth":"Tested","width":1}],"triangles":[{"a":[0,0,0],"b":[1,0,0],"c":[0,1,0],"color":[0,1,0,1],"depth":"OnTop"}],"points":[{"p":[0,0,0],"color":[0,0,1,1],"depth":"OnTop","size":2}]},{"anchor":{"position":[0,1,2],"pixelsPerUnit":0},"segments":[],"triangles":[],"points":[]}]}}""";
        // pixelsPerUnit=0 is a Runtime refusal, not a host-side policy check.
        CheckCall("control.set_viewport_overlay", overlay, """{"ok":null}""");
        CheckCall("control.set_viewport_camera", "{\"camera\":" + Camera.Replace("0.1", "0") + "}", "null");
        CheckCall("control.raycast", """{"ray":{"origin":["nan",0,0],"direction":[0,0,0]}}""", """{"ok":null}""");
        CheckCall("world.get_property", """{"address":{"entity":"not-a-guid","component":"unknown","field":"unknown"}}""", """{"ok":null}""");

        (string Tool, string Args)[] invalid =
        [
            ("world.list_entities", """{"extra":1}"""), ("world.get_property", "{}"),
            ("world.get_property", """{"address":null}"""), ("world.get_property", "{\"address\":" + SceneAddress + "}"),
            ("authoring.get_property", "{\"address\":" + WorldAddress + "}"),
            ("world.submit_transaction", """{"commands":null}"""),
            ("world.submit_transaction", """{"commands":[{"kind":"SetParent","node":"x","parent":null}]}"""),
            ("authoring.submit_transaction", """{"commands":[{"kind":"CreateEntity","entity":"x"}]}"""),
            ("authoring.submit_transaction", """{"commands":[{"kind":"CreateNode","position":0,"components":[]}]}"""),
            ("world.submit_transaction", "{\"commands\":[{\"kind\":\"SetProperty\",\"address\":" + WorldAddress + ",\"value\":{\"f32\":1,\"enum\":1}}]}"),
            ("world.submit_transaction", "{\"commands\":[{\"kind\":\"SetProperty\",\"address\":" + WorldAddress + ",\"value\":{\"absent\":false}}]}"),
            ("control.step", """{"request":{"frames":1.5,"image":null}}"""),
            ("control.step", """{"request":{"frames":1}}"""),
            ("control.step", """{"request":{"frames":1,"image":null,"withOverlay":"yes"}}"""),
            ("control.raycast", """{"ray":{"origin":[0,0],"direction":[0,0,1]}}"""),
            ("control.set_viewport_camera", """{"camera":{"worldMatrix":[],"fovYRadians":1,"nearZ":0,"farZ":1}}"""),
            ("control.set_viewport_overlay", """{"overlay":{"selection":[],"groups":[{"anchor":null,"segments":[],"triangles":[]}]}}"""),
            ("control.diagnostics", """{"after":"1","max":1}"""), ("control.pause", """{"apply":true}"""),
        ];
        foreach (var c in invalid)
        {
            FakeSession session = new();
            CallToolResult result = new ToolRegistry(session).Invoke(Request(c.Tool, c.Args));
            Check(result.IsError == true && session.Calls.Count == 0, c.Tool + " rejects malformed argument shape before wire: " + c.Args);
        }
        FakeSession untouched = new();
        var registry = new ToolRegistry(untouched);
        Check(registry.Invoke(Request("authoring.subscribe", "{}")).IsError == true && untouched.Calls.Count == 0, "unknown tool never forwards");
        Check(registry.Invoke(null).IsError == true && untouched.Calls.Count == 0, "absent request never forwards");
        using CancellationTokenSource cancelled = new();
        cancelled.Cancel();
        try { registry.Invoke(Request("control.status", "{}"), cancelled.Token); throw new Exception("cancellation ignored"); }
        catch (OperationCanceledException) { Check(untouched.Calls.Count == 0, "cancelled request never forwards"); }
    }

    private static void RefusalChecks()
    {
        Refusals<AccessError>("world.get_property");
        Refusals<A.AuthoringError>("authoring.get_property");
        foreach (string reason in Enum.GetNames<A.AuthoringError>())
        {
            string failuresJson = "[{\"ticket\":18446744073709551615,\"error\":\"" + reason
                + "\"},{\"ticket\":2,\"error\":\"UnknownNode\"}]";
            FakeSession session = new() { Response = "{\"result\":" + failuresJson + "}" };
            CallToolResult result = new ToolRegistry(session).Invoke(Request("authoring.drain_failures", "{}"));
            Check(result.IsError == true && session.Calls.Count == 1
                && session.Calls[0].Method == "authoring.drainFailures"
                && Json.Write(session.Calls[0].Parameters) == "{}", "authoring drain " + reason + " is one exact request");
            CheckForward(result, failuresJson, "authoring drain preserves all tickets, order and " + reason);
        }
        Refusals<A.OpenError>("authoring.open");
        Refusals<A.ApplyError>("authoring.apply");
        Refusals<A.SaveError>("authoring.save");
        Refusals<ControlError>("control.step");
        Refusals<ControlError>("control.raycast");
        FakeSession overlayRefusal = new() { Response = "{\"result\":{\"err\":\"InvalidRequest\"}}" };
        CallToolResult overlay = new ToolRegistry(overlayRefusal).Invoke(Request("control.set_viewport_overlay", "{\"overlay\":null}"));
        Check(overlay.IsError == true && overlayRefusal.Calls.Count == 1, "overlay refusal is a tool error without retry");
        CheckForward(overlay, "{\"err\":\"InvalidRequest\"}", "overlay refusal");
        foreach (string reason in new[] { "BadRequest", "NoControl", "NoAuthoring", "HelloRequired", "WrongProtocol", "BadToken" })
        {
            string error = "{\"code\":\"" + reason + "\",\"message\":\"原样 reason: <&>\"}";
            FakeSession session = new() { Response = "{\"id\":1,\"error\":" + error + "}" };
            CallToolResult result = new ToolRegistry(session).Invoke(Request("control.status", "{}"));
            Check(result.IsError == true && session.Calls.Count == 1, reason + " is a tool error without retry");
            CheckForward(result, error, reason);
        }
        string failures = """[{"ticket":18446744073709551615,"error":"UnknownEntity"},{"ticket":2,"error":"NonFiniteValue"}]""";
        FakeSession deferred = new() { Response = "{\"result\":" + failures + "}" };
        CallToolResult drained = new ToolRegistry(deferred).Invoke(Request("world.drain_failures", "{}"));
        Check(drained.IsError == true && deferred.Calls.Count == 1, "deferred command refusals are tool errors");
        CheckForward(drained, failures, "deferred failures retain tickets and reasons");
        foreach (RemoteError error in Enum.GetValues<RemoteError>())
        {
            RemoteException failure = new(error, "原样 transport detail");
            FakeSession session = new() { Failure = failure };
            CallToolResult result = new ToolRegistry(session).Invoke(Request("control.status", "{}"));
            Check(result.IsError == true && result.Content[0] is TextContentBlock text && text.Text == failure.Message
                && session.Calls.Count == 1, error + " transport failure is not hidden or retried");
        }
        FakeSession malformed = new() { Response = "{\"id\":1}" };
        Check(new ToolRegistry(malformed).Invoke(Request("control.status", "{}")).IsError == true, "missing response payload is an error");
    }

    private static void Refusals<T>(string tool) where T : struct, Enum
    {
        foreach (string reason in Enum.GetNames<T>())
        {
            string resultJson = "{\"err\":\"" + reason + "\"}";
            FakeSession session = new() { Response = "{\"result\":" + resultJson + "}" };
            CallToolResult result = new ToolRegistry(session).Invoke(Request(tool, Cases.Single(c => c.Tool == tool).Args));
            Check(result.IsError == true && session.Calls.Count == 1, tool + " " + reason + " no retry");
            CheckForward(result, resultJson, tool + " " + reason);
        }
    }

    private static async Task<int> FakeHost(bool results, IAtlantisSession? seam = null)
    {
        var registry = new ToolRegistry(seam ?? new FakeSession { Echo = !results, FixtureResults = results });
        registry.InitializeSubscriptions();
        McpServerOptions options = new()
        {
            ServerInfo = new Implementation { Name = "M2-fake-session", Version = "1" },
            Capabilities = new ServerCapabilities { Tools = new ToolsCapability() },
        };
        registry.Wire(options);
        using StderrLoggerFactory logs = new();
        await using McpServer server = McpServer.Create(new StdioServerTransport("M2-fake-session", logs), options, logs);
        await server.RunAsync();
        return 0;
    }

    private static async Task McpChecks()
    {
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(30));
        await using McpClient client = await McpClient.CreateAsync(new StdioClientTransport(new StdioClientTransportOptions
        {
            Name = "M2 fake session", Command = "dotnet",
            Arguments = [typeof(Program).Assembly.Location, "--fake-host"],
        }), cancellationToken: deadline.Token);
        var tools = await client.ListToolsAsync(cancellationToken: deadline.Token);
        Check(tools.Select(t => t.Name).Order().SequenceEqual(ExpectedNames.Order()), "MCP tools/list exposes exactly all 32 approved M3 tools");
        foreach (var c in Cases)
        {
            var arguments = JsonSerializer.Deserialize<Dictionary<string, object?>>(c.Args)!;
            CallToolResult result = await client.CallToolAsync(c.Tool, arguments, cancellationToken: deadline.Token);
            string expected = "{\"method\":\"" + c.Method + "\",\"params\":" + Json.Write(Json.Parse(c.Args)) + "}";
            Check(result.IsError != true, "MCP tools/call " + c.Tool + " succeeds");
            CheckForward(result, expected, "MCP dispatch " + c.Tool);
        }
        CallToolResult bad = await client.CallToolAsync("control.step", new Dictionary<string, object?>(), cancellationToken: deadline.Token);
        Check(bad.IsError == true, "MCP dispatch returns argument errors as tool errors");

        await using McpClient resultClient = await McpClient.CreateAsync(new StdioClientTransport(new StdioClientTransportOptions
        {
            Name = "M2 wire results", Command = "dotnet",
            Arguments = [typeof(Program).Assembly.Location, "--result-host"],
        }), cancellationToken: deadline.Token);
        foreach (var c in Cases)
        {
            CallToolResult result = await resultClient.CallToolAsync(c.Tool,
                JsonSerializer.Deserialize<Dictionary<string, object?>>(c.Args)!, cancellationToken: deadline.Token);
            Check(result.IsError != true, "MCP result " + c.Tool + " succeeds");
            CheckForward(result, c.Result, "MCP result " + c.Tool);
        }
    }

    private sealed class FakeSession : IAtlantisSession
    {
        public string Response { get; init; } = "{\"result\":null}";
        public RemoteException? Failure { get; init; }
        public bool Echo { get; init; }
        public bool FixtureResults { get; init; }
        public List<(string Method, JsonValue Parameters)> Calls { get; } = new();

        public JsonValue Request(string method, JsonValue parameters)
        {
            Calls.Add((method, parameters));
            if (Failure is not null) throw Failure;
            if (method == "connection.subscribe") return Json.Parse("{\"result\":41}");
            if (method == "authoring.subscribe") return Json.Parse("{\"result\":73}");
            if (FixtureResults) return Json.Parse("{\"result\":" + Cases.Single(c => c.Method == method).Result + "}");
            return Echo ? JsonValue.NewObject().Set("result", JsonValue.NewObject()
                .Set("method", JsonValue.String(method)).Set("params", parameters)) : Json.Parse(Response);
        }
    }
}
