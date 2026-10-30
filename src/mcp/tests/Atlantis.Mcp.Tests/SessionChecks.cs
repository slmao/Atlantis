using System;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using ModelContextProtocol.Client;
using ModelContextProtocol.Protocol;

namespace Atlantis.Mcp.Tests;

internal static class SessionChecks
{
    // GPU-free SDK fixture: verifies the production adapter's raw Send/AwaitResponse path, refusal
    // recovery and mutual exclusion, rather than testing only a fake implementation of the seam.
    internal static async Task Run(Action<bool, string> check)
    {
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(15));
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var requests = new ConcurrentQueue<JsonValue>();
        Task server = Serve(listener, requests, deadline.Token);
        try
        {
            var info = new SessionInfo((ushort)((IPEndPoint)listener.LocalEndpoint).Port, "M2-fixture", 1, default);
            using (RemoteSession remote = RemoteSession.Connect(info, new RemoteOptions { ResponseTimeoutMilliseconds = 5000 }))
            {
                IAtlantisSession session = new AtlantisSession(remote);
                JsonValue refusal = session.Request("authoring.open", JsonValue.NewObject());
                check(Json.Write(refusal.Find("error")!) == "{\"code\":\"NoAuthoring\",\"message\":\"原样 refusal\"}",
                    "SDK seam preserves wire error code and reason");
                JsonValue parameters = Json.Parse("{\"after\":18446744073709551615,\"max\":20}");
                JsonValue success = session.Request("control.diagnostics", parameters);
                check(Json.Write(success.Find("result")!) == Json.Write(parameters), "wire refusal does not poison SDK session or alter integers");
                JsonValue[] answers = await Task.WhenAll(Enumerable.Range(0, 24).Select(i => Task.Run(() =>
                    session.Request("control.status", JsonValue.NewObject().Set("marker", JsonValue.Number((long)i))))));
                for (int i = 0; i < answers.Length; ++i)
                    check(answers[i].Find("result")?.Find("marker")?.NumberText == i.ToString(System.Globalization.CultureInfo.InvariantCulture),
                        "concurrent SDK requests retain their response identity " + i);
            }
            await server.WaitAsync(deadline.Token);
            JsonValue[] sent = requests.ToArray();
            check(sent.Length == 28, "SDK seam: hello + connect schema + exactly one request per call, no retries");
            check(sent[0].Find("method")?.AsString == "hello"
                && sent[1].Find("method")?.AsString == "connection.schema", "SDK performs its own hello and schema discovery");
            check(sent[2].Find("method")?.AsString == "authoring.open" && sent[3].Find("method")?.AsString == "control.diagnostics",
                "adapter forwards methods unchanged");
            check(Json.Write(sent[3].Find("params")!) == "{\"after\":18446744073709551615,\"max\":20}",
                "adapter forwards parameter JSON unchanged");
        }
        finally
        {
            deadline.Cancel();
            listener.Stop();
            try { await server; }
            catch (OperationCanceledException) { }
        }
    }

    // The production executable, not the test server entry point: verify that its connect path
    // actually initializes the subscriptions before MCP tools are served. The engine is a fixture,
    // so this remains GPU-free and does not substitute for Plan 0070 M4's live Runtime sessions.
    internal static async Task RunHost(Action<bool, string> check)
    {
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(20));
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var requests = new ConcurrentQueue<JsonValue>();
        Task server = Serve(listener, requests, deadline.Token);
        string file = Path.Combine(Path.GetTempPath(), "atlantis-mcp-session-" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            JsonValue info = JsonValue.NewObject().Set("protocol", JsonValue.String("atlantis.remote/1"))
                .Set("token", JsonValue.String("M2-fixture"))
                .Set("port", JsonValue.Number((long)((IPEndPoint)listener.LocalEndpoint).Port))
                .Set("pid", JsonValue.Number(1L)).Set("scene", JsonValue.String("11111111-2222-4333-8444-555555555555"));
            File.WriteAllText(file, Json.Write(info));
            await using (McpClient client = await McpClient.CreateAsync(new StdioClientTransport(new StdioClientTransportOptions
            {
                Name = "M3 production host", Command = "dotnet", Arguments = [typeof(ToolRegistry).Assembly.Location, "--session", file],
            }), cancellationToken: deadline.Token))
            {
                var tools = await client.ListToolsAsync(cancellationToken: deadline.Token);
                check(tools.Count == 32, "production host discovers all 32 tools after connecting");
                CallToolResult status = await client.CallToolAsync("control.status", cancellationToken: deadline.Token);
                check(status.IsError != true, "production host forwards control through the SDK");
                foreach (string name in new[] { "world.drain_events", "authoring.drain_events" })
                {
                    CallToolResult result = await client.CallToolAsync(name, cancellationToken: deadline.Token);
                    check(result.IsError != true && result.Content[0] is TextContentBlock text && text.Text == "{\"ok\":[]}",
                        "production host drains its own subscription: " + name);
                }
            }
            await server.WaitAsync(deadline.Token);
            JsonValue[] sent = requests.ToArray();
            check(sent[2].Find("method")?.AsString == "connection.subscribe"
                && sent[3].Find("method")?.AsString == "authoring.subscribe", "both subscriptions precede production tool calls");
            check(sent.Count(r => r.Find("method")?.AsString.EndsWith(".subscribe", StringComparison.Ordinal) == true) == 2,
                "production host creates exactly one subscription per family");
            check(sent.Single(r => r.Find("method")?.AsString == "connection.drainEvents").Find("params")?.Find("subscription")?.NumberText == "41",
                "production World drain uses the server's subscription ID");
            check(sent.Single(r => r.Find("method")?.AsString == "authoring.drainEvents").Find("params")?.Find("subscription")?.NumberText == "73",
                "production authoring drain uses the server's subscription ID");
        }
        finally
        {
            File.Delete(file);
            deadline.Cancel();
            listener.Stop();
            try { await server; }
            catch (OperationCanceledException) { }
        }
    }

    private static async Task Serve(TcpListener listener, ConcurrentQueue<JsonValue> requests, CancellationToken token)
    {
        using TcpClient client = await listener.AcceptTcpClientAsync(token);
        await using NetworkStream stream = client.GetStream();
        using StreamReader reader = new(stream, new UTF8Encoding(false), leaveOpen: true);
        await using StreamWriter writer = new(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
        while (await reader.ReadLineAsync(token) is string line)
        {
            JsonValue request = Json.Parse(line);
            requests.Enqueue(request);
            string method = request.Find("method")!.AsString;
            JsonValue response = JsonValue.NewObject().Set("id", request.Find("id")!);
            if (method == "authoring.open")
                response.Set("error", Json.Parse("{\"code\":\"NoAuthoring\",\"message\":\"原样 refusal\"}"));
            else
                response.Set("result", method switch
                {
                    "hello" => Json.Parse("{\"scene\":\"11111111-2222-4333-8444-555555555555\"}"),
                    "connection.schema" => JsonValue.NewArray(),
                    "connection.subscribe" => JsonValue.Number(41L),
                    "authoring.subscribe" => JsonValue.Number(73L),
                    "connection.drainEvents" or "authoring.drainEvents" => Json.Parse("{\"ok\":[]}"),
                    _ => request.Find("params")!,
                });
            await writer.WriteLineAsync(Json.Write(response).AsMemory(), token);
        }
    }
}
