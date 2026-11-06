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
    public static int RunNameUiSmoke(string runtimePath, string outputFile)
    {
        var checks = new List<NameUiCheck>();
        int exit = 1;
        string? work = null;
        var thread = new Thread(() =>
        {
            try
            {
                exit = AppBuilder.Configure(() => new NameUiApplication(runtimePath, checks, path => work = path))
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
            kind = "product-name-and-asset-ui-smoke",
            boundary = "Actual Windows MainWindow/worker/native Runtime; Avalonia focus and routed key/button events, plus controlled stale-snapshot/detail callbacks on the UI dispatcher. No OS scancode injection, UIAutomation, screenshots, timing or performance verdict.",
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
        private readonly List<NameUiCheck> _checks;
        private readonly Action<string> _work;

        public NameUiApplication(string runtime, List<NameUiCheck> checks, Action<string> work)
        {
            _runtime = runtime;
            _checks = checks;
            _work = work;
        }

        public override void Initialize() => Styles.Add(new FluentTheme());

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                var window = new MainWindow(new EditorOptions(_runtime, "integrated_showcase_demo"));
                desktop.MainWindow = window;
                window.Opened += async (_, _) =>
                {
                    try { await CheckNameUi(window, _checks, _work); await CheckAssetsUi(window, _checks); }
                    catch (Exception e) { _checks.Add(new NameUiCheck("actual Name UI checks completed", false, e.ToString())); }
                    finally { desktop.Shutdown(_checks.Count > 0 && _checks.All(c => c.Ok) ? 0 : 1); }
                };
            }
            base.OnFrameworkInitializationCompleted();
        }
    }

    private static T? NameUiField<T>(MainWindow window, string field) where T : class =>
        typeof(MainWindow).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window) as T;

    private static void NameUiSnapshot(MainWindow window, InspectorSnapshot snapshot) =>
        typeof(MainWindow).GetMethod("OnInspectorChanged", BindingFlags.Instance | BindingFlags.NonPublic)!
                          .Invoke(window, new object[] { snapshot });

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
        await NameUiWait(() => document.GetProperty(address)!.Value.AsAsset() == original, "restore original material");
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
