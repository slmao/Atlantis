using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia.Controls;
using Avalonia.Interactivity;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
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
        window.Activate();
        await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground storefront Editor");
        Verify("Windows 100% and embedded Runtime", GetDpiForWindow(hwnd) == 96 && viewport.RuntimeChildIsHosted());
        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));

        async Task Capture(string name)
        {
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
