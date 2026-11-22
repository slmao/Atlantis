using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static async Task<int> RunBatchRenameReceipts(string executable, string scene)
    {
        using var budget = new CancellationTokenSource(TimeSpan.FromSeconds(90));
        string work = RuntimeProcess.NewWorkDirectory();
        using var runtime = RuntimeProcess.Start(new(executable, scene, null, work));
        var endpoint = runtime.WaitForSession(TimeSpan.FromSeconds(25));
        using var peer = RemoteSession.Connect(endpoint);
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int hold = 0, drop = 0, submits = 0, applies = 0;
        var held = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var released = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Task relay = Relay();
        try
        {
            using var log = new EditorLog(Path.Combine(work, "guarded-rename.log"));
            using var worker = new EditorWorker(endpoint with { Port = (ushort)((IPEndPoint)listener.LocalEndpoint).Port },
                action => action(), log);
            var opened = new TaskCompletionSource<DocumentInfo?>(TaskCreationOptions.RunContinuationsAsynchronously);
            worker.DocumentOpened += info => opened.TrySetResult(info);
            worker.Start();
            Require((await opened.Task.WaitAsync(budget.Token))?.SupportsGuardedNameTransactions == true, "fixture opened");
            var targets = peer.Authoring.ListNodes().Value.Take(2).Select(node => node.Node).ToArray();
            var before = peer.Authoring.DocumentState().Value!.Token;
            ulong projection = peer.Control.Status().SceneGeneration;

            var (intent, preview) = await ReadPreview(1);
            Interlocked.Exchange(ref hold, 1);
            var pending = QueueRename(intent, preview);
            await held.Task.WaitAsync(budget.Token);
            worker.ShowSelection([targets[0]], 2);
            released.TrySetResult();
            var cancelled = await pending.WaitAsync(budget.Token);
            Require(cancelled.State == BatchReplayState.Retry && submits == 0 && applies == 0 &&
                peer.Authoring.DocumentState().Value!.Token == before,
                "selection changed after capability request: no submit, revision or Apply");

            (intent, preview) = await ReadPreview(3);
            Interlocked.Exchange(ref drop, 1);
            var unknown = await QueueRename(intent, preview).WaitAsync(budget.Token);
            Require(unknown.State == BatchReplayState.Drop && unknown.Notice.Contains("outcome unknown", StringComparison.Ordinal) &&
                targets.All(target => unknown.Notice.Contains(target.ToString(), StringComparison.Ordinal)) &&
                submits == 1 && applies == 0, "lost transaction ticket reports unknown and both GUIDs without a retry or Apply");
            Require(peer.Authoring.DocumentState().Value!.Token.DocumentRevision == before.DocumentRevision + 1 &&
                peer.Control.Status().SceneGeneration == projection, "peer observes the whole commit on the previous projection");

            async Task<(BatchRenameIntent Intent, BatchRenamePreview Preview)> ReadPreview(ulong generation)
            {
                worker.ShowSelection(targets, generation);
                var current = new BatchRenameIntent(worker.MeshDocumentLifetime, generation, targets);
                var ready = new TaskCompletionSource<BatchRenamePreview>(TaskCreationOptions.RunContinuationsAsynchronously);
                worker.PreviewBatchRename(current, (result, reason) =>
                {
                    if (result is null) ready.TrySetException(new InvalidOperationException(reason));
                    else ready.TrySetResult(result);
                });
                return (current, await ready.Task.WaitAsync(budget.Token));
            }

            Task<BatchReplayResult> QueueRename(BatchRenameIntent current, BatchRenamePreview view)
            {
                var done = new TaskCompletionSource<BatchReplayResult>(TaskCreationOptions.RunContinuationsAsynchronously);
                Require(worker.CommitBatchRename(current, view, view.Propose("Ticket Lamp")!, result => done.TrySetResult(result)),
                    "queued guarded name batch");
                return done.Task;
            }
            return 0;
        }
        finally
        {
            released.TrySetResult();
            budget.Cancel();
            try { await relay.WaitAsync(TimeSpan.FromSeconds(5)); }
            catch (OperationCanceledException) when (budget.IsCancellationRequested) { }
        }

        async Task Relay()
        {
            using var frontend = await listener.AcceptTcpClientAsync(budget.Token);
            using var backend = new TcpClient();
            await backend.ConnectAsync(IPAddress.Loopback, endpoint.Port, budget.Token);
            using var input = new StreamReader(frontend.GetStream());
            using var output = new StreamWriter(frontend.GetStream()) { AutoFlush = true, NewLine = "\n" };
            using var requests = new StreamWriter(backend.GetStream()) { AutoFlush = true, NewLine = "\n" };
            using var responses = new StreamReader(backend.GetStream());
            while (await input.ReadLineAsync(budget.Token) is string line)
            {
                var method = Json.Parse(line).Find("method")?.AsString;
                await requests.WriteLineAsync(line.AsMemory(), budget.Token);
                var reply = await responses.ReadLineAsync(budget.Token) ?? throw new IOException("Runtime closed the relay.");
                if (method == "authoring.document" && Interlocked.Exchange(ref hold, 0) == 1)
                {
                    held.TrySetResult();
                    await released.Task.WaitAsync(budget.Token);
                }
                if (method == "authoring.applyTracked") Interlocked.Increment(ref applies);
                if (method == "authoring.submitTransaction")
                {
                    Interlocked.Increment(ref submits);
                    if (Volatile.Read(ref drop) == 1) return;
                }
                await output.WriteLineAsync(reply.AsMemory(), budget.Token);
            }
        }

        static void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            Console.WriteLine("PASS " + message);
        }
    }
}
