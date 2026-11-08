// Plan 0068 M3 (Spec 0068 R4; ADR-0135 D2): the Inspector's model against the
// fake document -- reflection from the schema (nested structs expanded,
// descriptor order, Transform first), the editor kind of each field, an
// optional field, one transaction per commit, refusals with their reasons,
// outside changes, and add / remove component.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.VisualTree;
using Avalonia.Interactivity;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;
using Command = Atlantis.Gameplay.Authoring.Command;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;
using AddComponent = Atlantis.Gameplay.Authoring.AddComponent;
using RemoveComponent = Atlantis.Gameplay.Authoring.RemoveComponent;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private sealed record NameUiCheck(string Name, bool Ok, string Detail = "");

    /// <summary>
    /// Runs the product MainWindow on Windows, with actual Avalonia controls,
    /// focus and routed keys. It does not inject OS input or capture a screen.
    /// A separate runner invokes this method so normal model tests stay headless.
    /// </summary>
    public static int RunNameUiSmoke(string runtimePath, string outputFile, string scene = "integrated_showcase_demo")
    {
        var checks = new List<NameUiCheck>();
        int exit = 1;
        string? work = null;
        var thread = new Thread(() =>
        {
            try
            {
                exit = AppBuilder.Configure(() => new NameUiApplication(runtimePath, scene, checks, path => work = path))
                                 .UsePlatformDetect().StartWithClassicDesktopLifetime(Array.Empty<string>());
            }
            catch (Exception e)
            {
                checks.Add(new NameUiCheck("UI lifetime completed", false, e.ToString()));
            }
        }) { Name = "Atlantis name UI smoke", IsBackground = true };
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        if (!thread.Join(TimeSpan.FromMinutes(6)))
        {
            Dispatcher.UIThread.Post(() =>
            {
                if (Application.Current?.ApplicationLifetime is IControlledApplicationLifetime desktop) desktop.Shutdown(1);
            });
            thread.Join(TimeSpan.FromSeconds(30));
            checks.Add(new NameUiCheck("bounded UI lifetime", false, "exceeded six minutes"));
            exit = 1;
        }
        if (checks.Any(c => !c.Ok) || checks.Count == 0) exit = 1;
        var report = new
        {
            kind = "product-name-asset-and-apply-ui-smoke",
            boundary = "Actual Windows MainWindow/worker/native Runtime; Avalonia focus and routed key/button/pointer events, plus controlled callback and Gizmo inputs. No OS scancode injection, UIAutomation, screenshots, timing or performance verdict.",
            runtime = Path.GetFullPath(runtimePath), workDirectory = work,
            passed = checks.Count(c => c.Ok), failed = checks.Count(c => !c.Ok), exitCode = exit, checks,
        };
        string json = System.Text.Json.JsonSerializer.Serialize(report, new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outputFile))!);
        File.WriteAllText(outputFile, json + "\n");
        Console.WriteLine(json);
        return exit;
    }

    private sealed class NameUiApplication : Application
    {
        private readonly string _runtime;
        private readonly string _scene;
        private readonly List<NameUiCheck> _checks;
        private readonly Action<string> _work;

        public NameUiApplication(string runtime, string scene, List<NameUiCheck> checks, Action<string> work)
        {
            _runtime = runtime; _scene = scene;
            _checks = checks;
            _work = work;
        }

        public override void Initialize() => Styles.Add(new FluentTheme());

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                bool legacyReadiness = _scene == "--legacy-readiness";
                bool rebuild = _scene == "--asset-rebuild";
                bool import = _scene == "--asset-import";
                var window = new MainWindow(new EditorOptions(_runtime,
                    legacyReadiness || rebuild || import ? "integrated_showcase_demo" : _scene)
                {
                    AssetBuildProfile = import ? Environment.GetEnvironmentVariable("ATLANTIS_0086_UI_PROFILE") :
                        rebuild ? Environment.GetEnvironmentVariable("ATLANTIS_0085_UI_PROFILE") : null,
                    AssetGeneration = import ? Environment.GetEnvironmentVariable("ATLANTIS_0086_UI_GENERATION") :
                        rebuild ? Environment.GetEnvironmentVariable("ATLANTIS_0085_UI_GENERATION") : null,
                });
                desktop.MainWindow = window;
                window.Opened += async (_, _) =>
                {
                    try
                    {
                        if (legacyReadiness) await CheckLegacyReadiness(window, _checks, _work);
                        else if (import) await CheckImportedMeshUi(window, _checks, _work);
                        else if (rebuild) await CheckAssetRebuildUi(window, _checks, _work);
                        else { await CheckNameUi(window, _checks, _work); await CheckAssetsUi(window, _checks); await CheckAssetPickerUi(window, _checks); }
                    }
                    catch (Exception e) { _checks.Add(new NameUiCheck("actual Name UI checks completed", false, e.ToString())); }
                    finally { desktop.Shutdown(_checks.Count > 0 && _checks.All(c => c.Ok) ? 0 : 1); }
                };
            }
            base.OnFrameworkInitializationCompleted();
        }
    }

    private static async Task CheckAssetRebuildUi(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        await NameUiWait(() => NameUiField<RuntimeProcess>(window, "_runtime") is { } process &&
            File.Exists(process.SessionPath), "managed Runtime session");
        var runtime = NameUiField<RuntimeProcess>(window, "_runtime")!;
        work(Path.GetDirectoryName(runtime.SessionPath)!);
        var tabs = NameUiField<TabControl>(window, "_assetTabs")!;
        tabs.SelectedIndex = 1;
        var browser = NameUiField<AssetBrowser>(window, "_assetBrowser")!;
        await NameUiWait(() => browser.State == "Ready", "managed asset catalog", 60);
        var row = browser.Visible.First(a => a.Source == "assets:materials/pbr_dielectric_rough.material.txt");
        NameUiField<ListBox>(window, "_assetsList")!.SelectedItem = row;
        var start = NameUiField<Button>(window, "_rebuildStart")!;
        var state = NameUiField<TextBlock>(window, "_rebuildState")!;
        await NameUiWait(() => start.IsEnabled, "selected producer is actionable");
        start.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        checks.Add(new("pending request disables duplicate rebuild", !start.IsEnabled));
        await NameUiWait(() => state.Text?.Contains(" · Running", StringComparison.Ordinal) == true, "running receipt", 30);
        using var observer = await Task.Run(() => RemoteSession.Connect(SessionFile.Read(runtime.SessionPath)));
        var running = await Task.Run(() => observer.Assets.RebuildStatus());
        checks.Add(new("SDK observes the same admitted job as Assets",
            running.IsOk && running.Value?.Job is { } known && known.Asset == row.Guid &&
            state.Text!.Contains(known.Id.ToString(), StringComparison.Ordinal)));
        await NameUiWait(() => state.Text?.Contains(" · Succeeded", StringComparison.Ordinal) == true,
            "Assets observes published generation", 180);
        var terminal = await Task.Run(() => observer.Assets.RebuildStatus(running.Value!.Job!.Id));
        checks.Add(new("publication includes explicit restart guidance",
            terminal.Value?.Job?.Generation is GenerationId generation &&
            state.Text!.Contains("--asset-generation " + generation, StringComparison.Ordinal)));
        checks.Add(new("publication does not select active generation or replace Assets selection",
            terminal.IsOk && terminal.Value?.ActiveGeneration?.ToString() == runtime.Launch.AssetGeneration &&
            browser.Selected == row.Guid));
    }

    private static async Task CheckImportedMeshUi(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        await NameUiWait(() => NameUiField<RuntimeProcess>(window, "_runtime") is { } process &&
            File.Exists(process.SessionPath) && NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")?.Count > 0,
            "imported generation and Hierarchy", 60);
        var runtime = NameUiField<RuntimeProcess>(window, "_runtime")!;
        work(Path.GetDirectoryName(runtime.SessionPath)!);
        using var observer = await Task.Run(() => RemoteSession.Connect(SessionFile.Read(runtime.SessionPath)));
        var status = await Task.Run(() => observer.Assets.ImportStatus());
        checks.Add(new("SDK reads the selected import generation",
            status.IsOk && status.Value?.ActiveGeneration?.ToString() == runtime.Launch.AssetGeneration));
        if (Environment.GetEnvironmentVariable("ATLANTIS_0086_UI_INBOX_NAME") is string inbox)
        {
            var tabs = NameUiField<TabControl>(window, "_assetTabs")!;
            tabs.SelectedIndex = 1;
            NameUiField<TextBox>(window, "_importName")!.Text = inbox;
            var register = NameUiField<Button>(window, "_importRegister")!;
            await NameUiWait(() => register.IsEnabled, "import action is available");
            register.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var display = NameUiField<TextBlock>(window, "_importState")!;
            await NameUiWait(() => display.Text?.Contains(" · Succeeded", StringComparison.Ordinal) == true,
                "Assets panel observes the published import", 150);
            var observed = await Task.Run(() => observer.Assets.ImportStatus());
            checks.Add(new("Assets panel and SDK agree on the job and published generation",
                observed.IsOk && observed.Value?.Job is { Generation: GenerationId published } job && job.Name == inbox &&
                display.Text!.Contains(job.Id.ToString(), StringComparison.Ordinal) &&
                display.Text.Contains("--asset-generation " + published, StringComparison.Ordinal) &&
                observed.Value.ActiveGeneration?.ToString() == runtime.Launch.AssetGeneration));
            tabs.SelectedIndex = 0;
        }
        var assets = (await Task.Run(() => observer.Assets.ListAssets())).Value;
        var mesh = assets.Single(a => a.Source == "content:managed_sample/source.gltf#mesh/0/0");
        var schema = await Task.Run(() => observer.Authoring.Schema);
        TypeId renderable = SceneSchema.TypeOf(schema, "Renderable")!.Value;
        var subject = await Task.Run(() => observer.Authoring.ListNodes().Value
            .First(n => observer.Authoring.ListComponents(n.Node).Value.Contains(renderable)).Node);
        var rows = NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!;
        NameUiField<ListBox>(window, "_entities")!.SelectedItem = rows.First(r => r.Node == subject);
        await NameUiWait(() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.Subject == subject,
            "renderable Inspector");
        var address = SceneSchema.AddressOf(schema, subject, "Renderable", "meshAsset")!.Value;
        var document = await Task.Run(() => new RemoteAuthoringDocument(observer.Authoring, observer));
        AssetGuid Read() => document.GetProperty(address)!.Value.AsAsset();
        var original = await Task.Run(Read);
        var info = (await Task.Run(() => observer.Authoring.Document()))!;
        var allowed = Environment.GetEnvironmentVariable("ATLANTIS_0086_DISPOSABLE_SCENE");
        if (allowed is null || info.Provenance != Provenance.HandAuthored ||
            !Path.GetFullPath(allowed).Equals(Path.GetFullPath(info.SourcePath), StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The test requires an explicitly named disposable hand-authored scene");
        byte[] source = File.ReadAllBytes(info.SourcePath);
        void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Task<ulong> Generation() => Task.Run(() => observer.Control.Status().SceneGeneration);
        Task<byte[]> Capture(string name) => Task.Run(() =>
        {
            string path = Path.Combine(runtime.Launch.WorkDirectory, name + ".png");
            observer.Control.Pause();
            try
            {
                var step = observer.Control.Step(new StepRequest(1, path));
                if (step.Report?.Image is null || step.Report.Data.DrawItemCount == 0)
                    throw new InvalidOperationException("No rendered capture for " + name);
                return File.ReadAllBytes(path);
            }
            finally { observer.Control.Resume(); }
        });
        async Task WaitEdit(AssetGuid expected, ulong before, string label)
        {
            await NameUiWait(async () => await Task.Run(Read) == expected && await Generation() > before, label, 60);
            checks.Add(new(label, true));
        }

        try
        {
            var renderedBefore = await Capture("original-mesh");
            var row = NameUiField<StackPanel>(window, "_inspectorPanel")!.Children.OfType<Grid>()
                .Single(g => g.Children.OfType<TextBlock>().Any(t => t.Text == "meshAsset"));
            Click(row.Children.OfType<Button>().Single(b => b.Content?.ToString() == "Choose…"));
            AssetReferencePickerWindow? Picker() => NameUiField<AssetReferencePickerWindow>(window, "_referenceWindow");
            await NameUiWait(() => Picker() is { } p && PickerField<AssetBrowser>(p, "_browser").State == "Ready",
                "mesh picker uses activated catalog");
            var picker = Picker()!;
            var browser = PickerField<AssetBrowser>(picker, "_browser");
            PickerField<ListBox>(picker, "_list").SelectedItem = browser.Visible.Single(a => a.Guid == mesh.Guid);
            await NameUiWait(() => browser.Details?.Guid == mesh.Guid, "imported mesh detail");
            ulong before = await Generation();
            Click(PickerField<Button>(picker, "_confirm"));
            await WaitEdit(mesh.Guid, before, "typed picker commits the imported GUID and reloads");
            checks.Add(new("imported mesh produces a distinct rendered capture",
                !(await Capture("imported-mesh")).SequenceEqual(renderedBefore)));
            before = await Generation();
            Click(NameUiField<Button>(window, "_undo")!);
            await WaitEdit(original, before, "Undo restores the prior mesh and reloads");
            before = await Generation();
            Click(NameUiField<Button>(window, "_redo")!);
            await WaitEdit(mesh.Guid, before, "Redo restores the imported mesh and reloads");
            Click(NameUiField<Button>(window, "_save")!);
            await NameUiWait(() => File.ReadAllText(info.SourcePath).Contains(mesh.Guid.ToString(), StringComparison.Ordinal),
                "Save writes imported GUID");
            checks.Add(new("Save persists the literal derived mesh GUID", true));

            using var restarted = RuntimeProcess.Start(runtime.Launch with
                { ParentWindow = null, WorkDirectory = RuntimeProcess.NewWorkDirectory() });
            using var reopened = await Task.Run(() => RemoteSession.Connect(restarted.WaitForSession(TimeSpan.FromSeconds(60))));
            var opened = await Task.Run(() => reopened.Authoring.Open());
            var reopenedDoc = await Task.Run(() => new RemoteAuthoringDocument(reopened.Authoring, reopened));
            checks.Add(new("fresh Runtime authoring opens the saved reference",
                opened is null && (await Task.Run(() => reopenedDoc.GetProperty(address)))?.AsAsset() == mesh.Guid));
            ulong prior = (await Task.Run(() => reopened.Control.Status())).SceneGeneration;
            var applied = await Task.Run(() => reopened.Authoring.ApplyTracked());
            if (!applied.IsOk) throw new InvalidOperationException("Saved scene apply refused: " + applied.Error);
            await NameUiWait(async () => (await Task.Run(() => reopened.Control.Status())).SceneGeneration > prior,
                "saved imported reference reloads in the fresh Runtime", 60);
            checks.Add(new("saved reference applies under the same generation", true));
            restarted.CloseMainWindow();
        }
        finally { File.WriteAllBytes(info.SourcePath, source); }
    }

    private static async Task CheckLegacyReadiness(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        var edit = NameUiField<TextBlock>(window, "_editLine")!;
        await NameUiWait(() => edit.Text?.Contains("BadRequest", StringComparison.Ordinal) == true,
            "legacy Runtime refusal at read-only readiness");
        var runtime = NameUiField<RuntimeProcess>(window, "_runtime")!;
        work(Path.GetDirectoryName(runtime.SessionPath)!);
        using var observer = await Task.Run(() => RemoteSession.Connect(SessionFile.Read(runtime.SessionPath)));
        bool unopened = await Task.Run(() => observer.Authoring.Document() is null);
        checks.Add(new NameUiCheck("legacy Runtime refused before open or history", unopened &&
            !NameUiField<Button>(window, "_undo")!.IsEnabled &&
            !NameUiField<Button>(window, "_retryApply")!.IsEnabled, edit.Text!));
    }

    private static T PickerField<T>(AssetReferencePickerWindow window, string name) where T : class =>
        (T)typeof(AssetReferencePickerWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;

    private static async Task CheckAssetPickerUi(MainWindow window, List<NameUiCheck> checks)
    {
        void Check(string name, bool ok, string detail = "") => checks.Add(new NameUiCheck("picker UI: " + name, ok, detail));
        var runtime = NameUiField<RuntimeProcess>(window, "_runtime")!;
        using var observer = await Task.Run(() => RemoteSession.Connect(SessionFile.Read(runtime.SessionPath)));
        var document = await Task.Run(() => new RemoteAuthoringDocument(observer.Authoring, observer));
        NameUiField<TextBox>(window, "_notes")!.Focus();
        await Task.Delay(100);
        var snapshot = NameUiField<InspectorSnapshot>(window, "_inspectorShown")!;
        EntityGuid node = snapshot.Subject!.Value;
        SceneAddress address = SceneSchema.AddressOf(document.Schema, node, "Renderable", "materialAsset")!.Value;
        async Task<PropertyValue> ReadValue() => await Task.Run(() => document.GetProperty(address)!.Value);
        PropertyValue original = await ReadValue();
        DocumentInfo info = (await Task.Run(() => observer.Authoring.Document()))!;
        byte[]? sourceBefore = info.Provenance == Provenance.HandAuthored ? File.ReadAllBytes(info.SourcePath) : null;
        string? brokenArtifact = null;
        byte[]? artifactBefore = null;
        AssetGuid replacement = (await Task.Run(() => observer.Assets.ListAssets())).Value.First(a => a.Type == Atlantis.Gameplay.Assets.AssetType.Material &&
            a.Source.StartsWith("assets:", StringComparison.Ordinal) && a.Guid != original.AsAsset()).Guid;
        var undo = NameUiField<Button>(window, "_undo")!;
        var redo = NameUiField<Button>(window, "_redo")!;
        Grid Row() => NameUiField<StackPanel>(window, "_inspectorPanel")!.Children.OfType<Grid>()
            .Single(g => g.Children.OfType<TextBlock>().Any(t => t.Text == "materialAsset"));
        TextBox Box() => Row().Children.OfType<TextBox>().Single();
        Button Choose() => Row().Children.OfType<Button>().Single(b => b.Content?.ToString() == "Choose…");
        AssetReferencePickerWindow? Picker() => NameUiField<AssetReferencePickerWindow>(window, "_referenceWindow");
        bool Busy() => (bool)typeof(MainWindow).GetField("_referenceBusy", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;
        bool Applied() => NameUiField<TextBlock>(window, "_applyLine")!.Text!.StartsWith("apply: applied", StringComparison.Ordinal);
        string ApplyText() => NameUiField<TextBlock>(window, "_applyLine")!.Text ?? "";
        Button Retry() => NameUiField<Button>(window, "_retryApply")!;
        void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        async Task ApplyCurrent(string stage)
        {
            ulong before = (await Task.Run(() => observer.Control.Status())).SceneGeneration;
            await NameUiWait(() => Retry().IsEnabled, stage + " Retry enabled"); Click(Retry());
            await NameUiWait(async () => Applied() && (await Task.Run(() => observer.Control.Status())).SceneGeneration > before, stage, 60);
        }
        async Task Refreshed() => await NameUiWait(async () => !Busy() && window.IsEffectivelyEnabled && Box().Text ==
            ((await ReadValue()).IsAbsent ? "" : (await ReadValue()).AsAsset().ToString()), "picker target Inspector refreshed");
        async Task<AssetReferencePickerWindow> Open()
        {
            Box().Focus();
            Check("F4 handled", NameUiKey(Box(), Key.F4));
            await NameUiWait(() => Picker() is not null && PickerField<AssetBrowser>(Picker()!, "_browser").State == "Ready", "chooser opens and lists", 60);
            return Picker()!;
        }
        async Task Cancel(AssetReferencePickerWindow p)
        { p.Close(); await Refreshed(); }
        async Task Select(AssetReferencePickerWindow p, AssetGuid guid)
        {
            var browser = PickerField<AssetBrowser>(p, "_browser");
            PickerField<ListBox>(p, "_list").SelectedItem = browser.Visible.First(a => a.Guid == guid);
            await NameUiWait(() => browser.Details?.Guid == guid && browser.DetailState == "Ready", "chooser selected detail");
        }
        async Task Confirm(AssetReferencePickerWindow p)
        { PickerField<Button>(p, "_confirm").RaiseEvent(new RoutedEventArgs(Button.ClickEvent)); await Refreshed(); }
        try
        {
            await Refreshed();
            await NameUiWait(Applied, "previous Assets UI apply settles before picker baseline");
            ulong generation = (await Task.Run(() => observer.Control.Status())).SceneGeneration;
            var history = (undo.Content?.ToString(), redo.Content?.ToString(), undo.IsEnabled, redo.IsEnabled);
            var assetTab = NameUiField<AssetBrowser>(window, "_assetBrowser")!;
            var tabState = (assetTab.Selected, assetTab.Search, assetTab.Type);
            TextBox box = Box(); Button button = Choose(); box.Focus();
            var modifiedF4 = new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Source = box, Key = Key.F4, KeyModifiers = KeyModifiers.Control };
            box.RaiseEvent(modifiedF4);
            Check("modified F4 is not captured by chooser", !modifiedF4.Handled && !Busy() && Picker() is null);
            box.Text = replacement.ToString();
            var order = new List<string>();
            box.LostFocus += (_, _) => order.Add("lost-focus"); button.Click += (_, _) => order.Add("click");
            var pointer = new Avalonia.Input.Pointer(82, PointerType.Mouse, true);
            Point point = button.TranslatePoint(new Point(3, 3), window)!.Value;
            button.RaiseEvent(new PointerPressedEventArgs(button, pointer, window, point, 1,
                new PointerPointProperties(RawInputModifiers.LeftMouseButton, PointerUpdateKind.LeftButtonPressed), KeyModifiers.None, 1) { Source = button });
            button.RaiseEvent(new PointerReleasedEventArgs(button, pointer, window, point, 2,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.LeftButtonReleased), KeyModifiers.None, MouseButton.Left) { Source = button });
            await NameUiWait(() => Picker() is not null && PickerField<AssetBrowser>(Picker()!, "_browser").State == "Ready", "pointer chooser launch");
            var chooser = Picker()!;
            Check("pointer LostFocus precedes actual Click", order.SequenceEqual(new[] { "lost-focus", "click" }), string.Join(",", order));
            Check("pointer launch discarded uncommitted draft", chooser.Target.Before.Equals(original) && (await ReadValue()).Equals(original));
            Check("single owner-modal window", chooser.Owner == window && chooser.IsVisible);
            await Cancel(chooser);
            Check("cancel adds no edit/history/apply", (await ReadValue()).Equals(original) && (await Task.Run(() => observer.Control.Status())).SceneGeneration == generation &&
                history == (undo.Content?.ToString(), redo.Content?.ToString(), undo.IsEnabled, redo.IsEnabled),
                $"generation {generation}/{(await Task.Run(() => observer.Control.Status())).SceneGeneration}; history {history}/{(undo.Content?.ToString(), redo.Content?.ToString(), undo.IsEnabled, redo.IsEnabled)}");
            Check("Assets tab state independent", tabState == (assetTab.Selected, assetTab.Search, assetTab.Type));

            Box().Focus(); Box().Text = replacement.ToString();
            chooser = await Open();
            Check("F4 launch discards draft", chooser.Target.Before.Equals(original));
            var deselectBrowser = PickerField<AssetBrowser>(chooser, "_browser");
            var deselectList = PickerField<ListBox>(chooser, "_list");
            var cancelledCandidate = deselectBrowser.Visible.First(a => a.Guid != original.AsAsset());
            await Select(chooser, cancelledCandidate.Guid);
            var cancelledDetails = deselectBrowser.Details!;
            // No dispatcher yield between starting a real pending request and Ctrl-deselect.
            deselectList.SelectedItem = null;
            deselectList.SelectedItem = deselectBrowser.Visible.First(a => a.Guid == cancelledCandidate.Guid);
            long cancelledRequest = (long)typeof(AssetBrowser).GetField("_detailRequest", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(deselectBrowser)!;
            Check("different candidate detail pending before Ctrl-deselect", cancelledCandidate.Guid != original.AsAsset() && deselectBrowser.DetailState == "Loading");
            chooser.UpdateLayout();
            var selectedRow = (ListBoxItem)deselectList.ContainerFromIndex(deselectList.SelectedIndex)!;
            Point rowPoint = selectedRow.TranslatePoint(new Point(5, 5), chooser)!.Value;
            var ctrlPointer = new Avalonia.Input.Pointer(328, PointerType.Mouse, true);
            selectedRow.RaiseEvent(new PointerPressedEventArgs(selectedRow, ctrlPointer, chooser, rowPoint, 1,
                new PointerPointProperties(RawInputModifiers.LeftMouseButton | RawInputModifiers.Control, PointerUpdateKind.LeftButtonPressed), KeyModifiers.Control, 1));
            selectedRow.RaiseEvent(new PointerReleasedEventArgs(selectedRow, ctrlPointer, chooser, rowPoint, 2,
                new PointerPointProperties(RawInputModifiers.Control, PointerUpdateKind.LeftButtonReleased), KeyModifiers.Control, MouseButton.Left));
            var deselectConfirm = PickerField<Button>(chooser, "_confirm");
            Check("Ctrl-deselect clears control/model/details and disables Confirm", deselectList.SelectedItem is null &&
                deselectBrowser.Selected is null && deselectBrowser.Details is null && !deselectConfirm.IsEnabled);
            deselectConfirm.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check("Confirm after deselection cannot submit or close", chooser.IsVisible && ReferenceEquals(Picker(), chooser) && (await ReadValue()).Equals(original));
            bool priorCallbackDrained = false;
            NameUiField<EditorWorker>(window, "_worker")!.ReadAsset(cancelledCandidate.Guid, _ => priorCallbackDrained = true);
            await NameUiWait(() => priorCallbackDrained, "actual earlier detail callback drained after deselection");
            bool? cancelledAccepted = null;
            Dispatcher.UIThread.Post(() =>
            {
                cancelledAccepted = deselectBrowser.CompleteDetail(cancelledRequest, cancelledCandidate.Guid,
                    Outcome<Atlantis.Gameplay.Assets.AssetDetails, Atlantis.Gameplay.Assets.AssetError>.Ok(cancelledDetails with { Tool = "late cancelled detail" }));
                typeof(AssetReferencePickerWindow).GetMethod("Render", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(chooser, null);
            });
            await NameUiWait(() => cancelledAccepted is not null, "controlled old detail arrives after deselection");
            Check("late real/controlled detail cannot revive candidate or Confirm", cancelledAccepted == false &&
                deselectList.SelectedItem is null && deselectBrowser.Selected is null && deselectBrowser.Details is null && !deselectConfirm.IsEnabled &&
                PickerField<StackPanel>(chooser, "_details").Children.OfType<TextBlock>().All(b => b.Text?.Contains("late cancelled detail", StringComparison.Ordinal) != true));
            deselectConfirm.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check("Confirm after old callback leaves target and generation unchanged", chooser.IsVisible &&
                (await ReadValue()).Equals(original) && (await Task.Run(() => observer.Control.Status())).SceneGeneration == generation);
            var search = chooser.GetVisualDescendants().OfType<TextBox>().Single(b => b.PlaceholderText == "Source or GUID");
            search.Text = "no-match-0082";
            await NameUiWait(() => PickerField<AssetBrowser>(chooser, "_browser").ViewState == "No matching assets", "picker local filter");
            Check("hidden candidate cannot confirm", !PickerField<Button>(chooser, "_confirm").IsEnabled);
            search.Text = "";
            await NameUiWait(() => PickerField<AssetBrowser>(chooser, "_browser").Search == "", "picker reset filter");
            await Select(chooser, original.AsAsset());
            await Confirm(chooser);
            Check("same-value Confirm is no-op", (await Task.Run(() => observer.Control.Status())).SceneGeneration == generation && history == (undo.Content?.ToString(), redo.Content?.ToString(), undo.IsEnabled, redo.IsEnabled),
                $"generation {generation}/{(await Task.Run(() => observer.Control.Status())).SceneGeneration}; history {history}/{(undo.Content?.ToString(), redo.Content?.ToString(), undo.IsEnabled, redo.IsEnabled)}");

            // The button gained keyboard focus only after ordinary field LostFocus.
            Box().Focus(); Box().Text = replacement.ToString(); Choose().Focus(NavigationMethod.Tab);
            await NameUiWait(async () => (await ReadValue()).AsAsset() == replacement && Applied(), "ordinary Tab commit");
            Check("prior Tab commit remains ordinary edit", (await ReadValue()).AsAsset() == replacement);
            Choose().RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => Picker() is not null, "keyboard button activation after Tab");
            chooser = Picker()!;
            Check("button activation captures post-Tab worker value", chooser.Target.Before.AsAsset() == replacement);
            await Cancel(chooser);
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).Equals(original) && Applied(), "undo prior Tab commit"); await Refreshed();

            chooser = await Open();
            var browser = PickerField<AssetBrowser>(chooser, "_browser");
            var list = PickerField<ListBox>(chooser, "_list");
            list.SelectedItem = null; list.SelectedItem = browser.Visible.First(a => a.Guid == replacement);
            long oldDetail = (long)typeof(AssetBrowser).GetField("_detailRequest", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(browser)!;
            chooser.GetVisualDescendants().OfType<Button>().Single(b => b.Content?.ToString() == "Reload view").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => browser.State == "Ready" && browser.Details?.Guid == replacement && browser.DetailState == "Ready", "chooser pending-detail Reload");
            Check("Reload queries same snapshot and settles selected detail", browser.Details == (await Task.Run(() => observer.Assets.GetAsset(replacement))).Value ||
                Json.Write(Codec.Encode(browser.Details!)) == Json.Write(Codec.Encode((await Task.Run(() => observer.Assets.GetAsset(replacement))).Value)));
            Check("late detail refused after Reload", !browser.CompleteDetail(oldDetail, replacement,
                Outcome<Atlantis.Gameplay.Assets.AssetDetails, Atlantis.Gameplay.Assets.AssetError>.Ok(browser.Details! with { Tool = "late" })));
            long staleList = browser.BeginList(); await Cancel(chooser);
            chooser = await Open();
            Check("closed chooser callbacks rejected after reopen", !browser.CompleteList(staleList, (await Task.Run(() => observer.Assets.ListAssets()))));
            await Select(chooser, replacement);
            // Close on Confirm must not cancel the worker request; duplicate button input cannot submit twice.
            var confirm = PickerField<Button>(chooser, "_confirm");
            generation = (await Task.Run(() => observer.Control.Status())).SceneGeneration;
            confirm.RaiseEvent(new RoutedEventArgs(Button.ClickEvent)); confirm.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check("Confirm closes while request is pending", !chooser.IsVisible && Busy() && !Choose().IsEnabled);
            await NameUiWait(async () => !Busy() && (await ReadValue()).AsAsset() == replacement && Applied() && (await Task.Run(() => observer.Control.Status())).SceneGeneration > generation, "chooser transaction and apply settle");
            Check("shared SDK observes actual chooser edit", (await ReadValue()).AsAsset() == replacement);
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).Equals(original) && Applied(), "picker Undo");
            Check("one Undo restores original", (await ReadValue()).Equals(original));
            redo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).AsAsset() == replacement && Applied(), "picker Redo");
            Check("Redo restores chosen GUID", (await ReadValue()).AsAsset() == replacement);
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).Equals(original) && Applied(), "picker restore"); await Refreshed();

            chooser = await Open();
            await Task.Run(() => { observer.Authoring.SubmitTransaction(new Command[] { new SetProperty(address, PropertyValue.FromAsset(replacement)) }); });
            _ = observer.Authoring.DrainFailures();
            await Task.Run(() => { observer.Authoring.SubmitTransaction(new Command[] { new SetProperty(address, original) }); _ = observer.Authoring.DrainFailures(); });
            await NameUiWait(() => !PickerField<Button>(chooser, "_confirm").IsEnabled, "change-and-restore invalidates target");
            Check("observed target restoration cannot revive chooser", !PickerField<Button>(chooser, "_confirm").IsEnabled);
            await Cancel(chooser);
            chooser = await Open();
            EntityGuid other = NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!.First(r => r.Node != node).Node;
            var worker = NameUiField<EditorWorker>(window, "_worker")!;
            worker.ShowSelection(other); worker.ShowSelection(node);
            await NameUiWait(() => !PickerField<Button>(chooser, "_confirm").IsEnabled, "worker selection changes invalidate");
            Check("selection away and back remains stale", !PickerField<Button>(chooser, "_confirm").IsEnabled);
            await Cancel(chooser);
            chooser = await Open(); await Select(chooser, replacement); await Confirm(chooser);
            await NameUiWait(Applied, "chooser edit before persistence check");
            if (sourceBefore is not null)
            {
                Check("chosen reference Save accepted", (await Task.Run(() => observer.Authoring.Save())) is null);
                Check("chosen reference reopen accepted", (await Task.Run(() => observer.Authoring.Open())) is null);
                await NameUiWait(async () => (await ReadValue()).AsAsset() == replacement, "saved chooser reference reopened");
                Check("Save and reopen preserve chosen GUID", (await ReadValue()).AsAsset() == replacement);
                File.WriteAllBytes(info.SourcePath, sourceBefore);
                await Task.Run(() => observer.Authoring.Open());
                await NameUiWait(() => ApplyText().Contains("reopened", StringComparison.Ordinal), "source restore reopen observed");
                await ApplyCurrent("explicit apply after restoring source");
            }
            else
            {
                Check("imported Save retains refusal", (await Task.Run(() => observer.Authoring.Save())) == SaveError.ImportedSceneNotSavable);
                await Task.Run(() => { observer.Authoring.SubmitTransaction(new Command[] { new SetProperty(address, original) }); _ = observer.Authoring.DrainFailures(); observer.Authoring.Apply(); });
            }
            await NameUiWait(async () => (await ReadValue()).Equals(original) && Applied(), "persistence fixture restored"); await Refreshed();

            // A removed Inspector row cannot redirect its launch to the new node.
            var entities = NameUiField<ListBox>(window, "_entities")!;
            Button staleChoose = Choose();
            var rowsNow = NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!;
            entities.SelectedItem = rowsNow.First(r => r.Node != node);
            staleChoose.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check("stale Inspector row cannot launch on another node", !Busy() && Picker() is null);
            entities.SelectedItem = rowsNow.First(r => r.Node == node);
            await NameUiWait(() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.Subject == node, "selection restored for launch failure");
            worker.ShowSelection(other);
            Box().Focus(); Box().Text = replacement.ToString(); NameUiKey(Box(), Key.F4);
            await NameUiWait(() => !Busy(), "worker refuses outdated capture");
            Check("launch failure cannot submit replaced draft", Picker() is null && (await ReadValue()).Equals(original));
            worker.ShowSelection(node);
            await NameUiWait(() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.Subject == node, "worker selection restored after refusal");

            // Optional absence is a value state, not a failed read.
            Row().Children.OfType<Button>().Single(b => b.Content?.ToString() == "×").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).IsAbsent && Applied(), "clear optional material"); await Refreshed();
            chooser = await Open();
            Check("optional absent target captured", chooser.Target.Before.IsAbsent);
            await Select(chooser, original.AsAsset()); await Confirm(chooser);
            await NameUiWait(async () => (await ReadValue()).Equals(original) && Applied(), "assign absent material");
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).IsAbsent && Applied(), "undo absent assignment");
            Check("Undo preserves optional absence", (await ReadValue()).IsAbsent);
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).Equals(original) && Applied(), "undo optional clear"); await Refreshed();

            NameUiField<Button>(window, "_pause")!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => NameUiField<Button>(window, "_play")!.IsEnabled, "Runtime paused");
            chooser = await Open(); await Select(chooser, replacement); await Confirm(chooser);
            await NameUiWait(() => NameUiField<TextBlock>(window, "_applyLine")!.Text!.Contains("paused", StringComparison.Ordinal), "chooser apply held while paused");
            Check("paused confirmation commits document and holds apply", (await ReadValue()).AsAsset() == replacement);
            NameUiField<Button>(window, "_step")!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(Applied, "Step applies chooser edit");
            Check("Step releases held apply", Applied());
            NameUiField<Button>(window, "_play")!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => NameUiField<Button>(window, "_pause")!.IsEnabled, "Runtime resumed");
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).Equals(original) && Applied(), "restore paused choice"); await Refreshed();

            chooser = await Open(); await Select(chooser, replacement); await Confirm(chooser);
            await NameUiWait(Applied, "edit before outside overwrite");
            await Task.Run(() => { observer.Authoring.SubmitTransaction(new Command[] { new SetProperty(address, original) }); _ = observer.Authoring.DrainFailures(); observer.Authoring.Apply(); });
            await NameUiWait(() => Box().Text == original.AsAsset().ToString(), "outside overwrite observed");
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => NameUiField<TextBlock>(window, "_editLine")!.Text!.Contains("changed elsewhere", StringComparison.Ordinal), "conflicting Undo refused");
            Check("outside overwrite refuses picker history replay", (await ReadValue()).Equals(original));
            await NameUiWait(Applied, "outside apply settles"); await Refreshed();

            chooser = await Open();
            Check("document reopen accepted", (await Task.Run(() => observer.Authoring.Open())) is null);
            await NameUiWait(() => !PickerField<Button>(chooser, "_confirm").IsEnabled, "identical-value document reopen invalidates");
            Check("reopen with same GUID invalidates chooser", !PickerField<Button>(chooser, "_confirm").IsEnabled);
            await Cancel(chooser);
            await NameUiWait(() => ApplyText().Contains("reopened", StringComparison.Ordinal), "same-value reopen suspends automatic apply");
            await ApplyCurrent("apply current document after same-value reopen");

            // Normal text editing after chooser cancellation must still commit.
            Box().Focus(); Box().Text = replacement.ToString(); NameUiKey(Box(), Key.Enter); window.Focus();
            await NameUiWait(async () => (await ReadValue()).AsAsset() == replacement && Applied(), "normal text edit after cancel");
            Check("launch suppression does not leak into text editing", (await ReadValue()).AsAsset() == replacement);
            undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(async () => (await ReadValue()).Equals(original) && Applied(), "normal edit restored");

            AssetGuid wrongType = (await Task.Run(() => observer.Assets.ListAssets())).Value
                .First(x => x.Type == Atlantis.Gameplay.Assets.AssetType.Mesh).Guid;
            AssetGuid incompatible = wrongType;
            Click(NameUiField<Button>(window, "_pause")!);
            var stepButton = NameUiField<Button>(window, "_step")!;
            await NameUiWait(() => stepButton.IsEnabled, "pause before incompatible apply");
            ulong oldGeneration = (await Task.Run(() => observer.Control.Status())).SceneGeneration;
            chooser = await Open();
            if (chooser.Target.TargetAssetType is Atlantis.Gameplay.Assets.AssetType.Material)
            {
                var typed = PickerField<AssetBrowser>(chooser, "_browser");
                Check("material picker excludes meshes without losing the fixed catalog", typed.Items.Any(a => a.Guid == wrongType) &&
                    typed.Visible.Count != 0 && typed.Visible.All(a => a.Type == Atlantis.Gameplay.Assets.AssetType.Material));
                Check("eligible current GUID is preselected", typed.Selected == original.AsAsset());
                await Select(chooser, replacement);
                var typedDetail = typed.Details!;
                var render = typeof(AssetReferencePickerWindow).GetMethod("Render", BindingFlags.Instance | BindingFlags.NonPublic)!;
                var details = PickerField<StackPanel>(chooser, "_details");
                var typedConfirm = PickerField<Button>(chooser, "_confirm");
                // Drive the view's existing response boundary with synthetic facts to cover
                // states the immutable, populated catalog cannot return during this run.
                typed.CompleteDetail(typed.Select(replacement), replacement,
                    Outcome<Atlantis.Gameplay.Assets.AssetDetails, Atlantis.Gameplay.Assets.AssetError>.Ok(
                        typedDetail with { Dependencies = new[] { wrongType, original.AsAsset() } }));
                render.Invoke(chooser, null);
                var sameType = details.Children.OfType<Button>().Single(b => b.Content?.ToString() == original.AsAsset().ToString());
                Check("out-of-type dependency is readable without navigation", details.Children.OfType<TextBlock>().Any(t => t.Text == wrongType.ToString()));
                Click(sameType);
                await NameUiWait(() => typed.Details?.Guid == original.AsAsset(), "same-type dependency opens detail");
                Check("visible matching dependency becomes the candidate", typed.Selected == original.AsAsset() && typedConfirm.IsEnabled);
                typed.CompleteDetail(typed.Select(original.AsAsset()), original.AsAsset(),
                    Outcome<Atlantis.Gameplay.Assets.AssetDetails, Atlantis.Gameplay.Assets.AssetError>.Err(Atlantis.Gameplay.Assets.AssetError.UnknownAsset));
                render.Invoke(chooser, null);
                Check("detail refusal preserves eligible selection", typed.DetailState == "UnknownAsset" && typedConfirm.IsEnabled);
                typed.CompleteList(typed.BeginList(), Outcome<IReadOnlyList<Atlantis.Gameplay.Assets.AssetSummary>, Atlantis.Gameplay.Assets.AssetError>.Ok(
                    typed.Items.Where(a => a.Guid == wrongType).ToArray()));
                render.Invoke(chooser, null);
                var listState = PickerField<TextBlock>(chooser, "_state");
                Check("populated catalog with no typed match is distinct", listState.Text == "No assets of type Material" && !typedConfirm.IsEnabled);
                typed.CompleteList(typed.BeginList(), Outcome<IReadOnlyList<Atlantis.Gameplay.Assets.AssetSummary>, Atlantis.Gameplay.Assets.AssetError>.Ok(
                    Array.Empty<Atlantis.Gameplay.Assets.AssetSummary>()));
                render.Invoke(chooser, null);
                Check("empty catalog is distinct from no typed match", listState.Text == "Catalog empty" && !typedConfirm.IsEnabled);
                typed.CompleteList(typed.BeginList(), Outcome<IReadOnlyList<Atlantis.Gameplay.Assets.AssetSummary>, Atlantis.Gameplay.Assets.AssetError>.Err(
                    Atlantis.Gameplay.Assets.AssetError.CatalogUnavailable));
                render.Invoke(chooser, null);
                Check("catalog refusal disables confirmation", listState.Text == "CatalogUnavailable" && !typedConfirm.IsEnabled);
                await Cancel(chooser);
                var meshRow = NameUiField<StackPanel>(window, "_inspectorPanel")!.Children.OfType<Grid>()
                    .Single(g => g.Children.OfType<TextBlock>().Any(t => t.Text == "meshAsset"));
                var meshBox = meshRow.Children.OfType<TextBox>().Single();
                meshBox.Focus();
                Check("mesh field opens chooser", NameUiKey(meshBox, Key.F4));
                await NameUiWait(() => Picker() is not null && PickerField<AssetBrowser>(Picker()!, "_browser").State == "Ready", "mesh chooser lists");
                var meshPicker = Picker()!;
                var meshBrowser = PickerField<AssetBrowser>(meshPicker, "_browser");
                Check("mesh picker excludes materials", meshPicker.Target.TargetAssetType == Atlantis.Gameplay.Assets.AssetType.Mesh &&
                    meshBrowser.Visible.Count != 0 && meshBrowser.Visible.All(a => a.Type == Atlantis.Gameplay.Assets.AssetType.Mesh));
                await Cancel(meshPicker);

                var unlisted = AssetGuid.Parse("00840084-0084-4084-8084-008400840001");
                await Task.Run(() => observer.Authoring.Submit(new SetProperty(address, PropertyValue.FromAsset(unlisted))));
                Check("shared unlisted GUID remains authorable", (await Task.Run(() => observer.Authoring.DrainFailures())).Count == 0);
                await Refreshed();
                chooser = await Open();
                Check("unlisted current GUID stays visible without preselection",
                    PickerField<TextBlock>(chooser, "_current").Text == "Current: " + unlisted + " (outside this snapshot)" &&
                    !PickerField<Button>(chooser, "_confirm").IsEnabled);
                await Cancel(chooser);

                await Task.Run(() => observer.Authoring.Submit(new SetProperty(address, PropertyValue.FromAsset(wrongType))));
                Check("manual/shared wrong-type value remains authorable", (await Task.Run(() => observer.Authoring.DrainFailures())).Count == 0);
                await Refreshed();
                chooser = await Open();
                Check("wrong-type current GUID stays visible as text but is not preselected",
                    PickerField<TextBlock>(chooser, "_current").Text!.Contains("listed as Mesh", StringComparison.Ordinal) &&
                    !PickerField<Button>(chooser, "_confirm").IsEnabled && (await ReadValue()).AsAsset() == wrongType);
                await Cancel(chooser);
                await Task.Run(() => observer.Authoring.Submit(new SetProperty(address, original)));
                Check("shared setup restores the original reference", (await Task.Run(() => observer.Authoring.DrainFailures())).Count == 0);
                await Refreshed();

                // Only the explicitly supplied disposable build catalog may be used for mutation.
                string? catalog = Environment.GetEnvironmentVariable("ATLANTIS_0084_DISPOSABLE_CATALOG");
                string buildRoot = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(runtime.Launch.RuntimePath)!, "..", "..", ".."));
                string expectedCatalog = Path.Combine(buildRoot, "asset_catalog.txt");
                if (catalog is null || !StringComparer.OrdinalIgnoreCase.Equals(Path.GetFullPath(catalog), expectedCatalog))
                    throw new InvalidOperationException("typed UI recovery requires the disposable Runtime catalog");
                chooser = await Open();
                typed = PickerField<AssetBrowser>(chooser, "_browser");
                var initialDependencies = (await Task.Run(() => observer.Assets.GetAsset(info.Scene))).Value.Dependencies;
                incompatible = typed.Visible.First(a => a.Guid != replacement && a.Guid != original.AsAsset() &&
                    !initialDependencies.Contains(a.Guid)).Guid;
                await Select(chooser, incompatible);
                string record = File.ReadLines(expectedCatalog).Single(line =>
                    line.StartsWith("record: guid=" + incompatible + " ", StringComparison.Ordinal));
                string relative = record.Split(' ').Single(part => part.StartsWith("artifact=", StringComparison.Ordinal))[9..];
                brokenArtifact = Path.GetFullPath(Path.Combine(buildRoot, relative));
                if (!brokenArtifact.StartsWith(buildRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("fixture artifact is outside the disposable build");
                artifactBefore = File.ReadAllBytes(brokenArtifact);
                File.WriteAllBytes(brokenArtifact, Array.Empty<byte>());
                Check("type-matching broken artifact remains a valid catalog candidate",
                    typed.Details?.Type == Atlantis.Gameplay.Assets.AssetType.Material && PickerField<Button>(chooser, "_confirm").IsEnabled);
            }
            else
            {
                Check("0083 host retains generic picker without target metadata", chooser.Target.TargetAssetType is null &&
                    PickerField<AssetBrowser>(chooser, "_browser").Visible.Any(a => a.Guid == wrongType));
                await Select(chooser, incompatible);
            }
            await Confirm(chooser);
            await NameUiWait(() => ApplyText().Contains("pending: paused", StringComparison.Ordinal), "incompatible edit accepted and held");
            var pending = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!;
            Check("incompatible document edit accepted before Runtime result", pending.State == ApplyState.Pending &&
                (await ReadValue()).AsAsset() == incompatible && undo.IsEnabled);

            worker.BeginGizmoDrag(GizmoAxis.X);
            var drag = typeof(EditorWorker).GetField("_gizmo", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(worker) as GizmoDragController;
            await NameUiWait(() => drag?.Dragging == true, "uncommitted gesture active before failed Step");
            SceneAddress position = SceneSchema.AddressOf(document.Schema, node, "Transform", "localPosition")!.Value;
            var oldPosition = await Task.Run(() => document.GetProperty(position));
            Click(stepButton); worker.EndGizmoDrag(1, System.Diagnostics.Stopwatch.GetTimestamp());
            await NameUiWait(() => ApplyText().Contains("ScenePreparationFailed", StringComparison.Ordinal) && Retry().IsEnabled,
                "failure reaches UI even with no Applied event", 60);
            var failed = (await Task.Run(() => observer.Authoring.ApplyStatus(pending.Receipt.Id))).Value!;
            Check("typed failure retains document, history and old World generation", failed.Request?.State == ApplyState.Failed &&
                failed.Request.Error == ReloadError.ScenePreparationFailed && (await ReadValue()).AsAsset() == incompatible && undo.IsEnabled &&
                (await Task.Run(() => observer.Control.Status())).SceneGeneration == oldGeneration);
            bool drained = false; worker.ReadAsset(incompatible, _ => drained = true);
            await NameUiWait(() => drained, "late release drained after recovery invalidation");
            Check("recovery abandons gesture and its late release without a document edit", drag?.Dragging == false &&
                !NameUiField<ComboBox>(window, "_gizmoMode")!.IsEnabled && (await Task.Run(() => document.GetProperty(position))).Equals(oldPosition) &&
                (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.CurrentDocumentRevision == failed.CurrentDocumentRevision);

            string runtimeDir = Path.GetDirectoryName(runtime.Launch.RuntimePath)!;
            string cli = Path.GetFullPath(Path.Combine(runtimeDir, "..", "..", "cli", Path.GetFileName(runtimeDir), "atlantis.exe"));
            int cliExit = await Task.Run(() => IntegrationTest.Cli(cli, runtime.SessionPath, "--json", "scene", "apply-status", pending.Receipt.Id.ToString()));
            string cliLog = File.ReadAllText(Path.Combine(Path.GetDirectoryName(runtime.SessionPath)!, "name-cli.log"));
            Check("CLI sees the same failed receipt with nonzero exit", cliExit != 0 && cliLog.Contains(pending.Receipt.Id.ToString(), StringComparison.Ordinal) &&
                cliLog.Contains("ScenePreparationFailed", StringComparison.Ordinal));

            Click(undo); await NameUiWait(async () => (await ReadValue()).Equals(original), "Undo repair while suspended");
            Check("suspended Undo sends no apply", (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!.Receipt.Id == pending.Receipt.Id);
            Click(redo); await NameUiWait(async () => (await ReadValue()).AsAsset() == incompatible, "Redo while suspended");
            Check("suspended Redo sends no apply", (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!.Receipt.Id == pending.Receipt.Id);
            Click(Retry());
            await NameUiWait(() => ApplyText().Contains("pending: paused", StringComparison.Ordinal), "retry of still-invalid document");
            var repeated = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!;
            Click(stepButton);
            await NameUiWait(() => ApplyText().Contains("ScenePreparationFailed", StringComparison.Ordinal) && Retry().IsEnabled,
                "repeated failure returns to recovery", 60);
            Check("repeated failure is correlated and retains history", repeated.Receipt.Id != pending.Receipt.Id &&
                (await Task.Run(() => observer.Authoring.ApplyStatus(repeated.Receipt.Id))).Value!.Request!.State == ApplyState.Failed && undo.IsEnabled);
            if (artifactBefore is not null)
            {
                chooser = await Open();
                await Select(chooser, replacement);
                await Confirm(chooser);
                Check("filtered repair submits the document only while suspended", (await ReadValue()).AsAsset() == replacement &&
                    (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!.Receipt == repeated.Receipt);
                Click(undo);
                await NameUiWait(async () => (await ReadValue()).AsAsset() == incompatible, "Undo of suspended chooser repair");
            }
            Click(undo); await NameUiWait(async () => (await ReadValue()).Equals(original), "repair again before retry");
            var beforeSave = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!;
            Click(NameUiField<Button>(window, "_save")!);
            var saveNotice = NameUiField<TextBlock>(window, "_editLine")!;
            await NameUiWait(() => saveNotice.Text?.StartsWith(
                info.Provenance == Provenance.Imported ? "save refused" : "saved ", StringComparison.Ordinal) == true,
                "save during recovery");
            var afterSave = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!;
            Check("Save during recovery keeps revision, receipt and Runtime projection",
                afterSave == beforeSave && (await Task.Run(() => observer.Control.Status())).SceneGeneration == oldGeneration);
            Click(Retry()); Click(Retry());
            await NameUiWait(() => ApplyText().Contains("pending: paused", StringComparison.Ordinal) && !Retry().IsEnabled, "one explicit retry held");
            var repair = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!;
            Check("retry has a new receipt for the repaired revision", repair.Receipt.Id != pending.Receipt.Id && repair.State == ApplyState.Pending &&
                repair.Receipt.DocumentRevision > pending.Receipt.DocumentRevision);
            Click(redo); await NameUiWait(async () => (await ReadValue()).AsAsset() == incompatible, "local Redo during pending recovery");
            Click(undo); await NameUiWait(async () => (await ReadValue()).Equals(original), "local Undo during pending recovery");
            Check("local commits do not replace a pending recovery attempt",
                (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!.Receipt == repair.Receipt);
            Click(stepButton);
            await NameUiWait(async () => (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request is
                { State: ApplyState.Pending } next && next.Receipt.Id != repair.Receipt.Id, "one coalesced latest recovery after Step", 60);
            var coalesced = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!;
            Check("coalesced recovery waits for its own Step and leaves previews blocked",
                coalesced.Receipt.DocumentRevision > repair.Receipt.DocumentRevision &&
                !NameUiField<ComboBox>(window, "_gizmoMode")!.IsEnabled && !Retry().IsEnabled);
            Click(stepButton); await NameUiWait(Applied, "second Step settles coalesced recovery", 60);
            var repaired = (await Task.Run(() => observer.Authoring.ApplyStatus(coalesced.Receipt.Id))).Value!;
            Check("repair succeeds, restores preview and retains redo history", repaired.Request?.State == ApplyState.Applied &&
                repaired.Projection?.Receipt == coalesced.Receipt && repaired.CurrentDocumentRevision == coalesced.Receipt.DocumentRevision &&
                NameUiField<ComboBox>(window, "_gizmoMode")!.IsEnabled && redo.IsEnabled);

            Click(Retry());
            await NameUiWait(() => ApplyText().Contains("pending: paused", StringComparison.Ordinal), "local request before external replacement");
            var displaced = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!;
            var outsideApply = (await Task.Run(() => observer.Authoring.ApplyTracked())).Value!;
            await NameUiWait(() => ApplyText().Contains("superseded", StringComparison.OrdinalIgnoreCase) && Retry().IsEnabled, "external replacement observed");
            Check("external replacement is reported for the local receipt",
                (await Task.Run(() => observer.Authoring.ApplyStatus(displaced.Receipt.Id))).Value!.Request!.SupersededBy == outsideApply.Id);
            Click(Retry());
            await NameUiWait(() => ApplyText().Contains("pending: paused", StringComparison.Ordinal), "recovery request before external edit");
            var staleRevision = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!;
            Click(redo);
            await NameUiWait(async () => (await ReadValue()).AsAsset() == incompatible, "queue local Redo before external edit");
            Click(undo);
            await NameUiWait(async () => (await ReadValue()).Equals(original), "queue local Undo before external edit");
            await Task.Run(() => { observer.Authoring.SubmitTransaction(new Command[] { new SetProperty(address, original) }); });
            Check("external equal-value edit still advances revision", (await Task.Run(() => observer.Authoring.DrainFailures())).Count == 0 &&
                (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.CurrentDocumentRevision > staleRevision.Receipt.DocumentRevision);
            Click(stepButton);
            await NameUiWait(() => ApplyText().Contains("document/projection changed", StringComparison.Ordinal) && Retry().IsEnabled,
                "historical success keeps recovery suspended", 60);
            Check("external edit prevents queued recovery apply and keeps previews blocked",
                (await Task.Run(() => observer.Authoring.ApplyStatus(staleRevision.Receipt.Id))).Value!.Request!.State == ApplyState.Applied &&
                (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!.Receipt == staleRevision.Receipt &&
                !NameUiField<ComboBox>(window, "_gizmoMode")!.IsEnabled);

            Click(Retry()); await NameUiWait(() => ApplyText().Contains("pending: paused", StringComparison.Ordinal), "old snapshot held before shared reopen");
            var oldRequest = (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!.Request!;
            byte[] reopenBackup = File.ReadAllBytes(info.SourcePath);
            try
            {
                File.WriteAllText(info.SourcePath, "invalid disposable scene source");
                Check("refused shared reopen preserves document, history and recovery",
                    (await Task.Run(() => observer.Authoring.Open())) == OpenError.SourceParseFailed &&
                    (await Task.Run(() => observer.Authoring.ApplyStatus(oldRequest.Receipt.Id))).Value!.Request!.State == ApplyState.Pending &&
                    (await ReadValue()).Equals(original) && redo.IsEnabled && !Retry().IsEnabled);
            }
            finally { File.WriteAllBytes(info.SourcePath, reopenBackup); }
            Check("shared reopen succeeds with older request pending", (await Task.Run(() => observer.Authoring.Open())) is null);
            await NameUiWait(() => ApplyText().Contains("reopened", StringComparison.Ordinal) && !undo.IsEnabled && !redo.IsEnabled,
                "reopen clears history and invalidates local waiter");
            Check("shared reopen does not cancel the accepted snapshot", (await Task.Run(() => observer.Authoring.ApplyStatus(oldRequest.Receipt.Id))).Value!.Request!.State == ApplyState.Pending);
            Click(stepButton);
            await NameUiWait(async () => (await Task.Run(() => observer.Authoring.ApplyStatus(oldRequest.Receipt.Id))).Value!.Request!.State == ApplyState.Applied,
                "older snapshot can still swap after shared reopen", 60);
            Check("old successful snapshot cannot settle reopened document", !Applied() && !NameUiField<ComboBox>(window, "_gizmoMode")!.IsEnabled &&
                ApplyText().Contains("reopened", StringComparison.Ordinal) && !undo.IsEnabled);
            Click(NameUiField<Button>(window, "_play")!);
            await NameUiWait(() => !stepButton.IsEnabled, "resume before final explicit apply");
            await ApplyCurrent("reopened document explicitly projected"); await Refreshed();
        }
        finally
        {
            Picker()?.Close();
            if (brokenArtifact is not null && artifactBefore is not null)
            {
                File.WriteAllBytes(brokenArtifact, artifactBefore);
                Check("disposable artifact bytes restored", File.ReadAllBytes(brokenArtifact).SequenceEqual(artifactBefore));
            }
            if (sourceBefore is not null) File.WriteAllBytes(info.SourcePath, sourceBefore);
            await Task.Run(() => { observer.Authoring.SubmitTransaction(new Command[] { new SetProperty(address, original) }); _ = observer.Authoring.DrainFailures(); observer.Authoring.Apply(); });
        }
        var disconnectedChooser = await Open();
        using (var process = System.Diagnostics.Process.GetProcessById(runtime.Pid))
        { process.Kill(entireProcessTree: true); process.WaitForExit(); }
        await NameUiWait(() => !PickerField<Button>(disconnectedChooser, "_confirm").IsEnabled, "cached-list chooser invalidates on real session failure");
        Check("real transport failure disables cached-list Confirm", !PickerField<Button>(disconnectedChooser, "_confirm").IsEnabled);
        disconnectedChooser.Close();
    }



    private static T? NameUiField<T>(MainWindow window, string field) where T : class =>
        typeof(MainWindow).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window) as T;

    private static void NameUiSnapshot(MainWindow window, InspectorSnapshot snapshot) =>
        typeof(MainWindow).GetMethod("OnInspectorChanged", BindingFlags.Instance | BindingFlags.NonPublic)!
                          .Invoke(window, new object[] { snapshot });

    // Wire calls of the independent observer must not block the Windows dispatcher,
    // particularly while a dialog disables the owner of the native child window.
    private static async Task NameUiWait(Func<Task<bool>> ready, string purpose, int seconds = 30)
    {
        long deadline = Environment.TickCount64 + seconds * 1000L;
        while (!await ready())
        {
            if (Environment.TickCount64 >= deadline) throw new TimeoutException(purpose);
            await Task.Delay(25);
        }
    }

    private static async Task NameUiWait(Func<bool> ready, string purpose, int seconds = 30)
    {
        long deadline = Environment.TickCount64 + seconds * 1000L;
        while (!ready())
        {
            if (Environment.TickCount64 >= deadline) throw new TimeoutException(purpose);
            await Task.Delay(25);
        }
    }

    private static bool NameUiKey(TextBox box, Key key)
    {
        var input = new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Source = box, Key = key };
        box.RaiseEvent(input);
        return input.Handled;
    }

    private static async Task CheckNameUi(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        void Check(string name, bool ok, string detail = "") => checks.Add(new NameUiCheck(name, ok, detail));
        await NameUiWait(() => NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")?.Count > 0 &&
                               NameUiField<EditorWorker>(window, "_worker") is not null, "the actual editor connects", 120);
        RuntimeProcess runtime = NameUiField<RuntimeProcess>(window, "_runtime") ?? throw new InvalidOperationException("no hosted Runtime");
        work(runtime.Launch.WorkDirectory);
        Check("actual Windows MainWindow is visible", window.IsVisible && window.ClientSize.Width > 0 && window.ClientSize.Height > 0);
        var rows = NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!;
        HierarchyRow row = rows[0];
        NameUiField<ListBox>(window, "_entities")!.SelectedItem = row;
        await NameUiWait(() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.Subject == row.Node &&
                               NameUiField<TextBox>(window, "_nameBox") is not null, "the actual Name field appears");

        using RemoteSession observer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        SubscriptionId names = observer.Authoring.Subscribe(new Atlantis.Gameplay.Authoring.EventFilter(
            Atlantis.Gameplay.Authoring.EventKind.NodeNameChanged, row.Node));
        string ReadName() => observer.Authoring.ListNodes().Value!.First(n => n.Node == row.Node).Name;
        int DrainNames() => observer.Authoring.DrainEvents(names).Value!.Count;
        TextBox NameBox() => NameUiField<TextBox>(window, "_nameBox") ?? throw new InvalidOperationException("no Name TextBox");
        TextBox notes = NameUiField<TextBox>(window, "_notes")!;
        string original = ReadName();
        bool Applied() => NameUiField<TextBlock>(window, "_applyLine")?.Text?.StartsWith("apply: applied", StringComparison.Ordinal) == true;

        async Task Enter(string name, string check)
        {
            ulong beforeGeneration = observer.Control.Status().SceneGeneration;
            TextBox box = NameBox();
            Check(check + ": focus", box.Focus() && box.IsFocused);
            box.Text = name;
            Check(check + ": routed Enter handled", NameUiKey(box, Key.Enter));
            string prefix = name.Length > 0 ? name + " · " : row.Node.Value.ToString("N")[..8] + " · ";
            await NameUiWait(() => ReadName() == name && NameBox().Text == name && Applied() &&
                                   observer.Control.Status().SceneGeneration > beforeGeneration &&
                                   NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!
                                   .First(r => r.Node == row.Node).Label.StartsWith(prefix, StringComparison.Ordinal), check + " settles");
            Check(check + ": backend and actual field", ReadName() == name && NameBox().Text == name);
            Check(check + ": one name event", DrainNames() == 1);
        }

        async Task NoChange(string check, string expected, ulong generation)
        {
            // Ordinary functional settling, not a latency/performance threshold.
            await Task.Delay(250);
            Check(check + ": no mutation/event/apply", ReadName() == expected && DrainNames() == 0 &&
                  observer.Control.Status().SceneGeneration == generation);
        }

        void Outside(string name)
        {
            observer.Authoring.SubmitTransaction(new Command[] { new SetNodeName(row.Node, name) });
            if (observer.Authoring.DrainFailures().Count != 0) throw new InvalidOperationException("external rename refused");
        }

        try
        {
            Check("Name field starts at observed source value", NameBox().Text == original);
            _ = DrainNames();
            await Enter("UI smoke 灯", "Enter rename");
            Check("actual Hierarchy label follows rename", NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!
                  .First(r => r.Node == row.Node).Label.StartsWith("UI smoke 灯 · ", StringComparison.Ordinal));

            ulong generation = observer.Control.Status().SceneGeneration;
            TextBox same = NameBox();
            same.Focus();
            same.Text = "UI smoke 灯";
            Check("same-value Enter handled", NameUiKey(same, Key.Enter));
            await NoChange("same-value Enter", "UI smoke 灯", generation);

            TextBox escape = NameBox();
            Check("Escape focus", escape.Focus() && escape.IsFocused);
            escape.Text = "cancel this draft";
            Check("Escape routed key handled", NameUiKey(escape, Key.Escape));
            Check("Escape restores actual text", escape.Text == "UI smoke 灯");
            await NoChange("Escape", "UI smoke 灯", generation);

            TextBox unfocus = NameBox();
            Check("lost-focus draft focus", unfocus.Focus() && unfocus.IsFocused);
            unfocus.Text = "leave this draft";
            Check("focus moves to another real TextBox", notes.Focus() && notes.IsFocused);
            Check("LostFocus restores actual text", unfocus.Text == "UI smoke 灯");
            await NoChange("LostFocus", "UI smoke 灯", generation);

            TextBox invalid = NameBox();
            invalid.Focus();
            invalid.Text = new string('x', 256);
            NameUiKey(invalid, Key.Enter);
            await NameUiWait(() => NameUiField<TextBlock>(window, "_editLine")?.Text?.Contains("InvalidNodeName", StringComparison.Ordinal) == true,
                             "invalid name refusal is displayed");
            Check("invalid name refusal is displayed", NameUiField<TextBlock>(window, "_editLine")!.Text!.Contains("InvalidNodeName", StringComparison.Ordinal));
            await NoChange("invalid draft", "UI smoke 灯", generation);

            await Enter("", "clear name");
            Check("empty name uses actual fallback label", NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!
                  .First(r => r.Node == row.Node).Label.StartsWith(row.Node.Value.ToString("N")[..8] + " · ", StringComparison.Ordinal));
            generation = observer.Control.Status().SceneGeneration;
            NameUiField<Button>(window, "_undo")!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => ReadName() == "UI smoke 灯" && NameBox().Text == "UI smoke 灯" && Applied() &&
                                   observer.Control.Status().SceneGeneration > generation, "actual Undo button settles");
            Check("actual Undo button restores name", ReadName() == "UI smoke 灯" && DrainNames() == 1);
            generation = observer.Control.Status().SceneGeneration;
            NameUiField<Button>(window, "_redo")!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => ReadName() == "" && NameBox().Text == "" && Applied() &&
                                   observer.Control.Status().SceneGeneration > generation, "actual Redo button settles");
            Check("actual Redo button clears name", ReadName() == "" && DrainNames() == 1);

            TextBox old = NameBox();
            old.Focus();
            old.Text = "stale typed draft";
            Outside("external UI smoke");
            await NameUiWait(() => !ReferenceEquals(old, NameBox()) && NameBox().Text == "external UI smoke", "external rename cancels actual draft");
            Check("observed external rename cancels actual draft", !ReferenceEquals(old, NameBox()) && NameBox().Text == "external UI smoke");
            _ = DrainNames();
            generation = observer.Control.Status().SceneGeneration;
            TextBox external = NameBox();
            external.Focus();
            NameUiKey(external, Key.Enter);
            await NoChange("Enter after external refresh", "external UI smoke", generation);

            // Deterministic dispatcher ordering: an old deferred callback must
            // not overwrite a newer snapshot that arrived before it was run.
            InspectorSnapshot shown = NameUiField<InspectorSnapshot>(window, "_inspectorShown")!;
            TextBox queued = NameBox();
            queued.Focus();
            queued.Text = "queued draft";
            NameUiSnapshot(window, shown with { });
            Check("queued-race setup deferred a real snapshot", NameUiField<InspectorSnapshot>(window, "_inspectorDeferred") is not null);
            notes.Focus();
            NameUiSnapshot(window, shown with { Name = "newer queued snapshot" });
            await Task.Delay(50);
            Check("old deferred callback cannot overwrite newer actual field", NameBox().Text == "newer queued snapshot", NameBox().Text ?? "");
            NameUiSnapshot(window, shown);
        }
        finally
        {
            Outside(original);
            await NameUiWait(() => ReadName() == original && NameBox().Text == original, "original observed name restored");
            Check("original name restored without Save", ReadName() == original);
        }
    }

    private static async Task CheckAssetsUi(MainWindow window, List<NameUiCheck> checks)
    {
        void Check(string name, bool ok, string detail = "") => checks.Add(new NameUiCheck(name, ok, detail));
        RuntimeProcess runtime = NameUiField<RuntimeProcess>(window, "_runtime")!;
        using RemoteSession observer = RemoteSession.Connect(SessionFile.Read(runtime.SessionPath));
        var tabs = NameUiField<TabControl>(window, "_assetTabs")!;
        var browser = NameUiField<AssetBrowser>(window, "_assetBrowser")!;
        var selection = NameUiField<Selection>(window, "_selection")!;
        var undo = NameUiField<Button>(window, "_undo")!;
        var redo = NameUiField<Button>(window, "_redo")!;
        EntityGuid? selected = selection.Node;
        var history = (undo.Content, redo.Content, undo.IsEnabled, redo.IsEnabled);
        ulong generation = observer.Control.Status().SceneGeneration;
        tabs.SelectedIndex = 1;
        await NameUiWait(() => browser.State == "Ready" && browser.Items.Count > 0, "Assets tab loads through actual worker");
        Check("assets UI: full shared list", browser.Items.SequenceEqual(observer.Assets.ListAssets().Value));
        long ListRequests() => (long)typeof(AssetBrowser).GetField("_listRequest", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(browser)!;
        long listRequests = ListRequests();
        var controls = window.GetVisualDescendants().OfType<Control>().ToArray();
        Check("assets UI: fixed snapshot recovery note", controls.OfType<TextBlock>().Any(b => b.Text?.Contains("restart Runtime", StringComparison.Ordinal) == true && b.Text.Contains("does not reread disk", StringComparison.Ordinal)));
        TextBox search = controls.OfType<TextBox>().Single(b => b.PlaceholderText == "Source or GUID");
        ComboBox types = controls.OfType<ComboBox>().Single(b => b.Items.Cast<object>().Any(v => v?.ToString() == "All types"));
        ListBox list = NameUiField<ListBox>(window, "_assetsList")!;
        var material = browser.Items.First(v => v.Type == Atlantis.Gameplay.Assets.AssetType.Material && v.Source.StartsWith("assets:", StringComparison.Ordinal));
        types.SelectedIndex = 3;
        search.Text = "no-such-asset-0081";
        await NameUiWait(() => browser.Search == "no-such-asset-0081", "text filter dispatches");
        Check("assets UI: no matching state", browser.ViewState == "No matching assets");
        search.Text = material.Guid.ToString();
        await NameUiWait(() => browser.Search == material.Guid.ToString(), "GUID filter dispatches");
        Check("assets UI: type and GUID filters are local", browser.Visible.Count == 1 && browser.Visible[0] == material);
        search.Text = material.Source.ToUpperInvariant();
        await NameUiWait(() => browser.Search == material.Source.ToUpperInvariant(), "source filter dispatches");
        Check("assets UI: source filter", browser.Visible.Any(v => v.Guid == material.Guid));
        list.SelectedItem = browser.Visible.First(v => v.Guid == material.Guid);
        await NameUiWait(() => browser.Details?.Guid == material.Guid, "actual detail query");
        Check("assets UI: displayed metadata", NameUiField<StackPanel>(window, "_assetDetails")!.Children.OfType<TextBlock>().Any(b => b.Text?.Contains("artifact schema:", StringComparison.Ordinal) == true));
        var copy = controls.OfType<Button>().Single(b => b.Content?.ToString() == "Copy GUID");
        copy.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => browser.DetailState == "Ready", "copy after detail");
        string? copied = await window.Clipboard!.TryGetTextAsync();
        Check("assets UI: actual clipboard GUID", copied == material.Guid.ToString(), copied ?? "");
        Check("assets UI: filters selection and copy do not reload", ListRequests() == listRequests);
        var reload = controls.OfType<Button>().Single(b => b.Content?.ToString() == "Reload view");
        // No dispatcher yield between selection and Reload: the real worker's
        // old completion cannot be consumed before the view invalidates it.
        var oldDetails = browser.Details!;
        list.SelectedItem = null;
        list.SelectedItem = browser.Visible.First(v => v.Guid == material.Guid);
        long pendingDetail = (long)typeof(AssetBrowser).GetField("_detailRequest", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(browser)!;
        Check("assets UI: detail is pending before Reload", browser.Details is null && browser.DetailState == "Loading");
        reload.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => browser.State == "Ready" && browser.Details?.Guid == material.Guid && browser.DetailState == "Ready",
                         "pending detail is requested again after Reload");
        var reloadedDetails = browser.Details!;
        Check("assets UI: pending-detail Reload resolves from current Runtime snapshot",
              Json.Write(Codec.Encode(reloadedDetails)) == Json.Write(Codec.Encode(observer.Assets.GetAsset(material.Guid).Value)));
        bool? lateAccepted = null;
        Dispatcher.UIThread.Post(() =>
        {
            lateAccepted = browser.CompleteDetail(pendingDetail, material.Guid,
                Outcome<Atlantis.Gameplay.Assets.AssetDetails, Atlantis.Gameplay.Assets.AssetError>.Ok(oldDetails with { Tool = "late stale callback" }));
            if (lateAccepted == true)
                typeof(MainWindow).GetMethod("RenderAssets", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(window, null);
        });
        await NameUiWait(() => lateAccepted is not null, "old detail callback arrives after refreshed result");
        Check("assets UI: late old callback cannot overwrite refreshed model or panel", lateAccepted == false &&
              ReferenceEquals(browser.Details, reloadedDetails) && NameUiField<StackPanel>(window, "_assetDetails")!.Children.OfType<TextBlock>()
                  .Any(b => b.Text?.Contains("tool: " + reloadedDetails.Tool, StringComparison.Ordinal) == true && !b.Text.Contains("late stale callback", StringComparison.Ordinal)));
        for (int i = 0; i < 3; ++i)
        {
            reload.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => browser.State == "Ready" && browser.Details?.Guid == material.Guid && browser.DetailState == "Ready",
                             "actual Reload view and selected detail");
            Check("assets UI: Reload preserves selection " + i, browser.Selected == material.Guid);
        }
        Check("assets UI: browsing leaves Hierarchy history and generation intact", selection.Node == selected &&
            history == (undo.Content, redo.Content, undo.IsEnabled, redo.IsEnabled) && observer.Control.Status().SceneGeneration == generation);
        if (browser.Details!.Dependencies.Count > 0)
        {
            AssetGuid dependency = browser.Details.Dependencies[0];
            var button = NameUiField<StackPanel>(window, "_assetDetails")!.Children.OfType<Button>().First();
            button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            await NameUiWait(() => browser.Details?.Guid == dependency, "actual dependency navigation");
            Check("assets UI: dependency navigation", browser.Selected == dependency && selection.Node == selected);
        }
        tabs.SelectedIndex = 0;
        var document = new RemoteAuthoringDocument(observer.Authoring, observer);
        TypeId renderable = SceneSchema.TypeOf(document.Schema, "Renderable")!.Value;
        EntityGuid node = observer.Authoring.ListNodes().Value.First(n => observer.Authoring.ListComponents(n.Node).Value.Contains(renderable)).Node;
        var rows = NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!;
        NameUiField<ListBox>(window, "_entities")!.SelectedItem = rows.First(r => r.Node == node);
        await NameUiWait(() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.Subject == node, "actual Renderable Inspector");
        TextBox MaterialBox() => NameUiField<StackPanel>(window, "_inspectorPanel")!.Children.OfType<Grid>()
            .Single(g => g.Children.OfType<TextBlock>().Any(b => b.Text == "materialAsset")).Children.OfType<TextBox>().Single();
        SceneAddress address = SceneSchema.AddressOf(document.Schema, node, "Renderable", "materialAsset")!.Value;
        AssetGuid original = document.GetProperty(address)!.Value.AsAsset();
        var replacement = browser.Items.First(v => v.Type == Atlantis.Gameplay.Assets.AssetType.Material && v.Guid != original && v.Source.StartsWith("assets:", StringComparison.Ordinal));
        tabs.SelectedIndex = 1; types.SelectedIndex = 0; search.Text = replacement.Guid.ToString();
        await NameUiWait(() => browser.Search == replacement.Guid.ToString(), "replacement filter dispatches");
        list.SelectedItem = browser.Visible.Single();
        await NameUiWait(() => browser.Details?.Guid == replacement.Guid, "replacement selected");
        copy.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        copied = await window.Clipboard!.TryGetTextAsync();
        tabs.SelectedIndex = 0;
        TextBox field = MaterialBox(); field.Focus(); field.Text = copied;
        NameUiKey(field, Key.Enter); window.Focus();
        await NameUiWait(() => document.GetProperty(address)!.Value.AsAsset() == replacement.Guid && observer.Control.Status().SceneGeneration > generation, "pasted GUID commits and applies");
        Check("assets UI: clipboard pasted through existing Inspector", document.GetProperty(address)!.Value.AsAsset() == replacement.Guid);
        undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => document.GetProperty(address)!.Value.AsAsset() == original, "actual material Undo");
        Check("assets UI: actual Undo restores GUID", document.GetProperty(address)!.Value.AsAsset() == original);
        redo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => document.GetProperty(address)!.Value.AsAsset() == replacement.Guid, "actual material Redo");
        Check("assets UI: actual Redo restores GUID", document.GetProperty(address)!.Value.AsAsset() == replacement.Guid);
        undo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => document.GetProperty(address)!.Value.AsAsset() == original && redo.IsEnabled &&
            redo.Content?.ToString() == "Redo edit Renderable.materialAsset" &&
            NameUiField<TextBlock>(window, "_applyLine")!.Text!.StartsWith("apply: applied", StringComparison.Ordinal), "restore original material and settle history/apply");
    }

    private static void InspectorChecks()
    {
        var document = new FakeDocument();
        EntityGuid light = document.Add(1, null, LocalTransform.Identity, FakeDocument.Light);
        EntityGuid camera = document.Add(2, null, LocalTransform.Identity, FakeDocument.CameraType);
        var tree = new DocumentTree();
        tree.Rebuild(document);
        var selection = new Selection();
        var inspector = new InspectorModel(document);

        inspector.Refresh(light);
        Check(inspector.Fields.Select(f => f.Path).SequenceEqual(new[]
              {
                  "Transform.localPosition", "Transform.localEulerAnglesRadians", "Transform.localScale",
                  "Light.intensity", "Light.color", "Light.kind", "Light.range",
              }), "inspector: every field of every component, Transform first, in descriptor order");
        Check(inspector.Find("Light.intensity") is { Control: FieldControl.Float, Text: "1" } &&
              inspector.Find("Light.color") is { Control: FieldControl.Float3, Text: "1 1 1" } &&
              inspector.Find("Light.kind") is { Control: FieldControl.Enum, Text: "Directional" } &&
              inspector.Find("Transform.localScale") is { Control: FieldControl.Float3, Text: "1 1 1" },
              "inspector: each field's editor kind and the value as it shows it (enums by name)");
        Check(inspector.Find("Light.range") is { Optional: true, Present: false, Text: "" }, "inspector: an absent optional field shows empty");

        inspector.Refresh(camera);
        Check(inspector.Find("Camera.fog.density") is { Label: "fog.density", Text: "0.5" } && inspector.Find("Camera.fog") is null,
              "inspector: a nested struct is expanded in place, its leaf labelled by its path");
        Check(inspector.Find("Camera.exposureCompensationEv") is { Editable: false }, "inspector: a read-only field is not editable");

        // Commits: one transaction (one SetProperty) each.
        inspector.Refresh(light);
        EditPlan set = inspector.Commit("Light.intensity", " 2.5 ");
        Check(set.Refusal is null && set.Commands.Count == 1 && set.Commands[0] is SetProperty { Value: var v } && v.Equals(PropertyValue.FromFloat32(2.5f)),
              "inspector: a commit is one SetProperty of the parsed value");
        Check(Submit(document, tree, selection, set) is null, "inspector: the document accepts it");
        inspector.Refresh(light);
        Check(inspector.Find("Light.intensity")!.Text == "2.5", "inspector: the refreshed field shows the committed value");
        Check(inspector.Commit("Light.intensity", "2.5").Refusal == "unchanged", "inspector: a value equal to the current one is not a commit");
        Check(inspector.Commit("Light.intensity", "abc").Refusal == "not a number" && inspector.Commit("Light.intensity", "").Refusal == "not a number" &&
              inspector.Commit("Light.intensity", "NaN").Refusal == "not a number", "inspector: text that is not a number is refused with its reason");
        Check(inspector.Commit("Light.color", "1 2").Refusal == "expected three numbers", "inspector: a vector with the wrong count is refused");
        Check(Submit(document, tree, selection, inspector.Commit("Light.color", "0.5, 0.25 1")) is null &&
              document.GetProperty(new SceneAddress(light, FakeDocument.Light, FakeDocument.Color)) is { } color && color.Equals(PropertyValue.FromVec3(new Vector3(0.5f, 0.25f, 1))),
              "inspector: a vector commits (spaces or commas)");
        Check(Submit(document, tree, selection, inspector.Commit("Light.kind", "Point")) is null &&
              document.GetProperty(new SceneAddress(light, FakeDocument.Light, FakeDocument.Kind)) is { } kind && kind.Equals(PropertyValue.FromEnum(1)),
              "inspector: an enum commits by name");
        inspector.Refresh(light);
        Check(inspector.Find("Light.kind")!.Text == "Point" && inspector.Commit("Light.kind", "1").Refusal == "unchanged" &&
              inspector.Commit("Light.kind", "Sun").Refusal == "one of Directional, Point", "inspector: an enum shows its name, accepts its number, and lists its choices on a bad name");
        inspector.Refresh(camera);
        Check(inspector.Commit("Camera.exposureCompensationEv", "1").Refusal == "the field is read-only", "inspector: a read-only field cannot be committed");
        Check(inspector.Commit("Light.missing", "1").Refusal == "no such field", "inspector: an unknown field is refused");

        // An optional field: set, then cleared; empty text clears it.
        inspector.Refresh(light);
        Check(Submit(document, tree, selection, inspector.Commit("Light.range", "12")) is null, "inspector: an optional field is set");
        inspector.Refresh(light);
        Check(inspector.Find("Light.range") is { Present: true, Text: "12" } && inspector.Clear("Light.range").Commands.Count == 1,
              "inspector: it shows present, and offers its clear");
        Check(Submit(document, tree, selection, inspector.Commit("Light.range", "")) is null, "inspector: empty text clears an optional field");
        inspector.Refresh(light);
        Check(inspector.Find("Light.range") is { Present: false } && inspector.Clear("Light.range").Refusal == "unchanged" &&
              inspector.Clear("Light.intensity").Refusal == "the field is not optional", "inspector: cleared, and a clear of a non-optional field is refused");

        // An outside change (another client) is seen by a refresh: the field follows the document.
        document.SubmitTransaction(new Command[] { new SetProperty(new SceneAddress(light, FakeDocument.Light, FakeDocument.Intensity), PropertyValue.FromFloat32(9.0f)) });
        document.DrainEvents();
        inspector.Refresh(light);
        Check(inspector.Find("Light.intensity")!.Text == "9", "inspector: a value changed elsewhere shows after a refresh (the field reverts to the document)");

        // Components: addable ones exclude what the node has and nested structs; add and remove are commands.
        Check(inspector.Addable().Select(t => t.ShortName).SequenceEqual(new[] { "Camera" }), "inspector: the addable components are the schema's top-level ones the node lacks");
        EditPlan add = inspector.AddComponent(FakeDocument.CameraType);
        Check(add.Commands.Single() is AddComponent && Submit(document, tree, selection, add) is null, "inspector: add component is one command");
        inspector.Refresh(light);
        Check(inspector.Find("Camera.fog.density") is not null && inspector.Addable().Count == 0 && inspector.AddComponent(FakeDocument.CameraType).Refusal == "the node has it",
              "inspector: the new component's fields appear");
        Check(inspector.RemoveComponent(FakeDocument.Transform).Refusal == "the Transform is required", "inspector: the Transform cannot be removed");
        Check(Submit(document, tree, selection, inspector.RemoveComponent(FakeDocument.Light)) is null, "inspector: remove component is one command");
        inspector.Refresh(light);
        Check(inspector.Find("Light.intensity") is null && inspector.RemoveComponent(FakeDocument.Light).Refusal == "the node does not have it",
              "inspector: the removed component's fields are gone");
        inspector.Refresh(null);
        Check(inspector.Fields.Count == 0 && inspector.Commit("Light.intensity", "1").Refusal == "no such field", "inspector: nothing selected shows nothing");
    }
}
