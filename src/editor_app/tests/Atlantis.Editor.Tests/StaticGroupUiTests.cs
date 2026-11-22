using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Numerics;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static int RunStaticGroupReceipt(string executable, string scene)
    {
        string work = RuntimeProcess.NewWorkDirectory();
        using var runtime = RuntimeProcess.Start(new RuntimeLaunch(Path.GetFullPath(executable), scene, null, work));
        SessionInfo host = runtime.WaitForSession(TimeSpan.FromSeconds(25));
        using var observer = RemoteSession.Connect(host);
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        string attach = Path.Combine(work, "static-group-relay.json");
        File.WriteAllText(attach, System.Text.Json.JsonSerializer.Serialize(new {
            protocol = SessionFile.Protocol, port = ((IPEndPoint)listener.LocalEndpoint).Port,
            token = host.Token, pid = host.Pid, scene = host.Scene.ToString()
        }));
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(55));
        var acceptedRoot = new TaskCompletionSource<EntityGuid>(TaskCreationOptions.RunContinuationsAsynchronously);
        int applies = 0;
        Task relay = Relay();
        try
        {
            return AppBuilder.Configure(() => new SceneObjectUiApp(executable, scene, attach, CheckReceipt))
                .UsePlatformDetect().StartWithClassicDesktopLifetime([]);
        }
        finally
        {
            deadline.Cancel();
            try { relay.GetAwaiter().GetResult(); }
            catch (OperationCanceledException) when (deadline.IsCancellationRequested) { }
            finally
            {
                runtime.CloseMainWindow();
                runtime.WaitForExit(TimeSpan.FromSeconds(8));
            }
        }

        async Task Relay()
        {
            var token = deadline.Token;
            using var editor = await listener.AcceptTcpClientAsync(token);
            using var backend = new TcpClient();
            await backend.ConnectAsync(IPAddress.Loopback, host.Port, token);
            using var input = new StreamReader(editor.GetStream());
            using var output = new StreamWriter(editor.GetStream()) { AutoFlush = true, NewLine = "\n" };
            using var forward = new StreamWriter(backend.GetStream()) { AutoFlush = true, NewLine = "\n" };
            using var read = new StreamReader(backend.GetStream());
            while (await input.ReadLineAsync(token) is string text)
            {
                JsonValue request = Json.Parse(text);
                string method = request.Find("method")!.AsString;
                if (method is "authoring.apply" or "authoring.applyTracked") Interlocked.Increment(ref applies);
                await forward.WriteLineAsync(text.AsMemory(), token);
                string reply = await read.ReadLineAsync(token) ?? throw new IOException("Runtime closed the relay");
                if (method == "authoring.submitTransaction")
                {
                    string root = request.Find("params")!.Find("commands")!.Items
                        .First(c => c.Find("kind")?.AsString == "CreateNode").Find("node")!.AsString;
                    var ticket = Codec.DecodeTransactionTicket(Json.Parse(reply).Find("result")!);
                    if (ticket is null) throw new InvalidOperationException("Expected a real transaction receipt.");
                    acceptedRoot.SetResult(EntityGuid.Parse(root));
                    return; // Close the Editor connection without forwarding the receipt.
                }
                await output.WriteLineAsync(reply.AsMemory(), token);
            }
        }

        async Task CheckReceipt(MainWindow window)
        {
            T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
            Field<TabControl>("_assetTabs").SelectedIndex = 1;
            var list = Field<ListBox>("_assetsList");
            const string storefront = "content:managed_storefront/source.gltf#scene";
            await NameUiWait(() => list.Items.OfType<AssetSummary>().Any(a => a.Source == storefront), "storefront row ready");
            list.SelectedItem = list.Items.OfType<AssetSummary>().Single(a => a.Source == storefront);
            var add = Field<Button>("_addGroup");
            await NameUiWait(() => add.IsVisible && add.IsEnabled, "group add ready");
            var before = observer.Authoring.ApplyStatus().Value!;
            add.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var root = await acceptedRoot.Task.WaitAsync(TimeSpan.FromSeconds(15));
            await NameUiWait(() => !Field<AssetBrowser>("_assetBrowser").SelectionVisible, "Editor observes the disconnect");
            string notice = Field<TextBlock>("_addMeshState").Text ?? "";
            if (!notice.Contains("outcome unknown", StringComparison.Ordinal) ||
                !notice.Contains(root.ToString(), StringComparison.Ordinal) ||
                !notice.Contains("Inspect the document and confirm the prior request ended before retrying", StringComparison.Ordinal))
                throw new InvalidOperationException("Lost Add receipt hid the settlement: " + notice);
            await NameUiWait(() => observer.Authoring.ListNodes().Value!.Any(node => node.Node == root),
                "Runtime finishes processing the accepted ticket");
            var after = observer.Authoring.ApplyStatus().Value!;
            if (Field<Button>("_undo").IsEnabled || Volatile.Read(ref applies) != 0 ||
                !Equals(after.Request, before.Request) ||
                before.CurrentDocumentRevision is not { } revision || after.CurrentDocumentRevision != revision + 1)
                throw new InvalidOperationException("Expected one committed revision, zero Apply requests, and no undo entry.");
            Console.WriteLine(notice);
        }
    }

    private static async Task CheckStaticGroupUi(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        const BindingFlags hidden = BindingFlags.Instance | BindingFlags.NonPublic;
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        static string Setting(string name) => Environment.GetEnvironmentVariable("ATLANTIS_0130_" + name)
            ?? throw new InvalidOperationException("Missing test setting " + name);
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        void Verify(string name, bool ok, string detail = "")
        {
            checks.Add(new(name, ok, detail));
            if (!ok) throw new InvalidOperationException(name + " " + detail);
        }
        string phase = Setting("PHASE");
        string evidence = Setting("EVIDENCE");
        Field<TabControl>("_assetTabs").SelectedIndex = 1;
        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } runtime && File.Exists(runtime.SessionPath) &&
            Field<AssetBrowser>("_assetBrowser").State == "Ready" && Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count > 0,
            "hosted storefront test Runtime", 120);
        var runtime = Field<RuntimeProcess>("_runtime");
        work(runtime.Launch.WorkDirectory);
        var viewport = Field<ViewportHost>("_viewport").Window!;
        nint hwnd = window.TryGetPlatformHandle()!.Handle;
        async Task DeferFixtureRecovery()
        {
            // Previous disposable attempts may leave a genuine recovery copy.
            // Keep that copy and restore the test window's ordinary interaction.
            if (Field<Window>("_recoveryWindow") is { } recovery)
            {
                Click(recovery.GetVisualDescendants().OfType<Button>().Single(b => Equals(b.Content, "Decide Later")));
                await NameUiWait(() => Field<Window>("_recoveryWindow") is null, "fixture recovery dialog closed");
            }
        }
        await DeferFixtureRecovery();
        window.Activate();
        await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground storefront Editor");
        Verify("Windows 100% and embedded Runtime", GetDpiForWindow(hwnd) == 96 && viewport.RuntimeChildIsHosted());
        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));

        async Task Capture(string name)
        {
            await DeferFixtureRecovery();
            window.Activate();
            await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground for " + name);
            await Task.Delay(250);
            var start = new ProcessStartInfo("py") { UseShellExecute = false, CreateNoWindow = true,
                RedirectStandardOutput = true, RedirectStandardError = true };
            foreach (string arg in new[] { "-3", Setting("CAPTURE"), Environment.ProcessId.ToString(CultureInfo.InvariantCulture),
                Path.Combine(evidence, name + ".png"), window.Title ?? "" }) start.ArgumentList.Add(arg);
            try
            {
                string captured = await Task.Run(() =>
                {
                    using var capture = Process.Start(start)!;
                    var stdout = capture.StandardOutput.ReadToEndAsync();
                    var stderr = capture.StandardError.ReadToEndAsync();
                    if (!capture.WaitForExit(15000) || capture.ExitCode != 0)
                        throw new InvalidOperationException(stderr.IsCompletedSuccessfully ? stderr.Result : "Capture timed out.");
                    return stdout.Result;
                });
                File.WriteAllText(Path.Combine(evidence, name + "-capture.txt"), captured);
                checks.Add(new("Native capture " + name, true, captured));
            }
            catch (InvalidOperationException error)
            {
                checks.Add(new("Native capture " + name, false, error.Message));
            }
        }

        var roster = Field<AssetBrowser>("_assetBrowser");
        string storefront = "content:managed_storefront/source.gltf#scene";
        if (phase == "publish")
        {
            var initial = peer.Assets.ListAssets().Value!;
            Verify("the seed catalog has no storefront", initial.All(a => a.Source != storefront));
            foreach (string name in new[] { "invalid_storefront", "storefront" })
            {
                Field<TextBox>("_importName").Text = name;
                await NameUiWait(() => Field<Button>("_importRegister").IsEnabled, "Register action enabled");
                Click(Field<Button>("_importRegister"));
                ImportJob? job = null;
                await NameUiWait(() => (job = peer.Assets.ImportStatus().Value?.Job) is { State: not RebuildState.Running } &&
                    job.Name == name && Field<Button>("_importRegister").IsEnabled, "import settles for " + name, 135);
                string notice = Field<TextBlock>("_importState").Text ?? "";
                if (name == "invalid_storefront")
                    Verify("invalid source fails before registration and publication", job?.Failure?.Registration == RegistrationState.NotCommitted &&
                        job.RootGuid is null && job.Generation is null, notice);
                else
                {
                    Verify("valid storefront registers and publishes", job is { State: RebuildState.Succeeded, Generation: not null,
                        RootGuid: not null }, notice);
                    File.WriteAllText(Path.Combine(evidence, "generation.txt"), job!.Generation!.Value.ToString());
                    File.WriteAllText(Path.Combine(evidence, "registered-root.txt"), job.RootGuid!.Value.ToString());
                }
            }
            Verify("publication leaves the running catalog snapshot unchanged", peer.Assets.ListAssets().Value!.SequenceEqual(initial));
            return;
        }

        var scene = roster.Items.Single(a => a.Source == storefront && a.Type == AssetType.Scene);
        var group = peer.Assets.StaticGroup(scene.Guid).Value!;
        if (phase == "reimport")
        {
            var reviewSearch = Field<TextBox>("_assetSearch");
            var list = Field<ListBox>("_assetsList");
            var reviewButton = Field<Button>("_reviewStaticGroupButton");
            reviewSearch.Text = scene.Guid.ToString();
            await NameUiWait(() => list.Items.OfType<AssetSummary>().Any(a => a.Guid == scene.Guid), "storefront visible");
            list.SelectedItem = list.Items.OfType<AssetSummary>().Single(a => a.Guid == scene.Guid);
            await NameUiWait(() => reviewButton.IsEnabled, "review button ready");
            var inbox = Path.Combine(Setting("PROJECT"), "inbox/storefront.gltf");
            var registration = Path.Combine(Setting("PROJECT"), "registered/managed_storefront/source.gltf");
            byte[] original = File.ReadAllBytes(registration);
            var beforeReview = peer.Authoring.DocumentState().Value!;
            foreach (var (name, state) in new[] { ("missing", "Blocked"), ("ambiguous", "Blocked"), ("updated", "Ready") })
            {
                File.Copy(Path.Combine(evidence, name + ".gltf"), inbox, true);
                Click(reviewButton);
                await NameUiWait(() => Field<StaticGroupReimportWindow>("_staticGroupReviewWindow") is { IsVisible: true }, "review window opens");
                var dialog = Field<StaticGroupReimportWindow>("_staticGroupReviewWindow");
                var confirm = dialog.GetVisualDescendants().OfType<Button>().Single(button => Equals(button.Content, "Confirm reimport"));
                var status = (TextBlock)typeof(StaticGroupReimportWindow).GetField("_status", hidden)!.GetValue(dialog)!;
                await NameUiWait(() => state == "Ready" ? confirm.IsEnabled :
                    status.Text?.StartsWith(state + ".", StringComparison.Ordinal) == true, "review settles for " + name);
                Verify("review confirmation state for " + name, confirm.IsEnabled == (state == "Ready"), status.Text ?? "");
                Verify("native review dialog DPI", GetDpiForWindow(dialog.TryGetPlatformHandle()!.Handle) == 96);
                await NativeCapture(dialog, Path.Combine(evidence, "review-" + name + ".png"));
                Click(dialog.GetVisualDescendants().OfType<Button>().Single(button => Equals(button.Content, "Cancel")));
                await NameUiWait(() => Field<StaticGroupReimportWindow>("_staticGroupReviewWindow") is null, "review closes on Cancel");
            }
            Verify("reviews preserve selection, registration and document state",
                roster.Selected == scene.Guid && original.AsSpan().SequenceEqual(File.ReadAllBytes(registration)) &&
                beforeReview == peer.Authoring.DocumentState().Value);
            await Capture("review-closed");
            return;
        }
        if (phase is "reimport-place" or "reimport-reopen" or "reimport-cold")
        {
            await CheckPlacedReimport();
            return;
        }
        async Task CheckPlacedReimport()
        {
            var edit = new RemoteAuthoringDocument(peer.Authoring, peer);
            var before = edit.ListNodes().Select(n => n.Node).ToHashSet();
            EntityGuid[] placed;
            string prefix = phase;
            var evidenceFile = Path.Combine(evidence, "reimport-placement.json");
            if (phase == "reimport-place")
            {
                string source = Path.GetFullPath(Setting("DISPOSABLE_SCENE"));
                Verify("named Runtime uses the designated disposable scene",
                    Path.GetFullPath(edit.Info()!.SourcePath).Equals(source, StringComparison.OrdinalIgnoreCase), source);
                var assetsList = Field<ListBox>("_assetsList");
                Field<TextBox>("_assetSearch").Text = scene.Guid.ToString();
                await NameUiWait(() => assetsList.Items.OfType<AssetSummary>().Any(a => a.Guid == scene.Guid), "storefront visible");
                assetsList.SelectedItem = assetsList.Items.OfType<AssetSummary>().Single(a => a.Guid == scene.Guid);
                var addGroup = Field<Button>("_addGroup");
                await NameUiWait(() => addGroup.IsVisible && addGroup.IsEnabled, "group Add enabled");
                Click(addGroup);
                await NameUiWait(() => edit.ListNodes().Count == before.Count + group.Nodes.Count + 1 &&
                    edit.ApplyStatus().Value?.Request?.State == ApplyState.Applied, "group committed and applied");
                placed = edit.ListNodes().Where(n => !before.Contains(n.Node)).Select(n => n.Node).ToArray();
                var awningMesh = group.Nodes.Last(n => n.Renderable is not null).Renderable!.Mesh;
                var awning = placed.Single(id => Binding(id, "meshAsset") == awningMesh);
                var originalMaterial = Binding(awning, "materialAsset");
                var replacement = group.Nodes.Select(n => n.Renderable?.Material).OfType<AssetGuid>()
                    .First(material => material != originalMaterial);
                var position = SceneSchema.AddressOf(edit.Schema, placed[0], "Transform", "localPosition")!.Value;
                var material = SceneSchema.AddressOf(edit.Schema, awning, "Renderable", "materialAsset")!.Value;
                ulong oldRevision = edit.ApplyStatus().Value!.CurrentDocumentRevision!.Value;
                edit.SubmitTransaction([new Atlantis.Gameplay.Authoring.SetProperty(position,
                    PropertyValue.FromVec3(new Vector3(-12, 0.25f, 12.1f))),
                    new Atlantis.Gameplay.Authoring.SetProperty(material, PropertyValue.FromAsset(replacement))]);
                await NameUiWait(() => edit.ApplyStatus().Value?.CurrentDocumentRevision > oldRevision, "placement and override settle");
                Verify("placement and awning override commit as requested",
                    TransformMath.LocalOf(edit, placed[0]).Position == new Vector3(-12, 0.25f, 12.1f) &&
                    Binding(awning, "materialAsset") == replacement);
                Verify("explicit Apply admits the placed projection", peer.Authoring.ApplyTracked().IsOk);
                await NameUiWait(() => edit.ApplyStatus().Value?.Request?.State == ApplyState.Applied, "override projected");
                var worker = Field<EditorWorker>("_worker");
                var reviewed = await worker.ReviewDocument();
                var saved = await worker.SaveOrReopen(reviewed.State!, saving: true);
                Verify("original hand-authored source is saved", saved.State is { Dirty: false });
                var copy = peer.Authoring.SaveAs("storefront_0139");
                Verify("existing Save As produces the independent 0090 source", copy.IsOk, copy.IsOk ? copy.Value.Source : copy.Error.ToString());
                File.WriteAllText(Path.Combine(evidence, "reimport-placed-source.txt"), copy.Value.Source);
                File.WriteAllText(Path.Combine(evidence, "reimport-placed-ids.txt"), string.Join("\n", placed.Select(id => id.ToString())));
                File.WriteAllText(evidenceFile, Fingerprint(placed));
            }
            else
            {
                placed = File.ReadAllLines(Path.Combine(evidence, "reimport-placed-ids.txt")).Select(EntityGuid.Parse).ToArray();
                Verify("source has the same node GUIDs, hierarchy, placement and material overrides",
                    Fingerprint(placed) == File.ReadAllText(evidenceFile), edit.Info()!.SourcePath);
                if (phase == "reimport-reopen")
                {
                    Verify("upgraded named generation exposes the new sign without writing to the placed group",
                        runtime.Launch.AssetGeneration is not null && group.Nodes.Count > placed.Length - 1 &&
                        !placed.Any(id => Binding(id, "meshAsset") == group.Nodes.Last().Renderable?.Mesh));
                    Verify("applying the reopened document is explicit", peer.Authoring.ApplyTracked().IsOk);
                    await NameUiWait(() => edit.ApplyStatus().Value?.Request?.State == ApplyState.Applied, "active generation displays saved placement");
                }
            }
            var view = BistroReferenceView.Terrace;
            var pose = view.Pose();
            window.Width = 1650;
            window.Height = 910;
            Field<ViewportHost>("_viewport").SetPhysicalSize((BistroReferenceView.Width, BistroReferenceView.Height), window.RenderScaling);
            await NameUiWait(() => viewport.Width == BistroReferenceView.Width && viewport.Height == BistroReferenceView.Height,
                "physical reference viewport size");
            peer.Control.SetViewportCamera(new ViewportCamera(pose.WorldMatrix, pose.Lens.FovYRadians, pose.Lens.NearZ, pose.Lens.FarZ));
            try
            {
                var report = peer.Control.Step(new StepRequest(2, Path.Combine(evidence, prefix + "-fixed.png"))).Report;
                Verify("960 by 540 capture from the fixed Bistro terrace pose", report?.Image is { Width: 960, Height: 540 } &&
                    view.Matches(report.Data.View, report.Data.Projection));
                File.WriteAllText(Path.Combine(evidence, prefix + "-frame.json"), System.Text.Json.JsonSerializer.Serialize(report));
                await Capture(prefix + "-window");
            }
            finally { peer.Control.SetViewportCamera(null); }

            string Fingerprint(EntityGuid[] subject)
            {
                var tree = new DocumentTree();
                tree.Rebuild(edit);
                return string.Join("\n", subject.Select(id => FormattableString.Invariant(
                    $"{id} {tree.ParentOf(id)} {TransformMath.LocalOf(edit, id)} ") +
                    string.Join(",", TransformMath.WorldOf(edit, tree, id).Select(x => x.ToString("R", CultureInfo.InvariantCulture))) +
                    $" {Binding(id, "meshAsset")} {Binding(id, "materialAsset")}"));
            }

            AssetGuid? Binding(EntityGuid id, string key) =>
                SceneSchema.AddressOf(edit.Schema, id, "Renderable", key) is SceneAddress address &&
                edit.GetProperty(address) is { IsAbsent: false } value ? value.AsAsset() : null;
        }
        Verify("shared cooked projection retains four authored nodes and three material bindings", group.Nodes.Count == 4 &&
            group.Nodes[0].Parent is null && group.Nodes[1].Parent == 0 && group.Nodes[2].Parent == 1 &&
            group.Nodes.Count(n => n.Renderable is not null) == 3 &&
            group.Nodes.Where(n => n.Renderable is not null).Select(n => n.Renderable!.Material).Distinct().Count() == 3,
            scene.Guid.ToString());
        const string descriptor = "group-projection.txt";
        string groupIdentity = string.Join('\n', group.Nodes.Select(n =>
            FormattableString.Invariant($"{n.Parent} {n.Position} {n.EulerRadians} {n.Scale} {n.Renderable}")));
        if (phase == "activate")
        {
            File.WriteAllText(Path.Combine(evidence, descriptor), scene.Guid + "\n" + groupIdentity);
            Verify("the group is now present in explicitly activated named generation", runtime.Launch.AssetGeneration is not null);
            return;
        }
        Verify("0090 build produces the same group IDs, order, values and bindings",
            File.ReadAllText(Path.Combine(evidence, descriptor)) == scene.Guid + "\n" + groupIdentity &&
            runtime.Launch.AssetGeneration is null);
        var document = new RemoteAuthoringDocument(peer.Authoring, peer);
        var info = document.Info()!;
        string source = Path.GetFullPath(Setting("DISPOSABLE_SCENE"));
        Verify("test writes only its designated hand-authored saved scene", info.Provenance == Provenance.HandAuthored &&
            string.Equals(Path.GetFullPath(info.SourcePath), source, StringComparison.OrdinalIgnoreCase));
        var idsFile = Path.Combine(evidence, "instance.txt");
        string Fingerprint() => string.Join('\n', document.ListNodes().Select(n =>
            FormattableString.Invariant($"{n.Node} {n.Parent} {n.Name} {TransformMath.LocalOf(document, n.Node)} ") +
            string.Join(" ", new[] { "meshAsset", "materialAsset" }.Select(field =>
                SceneSchema.AddressOf(document.Schema, n.Node, "Renderable", field) is SceneAddress address
                    ? document.GetProperty(address)?.ToString() : ""))));
        if (phase == "cold")
        {
            Verify("independent Saved Scenes Editor reads the exact saved nodes and bindings",
                Fingerprint() == File.ReadAllText(idsFile), info.SourcePath);
            await Capture("storefront-cold-open");
            return;
        }
        var worker = Field<EditorWorker>("_worker");
        async Task Reopen(string expected)
        {
            Verify("saved values match before reopen", Fingerprint() == expected);
            var reviewed = await worker.ReviewDocument();
            Verify("reopen targets the clean saved document", reviewed.Clean && reviewed.State is not null, reviewed.Title);
            long opened = worker.MeshDocumentLifetime;
            var open = await worker.SaveOrReopen(reviewed.State, saving: false);
            Verify("shared guarded reopen accepts the same saved source", open.State?.Source == reviewed.State?.Source,
                open.Diagnostic ?? open.Refusal?.ToString() ?? "");
            await NameUiWait(() => worker.MeshDocumentLifetime != opened, "the reopened document gets a new lifetime");
            Verify("reopen preserves all identities, transforms and references", Fingerprint() == expected);
            ulong beforeReopenApply = peer.Control.Status().SceneGeneration;
            await NameUiWait(() => Field<Button>("_retryApply").IsEnabled, "explicit Apply is available after reopen");
            Click(Field<Button>("_retryApply"));
            await NameUiWait(() => peer.Control.Status().SceneGeneration > beforeReopenApply, "reopened scene is shown");
            await Capture("storefront-reopened");
        }
        if (phase == "reopen")
        {
            await Reopen(File.ReadAllText(idsFile));
            return;
        }
        if (phase != "authored") throw new InvalidOperationException("Unknown storefront phase.");

        var baseline = document.ListNodes().ToArray();
        ulong revision = document.ApplyStatus().Value!.CurrentDocumentRevision!.Value;
        var add = Field<Button>("_addGroup");
        var search = Field<TextBox>("_assetSearch");
        var assets = Field<ListBox>("_assetsList");
        async Task SelectAsset()
        {
            search.Text = scene.Guid.ToString();
            await NameUiWait(() => assets.Items.OfType<AssetSummary>().Any(a => a.Guid == scene.Guid), "group row is visible");
            assets.SelectedItem = assets.Items.OfType<AssetSummary>().Single(a => a.Guid == scene.Guid);
            await NameUiWait(() => add.IsVisible && add.IsEnabled, "verified Add Group action");
        }
        await SelectAsset();
        using (var held = new ManualResetEventSlim())
        {
            bool entered = false;
            typeof(EditorWorker).GetMethod("Enqueue", hidden)!.Invoke(worker, new object[] { (Action<RemoteSession>)(_ =>
            {
                Volatile.Write(ref entered, true);
                held.Wait(TimeSpan.FromSeconds(12));
            }) });
            try
            {
                await NameUiWait(() => Volatile.Read(ref entered), "controlled worker queue");
                Click(add);
                search.Text = "hidden-storefront-test";
                await NameUiWait(() => !roster.SelectionVisible, "asset selection hidden before send");
            }
            finally { held.Set(); }
            await NameUiWait(() => Field<TextBlock>("_addMeshState").Text?.Contains("Add refused", StringComparison.Ordinal) == true,
                "stale Add settled");
        }
        Verify("stale Add creates no nodes, history or Apply", document.ListNodes().SequenceEqual(baseline) &&
            document.ApplyStatus().Value?.CurrentDocumentRevision == revision && !Field<Button>("_undo").IsEnabled);

        await SelectAsset();
        var selectedMaterial = group.Nodes.Select(n => n.Renderable?.Material).OfType<AssetGuid>().First();
        var buildRoot = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(runtime.Launch.RuntimePath)!, "..", "..", ".."));
        string materialRecord = File.ReadLines(Path.Combine(buildRoot, "asset_catalog.txt")).Single(line =>
            line.StartsWith("record: guid=" + selectedMaterial + " ", StringComparison.Ordinal));
        string artifact = Path.GetFullPath(Path.Combine(buildRoot, materialRecord.Split(' ')
            .Single(part => part.StartsWith("artifact=", StringComparison.Ordinal))[9..]));
        if (!artifact.StartsWith(buildRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The test material must belong to the disposable build.");
        ulong beforeProjection = peer.Control.Status().SceneGeneration;
        string heldMaterial = artifact + ".group-test-held";
        File.Move(artifact, heldMaterial);
        try
        {
            Click(add);
            await NameUiWait(() => document.ListNodes().Count == baseline.Length + group.Nodes.Count + 1 &&
                Field<Button>("_undo").IsEnabled, "verified atomic storefront Add");
            await NameUiWait(() => document.ApplyStatus().Value?.Request?.State == ApplyState.Failed &&
                Field<Button>("_retryApply").IsEnabled, "deliberate Apply failure");
            Verify("Apply failure retains the committed group, history and last usable projection",
                document.ListNodes().Count == baseline.Length + group.Nodes.Count + 1 &&
                Field<Button>("_undo").IsEnabled && peer.Control.Status().SceneGeneration == beforeProjection);
        }
        finally { File.Move(heldMaterial, artifact); }
        ulong committedRevision = document.ApplyStatus().Value!.CurrentDocumentRevision!.Value;
        Click(Field<Button>("_retryApply"));
        await NameUiWait(() => document.ApplyStatus().Value?.Request?.State == ApplyState.Applied &&
            peer.Control.Status().SceneGeneration > beforeProjection, "explicit Apply recovers storefront projection");
        Verify("explicit Apply does not create more nodes or change the document revision",
            document.ListNodes().Count == baseline.Length + group.Nodes.Count + 1 &&
            document.ApplyStatus().Value?.CurrentDocumentRevision == committedRevision);
        var appended = document.ListNodes().Skip(baseline.Length).ToArray();
        var root = appended[0].Node;
        bool SameChildren() => document.ListNodes().Skip(baseline.Length + 1).Select((node, i) =>
        {
            var expected = group.Nodes[i];
            return node == appended[i + 1] && node.Parent == appended[(expected.Parent ?? -1) + 1].Node &&
                TransformMath.LocalOf(document, node.Node) == new LocalTransform(expected.Position, expected.EulerRadians, expected.Scale) &&
                (expected.Renderable is null || Binding(node.Node, "meshAsset") == expected.Renderable.Mesh &&
                    Binding(node.Node, "materialAsset") == expected.Renderable.Material);
        }).SequenceEqual(Enumerable.Repeat(true, group.Nodes.Count));
        AssetGuid? Binding(EntityGuid node, string key) =>
            document.GetProperty(SceneSchema.AddressOf(document.Schema, node, "Renderable", key)!.Value) is { IsAbsent: false } value
                ? value.AsAsset() : null;
        Verify("group adds exactly one named root and all local bindings", appended[0].Name == group.Name && appended[0].Parent is null &&
            TransformMath.LocalOf(document, root) == LocalTransform.Identity && SameChildren(), root.ToString());
        await NameUiWait(() => Field<Selection>("_selection").Node == root, "Add selects its verified wrapper");
        await Capture("storefront-added");
        Click(Field<Button>("_undo"));
        await NameUiWait(() => document.ListNodes().SequenceEqual(baseline) && Field<Button>("_redo").IsEnabled, "Undo removes all nodes");
        Click(Field<Button>("_redo"));
        await NameUiWait(() => document.ListNodes().Skip(baseline.Length).SequenceEqual(appended), "Redo restores the identical GUIDs");
        Verify("one Undo and one Redo restore all links and bindings", SameChildren());
        await NameUiWait(() => Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Any(n => n.Node == root) &&
            document.ApplyStatus().Value?.Request?.State == ApplyState.Applied, "Redo appears in Hierarchy and Runtime");
        var tree = new DocumentTree();
        tree.Rebuild(document);
        Vector3 WorldPosition(EntityGuid node)
        {
            float[] matrix = TransformMath.WorldOf(document, tree, node);
            return new(matrix[12], matrix[13], matrix[14]);
        }
        var initialPositions = appended.Skip(1).Select(node => WorldPosition(node.Node)).ToArray();
        var row = Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Single(n => n.Node == root);
        Field<ListBox>("_entities").SelectedItem = row;
        await NameUiWait(() => Field<GizmoSnapshot>("_gizmoShown")?.Node == root, "root Gizmo ready");
        worker.BeginGizmoDrag(GizmoAxis.X);
        var gizmo = (GizmoDragController)typeof(EditorWorker).GetField("_gizmo", hidden)!.GetValue(worker)!;
        await NameUiWait(() => gizmo.Dragging, "ordinary Move Gizmo grabbed");
        worker.GizmoDragTo(0.75f, Stopwatch.GetTimestamp());
        await NameUiWait(() => gizmo.LatestWorld is { } matrix && matrix[12] > 0.5f, "Move Gizmo preview translated the root");
        worker.EndGizmoDrag(0.75f, Stopwatch.GetTimestamp());
        await NameUiWait(() => TransformMath.LocalOf(document, root) != LocalTransform.Identity, "Move Gizmo commits the group translation");
        Vector3 rootDelta = TransformMath.LocalOf(document, root).Position;
        Verify("Gizmo moves every child with the root and preserves its local values", SameChildren() &&
            appended.Skip(1).Select((n, i) => Vector3.Distance(WorldPosition(n.Node), initialPositions[i] + rootDelta) < 1e-5f)
                .All(moved => moved), rootDelta.ToString());
        await NameUiWait(() => document.ApplyStatus().Value?.Request?.State == ApplyState.Applied, "group move applied");
        Click(Field<Button>("_save"));
        await NameUiWait(() => File.ReadAllText(source).Contains(root.ToString(), StringComparison.Ordinal), "Save writes the same source");
        string saved = Fingerprint();
        File.WriteAllText(idsFile, saved);
        await Reopen(saved);
    }
}
