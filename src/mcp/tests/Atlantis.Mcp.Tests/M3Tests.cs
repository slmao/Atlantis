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

namespace Atlantis.Mcp.Tests;

internal static partial class Program
{
    private static IEnumerable<string> ExpectedNames => Cases.Select(c => c.Tool).Concat(new[]
    {
        "create_node", "destroy_node", "set_properties", "add_component", "remove_component",
        "set_parent", "set_active_camera", "world.drain_events", "authoring.drain_events",
    });

    private static (string Tool, string Args, string Commands)[] CompositionCases =>
    [
        ("create_node", "{\"position\":2,\"components\":[\"" + TypeId + "\"]}",
            "[{\"kind\":\"CreateNode\",\"node\":\"MINTED\",\"position\":2,\"components\":[\"" + TypeId + "\"]}]"),
        ("destroy_node", "{\"node\":\"" + Entity + "\"}", "[{\"kind\":\"DestroyNode\",\"node\":\"" + Entity + "\"}]"),
        ("set_properties", "{\"properties\":[{\"address\":" + SceneAddress + ",\"value\":{\"f32\":4}},"
            + "{\"address\":" + SceneAddress + ",\"value\":{\"f32\":-0}}]}",
            "[{\"kind\":\"SetProperty\",\"address\":" + SceneAddress + ",\"value\":{\"f32\":4}},"
            + "{\"kind\":\"SetProperty\",\"address\":" + SceneAddress + ",\"value\":{\"f32\":-0}}]"),
        ("add_component", "{\"node\":\"" + Entity + "\",\"component\":\"" + TypeId + "\"}",
            "[{\"kind\":\"AddComponent\",\"node\":\"" + Entity + "\",\"component\":\"" + TypeId + "\"}]"),
        ("remove_component", "{\"node\":\"" + Entity + "\",\"component\":\"" + TypeId + "\"}",
            "[{\"kind\":\"RemoveComponent\",\"node\":\"" + Entity + "\",\"component\":\"" + TypeId + "\"}]"),
        ("set_parent", "{\"node\":\"" + Entity + "\",\"parent\":null}",
            "[{\"kind\":\"SetParent\",\"node\":\"" + Entity + "\",\"parent\":null}]"),
        ("set_active_camera", "{\"camera\":null}", "[{\"kind\":\"SetActiveCamera\",\"camera\":null}]"),
    ];

    private static void CompositionChecks(string repo)
    {
        Check(ToolRegistry.AllMappings.Length == 32 && ToolRegistry.AllMappings.Select(m => m.Tool.Name).Distinct().Count() == 32,
            "32 unique approved tools at the M3 checkpoint");
        foreach (var c in CompositionCases)
        {
            FakeSession session = new() { Response = "{\"result\":{\"first\":100,\"count\":1}}" };
            CallToolResult result = new ToolRegistry(session).Invoke(Request(c.Tool, c.Args));
            Check(result.IsError != true && session.Calls.Count == 1, c.Tool + " submits once, without pre-read, drain or apply");
            Check(session.Calls[0].Method == "authoring.submitTransaction", c.Tool + " uses the existing transaction call");
            CheckComposition(c.Tool, session.Calls[0].Parameters, c.Commands);
            CheckForward(result, "{\"first\":100,\"count\":1}", c.Tool + " forwards its wire ticket");
            string description = ToolRegistry.AllMappings.Single(m => m.Tool.Name == c.Tool).Tool.Description!;
            Check(description.Contains("authoring.drain_failures") && description.Contains("ticket")
                && description.Contains("authoring.apply"), c.Tool + " teaches explicit settlement and apply");
        }
        // Use the C++-generated wire fixtures that the SDK's codec suite uses (Plan 0070 P3).
        int fixtureCount = 0;
        foreach (string line in File.ReadLines(Path.Combine(repo, "tests/csharp/conformance/codec_vectors.jsonl")))
        {
            JsonValue fixture = Json.Parse(line);
            string type = fixture.Find("type")!.AsString;
            if (type is not ("AuthoringCommand" or "PropertyValue")) continue;
            JsonValue wire = fixture.Find("wire")!;
            JsonValue command = type == "PropertyValue"
                ? JsonValue.NewObject().Set("kind", JsonValue.String("SetProperty"))
                    .Set("address", Json.Parse(SceneAddress)).Set("value", wire)
                : Json.Parse(Json.Write(wire));
            string kind = command.Find("kind")!.AsString;
            string tool = kind switch
            {
                "CreateNode" => "create_node", "DestroyNode" => "destroy_node", "SetProperty" => "set_properties",
                "AddComponent" => "add_component", "RemoveComponent" => "remove_component",
                "SetParent" => "set_parent", "SetActiveCamera" => "set_active_camera",
                _ => throw new InvalidOperationException("new command fixture requires review: " + kind),
            };
            JsonValue arguments = JsonValue.NewObject();
            if (kind == "SetProperty")
                arguments.Set("properties", JsonValue.NewArray().Push(JsonValue.NewObject()
                    .Set("address", command.Find("address")!).Set("value", command.Find("value")!)));
            else
                foreach (var member in command.Members)
                    if (member.Key != "kind" && !(kind == "CreateNode" && member.Key == "node"))
                        arguments.Set(member.Key, member.Value);
            FakeSession session = new() { Response = "{\"result\":{\"first\":100,\"count\":1}}" };
            CallToolResult result = new ToolRegistry(session).Invoke(Request(tool, Json.Write(arguments)));
            Check(result.IsError != true && session.Calls.Count == 1, fixture.Find("case")!.AsString + " composes once");
            JsonValue submitted = session.Calls[0].Parameters.Find("commands")!.Items.Single();
            if (kind == "CreateNode") command.Set("node", submitted.Find("node")!);
            Check(Json.Write(submitted) == Json.Write(command), "composition matches C++/SDK fixture " + fixture.Find("case")!.AsString);
            Check(Codec.DecodeAuthoringCommand(submitted) is not null, "SDK accepts the composed command fixture");
            ++fixtureCount;
        }
        Check(fixtureCount > 20, "shared SDK fixture coverage was exercised");
        QueueSession edgeSession = new();
        var edgeRegistry = new ToolRegistry(edgeSession);
        CallToolResult empty = edgeRegistry.Invoke(Request("set_properties", "{\"properties\":[]}"));
        CheckForward(empty, "{\"first\":100,\"count\":0}", "empty set_properties returns the wire's zero-count ticket");
        Check(Json.Write(edgeSession.Calls[0].Parameters) == "{\"commands\":[]}", "empty property list still submits one transaction");
        edgeRegistry.Invoke(Request("set_parent", "{\"node\":\"not-a-guid\",\"parent\":\"not-a-guid\"}"));
        Check(Json.Write(edgeSession.Calls[1].Parameters)
            == "{\"commands\":[{\"kind\":\"SetParent\",\"node\":\"not-a-guid\",\"parent\":\"not-a-guid\"}]}",
            "GUID validity remains a wire decision for convenience tools");

        foreach (var c in new[] { ("create_node", "{\"position\":0,\"components\":[],\"node\":null}"),
            ("destroy_node", "{}"), ("set_properties", "{\"properties\":[{\"address\":null,\"value\":4}]}"),
            ("add_component", "{\"node\":null,\"component\":1}"), ("remove_component", "{\"node\":\"x\"}"),
            ("set_parent", "{\"node\":\"x\"}"), ("set_active_camera", "{\"camera\":4}") })
        {
            FakeSession session = new();
            Check(new ToolRegistry(session).Invoke(Request(c.Item1, c.Item2)).IsError == true && session.Calls.Count == 0,
                c.Item1 + " shape errors do not submit");
        }
        FakeSession minted = new();
        var mintingRegistry = new ToolRegistry(minted);
        for (int i = 0; i < 2; ++i) mintingRegistry.Invoke(Request("create_node", CompositionCases[0].Args));
        Check(minted.Calls[0].Parameters.Find("commands")!.Items[0].Find("node")!.AsString
            != minted.Calls[1].Parameters.Find("commands")!.Items[0].Find("node")!.AsString, "each create mints a distinct GUID");
    }

    private static void CheckComposition(string tool, JsonValue parameters, string expectedCommands)
    {
        if (tool == "create_node")
        {
            string guid = parameters.Find("commands")!.Items.Single().Find("node")!.AsString;
            Check(EntityGuid.TryParse(guid, out EntityGuid node) && !node.IsNil, "create_node mints a canonical non-nil GUID");
            expectedCommands = expectedCommands.Replace("MINTED", guid, StringComparison.Ordinal);
        }
        Check(Json.Write(parameters) == "{\"commands\":" + expectedCommands + "}", tool + " exact ordered transaction");
    }

    private static void EventAndFailureChecks()
    {
        QueueSession session = new();
        var registry = new ToolRegistry(session);
        registry.InitializeSubscriptions();
        Check(session.Calls.Count == 2 && session.Calls[0].Method == "connection.subscribe"
            && session.Calls[1].Method == "authoring.subscribe", "one subscription per family at connect");
        Check(Json.Write(session.Calls[0].Parameters) == "{\"filter\":{\"kinds\":[\"EntityCreated\",\"EntityDestroyed\",\"ComponentAdded\",\"ComponentRemoved\",\"PropertyChanged\"],\"entity\":null,\"component\":null}}",
            "World filter has no restrictions");
        Check(Json.Write(session.Calls[1].Parameters) == "{\"filter\":{\"kinds\":[\"PropertyChanged\",\"NodeCreated\",\"NodeDestroyed\",\"ComponentAdded\",\"ComponentRemoved\",\"ParentChanged\",\"ActiveCameraChanged\",\"DocumentReopened\",\"Applied\"],\"node\":null,\"component\":null}}",
            "authoring filter includes every kind, with no node/component restriction");
        try { registry.InitializeSubscriptions(); throw new Exception("duplicate initialization accepted"); }
        catch (InvalidOperationException) { Check(session.Calls.Count == 2, "initialization cannot create duplicate subscriptions"); }
        foreach (var c in CompositionCases)
        {
            session.NextError = "NoDocument";
            int before = session.Calls.Count;
            CallToolResult submitted = registry.Invoke(Request(c.Tool, c.Args));
            Check(submitted.IsError != true && session.Calls.Count == before + 1 && session.Failures.Count == 1,
                c.Tool + " returns a ticket, leaving its deferred refusal for an explicit drain");
            JsonValue ticket = Json.Parse(((TextContentBlock)submitted.Content[0]).Text);
            string failures = "[{\"ticket\":" + ticket.Find("first")!.NumberText + ",\"error\":\"NoDocument\"}]";
            CallToolResult drain = registry.Invoke(Request("authoring.drain_failures", "{}"));
            Check(drain.IsError == true, "explicit drain reports the refusal as a tool error");
            CheckForward(drain, failures, "explicit settlement retains transaction identity");
            CheckForward(registry.Invoke(Request("authoring.drain_failures", "{}")), "[]", "second failure drain clears");
        }
        foreach (string reason in new[] { "UnknownNode", "ComponentMissing" })
        {
            session.NextError = reason;
            registry.Invoke(Request("destroy_node", CompositionCases[1].Args));
        }
        string accumulated = Json.Write(JsonValue.NewArray().Push(session.Failures[0]).Push(session.Failures[1]));
        session.WorldEvents.Add(Json.Parse("{\"kind\":\"EntityCreated\",\"entity\":\"" + Entity + "\"}"));
        session.WorldEvents.Add(Json.Parse("{\"kind\":\"EntityDestroyed\",\"entity\":\"" + Entity + "\"}"));
        session.AuthoringEvents.Add(Json.Parse("{\"kind\":\"DocumentReopened\",\"scene\":\"" + Entity + "\"}"));
        session.AuthoringEvents.Add(Json.Parse("{\"kind\":\"Applied\",\"generation\":18446744073709551615}"));
        string authoringEvents = Json.Write(JsonValue.NewObject().Set("ok", ToArray(session.AuthoringEvents)));
        CallToolResult events = registry.Invoke(Request("authoring.drain_events", "{}"));
        CheckForward(events, authoringEvents, "authoring events accumulate in wire order, including Applied");
        Check(session.Failures.Count == 2 && session.WorldEvents.Count == 2, "authoring event drain preserves failures and World events");
        CheckForward(registry.Invoke(Request("authoring.drain_events", "{}")), "{\"ok\":[]}", "second authoring event drain clears");
        CheckForward(registry.Invoke(Request("authoring.drain_failures", "{}")), accumulated, "failure drain returns all pending transactions in order");
        Check(session.WorldEvents.Count == 2, "failure drain does not consume World events");
        CheckForward(registry.Invoke(Request("world.drain_events", "{}")),
            "{\"ok\":[{\"kind\":\"EntityCreated\",\"entity\":\"" + Entity + "\"},{\"kind\":\"EntityDestroyed\",\"entity\":\"" + Entity + "\"}]}", "World events accumulate and clear");
        CheckForward(registry.Invoke(Request("world.drain_events", "{}")), "{\"ok\":[]}", "second World drain clears");
        Check(session.Calls.Count(c => c.Method.EndsWith(".subscribe", StringComparison.Ordinal)) == 2, "drains never subscribe or retry");
        session.EventError = "UnknownSubscription";
        foreach (string tool in new[] { "world.drain_events", "authoring.drain_events" })
        {
            CallToolResult result = registry.Invoke(Request(tool, "{}"));
            Check(result.IsError == true, tool + " maps subscription refusal to a tool error");
            CheckForward(result, "{\"err\":\"UnknownSubscription\"}", tool);
        }
        QueueSession unavailable = new() { NoAuthoring = true };
        var unavailableRegistry = new ToolRegistry(unavailable);
        unavailableRegistry.InitializeSubscriptions();
        CallToolResult refused = unavailableRegistry.Invoke(Request("authoring.drain_events", "{}"));
        Check(refused.IsError == true && unavailable.Calls.Count == 2, "refused startup subscription is reported without retry");
        CheckForward(refused, "{\"code\":\"NoAuthoring\",\"message\":\"no document service\"}", "subscription refusal");
        Check(unavailableRegistry.Invoke(Request("control.status", "{}")).IsError != true, "unavailable authoring subscription does not hide control tools");
    }

    private static readonly byte[] Png = Convert.FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=");

    private static void CaptureChecks()
    {
        using CaptureDirectory files = new();
        string path = Path.Combine(files.Path, "帧.png");
        File.WriteAllBytes(path, Png);
        JsonValue report = StepReport(path);
        FakeSession session = new() { Response = "{\"result\":" + Json.Write(report) + "}" };
        var registry = new ToolRegistry(session);
        string args = "{\"request\":{\"frames\":1,\"image\":\"requested-path.png\",\"withOverlay\":true}}";
        CallToolResult captured = registry.Invoke(Request("control.step", args));
        Check(captured.IsError != true && session.Calls.Count == 1, "capture performs one wire step without retries");
        CheckImage(captured, report, "capture reads the report path, not the request path");
        File.WriteAllBytes(path, new byte[] { 1, 2, 3 });
        Check(((ImageContentBlock)captured.Content[1]).DecodedData.Span.SequenceEqual(Png), "capture bytes are owned by the result after file reuse");
        File.WriteAllBytes(path, Array.Empty<byte>());
        CallToolResult empty = registry.Invoke(Request("control.step", args));
        Check(empty.IsError == true && empty.Content.All(c => c is not ImageContentBlock), "empty image is a tool error with no image block");
        File.Delete(path);
        CallToolResult missing = registry.Invoke(Request("control.step", args));
        Check(missing.IsError == true && missing.StructuredContent is JsonElement json
            && Json.Write(Json.Parse(json.GetRawText())) == Json.Write(report), "unreadable PNG retains the frame report and path as a tool error");
        FakeSession refusal = new() { Response = "{\"result\":{\"err\":\"CaptureFailed\"}}" };
        CallToolResult failed = new ToolRegistry(refusal).Invoke(Request("control.step", args));
        Check(failed.IsError == true, "wire capture refusal remains a tool error");
        CheckForward(failed, "{\"err\":\"CaptureFailed\"}", "capture refusal is not replaced by a file error");
        FakeSession noPath = new() { Response = "{\"result\":{\"ok\":{\"image\":null}}}" };
        Check(new ToolRegistry(noPath).Invoke(Request("control.step", args)).IsError == true, "requested capture missing its report path is an error");
        Check(new ToolRegistry(noPath).Invoke(Request("control.step", "{\"request\":{\"frames\":1,\"image\":null}}")).Content.Count == 1,
            "step without image does not touch files or add image content");
    }

    private static void CheckImage(CallToolResult result, JsonValue report, string context)
    {
        Check(result.Content.Count == 2 && result.Content[0] is TextContentBlock text && text.Text == Json.Write(report), context + " retains frame JSON text");
        Check(result.StructuredContent is JsonElement structured && Json.Write(Json.Parse(structured.GetRawText())) == Json.Write(report), context + " retains structured frame report and PNG path");
        Check(result.Content[1] is ImageContentBlock image && image.MimeType == "image/png" && image.DecodedData.Span.SequenceEqual(Png), context + " returns the PNG bytes in-context");
    }

    private static async Task M3McpChecks()
    {
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(30));
        await using McpClient client = await McpClient.CreateAsync(new StdioClientTransport(new StdioClientTransportOptions
        {
            Name = "M3 queue fixture", Command = "dotnet", Arguments = [typeof(Program).Assembly.Location, "--m3-host"],
        }), cancellationToken: deadline.Token);
        foreach (var c in CompositionCases)
        {
            CallToolResult result = await client.CallToolAsync(c.Tool,
                JsonSerializer.Deserialize<Dictionary<string, object?>>(c.Args)!, cancellationToken: deadline.Token);
            Check(result.IsError != true, "MCP " + c.Tool + " returns the submission ticket");
            CallToolResult refusal = await client.CallToolAsync("authoring.drain_failures", cancellationToken: deadline.Token);
            Check(refusal.IsError == true && ((TextContentBlock)refusal.Content[0]).Text.Contains("NoDocument"), "MCP explicit failure drain reports deferred refusal");
            CallToolResult cleared = await client.CallToolAsync("authoring.drain_failures", cancellationToken: deadline.Token);
            Check(cleared.IsError != true, "MCP second failure drain succeeds");
            CheckForward(cleared, "[]", "MCP failure queue clears");
        }
        foreach (string tool in new[] { "world.drain_events", "authoring.drain_events" })
        {
            CallToolResult result = await client.CallToolAsync(tool, cancellationToken: deadline.Token);
            Check(result.IsError != true, "MCP event dispatch " + tool);
            CheckForward(result, "{\"ok\":[]}", "MCP host-held subscription " + tool);
        }
        using CaptureDirectory files = new();
        string path = Path.Combine(files.Path, "stdio.png");
        File.WriteAllBytes(path, Png);
        CallToolResult captured = await client.CallToolAsync("control.step", new Dictionary<string, object?>
        {
            ["request"] = new { frames = 1, image = path, withOverlay = false },
        }, cancellationToken: deadline.Token);
        Check(captured.IsError != true, "MCP capture succeeds");
        CheckImage(captured, StepReport(path), "MCP capture round trip");
    }

    private static JsonValue StepReport(string? path)
    {
        JsonValue report = Json.Parse(Cases.Single(c => c.Tool == "control.step").Result);
        report.Find("ok")!.Set("image", path is null ? JsonValue.Null() : JsonValue.NewObject()
            .Set("path", JsonValue.String(path)).Set("width", JsonValue.Number(1L)).Set("height", JsonValue.Number(1L)));
        return report;
    }

    private static JsonValue ToArray(IEnumerable<JsonValue> values)
    {
        JsonValue result = JsonValue.NewArray();
        foreach (JsonValue value in values) result.Push(value);
        return result;
    }

    // Sequential fixture, deliberately implementing the wire's per-connection queues rather than
    // consuming them in a convenience tool. The assertions above observe each request separately.
    private sealed class QueueSession : IAtlantisSession
    {
        public List<(string Method, JsonValue Parameters)> Calls { get; } = new();
        public List<JsonValue> Failures { get; } = new();
        public List<JsonValue> WorldEvents { get; } = new();
        public List<JsonValue> AuthoringEvents { get; } = new();
        public string? NextError { get; set; }
        public string? EventError { get; set; }
        public bool NoAuthoring { get; init; }
        public bool RejectCommands { get; init; }
        private ulong _ticket = 100;

        public JsonValue Request(string method, JsonValue parameters)
        {
            Calls.Add((method, parameters));
            JsonValue result;
            switch (method)
            {
                case "connection.subscribe": result = JsonValue.Number(41L); break;
                case "authoring.subscribe":
                    if (NoAuthoring) return Json.Parse("{\"error\":{\"code\":\"NoAuthoring\",\"message\":\"no document service\"}}");
                    result = JsonValue.Number(73L); break;
                case "authoring.submitTransaction":
                {
                    ulong count = (ulong)parameters.Find("commands")!.Items.Count;
                    result = JsonValue.NewObject().Set("first", JsonValue.Number(_ticket)).Set("count", JsonValue.Number(count));
                    string? error = NextError ?? (RejectCommands ? "NoDocument" : null);
                    if (error is not null && count > 0) Failures.Add(JsonValue.NewObject()
                        .Set("ticket", JsonValue.Number(_ticket)).Set("error", JsonValue.String(error)));
                    NextError = null;
                    _ticket += count;
                    break;
                }
                case "authoring.drainFailures": result = ToArray(Failures); Failures.Clear(); break;
                case "connection.drainEvents":
                case "authoring.drainEvents":
                {
                    string id = method == "connection.drainEvents" ? "41" : "73";
                    if (parameters.Find("subscription")?.NumberText != id) throw new InvalidOperationException("wrong subscription");
                    List<JsonValue> events = method == "connection.drainEvents" ? WorldEvents : AuthoringEvents;
                    result = EventError is null ? JsonValue.NewObject().Set("ok", ToArray(events))
                        : JsonValue.NewObject().Set("err", JsonValue.String(EventError));
                    events.Clear();
                    break;
                }
                case "control.step":
                    JsonValue image = parameters.Find("request")!.Find("image")!;
                    result = StepReport(image.IsString ? image.AsString : null); break;
                default: result = JsonValue.Null(); break;
            }
            return JsonValue.NewObject().Set("result", result);
        }
    }

    private sealed class CaptureDirectory : IDisposable
    {
        public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "atlantis-mcp-m3-" + Guid.NewGuid().ToString("N"));
        public CaptureDirectory() => Directory.CreateDirectory(Path);
        public void Dispose()
        {
            foreach (string file in Directory.EnumerateFiles(Path)) File.Delete(file);
            Directory.Delete(Path);
        }
    }
}
