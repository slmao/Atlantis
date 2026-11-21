using System;
using System.Collections.Generic;
using System.Diagnostics;
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
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static async Task CheckViewportPlacementUi(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        const BindingFlags privateInstance = BindingFlags.Instance | BindingFlags.NonPublic;
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        void Verify(string name, bool valid, string detail = "")
        {
            checks.Add(new(name, valid, detail));
            if (!valid) throw new InvalidOperationException(name + " " + detail);
        }
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));

        Field<TabControl>("_assetTabs").SelectedIndex = 1;
        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } runtime && File.Exists(runtime.SessionPath) &&
            Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count > 0 &&
            Field<AssetBrowser>("_assetBrowser").State == "Ready", "hosted placement scene and catalog", 180);
        var host = Field<ViewportHost>("_viewport");
        var view = host.Window!;
        var runtime = Field<RuntimeProcess>("_runtime");
        work(runtime.Launch.WorkDirectory);
        nint hwnd = window.TryGetPlatformHandle()!.Handle;
        window.Activate();
        await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground Editor for scene placement");
        Verify("native Editor at 96 DPI with a hosted Runtime drawable",
            GetDpiForWindow(hwnd) == 96 && view.RuntimeChildIsHosted());
        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        var document = new RemoteAuthoringDocument(peer.Authoring, peer);
        var info = document.Info()!;
        string? disposable = Environment.GetEnvironmentVariable("ATLANTIS_0128_DISPOSABLE_SCENE");
        bool persistence = disposable is not null;
        if (persistence && !Path.GetFullPath(disposable!).Equals(Path.GetFullPath(info.SourcePath), StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The write acceptance requires its explicitly named disposable scene.");
        Verify("the Editor uses a hand-authored document", info.Provenance == Provenance.HandAuthored);
        var original = document.ListNodes().Select(n => n.Node).ToHashSet();
        ulong originalRevision = document.ApplyStatus().Value!.CurrentDocumentRevision!.Value;
        ulong originalProjection = peer.Control.Status().SceneGeneration;
        bool Unchanged() => document.ListNodes().Count == original.Count &&
            document.ApplyStatus().Value?.CurrentDocumentRevision == originalRevision &&
            peer.Control.Status().SceneGeneration == originalProjection && !Field<Button>("_undo").IsEnabled;

        string? requiredMesh = Environment.GetEnvironmentVariable("ATLANTIS_0128_MESH");
        var mesh = Field<AssetBrowser>("_assetBrowser").Items.First(asset => asset.Type == AssetType.Mesh &&
            (requiredMesh is null || asset.Guid.ToString().Equals(requiredMesh, StringComparison.OrdinalIgnoreCase)));
        Field<TextBox>("_assetSearch").Text = mesh.Guid.ToString();
        var list = Field<ListBox>("_assetsList");
        var projection = typeof(MainWindow).GetMethod("ViewportProjection", privateInstance)!;
        await NameUiWait(() => projection.Invoke(window, [view]) is CameraMatrices, "placement projection");
        (PickRay ray, RayHit? hit) RayAt(int x, int y)
        {
            var camera = (CameraMatrices)projection.Invoke(window, [view])!;
            var ray = CameraConvention.RayThroughNdc(camera, (float)x / view.Width * 2 - 1, (float)y / view.Height * 2 - 1);
            return (ray, peer.Control.Raycast(new Ray(ray.Origin, ray.Direction)).Hit);
        }
        string? requiredFloor = Environment.GetEnvironmentVariable("ATLANTIS_0128_SURFACE");
        var (left, top, width, height) = view.ScreenRect();
        PixelPoint nativeSurface = default, nativeMiss = default;
        RayHit? floor = null;
        PickRay floorRay = default;
        foreach (int y in new[] { height * 3 / 4, height * 2 / 3, height * 4 / 5, height / 2, height * 9 / 10 })
        {
            foreach (int x in new[] { width * 4 / 5, width / 2, width * 2 / 5, width * 3 / 5, width / 3, width * 2 / 3 })
            {
                var hit = RayAt(x, y);
                if (hit.hit is null || requiredFloor is not null &&
                    !hit.hit.Entity.ToString().Equals(requiredFloor, StringComparison.OrdinalIgnoreCase)) continue;
                floor = hit.hit;
                floorRay = hit.ray;
                nativeSurface = new(left + x, top + y);
                break;
            }
            if (floor is not null) break;
        }
        if (floor is null) throw new InvalidOperationException("No expected unoccluded terrace surface in the acceptance samples.");
        foreach (int x in new[] { width / 2, width / 4, width * 3 / 4, width / 10, width * 9 / 10 })
        {
            if (RayAt(x, 6).hit is not null) continue;
            nativeMiss = new(left + x, top + 6);
            break;
        }
        if (nativeMiss == default) throw new InvalidOperationException("No sky point in the acceptance samples.");
        Vector3 expected = floorRay.Origin + floorRay.Direction * floor.Distance;

        async Task Begin(PixelPoint target)
        {
            await NameUiWait(() => list.GetVisualDescendants().OfType<ListBoxItem>().Any(IsMeshRow), "realized mesh asset");
            var row = list.GetVisualDescendants().OfType<ListBoxItem>().Single(IsMeshRow);
            row.BringIntoView();
            await Task.Delay(220);
            PixelPoint from = row.PointToScreen(new Point(row.Bounds.Width / 2, row.Bounds.Height / 2));
            await Task.Run(() =>
            {
                if (GetForegroundWindow() != hwnd || !SetCursorPos(from.X, from.Y))
                    throw new InvalidOperationException("Native pointer unavailable.");
                Thread.Sleep(80);
                mouse_event(2, 0, 0, 0, 0);
                SetCursorPos(from.X + 9, from.Y);
                Thread.Sleep(145);
                for (int move = 1; move <= 16; ++move)
                {
                    SetCursorPos(from.X + (target.X - from.X) * move / 16, from.Y + (target.Y - from.Y) * move / 16);
                    Thread.Sleep(43);
                }
            });
            bool IsMeshRow(ListBoxItem value) => value is { IsVisible: true, DataContext: AssetSummary asset } && asset.Guid == mesh.Guid;
        }
        static void Release() => mouse_event(4, 0, 0, 0, 0);
        Task WaitMarker() => NameUiWait(() => Field<MeshPlacementHit>("_viewportPlacementHit") is { } hit &&
            hit.Sample.X == nativeSurface.X - left && hit.Sample.Y == nativeSurface.Y - top, "current native surface marker", 35);
        async Task Capture(string name)
        {
            if (Environment.GetEnvironmentVariable("ATLANTIS_0128_CAPTURE") is not { } script) return;
            var start = new ProcessStartInfo("py") { UseShellExecute = false, CreateNoWindow = true };
            foreach (string argument in new[] { "-3", script, Environment.ProcessId.ToString(),
                Path.Combine(runtime.Launch.WorkDirectory, name + ".png"), window.Title ?? "" }) start.ArgumentList.Add(argument);
            await Task.Run(() =>
            {
                using var process = Process.Start(start)!;
                if (!process.WaitForExit(15000) || process.ExitCode != 0) throw new InvalidOperationException("Native screenshot failed: " + name);
            });
        }

        try
        {
            await Begin(nativeMiss);
            await NameUiWait(() => Field<TextBlock>("_viewportPlacementFeedback").Text?.Contains("No mesh surface") == true, "native miss feedback", 35);
            Release();
            await Task.Delay(400);
            Verify("native release in the sky does not change document, history or Apply", Unchanged());

            await Begin(nativeSurface);
            await WaitMarker();
            keybd_event(27, 0, 0, 0);
            keybd_event(27, 0, 2, 0);
            Release();
            await Task.Delay(420);
            Verify("Escape clears the marker and never commits", Field<MeshPlacementHit>("_viewportPlacementHit") is null && Unchanged());

            await Begin(nativeSurface);
            await WaitMarker();
            double priorWidth = window.Width;
            window.Width = priorWidth - 64;
            await NameUiWait(() => view.Width != width && Field<MeshPlacementHit>("_viewportPlacementHit") is null, "native resize expires marker");
            Release();
            await Task.Delay(600);
            Verify("release after the drawable changed is refused, including history and Apply", Unchanged());
            window.Width = priorWidth;
            await NameUiWait(() => view.ScreenRect() == (left, top, width, height), "original host extent restored");

            await Begin(nativeSurface);
            await WaitMarker();
            var advertised = Field<MeshPlacementHit>("_viewportPlacementHit");
            Verify("preview point matches the independently verified world hit and leaves document unchanged",
                advertised.Surface == floor.Entity && advertised.Distance == floor.Distance && advertised.Position == expected && Unchanged(),
                $"floor {floor.Entity}; mesh {mesh.Guid}; position {expected}; screen {nativeSurface}");
            await Capture("0128-preview");
            Release();
            await NameUiWait(() => document.ListNodes().Count == original.Count + 1 && Field<Button>("_undo").IsEnabled,
                "one verified creation", 50);
            await NameUiWait(() => peer.Control.Status().SceneGeneration > originalProjection, "placement applied", 50);
            var added = document.ListNodes().Single(n => !original.Contains(n.Node));
            var meshAddress = SceneSchema.AddressOf(document.Schema, added.Node, "Renderable", "meshAsset")!.Value;
            var material = SceneSchema.AddressOf(document.Schema, added.Node, "Renderable", "materialAsset")!.Value;
            bool MatchesPlacement() => TransformMath.LocalOf(document, added.Node) ==
                new LocalTransform(expected, Vector3.Zero, Vector3.One) &&
                document.GetProperty(meshAddress)?.AsAsset() == mesh.Guid && document.GetProperty(material)?.IsAbsent == true;
            Verify("one unnamed root with exact placement, selected mesh and fallback material",
                added.Parent is null && added.Name.Length == 0 && MatchesPlacement(), added.Node.ToString());
            await Capture("0128-placed");

            Click(Field<Button>("_undo"));
            await NameUiWait(() => document.ListNodes().Count == original.Count && Field<Button>("_redo").IsEnabled, "one undo removes the chair", 50);
            Click(Field<Button>("_redo"));
            await NameUiWait(() => document.ListNodes().Any(n => n.Node == added.Node) && Field<Button>("_undo").IsEnabled, "one redo restores the same chair", 50);
            Verify("one undo then one redo restores the same GUID and exact placement", MatchesPlacement());
            if (!persistence) return;
            await NameUiWait(() => document.ApplyStatus().Value?.Request?.State == ApplyState.Applied, "redo Apply settled", 50);

            Click(Field<Button>("_save"));
            await NameUiWait(() => File.ReadAllText(disposable!).Contains(added.Node.ToString(), StringComparison.OrdinalIgnoreCase),
                "Save persists the chair GUID to the disposable source", 50);
            long openBefore = Field<EditorWorker>("_worker").MeshDocumentLifetime;
            Click(Field<Button>("_reopen"));
            await NameUiWait(() => Field<EditorWorker>("_worker").MeshDocumentLifetime != openBefore &&
                document.ListNodes().Any(n => n.Node == added.Node), "reopen same saved scene", 60);
            ulong reopenedGeneration = peer.Control.Status().SceneGeneration;
            Click(Field<Button>("_retryApply"));
            await NameUiWait(() => peer.Control.Status().SceneGeneration > reopenedGeneration, "explicit Apply after reopen", 50);
            Verify("Save/reopen keeps same GUID, placed root and exact mesh/position", MatchesPlacement());
            await Capture("0128-reopened");
        }
        finally { Release(); }
    }
}
