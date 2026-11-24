using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void PostProcessingChecks()
    {
        string[] paths = ["Camera.exposureCompensationEv", "Camera.fog.color", "Camera.fog.density", "Camera.fog.height",
            "Camera.fog.heightFalloff", "Camera.fog.maxOpacity", "Camera.bloom.strength", "Camera.bloom.threshold"];
        Check(paths.Select(PostProcessingPanel.GroupOf).GroupBy(group => group).Select(group => group.Count()).SequenceEqual([1, 5, 2]) &&
            PostProcessingPanel.GroupOf("Camera.fovYRadians") is null, "post processing groups contain exactly the eight camera leaves");
        var document = new FakeDocument();
        var node = document.Add(1, null, LocalTransform.Identity, FakeDocument.CameraType);
        var tree = new DocumentTree();
        tree.Rebuild(document);
        var fields = PostProcessingPanel.Read(document, tree, node);
        Check(fields.All(field => field.Target.Address.Node == node && field.Target.IsCurrent(tree, true)),
            "post processing snapshots pin the addressed node, component and field stamps");
        var view = new PostProcessingView(1, 1, new DocumentToken(node, 4), node, "", true, fields);
        var editable = fields.First(field => field.Field.Editable && field.Field.Present);
        var draft = new PostProcessingDraft(view, editable, "0.35");
        Check(draft.CanSubmit(view) && !draft.CanSubmit(view with { Epoch = 2 }) && view.Label == "Unnamed camera",
            "post processing returning to a prior GUID requires a fresh view and draft");
        var legacy = view with { Writable = false };
        Check(!(draft with { View = legacy }).CanSubmit(legacy), "legacy post processing remains read-only");
        tree.Apply(new DocumentReopened(default), document);
        Check(!editable.Target.IsCurrent(tree, true), "rebuilding the same GUID retires its old post-processing target");
    }

    private static int RunPostProcessingRace(string executable, string scene, bool legacy)
    {
        string work = RuntimeProcess.NewWorkDirectory();
        using var runtime = RuntimeProcess.Start(new RuntimeLaunch(Path.GetFullPath(executable), scene, null, work));
        SessionInfo host = runtime.WaitForSession(TimeSpan.FromSeconds(25));
        using var peer = RemoteSession.Connect(host);

        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        string attach = Path.Combine(work, "post-processing-relay.json");
        File.WriteAllText(attach, JsonSerializer.Serialize(new
        {
            protocol = SessionFile.Protocol, port = ((IPEndPoint)listener.LocalEndpoint).Port,
            token = host.Token, pid = host.Pid, scene = host.Scene.ToString()
        }));
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(70));
        HeldHierarchyReply? beforeSend = null, afterRead = null;
        int submits = 0, applies = 0;
        var relay = Relay();
        try
        {
            return AppBuilder.Configure(() => new SceneObjectUiApp(executable, scene, attach, Exercise))
                .UsePlatformDetect().StartWithClassicDesktopLifetime([]);
        }
        finally
        {
            timeout.Cancel();
            try { relay.GetAwaiter().GetResult(); }
            catch (OperationCanceledException) when (timeout.IsCancellationRequested) { }
            finally { runtime.CloseMainWindow(); runtime.WaitForExit(TimeSpan.FromSeconds(8)); }
        }

        async Task Relay()
        {
            var token = timeout.Token;
            using var frontend = await listener.AcceptTcpClientAsync(token);
            using var backend = new TcpClient();
            await backend.ConnectAsync(IPAddress.Loopback, host.Port, token);
            using var f = frontend.GetStream();
            using var back = backend.GetStream();
            using var input = new StreamReader(f);
            using var output = new StreamWriter(f) { NewLine = "\n", AutoFlush = true };
            using var send = new StreamWriter(back) { NewLine = "\n", AutoFlush = true };
            using var read = new StreamReader(back);
            while (await input.ReadLineAsync(token) is string request)
            {
                string method = Json.Parse(request).Find("method")!.AsString;
                if (method == "authoring.submitTransaction") Interlocked.Increment(ref submits);
                if (method == "authoring.applyTracked") Interlocked.Increment(ref applies);
                if (Volatile.Read(ref beforeSend) is { } sending && sending.Method == method &&
                    ReferenceEquals(Interlocked.CompareExchange(ref beforeSend, null, sending), sending))
                { sending.Held.SetResult(); await sending.Release.Task.WaitAsync(token); }
                await send.WriteLineAsync(request.AsMemory(), token);
                string reply = (await read.ReadLineAsync(token))!;
                if (Volatile.Read(ref afterRead) is { } returned && returned.Method == method &&
                    ReferenceEquals(Interlocked.CompareExchange(ref afterRead, null, returned), returned))
                { returned.Held.SetResult(); await returned.Release.Task.WaitAsync(token); }
                if (legacy && method == "authoring.document")
                {
                    var line = Json.Parse(reply);
                    line.Find("result")?.Set("supportsGuardedPropertyTransactions", JsonValue.Bool(false));
                    reply = Json.Write(line);
                }
                await output.WriteLineAsync(reply.AsMemory(), token);
            }
        }

        async Task Exercise(MainWindow window)
        {
            T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
            static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            PostProcessingView? View() => Field<PostProcessingView>("_postProcessingView");
            const string exposure = "Camera.exposureCompensationEv";
            var rows = Field<StackPanel>("_postProcessingRows");
            TextBox Input() => rows.GetVisualDescendants().OfType<TextBox>().Single(box => AutomationProperties.GetName(box) == exposure);
            Button Set() => rows.GetVisualDescendants().OfType<Button>().Single(button => AutomationProperties.GetName(button) == "Set " + exposure);
            float Value(EntityGuid camera) => peer.Authoring.GetProperty(SceneSchema.AddressOf(peer.Authoring.Schema,
                camera, "Camera", "exposureCompensationEv")!.Value).Value.AsFloat32();
            void Activate(EntityGuid camera)
            {
                peer.Authoring.Submit(new SetActiveCamera(camera));
                Require(peer.Authoring.DrainFailures().Count == 0 && peer.Authoring.ActiveCamera().Value == camera, "peer activated " + camera);
            }
            await NameUiWait(() => Field<ListBox>("_entities").ItemCount > 2, "attached Editor opened", 30);
            EntityGuid a = peer.Authoring.ActiveCamera().Value!.Value;
            EntityGuid b = new(Guid.NewGuid());
            var plan = SceneObjectPresets.Build(peer.Authoring.Schema, SceneObjectPreset.Camera, b,
                (ulong)peer.Authoring.ListNodes().Value.Count, null)!;
            peer.Authoring.SubmitTransaction(plan.Edit.Commands.Concat([new SetNodeName(b, "Alternate camera")]).ToArray());
            Require(peer.Authoring.DrainFailures().Count == 0, "two valid authored cameras");
            Click(Field<Button>("_postProcessingMode"));
            await NameUiWait(() => View()?.Camera == a, "camera A read", 30);
            Require(View()!.Fields.Count == 8 && Field<TextBlock>("_postProcessingGuid").Text == a.ToString(), "all eight fields and authoritative GUID are displayed");
            if (legacy)
            {
                Require(!View()!.Writable && Input().IsReadOnly && !Set().IsEnabled && submits == 0, "legacy capability leaves all writes disabled");
                return;
            }

            float first = Value(a), second = Value(b);
            var delayed = new HeldHierarchyReply("authoring.activeCamera");
            Volatile.Write(ref afterRead, delayed);
            Field<EditorWorker>("_worker").ShowPostProcessing(true);
            await delayed.Held.Task.WaitAsync(timeout.Token);
            Activate(b);
            delayed.Release.SetResult();
            await NameUiWait(() => View()?.Camera == b, "late A read did not replace B", 25);
            Input().Focus();
            Input().Text = "1.5";
            Field<TextBox>("_hierarchySearch").Focus();
            Require(submits == 0 && Input().Text == "1.5", "focus loss leaves an explicit, unsubmitted draft");
            Activate(a);
            await NameUiWait(() => View()?.Camera == a && Field<PostProcessingDraft>("_postProcessingStale")?.View.Camera == b,
                "B draft becomes copy-only on camera switch");
            Require(Value(a) == first && Value(b) == second && submits == 0, "camera switch writes neither camera");
            Click(rows.GetVisualDescendants().OfType<Button>().Single(button => Equals(button.Content,"Discard stale input")));

            Input().Text = "2";
            var heldWrite = new HeldHierarchyReply("authoring.submitTransaction");
            Volatile.Write(ref beforeSend, heldWrite);
            Click(Set());
            await heldWrite.Held.Task.WaitAsync(timeout.Token);
            Activate(b);
            heldWrite.Release.SetResult();
            await NameUiWait(() => View()?.Camera == b && Field<PostProcessingDraft>("_postProcessingPending") is null,
                "stale guarded write settled after B activation");
            Require(Value(a) == first && Value(b) == second && applies == 0 && !Field<Button>("_undo").IsEnabled,
                "host revision guard prevented write, history and Apply");
            var expired = Field<PostProcessingDraft>("_postProcessingStale");
            Click(rows.GetVisualDescendants().OfType<Button>().Single(button => Equals(button.Content,"Discard stale input")));

            Input().Text = "2.5";
            var currentDraft = Field<PostProcessingDraft>("_postProcessingDraft");
            const BindingFlags local = BindingFlags.Instance | BindingFlags.NonPublic;
            typeof(MainWindow).GetField("_postProcessingPending", local)!.SetValue(window, expired);
            typeof(MainWindow).GetMethod("SettlePostProcessing", local)!.Invoke(window,
                [Field<EditorWorker>("_worker"), expired, new BatchReplayResult(BatchReplayState.Committed, "Delayed A settlement")]);
            Require(ReferenceEquals(currentDraft, Field<PostProcessingDraft>("_postProcessingDraft")) && Input().Text == "2.5",
                "delivered settlement for the retired camera leaves the new camera draft intact");
            Click(Set());
            await NameUiWait(() => Value(b) == 2.5f && Field<Button>("_undo").IsEnabled && Field<PostProcessingDraft>("_postProcessingPending") is null,
                "verified captured-camera edit creates one Undo");
            Activate(a);
            await NameUiWait(() => View()?.Camera == a, "new active camera read before Undo");
            Click(Field<Button>("_undo"));
            await NameUiWait(() => Value(b) == second && Field<Button>("_redo").IsEnabled, "Undo addressed inactive B");
            Click(Field<Button>("_redo"));
            await NameUiWait(() => Value(b) == 2.5f && Field<Button>("_undo").IsEnabled, "Redo addressed inactive B");
            await NameUiWait(() => View()?.Camera == a, "panel settled on the current camera after Redo");
            Require(Value(a) == first && peer.Authoring.ActiveCamera().Value == a, "Undo and Redo preserve current active camera A");
        }

        static void Require(bool ok, string name)
        {
            if (!ok) throw new InvalidOperationException(name);
            Console.WriteLine("PASS " + name);
        }
    }
}
