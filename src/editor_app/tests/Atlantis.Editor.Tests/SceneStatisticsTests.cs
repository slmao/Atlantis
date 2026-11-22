using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Json = Atlantis.Gameplay.Remote.Json;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void SceneStatisticsChecks()
    {
        var root = new EntityGuid(Guid.NewGuid());
        var child = new EntityGuid(Guid.NewGuid());
        var other = new EntityGuid(Guid.NewGuid());
        var nodes = new NodeSummary[] { new(root, null, "store"), new(other, null), new(child, root, "awning") };
        Check(SceneStatistics.SelectScope(nodes, root)!.Select(row => row.Node).SequenceEqual(new[] { root, child }),
            "statistics includes the selected root and its descendants regardless of Hierarchy display");
        Check(SceneStatistics.SelectScope([.. nodes, nodes[0]], root) is null, "invalid roster cannot settle subtree scope");

        var mesh = new AssetGuid(Guid.NewGuid());
        var material = new AssetGuid(Guid.NewGuid());
        var texture = new AssetGuid(Guid.NewGuid());
        var missing = new AssetGuid(Guid.NewGuid());
        var stats = new SceneStatistics
        {
            Catalog = new System.Collections.Generic.Dictionary<AssetGuid, AssetType>
            {
                [mesh] = AssetType.Mesh, [material] = AssetType.Material, [texture] = AssetType.Texture,
            },
        };
        var meshAddress = new SceneAddress(root, new(1), new(2));
        var materialAddress = new SceneAddress(root, new(1), new(3));
        var identity = new object();
        stats.Renderable(nodes[0], meshAddress, PropertyValue.FromAsset(mesh),
            materialAddress, PropertyValue.FromAsset(material), identity, identity);
        stats.Renderable(nodes[2], meshAddress with { Node = child }, PropertyValue.FromAsset(mesh),
            materialAddress with { Node = child }, PropertyValue.FromAsset(material), identity, identity);
        stats.Renderable(nodes[1], meshAddress with { Node = other }, PropertyValue.FromAsset(missing),
            materialAddress with { Node = other }, null, identity, identity);
        stats.Material(material, new(material, AssetType.Material, "", 1, null, "", [texture, texture]));
        stats.Metric(mesh, new MeshContentMetrics(5, 12));
        stats.Metric(texture, new TextureContentMetrics(3, 4, 4, ContentTextureLayout.Rgba8, 1, 64));
        var report = stats.Report("Whole scene", null, null, 3, null);
        Check(report.Triangles == 24 && report.TextureBytes == 64 && report.Textures == 1 && report.DrawItems == 2,
            "statistics counts geometry per instance and texture payload once per GUID");
        Check(!report.ReferencesComplete && report.Missing.Single() is { CanLocate: true } row &&
            row.Address.Node == other && row.Asset == missing && row.FieldName == "meshAsset",
            "incomplete statistics retains the proven missing field without converting an unread binding into a diagnosis");
    }

    private static async Task CheckSceneStatisticsUi(MainWindow window, string evidence)
    {
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        static void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            Console.WriteLine("PASS " + message);
        }
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        static void Menu(MenuItem item) => item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } process && File.Exists(process.SessionPath) &&
            Field<Button>("_bistroViews").IsEnabled, "Bistro document and native viewport", 120);
        var runtime = Field<RuntimeProcess>("_runtime");
        File.WriteAllText(Path.Combine(evidence, "runtime-workdir.txt"), runtime.Launch.WorkDirectory);
        Require(GetDpiForWindow(window.TryGetPlatformHandle()!.Handle) == 96 &&
            Field<ViewportHost>("_viewport").Window!.RuntimeChildIsHosted(), "Windows 100% and embedded Runtime");
        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        var roster = peer.Authoring.ListNodes().Value;
        var storefront = roster.Single(row => row.Node == new EntityGuid(Guid.Parse("64f94690-88e3-8c07-bcef-495779976847")));
        var subtree = SceneStatistics.SelectScope(roster, storefront.Node)!;
        var capture = Field<MenuItem>("_bistroCapture");
        if (Field<MenuItem>("_bistroEnable").IsVisible) Menu(Field<MenuItem>("_bistroEnable"));
        Menu(Field<MenuItem>("_bistroStreet"));
        await NameUiWait(() => capture.IsEnabled, "existing street camera settled", 45);
        Menu(capture);
        await NameUiWait(() => Field<BistroReferenceWindow>("_bistroCompare")?.Latest is not null, "existing frame sample", 60);
        var comparison = Field<BistroReferenceWindow>("_bistroCompare");
        var sample = comparison.Latest!;
        File.WriteAllText(Path.Combine(evidence, "existing-frame.json"), Json.Write(Codec.Encode(sample.Report)));
        comparison.Hide();

        Click(Field<Button>("_sceneStatisticsButton"));
        var stats = Field<SceneStatisticsWindow>("_sceneStatistics");
        T View<T>(string name) where T : class =>
            (T)typeof(SceneStatisticsWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(stats)!;
        var refresh = View<Button>("_refresh");
        var frame = View<TextBlock>("_frame");
        var sessionState = peer.Authoring.DocumentState().Value;
        bool undo = Field<Button>("_undo").IsEnabled;
        bool redo = Field<Button>("_redo").IsEnabled;
        var whole = await Refresh("street", complete: true);
        Require(whole.Nodes == 5969 && whole.DrawItems == 2909, "Bistro full street authored cardinality");
        Require(frame.Text!.Contains($"frame {sample.Report.Frame:N0}") &&
            frame.Text.Contains($"projection {sample.Status.SceneGeneration}") &&
            frame.Text.Contains($"{sample.Report.Data.DrawItemCount:N0} draw items"), "historical full-scene frame provenance");
        typeof(MainWindow).GetMethod("SetSelection", BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(window, [storefront.Node, true]);
        View<ComboBox>("_scope").SelectedIndex = 1;
        var selected = await Refresh("storefront", complete: true);
        Require(selected.Nodes == (ulong)subtree.Length && selected.DrawItems < whole.DrawItems &&
            selected.Missing.Count == 0, "named storefront subtree and healthy reference check");
        Require(peer.Authoring.DocumentState().Value == sessionState &&
            peer.Control.Status().SceneGeneration == sample.Status.SceneGeneration &&
            Field<Button>("_undo").IsEnabled == undo && Field<Button>("_redo").IsEnabled == redo &&
            ReferenceEquals(sample, comparison.Latest), "statistics preserves document, projection, history and the existing capture");

        var target = subtree.First(row => peer.Authoring.GetProperty(
            SceneSchema.AddressOf(peer.Authoring.Schema, row.Node, "Renderable", "materialAsset")!.Value) is { IsOk: true, Value: { IsAbsent: false } value } && value.TryGetAsset(out _));
        var address = SceneSchema.AddressOf(peer.Authoring.Schema, target.Node, "Renderable", "materialAsset")!.Value;
        var original = peer.Authoring.GetProperty(address)!.Value;
        var absent = new AssetGuid(Guid.Parse("01400140-0140-4140-8140-014001400001"));
        var failed = await Apply(PropertyValue.FromAsset(absent), ApplyState.Failed);
        Require(peer.Control.Status().SceneGeneration == sample.Status.SceneGeneration && failed.Error is not null,
            "failed Apply keeps the old Runtime projection");
        // Whole-scene intent remains anchored when Locate selects a child of the storefront.
        View<ComboBox>("_scope").SelectedIndex = 0;
        var invalid = await Refresh("missing-binding", complete: false);
        Require(invalid.ReferencesComplete && invalid.Missing.Single().Address == address &&
            invalid.Missing.Single().Asset == absent, "missing direct binding points to the exact authored field");
        var locate = View<Button>("_locate");
        Require(locate.IsEnabled, "proven stable field is actionable");
        Click(locate);
        await NameUiWait(() => Field<StackPanel>("_inspectorPanel").GetVisualDescendants().OfType<Button>()
            .Any(button => button.IsFocused && button.DataContext is SceneAddress field && field == address),
            "Inspector focus on the confirmed missing material field", 30);
        await NativeCapture(window, Path.Combine(evidence, "located-inspector.png"));

        var repaired = await Apply(original, ApplyState.Applied);
        Require(repaired.Generation > sample.Status.SceneGeneration, "explicit Apply activated the repaired projection");
        await NameUiWait(() => !locate.IsEnabled, "old diagnostic disabled after external repair");
        var healthy = await Refresh("repaired", complete: true);
        Require(healthy.Missing.Count == 0 && healthy.ReferencesComplete &&
            frame.Text!.Contains("earlier projection"), "refresh clears the corrected issue and retains historical frame labeling");

        async Task<SceneStatisticsReport> Refresh(string name, bool complete)
        {
            Click(refresh);
            await NameUiWait(() => Field<SceneStatisticsReport>("_sceneStatisticsReport") is not null ||
                View<TextBlock>("_notice").Text?.Contains("budget expired") == true, name + " statistics settled", 35);
            var report = Field<SceneStatisticsReport>("_sceneStatisticsReport") ??
                throw new InvalidOperationException(View<TextBlock>("_notice").Text);
            File.WriteAllText(Path.Combine(evidence, name + ".json"),
                JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
            if (complete)
                Require(report.DrawComplete && report.TrianglesComplete && report.TexturesComplete && report.ReferencesComplete &&
                    report.Missing.Count == 0, name + " complete: " + string.Join(" ", report.Reasons));
            Require(GetDpiForWindow(stats.TryGetPlatformHandle()!.Handle) == 96, name + " native 96 DPI");
            await NativeCapture(stats, Path.Combine(evidence, name + ".png"));
            return report;
        }

        async Task<ApplyRecord> Apply(PropertyValue value, ApplyState expected)
        {
            peer.Authoring.SubmitTransaction([new Atlantis.Gameplay.Authoring.SetProperty(address, value)]);
            Require(peer.Authoring.DrainFailures().Count == 0, "ordinary authoring commit");
            var sent = peer.Authoring.ApplyTracked();
            Require(sent.IsOk, "explicit Apply accepted");
            ApplyRecord? request = null;
            await NameUiWait(() => (request = peer.Authoring.ApplyStatus(sent.Value!.Id).Value?.Request)?.State == expected,
                "Apply settles " + expected, 60);
            return request!;
        }
    }
}
