using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void WorkspaceLayoutChecks()
    {
        var ordinary = default(WorkspaceLayout);
        Check(ordinary.Project(1400) == new WorkspacePaneWidths(220, 802, 368) &&
              ordinary.Project(960) == new WorkspacePaneWidths(173, 469, 308), "layout preserves 0093's standard and compact defaults");
        var saved = new WorkspaceLayout(new(460, 490), WorkspaceTab.SavedScenes);
        var wide = saved.Project(1440);
        Check(wide == new WorkspacePaneWidths(460, 480, 490), "layout reproduces the preferred side widths at the original DIP width");

        // Windows reports a smaller logical extent when the same physical
        // client area moves from 96 to 144 DPI. Use the production projection.
        var compact = saved.Project(1440 / 1.5)!.Value;
        Check(compact.Viewport == WorkspaceLayout.MinimumViewport && compact.Hierarchy >= WorkspaceLayout.MinimumHierarchy &&
              compact.Right >= WorkspaceLayout.MinimumRight &&
              Math.Abs(compact.Hierarchy + compact.Viewport + compact.Right + 2 * WorkspaceLayout.SplitterWidth - 960) < 1e-9,
            "DPI 96 to 144: measured DIP extent reserves both splitters and every pane minimum");
        Check(Math.Abs((compact.Hierarchy - 173) / (460 - 173d) - (compact.Right - 298) / (490 - 298d)) < 1e-9 &&
              saved.Project(1440 / 1.0) == wide && saved.Preferred == new WorkspaceSideWidths(460, 490),
            "DPI 144 to 96: proportional compression never changes preferences and re-expansion recovers both sides");
        Check(saved.Project(0) is null && saved.Project(760) is null && saved.Project(double.NaN) is null &&
              saved.Project(761) == new WorkspacePaneWidths(173, 280, 298), "layout defers unmeasured or infeasible passes");
        Check(saved.AvailableTab(false) == WorkspaceTab.Inspector && saved.AvailableTab(true) == WorkspaceTab.SavedScenes &&
              saved.Tab == WorkspaceTab.SavedScenes, "unavailable tab fallback preserves the saved intent");

        if (!OperatingSystem.IsWindows()) return;
        var scratch = Directory.CreateTempSubdirectory("atlantis-0127-settings-");
        try
        {
            string root = Path.Combine(scratch.FullName, "settings");
            var first = new WorkspaceLayoutStore(root);
            var second = new WorkspaceLayoutStore(root);
            Check(first.Read() == default && !Directory.Exists(root), "absent preferences are defaults and reading does not create storage");
            first.SetWidths(saved.Preferred!.Value);
            second.SetTab(WorkspaceTab.SavedScenes);
            byte[] beforeScale = File.ReadAllBytes(first.Pathname);
            Check(first.Read().Project(1440 / 1.5) == compact && second.Read().Project(1440 / 1.0) == wide &&
                  File.ReadAllBytes(first.Pathname).SequenceEqual(beforeScale), "DPI transition and return do not write the stored width pair or tab");
            var alternate = new WorkspaceSideWidths(270, 410);
            first.SetWidths(alternate);
            Check(second.Read() == new WorkspaceLayout(alternate, WorkspaceTab.SavedScenes), "independent writes merge the current file's other field");
            byte[] valid = File.ReadAllBytes(first.Pathname);
            using (var json = JsonDocument.Parse(valid))
                Check(json.RootElement.EnumerateObject().Select(field => field.Name).ToHashSet().SetEquals(
                    ["version", "hierarchyDip", "rightDip", "tab"]), "the record contains only the approved layout fields");

            using (File.Open(Path.Combine(root, "workspace-layout.lease"), FileMode.Open, FileAccess.ReadWrite, FileShare.None))
                Check(Refused(() => second.SetTab(WorkspaceTab.Assets)) && File.ReadAllBytes(first.Pathname).SequenceEqual(valid),
                    "another writer's lease prevents overwriting the complete layout");
            using (File.Open(first.Pathname, FileMode.Open, FileAccess.Read, FileShare.Read))
                Check(Refused(first.Reset) && File.ReadAllBytes(first.Pathname).SequenceEqual(valid) && !Directory.EnumerateFiles(root, "*.tmp").Any(),
                    "failed atomic replacement retains the old record and removes its own temporary file");

            foreach (string invalid in new[]
            {
                "{", "{\"version\":2}", "{\"version\":1,\"tab\":\"other\"}", "{\"version\":1,\"hierarchyDip\":231}",
                "{\"version\":1,\"hierarchyDip\":-3,\"rightDip\":500}", "{\"version\":1,\"hierarchyDip\":1e999,\"rightDip\":500}",
                "{\"version\":1,\"tab\":\"assets\",\"tab\":\"inspector\"}", "{\"version\":1,\"scene\":\"other\"}", new string(' ', 4097),
            })
            {
                File.WriteAllText(first.Pathname, invalid);
                Check(Refused(() => _ = first.Read()) && Refused(() => second.SetTab(WorkspaceTab.Assets)) &&
                      File.ReadAllText(first.Pathname) == invalid, "invalid or unsupported settings are refused and preserved");
            }
            first.Reset();
            Check(second.Read() == default, "explicit reset can safely replace invalid bytes with both defaults");
            second.SetTab(WorkspaceTab.Assets);
            Check(first.Read() == new WorkspaceLayout(null, WorkspaceTab.Assets), "a later explicit edit after another window's reset preserves default widths");

            string alias = Path.Combine(scratch.FullName, "alias");
            using var link = Process.Start(new ProcessStartInfo("cmd.exe", $"/d /c mklink /J \"{alias}\" \"{root}\"")
                { UseShellExecute = false, CreateNoWindow = true })!;
            link.WaitForExit();
            try
            {
                var unsafeStore = new WorkspaceLayoutStore(alias);
                Check(link.ExitCode == 0 && Refused(() => _ = unsafeStore.Read()) && Refused(unsafeStore.Reset) &&
                    first.Read().Tab == WorkspaceTab.Assets, "reparse-point storage cannot read or reset another directory's preferences");
            }
            finally { if (link.ExitCode == 0) Directory.Delete(alias); }
        }
        finally { scratch.Delete(recursive: true); }

        static bool Refused(Action action)
        {
            try { action(); return false; }
            catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { return true; }
        }
    }

    private static string WorkspaceFixtureDirectory() => Path.GetFullPath(Environment.GetEnvironmentVariable("ATLANTIS_0127_SETTINGS") ??
        throw new InvalidOperationException("Workspace UI tests require an isolated settings directory."));

    private static async Task CheckWorkspaceLayoutUi(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        string phase = Environment.GetEnvironmentVariable("ATLANTIS_0127_PHASE") ?? throw new InvalidOperationException("Missing phase.");
        string evidence = Environment.GetEnvironmentVariable("ATLANTIS_0127_EVIDENCE") ?? throw new InvalidOperationException("Missing evidence folder.");
        var store = new WorkspaceLayoutStore(WorkspaceFixtureDirectory());
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        void Verify(string message, bool result, string detail = "")
        {
            checks.Add(new(message, result, detail));
            if (!result) throw new InvalidOperationException(message + " " + detail);
        }
        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } process && File.Exists(process.SessionPath) &&
                              Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count > 0, "workspace Runtime ready", 90);
        var runtime = Field<RuntimeProcess>("_runtime");
        work(runtime.Launch.WorkDirectory);
        nint hwnd = window.TryGetPlatformHandle()!.Handle;
        window.Activate();
        await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground workspace Editor");
        Verify("workspace Editor is at actual native Windows 100%", GetDpiForWindow(hwnd) == 96 && runtime.Launch.ParentWindow is not null);

        var tabs = Field<TabControl>("_assetTabs");
        var reset = Field<Button>("_resetWorkspaceLayout");
        var grid = tabs.GetVisualAncestors().OfType<Grid>().Single(item => item.ColumnDefinitions.Count == 5);
        var splitters = grid.Children.OfType<GridSplitter>().ToArray();
        TabItem Tab(WorkspaceTab tab) => tabs.Items.OfType<TabItem>().Single(item => Equals(item.Tag, tab));
        WorkspacePaneWidths Widths() => new(grid.ColumnDefinitions[0].ActualWidth, grid.ColumnDefinitions[2].ActualWidth, grid.ColumnDefinitions[4].ActualWidth);
        static bool Near(double one, double two) => Math.Abs(one - two) < 1.1;
        bool Matches(WorkspaceLayout layout)
        {
            var actual = Widths();
            return layout.Project(grid.Bounds.Width) is { } projected && Near(actual.Hierarchy, projected.Hierarchy) &&
                Near(actual.Viewport, projected.Viewport) && Near(actual.Right, projected.Right);
        }
        static PixelPoint Center(Control control) => control.PointToScreen(new Point(control.Bounds.Width / 2, control.Bounds.Height / 2));
        async Task Tap(PixelPoint point)
        {
            if (GetForegroundWindow() != hwnd || !SetCursorPos(point.X, point.Y)) throw new InvalidOperationException("Native pointer unavailable.");
            await Task.Delay(60);
            mouse_event(2, 0, 0, 0, 0);
            await Task.Delay(60);
            mouse_event(4, 0, 0, 0, 0);
            await Task.Delay(60);
        }
        async Task Capture(string name)
        {
            File.WriteAllText(Path.Combine(evidence, name + ".ready"), Environment.ProcessId.ToString());
            await NameUiWait(() => File.Exists(Path.Combine(evidence, name + ".captured")), "native capture " + name, 45);
        }
        async Task Drag(GridSplitter splitter, int dx)
        {
            await NameUiWait(() => Matches(store.Read()) && GetForegroundWindow() == hwnd, "settled splitter layout");
            var point = Center(splitter);
            if (!SetCursorPos(point.X, point.Y)) throw new InvalidOperationException("Native splitter unavailable.");
            await Task.Delay(70);
            mouse_event(2, 0, 0, 0, 0);
            try
            {
                for (int i = 1; i <= 5; ++i)
                {
                    SetCursorPos(point.X + dx * i / 5, point.Y);
                    await Task.Delay(42);
                }
                Verify("the real splitter drag disables reset while captured", !reset.IsEnabled, $"{point} to x={point.X + dx}");
            }
            finally { mouse_event(4, 0, 0, 0, 0); }
        }

        if (phase == "seed")
        {
            Verify("a new layout opens with defaults and creates no record", Matches(default) && !File.Exists(store.Pathname));
            await Drag(splitters[0], 165);
            await NameUiWait(() => store.Read().Preferred is { Hierarchy: > 300 }, "left splitter preference written");
            await Drag(splitters[1], -96);
            await NameUiWait(() => store.Read().Preferred is { Right: > 425 }, "right splitter preference written");
            var beforeKeyboard = store.Read().Preferred;
            splitters[0].Focus();
            splitters[0].RaiseEvent(new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Source = splitters[0], Key = Key.Right });
            await NameUiWait(() => store.Read().Preferred != beforeKeyboard, "keyboard splitter adjustment written");
            await Tap(Center(Tab(WorkspaceTab.SavedScenes)));
            await NameUiWait(() => store.Read().Tab == WorkspaceTab.SavedScenes, "native tab choice persisted");
            var savedTab = Tab(WorkspaceTab.SavedScenes);
            savedTab.Focus();
            savedTab.RaiseEvent(new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Source = savedTab, Key = Key.Left });
            await NameUiWait(() => store.Read().Tab == WorkspaceTab.Assets, "keyboard tab choice persisted");
            await Tap(Center(savedTab));
            await NameUiWait(() => store.Read().Tab == WorkspaceTab.SavedScenes, "native tab choice restored");
            Verify("actual side widths and stable tab round-trip through a closed record", Matches(store.Read()) &&
                ReferenceEquals(tabs.SelectedItem, Tab(WorkspaceTab.SavedScenes)));
            await Capture("seed");
            return;
        }

        if (phase == "fallback")
        {
            var prior = store.Read();
            byte[] bytes = File.ReadAllBytes(store.Pathname);
            Verify("launch without Saved Scenes restores Inspector without granting a tab or modifying preferences", prior.Tab == WorkspaceTab.SavedScenes &&
                tabs.ItemCount == 2 && ReferenceEquals(tabs.SelectedItem, Tab(WorkspaceTab.Inspector)) && Matches(prior) &&
                File.ReadAllBytes(store.Pathname).SequenceEqual(bytes));
            tabs.SelectedItem = Tab(WorkspaceTab.Assets);
            await Dispatcher.UIThread.InvokeAsync(() => { }, DispatcherPriority.Background);
            Verify("programmatic tab navigation never writes the preference", File.ReadAllBytes(store.Pathname).SequenceEqual(bytes));
            tabs.SelectedItem = Tab(WorkspaceTab.Inspector);
            await Capture("fallback");
            await Tap(Center(Tab(WorkspaceTab.Inspector)));
            await NameUiWait(() => store.Read().Tab == WorkspaceTab.Inspector, "same visible fallback tab picked explicitly");
            Verify("an explicit click of the visible fallback can replace its saved choice", store.Read().Preferred == prior.Preferred);
            return;
        }

        if (phase == "corrupt" || phase == "defaults")
        {
            Verify("defaults and Inspector after " + phase, Matches(default) && ReferenceEquals(tabs.SelectedItem, Tab(WorkspaceTab.Inspector)));
            if (phase == "corrupt")
            {
                Verify("corrupt record is intact and gives local feedback", File.ReadAllText(store.Pathname) == "{broken" &&
                    Field<TextBlock>("_workspaceLayoutFeedback").IsVisible);
                await Capture("corrupt");
                await Tap(Center(reset));
                await NameUiWait(() => !Field<TextBlock>("_workspaceLayoutFeedback").IsVisible && store.Read() == default, "explicit repair by reset");
            }
            else Verify("the durable reset survives a new process", store.Read() == default);
            return;
        }

        using var peer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        var saved = store.Read();
        byte[] savedBytes = File.ReadAllBytes(store.Pathname);
        Verify("restart restores the saved widths and authorized Saved Scenes tab", saved.Tab == WorkspaceTab.SavedScenes &&
            Matches(saved) && ReferenceEquals(tabs.SelectedItem, Tab(WorkspaceTab.SavedScenes)));
        await Capture("restart");
        double wide = window.Width;
        window.Width = 960;
        window.Height = 600;
        await NameUiWait(() => grid.Bounds.Width < 990 && Matches(saved), "compact saved layout arranged");
        var host = Field<ViewportHost>("_viewport");
        var viewport = host.Window!;
        Verify("compact clamping retains the Viewport minimum and untouched settings", Widths().Viewport >= 279 &&
            viewport.RuntimeChildIsHosted() && Near(viewport.Width, host.Bounds.Width) && File.ReadAllBytes(store.Pathname).SequenceEqual(savedBytes));
        await Capture("compact");

        var projection = typeof(MainWindow).GetMethod("ViewportProjection", BindingFlags.NonPublic | BindingFlags.Instance)!;
        CameraMatrices? matrices = null;
        await NameUiWait(() => (matrices = (CameraMatrices?)projection.Invoke(window, [viewport])) is not null, "current pick projection");
        var marker = Field<IReadOnlyList<Marker>>("_markers");
        var rect = new Atlantis.Editor.Model.PixelRect(0, 0, viewport.Width, viewport.Height);
        EntityGuid? expected = null;
        PixelPoint pixel = default;
        foreach (int y in new[] { viewport.Height / 2, viewport.Height * 3 / 5, viewport.Height * 7 / 10 })
        {
            foreach (int x in new[] { viewport.Width / 2, viewport.Width / 3, viewport.Width * 2 / 3 })
            {
                if (Picking.PickMarker(marker, matrices!, rect, x, y) is not null) continue;
                var ray = CameraConvention.RayThroughNdc(matrices!, (float)x / viewport.Width * 2 - 1, (float)y / viewport.Height * 2 - 1);
                if (peer.Control.Raycast(new Ray(ray.Origin, ray.Direction)).Hit is not { } hit) continue;
                expected = hit.Entity;
                var (left, top, _, _) = viewport.ScreenRect();
                pixel = new(left + x, top + y);
                break;
            }
            if (expected is not null) break;
        }
        if (expected is null) throw new InvalidOperationException("Could not find a representative geometry pick in the compact viewport.");
        await Tap(pixel);
        var selection = Field<Selection>("_selection");
        await NameUiWait(() => selection.Node == expected, "native compact click selects the expected Runtime hit");
        Verify("physical click is aligned with the independently queried hit after width changes", selection.Node == expected);

        window.Width = wide;
        window.Height = 860;
        await NameUiWait(() => grid.Bounds.Width > 1200 && Matches(saved), "preferred layout recovered after re-expanding");
        Verify("re-expanding recovers preferences without writing", File.ReadAllBytes(store.Pathname).SequenceEqual(savedBytes));
        // The tab switch is intentional but programmatic here. The widths
        // and current document are left in place for reset verification.
        tabs.SelectedItem = Tab(WorkspaceTab.Inspector);
        await NameUiWait(() => Field<InspectorSnapshot>("_inspectorShown")?.Subject == expected, "selected Inspector ready");
        const string transform = "Transform.localPosition";
        var draft = InspectorRow(window, transform).GetVisualDescendants().OfType<TextBox>().Single();
        var sourceState = peer.Authoring.DocumentState().Value!;
        byte[] sourceBytes = File.ReadAllBytes(sourceState.Source);
        Verify("focused Inspector vector editable", draft.Focus());
        draft.Text = "1.5 2.5 3.5";
        await NameUiWait(() => !reset.IsEnabled, "active scene draft guards reset");
        await Tap(Center(reset));
        window.Width = 960;
        await NameUiWait(() => grid.Bounds.Width < 990 && Matches(saved), "compact layout with a live draft");
        Verify("blocked reset and reflow preserve exact draft, focus and document revision", draft.IsFocused && !reset.IsEnabled &&
            draft.Text == "1.5 2.5 3.5" && peer.Authoring.DocumentState().Value!.Token == sourceState.Token &&
            File.ReadAllBytes(store.Pathname).SequenceEqual(savedBytes));
        NameUiKey(draft, Key.Escape);

        draft.Text = "1.25 2.5 3.75";
        NameUiKey(draft, Key.Enter);
        Field<Button>("_save").Focus();
        var undo = Field<Button>("_undo");
        var redo = Field<Button>("_redo");
        await NameUiWait(() => peer.Authoring.DocumentState().Value is { Dirty: true } && undo.IsEnabled && reset.IsEnabled &&
            Field<TextBlock>("_applyLine").Text?.StartsWith("apply: applied", StringComparison.Ordinal) == true, "committed dirty edit settles", 60);

        var beforeReset = peer.Authoring.DocumentState().Value;
        var runtimeBefore = peer.Control.Status();
        var previousUndo = undo.Content;
        var previousRedo = redo.Content;
        bool hadRedo = redo.IsEnabled;
        await Tap(Center(reset));
        await NameUiWait(() => store.Read() == default && Matches(default), "reset persists both defaults");
        var runtimeAfter = peer.Control.Status();
        Verify("one-click reset preserves dirty document, selection, history and Runtime state",
            Equals(beforeReset, peer.Authoring.DocumentState().Value) && File.ReadAllBytes(sourceState.Source).SequenceEqual(sourceBytes) &&
            selection.Node == expected && undo.IsEnabled && Equals(previousUndo, undo.Content) && redo.IsEnabled == hadRedo && Equals(previousRedo, redo.Content) &&
            runtimeAfter.SceneGeneration == runtimeBefore.SceneGeneration && runtimeAfter.Paused == runtimeBefore.Paused);

        bool oldCallbackRan = false;
        typeof(MainWindow).GetMethod("AfterWorkspaceInput", BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(window, [(Action)(() => { oldCallbackRan = true; store.SetTab(WorkspaceTab.Assets); })]);
        reset.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await Dispatcher.UIThread.InvokeAsync(() => { }, DispatcherPriority.Background);
        Verify("a reset fences pending same-window preference writes", !oldCallbackRan && store.Read() == default);
        await Capture("reset");
        undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => peer.Authoring.DocumentState().Value is { Dirty: false }, "disposable edit undone");
    }
}
