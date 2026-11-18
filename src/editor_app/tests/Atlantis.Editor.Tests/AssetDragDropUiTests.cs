using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
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
    private static async Task CheckAssetDragUi(MainWindow window, List<NameUiCheck> checks, Action<string> work, bool compact)
    {
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        void CheckHere(string label, bool ok) => checks.Add(new(label, ok));
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        window.Width = compact ? 980 : 1380;
        window.Height = compact ? 650 : 860;
        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } p && File.Exists(p.SessionPath) &&
            Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count != 0, "asset drag Runtime and document", 90);
        var runtime = Field<RuntimeProcess>("_runtime");
        work(runtime.Launch.WorkDirectory);
        nint hwnd = window.TryGetPlatformHandle()!.Handle;
        window.Activate();
        await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground desktop for native drag");
        if (GetDpiForWindow(hwnd) != 96) throw new InvalidOperationException("Native drag requires Windows at 100%.");
        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        var doc = new RemoteAuthoringDocument(peer.Authoring, peer);
        var info = doc.Info()!;
        string? disposable = Environment.GetEnvironmentVariable("ATLANTIS_0115_DISPOSABLE_SCENE");
        if (disposable is null || !Path.GetFullPath(disposable).Equals(Path.GetFullPath(info.SourcePath), StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("An explicit disposable scene is required for the Save/reopen check: " + info.SourcePath);
        byte[] original = File.ReadAllBytes(info.SourcePath);
        string size = compact ? "compact" : "standard";
        var initial = doc.ListNodes().Select(n => n.Node).ToHashSet();

        async Task<ListBoxItem> Source(AssetSummary asset, bool filtered = true)
        {
            Field<TextBox>("_assetSearch").Text = filtered ? asset.Guid.ToString() : "";
            var list = Field<ListBox>("_assetsList");
            await NameUiWait(() => list.Items.OfType<AssetSummary>().Any(a => a.Guid == asset.Guid), "find asset by GUID");
            list.ScrollIntoView(list.Items.OfType<AssetSummary>().Single(a => a.Guid == asset.Guid));
            await NameUiWait(() => list.GetVisualDescendants().OfType<ListBoxItem>().Any(Row), "realized asset row");
            return list.GetVisualDescendants().OfType<ListBoxItem>().Single(Row);
            bool Row(ListBoxItem item) => item is { DataContext: AssetSummary a, IsVisible: true } && a.Guid == asset.Guid;
        }

        async Task<TaskCompletionSource> HoldWorker()
        {
            var worker = Field<EditorWorker>("_worker");
            var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            typeof(EditorWorker).GetMethod("Enqueue", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!
                .Invoke(worker, new object[] { (Action<RemoteSession>)(_ => { entered.SetResult(); release.Task.GetAwaiter().GetResult(); }) });
            try { await entered.Task.WaitAsync(TimeSpan.FromSeconds(8)); }
            catch { release.TrySetResult(); throw; }
            return release;
        }

        async Task NativeDrag(Control source, Func<Control> resolveTarget, string capture, bool escape = false, bool viaInspector = false, bool filtered = true)
        {
            var tabs = Field<TabControl>("_assetTabs");
            var tab = tabs.Items.OfType<TabItem>().First();
            PixelPoint? via = viaInspector ? Center(tab) : null;
            if (viaInspector) tabs.SelectedIndex = 0;
            resolveTarget().BringIntoView();
            await Task.Delay(220);
            var to = Center(resolveTarget());
            if (viaInspector) tabs.SelectedIndex = 1;
            var asset = (AssetSummary)((ListBoxItem)source).DataContext!;
            source = await Source(asset, filtered);
            source.BringIntoView();
            await Task.Delay(220);
            var from = Center(source);
            static PixelPoint Center(Control control) =>
                control.PointToScreen(new Point(control.Bounds.Width / 2, control.Bounds.Height / 2));
            if (GetForegroundWindow() != hwnd) throw new InvalidOperationException("Editor lost the foreground.");
            await Task.Run(() =>
            {
                if (!SetCursorPos(from.X, from.Y)) throw new InvalidOperationException("Native cursor unavailable.");
                Thread.Sleep(80);
                mouse_event(2, 0, 0, 0, 0);
                try
                {
                    // Cross the threshold inside the row before moving to the
                    // tab header or target.
                    SetCursorPos(from.X + 8, from.Y);
                    Thread.Sleep(90);
                    var next = from;
                    if (via is PixelPoint header)
                    {
                        Move(next, header);
                        Thread.Sleep(380);
                        next = header;
                    }
                    Move(next, to);
                    static void Move(PixelPoint start, PixelPoint end)
                    {
                        for (int step = 1; step <= 14; step++)
                        {
                            SetCursorPos(start.X + (end.X - start.X) * step / 14,
                                start.Y + (end.Y - start.Y) * step / 14);
                            Thread.Sleep(45);
                        }
                    }
                    Thread.Sleep(240);
                    if (Environment.GetEnvironmentVariable("ATLANTIS_0115_CAPTURE") is { } helper)
                    {
                        var options = new ProcessStartInfo("python") { UseShellExecute = false, CreateNoWindow = true };
                        options.ArgumentList.Add(helper);
                        options.ArgumentList.Add(Environment.ProcessId.ToString());
                        options.ArgumentList.Add(Path.Combine(runtime.Launch.WorkDirectory, size + "-" + capture + ".png"));
                        using var process = Process.Start(options)!;
                        if (!process.WaitForExit(10000) || process.ExitCode != 0)
                            throw new InvalidOperationException("Native capture failed.");
                    }
                    if (escape)
                    {
                        keybd_event(27, 0, 0, 0);
                        keybd_event(27, 0, 2, 0);
                        Thread.Sleep(120);
                    }
                }
                finally { mouse_event(4, 0, 0, 0, 0); }
            });
            await Task.Delay(240);
        }

        try
        {
            Field<TabControl>("_assetTabs").SelectedIndex = 1;
            var browser = Field<AssetBrowser>("_assetBrowser");
            await NameUiWait(() => browser.State == "Ready", "catalog ready");
            var mesh = browser.Items.First(a => a.Type == AssetType.Mesh && a.Source.StartsWith("content:bistro/"));
            var material = browser.Items.Single(a => a.Type == AssetType.Material &&
                a.Source == "assets:materials/pbr_dielectric_rough.material.txt");
            var root = window.GetVisualDescendants().OfType<Border>().Single(b =>
                AutomationProperties.GetName(b) == "Drop mesh to add at scene root");

            await NativeDrag(await Source(material), () => root, "wrong-type");
            CheckHere("material refused by mesh root target", doc.ListNodes().Count == initial.Count && !Field<Button>("_undo").IsEnabled);
            await NativeDrag(await Source(mesh), () => Field<ListBox>("_entities"), "outside-target");
            CheckHere("release over a Hierarchy node/list is not a root addition", doc.ListNodes().Count == initial.Count &&
                !Field<Button>("_undo").IsEnabled);
            await NativeDrag(await Source(mesh), () => root, "escape", escape: true);
            CheckHere("Escape leaves root, history and document unchanged", doc.ListNodes().Count == initial.Count &&
                !Field<Button>("_undo").IsEnabled);

            var scrolled = await Source(mesh, filtered: false);
            var assetList = Field<ListBox>("_assetsList");
            CheckHere("source realized by scrolling the virtualized catalog", assetList.ItemCount > 1000 &&
                assetList.GetVisualDescendants().OfType<ListBoxItem>().Count() < assetList.ItemCount);
            await NativeDrag(scrolled, () => root, "root-copy", filtered: false);
            await NameUiWait(() => Field<TextBlock>("_addMeshState").Text?.StartsWith("Added ") == true, "root drop settlement");
            var added = doc.ListNodes().Single(n => !initial.Contains(n.Node));
            var meshAddress = SceneSchema.AddressOf(doc.Schema, added.Node, "Renderable", "meshAsset")!.Value;
            var materialAddress = SceneSchema.AddressOf(doc.Schema, added.Node, "Renderable", "materialAsset")!.Value;
            await NameUiWait(() => Field<InspectorSnapshot>("_inspectorShown")?.Subject == added.Node, "root selection");
            CheckHere("one unnamed root, identity transform, captured mesh and absent material", added.Parent is null &&
                added.Name == "" && TransformMath.LocalOf(doc, added.Node) == LocalTransform.Identity &&
                doc.GetProperty(meshAddress)?.AsAsset() == mesh.Guid && doc.GetProperty(materialAddress)?.IsAbsent == true);
            Click(Field<Button>("_undo"));
            await NameUiWait(() => !doc.ListNodes().Any(n => n.Node == added.Node) && Field<Button>("_redo").IsEnabled, "root Undo");
            Click(Field<Button>("_redo"));
            await NameUiWait(() => doc.ListNodes().Any(n => n.Node == added.Node) && Field<Button>("_undo").IsEnabled, "root Redo");
            CheckHere("root Undo/Redo restores the same created GUID and mesh", doc.GetProperty(meshAddress)?.AsAsset() == mesh.Guid);

            await NameUiWait(() => Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Any(row => row.Node == added.Node), "restored hierarchy row");
            typeof(MainWindow).GetMethod("SetSelection", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!
                .Invoke(window, new object?[] { (EntityGuid?)added.Node, true });
            await NameUiWait(() => Field<InspectorSnapshot>("_inspectorShown")?.Subject == added.Node, "restored node Inspector");
            Control MaterialRow() => InspectorRow(window, "Renderable.materialAsset");
            if (!compact)
            {
                // Route a real focus change before dragging. Its ordinary
                // field commit owns the draft while the worker is busy.
                var tabs = Field<TabControl>("_assetTabs");
                tabs.SelectedIndex = 0;
                var input = InspectorRow(window, "Transform.localPosition").GetVisualDescendants().OfType<TextBox>().First();
                input.BringIntoView();
                await Task.Delay(150);
                if (!input.Focus()) throw new InvalidOperationException("Transform draft could not take focus.");
                input.Text = "0.125 0 0";
                var releaseBlur = await HoldWorker();
                try
                {
                    Field<Button>("_save").Focus();
                    var pendingEdits = typeof(MainWindow).GetField("_pendingSceneFieldEdits",
                        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!;
                    await NameUiWait(() => (int)pendingEdits.GetValue(window)! == 1, "ordinary focus-loss write pending");
                    tabs.SelectedIndex = 1;
                    await NativeDrag(await Source(mesh), () => root, "pending-draft");
                    CheckHere("draft settlement blocks native drop without consuming ordinary field edit",
                        doc.ListNodes().Count == initial.Count + 1 &&
                        !typeof(MainWindow).GetField("_assetDropPending",
                            System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(window)!.Equals(true));
                    releaseBlur.SetResult();
                    await NameUiWait(() => (int)pendingEdits.GetValue(window)! == 0, "ordinary field edit settles independently");
                    CheckHere("normal focus-loss commit retains its exact input", TransformMath.LocalOf(doc, added.Node).Position.X == 0.125f);
                }
                finally { releaseBlur.TrySetResult(); }
            }

            if (!compact)
            {
                // Hold the worker before lookup, then make the source invisible and
                // restore the filter. The native gesture already handed off a drop.
                var release = await HoldWorker();
                try
                {
                    var beforeDrop = doc.ApplyStatus().Value!;
                    await NativeDrag(await Source(material), MaterialRow, "reference-queued", viaInspector: true);
                    var pending = typeof(MainWindow).GetField("_assetDropPending",
                        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!;
                    CheckHere("native completion hands off exactly one queued drop", (bool)pending.GetValue(window)!);
                    var filter = Field<TextBox>("_assetSearch");
                    filter.Text = "no drop source remains";
                    filter.Text = material.Guid.ToString();
                    release.SetResult();
                    await NameUiWait(() => !(bool)pending.GetValue(window)!, "expired source settlement");
                    var afterDrop = doc.ApplyStatus().Value!;
                    CheckHere("hide/show before send changes neither revision nor Apply nor reference",
                        beforeDrop.CurrentDocumentRevision == afterDrop.CurrentDocumentRevision &&
                        beforeDrop.Request?.Receipt.Id == afterDrop.Request?.Receipt.Id && doc.GetProperty(materialAddress)?.IsAbsent == true);
                }
                finally { release.TrySetResult(); }
            }
            Field<TabControl>("_assetTabs").SelectedIndex = 1;
            await NativeDrag(await Source(material), MaterialRow, "reference-copy", viaInspector: true);
            await NameUiWait(() => doc.GetProperty(materialAddress)?.Equals(PropertyValue.FromAsset(material.Guid)) == true, "reference drop");
            await NameUiWait(() => Field<TextBlock>("_editLine").Text?.StartsWith("Assigned ") == true, "verified reference history");
            await NameUiWait(() => window.FocusManager?.GetFocusedElement() is Button { DataContext: SceneAddress address }
                && address == materialAddress, "focus returns to the current reference launcher");
            Click(Field<Button>("_undo"));
            await NameUiWait(() => doc.GetProperty(materialAddress)?.IsAbsent == true && Field<Button>("_redo").IsEnabled, "reference Undo");
            Click(Field<Button>("_redo"));
            await NameUiWait(() => doc.GetProperty(materialAddress)?.Equals(PropertyValue.FromAsset(material.Guid)) == true, "reference Redo");
            CheckHere("one reference history step preserves optional absence", doc.ListNodes().Any(n => n.Node == added.Node));

            var position = SceneSchema.AddressOf(doc.Schema, added.Node, "Transform", "localPosition")!.Value;
            var expectedPosition = PropertyValue.FromVec3(new System.Numerics.Vector3(0.2f, 0.25f, 0.1f));
            var edit = doc.SubmitTransaction([new Atlantis.Gameplay.Authoring.SetProperty(position, expectedPosition)]);
            if (doc.DrainFailures().Any(f => edit.Contains(f.Ticket))) throw new InvalidOperationException("Transform edit refused.");
            Click(Field<Button>("_save"));
            await NameUiWait(() => File.ReadAllText(info.SourcePath).Contains(added.Node.ToString()), "explicit Save writes the node");
            if (peer.Authoring.Open() is not null) throw new InvalidOperationException("Saved source reopen refused.");
            await NameUiWait(() => !Field<Button>("_undo").IsEnabled && !Field<Button>("_redo").IsEnabled, "reopen clears local history");
            CheckHere("reopened exact root/node, mesh, material and Transform", doc.ListNodes().Any(n => n.Node == added.Node && n.Parent is null) &&
                doc.GetProperty(meshAddress)?.AsAsset() == mesh.Guid && doc.GetProperty(materialAddress)?.Equals(PropertyValue.FromAsset(material.Guid)) == true &&
                doc.GetProperty(position)?.Equals(expectedPosition) == true);

            var apply = doc.ApplyTracked();
            if (!apply.IsOk) throw new InvalidOperationException("Reopened document Apply refused: " + apply.Error);
            peer.Control.Pause();
            var frame = peer.Control.Step(new StepRequest(1, Path.Combine(runtime.Launch.WorkDirectory, size + "-reopened-runtime.png")));
            await NameUiWait(() => doc.ApplyStatus(apply.Value!.Id).Value?.Request?.State == ApplyState.Applied, "reopened Runtime projection");
            CheckHere("explicit reopened Apply renders a captured Runtime frame", frame.Report?.Data is { DrawItemCount: > 0 });
        }
        finally { File.WriteAllBytes(info.SourcePath, original); }
    }

    // Use the same one-client relay setup as the creation fixture. Only the
    // selected mutation/reply is changed; the shared Runtime settles all calls.
    private static int RunAssetDragReceipts(string executable, string scene, string scenario)
    {
        if (scenario is not ("refused" or "lost-submit" or "lost-apply" or "retry-apply" or
            "batch-draft" or "stale-target" or "accepted-apply-overlay-loss")) return 2;
        string work = RuntimeProcess.NewWorkDirectory();
        using var runtime = RuntimeProcess.Start(new RuntimeLaunch(Path.GetFullPath(executable), scene, null, work));
        SessionInfo host = runtime.WaitForSession(TimeSpan.FromSeconds(25));
        using var observer = RemoteSession.Connect(host);
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        string attach = Path.Combine(work, "asset-drop-relay.json");
        File.WriteAllText(attach, JsonSerializer.Serialize(new { protocol = SessionFile.Protocol,
            port = ((IPEndPoint)listener.LocalEndpoint).Port, token = host.Token, pid = host.Pid, scene = host.Scene.ToString() }));
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
        int submits = 0, applies = 0;
        var held = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        ApplyReceipt? acceptedApply = null;
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
            CancellationToken token = deadline.Token;
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
                if (scenario == "accepted-apply-overlay-loss" && acceptedApply is not null &&
                    method == "control.setViewportOverlay") return;
                if (method == "authoring.submitTransaction")
                {
                    Interlocked.Increment(ref submits);
                    if (scenario == "refused")
                    {
                        // Keep the ticket count; turn the final write into an
                        // unknown-node command so the real host rejects atomically.
                        var args = request.Find("params")!;
                        var commands = args.Find("commands")!.Items.ToArray();
                        var address = SceneSchema.AddressOf(observer.Authoring.Schema, new EntityGuid(Guid.NewGuid()),
                            "Renderable", "meshAsset")!.Value;
                        var mesh = observer.Assets.ListAssets().Value.First(asset => asset.Type == AssetType.Mesh);
                        var replacement = JsonValue.NewArray();
                        for (int i = 0; i < commands.Length - 1; i++) replacement.Push(commands[i]);
                        replacement.Push(Codec.Encode(new Atlantis.Gameplay.Authoring.SetProperty(address, PropertyValue.FromAsset(mesh.Guid))));
                        args.Set("commands", replacement);
                        text = Json.Write(request);
                    }
                }
                if (method == "authoring.applyTracked" && Interlocked.Increment(ref applies) == 1 && scenario == "retry-apply")
                {
                    var failure = JsonValue.NewObject().Set("id", request.Find("id")!)
                        .Set("result", JsonValue.NewObject().Set("err", JsonValue.String(nameof(ApplyError.CookFailed))));
                    await output.WriteLineAsync(Json.Write(failure).AsMemory(), token);
                    continue;
                }
                await forward.WriteLineAsync(text.AsMemory(), token);
                string reply = await read.ReadLineAsync(token) ?? throw new IOException("Runtime closed the relay");
                if (method == "assets.get" && scenario is "batch-draft" or "stale-target")
                {
                    held.TrySetResult();
                    await release.Task.WaitAsync(token);
                }
                if (method == "authoring.applyTracked" && scenario == "accepted-apply-overlay-loss" &&
                    Json.Parse(reply).Find("result")?.Find("ok") is JsonValue ok)
                    acceptedApply = Codec.DecodeApplyReceipt(ok) ?? throw new InvalidOperationException("Expected an accepted receipt.");
                if (method == "authoring.submitTransaction" && scenario == "lost-submit") return;
                if (method == "authoring.applyTracked" && scenario == "lost-apply")
                {
                    held.TrySetResult();
                    await release.Task.WaitAsync(token);
                    return;
                }
                await output.WriteLineAsync(reply.AsMemory(), token);
            }
        }

        async Task CheckReceipt(MainWindow window)
        {
            T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
            await NameUiWait(() => Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count != 0, "receipt fixture ready");
            int count = observer.Authoring.ListNodes().Value.Count;
            var catalog = observer.Assets.ListAssets().Value;
            var worker = Field<EditorWorker>("_worker");
            const System.Reflection.BindingFlags instancePrivate =
                System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;
            object? Call(string name, params object?[] parameters) =>
                typeof(MainWindow).GetMethod(name, instancePrivate)!.Invoke(window, parameters);
            void Queue(AssetSummary asset, AssetDropField? target = null)
            {
                var drag = new AssetDragIntent(asset, catalog, worker.MeshDocumentLifetime);
                drag.Queue();
                typeof(MainWindow).GetField("_assetDrag", instancePrivate)!.SetValue(window, drag);
                Call("StartAssetDrop", worker, drag, target);
            }
            var mesh = catalog.First(a => a.Type == AssetType.Mesh);
            var feedback = Field<TextBlock>("_addMeshState");
            var undo = Field<Button>("_undo");
            var line = Field<TextBlock>("_applyLine");

            if (scenario == "batch-draft")
            {
                Field<Selection>("_selection").Set(Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows")
                    .Take(2).Select(row => row.Node).ToArray());
                Call("PublishSelection", false);
                const string key = "Transform.localPosition";
                await NameUiWait(() => Field<Dictionary<string, TextBox>>("_batchInputs").TryGetValue(key, out var box) &&
                    box.IsEnabled, "editable multi-selection Inspector");
                Queue(mesh);
                await held.Task.WaitAsync(deadline.Token);
                var input = Field<Dictionary<string, TextBox>>("_batchInputs")[key];
                input.Text = input.Text == "1 2 3" ? "3 2 1" : "1 2 3";
                if (Call("CurrentSceneInput") is null) throw new InvalidOperationException("Expected a live batch draft.");
                release.TrySetResult();
                await NameUiWait(() => !Equals(typeof(MainWindow).GetField("_assetDropPending", instancePrivate)!.GetValue(window),
                    true), "cancelled root drop settles");
                if (submits != 0 || applies != 0 || undo.IsEnabled ||
                    observer.Authoring.ListNodes().Value.Count != count || Call("CurrentSceneInput") is null)
                    throw new InvalidOperationException("The new batch draft did not cancel the queued root drop.");
                Console.WriteLine("PASS batch-draft: input retained; no submit, history or Apply; work=" + work);
                return;
            }
            if (scenario == "stale-target")
            {
                var doc = new RemoteAuthoringDocument(observer.Authoring, observer);
                var address = doc.ListNodes().Select(node => SceneSchema.AddressOf(doc.Schema, node.Node,
                    "Renderable", "materialAsset")).OfType<SceneAddress>().First(address => doc.GetProperty(address) is not null);
                Call("SetSelection", address.Node, false);
                AssetDropField? Displayed() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.AssetDropFields
                    .FirstOrDefault(field => field.Field.Address == address);
                await NameUiWait(() => Displayed() is not null, "initial Inspector reference");
                var target = Displayed()!;
                var materials = catalog.Where(asset => asset.Type == AssetType.Material &&
                    PropertyValue.FromAsset(asset.Guid) != target.Field.Before).Take(2).ToArray();
                Queue(materials[1], target);
                await held.Task.WaitAsync(deadline.Token);
                var value = PropertyValue.FromAsset(materials[0].Guid);
                var external = doc.SubmitTransaction([new Atlantis.Gameplay.Authoring.SetProperty(address, value)]);
                if (doc.DrainFailures().Any(failure => external.Contains(failure.Ticket)) || doc.GetProperty(address) != value)
                    throw new InvalidOperationException("External reference update refused.");
                release.TrySetResult();
                await NameUiWait(() => Field<TextBlock>("_editLine").Text?.StartsWith("Drop refused:") == true, "stale target refused");
                await NameUiWait(() => Displayed()?.Field.Before == value, "Inspector republishes the external value after refusal", 5);
                if (submits != 0 || applies != 0 || undo.IsEnabled)
                    throw new InvalidOperationException("Stale target sent a mutation or recorded local history.");
                Console.WriteLine("PASS stale-target: external value republished without submit, history or Apply; work=" + work);
                return;
            }

            Queue(mesh);
            if (scenario == "accepted-apply-overlay-loss")
            {
                await NameUiWait(() => Field<TextBlock>("_connectionLine").Text?.StartsWith("session failed") == true,
                    "post-Apply overlay connection loss");
                var receipt = acceptedApply ?? throw new InvalidOperationException("The relay never received a valid Apply receipt.");
                if (line.Text?.Contains(receipt.Id.ToString()) != true || line.Text.Contains("reply lost") ||
                    !undo.IsEnabled || feedback.Text?.StartsWith("Added ") != true || submits != 1 || applies != 1 ||
                    observer.Authoring.ApplyStatus(receipt.Id).Value?.Request?.Receipt != receipt)
                    throw new InvalidOperationException("A verified Apply receipt was reclassified: " + line.Text);
                Console.WriteLine("PASS accepted-apply-overlay-loss: committed history and accepted receipt retained; work=" + work);
                return;
            }
            if (scenario == "lost-apply")
            {
                await held.Task.WaitAsync(deadline.Token);
                await NameUiWait(() => undo.IsEnabled && feedback.Text?.StartsWith("Added ") == true, "known edit before Apply reply");
                release.TrySetResult();
                await NameUiWait(() => Field<TextBlock>("_applyLine").Text?.Contains("unknown completion") == true, "only Apply unknown");
            }
            else if (scenario == "retry-apply")
            {
                var retry = Field<Button>("_retryApply");
                await NameUiWait(() => undo.IsEnabled && retry.IsEnabled && line.Text?.Contains("CookFailed") == true,
                    "known edit with failed Apply acceptance");
                if (Volatile.Read(ref applies) != 1 || observer.Authoring.ApplyStatus().Value!.Request is not null)
                    throw new InvalidOperationException("Unexpected automatic Apply after failure.");
                retry.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                await NameUiWait(() => line.Text?.StartsWith("apply: applied") == true, "explicit Apply repairs projection");
            }
            else if (scenario == "lost-submit")
            {
                await NameUiWait(() => Field<TextBlock>("_connectionLine").Text?.StartsWith("session failed") == true,
                    "lost transaction connection surfaced");
                if (feedback.Text?.StartsWith("Drop outcome unknown") != true)
                    throw new InvalidOperationException("Missing transaction-unknown feedback: " + feedback.Text);
            }
            else await NameUiWait(() => feedback.Text?.StartsWith("Drop refused:") == true, "transaction refusal feedback");
            if (Volatile.Read(ref submits) != 1 || Volatile.Read(ref applies) != (scenario == "lost-apply" ? 1 : scenario == "retry-apply" ? 2 : 0) ||
                observer.Authoring.ListNodes().Value.Count != count + (scenario == "refused" ? 0 : 1) ||
                undo.IsEnabled != (scenario is "lost-apply" or "retry-apply") ||
                (scenario == "lost-apply" && feedback.Text?.StartsWith("Added ") != true))
                throw new InvalidOperationException("Wrong receipt outcome: " + feedback.Text);
            Console.WriteLine($"PASS {scenario}: one submit; {applies} Apply; expected document, history and feedback; work={work}");
        }
    }

    [DllImport("user32.dll")] private static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint hwnd);
    [DllImport("user32.dll")] private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] private static extern void mouse_event(uint flags, int dx, int dy, uint data, nuint extra);
    [DllImport("user32.dll")] private static extern void keybd_event(byte key, byte scan, uint flags, nuint extra);
}
