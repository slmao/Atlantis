using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Assets;
using Avalonia.Automation;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void BistroReferenceChecks()
    {
        foreach (var preset in new[] { BistroReferenceView.Street, BistroReferenceView.Terrace })
        {
            var pose = preset.Pose();
            var camera = new EditorCamera();
            camera.SeedNavigation(pose);
            var at = CameraConvention.Matrices(camera.Pose(), 960f / 540)!;
            Check(preset.Matches(at.View, at.Projection), preset.Name + " reproduces the frozen camera and lens");
            camera.Dolly(1);
            var away = CameraConvention.Matrices(camera.Pose(), 960f / 540)!;
            Check(!preset.Matches(away.View, away.Projection), preset.Name + " rejects a navigation excursion");
            var wrongAspect = CameraConvention.Matrices(pose, 1)!;
            Check(!preset.Matches(wrongAspect.View, wrongAspect.Projection), preset.Name + " rejects a different aspect");
        }
        var data = new FrameData([], [], new float[16], new float[16], 0);
        Check(Codec.DecodeFrameData(Codec.Encode(data)) is { ExposureCompensationEv: null }, "legacy frame without exposure");
        Check(Codec.DecodeFrameData(Codec.Encode(data with { ExposureCompensationEv = 0 }))?.ExposureCompensationEv == 0,
            "frame preserves measured zero EV");
        foreach (string value in new[] { "null", "\"nan\"", "\"inf\"", "\"0\"", "1e40" })
            Check(Codec.DecodeFrameData(Codec.Encode(data).Set("exposureCompensationEv", Json.Parse(value))) is null,
                "frame rejects malformed exposure " + value);
        BistroProjectionChecks();
    }

    private static void BistroProjectionChecks()
    {
        var document = new DocumentToken(new(Guid.NewGuid()), 12);
        var camera = new EntityGuid(Guid.NewGuid());
        var receipt = new ApplyReceipt(new(Guid.NewGuid()), document.DocumentRevision);
        var view = new ViewportStamp(5, 1);
        var snapshot = new ApplySnapshot(document.DocumentRevision, new(receipt, ApplyState.Applied, 5),
            new(receipt, 5));
        var proof = BistroProjection.Confirm(document, camera, snapshot, view);
        Check(proof is not null, "Bistro pair accepts a confirmed current projection");
        Check(BistroProjection.Confirm(document, camera, null, view) is null, "Bistro pair requires projection evidence");
        Check(BistroProjection.Confirm(document with { DocumentRevision = 13 }, camera, snapshot, view) is null,
            "Bistro pair refuses a newer commit on the previous projection");
        Check(BistroProjection.Confirm(document, camera, snapshot, view with { SceneGeneration = 6 }) is null,
            "Bistro pair refuses the wrong Runtime generation");
        foreach (var stopped in new[] { ApplyState.Failed, ApplyState.Pending, ApplyState.Superseded })
            Check(BistroProjection.Confirm(document, camera, snapshot with { Request = new(receipt, stopped) }, view) is null,
                "Bistro pair refuses " + stopped);
        var next = proof! with { Document = document with { DocumentRevision = 13 }, Viewport = new(6, 2) };
        Check(next.Follows(proof!), "Bistro After accepts a later revision at the same active camera");
        Check(!proof!.Follows(proof) && !(next with { Camera = new(Guid.NewGuid()) }).Follows(proof) &&
            !(next with { Document = next.Document with { DocumentId = new(Guid.NewGuid()) } }).Follows(proof),
            "Bistro After refuses the same projection or another camera or document");
    }

    private static int RunBistroLocator(string runtimePath, string scene, string evidence)
    {
        Directory.CreateDirectory(evidence);
        using var runtime = RuntimeProcess.Start(new(runtimePath, scene, null, evidence));
        var session = runtime.WaitForSession(TimeSpan.FromSeconds(25));
        using var peer = RemoteSession.Connect(session);
        var posted = new ConcurrentQueue<Action>();
        using var log = new EditorLog(Path.Combine(evidence, "editor.log"));
        using var worker = new EditorWorker(session, posted.Enqueue, log);
        HierarchySnapshot? tree = null;
        bool switched = false;
        worker.HierarchyChanged += snapshot => tree = snapshot;
        worker.LeaveDocumentEvents += events => switched |= events.Any(e => e is ActiveCameraChanged);
        worker.Start();

        void Pump() { while (posted.TryDequeue(out var action)) action(); }
        DocumentToken ReadWorkerDocument() => worker.ReviewDocument()
            .WaitAsync(TimeSpan.FromSeconds(25)).GetAwaiter().GetResult().State?.Token ??
            throw new InvalidOperationException("Worker could not verify the test document.");
        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            Console.WriteLine("PASS " + message);
        }

        var originalDocument = ReadWorkerDocument();
        Pump();
        var original = peer.Authoring.ActiveCamera().Value!.Value;
        var alternate = new EntityGuid(Guid.Parse("01370137-0137-4137-8137-013700000010"));
        var rows = tree!.Rows;
        var originalRow = rows.Single(row => row.Node == original);
        EntityGuid selection = rows.First(row => row.Node != original && row.Node != alternate).Node;
        worker.ShowSelection([selection], 1);
        HierarchyRow? located = null;
        string? reason = null;
        bool delivered = false;
        void Receive(HierarchyRow? row, string? error)
        {
            delivered = true;
            located = row;
            reason = error;
        }

        worker.LocateBistroCamera(rows, 1, 0, Receive);
        // ReviewDocument is queued behind Locate and drains authoring events on the Worker.
        // Keep all UI callbacks queued while changing the active camera through the peer.
        Require(ReadWorkerDocument() == originalDocument && !delivered, "A's locator response is pending UI delivery");
        peer.Authoring.SubmitTransaction([new SetActiveCamera(alternate)]);
        Require(peer.Authoring.DrainFailures().Count == 0, "peer changed the active camera to B");
        var changed = ReadWorkerDocument();
        Require(changed.DocumentId == originalDocument.DocumentId &&
            changed.DocumentRevision > originalDocument.DocumentRevision &&
            peer.Authoring.ActiveCamera().Value == alternate, "Worker observed the newer document before UI delivery");
        Pump();
        Require(switched && tree!.Rows.Any(row => row.Node == original &&
            ReferenceEquals(row.Incarnation, originalRow.Incarnation)), "A's visible row and incarnation still match");
        Require(delivered && located is null && reason?.Contains("again", StringComparison.Ordinal) == true,
            "delayed A result returns a retry diagnostic without a selectable row");

        delivered = false;
        worker.LocateBistroCamera(tree!.Rows, 1, 0, Receive);
        Require(ReadWorkerDocument() == changed, "retry observes the current document");
        Pump();
        Require(delivered && located?.Node == alternate && reason is null, "fresh locator resolves B");
        return 0;
    }

    private static int RunBistroReferenceUi(string runtime, string scene, string evidence,
        bool tuning = false, bool materialBinding = false)
    {
        Directory.CreateDirectory(evidence);
        return AppBuilder.Configure(() => new BistroReferenceApp(runtime, scene, evidence, tuning, materialBinding)).UsePlatformDetect()
            .StartWithClassicDesktopLifetime([]);
    }

    private sealed class BistroReferenceApp(string runtime, string scene, string evidence, bool tuning, bool materialBinding) : Application
    {
        public override void Initialize()
        {
            Styles.Add(new FluentTheme());
            EditorVisuals.Install(this);
        }

        public override void OnFrameworkInitializationCompleted()
        {
            var desktop = (IClassicDesktopStyleApplicationLifetime)ApplicationLifetime!;
            var window = new MainWindow(new EditorOptions(runtime, scene) { DisableWorkspaceSettings = true,
                AssetBuildProfile = materialBinding ? Environment.GetEnvironmentVariable("ATLANTIS_0138_PROFILE") : null,
                AssetGeneration = materialBinding ? Environment.GetEnvironmentVariable("ATLANTIS_0138_GENERATION") : null })
            {
                Width = 1620, Height = 880, WindowStartupLocation = WindowStartupLocation.CenterScreen,
            };
            desktop.MainWindow = window;
            window.Opened += (_, _) => Dispatcher.UIThread.Post(async () =>
            {
                int exit = 1;
                try
                {
                    if (materialBinding) await CheckMaterialBindingUi(window, evidence);
                    else if (tuning) await CheckBistroTuningUi(window, evidence);
                    else await CheckBistroReferenceUi(window, evidence);
                    exit = 0;
                }
                catch (Exception error) { Console.Error.WriteLine(error); }
                finally
                {
                    typeof(MainWindow).GetField("_leaveCloseApproved", BindingFlags.Instance | BindingFlags.NonPublic)!
                        .SetValue(window, true);
                    window.Close();
                    desktop.Shutdown(exit);
                }
            });
            base.OnFrameworkInitializationCompleted();
        }
    }

    private static async Task CheckBistroReferenceUi(MainWindow window, string evidence)
    {
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        void Verify(bool ok, string name)
        {
            if (!ok) throw new InvalidOperationException(name);
            Console.WriteLine("PASS " + name);
        }
        static void Click(MenuItem item) => item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } process && File.Exists(process.SessionPath) &&
            Field<Button>("_bistroViews").IsEnabled, "Bistro document and hosted Runtime", 120);
        var runtime = Field<RuntimeProcess>("_runtime");
        File.WriteAllText(Path.Combine(evidence, "runtime-workdir.txt"), runtime.Launch.WorkDirectory);
        nint hwnd = window.TryGetPlatformHandle()!.Handle;
        var viewport = Field<ViewportHost>("_viewport").Window!;
        Verify(GetDpiForWindow(hwnd) == 96 && viewport.RuntimeChildIsHosted(), "actual Windows 100% with embedded Runtime");
        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        var active = peer.Authoring.ActiveCamera().Value!.Value;
        var cameraBefore = Json.Write(Codec.Encode(peer.Control.ViewportNavigation([]).Value!.SceneCamera));
        var before = peer.Authoring.DocumentState().Value!;
        bool undo = Field<Button>("_undo").IsEnabled;
        var list = Field<ListBox>("_entities");
        list.SelectedItem = list.Items.OfType<HierarchyRow>().Single(row => row.Node == active);
        await NameUiWait(() => Field<TextBox>("_nameBox") is not null, "camera's Inspector draft field", 40);
        await Task.Delay(500);
        if (Field<Window>("_recoveryWindow") is { } recovery)
        {
            var later = recovery.GetVisualDescendants().OfType<Button>().Single(button => Equals(button.Content, "Decide Later"));
            later.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => Field<Window>("_recoveryWindow") is null, "defer the disposable fixture's previous recovery copy");
        }
        var draft = Field<TextBox>("_nameBox");
        draft.Focus();
        draft.Text = "0131 unsubmitted camera name";
        Field<Button>("_bistroViews").Focus();
        var enable = Field<MenuItem>("_bistroEnable");
        if (enable.IsVisible) Click(enable);
        var captureAction = Field<MenuItem>("_bistroCapture");
        foreach (var (id, preset, menu) in new[] {
            ("street", BistroReferenceView.Street, "_bistroStreet"),
            ("terrace", BistroReferenceView.Terrace, "_bistroTerrace")
        })
        {
            BistroCapture? first = null;
            for (int visit = 1; visit <= 2; ++visit)
            {
                var item = Field<MenuItem>(menu);
                Verify(item.IsEnabled, preset.Name + " is available");
                Click(item);
                await NameUiWait(() => captureAction.IsEnabled, preset.Name + " settled", 35);
                Verify(viewport.RuntimeChildHasExtent(960, 540), preset.Name + " live native drawable 960 × 540");
                string prefix = Path.Combine(evidence, id + "-" + visit);
                var previous = Field<BistroReferenceWindow>("_bistroCompare");
                if (visit == 1)
                {
                    previous?.Hide();
                    await NativeCapture(window, prefix + "-viewport.png");
                }
                ulong beforeFrame = previous?.Latest?.Report.Frame ?? 0;
                Click(captureAction);
                await NameUiWait(() => Field<BistroReferenceWindow>("_bistroCompare")?.Latest is { } next &&
                    next.View == preset && next.Report.Frame != beforeFrame, preset.Name + " captured", 40);
                var compare = Field<BistroReferenceWindow>("_bistroCompare");
                var shot = compare.Latest!;
                File.WriteAllBytes(prefix + ".png", compare.CapturedPng.ToArray());
                File.WriteAllText(prefix + ".json", Json.Write(Codec.Encode(shot.Report)) + "\n");
                Verify(shot.Report.Image is { Width: 960, Height: 540 } &&
                    preset.Matches(shot.Report.Data.View, shot.Report.Data.Projection), preset.Name + " frame and lens");
                Verify(shot.Report.Data.ExposureCompensationEv is not null, preset.Name + " actual EV in shared report");
                if (visit == 1)
                {
                    first = shot;
                    var refs = Path.GetDirectoryName(evidence)!;
                    var other = preset == BistroReferenceView.Street ? BistroReferenceView.Terrace : BistroReferenceView.Street;
                    Verify(!compare.LoadReference(preset, Path.Combine(refs, other.ReferenceFile)), "wrong official still is refused");
                    Verify(compare.LoadReference(preset, Path.Combine(refs, preset.ReferenceFile)), "correct official still is shown");
                    if (!compare.IsVisible) compare.Show();
                    await NativeCapture(compare, prefix + "-comparison.png");
                    compare.Hide();
                    window.Activate();
                    await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground for ordinary wheel navigation");
                    var area = viewport.ScreenRect();
                    Verify(PostBistroInput(viewport.RuntimeChild, 0x020a, 120 << 16,
                        Coordinates(area.Left + area.Width / 2, area.Top + area.Height / 2)), "native wheel routed through embedded Runtime");
                    await NameUiWait(() => Field<BistroReferenceView>("_bistroView") is null, "wheel leaves reference mode");
                    await NameUiWait(() => !viewport.RuntimeChildHasExtent(960, 540), "flexible viewport after wheel");
                    var moved = await Task.Run(() =>
                    {
                        using var observer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
                        return observer.Control.Step(new StepRequest(1)).Report!;
                    }).WaitAsync(TimeSpan.FromSeconds(15));
                    Verify(!preset.Matches(moved.Data.View, moved.Data.Projection), preset.Name + " ordinary navigation leaves frozen pose");
                    File.WriteAllText(Path.Combine(evidence, id + "-away.json"), Json.Write(Codec.Encode(moved)) + "\n");
                }
                else
                {
                    bool Near(IReadOnlyList<float> left, IReadOnlyList<float> right) =>
                        left.Zip(right).All(p => Math.Abs(p.First - p.Second) <= 0.0001f);
                    Verify(Near(first!.Report.Data.View, shot.Report.Data.View) &&
                        Near(first.Report.Data.Projection, shot.Report.Data.Projection), preset.Name + " return matches initial framing");
                }
            }
        }
        Verify(peer.Authoring.ActiveCamera().Value == active &&
            Json.Write(Codec.Encode(peer.Control.ViewportNavigation([]).Value!.SceneCamera)) == cameraBefore &&
            peer.Authoring.DocumentState().Value == before && Field<Button>("_undo").IsEnabled == undo &&
            Field<TextBox>("_nameBox").Text == "0131 unsubmitted camera name",
            "active scene camera, document state, unsubmitted draft and Undo unchanged");
        window.Width = 1050;
        await NameUiWait(() => Field<BistroReferenceView>("_bistroView") is null, "compact window leaves fixed view");
        Click(Field<MenuItem>("_bistroStreet"));
        Verify(Field<BistroReferenceView>("_bistroView") is null && !captureAction.IsEnabled &&
            Field<TextBlock>("_viewportNavFeedback").Text?.Contains("960 × 540", StringComparison.Ordinal) == true,
            "compact viewport refuses the preset and explains the physical size");
        window.Width = 1620;
        await NameUiWait(() => Field<Grid>("_bistroViewportArea").Bounds.Width >= 960, "wide viewport restored");
        Click(Field<MenuItem>("_bistroStreet"));
        await NameUiWait(() => captureAction.IsEnabled, "reference view restored after compact refusal");
        var pause = Field<Button>("_pause");
        pause.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => !captureAction.IsEnabled && Field<Button>("_play").IsEnabled, "paused Runtime blocks capture");
        Field<Button>("_play").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => captureAction.IsEnabled, "running Runtime allows explicit capture again");
        var currentDraft = Field<TextBox>("_nameBox");
        currentDraft.Focus();
        Verify(NameUiKey(currentDraft, Key.Enter), "draft still supports normal explicit commit");
        await NameUiWait(() => Field<Button>("_undo").IsEnabled &&
            Field<TextBlock>("_applyLine").Text?.StartsWith("apply: applied", StringComparison.Ordinal) == true,
            "explicit edit settles with ordinary history and Apply", 75);
        Verify(peer.Authoring.DocumentState().Value!.Token.DocumentRevision > before.Token.DocumentRevision,
            "explicit edit is visible to a shared client");
        var sceneCamera = Field<Button>("_resetCamera");
        sceneCamera.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => Field<BistroReferenceView>("_bistroView") is null &&
            Field<TextBlock>("_viewportNavFeedback").Text?.Contains("Scene camera", StringComparison.Ordinal) == true,
            "ordinary Scene Camera return");
        Verify(Json.Write(Codec.Encode(peer.Control.ViewportNavigation([]).Value!.SceneCamera)) == cameraBefore,
            "clearing the override returns to the original scene-camera pose and lens");
        Click(Field<MenuItem>("_bistroStreet"));
        await NameUiWait(() => captureAction.IsEnabled, "street view after explicit Apply");
        var preserved = Field<BistroReferenceWindow>("_bistroCompare").Latest;
        Click(captureAction);
        Click(Field<MenuItem>("_bistroTerrace"));
        await NameUiWait(() => captureAction.IsEnabled, "new view supersedes pending capture");
        Verify(Field<BistroReferenceWindow>("_bistroCompare").Latest == preserved,
            "a superseded capture cannot replace the successful comparison");
        Click(Field<MenuItem>("_bistroStreet"));
        await NameUiWait(() => captureAction.IsEnabled, "street view ready for native picking");
        const int pickX = 495, pickY = 260;
        var matrices = CameraConvention.Matrices(BistroReferenceView.Street.Pose(), 960f / 540)!;
        var ray = CameraConvention.RayThroughNdc(matrices, 2f * pickX / 960 - 1, 2f * pickY / 540 - 1);
        var expected = await Task.Run(() =>
        {
            using var observer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
            return observer.Control.Raycast(new Ray(ray.Origin, ray.Direction)).Hit;
        }).WaitAsync(TimeSpan.FromSeconds(15));
        Verify(expected is not null, "reference projection ray intersects a Bistro mesh");
        window.Activate();
        await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground for native picking");
        Verify(PostBistroInput(viewport.RuntimeChild, 0x0201, 1, Coordinates(pickX, pickY)),
            "native down routed through embedded Runtime");
        await Task.Delay(80);
        Verify(PostBistroInput(viewport.RuntimeChild, 0x0202, 0, Coordinates(pickX, pickY)),
            "native up routed through embedded Runtime");
        await NameUiWait(() => Field<Selection>("_selection").Node == expected!.Entity &&
            Field<GizmoSnapshot>("_gizmoShown")?.Node == expected.Entity, "native pick and Gizmo target match the fixed projection");
        Verify(true, "native pick and Gizmo target match the fixed projection");
    }

    private static async Task CheckBistroTuningUi(MainWindow window, string evidence)
    {
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        static void Click(MenuItem item) => item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
        static void Press(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        void Verify(bool ok, string name)
        {
            if (!ok) throw new InvalidOperationException(name);
            Console.WriteLine("PASS " + name);
        }

        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } process && File.Exists(process.SessionPath) &&
            Field<Button>("_bistroViews").IsEnabled, "Bistro tuning document and hosted Runtime", 120);
        var runtime = Field<RuntimeProcess>("_runtime");
        File.WriteAllText(Path.Combine(evidence, "runtime-workdir.txt"), runtime.Launch.WorkDirectory);
        var viewport = Field<ViewportHost>("_viewport").Window!;
        Verify(GetDpiForWindow(window.TryGetPlatformHandle()!.Handle) == 96 && viewport.RuntimeChildIsHosted(),
            "actual Windows 100% and embedded Runtime");
        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        var active = peer.Authoring.ActiveCamera().Value!.Value;
        var alternate = new EntityGuid(Guid.Parse("01370137-0137-4137-8137-013700000010"));
        EntityGuid[] lights = [new(Guid.Parse("01370137-0137-4137-8137-013700000001")),
            new(Guid.Parse("01370137-0137-4137-8137-013700000002"))];
        var schema = peer.Authoring.Schema;
        var cameraPose = Json.Write(Codec.Encode(peer.Control.ViewportNavigation([]).Value!.SceneCamera));
        var source = peer.Authoring.DocumentState().Value!.Source;
        var sourceBytes = File.ReadAllBytes(source);
        float Read(EntityGuid node, string component, string field)
        {
            var address = SceneSchema.AddressOf(schema, node, component, field)!.Value;
            return peer.Authoring.GetProperty(address).Value.TryGetFloat32(out float value)
                ? value : throw new InvalidOperationException("Missing " + field);
        }

        var selected = Field<Selection>("_selection");
        var list = Field<ListBox>("_entities");
        void Select(EntityGuid node) => list.SelectedItem = list.Items.OfType<HierarchyRow>().Single(row => row.Node == node);
        var before = Field<MenuItem>("_bistroBefore");
        var after = Field<MenuItem>("_bistroAfter");
        var locate = Field<MenuItem>("_bistroLocateCamera");
        Click(Field<MenuItem>("_bistroEnable"));
        Click(Field<MenuItem>("_bistroTerrace"));
        await NameUiWait(() => before.IsEnabled, "fixed terrace settled");
        Verify(viewport.RuntimeChildHasExtent(960, 540), "960 × 540 native drawable");
        if (peer.Authoring.ApplyStatus().Value?.Projection is null)
        {
            Click(before);
            await NameUiWait(() => Field<TextBlock>("_viewportNavFeedback").Text?.Contains(
                "not verified", StringComparison.Ordinal) == true, "unverified projection refused");
            Verify(Field<BistroReferenceWindow>("_bistroCompare") is null, "unverified capture did not populate a slot");
            Verify(peer.Authoring.ApplyTracked().IsOk, "explicit baseline projection");
        }

        async Task<ApplyProjection> Settle(ulong afterRevision = 0)
        {
            ApplyProjection? confirmed = null;
            await NameUiWait(() =>
            {
                var state = peer.Authoring.ApplyStatus().Value;
                confirmed = state?.Projection;
                return confirmed is not null && confirmed.Receipt.DocumentRevision >= afterRevision &&
                    confirmed.Receipt.DocumentRevision == state!.CurrentDocumentRevision &&
                    state.Request?.State == ApplyState.Applied &&
                    peer.Control.Status().SceneGeneration == confirmed.Generation && before.IsEnabled;
            }, "verified projection", 75);
            return confirmed!;
        }

        async Task<BistroCapture> Capture(MenuItem action, string stem)
        {
            bool later = action == after;
            BistroReferenceWindow.PairImage? Current()
            {
                var compare = Field<BistroReferenceWindow>("_bistroCompare");
                return later ? compare?.After : compare?.Before;
            }
            var prior = Current();
            var state = peer.Authoring.DocumentState().Value;
            var projection = peer.Authoring.ApplyStatus().Value!.Projection!;
            bool undo = Field<Button>("_undo").IsEnabled;
            Click(action);
            await NameUiWait(() => Current() is { } image && image != prior, stem, 40);
            var result = Current()!;
            var capture = result.Capture;
            Verify(capture.Projection is { } proof && proof.Document == state!.Token && proof.Camera == active &&
                proof.Apply == projection.Receipt.Id && proof.Viewport.SceneGeneration == projection.Generation &&
                capture.Report.Image is { Width: 960, Height: 540 } &&
                BistroReferenceView.Terrace.Matches(capture.Report.Data.View, capture.Report.Data.Projection),
                stem + " is settled and matches fixed framing");
            var observed = peer.Authoring.DocumentState().Value;
            var observedProjection = peer.Authoring.ApplyStatus().Value?.Projection;
            bool canUndo = Field<Button>("_undo").IsEnabled;
            Verify(observed == state && observedProjection == projection && canUndo == undo,
                stem + " leaves document, history and Apply unchanged" +
                (observed == state && observedProjection == projection && canUndo == undo ? "" :
                    $" (document match: {observed == state}, projection match: {observedProjection == projection}, Undo: {undo} -> {canUndo})"));
            string path = Path.Combine(evidence, stem);
            File.WriteAllBytes(path + ".png", result.Png);
            File.WriteAllText(path + ".json", Json.Write(Codec.Encode(capture.Report)) + "\n");
            File.WriteAllText(path + "-projection.txt", capture.Projection + "\n");
            return capture;
        }

        float[] Intensities(BistroCapture capture) => new[] { new System.Numerics.Vector3(-10, 2, 13),
            new System.Numerics.Vector3(-12, 2.5f, 12) }.Select(position =>
            capture.Report.Data.PointLights.Single(light => light.Position == position).Intensity).ToArray();
        bool SameOtherLights(BistroCapture left, BistroCapture right) =>
            left.Report.Data.DirectionalLights.SequenceEqual(right.Report.Data.DirectionalLights) &&
            left.Report.Data.PointLights.Select(light => IntensitiesOfFixture(light) ? light with { Intensity = 0 } : light)
                .SequenceEqual(right.Report.Data.PointLights.Select(light => IntensitiesOfFixture(light) ? light with { Intensity = 0 } : light));
        static bool IntensitiesOfFixture(FramePointLight light) =>
            light.Position == new System.Numerics.Vector3(-10, 2, 13) ||
            light.Position == new System.Numerics.Vector3(-12, 2.5f, 12);

        var initialized = await Settle();
        var lightRepeat = await Capture(before, "light-repeat");
        var lightBefore = await Capture(before, "light-before");
        float[] mixed = lights.Select(node => Read(node, "Light", "intensity")).ToArray();
        Verify(mixed.SequenceEqual(new[] { 0.125f, 0.25f }) && Intensities(lightBefore).SequenceEqual(mixed) &&
            lightBefore.Report.Data.ExposureCompensationEv is float && SameOtherLights(lightRepeat, lightBefore),
            "mixed point lights are individually present in the report");
        var beforePair = Field<BistroReferenceWindow>("_bistroCompare").Before;
        Click(after);
        await NameUiWait(() => Field<TextBlock>("_viewportNavFeedback").Text?.Contains(
            "newer applied revision", StringComparison.Ordinal) == true, "same revision rejected as After");
        Verify(Field<BistroReferenceWindow>("_bistroCompare").Before == beforePair &&
            Field<BistroReferenceWindow>("_bistroCompare").After is null, "rejection preserves Before");

        list.SelectedItems!.Clear();
        foreach (EntityGuid light in lights)
            list.SelectedItems.Add(list.Items.OfType<HierarchyRow>().Single(row => row.Node == light));
        var inputs = Field<Dictionary<string, TextBox>>("_batchInputs");
        await NameUiWait(() => inputs.TryGetValue("Light.intensity", out _), "mixed intensity field in batch Inspector");
        inputs["Light.intensity"].Text = "12";
        var set = Field<StackPanel>("_inspectorPanel").GetVisualDescendants().OfType<Button>()
            .Single(button => AutomationProperties.GetName(button) == "Set Light.intensity");
        Press(Field<Button>("_pause"));
        await NameUiWait(() => Field<Button>("_play").IsEnabled, "Runtime paused before batch commit");
        Press(set);
        await NameUiWait(() => peer.Authoring.ApplyStatus().Value?.Request?.State == ApplyState.Pending &&
            Field<Button>("_undo").IsEnabled, "document committed while Apply is pending");
        var pending = peer.Authoring.ApplyStatus().Value!;
        Verify(pending.CurrentDocumentRevision > initialized.Receipt.DocumentRevision &&
            pending.Projection == initialized && !after.IsEnabled && Field<BistroReferenceWindow>("_bistroCompare").Before == beforePair &&
            lights.All(node => Read(node, "Light", "intensity") == 12), "pending Apply retains Before with its original projection");
        Press(Field<Button>("_play"));
        var committedLight = await Settle(initialized.Receipt.DocumentRevision + 1);
        Verify(committedLight.Receipt.DocumentRevision == initialized.Receipt.DocumentRevision + 1 &&
            Field<Button>("_undo").IsEnabled && lights.All(node => Read(node, "Light", "intensity") == 12),
            "one guarded batch commit lights both points");
        var lightAfter = await Capture(after, "light-after");
        Verify(Intensities(lightAfter).SequenceEqual(new[] { 12f, 12f }) && SameOtherLights(lightBefore, lightAfter) &&
            lightBefore.Report.Data.ExposureCompensationEv == lightAfter.Report.Data.ExposureCompensationEv,
            "only point light intensity changed in the frame report");
        await NativeCapture(Field<BistroReferenceWindow>("_bistroCompare"), Path.Combine(evidence, "light-pair-window.png"));
        Press(Field<Button>("_undo"));
        var undoneLight = await Settle(committedLight.Receipt.DocumentRevision + 1);
        await NameUiWait(() => !Field<Button>("_undo").IsEnabled, "Undo history settled after restoring lights");
        var lightRestored = await Capture(after, "light-undo");
        Verify(!Field<Button>("_undo").IsEnabled && lights.Select(node => Read(node, "Light", "intensity")).SequenceEqual(mixed) &&
            Intensities(lightRestored).SequenceEqual(mixed) && SameOtherLights(lightBefore, lightRestored),
            "one Undo restores both original values and the projected light report");

        var exposureRepeat = await Capture(before, "exposure-repeat");
        var exposureBefore = await Capture(before, "exposure-before");
        Verify(Field<BistroReferenceWindow>("_bistroCompare").After is null, "new Before clears old After");
        var reference = Path.Combine(Path.GetDirectoryName(evidence)!, BistroReferenceView.Terrace.ReferenceFile);
        if (File.Exists(reference))
        {
            var comparison = Field<BistroReferenceWindow>("_bistroCompare");
            Verify(comparison.LoadReference(BistroReferenceView.Terrace, reference),
                "original attributed ORCA source remains available");
            var tabs = (TabControl)comparison.Content!;
            tabs.SelectedIndex = 0;
            await NativeCapture(comparison, Path.Combine(evidence, "orca-reference-window.png"));
            tabs.SelectedIndex = 1;
        }

        Select(alternate);
        await NameUiWait(() => Field<TextBox>("_nameBox")?.Text == "Bistro alternate camera", "alternate camera in Inspector");
        var draft = Field<TextBox>("_nameBox");
        draft.Text = "Retain this unsubmitted text";
        Click(locate);
        Verify(selected.Node == alternate && draft.Text == "Retain this unsubmitted text", "locator protects unsubmitted draft");
        Verify(NameUiKey(draft, Key.Escape), "dismiss the test draft");
        Click(locate);
        Select(lights[0]);
        await NameUiWait(() => Field<TextBlock>("_viewportNavFeedback").Text?.Contains("again", StringComparison.Ordinal) == true,
            "locator retired after selection changes");
        Verify(selected.Node == lights[0], "late locator cannot change selection");
        var search = Field<TextBox>("_hierarchySearch");
        search.Text = "Terrace fill";
        await NameUiWait(() => !list.Items.OfType<HierarchyRow>().Any(row => row.Node == active), "filter hides the camera");
        Click(locate);
        await NameUiWait(() => Field<TextBlock>("_viewportNavFeedback").Text?.Contains("hidden", StringComparison.Ordinal) == true,
            "locator refuses hidden camera");
        Verify(selected.Node == lights[0], "hidden locator keeps current selection");
        search.Text = "";
        await NameUiWait(() => list.Items.OfType<HierarchyRow>().Any(row => row.Node == active), "camera visible");
        Click(locate);
        await NameUiWait(() => selected.Node == active, "locate selects the actual active camera");

        float ev = Read(active, "Camera", "exposureCompensationEv");
        var inspector = Field<StackPanel>("_inspectorPanel");
        TextBox? ExposureBox() => inspector.GetVisualDescendants().OfType<Grid>()
            .SingleOrDefault(row => Equals(row.Tag, "Camera.exposureCompensationEv"))?
            .GetVisualDescendants().OfType<TextBox>().SingleOrDefault();
        await NameUiWait(() => ExposureBox() is not null, "active camera exposure field");
        var exposure = ExposureBox()!;
        window.Activate();
        exposure.Focus();
        exposure.Text = (ev + 1).ToString(System.Globalization.CultureInfo.InvariantCulture);
        NameUiKey(exposure, Key.Enter);
        var committedExposure = await Settle(undoneLight.Receipt.DocumentRevision + 1);
        Verify(committedExposure.Receipt.DocumentRevision == undoneLight.Receipt.DocumentRevision + 1 &&
            Read(active, "Camera", "exposureCompensationEv") == ev + 1, "one exposure commit");
        var exposureAfter = await Capture(after, "exposure-after");
        Verify(exposureAfter.Report.Data.ExposureCompensationEv == ev + 1 &&
            exposureRepeat.Report.Data.ExposureCompensationEv == ev &&
            exposureBefore.Report.Data.PointLights.SequenceEqual(exposureAfter.Report.Data.PointLights) &&
            exposureBefore.Report.Data.DirectionalLights.SequenceEqual(exposureAfter.Report.Data.DirectionalLights),
            "exposure alone changed with identical effective lights");
        await NativeCapture(Field<BistroReferenceWindow>("_bistroCompare"), Path.Combine(evidence, "exposure-pair-window.png"));
        Press(Field<Button>("_undo"));
        await Settle(committedExposure.Receipt.DocumentRevision + 1);
        await NameUiWait(() => !Field<Button>("_undo").IsEnabled, "Undo history settled after restoring EV");
        var exposureRestored = await Capture(after, "exposure-undo");
        Verify(!Field<Button>("_undo").IsEnabled && Read(active, "Camera", "exposureCompensationEv") == ev &&
            exposureRestored.Report.Data.ExposureCompensationEv == ev &&
            exposureRestored.Report.Data.PointLights.SequenceEqual(exposureBefore.Report.Data.PointLights),
            "one Undo restores authored and effective exposure");
        Verify(peer.Authoring.ActiveCamera().Value == active &&
            Json.Write(Codec.Encode(peer.Control.ViewportNavigation([]).Value!.SceneCamera)) == cameraPose &&
            File.ReadAllBytes(source).SequenceEqual(sourceBytes), "camera identity, pose, lens and formal source unchanged");

        Click(before);
        Click(Field<MenuItem>("_bistroExit"));
        await NameUiWait(() => !before.IsEnabled && !after.IsEnabled, "leave invalidates tuning");
        await Task.Delay(650);
        Verify(Field<BistroReferenceWindow>("_bistroCompare").Before is null &&
            Field<BistroReferenceWindow>("_bistroCompare").After is null, "late capture cannot reopen a retired pair");
    }

    private static async Task CheckMaterialBindingUi(MainWindow window, string evidence)
    {
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        static T ModalField<T>(Window modal, string name) where T : class =>
            (T)modal.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(modal)!;
        static void Press(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } process && File.Exists(process.SessionPath) &&
            Field<Button>("_bistroViews").IsEnabled, "ready hosted material binding fixture", 120);
        var runtime = Field<RuntimeProcess>("_runtime");
        File.WriteAllText(Path.Combine(evidence, "runtime-workdir.txt"), runtime.Launch.WorkDirectory);
        var viewport = Field<ViewportHost>("_viewport").Window!;
        if (GetDpiForWindow(window.TryGetPlatformHandle()!.Handle) != 96 || !viewport.RuntimeChildIsHosted())
            throw new InvalidOperationException("The test requires the hosted Editor at Windows 100%.");

        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        peer.Authoring.ApplyTracked();
        await NameUiWait(() => peer.Authoring.ApplyStatus().Value?.Projection is not null,
            "baseline projection settled", 75);
        Field<MenuItem>("_bistroEnable").RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
        Field<MenuItem>("_bistroTerrace").RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
        await NameUiWait(() => Field<Button>("_locateSurfaceMaterial").IsEnabled && viewport.RuntimeChildHasExtent(960, 540),
            "fixed Terrace is ready");
        var pose = BistroReferenceView.Terrace.Pose();
        var matrices = CameraConvention.Matrices(pose, 960f / 540)!;
        var stamp = peer.Control.ViewportVisibility().Value!.Stamp;
        var fabric = AssetGuid.Parse(Environment.GetEnvironmentVariable("ATLANTIS_0138_FABRIC") ??
            "a7411679-3a1c-873e-8afb-c00acb997db8");
        MaterialBindingPick? target = null;
        int targetX = 0, targetY = 0;
        foreach (var (x,y) in new[] { (240,138), (320,92), (120,140), (190,115), (291,158),
                     (378,132), (275,110), (415,72), (180,152), (350,171), (320,130) })
        {
            var ray = CameraConvention.RayThroughNdc(matrices, x / 480f - 1, y / 270f - 1);
            var forward = -new System.Numerics.Vector3(matrices.View[2], matrices.View[6], matrices.View[10]);
            float dot = System.Numerics.Vector3.Dot(ray.Direction, forward);
            var outcome = peer.Control.PickMaterialBinding(new(new Ray(ray.Origin, ray.Direction), pose.Lens.NearZ / dot,
                pose.Lens.FarZ / dot, stamp.SceneGeneration, stamp));
            File.AppendAllText(Path.Combine(evidence, "surface-samples.txt"), $"{x},{y}: {outcome}\n");
            if (outcome.Value is { } hit && hit.Material == fabric) { target = hit; targetX = x; targetY = y; break; }
        }
        if (target is null) throw new InvalidOperationException("No fixed sample resolved the fabric.");
        var before = peer.Authoring.DocumentState().Value!;
        async Task<LocatedMaterialBinding> Locate()
        {
            Press(Field<Button>("_locateSurfaceMaterial"));
            window.Activate();
            PostBistroInput(viewport.RuntimeChild, 0x0201, 1, Coordinates(targetX, targetY));
            await Task.Delay(70);
            PostBistroInput(viewport.RuntimeChild, 0x0202, 0, Coordinates(targetX, targetY));
            await NameUiWait(() => Field<LocatedMaterialBinding>("_locatedMaterial")?.Hit.Entity == target.Entity,
                "verified fabric hit in Inspector", 75);
            return Field<LocatedMaterialBinding>("_locatedMaterial");
        }
        var located = await Locate();
        if (located.Hit != target || located.KnownUses.Count < 1 ||
            Field<Selection>("_selection").Node != target.Entity || peer.Authoring.DocumentState().Value != before)
            throw new InvalidOperationException("The real Editor did not reveal the unique fabric slot and known sharing.");
        Console.WriteLine($"PASS Windows 100%, fixed Terrace pixel {targetX},{targetY}: {located.Hit}; known other uses {located.KnownUses.Count}");
        var materialPanel = Field<StackPanel>("_inspectorPanel");
        await NameUiWait(() => materialPanel.GetVisualDescendants().OfType<Button>()
            .Any(button => Equals(button.Content, "Duplicate for this binding")), "located material actions rendered", 75);
        await NativeCapture(window, Path.Combine(evidence, "located-fabric-window.png"));

        void CaptureBindingFrame(string stem)
        {
            string path = Path.Combine(evidence, stem + ".png");
            var report = peer.Control.Step(new StepRequest(1, path, WithOverlay: false)).Report;
            if (report?.Image is not { Width: 960, Height: 540 } ||
                !BistroReferenceView.Terrace.Matches(report.Data.View, report.Data.Projection))
                throw new InvalidOperationException("Capture did not use the settled Terrace framing.");
            File.WriteAllText(Path.Combine(evidence, stem + ".json"), Json.Write(Codec.Encode(report)) + "\n");
            File.WriteAllText(Path.Combine(evidence, stem + "-apply.txt"), peer.Authoring.ApplyStatus().Value!.Projection!.ToString());
        }
        CaptureBindingFrame("before");
        CaptureBindingFrame("repeat");
        string? output = Environment.GetEnvironmentVariable("ATLANTIS_0138_COPY_NAME");
        if (output is not null)
        {
            var panel = Field<StackPanel>("_inspectorPanel");
            Button Find(string label) => panel.GetVisualDescendants().OfType<Button>().Single(b => Equals(b.Content, label));
            Press(Find("Duplicate for this binding"));
            await NameUiWait(() => Field<MaterialEditorWindow>("_materialWindow") is not null, "existing copy editor opens", 75);
            var editor = Field<MaterialEditorWindow>("_materialWindow");
            ModalField<TextBox>(editor, "_name")!.Text = output;
            ModalField<TextBox[]>(editor, "_values")![0].Text = "0.08";
            Press(ModalField<Button>(editor, "_save")!);
            await NameUiWait(() => Field<MaterialBindingReview>("_publishedMaterialCopy") is not null,
                "verified copy publication", 180);
            if (peer.Authoring.DocumentState().Value != before)
                throw new InvalidOperationException("Publication changed the authoring document.");
            var result = Field<MaterialBindingReview>("_publishedMaterialCopy");
            File.WriteAllText(Path.Combine(evidence, "review-note.txt"), result.ToString());
            File.WriteAllText(Path.Combine(evidence, "generation.txt"),
                Field<MaterialSettlement>("_materialSettlement").Job!.Generation!.Value.ToString());
            await NativeCapture(editor, Path.Combine(evidence, "published-material-copy.png"));
            editor.Close();
        }
        var notePath = Environment.GetEnvironmentVariable("ATLANTIS_0138_REVIEW_NOTE");
        if (notePath is null) return;
        var review = MaterialBindingReview.Parse(File.ReadAllText(notePath)) ??
            throw new InvalidOperationException("Invalid test handoff.");
        if (review.Original != fabric || review.Node != located.Hit.Entity)
            throw new InvalidOperationException("The test fixture no longer identifies the original target.");

        var body = Field<StackPanel>("_inspectorPanel");
        Button Action(string label) => body.GetVisualDescendants().OfType<Button>().Single(b => Equals(b.Content, label));
        var text = body.GetVisualDescendants().OfType<TextBox>()
            .Single(box => box.PlaceholderText == "Paste the published copy review note");
        text.Text = (review with { Node = new EntityGuid(Guid.NewGuid()) }).ToString();
        Press(Action("Replace this binding with copy"));
        if (Field<AssetReferencePickerWindow>("_referenceWindow") is not null || peer.Authoring.DocumentState().Value != before)
            throw new InvalidOperationException("Stale handoff changed the scene.");
        if (Field<Button>("_undo").IsEnabled)
            throw new InvalidOperationException("A stale handoff wrote history.");
        Console.WriteLine("PASS stale handoff leaves document and history untouched");
        text.Text = review.ToString();
        Press(Action("Replace this binding with copy"));
        await NameUiWait(() => Field<AssetReferencePickerWindow>("_referenceWindow") is { } chooser &&
            ModalField<Button>(chooser, "_confirm")!.IsEnabled, "exact copy preselected", 75);
        var picker = Field<AssetReferencePickerWindow>("_referenceWindow");
        Press(ModalField<Button>(picker, "_confirm")!);
        var field = SceneSchema.AddressOf(peer.Authoring.Schema, target.Entity, "Renderable", "materialAsset")!.Value;
        async Task CheckValue(AssetGuid value)
        {
            await NameUiWait(() => peer.Authoring.GetProperty(field).Value.TryGetAsset(out var material) && material == value &&
                peer.Authoring.ApplyStatus().Value is { Projection: { } projection } s &&
                projection.Receipt.DocumentRevision == s.CurrentDocumentRevision, "binding and Apply settle", 75);
        }
        await CheckValue(review.Copy);
        foreach (var node in peer.Authoring.ListNodes().Value.Where(n => n.Node != target.Entity).Select(n => n.Node))
        {
            if (!located.KnownUses.Any(use => use.EndsWith(node.ToString(), StringComparison.Ordinal))) continue;
            var other = field with { Node = node };
            if (peer.Authoring.GetProperty(other).Value.AsAsset() != fabric)
                throw new InvalidOperationException("A shared binding changed outside the target.");
        }
        Console.WriteLine("PASS only one binding changed after Apply");
        CaptureBindingFrame("after");
        await NameUiWait(() => Field<Button>("_undo").IsEnabled, "binding Undo is available");
        Press(Field<Button>("_undo"));
        await CheckValue(fabric);
        CaptureBindingFrame("undo");
        await NameUiWait(() => Field<Button>("_redo").IsEnabled, "binding Redo is available");
        Press(Field<Button>("_redo"));
        await CheckValue(review.Copy);
        CaptureBindingFrame("redo");
        Console.WriteLine("PASS one Undo and Redo restore only the target binding");
        var saved = peer.Authoring.SaveIfCurrent(peer.Authoring.DocumentState().Value!.Token);
        if (!saved.IsOk) throw new InvalidOperationException("Save refused: " + saved.Error);
        var reopened = peer.Authoring.ReopenIfCurrent(saved.Value.Token);
        if (!reopened.IsOk || peer.Authoring.GetProperty(field).Value.AsAsset() != review.Copy)
            throw new InvalidOperationException("The saved and reopened source lost the replacement.");
        File.WriteAllText(Path.Combine(evidence, "saved-source.txt"), reopened.Value.Source);
        Console.WriteLine("PASS save and reopen preserve the replacement and GUID");
    }

    private static async Task NativeCapture(Window target, string output)
    {
        string? helper = Environment.GetEnvironmentVariable("ATLANTIS_0131_CAPTURE");
        if (helper is null) return;
        var handle = target.TryGetPlatformHandle()!.Handle;
        target.Activate();
        await NameUiWait(() => GetForegroundWindow() == handle, "foreground native capture window");
        await Task.Delay(200);
        using var process = Process.Start(new ProcessStartInfo("python") { UseShellExecute = false, CreateNoWindow = true,
            ArgumentList = { helper, handle.ToString(), output } })!;
        await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(8));
        if (process.ExitCode != 0)
            throw new InvalidOperationException("Native window capture failed: " + output);
    }

    private static nint Coordinates(int x, int y) => (nint)(unchecked((uint)(ushort)x | ((uint)(ushort)y << 16)));

    [DllImport("user32.dll", EntryPoint = "PostMessageW")]
    private static extern bool PostBistroInput(nint window, uint message, nuint wParam, nint lParam);
}
