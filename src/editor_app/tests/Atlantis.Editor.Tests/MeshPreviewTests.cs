using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text.RegularExpressions;
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

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static async Task CheckMeshPreviewUi(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        void Check(string name, bool ok, string detail = "") => checks.Add(new(name, ok, detail));
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        static bool SamePose(CameraPose? first, CameraPose? second) => first is null ? second is null :
            second is not null && first.Lens == second.Lens && first.WorldMatrix.SequenceEqual(second.WorldMatrix);

        await NameUiWait(() => NameUiField<RuntimeProcess>(window, "_runtime") is { } process &&
            File.Exists(process.SessionPath) && NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")?.Count > 0,
            "actual Editor and hosted Runtime ready", 120);
        var runtime = Field<RuntimeProcess>("_runtime");
        work(runtime.Launch.WorkDirectory);
        Check("actual Windows Editor has an embedded Runtime", window.IsVisible && runtime.Launch.ParentWindow is not null);
        using var observer = await Task.Run(() => RemoteSession.Connect(SessionFile.Read(runtime.SessionPath)));
        var world = new GameplayWorld(observer);
        var scene = await Task.Run(() => SceneReader.Read(world));
        var subject = Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").First(row => row.Node == scene.FirstRenderable);
        Field<ListBox>("_entities").SelectedItem = subject;
        await NameUiWait(() => NameUiField<InspectorSnapshot>(window, "_inspectorShown")?.Subject == subject.Node &&
            NameUiField<TextBox>(window, "_nameBox") is not null, "actual Inspector name is editable");

        async Task<string> ReadName() => (await Task.Run(observer.Authoring.ListNodes)).Value.Single(n => n.Node == subject.Node).Name;
        async Task<ApplySnapshot> Apply() => (await Task.Run(() => observer.Authoring.ApplyStatus())).Value!;
        async Task<RuntimeStatus> Status() => await Task.Run(observer.Control.Status);
        var originalName = await ReadName();
        var originalDocument = (await Task.Run(observer.Authoring.Document))!;
        var originalSource = File.ReadAllBytes(originalDocument.SourcePath);
        var pause = Field<Button>("_pause");
        var play = Field<Button>("_play");
        var step = Field<Button>("_step");
        var undo = Field<Button>("_undo");
        var redo = Field<Button>("_redo");
        (bool Undo, string? UndoLabel, bool Redo, string? RedoLabel) History() =>
            (undo.IsEnabled, undo.Content as string, redo.IsEnabled, redo.Content as string);
        await NameUiWait(() => pause.IsEnabled, "pause is available");
        Click(pause);
        await NameUiWait(async () => step.IsEnabled && (await Status()).Paused, "paused controls and Runtime");
        var nameBox = Field<TextBox>("_nameBox");
        Check("unsaved edit uses the real focused Inspector field", nameBox.Focus() && nameBox.IsFocused);
        const string editedName = "Mesh preview unsaved edit";
        nameBox.Text = editedName;
        Check("Inspector commits the routed name edit", NameUiKey(nameBox, Key.Enter));
        await NameUiWait(async () => undo.IsEnabled && await ReadName() == editedName &&
            Field<TextBlock>("_applyLine").Text?.Contains("pending: paused", StringComparison.Ordinal) == true &&
            (await Apply()).Request?.State == ApplyState.Pending, "name edit held in a tracked apply with undo history");

        var before = await Status();
        var pending = await Apply();
        var beforeHistory = History();
        var beforeWorld = await Task.Run(() => SceneReader.Read(world));
        var beforeCamera = Field<EditorCamera>("_camera").Pose();
        var beforeSelection = Field<Selection>("_selection").Node;
        var beforeApplyUi = Field<TextBlock>("_applyLine").Text;
        var previewGuid = AssetGuid.Parse("58e57a64-ad60-4925-b6d6-6561540e8476");
        Check("preview mesh is absent from the scene dependency closure", await Task.Run(() =>
        {
            var visited = new HashSet<AssetGuid>();
            var pendingAssets = new Queue<AssetGuid>();
            pendingAssets.Enqueue(before.Scene);
            while (pendingAssets.TryDequeue(out var guid))
            {
                if (!visited.Add(guid)) continue;
                if (guid == previewGuid) return false;
                foreach (var dependency in observer.Assets.GetAsset(guid).Value.Dependencies) pendingAssets.Enqueue(dependency);
            }
            return true;
        }), previewGuid.ToString());

        var tabs = Field<TabControl>("_assetTabs");
        var browser = Field<AssetBrowser>("_assetBrowser");
        var list = Field<ListBox>("_assetsList");
        var preview = Field<Button>("_previewMesh");
        var state = Field<TextBlock>("_previewState");
        var image = Field<Image>("_previewImage");
        tabs.SelectedIndex = 1;
        await NameUiWait(() => browser.State == "Ready", "Assets catalog loaded");
        list.SelectedItem = browser.Visible.Single(row => row.Guid == previewGuid && row.Type == AssetType.Mesh);
        await NameUiWait(() => preview.IsEnabled, "explicit geometry preview available");
        Check("mesh selection does not start preview work", image.Source is null && !image.IsVisible && state.Text == "");

        async Task RunPreview(string check)
        {
            Click(preview);
            Check(check + ": pending disables duplicate activation", !preview.IsEnabled && state.Text?.StartsWith("Loading", StringComparison.Ordinal) == true);
            await NameUiWait(() => state.Text?.StartsWith("Loading", StringComparison.Ordinal) == false, check + " settles", 120);
        }
        bool GeometryShown() => image.IsVisible && image.Source is Bitmap bitmap && bitmap.PixelSize.Width == 256 &&
            bitmap.PixelSize.Height == 256 && state.Text?.Contains("unlit vertex colors", StringComparison.Ordinal) == true;

        await RunPreview("real off-scene mesh preview");
        Check("Assets displays the 256×256 geometry image while paused", GeometryShown(), state.Text ?? "");
        tabs.SelectedIndex = 0;
        Check("leaving Assets clears the image and current preview state", image.Source is null && !image.IsVisible &&
            state.Text == "" && !preview.IsEnabled);
        tabs.SelectedIndex = 1;
        await NameUiWait(() => preview.IsEnabled, "explicit preview is available again");
        Check("returning to Assets keeps the selection without reissuing preview", browser.Selected == previewGuid && image.Source is null && state.Text == "");

        string build = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(runtime.Launch.RuntimePath)!, "..", "..", ".."));
        if (Environment.GetEnvironmentVariable("ATLANTIS_0091_UI_DISPOSABLE_BUILD") is not { } disposable ||
            !Path.TrimEndingDirectorySeparator(Path.GetFullPath(disposable)).Equals(build, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Set ATLANTIS_0091_UI_DISPOSABLE_BUILD to the disposable Runtime build root before testing failure.");
        string artifact = Path.Combine(build, "assets", "meshes", "textured_quad_left.amesh");
        string hidden = artifact + ".preview-ui-hidden";
        File.Move(artifact, hidden);
        try
        {
            await RunPreview("missing cooked mesh");
            Check("recoverable missing artifact displays a diagnostic and allows explicit retry", image.Source is null &&
                state.Text?.Contains("InvalidMesh", StringComparison.Ordinal) == true && preview.IsEnabled &&
                (string?)preview.Content == "Retry preview", state.Text ?? "");
        }
        finally { File.Move(hidden, artifact); }
        await RunPreview("explicit retry after restoring the artifact");
        Check("same selected mesh displays on retry", browser.Selected == previewGuid && GeometryShown(), state.Text ?? "");

        // The real editor worker and Runtime stay connected; only the preview's
        // separate SDK session talks to this controlled peer.
        var previewSessionField = typeof(MainWindow).GetField("_previewSession", BindingFlags.Instance | BindingFlags.NonPublic)!;
        var liveSession = Field<SessionInfo>("_previewSession");
        async Task<int> WithPreviewPeer(Func<Task> exercise, TaskCompletionSource? schemaSeen = null, Task? releaseSchema = null)
        {
            using var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            var peer = ServePreviewUiPeer(listener, liveSession.Scene, previewGuid, timeout.Token, schemaSeen, releaseSchema);
            previewSessionField.SetValue(window, liveSession with { Port = (ushort)((IPEndPoint)listener.LocalEndpoint).Port });
            try { await exercise(); return await peer; }
            finally
            {
                previewSessionField.SetValue(window, liveSession);
                timeout.Cancel();
            }
        }
        int invalidImages = await WithPreviewPeer(async () =>
        {
            await RunPreview("wire-valid header with corrupt PNG image data");
            Check("Avalonia bitmap decode failure clears the previous image and offers retry", image.Source is null && !image.IsVisible &&
                state.Text?.StartsWith("Could not decode", StringComparison.Ordinal) == true && preview.IsEnabled &&
                (string?)preview.Content == "Retry preview", state.Text ?? "");
        });
        Check("the decoder case used one ordinary SDK preview call", invalidImages == 1);
        var schemaSeen = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseSchema = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        int lateSends = await WithPreviewPeer(async () =>
        {
            Click(preview);
            try
            {
                await schemaSeen.Task.WaitAsync(TimeSpan.FromSeconds(15));
                tabs.SelectedIndex = 0;
            }
            finally { releaseSchema.TrySetResult(); }
        }, schemaSeen, releaseSchema.Task);
        Check("leaving Assets during SDK setup stops the stale request before sending", lateSends == 0 &&
            image.Source is null && state.Text == "" && !preview.IsEnabled);

        var after = await Status();
        var afterWorld = await Task.Run(() => SceneReader.Read(world));
        Check("preview leaves document revision, dirty source, pending apply and undo history unchanged",
            await Apply() == pending && await Task.Run(observer.Authoring.Document) == originalDocument &&
            await ReadName() == editedName && File.ReadAllBytes(originalDocument.SourcePath).SequenceEqual(originalSource) &&
            History() == beforeHistory && Field<TextBlock>("_applyLine").Text == beforeApplyUi);
        Check("preview leaves pause/step, World, scene generation and Editor camera/overlay selection unchanged",
            before.Paused && after.Paused && before.Scene == after.Scene && before.SceneGeneration == after.SceneGeneration &&
            step.IsEnabled && play.IsEnabled && beforeWorld.Entities.SequenceEqual(afterWorld.Entities) &&
            SamePose(beforeWorld.ScenePose, afterWorld.ScenePose) && beforeWorld.FirstRenderable == afterWorld.FirstRenderable &&
            beforeWorld.FirstRenderablePosition == afterWorld.FirstRenderablePosition &&
            SamePose(beforeCamera, Field<EditorCamera>("_camera").Pose()) && beforeSelection == Field<Selection>("_selection").Node,
            $"scene generation {after.SceneGeneration}, ordinary frames {before.Frame} → {after.Frame}");

        Click(play);
        await NameUiWait(async () => !step.IsEnabled && !(await Status()).Paused && (await Apply()).Request is
            { State: ApplyState.Applied } resumed && resumed.Receipt == pending.Request!.Receipt, "resume settles the same tracked apply", 60);
        Check("existing Play settles the original pending apply", (await Status()).SceneGeneration > before.SceneGeneration);
        ulong generation = (await Status()).SceneGeneration;
        Click(undo);
        await NameUiWait(async () => redo.IsEnabled && await ReadName() == originalName &&
            (await Status()).SceneGeneration > generation && (await Apply()).Request?.State == ApplyState.Applied,
            "real Undo and its apply settle", 60);
        Check("real Undo restores the original name", !undo.IsEnabled && redo.IsEnabled);
        generation = (await Status()).SceneGeneration;
        Click(redo);
        await NameUiWait(async () => undo.IsEnabled && await ReadName() == editedName &&
            (await Status()).SceneGeneration > generation && (await Apply()).Request?.State == ApplyState.Applied,
            "real Redo and its apply settle", 60);
        Check("real Redo restores the unsaved name and never writes source", undo.IsEnabled && !redo.IsEnabled &&
            File.ReadAllBytes(originalDocument.SourcePath).SequenceEqual(originalSource));
    }

    private static async Task<int> ServePreviewUiPeer(TcpListener listener, AssetGuid scene, AssetGuid asset, CancellationToken token,
        TaskCompletionSource? schemaSeen, Task? releaseSchema)
    {
        using var socket = await listener.AcceptTcpClientAsync(token);
        using var stream = socket.GetStream();
        using var reader = new StreamReader(stream);
        using var writer = new StreamWriter(stream) { AutoFlush = true, NewLine = "\n" };
        int requests = 0;
        while (await reader.ReadLineAsync(token) is string line)
        {
            JsonValue request = Json.Parse(line);
            JsonValue response = JsonValue.NewObject().Set("id", request.Find("id")!);
            string method = request.Find("method")!.AsString;
            switch (method)
            {
                case "hello":
                    response.Set("result", JsonValue.NewObject().Set("scene", Codec.Encode(scene)));
                    break;
                case "connection.schema":
                    schemaSeen?.TrySetResult();
                    if (releaseSchema is not null) await releaseSchema.WaitAsync(token);
                    response.Set("result", JsonValue.NewArray());
                    break;
                case "assets.previewMesh" when request.Find("params")?.Find("guid")?.AsString == asset.ToString():
                    ++requests;
                    // Complete 256×256 IHDR; no image data. The SDK validates
                    // the wire header; only the actual Avalonia decoder refuses it.
                    byte[] header = [137, 80, 78, 71, 13, 10, 26, 10,
                        0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 1, 0, 0, 0, 1, 0, 8, 6, 0, 0, 0, 0, 0, 0, 0];
                    var image = JsonValue.NewObject().Set("guid", Codec.Encode(asset)).Set("width", JsonValue.Number(256L))
                        .Set("height", JsonValue.Number(256L)).Set("mimeType", JsonValue.String("image/png"))
                        .Set("pngBase64", JsonValue.String(Convert.ToBase64String(header)));
                    response.Set("result", JsonValue.NewObject().Set("ok", image));
                    break;
                default: throw new InvalidOperationException("Unexpected test peer method: " + method);
            }
            await writer.WriteLineAsync(Json.Write(response).AsMemory(), token);
        }
        return requests;
    }

    private static void CheckMeshPreviewValidation(string work, List<NameUiCheck> checks)
    {
        string log = Path.Combine(work, "runtime.log");
        if (!File.Exists(log)) { checks.Add(new("Runtime log is available for validation scan", false, log)); return; }
        string[] messages = File.ReadLines(log).Where(line => Regex.IsMatch(line,
            "VUID-|UNASSIGNED-|Validation Error|Validation Warning|Vulkan Validation Layers reported", RegexOptions.IgnoreCase)).ToArray();
        checks.Add(new("Runtime log has no Vulkan validation warnings or errors", messages.Length == 0,
            messages.Length == 0 ? log : string.Join("\n", messages)));
        string editorLog = Path.Combine(work, "editor.log");
        checks.Add(new("Editor shutdown observes normal Runtime exit", File.Exists(editorLog) &&
            File.ReadLines(editorLog).Any(line => line.Contains("runtime exited with 0 after ", StringComparison.Ordinal)), editorLog));
    }
}
