using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void BatchRenameChecks()
    {
        EntityGuid root = new(Guid.NewGuid()), child = new(Guid.NewGuid()), second = new(Guid.NewGuid());
        var tree = new DocumentTree();
        var fake = new FakeDocument();
        tree.Apply(new NodeCreated(root, 0), fake);
        tree.Apply(new NodeCreated(second, 1), fake);
        tree.Apply(new NodeCreated(child, 2), fake);
        tree.Apply(new ParentChanged(child, root), fake);
        tree.Apply(new NodeNameChanged(root, "Lamp 01"), fake);
        tree.Apply(new NodeNameChanged(child, "old child"), fake);
        tree.Apply(new NodeNameChanged(second, "other"), fake);
        NodeSummary[] roster = [new(root, null, "Lamp 01"), new(second, null, "other"), new(child, root, "old child")];
        var token = new DocumentToken(root, 4);
        var preview = BatchRenamePreview.Capture(token, HierarchyFilter.Empty, tree, roster, [second, child, root])!;
        Check(preview.Targets.Select(target => target.Node).SequenceEqual(new[] { root, child, second }),
            "number mixed root and child selection in depth-first order rather than document slots");
        var names = preview.Propose("Lamp")!;
        Check(!names[0].Changed && names[1].After == "Lamp 02" && names[2].After == "Lamp 03",
            "an unchanged row retains its ordinal without adding a write");
        Check(preview.Propose(new string('x', 252)) is not null &&
            preview.Propose(new string('x', 253)) is null &&
            preview.Propose("bad\nname") is null && preview.Propose("\ud800") is null &&
            preview.Propose("  ") is null && preview.Propose("灯 \"A\"")![1].After == "灯 \"A\" 02",
            "validate the exact UTF-8 suffix budget and preserve printable prefix input");
        Check(BatchRenamePreview.Capture(token, new HierarchyFilter("old child", null), tree, roster, [second, child]) is null,
            "hidden rows prevent a confirmable preview");
    }

    private static async Task CheckBatchRenameUi(MainWindow window, string evidence)
    {
        const BindingFlags hidden = BindingFlags.Instance | BindingFlags.NonPublic;
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        static T Modal<T>(BatchRenameWindow dialog, string name) where T : class =>
            (T)typeof(BatchRenameWindow).GetField(name, hidden)!.GetValue(dialog)!;
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        static void Verify(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            Console.WriteLine("PASS " + message);
        }

        var button = Field<Button>("_batchRename");
        await NameUiWait(() => Field<EditorWorker>("_worker")?.BatchRenameAvailable == true &&
            Field<RuntimeProcess>("_runtime") is { } running && File.Exists(running.SessionPath) &&
            Field<System.Collections.Generic.IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count > 0,
            "Bistro street authoring ready", 150);
        var runtime = Field<RuntimeProcess>("_runtime");
        File.WriteAllText(Path.Combine(evidence, "runtime-workdir.txt"), runtime.Launch.WorkDirectory);
        Verify(GetDpiForWindow(window.TryGetPlatformHandle()!.Handle) == 96 &&
            Field<ViewportHost>("_viewport").Window!.RuntimeChildIsHosted(), "Windows 100% and native embedded Runtime");
        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        var authoring = peer.Authoring;
        var info = authoring.Document()!;
        Verify(info is { Provenance: Provenance.HandAuthored, SupportsGuardedNameTransactions: true },
            "Bistro fixture is hand-authored and negotiates guarded name transactions");
        var sourceBefore = File.ReadAllBytes(info.SourcePath);
        File.WriteAllBytes(Path.Combine(evidence, "scene-before.scene.txt"), sourceBefore);
        var before = authoring.ListNodes().Value.ToArray();
        var lamps = before.Where(node => node.Name.StartsWith("paris_streetlight_", StringComparison.Ordinal)).ToArray();
        Verify(lamps.Length == 3, "three lamp subtrees from the pinned Bistro scene");
        var rootOrder = Field<System.Collections.Generic.IReadOnlyList<HierarchyRow>>("_hierarchyRows")
            .Where(row => lamps.Any(lamp => lamp.Node == row.Node)).Select(row => row.Node).ToArray();
        var search = Field<TextBox>("_hierarchySearch");
        search.Text = "paris_streetlight_";
        await NameUiWait(() => Field<HierarchyFilter>("_hierarchyFilter").Active, "Hierarchy search applied");
        var selection = Field<Selection>("_selection");
        selection.Set(rootOrder.Reverse().ToArray());
        typeof(MainWindow).GetMethod("PublishSelection", hidden)!.Invoke(window, [false]);
        await NameUiWait(() => button.IsEnabled, "batch rename action available");
        if (Environment.GetEnvironmentVariable("ATLANTIS_0141_RECOVERY_ONLY") == "1")
        {
            await VerifyApplyRecovery();
            return;
        }
        var started = authoring.DocumentState().Value!.Token;
        var projection = peer.Control.Status().SceneGeneration;
        search.Text = lamps[0].Name;
        await NameUiWait(() => Field<HierarchyProjection>("_hierarchyProjection").Rows.Count(row =>
            rootOrder.Contains(row.Node)) == 1, "search hides two selected lamps without shrinking selection");
        var dialog = await Open();
        var locate = Modal<Button>(dialog, "_reveal");
        Verify(locate.IsVisible && !Modal<Button>(dialog, "_confirm").IsEnabled && selection.Nodes.Count == 3,
            "hidden selection requires explicit reveal and keeps all three nodes");
        Verify(GetDpiForWindow(dialog.TryGetPlatformHandle()!.Handle) == 96, "native rename dialog is at Windows 100%");
        await NativeCapture(dialog, Path.Combine(evidence, "hidden-selection.png"));
        Click(locate);
        await Preview(dialog);
        await NativeCapture(dialog, Path.Combine(evidence, "ordered-preview.png"));
        Verify(!Field<HierarchyFilter>("_hierarchyFilter").Active, "Locate selection cleared the active filter");
        Click(Modal<Button>(dialog, "_cancel"));
        await NameUiWait(() => Field<BatchRenameWindow>("_batchRenameWindow") is null && button.IsEnabled,
            "Cancel closed the batch preview");
        Verify(authoring.DocumentState().Value!.Token == started && authoring.ListNodes().Value.SequenceEqual(before) &&
            peer.Control.Status().SceneGeneration == projection && !Field<Button>("_undo").IsEnabled,
            "Cancel left names, revision, projection and history intact");

        dialog = await Open();
        await Preview(dialog);
        var changed = lamps[1];
        authoring.SubmitTransaction([new SetNodeName(changed.Node, "External lamp rename")]);
        Verify(authoring.DrainFailures().Count == 0, "second client changed one preview target");
        var external = authoring.DocumentState().Value!.Token;
        Click(Modal<Button>(dialog, "_confirm"));
        await NameUiWait(() => Modal<TextBlock>(dialog, "_notice").Text!.Contains("after preview", StringComparison.Ordinal),
            "stale preview rejected with a refresh instruction");
        Verify(authoring.DocumentState().Value!.Token == external && peer.Control.Status().SceneGeneration == projection &&
            authoring.ListNodes().Value.SequenceEqual(before.Select(node => node.Node == changed.Node
                ? node with { Name = "External lamp rename" } : node)) && !Field<Button>("_undo").IsEnabled,
            "stale rename added zero writes, history and Apply");
        await NativeCapture(dialog, Path.Combine(evidence, "stale-refusal.png"));
        authoring.SubmitTransaction([new SetNodeName(changed.Node, changed.Name)]);
        Verify(authoring.DrainFailures().Count == 0, "second client restored the fixture baseline");
        Click(Modal<Button>(dialog, "_refresh"));
        await Preview(dialog);
        var fresh = authoring.DocumentState().Value!.Token;
        var scope = rootOrder.Select((id, i) => (id, name: $"Bistro Lamp {i + 1:D2}")).ToDictionary(p => p.id, p => p.name);
        var renamed = before.Select(node => scope.TryGetValue(node.Node, out var name) ? node with { Name = name } : node).ToArray();
        Click(Modal<Button>(dialog, "_confirm"));
        await NameUiWait(() => Field<BatchRenameWindow>("_batchRenameWindow") is null &&
            Field<Button>("_undo").IsEnabled && authoring.ListNodes().Value.SequenceEqual(renamed), "rename and one history entry settled");
        Verify(authoring.DocumentState().Value!.Token.DocumentRevision == fresh.DocumentRevision + 1,
            "one atomic rename revision and unchanged node GUIDs, parents and order");
        await AwaitProjection();
        Click(Field<Button>("_undo"));
        await NameUiWait(() => !Field<Button>("_undo").IsEnabled && Field<Button>("_redo").IsEnabled &&
            authoring.ListNodes().Value.SequenceEqual(before), "one Undo restored all three original names");
        await AwaitProjection();
        Click(Field<Button>("_redo"));
        await NameUiWait(() => Field<Button>("_undo").IsEnabled && !Field<Button>("_redo").IsEnabled &&
            authoring.ListNodes().Value.SequenceEqual(renamed), "one Redo reinstated the exact mapping");
        await AwaitProjection();
        Verify(authoring.DocumentState().Value!.Token.DocumentRevision == fresh.DocumentRevision + 3,
            "Undo and Redo each advanced exactly one revision");
        await NativeCapture(window, Path.Combine(evidence, "numbered-hierarchy.png"));
        var save = Field<Button>("_save");
        await NameUiWait(() => save.IsEnabled, "Save available for hand-authored Bistro");
        Click(save);
        await NameUiWait(() => !authoring.DocumentState().Value!.Dirty &&
            Field<TextBlock>("_editLine").Text?.StartsWith("Saved ", StringComparison.Ordinal) == true, "ordinary Save settled");
        File.Copy(info.SourcePath, Path.Combine(evidence, "scene-saved.scene.txt"), overwrite: true);
        var saved = authoring.DocumentState().Value!.Token;
        var reopen = Field<Button>("_reopen");
        await NameUiWait(() => reopen.IsEnabled && !(bool)typeof(MainWindow).GetField("_leaveInProgress", hidden)!.GetValue(window)!,
            "Save released the document navigation action");
        window.Activate();
        window.RaiseEvent(new KeyEventArgs
        {
            RoutedEvent = InputElement.KeyDownEvent, Key = Key.R, KeyModifiers = KeyModifiers.Control | KeyModifiers.Shift,
        });
        await NameUiWait(() => authoring.DocumentState().Value!.Token.DocumentId != saved.DocumentId ||
            Field<Window>("_leaveDialog")?.IsVisible == true, "Reopen settled or showed its existing confirmation");
        if (Field<Window>("_leaveDialog") is { } leave)
        {
            var discard = (Button)leave.GetType().GetField("_discard", hidden)!.GetValue(leave)!;
            await NameUiWait(() => discard.IsEnabled && discard.IsVisible, "Reopen Without Saving is available");
            Verify(!authoring.DocumentState().Value!.Dirty, "the reviewed source is already clean before confirming reopen");
            Click(discard);
        }
        await NameUiWait(() => authoring.DocumentState().Value!.Token.DocumentId != saved.DocumentId &&
            !Field<Button>("_undo").IsEnabled && !Field<Button>("_redo").IsEnabled, "ordinary Reopen replaced the document and cleared history");
        Verify(authoring.ListNodes().Value.SequenceEqual(renamed), "saved scene reopened with the same names, GUIDs, parents and slots");
        File.WriteAllLines(Path.Combine(evidence, "roster-reopened.jsonl"), authoring.ListNodes().Value
            .Select(row => Json.Write(Codec.Encode(row))));
        await NativeCapture(window, Path.Combine(evidence, "saved-reopened.png"));

        async Task VerifyApplyRecovery()
        {
            var meshNode = before.First(node => authoring.ListComponents(node.Node).Value.Any(type =>
                authoring.Schema.Any(schema => schema.Id == type && schema.ShortName == "Renderable")));
            var meshField = SceneSchema.AddressOf(authoring.Schema, meshNode.Node, "Renderable", "meshAsset")!.Value;
            var originalMesh = authoring.GetProperty(meshField).Value;
            var missingMesh = new AssetGuid(Guid.NewGuid());
            authoring.SubmitTransaction([new Atlantis.Gameplay.Authoring.SetProperty(meshField, PropertyValue.FromAsset(missingMesh))]);
            Verify(authoring.DrainFailures().Count == 0, "second client set an unresolved binding for Apply failure");
            selection.Set(rootOrder.Reverse().ToArray());
            typeof(MainWindow).GetMethod("PublishSelection", hidden)!.Invoke(window, [false]);
            await NameUiWait(() => button.IsEnabled, "batch action available for the recovery fixture");
            var dialog = await Open();
            await Preview(dialog, "Recovery Lamp");
            ulong previousProjection = peer.Control.Status().SceneGeneration;
            Click(Modal<Button>(dialog, "_confirm"));
            var retry = Field<Button>("_retryApply");
            await NameUiWait(() => Field<BatchRenameWindow>("_batchRenameWindow") is null &&
                Field<Button>("_undo").IsEnabled && retry.IsEnabled &&
                authoring.ApplyStatus().Value?.Request?.State == ApplyState.Failed, "rename settles and independent Apply fails");
            Verify(peer.Control.Status().SceneGeneration == previousProjection &&
                authoring.ListNodes().Value.Count(node => node.Name.StartsWith("Recovery Lamp ", StringComparison.Ordinal)) == 3,
                "failed Apply retains the original projection, all renamed values and one Undo");
            await NativeCapture(window, Path.Combine(evidence, "apply-failed.png"));
            var events = authoring.Subscribe(new Atlantis.Gameplay.Authoring.EventFilter(Atlantis.Gameplay.Authoring.EventKind.AllWithSiblingMove));
            authoring.SubmitTransaction([new Atlantis.Gameplay.Authoring.SetProperty(meshField, originalMesh)]);
            Verify(authoring.DrainFailures().Count == 0, "second client repaired the binding");
            var repaired = authoring.DocumentState().Value!.Token;
            Click(retry);
            await AwaitProjection();
            Verify(authoring.DocumentState().Value!.Token == repaired &&
                authoring.DrainEvents(events).Value.All(e => e is not NodeNameChanged) && Field<Button>("_undo").IsEnabled,
                "explicit Apply repaired projection without resubmitting any name write");

            authoring.SubmitTransaction([new SetNodeName(rootOrder[0], "External conflict")]);
            Verify(authoring.DrainFailures().Count == 0, "second client superseded one recorded name");
            var conflictedRoster = authoring.ListNodes().Value.ToArray();
            var conflictToken = authoring.DocumentState().Value!.Token;
            ulong conflictProjection = peer.Control.Status().SceneGeneration;
            Click(Field<Button>("_undo"));
            await NameUiWait(() => !Field<Button>("_undo").IsEnabled && !Field<Button>("_redo").IsEnabled &&
                Field<TextBlock>("_editLine").Text?.Contains("changed elsewhere", StringComparison.Ordinal) == true,
                "conflicted Undo reports its refusal and drops the history entry");
            Verify(authoring.DocumentState().Value!.Token == conflictToken &&
                authoring.ListNodes().Value.SequenceEqual(conflictedRoster) && peer.Control.Status().SceneGeneration == conflictProjection,
                "conflicted Undo made no partial rollback or Apply");
        }

        async Task<BatchRenameWindow> Open()
        {
            Click(button);
            await NameUiWait(() => Field<BatchRenameWindow>("_batchRenameWindow")?.IsVisible == true, "native rename dialog open");
            return Field<BatchRenameWindow>("_batchRenameWindow");
        }

        async Task Preview(BatchRenameWindow present, string prefix = "Bistro Lamp")
        {
            Modal<TextBox>(present, "_prefix").Text = prefix;
            await NameUiWait(() => Modal<Button>(present, "_confirm").IsEnabled, "three reviewed rows ready");
            var rows = Modal<ListBox>(present, "_rows").Items.OfType<BatchRenameRow>().ToArray();
            Verify(rows.Select(row => row.Target.Node).SequenceEqual(rootOrder) &&
                rows.Select(row => row.After).SequenceEqual(new[] { prefix + " 01", prefix + " 02", prefix + " 03" }),
                "preview pinned unfiltered depth-first order and every numbered name");
        }

        async Task AwaitProjection()
        {
            await NameUiWait(() => authoring.ApplyStatus().Value?.Projection?.Receipt.DocumentRevision ==
                authoring.DocumentState().Value!.Token.DocumentRevision, "Apply projected the verified revision", 75);
        }
    }
}
