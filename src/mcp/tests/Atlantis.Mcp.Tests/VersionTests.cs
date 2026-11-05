using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Gameplay.Remote;
using ModelContextProtocol.Client;
using ModelContextProtocol.Protocol;

namespace Atlantis.Mcp.Tests;

internal static partial class Program
{
    private static async Task VersionChecks()
    {
        foreach (string version in new[] { "2025-11-25", "2026-07-28" })
        {
            bool legacy = version == "2025-11-25";
            foreach (string json in new[] { "[]", "[-0,18446744073709551615,1e-05]", "null", "true", "17", "\"原样\"", "{\"ok\":null}",
                "{\"ok\":[{\"kind\":\"NodeNameChanged\",\"node\":\"" + Entity + "\",\"name\":\"名称\"}]}" })
            {
                var original = new CallToolResult
                {
                    IsError = true,
                    StructuredContent = JsonSerializer.Deserialize<JsonElement>(json),
                    Content = [new TextContentBlock { Text = json }],
                };
                CallToolResult adapted = ToolRegistry.ForProtocol(original, version);
                Check(ReferenceEquals(original, adapted) && adapted.IsError == true,
                    version + " adapts only the envelope, preserving error flags");
                CheckVersionResult(adapted, json, legacy, version + " pure value " + json);
            }
            var absent = new CallToolResult { IsError = true, Content = [new TextContentBlock { Text = "NoAuthoring" }] };
            Check(ToolRegistry.ForProtocol(absent, version).StructuredContent is null
                && ((TextContentBlock)absent.Content[0]).Text == "NoAuthoring", version + " does not invent structured content for transport errors");

            using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(45));
            await using McpClient client = await VersionClient("--result-host", version, deadline.Token);
            Check(client.NegotiatedProtocolVersion == version, "explicit protocol negotiation: " + version);
            var tools = await client.ListToolsAsync(cancellationToken: deadline.Token);
            Check(tools.Select(t => t.Name).Order().SequenceEqual(ExpectedNames.Order()), version + " exposes unchanged tools");
            foreach (var c in Cases)
            {
                CallToolResult result = await client.CallToolAsync(c.Tool,
                    JsonSerializer.Deserialize<Dictionary<string, object?>>(c.Args)!, cancellationToken: deadline.Token);
                Check(result.IsError != true, version + " " + c.Tool + " succeeds");
                CheckVersionResult(result, c.Result, legacy, version + " " + c.Tool);
            }

            await using McpClient queue = await VersionClient("--m3-host", version, deadline.Token);
            CallToolResult renamed = await client.CallToolAsync("authoring.submit_transaction",
                new Dictionary<string, object?> { ["commands"] = new[] { new Dictionary<string, object?>
                    { ["kind"] = "SetNodeName", ["node"] = Entity, ["name"] = "名 \"quoted\" \\" } } }, cancellationToken: deadline.Token);
            CheckVersionResult(renamed, Cases.Single(c => c.Tool == "authoring.submit_transaction").Result, legacy, version + " rename through existing tool");
            await queue.CallToolAsync("destroy_node", new Dictionary<string, object?> { ["node"] = Entity }, cancellationToken: deadline.Token);
            CallToolResult failures = await queue.CallToolAsync("authoring.drain_failures", cancellationToken: deadline.Token);
            Check(failures.IsError == true, version + " preserves deferred failure error flag");
            CheckVersionResult(failures, "[{\"ticket\":100,\"error\":\"NoDocument\"}]", legacy, version + " deferred failure array");
            CallToolResult empty = await queue.CallToolAsync("authoring.drain_failures", cancellationToken: deadline.Token);
            Check(empty.IsError != true, version + " failure queue still clears");
            CheckVersionResult(empty, "[]", legacy, version + " empty failure array");

            using CaptureDirectory files = new();
            string path = System.IO.Path.Combine(files.Path, "version.png");
            System.IO.File.WriteAllBytes(path, Png);
            CallToolResult capture = await queue.CallToolAsync("control.step", new Dictionary<string, object?>
            {
                ["request"] = new { frames = 1, image = path, withOverlay = false },
            }, cancellationToken: deadline.Token);
            Check(capture.IsError != true, version + " capture succeeds");
            CheckVersionResult(capture, Json.Write(StepReport(path)), legacy, version + " capture object");
            Check(capture.Content.OfType<ImageContentBlock>().Single().DecodedData.Span.SequenceEqual(Png),
                version + " image block is unchanged");
        }
    }

    private static Task<McpClient> VersionClient(string entry, string version, CancellationToken token) =>
        McpClient.CreateAsync(new StdioClientTransport(new StdioClientTransportOptions
        {
            Name = "explicit " + version, Command = "dotnet", Arguments = [typeof(Program).Assembly.Location, entry],
        }), new McpClientOptions { ProtocolVersion = version }, cancellationToken: token);

    private static void CheckVersionResult(CallToolResult result, string expected, bool legacy, string context)
    {
        Check(result.Content[0] is TextContentBlock text && text.Text == expected, context + " original JSON text unchanged");
        using JsonDocument wire = JsonDocument.Parse(expected);
        if (legacy)
        {
            Check(result.StructuredContent is JsonElement { ValueKind: JsonValueKind.Object }, context + " legacy structured content is an object");
            JsonElement structured = (JsonElement)result.StructuredContent!;
            if (wire.RootElement.ValueKind != JsonValueKind.Object)
            {
                Check(structured.EnumerateObject().Count() == 1 && structured.TryGetProperty("result", out _), context + " one legacy result envelope");
                structured = structured.GetProperty("result");
            }
            Check(Json.Write(Json.Parse(structured.GetRawText())) == expected, context + " original structured value unchanged");
        }
        else
            Check(result.StructuredContent is JsonElement structured
                ? Json.Write(Json.Parse(structured.GetRawText())) == expected : expected == "null", context + " modern structured shape unchanged");
    }
}
