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
                    _ => request.Find("params")!,
                });
            await writer.WriteLineAsync(Json.Write(response).AsMemory(), token);
        }
    }
}
