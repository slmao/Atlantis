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
            var start = new ProcessStartInfo("py") { UseShellExecute = false, CreateNoWindow = true };
            foreach (string arg in new[] { "-3", Setting("CAPTURE"), Environment.ProcessId.ToString(CultureInfo.InvariantCulture),
                Path.Combine(evidence, name + ".png"), window.Title ?? "" }) start.ArgumentList.Add(arg);
            await Task.Run(() =>
            {
                using var capture = Process.Start(start)!;
                if (!capture.WaitForExit(15000) || capture.ExitCode != 0)
                    throw new InvalidOperationException("Native capture failed: " + name);
            });
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
        if (phase != "authored") throw new InvalidOperationException("Unknown storefront phase.");

        var baseline = document.ListNodes().ToArray();
        ulong revision = document.ApplyStatus().Value!.CurrentDocumentRevision!.Value;
        var add = Field<Button>("_addGroup");
        var search = Field<TextBox>("_assetSearch");
        var assets = Field<ListBox>("_assetsList");
        var worker = Field<EditorWorker>("_worker");
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
        await Capture("storefront-added");
        Click(Field<Button>("_undo"));
        await NameUiWait(() => document.ListNodes().SequenceEqual(baseline) && Field<Button>("_redo").IsEnabled, "Undo removes all nodes");
        Click(Field<Button>("_redo"));
        await NameUiWait(() => document.ListNodes().Skip(baseline.Length).SequenceEqual(appended), "Redo restores the identical GUIDs");
        Verify("one Undo and one Redo restore all links and bindings", SameChildren());
        var row = Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Single(n => n.Node == root);
        Field<ListBox>("_entities").SelectedItem = row;
        await NameUiWait(() => Field<GizmoSnapshot>("_gizmoShown")?.Node == root &&
            typeof(MainWindow).GetMethod("ViewportProjection", hidden)!.Invoke(window, [viewport]) is CameraMatrices, "root Gizmo ready");
        var camera = (CameraMatrices)typeof(MainWindow).GetMethod("ViewportProjection", hidden)!.Invoke(window, [viewport])!;
        var (left, top, width, height) = viewport.ScreenRect();
        var axes = new[] { Vector3.UnitX, Vector3.UnitY, Vector3.UnitZ };
        var handles = Gizmo.Handles(camera, new PixelRect(0, 0, width, height), Vector3.Zero, axes)!;
        var axis = handles[0] ?? throw new InvalidOperationException("World X handle unavailable.");
        Vector2 from = axis.Origin + axis.Direction * 56;
        Vector2 to = from + axis.Direction * 48;
        await Task.Run(() =>
        {
            if (GetForegroundWindow() != hwnd || !SetCursorPos(left + (int)from.X, top + (int)from.Y))
                throw new InvalidOperationException("Viewport pointer unavailable.");
            Thread.Sleep(75);
            mouse_event(2, 0, 0, 0, 0);
            try
            {
                for (int step = 1; step <= 8; ++step)
                {
                    Vector2 point = Vector2.Lerp(from, to, step / 8f);
                    SetCursorPos(left + (int)point.X, top + (int)point.Y);
                    Thread.Sleep(50);
                }
            }
            finally { mouse_event(4, 0, 0, 0, 0); }
        });
        await NameUiWait(() => TransformMath.LocalOf(document, root) != LocalTransform.Identity, "native Gizmo moves the group");
        Verify("Gizmo changes the root and preserves all child local values", SameChildren());
        await NameUiWait(() => document.ApplyStatus().Value?.Request?.State == ApplyState.Applied, "group move applied");
        Click(Field<Button>("_save"));
        await NameUiWait(() => File.ReadAllText(source).Contains(root.ToString(), StringComparison.Ordinal), "Save writes the same source");
        string saved = Fingerprint();
        File.WriteAllText(idsFile, saved);
        long opened = worker.MeshDocumentLifetime;
        Click(Field<Button>("_reopen"));
        await NameUiWait(() => worker.MeshDocumentLifetime != opened && document.ListNodes().Skip(baseline.Length).SequenceEqual(appended),
            "reopen restores same document");
        Verify("reopen preserves all identities, transforms and references", Fingerprint() == saved);
        ulong beforeReopenApply = peer.Control.Status().SceneGeneration;
        Click(Field<Button>("_retryApply"));
        await NameUiWait(() => peer.Control.Status().SceneGeneration > beforeReopenApply, "reopened scene is shown");
        await Capture("storefront-reopened");
    }
}
