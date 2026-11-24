using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void HierarchyFilterChecks()
    {
        var doc = new FakeDocument();
        var tree = new DocumentTree();
        var parent = doc.Add(901, null, LocalTransform.Identity, FakeDocument.CameraType);
        var child = doc.Add(902, parent, LocalTransform.Identity, FakeDocument.Light);
        var grandchild = doc.Add(903, child, LocalTransform.Identity);
        var sibling = doc.Add(904, parent, LocalTransform.Identity, FakeDocument.Light);
        var other = doc.Add(905, null, LocalTransform.Identity);
        tree.Rebuild(doc);
        Change(new NodeNameChanged(parent, "Lights group"));
        Change(new NodeNameChanged(child, "Key LIGHT"));
        Change(new NodeNameChanged(sibling, "Fill light"));
        Change(new NodeNameChanged(other, "Light camera name only"));

        var visibilityRows = Rows();
        var parentRow = visibilityRows.Single(row => row.Node == parent);
        var childRow = visibilityRows.Single(row => row.Node == child);
        var unrelatedRow = visibilityRows.Single(row => row.Node == other);
        var hideNode = ViewportMembership.Plan(tree, null, VisibilityAction.HideNode, [parentRow]);
        Check(hideNode.Refusal is null && hideNode.Policy?.Hidden.SequenceEqual([parent]) == true,
            "viewport hide node preserves descendants");
        var hideTree = ViewportMembership.Plan(tree, null, VisibilityAction.HideSubtree, [parentRow, childRow]);
        Check(hideTree.Policy?.Hidden.Count == 4, "viewport subtree membership deduplicates overlapping roots");
        var locked = ViewportMembership.Plan(tree, hideNode.Policy, VisibilityAction.LockPick, [unrelatedRow]);
        var isolated = ViewportMembership.Plan(tree, locked.Policy, VisibilityAction.Isolate, [unrelatedRow, parentRow]);
        var view = new ViewportMembership(isolated.Policy);
        Check(!view.Visible(parent) && view.Visible(child) && view.Visible(other) && !view.Pickable(other),
            "isolation preserves manual hides and visible pick locks");
        var exited = ViewportMembership.Plan(tree, isolated.Policy, VisibilityAction.ExitIsolation, []);
        Check(locked.Policy?.SameMembership(exited.Policy) == true &&
            ViewportMembership.Plan(tree, exited.Policy, VisibilityAction.Restore, []).Policy is null,
            "exit isolation retains manual sets and restore clears them without selection");
        Check(ViewportMembership.Plan(tree, null, VisibilityAction.Isolate, []).Refusal is not null,
            "empty selection cannot replace viewport isolation");

        var filter = new HierarchyFilter("  liGht ", FakeDocument.Light);
        var rows = Rows();
        var projection = filter.Project(rows);
        Check(projection.Rows.Select(row => row.Node).SequenceEqual(new[] { parent, child, sibling }) &&
            !projection.Matches(parent) && projection.MatchCount == 2 &&
            ReferenceEquals(projection.Rows[1], rows[1]),
            "Hierarchy filter matches both criteria and keeps ancestry, row identity and document order");
        Check(!projection.Visible(grandchild) && !projection.Visible(other) &&
            !new HierarchyFilter(rows[2].Label, null).Project(rows).Visible(grandchild),
            "Hierarchy filter omits unrelated descendants and does not search labels or GUID prefixes");
        Check(new HierarchyFilter("   ", null).Project(rows).Rows.SequenceEqual(rows) &&
            new HierarchyFilter(null, FakeDocument.Transform).Project(rows).Matches(grandchild),
            "Hierarchy filter shows unnamed nodes for component-only and blank searches");
        Check(new HierarchyFilter("Key", FakeDocument.CameraType).Project(rows).MatchCount == 0,
            "Hierarchy filter does not inherit a component from the parent");
        Check(!HierarchyFilter.ComponentChoices(doc.Schema).Any(choice => choice.Id == FakeDocument.CameraFog) &&
            HierarchyFilter.ComponentChoices(doc.Schema).Any(choice => choice.Id == FakeDocument.CameraType),
            "Hierarchy component choices include known components and omit nested structs");

        var kindAddress = new SceneAddress(sibling, FakeDocument.Light, FakeDocument.Kind);
        doc.SubmitTransaction([new Atlantis.Gameplay.Authoring.SetProperty(kindAddress, PropertyValue.FromEnum(1))]);
        var kinds = SceneMarkers.ReadLightKinds(doc, tree)!;
        Check(kinds.Count == 2 && kinds[child] == HierarchyLightKind.Directional && kinds[sibling] == HierarchyLightKind.Point,
            "Light kind is read through the authoring enum schema and only for Light components");
        var points = new HierarchyFilter("light", FakeDocument.Light, HierarchyLightKind.Point);
        var pointRows = points.Project(SceneMarkers.Rows(tree, doc.Schema, kinds));
        Check(pointRows.Rows.Select(row => row.Node).SequenceEqual([parent, sibling]) &&
            pointRows.MatchingLights.Select(row => row.Node).SequenceEqual([sibling]) &&
            points.IncludesSelection(tree, [sibling], kinds) && !points.IncludesSelection(tree, [child, sibling], kinds),
            "Light kind composes with name and component; ancestors and hidden lights are excluded from selection");
        Check(new HierarchyFilter("missing", null, HierarchyLightKind.All).Project(rows).MatchingLights.Count == 0 &&
            new HierarchyFilter(null, FakeDocument.CameraType, HierarchyLightKind.Point).Project(rows).MatchingLights.Count == 0,
            "Light filter returns no match for conflicting criteria");
        var allLights = new HierarchyFilter(null, null, HierarchyLightKind.All);
        var both = allLights.Project(SceneMarkers.Rows(tree, doc.Schema, kinds));
        Check(both.SameMatchingLights(both.MatchingLights) && !both.SameMatchingLights(pointRows.MatchingLights) &&
            !allLights.EquivalentTo(points), "Light matching is ordered and scoped to the current filter");
        Change(new NodeNameChanged(sibling, ""));
        Check(new HierarchyFilter(null, null, HierarchyLightKind.Point).Project(SceneMarkers.Rows(tree, doc.Schema, kinds))
            .MatchingLights.Single().Name == "", "Unnamed lights match by their authoring kind");
        Change(new NodeNameChanged(sibling, "Fill light"));
        doc.Unreadable = kindAddress;
        var partial = allLights.Project(SceneMarkers.Rows(tree, doc.Schema, SceneMarkers.ReadLightKinds(doc, tree)));
        Check(!partial.LightKindsComplete && partial.MatchingLights.Count == 1 &&
            !partial.SameMatchingLights(partial.MatchingLights), "Partial light read preserves known results but refuses selection");
        doc.Unreadable = null;

        var selection = new Selection();
        selection.Set(new[] { child, grandchild });
        ulong generation = selection.Generation;
        Check(projection.Locate(rows, selection.Nodes)?.Node == grandchild && !filter.IncludesSelection(tree, selection.Nodes) &&
            selection.Generation == generation && selection.Nodes.SequenceEqual(new[] { child, grandchild }),
            "Hierarchy Locate prefers the first hidden selection without mutating membership");
        Check(projection.Locate(rows, new[] { sibling, child })?.Node == child &&
            projection.Locate(rows, Array.Empty<EntityGuid>()) is null && filter.IncludesSelection(tree, new[] { parent, child }),
            "Hierarchy Locate uses document order and treats context as visible");
        Check(filter.EquivalentTo(new HierarchyFilter("light", FakeDocument.Light)) &&
            !filter.EquivalentTo(new HierarchyFilter("light", FakeDocument.CameraType)),
            "Hierarchy intent changes only with an effective criterion");

        var anchor = new HierarchyScrollAnchor(rows, rows[2]);
        Change(new ParentChanged(grandchild, sibling));
        Check(anchor.Restore(Rows())?.Node == grandchild &&
            filter.Project(Rows()).Rows.Select(row => row.Node).SequenceEqual(new[] { parent, child, sibling }),
            "Hierarchy scroll anchor follows its live row across a reparent");
        Change(new NodeNameChanged(child, "Key"));
        Check(!filter.IncludesSelection(tree, new[] { child, sibling }), "Hierarchy worker match sees an external rename");
        Change(new Atlantis.Gameplay.Authoring.ComponentRemoved(sibling, FakeDocument.Light));
        Check(!filter.Project(Rows()).Rows.Any(), "Hierarchy projection drops a path when its last match is removed");
        Change(new Atlantis.Gameplay.Authoring.ComponentAdded(sibling, FakeDocument.Light));
        Check(filter.Project(Rows()).Matches(sibling), "Hierarchy projection follows component creation");
        var oldVisibilityRow = Rows().Single(row => row.Node == grandchild);
        Change(new NodeDestroyed(grandchild));
        Change(new NodeCreated(grandchild, (ulong)tree.Count));
        Check(ViewportMembership.Plan(tree, null, VisibilityAction.HideNode, [oldVisibilityRow]).Refusal is not null,
            "viewport action refuses a stale row even if its GUID was reused");
        Check(anchor.Restore(Rows())?.Node == child, "Hierarchy scroll anchors do not follow replacements with the same GUID");
        Change(new DocumentReopened(default));
        Check(anchor.Restore(Rows())?.Node == Rows()[0].Node, "Hierarchy scroll falls back to the top after a new document");
        Check(!allLights.Project(SceneMarkers.Rows(tree, doc.Schema, kinds)).SameMatchingLights(both.MatchingLights),
            "Light matching refuses stale occurrences even when node GUIDs and kinds still match");

        HierarchyRow[] Rows() => SceneMarkers.Rows(tree, doc.Schema).ToArray();
        void Change(Atlantis.Gameplay.Authoring.Event e) => tree.Apply(e, doc);
    }

    private sealed record HeldHierarchyReply(string Method, TaskCompletionSource Held, TaskCompletionSource Release)
    {
        public HeldHierarchyReply(string method) : this(method,
            new(TaskCreationOptions.RunContinuationsAsynchronously), new(TaskCreationOptions.RunContinuationsAsynchronously)) { }
    }

    private static int RunHierarchyFilterRace(string executable, string scene)
    {
        string work = RuntimeProcess.NewWorkDirectory();
        using var runtime = RuntimeProcess.Start(new RuntimeLaunch(Path.GetFullPath(executable), scene, null, work));
        SessionInfo host = runtime.WaitForSession(TimeSpan.FromSeconds(25));
        using var observer = RemoteSession.Connect(host);
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        string attach = Path.Combine(work, "hierarchy-relay.json");
        File.WriteAllText(attach, JsonSerializer.Serialize(new
        {
            protocol = SessionFile.Protocol, port = ((IPEndPoint)listener.LocalEndpoint).Port,
            token = host.Token, pid = host.Pid, scene = host.Scene.ToString()
        }));
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
        HeldHierarchyReply? hold = null;
        int submits = 0, applies = 0;
        Task relay = Relay();
        try
        {
            return AppBuilder.Configure(() => new SceneObjectUiApp(executable, scene, attach, CheckRace))
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
                if (method == "authoring.submitTransaction") Interlocked.Increment(ref submits);
                if (method == "authoring.applyTracked") Interlocked.Increment(ref applies);
                await forward.WriteLineAsync(request.AsMemory(), token);
                string reply = await read.ReadLineAsync(token) ?? throw new IOException("Runtime closed the Hierarchy relay.");
                if (Volatile.Read(ref hold) is { } gate && gate.Method == method &&
                    ReferenceEquals(Interlocked.CompareExchange(ref hold, null, gate), gate))
                {
                    gate.Held.SetResult();
                    await gate.Release.Task.WaitAsync(token);
                }
                await output.WriteLineAsync(reply.AsMemory(), token);
            }
        }

        async Task CheckRace(MainWindow window)
        {
            var list = NameUiField<ListBox>(window, "_entities")!;
            var search = NameUiField<TextBox>(window, "_hierarchySearch")!;
            var undo = NameUiField<Button>(window, "_undo")!;
            var notice = NameUiField<TextBlock>(window, "_editLine")!;
            await NameUiWait(() => list.ItemCount > 2, "Hierarchy opened");
            var worker = NameUiField<EditorWorker>(window, "_worker")!;
            if (!list.Items.OfType<HierarchyRow>().Any(row => row.LightKind == HierarchyLightKind.Point))
            {
                var lamp = new EntityGuid(Guid.NewGuid());
                var plan = SceneObjectPresets.Build(observer.Authoring.Schema, SceneObjectPreset.PointLight, lamp,
                    (ulong)observer.Authoring.ListNodes().Value.Count, null)!;
                observer.Authoring.SubmitTransaction(plan.Edit.Commands);
                Require(observer.Authoring.DrainFailures().Count == 0, "seed a point light through the shared authoring contract");
                await NameUiWait(() => list.Items.OfType<HierarchyRow>().Any(row => row.Node == lamp &&
                    row.LightKind == HierarchyLightKind.Point), "The added light kind is read on its new Hierarchy row");
            }
            await CheckLightSelection(window, list, undo);
            EntityGuid[] nodes = observer.Authoring.ListNodes().Value.Take(2).Select(node => node.Node).ToArray();
            observer.Authoring.SubmitTransaction([new SetNodeName(nodes[0], "Batch first"), new SetNodeName(nodes[1], "Batch second")]);
            Require(observer.Authoring.DrainFailures().Count == 0, "seed matching selection");
            await NameUiWait(() => list.Items.OfType<HierarchyRow>().Count(row => row.Name.StartsWith("Batch", StringComparison.Ordinal)) == 2,
                "matching rows arrived");
            HierarchyRow source = list.Items.OfType<HierarchyRow>().Single(row => row.Node == nodes[0]);
            list.SelectedItem = source;
            await NameUiWait(() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.Subject == source.Node,
                "single structure target");
            var duplicate = new HeldHierarchyReply("authoring.document");
            Volatile.Write(ref hold, duplicate);
            var copy = new TaskCompletionSource<EntityGuid?>(TaskCreationOptions.RunContinuationsAsynchronously);
            worker.DuplicateSubtree(source.Node, worker.MeshDocumentLifetime, (node, _) => copy.TrySetResult(node), source);
            await duplicate.Held.Task.WaitAsync(deadline.Token);
            search.Text = "first";
            duplicate.Release.SetResult();
            Require(await copy.Task.WaitAsync(deadline.Token) is null && submits == 0 && applies == 0 && !undo.IsEnabled,
                "a filter change refused the queued structure edit before mutation, history or Apply");

            search.Text = "Batch";
            list.SelectedItems!.Clear();
            foreach (HierarchyRow row in list.Items.OfType<HierarchyRow>()) list.SelectedItems!.Add(row);
            await NameUiWait(() => NameUiField<BatchInspectorSnapshot>(window, "_batchView") is { Writable: true },
                "guarded batch Inspector");
            var snapshot = NameUiField<BatchInspectorSnapshot>(window, "_batchView")!;
            CommonInspectorField field = snapshot.Fields.First(item => item.Field.Path == "Transform.localPosition");
            var value = PropertyValue.FromVec3(new(2, 4, 8));

            var changedQuery = new HeldHierarchyReply("authoring.document");
            Volatile.Write(ref hold, changedQuery);
            Require(worker.CommitBatch(snapshot, field, value), "queue the first batch");
            await changedQuery.Held.Task.WaitAsync(deadline.Token);
            search.Text = "first";
            await NameUiWait(() => list.ItemCount == 1, "the changed filter is visible before sending");
            changedQuery.Release.SetResult();
            await NameUiWait(() => notice.Text?.Contains("before retrying", StringComparison.Ordinal) == true,
                "the changed filter refused a queued batch");
            Require(submits == 0 && applies == 0 && !undo.IsEnabled, "changed filter produced no mutation, history or Apply");

            search.Text = "Batch";
            await NameUiWait(() => list.ItemCount == 2, "restore both matches");
            var outsideRename = new HeldHierarchyReply("authoring.document");
            Volatile.Write(ref hold, outsideRename);
            await NameUiWait(() => worker.CommitBatch(snapshot, field, value), "queue a second batch");
            await outsideRename.Held.Task.WaitAsync(deadline.Token);
            observer.Authoring.Submit(new SetNodeName(nodes[1], "External rename"));
            Require(observer.Authoring.DrainFailures().Count == 0 && list.ItemCount == 2,
                "peer rename is committed while the Editor still shows both results");
            outsideRename.Release.SetResult();
            await NameUiWait(() => list.ItemCount == 1 && notice.Text?.Contains("before retrying", StringComparison.Ordinal) == true,
                "the worker noticed the peer change");
            Require(submits == 0 && applies == 0 && !undo.IsEnabled, "external rename produced no Editor mutation, history or Apply");

            observer.Authoring.Submit(new SetNodeName(nodes[1], "Batch second"));
            Require(observer.Authoring.DrainFailures().Count == 0, "restore the second direct match");
            await NameUiWait(() => list.ItemCount == 2, "both selected targets are visible while filtered");
            await NameUiWait(() => NameUiField<BatchInspectorSnapshot>(window, "_batchView") is { Writable: true },
                "full guarded selection available while filtered");
            snapshot = NameUiField<BatchInspectorSnapshot>(window, "_batchView")!;
            field = snapshot.Fields.First(item => item.Field.Path == "Transform.localPosition");
            var submitted = new HeldHierarchyReply("authoring.submitTransaction");
            Volatile.Write(ref hold, submitted);
            await NameUiWait(() => worker.CommitBatch(snapshot, field, value), "queue an eligible batch");
            await submitted.Held.Task.WaitAsync(deadline.Token);
            search.Text = "missing";
            await NameUiWait(() => list.ItemCount == 0, "the filter changes while the sent reply is held");
            submitted.Release.SetResult();
            await NameUiWait(() => undo.IsEnabled && Volatile.Read(ref applies) == 1, "the sent edit settled once");
            Require(submits == 1 && applies == 1 && field.Targets.All(target => observer.Authoring.GetProperty(target.Address).Value == value),
                "a sent batch wrote the full selection and kept its one Undo and one Apply");
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => field.Targets.All(target => observer.Authoring.GetProperty(target.Address).Value == target.Value),
                "explicit Undo while filtered restored both values");
        }

        async Task CheckLightSelection(MainWindow window, ListBox list, Button undo)
        {
            var light = NameUiField<ComboBox>(window, "_hierarchyLight")!;
            var select = NameUiField<Button>(window, "_selectMatchingLights")!;
            var status = NameUiField<TextBlock>(window, "_hierarchyFilterStatus")!;
            var selection = NameUiField<Selection>(window, "_selection")!;
            var initial = selection.Generation;
            light.SelectedIndex = 3;
            await NameUiWait(() => select.IsEnabled && list.Items.OfType<HierarchyRow>()
                .Any(row => row.LightKind == HierarchyLightKind.Point), "Point light matches are visible");

            var oldRequest = new HeldHierarchyReply("authoring.documentState");
            Volatile.Write(ref hold, oldRequest);
            select.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await oldRequest.Held.Task.WaitAsync(deadline.Token);
            light.SelectedIndex = 2;
            oldRequest.Release.SetResult();
            await NameUiWait(() => status.Text?.Contains("try again", StringComparison.Ordinal) == true,
                "A delayed light selection is refused after changing the kind filter");
            Require(initial == selection.Generation && submits == 0 && applies == 0 && !undo.IsEnabled,
                "Stale select preserves selection and produces zero transaction, Apply and history");

            light.SelectedIndex = 3;
            var matches = list.Items.OfType<HierarchyRow>().Where(row => row.LightKind == HierarchyLightKind.Point).Select(row => row.Node).ToArray();
            Require(matches.Length > 0, "Current point filter has matches");
            select.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => selection.Nodes.SequenceEqual(matches), "Current light selection keeps Hierarchy order");
            Require(submits == 0 && applies == 0 && !undo.IsEnabled,
                "Successful light select produces zero transaction, Apply and history");

            list.SelectedItems!.Clear();
            list.SelectedItem = list.Items.OfType<HierarchyRow>().Single(row => row.Node == matches[0]);
            await NameUiWait(() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.Subject == matches[0],
                "The point light's Inspector is ready");
            var draft = NameUiField<TextBox>(window, "_nameBox")!;
            draft.Focus();
            draft.Text = "Uncommitted point-light name";
            initial = selection.Generation;
            select.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Require(status.Text?.Contains("field edit", StringComparison.Ordinal) == true &&
                draft.Text == "Uncommitted point-light name" && selection.Generation == initial && submits == 0 && applies == 0,
                "Select matching lights preserves the active draft and emits no transaction or Apply");
            NameUiKey(draft, Key.Escape);
            light.SelectedIndex = 0;
            list.SelectedItems!.Clear();
        }

        static void Require(bool condition, string label)
        {
            if (!condition) throw new InvalidOperationException(label);
            Console.WriteLine("PASS " + label);
        }
    }
}
