using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Numerics;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Interactivity;
using Avalonia.Themes.Fluent;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static int RunSceneObjectUi(string executable, string scene, bool dropReply)
    {
        string work = RuntimeProcess.NewWorkDirectory();
        using var runtime = RuntimeProcess.Start(new RuntimeLaunch(Path.GetFullPath(executable), scene, null, work));
        SessionInfo host = runtime.WaitForSession(TimeSpan.FromSeconds(25));
        using var observer = RemoteSession.Connect(host);
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        string attach = Path.Combine(work, "editor-relay.json");
        File.WriteAllText(attach, JsonSerializer.Serialize(new { protocol = SessionFile.Protocol,
            port = ((IPEndPoint)listener.LocalEndpoint).Port, token = host.Token, pid = host.Pid, scene = host.Scene.ToString() }));
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(45));
        var held = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        int submits = 0, applies = 0;
        Task relay = Relay();
        try
        {
            return AppBuilder.Configure(() => new SceneObjectUiApp(executable, scene, attach, CheckCreation))
                .UsePlatformDetect().StartWithClassicDesktopLifetime([]);
        }
        finally
        {
            deadline.Cancel();
            try { relay.GetAwaiter().GetResult(); }
            catch (OperationCanceledException) when (deadline.IsCancellationRequested) { }
            finally
            {
                File.Delete(attach);
                runtime.CloseMainWindow();
                runtime.WaitForExit(TimeSpan.FromSeconds(8));
            }
        }

        async Task Relay()
        {
            CancellationToken token = deadline.Token;
            using var editor = await listener.AcceptTcpClientAsync(token);
            using var backend = new TcpClient();
            await backend.ConnectAsync(IPAddress.Loopback, host.Port, token);
            using var editorStream = editor.GetStream();
            using var backendStream = backend.GetStream();
            using var input = new StreamReader(editorStream);
            using var output = new StreamWriter(editorStream) { AutoFlush = true, NewLine = "\n" };
            using var forward = new StreamWriter(backendStream) { AutoFlush = true, NewLine = "\n" };
            using var read = new StreamReader(backendStream);
            while (await input.ReadLineAsync(token) is string request)
            {
                string method = Json.Parse(request).Find("method")!.AsString;
                await forward.WriteLineAsync(request.AsMemory(), token);
                string reply = await read.ReadLineAsync(token) ?? throw new IOException("Runtime closed the relay");
                if (method == "authoring.submitTransaction") Interlocked.Increment(ref submits);
                if (method == "authoring.applyTracked" && Interlocked.Increment(ref applies) == 1)
                {
                    held.SetResult();
                    await release.Task.WaitAsync(token);
                    if (dropReply) return;
                }
                await output.WriteLineAsync(reply.AsMemory(), token);
            }
        }

        async Task CheckCreation(MainWindow window)
        {
            var button = NameUiField<Button>(window, "_create")!;
            var item = NameUiField<Dictionary<SceneObjectPreset, MenuItem>>(window, "_createItems")![SceneObjectPreset.Empty];
            var selection = NameUiField<Selection>(window, "_selection")!;
            var feedback = NameUiField<TextBlock>(window, "_editLine")!;
            var projection = NameUiField<TextBlock>(window, "_applyLine")!;
            var connection = NameUiField<TextBlock>(window, "_connectionLine")!;
            var undo = NameUiField<Button>(window, "_undo")!;
            await NameUiWait(() => button.IsEnabled && item.IsEnabled, "Create is ready");
            var before = observer.Authoring.ListNodes().Value.Select(node => node.Node).ToHashSet();
            item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            await held.Task.WaitAsync(deadline.Token);
            await NameUiWait(() => feedback.Text?.Contains("committed for", StringComparison.Ordinal) == true,
                "known commit feedback appears before the Apply reply");
            EntityGuid created = observer.Authoring.ListNodes().Value.Single(node => !before.Contains(node.Node)).Node;
            string committed = feedback.Text!;
            bool busy = !button.IsEnabled && !item.IsEnabled;
            item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            if (!busy || feedback.Text != committed || !committed.Contains(created.ToString(), StringComparison.Ordinal) || !undo.IsEnabled)
                throw new InvalidOperationException("Create must remain busy and preserve the original commit and undo entry while Apply is held. " + feedback.Text);
            release.SetResult();
            if (dropReply)
            {
                await NameUiWait(() => projection.Text?.Contains("unknown completion", StringComparison.Ordinal) == true &&
                    connection.Text?.StartsWith("session failed", StringComparison.Ordinal) == true,
                    "lost Apply reply is reported independently of commit");
                if (feedback.Text?.Contains(created + ". Apply outcome unknown.", StringComparison.Ordinal) != true)
                    throw new InvalidOperationException("Lost Apply obscured the committed creation: " + feedback.Text);
            }
            else
            {
                await NameUiWait(() => button.IsEnabled && selection.Node == created &&
                    projection.Text?.StartsWith("apply: applied", StringComparison.Ordinal) == true,
                    "Create re-enables after projection settles and selects the committed node");
                if (feedback.Text != committed) throw new InvalidOperationException("Final creation feedback changed: " + feedback.Text);
            }
            if (Volatile.Read(ref submits) != 1 || Volatile.Read(ref applies) != 1 ||
                observer.Authoring.ListNodes().Value.Count != before.Count + 1 || !undo.IsEnabled)
                throw new InvalidOperationException("Expected one transaction, one Apply, and one undo entry");
            Console.WriteLine(dropReply ? "PASS held and lost Apply reply preserves known creation"
                                        : "PASS held Apply reply preserves creation feedback and selection");
        }
    }

    private sealed class SceneObjectUiApp(string executable, string scene, string attach, Func<MainWindow, Task> check) : Application
    {
        public override void Initialize() => Styles.Add(new FluentTheme());

        public override void OnFrameworkInitializationCompleted()
        {
            var desktop = (IClassicDesktopStyleApplicationLifetime)ApplicationLifetime!;
            var window = new MainWindow(new EditorOptions(executable, scene) { AttachSession = attach });
            desktop.MainWindow = window;
            window.Opened += async (_, _) =>
            {
                int exit = 1;
                try { await check(window); exit = 0; }
                catch (Exception error) { Console.Error.WriteLine(error); }
                finally { desktop.Shutdown(exit); }
            };
            base.OnFrameworkInitializationCompleted();
        }
    }

    private static void SceneObjectCreationChecks(string conformance)
    {
        // Use the native-generated authoring schema rather than a hand-mirrored descriptor table.
        var schema = Codec.DecodeSchema(File.ReadLines(Path.Combine(conformance, "codec_vectors.jsonl"))
            .Where(line => line.Contains("\"case\":\"schema.scene\"", StringComparison.Ordinal))
            .Select(line => Json.Parse(line).Find("wire")!).Single())!;
        var node = FakeDocument.Guid(10);
        var parent = FakeDocument.Guid(1);
        foreach (var preset in Enum.GetValues<SceneObjectPreset>())
        {
            bool child = preset == SceneObjectPreset.EmptyChild;
            var plan = SceneObjectPresets.Build(schema, preset, node, 4, child ? parent : null)!;
            Check(plan.Edit.Commands.OfType<CreateNode>().Single() is { Node: var id, Position: 4 } && id == node &&
                plan.Expected.Parent == (child ? parent : (EntityGuid?)null) &&
                plan.Edit.Commands.OfType<SetParent>().Count() == (child ? 1 : 0) &&
                !plan.Edit.Commands.Any(c => c is SetActiveCamera) &&
                plan.Edit.Commands.OfType<SetNodeName>().Count() == (SceneObjectPresets.Named(preset) ? 1 : 0),
                "preset composes one node and no active-camera write: " + preset);
        }
        var camera = SceneObjectPresets.Build(schema, SceneObjectPreset.Camera, node, 4, null)!;
        PropertyValue Camera(string field) => camera.Expected.Components.SelectMany(c => c.Values)
            .Single(v => v.Address == SceneSchema.AddressOf(schema, node, "Camera", field)).Value;
        Check(Camera("fovYRadians").AsFloat32() == MathF.PI / 3 && Camera("nearZ").AsFloat32() == .1f &&
            Camera("farZ").AsFloat32() == 100 && Camera("exposureCompensationEv").AsFloat32() == 0 &&
            camera.Expected.Components.Sum(c => c.Values.Count) == 14, "camera sets a valid lens and retains the nested defaults");
        var directional = SceneObjectPresets.Build(schema, SceneObjectPreset.DirectionalLight, node, 4, null)!;
        var point = SceneObjectPresets.Build(schema, SceneObjectPreset.PointLight, node, 4, null)!;
        var angle = SceneSchema.AddressOf(schema, node, "Transform", "localEulerAnglesRadians");
        var range = SceneSchema.AddressOf(schema, node, "Light", "range");
        Check(directional.Edit.Commands.OfType<SetProperty>().Any(s => s.Address == angle && s.Value.AsVec3() == new Vector3(-MathF.PI/4, 0, 0)) &&
              point.Edit.Commands.OfType<SetProperty>().Any(s => s.Address == range && s.Value.AsFloat32() == 10),
            "directional and point presets supply a usable angle or positive range");
        var mesh = SceneObjectPresets.Mesh(SceneObjectPreset.Cube)!.Value;
        var cube = SceneObjectPresets.Build(schema, SceneObjectPreset.Cube, node, 4, null)!;
        Check(cube.Edit.Commands.OfType<SetProperty>().Single().Value.AsAsset() == mesh &&
              cube.Expected.Components.SelectMany(c => c.Values).Any(v => v.Value == PropertyValue.Absent),
            "shape binds its stock mesh and leaves material absent");
        Check(!SceneObjectPresets.MatchesMesh(mesh, null) && SceneObjectPresets.Build([], SceneObjectPreset.Camera, node, 4, null) is null &&
            SceneObjectPresets.Build(schema, SceneObjectPreset.EmptyChild, node, 4, null) is null, "missing catalog/schema or parent fails closed");
        var detail = new AssetDetails(mesh, AssetType.Mesh, "fixture", 1, null, "fixture", []);
        Check(SceneObjectPresets.MatchesMesh(mesh, detail) &&
              !SceneObjectPresets.MatchesMesh(mesh, detail with { Type = AssetType.Material }) &&
              !SceneObjectPresets.MatchesMesh(SceneObjectPresets.Mesh(SceneObjectPreset.Plane)!.Value, detail),
              "stock Mesh eligibility checks both the catalog GUID and type");
        var sameLabel = SceneObjectPresets.Build(schema, SceneObjectPreset.Cube, parent, 5, null)!;
        Check(sameLabel.Expected.Name == cube.Expected.Name && sameLabel.Expected.Node != cube.Expected.Node &&
              sameLabel.Expected.Parent is null, "repeated named presets preserve the label and use distinct root identities");
        Check(SceneObjectPresets.Available(SceneObjectPreset.Cube, true, 2) &&
             !SceneObjectPresets.Available(SceneObjectPreset.Cube, false, 0) &&
             SceneObjectPresets.Available(SceneObjectPreset.EmptyChild, false, 1) &&
             SceneObjectPresets.Available(SceneObjectPreset.UnnamedRoot, false, 0) &&
             !SceneObjectPresets.Available(SceneObjectPreset.EmptyChild, true, 2) &&
             !SceneObjectPresets.Available(SceneObjectPreset.UnnamedRoot, true, 1), "named and legacy menu eligibility follows document and selection");

        var doc = new FakeDocument();
        var apply = new ApplyController(doc);
        var target = new AuthoringEditTarget(doc, new DocumentTree(), apply);
        var history = new EditHistory(target);
        var life = new NodeLife(cube.Expected, true) { Preset = SceneObjectPreset.Cube };
        history.Record(new EditTransaction("Create Cube", [], [life]), target.Committed([]));
        history.Update();
        Check(history.Undo(preset: (entry, forward) => entry == life && !forward
                ? new(BatchReplayState.Committed, "Undo settled") : new(BatchReplayState.Drop, "wrong operation")) == HistoryResult.Submitted &&
             history.CanRedo && doc.Submissions == 0 && doc.Applies == 0,
             "preset Undo settles through its callback, not the pipelined legacy history path");
        Check(history.Redo(preset: (_, _) => new(BatchReplayState.Retry, "not sent")) == HistoryResult.Waiting && history.CanRedo,
             "pre-send replay refusal keeps the retryable entry");
        Check(history.Redo(preset: (_, _) => new(BatchReplayState.Drop, "outcome unknown")) == HistoryResult.ChangedElsewhere &&
             !history.CanRedo && !history.CanUndo && doc.Submissions == 0 && doc.Applies == 0,
             "unverified replay never moves history or automatically applies");
    }
}
