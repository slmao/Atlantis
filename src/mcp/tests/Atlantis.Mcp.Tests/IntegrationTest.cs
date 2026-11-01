using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Gameplay.Remote;
using ModelContextProtocol.Client;
using ModelContextProtocol.Protocol;

namespace Atlantis.Mcp.Tests;

// Plan 0070 M4 / 0071 S4: no fixture endpoint and no direct SDK session calls. Every engine
// operation below crosses MCP stdio, through two real hosts with separate Runtime connections.
internal sealed class IntegrationTest
{
    private sealed record CheckResult(string Name, bool Ok, string Detail);
    private sealed record Reply(CallToolResult Mcp, JsonValue Wire);
    private readonly List<CheckResult> _checks = new();
    private readonly List<string> _captures = new();
    private readonly CancellationToken _token;
    private readonly string _work;

    private IntegrationTest(string work, CancellationToken token) { _work = work; _token = token; }

    internal static async Task<int> Run(string runtimePath, string hostPath, string scene, string outputDirectory)
    {
        string work = Path.Combine(Path.GetFullPath(outputDirectory), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(work);
        using CancellationTokenSource deadline = new(TimeSpan.FromMinutes(12));
        var test = new IntegrationTest(work, deadline.Token);
        RuntimeProcess? runtime = null;
        try
        {
            runtime = new RuntimeProcess(Path.GetFullPath(runtimePath), scene, work);
            await runtime.WaitForSession(deadline.Token);
            Console.WriteLine("MCP live " + scene + ": Runtime listening; artifacts " + work);
            await using (McpClient first = await Connect(hostPath, runtime.SessionPath, deadline.Token))
            await using (McpClient second = await Connect(hostPath, runtime.SessionPath, deadline.Token))
                await test.Session(first, second, scene);
        }
        catch (Exception e)
        {
            test._checks.Add(new CheckResult("live session completed", false, e.ToString()));
        }
        finally
        {
            if (runtime is not null)
            {
                int? exit = await runtime.Close();
                test._checks.Add(new CheckResult("Runtime exits normally under fatal Vulkan validation", exit == 0,
                    exit is int code ? "exit " + code : "forced termination"));
                string log = File.ReadAllText(runtime.LogPath);
                string[] hits = Regex.Matches(log, @"[^\r\n]*(?:VUID-|UNASSIGNED-|Validation Error|Validation Warning|Vulkan Validation Layers reported)[^\r\n]*",
                    RegexOptions.IgnoreCase).Select(m => m.Value).ToArray();
                test._checks.Add(new CheckResult("Vulkan validation warning/error log scan", hits.Length == 0, string.Join("\n", hits)));
            }
        }
        int failed = test._checks.Count(c => !c.Ok);
        var report = new
        {
            scene, runtime = Path.GetFullPath(runtimePath), host = Path.GetFullPath(hostPath),
            work, passed = test._checks.Count - failed, failed, captures = test._captures, checks = test._checks,
        };
        string reportPath = Path.Combine(work, "report.json");
        File.WriteAllText(reportPath, JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"MCP live {scene}: {test._checks.Count - failed} checks passed, {failed} failed; {reportPath}");
        foreach (CheckResult failure in test._checks.Where(c => !c.Ok)) Console.Error.WriteLine(failure.Name + ": " + failure.Detail);
        return failed == 0 ? 0 : 1;
    }

    private static Task<McpClient> Connect(string host, string session, CancellationToken token) =>
        McpClient.CreateAsync(new StdioClientTransport(new StdioClientTransportOptions
        {
            Name = "Atlantis M4 " + Guid.NewGuid().ToString("N"), Command = "dotnet",
            Arguments = [Path.GetFullPath(host), "--session", session],
        }), new McpClientOptions { InitializationTimeout = TimeSpan.FromMinutes(5) }, cancellationToken: token);

    private void Require(string name, bool ok, string detail = "")
    {
        _checks.Add(new CheckResult(name, ok, detail));
        if (!ok) throw new InvalidOperationException(name + (detail.Length == 0 ? "" : ": " + detail));
    }

    private async Task<Reply> Call(McpClient client, string name, JsonValue? parameters = null, bool error = false)
    {
        var arguments = JsonSerializer.Deserialize<Dictionary<string, object?>>(Json.Write(parameters ?? JsonValue.NewObject()))!;
        CallToolResult result = await client.CallToolAsync(name, arguments, cancellationToken: _token);
        string text = result.Content.OfType<TextContentBlock>().FirstOrDefault()?.Text ?? "";
        Require(name + ": MCP error flag", (result.IsError == true) == error, error ? text : result.IsError == true ? text : "");
        JsonValue wire = Json.Parse(text);
        Require(name + ": structured JSON matches wire text", result.StructuredContent is JsonElement structured
            ? Json.Write(Json.Parse(structured.GetRawText())) == Json.Write(wire) : wire.IsNull);
        return new Reply(result, wire);
    }

    private async Task<JsonValue> Query(McpClient client, string name, JsonValue? parameters = null) => (await Call(client, name, parameters)).Wire;
    private static JsonValue Object(string name, JsonValue value) => JsonValue.NewObject().Set(name, value);
    private static JsonValue String(string value) => JsonValue.String(value);
    private static JsonValue Address(string node, string component, string field) =>
        Object("node", String(node)).Set("component", String(component)).Set("field", String(field));
    private static JsonValue Property(JsonValue address, JsonValue value) => Object("address", address).Set("value", value);

    private static (string Component, string Field) Resolve(JsonValue schema, string type, string field)
    {
        JsonValue component = schema.Items.Single(t => t.Find("name")!.AsString.EndsWith("::" + type, StringComparison.Ordinal));
        JsonValue member = component.Find("fields")!.Items.Single(f => f.Find("name")!.AsString == field);
        return (component.Find("id")!.AsString, member.Find("id")!.AsString);
    }

    private async Task<JsonValue> Submit(McpClient client, string name, JsonValue parameters)
    {
        JsonValue ticket = await Query(client, name, parameters);
        Require(name + ": transaction ticket", ticket.Find("first")?.TryGetUInt64(out _) == true && ticket.Find("count")?.TryGetUInt64(out _) == true);
        JsonValue failures = await Query(client, "authoring.drain_failures");
        Require(name + ": explicit settlement", failures.IsArray && failures.Items.Count == 0, Json.Write(failures));
        return ticket;
    }

    private async Task<JsonValue> Read(McpClient client, JsonValue address) =>
        (await Query(client, "authoring.get_property", Object("address", address))).Find("ok")!;

    private async Task Session(McpClient first, McpClient second, string scene)
    {
        foreach (McpClient client in new[] { first, second })
        {
            var tools = await client.ListToolsAsync(cancellationToken: _token);
            Require("real host discovers exactly 32 tools", tools.Count == 32 && tools.Select(t => t.Name).Distinct().Count() == 32);
        }
        await Query(first, "control.pause");
        Require("Runtime paused", (await Query(first, "control.status")).Find("paused")?.AsBool == true);
        await Query(first, "authoring.open");
        JsonValue document = await Query(first, "authoring.document");
        Require("source provenance", document.Find("provenance")?.AsString == (scene == "bistro" ? "Imported" : "HandAuthored"));
        string source = document.Find("source")!.AsString;
        byte[] sourceHash = SHA256.HashData(File.ReadAllBytes(source));
        JsonValue nodes = (await Query(first, "authoring.list_nodes")).Find("ok")!;
        string originalNodes = Json.Write(nodes);
        JsonValue schema = await Query(first, "authoring.schema");
        Require("authoring schema available in-band", schema.IsArray && schema.Items.Count > 0);
        JsonValue worldSchema = await Query(first, "world.schema");
        Require("World schema available in-band", worldSchema.IsArray && worldSchema.Items.Count > 0);
        var cameraField = Resolve(schema, "Camera", "fovYRadians");
        var positionField = Resolve(schema, "Transform", "localPosition");
        string camera = (await Query(first, "authoring.active_camera")).Find("ok")!.AsString;
        JsonValue cameraAddress = Address(camera, cameraField.Component, cameraField.Field);
        JsonValue originalFov = await Read(first, cameraAddress);
        Require("active camera FOV is finite", originalFov.Find("f32")?.TryGetFloat(out _) == true);
        originalFov.Find("f32")!.TryGetFloat(out float fov);
        JsonValue editedFov = Object("f32", JsonValue.Number(fov * 0.9f));
        JsonValue before = await Capture(first, "before");
        await Query(first, "authoring.drain_events");
        await Query(second, "authoring.drain_events");

        await Submit(first, "set_properties", Object("properties", JsonValue.NewArray().Push(Property(cameraAddress, editedFov))));
        Require("document edit visible before apply", Json.Write(await Read(first, cameraAddress)) == Json.Write(editedFov));
        await ApplyAndWait(first, second, "camera-edit");
        JsonValue edited = await Capture(first, "camera-edit");
        Require("rendered projection reflects the document edit", Json.Write(before.Find("data")!.Find("projection")!)
            != Json.Write(edited.Find("data")!.Find("projection")!));
        var worldCamera = Resolve(worldSchema, "Camera", "fovYRadians");
        JsonValue worldAddress = Object("entity", String(camera)).Set("component", String(worldCamera.Component)).Set("field", String(worldCamera.Field));
        Require("reload reached the Runtime World", Json.Write((await Query(first, "world.get_property", Object("address", worldAddress))).Find("ok")!) == Json.Write(editedFov));
        Console.WriteLine("MCP live " + scene + ": edit, explicit settlement, apply/Applied and PNG capture passed");

        // The second connection observes a node that the first later destroys. Its stale
        // transaction first changes a live property, then writes the deleted node: refusal must
        // roll back the whole transaction, with the failed command's ticket in that connection.
        string scratch = await CreateNode(first, second, (ulong)nodes.Items.Count);
        await Submit(first, "destroy_node", Object("node", String(scratch)));
        JsonValue secondEvents = (await Query(second, "authoring.drain_events")).Find("ok")!;
        Require("second client observes the deletion", secondEvents.Items.Any(e => e.Find("kind")?.AsString == "NodeDestroyed" && e.Find("node")?.AsString == scratch));
        JsonValue snapshotNodes = (await Query(second, "authoring.list_nodes")).Find("ok")!;
        JsonValue snapshotFov = await Read(second, cameraAddress);
        JsonValue commands = JsonValue.NewArray()
            .Push(Object("kind", String("SetProperty")).Set("address", cameraAddress).Set("value", originalFov))
            .Push(Object("kind", String("SetProperty")).Set("address", Address(scratch, positionField.Component, positionField.Field))
                .Set("value", Object("vec3", Json.Parse("[1,2,3]"))));
        JsonValue refusedTicket = await Query(second, "authoring.submit_transaction", Object("commands", commands));
        Require("refused transaction still returns its two-command ticket", refusedTicket.Find("count")?.NumberText == "2");
        Reply failure = await Call(second, "authoring.drain_failures", error: true);
        refusedTicket.Find("first")!.TryGetUInt64(out ulong firstTicket);
        Require("UnknownNode is returned verbatim under the refused command ticket", failure.Wire.IsArray && failure.Wire.Items.Count == 1
            && failure.Wire.Items[0].Find("ticket")!.TryGetUInt64(out ulong failedTicket) && failedTicket == firstTicket + 1
            && failure.Wire.Items[0].Find("error")?.AsString == "UnknownNode", Json.Write(failure.Wire));
        Require("refusal preserves all-or-nothing document properties", Json.Write(await Read(second, cameraAddress)) == Json.Write(snapshotFov));
        Require("refusal preserves node inventory", Json.Write((await Query(second, "authoring.list_nodes")).Find("ok")!) == Json.Write(snapshotNodes));
        Require("refused writes emit no property changes", (await Query(second, "authoring.drain_events")).Find("ok")!.Items.Count == 0);
        Require("failure queue is connection-local", (await Query(first, "authoring.drain_failures")).Items.Count == 0);
        Require("second failure drain is empty", (await Query(second, "authoring.drain_failures")).Items.Count == 0);
        Require("scratch node removed; original order retained", Json.Write(snapshotNodes) == originalNodes);

        await Submit(first, "set_properties", Object("properties", JsonValue.NewArray().Push(Property(cameraAddress, originalFov))));
        await ApplyAndWait(first, second, "restore");
        JsonValue restored = await Capture(first, "restored");
        Require("rendered projection restored", Json.Write(restored.Find("data")!.Find("projection")!) == Json.Write(before.Find("data")!.Find("projection")!));
        Require("document restored without save", Json.Write(await Read(first, cameraAddress)) == Json.Write(originalFov)
            && Json.Write((await Query(first, "authoring.list_nodes")).Find("ok")!) == originalNodes);
        Require("source file bytes unchanged", SHA256.HashData(File.ReadAllBytes(source)).SequenceEqual(sourceHash));
        JsonValue diagnostics = await Query(first, "control.diagnostics", Object("after", JsonValue.Number(0L)).Set("max", JsonValue.Number(4096L)));
        Require("diagnostics complete and free of warnings/errors", diagnostics.Find("dropped")?.NumberText == "0"
            && diagnostics.Find("entries")!.Items.Count == 0, Json.Write(diagnostics));
        Require("World failure drain empty", (await Query(first, "world.drain_failures")).Items.Count == 0);
        await Query(first, "control.resume");
    }

    private async Task<string> CreateNode(McpClient first, McpClient second, ulong position)
    {
        await Query(first, "authoring.drain_events");
        await Query(second, "authoring.drain_events");
        await Submit(first, "create_node", Object("position", JsonValue.Number(position)).Set("components", JsonValue.NewArray()));
        JsonValue events = (await Query(first, "authoring.drain_events")).Find("ok")!;
        string node = events.Items.Single(e => e.Find("kind")?.AsString == "NodeCreated").Find("node")!.AsString;
        Require("created node has a non-nil GUID", Guid.TryParse(node, out Guid guid) && guid != Guid.Empty);
        Require("second client observes the created node", (await Query(second, "authoring.list_nodes")).Find("ok")!.Items.Any(n => n.Find("node")?.AsString == node));
        // Clear creation notifications before asserting the deletion and refused edit below.
        await Query(second, "authoring.drain_events");
        return node;
    }

    private async Task ApplyAndWait(McpClient first, McpClient second, string stage)
    {
        ulong old = StatusGeneration(await Query(first, "control.status"));
        await Query(first, "authoring.drain_events");
        await Query(second, "authoring.drain_events");
        await Query(first, "authoring.apply");
        await Query(first, "control.step", Object("request", Object("frames", JsonValue.Number(1L)).Set("image", JsonValue.Null())));
        var seen = new ulong?[2];
        long until = Environment.TickCount64 + 60000;
        while (Environment.TickCount64 < until && seen.Any(g => g is null))
        {
            McpClient[] clients = [first, second];
            for (int i = 0; i < clients.Length; ++i)
            {
                if (seen[i] is not null) continue;
                JsonValue events = (await Query(clients[i], "authoring.drain_events")).Find("ok")!;
                foreach (JsonValue e in events.Items)
                    if (e.Find("kind")?.AsString == "Applied" && e.Find("generation")!.TryGetUInt64(out ulong generation) && generation > old)
                        seen[i] = generation;
            }
            if (seen.Any(g => g is null)) await Task.Delay(10, _token);
        }
        ulong current = StatusGeneration(await Query(first, "control.status"));
        Require(stage + ": Applied observed on both connections and matches status", current == old + 1 && seen[0] == current && seen[1] == current);
    }

    private static ulong StatusGeneration(JsonValue status)
    {
        if (status.Find("sceneGeneration")?.TryGetUInt64(out ulong value) != true) throw new InvalidOperationException("missing sceneGeneration");
        return value;
    }

    private async Task<JsonValue> Capture(McpClient client, string name)
    {
        string path = Path.Combine(_work, name + ".png");
        Reply result = await Call(client, "control.step", Object("request", Object("frames", JsonValue.Number(1L))
            .Set("image", String(path)).Set("withOverlay", JsonValue.Bool(false))));
        JsonValue frame = result.Wire.Find("ok")!;
        JsonValue image = frame.Find("image")!;
        Require(name + ": frame reports requested PNG path", image.Find("path")?.AsString == path);
        ImageContentBlock[] blocks = result.Mcp.Content.OfType<ImageContentBlock>().ToArray();
        byte[] file = File.ReadAllBytes(path);
        Require(name + ": nonempty PNG image block equals file", blocks.Length == 1 && blocks[0].MimeType == "image/png"
            && blocks[0].DecodedData.Span.SequenceEqual(file) && file.Length > 24 && file.AsSpan(0, 8).SequenceEqual(new byte[] { 137,80,78,71,13,10,26,10 }));
        uint width = System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(file.AsSpan(16, 4));
        uint height = System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(file.AsSpan(20, 4));
        Require(name + ": PNG dimensions match frame report", width > 0 && height > 0
            && image.Find("width")?.NumberText == width.ToString(System.Globalization.CultureInfo.InvariantCulture)
            && image.Find("height")?.NumberText == height.ToString(System.Globalization.CultureInfo.InvariantCulture));
        Require(name + ": scene drew geometry", frame.Find("data")!.Find("drawItemCount")!.TryGetUInt64(out ulong count) && count > 0);
        _captures.Add(path);
        return frame;
    }

    private sealed class RuntimeProcess
    {
        private readonly Process _process;
        private readonly Task<string> _stdout;
        private readonly Task<string> _stderr;
        internal string SessionPath { get; }
        internal string LogPath { get; }

        internal RuntimeProcess(string executable, string scene, string work)
        {
            SessionPath = Path.Combine(work, "runtime.session.json");
            LogPath = Path.Combine(work, "runtime.log");
            ProcessStartInfo start = new(executable)
            {
                // A hidden/minimized window makes this windowed Runtime NotRendering. Follow the
                // existing live-test launch pattern; do not replace GPU acceptance with a fixture.
                UseShellExecute = false, CreateNoWindow = true,
                RedirectStandardOutput = true, RedirectStandardError = true, WorkingDirectory = Path.GetDirectoryName(executable)!,
            };
            if (scene != "integrated_showcase_demo") { start.ArgumentList.Add("--scene"); start.ArgumentList.Add(scene); }
            foreach (string argument in new[] { "--listen", "0", "--session-file", SessionPath }) start.ArgumentList.Add(argument);
            _process = Process.Start(start) ?? throw new InvalidOperationException("Runtime did not start");
            _stdout = _process.StandardOutput.ReadToEndAsync();
            _stderr = _process.StandardError.ReadToEndAsync();
        }

        internal async Task WaitForSession(CancellationToken token)
        {
            long until = Environment.TickCount64 + 300000;
            while (Environment.TickCount64 < until)
            {
                if (_process.HasExited) throw new InvalidOperationException("Runtime exited before listening: " + _process.ExitCode);
                if (File.Exists(SessionPath))
                {
                    try { _ = SessionFile.Read(SessionPath); return; }
                    catch (SessionFileException) { } // The writer may not have finished the file yet.
                }
                await Task.Delay(50, token);
            }
            throw new TimeoutException("Runtime did not publish its session file");
        }

        internal async Task<int?> Close()
        {
            int? code = null;
            if (!_process.HasExited)
            {
                _process.Refresh();
                _process.CloseMainWindow();
                try { await _process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(30)); }
                catch (TimeoutException) { _process.Kill(entireProcessTree: true); }
            }
            if (_process.HasExited) code = _process.ExitCode;
            await _process.WaitForExitAsync();
            File.WriteAllText(LogPath, "STDOUT\n" + await _stdout + "\nSTDERR\n" + await _stderr);
            _process.Dispose();
            return code;
        }
    }
}
