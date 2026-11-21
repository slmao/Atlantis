using System;
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
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;

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
    }

    private static int RunBistroReferenceUi(string runtime, string scene, string evidence)
    {
        Directory.CreateDirectory(evidence);
        return AppBuilder.Configure(() => new BistroReferenceApp(runtime, scene, evidence)).UsePlatformDetect()
            .StartWithClassicDesktopLifetime([]);
    }

    private sealed class BistroReferenceApp(string runtime, string scene, string evidence) : Application
    {
        public override void Initialize()
        {
            Styles.Add(new FluentTheme());
            EditorVisuals.Install(this);
        }

        public override void OnFrameworkInitializationCompleted()
        {
            var desktop = (IClassicDesktopStyleApplicationLifetime)ApplicationLifetime!;
            var window = new MainWindow(new EditorOptions(runtime, scene) { DisableWorkspaceSettings = true })
            {
                Width = 1620, Height = 880, WindowStartupLocation = WindowStartupLocation.CenterScreen,
            };
            desktop.MainWindow = window;
            window.Opened += (_, _) => Dispatcher.UIThread.Post(async () =>
            {
                int exit = 1;
                try { await CheckBistroReferenceUi(window, evidence); exit = 0; }
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
        static async Task NativeCapture(Window target, string output)
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
    }

    private static nint Coordinates(int x, int y) => (nint)(unchecked((uint)(ushort)x | ((uint)(ushort)y << 16)));

    [DllImport("user32.dll", EntryPoint = "PostMessageW")]
    private static extern bool PostBistroInput(nint window, uint message, nuint wParam, nint lParam);
}
