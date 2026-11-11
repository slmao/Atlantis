using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static async Task CheckMeshMaterialPreviewUi(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        void Check(string name, bool ok, string detail = "") => checks.Add(new(name, ok, detail));
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        static T PickerControl<T>(AssetReferencePickerWindow picker, string name) where T : class =>
            (T)typeof(AssetReferencePickerWindow).GetProperty(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(picker)!;
        static bool SamePose(CameraPose? a, CameraPose? b) => a is null ? b is null :
            b is not null && a.Lens == b.Lens && a.WorldMatrix.SequenceEqual(b.WorldMatrix);
        static bool Shown(Image image, TextBlock state, AssetGuid mesh, AssetGuid material) =>
            image.IsVisible && image.Source is Bitmap bitmap && bitmap.PixelSize.Width == 256 && bitmap.PixelSize.Height == 256 &&
            state.Text?.Contains("fixed lighting", StringComparison.Ordinal) == true &&
            state.Text.Contains(mesh.ToString(), StringComparison.Ordinal) && state.Text.Contains(material.ToString(), StringComparison.Ordinal);

        await NameUiWait(() => NameUiField<RuntimeProcess>(window, "_runtime") is { } runtime &&
            File.Exists(runtime.SessionPath) && Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows")?.Count > 0,
            "actual Editor and hosted Runtime ready", 120);
        var runtime = Field<RuntimeProcess>("_runtime");
        work(runtime.Launch.WorkDirectory);
        Check("actual Windows Editor has an embedded Runtime", window.IsVisible && runtime.Launch.ParentWindow is not null);
        using var observer = await Task.Run(() => RemoteSession.Connect(SessionFile.Read(runtime.SessionPath)));
        var document = await Task.Run(() => new RemoteAuthoringDocument(observer.Authoring, observer));
        var world = new GameplayWorld(observer);
        var scene = await Task.Run(() => SceneReader.Read(world));
        var subject = Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").First(row => row.Node == scene.FirstRenderable);
        Field<ListBox>("_entities").SelectedItem = subject;
        await NameUiWait(() => Field<InspectorSnapshot>("_inspectorShown")?.Subject == subject.Node &&
            Field<TextBox>("_nameBox") is not null, "actual Inspector renderable is editable");
        var materialAddress = SceneSchema.AddressOf(document.Schema, subject.Node, "Renderable", "materialAsset")!.Value;
        var meshAddress = SceneSchema.AddressOf(document.Schema, subject.Node, "Renderable", "meshAsset")!.Value;
        Task<PropertyValue> Read(SceneAddress address) => Task.Run(() => document.GetProperty(address)!.Value);
        var originalMaterial = await Read(materialAddress);
        var openingMesh = (await Read(meshAddress)).AsAsset();
        var originalDocument = (await Task.Run(observer.Authoring.Document))!;
        var originalSource = File.ReadAllBytes(originalDocument.SourcePath);
        AssetGuid offSceneMesh = AssetGuid.Parse("58e57a64-ad60-4925-b6d6-6561540e8476");
        AssetGuid baseMaterial = AssetGuid.Parse("057d2d26-8bb6-4a8a-a05a-39ec65a84857"); // showcase_wood_reddish_stain
        AssetGuid unsupportedMaterial = AssetGuid.Parse("3419806a-3f33-4688-bbca-3f2960290df4");
        if (originalMaterial.TryGetAsset(out AssetGuid existing) && baseMaterial == existing)
            throw new InvalidOperationException("fixture already uses the replacement material");

        Task<string> ReadName() => Task.Run(() => observer.Authoring.ListNodes().Value.Single(n => n.Node == subject.Node).Name);
        Task<ApplySnapshot> Apply() => Task.Run(() => observer.Authoring.ApplyStatus().Value!);
        Task<RuntimeStatus> Status() => Task.Run(observer.Control.Status);
        var undo = Field<Button>("_undo");
        var redo = Field<Button>("_redo");
        var step = Field<Button>("_step");
        var play = Field<Button>("_play");
        (bool Undo, string? UndoLabel, bool Redo, string? RedoLabel) History() =>
            (undo.IsEnabled, undo.Content as string, redo.IsEnabled, redo.Content as string);
        await NameUiWait(() => Field<Button>("_pause").IsEnabled, "pause available");
        Click(Field<Button>("_pause"));
        await NameUiWait(async () => step.IsEnabled && (await Status()).Paused, "paused Runtime");
        const string editedName = "Pair preview unsaved edit";
        var name = Field<TextBox>("_nameBox");
        name.Focus();
        name.Text = editedName;
        Check("real Inspector accepts the unsaved edit", NameUiKey(name, Key.Enter));
        await NameUiWait(async () => undo.IsEnabled && await ReadName() == editedName &&
            Field<TextBlock>("_applyLine").Text?.Contains("pending: paused", StringComparison.Ordinal) == true &&
            (await Apply()).Request?.State == ApplyState.Pending, "unsaved tracked edit remains pending while paused");

        var before = await Status();
        var pending = await Apply();
        var beforeWorld = await Task.Run(() => SceneReader.Read(world));
        var beforeCamera = Field<EditorCamera>("_camera").Pose();
        var beforeSelection = Field<Selection>("_selection").Node;
        var beforeHistory = History();
        var beforeApplyUi = Field<TextBlock>("_applyLine").Text;
        Check("both Assets preview inputs are outside the scene dependency closure", await Task.Run(() =>
        {
            var visited = new HashSet<AssetGuid>();
            var queue = new Queue<AssetGuid>();
            queue.Enqueue(before.Scene);
            while (queue.TryDequeue(out var guid))
            {
                if (!visited.Add(guid)) continue;
                if (guid == offSceneMesh || guid == baseMaterial) return false;
                foreach (var dependency in observer.Assets.GetAsset(guid).Value.Dependencies) queue.Enqueue(dependency);
            }
            return true;
        }));

        var tabs = Field<TabControl>("_assetTabs");
        var browser = Field<AssetBrowser>("_assetBrowser");
        var list = Field<ListBox>("_assetsList");
        var materialList = Field<ComboBox>("_previewMaterial");
        var pairButton = Field<Button>("_previewPair");
        var assetsImage = Field<Image>("_previewImage");
        var assetsState = Field<TextBlock>("_previewState");
        tabs.SelectedIndex = 1;
        await NameUiWait(() => browser.State == "Ready", "Assets catalog ready");
        list.SelectedItem = browser.Visible.Single(a => a.Guid == offSceneMesh);
        materialList.SelectedItem = materialList.Items.OfType<AssetSummary>().Single(a => a.Guid == baseMaterial);
        await NameUiWait(() => pairButton.IsEnabled, "explicit pair button enabled");
        Check("Assets selection alone starts no preview", assetsImage.Source is null && assetsState.Text == "");
        Click(pairButton);
        await NameUiWait(() => assetsImage.Source is Bitmap, "real Assets pair renders", 120);
        Check("Assets shows both requested GUIDs and a 256×256 PNG", Shown(assetsImage, assetsState, offSceneMesh, baseMaterial));

        Grid MaterialRow() => Field<StackPanel>("_inspectorPanel").Children.OfType<Grid>()
            .Single(row => row.Children.OfType<TextBlock>().Any(text => text.Text == "materialAsset"));
        Button Launcher() => MaterialRow().Children.OfType<Button>().Single(b => b.Content?.ToString() == "Choose…");
        bool Active() => typeof(AssetPreviewRequests).GetField("_active", BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(Field<AssetPreviewRequests>("_previewRequests")) is not null;
        AssetReferencePickerWindow? Picker() => NameUiField<AssetReferencePickerWindow>(window, "_referenceWindow");
        async Task<AssetReferencePickerWindow> Open()
        {
            tabs.SelectedIndex = 0;
            await NameUiWait(() => Launcher().IsEnabled && window.IsEffectivelyEnabled, "material chooser launcher is enabled");
            var box = MaterialRow().Children.OfType<TextBox>().Single();
            await NameUiWait(() => box.Focus() && box.IsFocused, "Inspector field accepts keyboard focus");
            if (!NameUiKey(box, Key.F4)) throw new InvalidOperationException("material chooser F4 was not routed");
            await NameUiWait(() => Picker() is { } picker && PickerField<AssetBrowser>(picker, "_browser").State == "Ready",
                "real material chooser opened");
            return Picker()!;
        }
        async Task Select(AssetReferencePickerWindow picker, AssetGuid guid)
        {
            var options = PickerField<AssetBrowser>(picker, "_browser");
            PickerField<ListBox>(picker, "_list").SelectedItem = options.Visible.Single(a => a.Guid == guid);
            await NameUiWait(() => options.Details?.Guid == guid &&
                PickerControl<Button>(picker, "PreviewButton").IsEnabled, "visible material choice ready");
        }
        async Task Close(AssetReferencePickerWindow picker)
        {
            picker.Close();
            await NameUiWait(() => Picker() is null && Launcher().IsEnabled, "chooser cancel settled");
        }

        // Only the preview socket goes to this peer. The real authoring worker stays attached.
        var sessionField = typeof(MainWindow).GetField("_previewSession", BindingFlags.Instance | BindingFlags.NonPublic)!;
        var live = Field<SessionInfo>("_previewSession");
        async Task HoldPair(Button button, AssetGuid mesh, AssetGuid material, Func<Task> whileWaiting, string? refusal = "Busy")
        {
            using var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(30));
            var received = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var peer = ServeHeldPair(listener, live.Scene, mesh, material, refusal, received, release.Task, deadline.Token);
            sessionField.SetValue(window, live with { Port = (ushort)((IPEndPoint)listener.LocalEndpoint).Port });
            try
            {
                Click(button);
                if (await Task.WhenAny(received.Task, peer).WaitAsync(deadline.Token) == peer)
                {
                    await peer;
                    throw new InvalidOperationException("Preview connection closed before sending the pair");
                }
                await whileWaiting();
                release.TrySetResult();
                await peer;
                await NameUiWait(() => !Active(), "admitted pair settled");
            }
            finally
            {
                release.TrySetResult();
                sessionField.SetValue(window, live);
            }
        }

        AssetReferencePickerWindow? activeChooser = null;
        await HoldPair(pairButton, offSceneMesh, baseMaterial, async () =>
        {
            activeChooser = await Open();
            // Exercise modal priority even if the owner tab is changed by code.
            tabs.SelectedIndex = 1;
        }, refusal: null);
        var chooser = activeChooser!;
        var chooserImage = PickerControl<Image>(chooser, "PreviewImage");
        var chooserState = PickerControl<TextBlock>(chooser, "PreviewState");
        var chooserPreview = PickerControl<Button>(chooser, "PreviewButton");
        Check("modal drops an admitted Assets success even while the Assets tab remains selected",
            tabs.SelectedIndex == 1 && assetsImage.Source is null && assetsState.Text == "" &&
            chooserImage.Source is null && chooserState.Text == "" && !pairButton.IsEnabled);
        tabs.SelectedIndex = 0;
        Check("chooser labels the exact same-node mesh read at opening",
            chooser.Target.OpeningMesh == openingMesh && PickerField<TextBlock>(chooser, "_openingMesh").Text?.Contains(
                "Mesh at opening:", StringComparison.Ordinal) == true && PickerField<TextBlock>(chooser, "_openingMesh").Text!
                    .Contains(openingMesh.ToString(), StringComparison.Ordinal));
        await Select(chooser, baseMaterial);
        Click(chooserPreview);
        await NameUiWait(() => chooserImage.Source is Bitmap, "real material chooser pair renders", 120);
        Check("chooser preview uses the opening mesh and selected material", Shown(chooserImage, chooserState, openingMesh, baseMaterial));
        await Select(chooser, unsupportedMaterial);
        Check("new chooser candidate clears the old bitmap without auto-preview", chooserImage.Source is null && chooserState.Text == "");
        Click(chooserPreview);
        await NameUiWait(() => chooserState.Text?.Contains("UnsupportedMaterial", StringComparison.Ordinal) == true,
            "Runtime refuses the transparent material preview", 120);
        Check("unsupported preview leaves the existing Confirm available",
            chooserImage.Source is null && PickerField<Button>(chooser, "_confirm").IsEnabled);
        await Close(chooser);
        Check("Cancel and both preview surfaces leave document, pending apply, source and history unchanged",
            (await Read(materialAddress)).Equals(originalMaterial) && await Apply() == pending &&
            await Task.Run(observer.Authoring.Document) == originalDocument && await ReadName() == editedName &&
            File.ReadAllBytes(originalDocument.SourcePath).SequenceEqual(originalSource) && History() == beforeHistory &&
            Field<TextBlock>("_applyLine").Text == beforeApplyUi);
        var after = await Status();
        var afterWorld = await Task.Run(() => SceneReader.Read(world));
        Check("pair preview leaves pause, World, generation and viewport camera/selection unchanged",
            before.Paused && after.Paused && step.IsEnabled && before.Scene == after.Scene &&
            before.SceneGeneration == after.SceneGeneration && beforeWorld.Entities.SequenceEqual(afterWorld.Entities) &&
            SamePose(beforeWorld.ScenePose, afterWorld.ScenePose) && beforeWorld.FirstRenderablePosition == afterWorld.FirstRenderablePosition &&
            SamePose(beforeCamera, Field<EditorCamera>("_camera").Pose()) && beforeSelection == Field<Selection>("_selection").Node);

        chooser = await Open();
        await Select(chooser, baseMaterial);
        var oldChooser = chooser;
        await HoldPair(PickerControl<Button>(oldChooser, "PreviewButton"), openingMesh, baseMaterial, async () =>
        {
            await Close(oldChooser);
            chooser = await Open();
            await Select(chooser, baseMaterial);
        });
        Check("same-pair refusal after chooser reopen leaves the new chooser clean and ready",
            !ReferenceEquals(oldChooser, chooser) && PickerControl<Image>(chooser, "PreviewImage").Source is null &&
            PickerControl<TextBlock>(chooser, "PreviewState").Text == "" && PickerControl<Button>(chooser, "PreviewButton").IsEnabled);
        var chosen = chooser;
        var launchControl = Field<Button>("_referenceFocus");
        var restoredLauncherFocus = false;
        void ObserveFocus(object? sender, FocusChangedEventArgs args)
        {
            if (ReferenceEquals(args.Source, launchControl) || args.Source is Button { Tag: "asset chooser" })
                restoredLauncherFocus = true;
        }
        window.GotFocus += ObserveFocus;
        await HoldPair(PickerControl<Button>(chosen, "PreviewButton"), openingMesh, baseMaterial, async () =>
        {
            Check("Confirm stays enabled during an admitted pair request", PickerField<Button>(chosen, "_confirm").IsEnabled);
            Click(PickerField<Button>(chosen, "_confirm"));
            await NameUiWait(async () => Picker() is null && (await Read(materialAddress)).AsAsset() == baseMaterial,
                "actual Confirm updates the document before the preview settles");
        });
        await NameUiWait(() => Launcher().IsEnabled &&
            MaterialRow().Children.OfType<TextBox>().Single().Text == baseMaterial.ToString(),
            "Confirm worker refreshes the editable field");
        window.GotFocus -= ObserveFocus;
        var focused = window.FocusManager?.GetFocusedElement();
        Check("Confirm restores launcher focus and late preview stays hidden",
            ReferenceEquals(focused, Launcher()) && PickerControl<Image>(chosen, "PreviewImage").Source is null &&
            assetsImage.Source is null,
            $"launcher focus observed during refresh: {restoredLauncherFocus}; focused after refresh: {focused?.GetType().Name ?? "none"}");
        async Task<bool> Applied()
        {
            var snapshot = await Apply();
            return snapshot.Request is { State: ApplyState.Applied } applied &&
                applied.Receipt.DocumentRevision == snapshot.CurrentDocumentRevision;
        }
        Click(play);
        await NameUiWait(async () => !(await Status()).Paused && await Applied(),
            "resume applies the confirmed edit", 60);
        ulong generation = (await Status()).SceneGeneration;
        Click(undo);
        await NameUiWait(async () => redo.IsEnabled && (await Read(materialAddress)).Equals(originalMaterial) &&
            (await Status()).SceneGeneration > generation && await Applied(), "Undo applies", 60);
        generation = (await Status()).SceneGeneration;
        Click(redo);
        await NameUiWait(async () => undo.IsEnabled && (await Read(materialAddress)).AsAsset() == baseMaterial &&
            (await Status()).SceneGeneration > generation && await Applied(), "Redo applies", 60);
        Check("existing Confirm, apply, Undo and Redo work without writing the scene source",
            File.ReadAllBytes(originalDocument.SourcePath).SequenceEqual(originalSource));

        chooser = await Open();
        await Select(chooser, baseMaterial);
        async Task SetMesh(AssetGuid mesh) => await Task.Run(() =>
        {
            observer.Authoring.SubmitTransaction(new Atlantis.Gameplay.Authoring.Command[]
                { new SetProperty(meshAddress, PropertyValue.FromAsset(mesh)) });
            if (observer.Authoring.DrainFailures().Count != 0) throw new InvalidOperationException("fixture sibling mesh change was refused");
        });
        try
        {
            await SetMesh(offSceneMesh);
            await NameUiWait(async () => (await Read(meshAddress)).AsAsset() == offSceneMesh, "external sibling mesh change visible");
            await HoldPair(PickerControl<Button>(chooser, "PreviewButton"), openingMesh, baseMaterial, () => Task.CompletedTask);
            Check("chooser still sends and labels its opening mesh after an external sibling change",
                chooser.Target.OpeningMesh == openingMesh && PickerField<TextBlock>(chooser, "_openingMesh").Text!
                    .Contains(openingMesh.ToString(), StringComparison.Ordinal));
        }
        finally
        {
            await Close(chooser);
            await SetMesh(openingMesh);
        }
    }

    private static async Task ServeHeldPair(TcpListener listener, AssetGuid scene, AssetGuid mesh, AssetGuid material, string? refusal,
        TaskCompletionSource received, Task release, CancellationToken token)
    {
        using var socket = await listener.AcceptTcpClientAsync(token);
        using var stream = socket.GetStream();
        using var reader = new StreamReader(stream);
        using var writer = new StreamWriter(stream) { AutoFlush = true, NewLine = "\n" };
        while (await reader.ReadLineAsync(token) is string line)
        {
            JsonValue request = Json.Parse(line);
            JsonValue result;
            switch (request.Find("method")?.AsString)
            {
                case "hello": result = JsonValue.NewObject().Set("scene", Codec.Encode(scene)); break;
                case "connection.schema": result = JsonValue.NewArray(); break;
                case "assets.previewMeshMaterial":
                    JsonValue parameters = request.Find("params")!;
                    if (parameters.Find("meshGuid")?.AsString != mesh.ToString() ||
                        parameters.Find("materialGuid")?.AsString != material.ToString())
                        throw new InvalidOperationException("preview did not send the expected mesh/material pair");
                    received.SetResult();
                    await release.WaitAsync(token);
                    if (refusal is not null) result = JsonValue.NewObject().Set("err", JsonValue.String(refusal));
                    else
                    {
                        const string png = "iVBORw0KGgoAAAANSUhEUgAAAQAAAAEAAQAAAAB0CZXLAAAAH0lEQVR4nO3BAQ0AAADCoPdPbQ43oAAAAAAAAAAAvg0hAAAB8Wch7gAAAABJRU5ErkJggg==";
                        JsonValue image = JsonValue.NewObject().Set("meshGuid", Codec.Encode(mesh)).Set("materialGuid", Codec.Encode(material))
                            .Set("width", JsonValue.Number(256L)).Set("height", JsonValue.Number(256L))
                            .Set("mimeType", JsonValue.String("image/png")).Set("pngBase64", JsonValue.String(png));
                        result = JsonValue.NewObject().Set("ok", image);
                    }
                    break;
                default: throw new InvalidOperationException("Unexpected preview peer request " + line);
            }
            var response = JsonValue.NewObject().Set("id", request.Find("id")!).Set("result", result);
            await writer.WriteLineAsync(Json.Write(response).AsMemory(), token);
        }
    }
}
