using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private sealed class ThumbnailTestImage : IDisposable
    {
        internal int Disposals;
        public void Dispose() => ++Disposals;
    }

    private static void MeshThumbnailChecks()
    {
        var a = NewGuid();
        var b = NewGuid();
        var c = NewGuid();
        var requests = new AssetPreviewRequests();
        var cache = new AssetThumbnailCache<ThumbnailTestImage>();
        static AssetGuid NewGuid() => new(Guid.NewGuid());
        requests.SetThumbnailDemand([a], [a], false, 1000);
        requests.SetThumbnailDemand([a], [a], false, 1150);
        Check(requests.ThumbnailDelay(1199) == 1 && requests.TakeThumbnail(1199) is null,
            "thumbnail dwell from changed effective demand, independent of unchanged layout");
        var auto = requests.TakeThumbnail(1200)!;
        requests.SetThumbnailLayoutPending(true);
        Check(!requests.AdmitThumbnail(auto, 1200), "pending scroll/layout blocks send before its visibility is observed");
        requests.SetThumbnailLayoutPending(false);
        requests.SetInputs(new(a, b));
        requests.Request(withMaterial: true);
        Check(requests.TakeNext() is null && !requests.CanSetUpThumbnail(auto) && !requests.AdmitThumbnail(auto, 1201),
            "manual pair preempts an unsent automatic setup without taking a second slot");
        requests.SettleThumbnail(auto);
        var pair = requests.TakeNext()!;
        Check(pair.Material == b && requests.ThumbnailDelay(5000) is null && requests.Settle(pair),
            "pair is next without automatic pacing and retains exclusive admission");
        auto = requests.TakeThumbnail(1202)!;
        Check(requests.AdmitThumbnail(auto, 1203) && !requests.AdmitThumbnail(auto, 1204),
            "automatic send can be admitted once");
        requests.Request();
        Check(requests.TakeNext() is null && requests.IsThumbnailCurrent(auto) && requests.SettleThumbnail(auto),
            "sent automatic request holds slot and may settle before queued manual");
        var mesh = requests.TakeNext()!;
        Check(mesh.Guid == a && mesh.Material is null && requests.Settle(mesh), "manual geometry follows sent thumbnail immediately");
        requests.SetThumbnailDemand([b, c], [b, c], false, 1300);
        Check(requests.TakeThumbnail(1702) is null && requests.ThumbnailDelay(1702) == 1,
            "automatic sends are at least 500 milliseconds apart");
        auto = requests.TakeThumbnail(1703)!;
        requests.SetThumbnailDemand([c, b], [c, b], false, 1704);
        Check(!requests.AdmitThumbnail(auto, 1704) && !requests.SettleThumbnail(auto) &&
            requests.TakeThumbnail(1903) is null, "ordering change retires pending setup and restarts visible dwell");
        auto = requests.TakeThumbnail(1904)!;
        Check(auto.Guid == c && requests.AdmitThumbnail(auto, 1904), "topmost current miss gets the next turn");
        requests.InvalidateThumbnails(1905, catalog: true);
        requests.SetThumbnailDemand([c], [c], false, 1906);
        Check(!requests.IsThumbnailCurrent(auto) && !requests.SettleThumbnail(auto) && requests.ThumbnailDelay(2106) == 298,
            "old same-GUID result discarded after catalog/Runtime change; spacing is retained");
        requests.SetThumbnailDemand([c], [], false, 2500);
        Check(requests.ThumbnailDelay(9000) is null, "settled view has no timer work");
        requests.SetThumbnailDemand([c], [c], false, 9000);
        auto = requests.TakeThumbnail(9200)!;
        var recipe = new MaterialRecipe(new(b, 1f, 1f, 1f, 0f, 0.5f), MaterialFilter.Linear, MaterialAddress.Repeat);
        requests.SetInputs(new(a, null, -1, recipe));
        requests.SetThumbnailDemand([c], [c], true, 9201);
        requests.RequestDraft();
        Check(!requests.AdmitThumbnail(auto, 9201) && requests.TakeNext() is null && !requests.SettleThumbnail(auto),
            "opening the material dialog retires automatic intent while setup settles");
        var draft = requests.TakeNext()!;
        Check(draft.Draft == recipe && requests.Settle(draft) && requests.ThumbnailDelay(9900) is null,
            "material dialog draft preview retains the shared slot and automatic work stays paused");

        var images = Enumerable.Range(0, AssetThumbnailCache<ThumbnailTestImage>.Limit)
            .Select(_ => (Guid: NewGuid(), Image: new ThumbnailTestImage())).ToArray();
        foreach (var item in images) cache.Store(item.Guid, item.Image, null, null);
        cache.SetVisible(images.Select(x => x.Guid).ToArray(), 0);
        Check(cache.Count == 128 && cache.RetainedPixelBytes == 2 * 1024 * 1024 &&
            !cache.CanInsert && !cache.Store(a, null, "Busy", 2000) && images.All(x => x.Image.Disposals == 0),
            "both budgets are enforced and displayed images are pinned");
        cache.SetVisible([images[0].Guid], 0);
        cache.TryGet(images[1].Guid, out _, out _);
        cache.Store(a, null, "Busy", 2000);
        Check(images[2].Image.Disposals == 1 && cache.Count == 128 &&
            cache.RetainedPixelBytes == (127 * 64 * 64 * 4) && images[0].Image.Disposals == 0 &&
            images[1].Image.Disposals == 0, "eviction disposes the least recently used unpinned entry");
        cache.SetVisible([a], 1000);
        cache.SetVisible([a], 2100);
        Check(cache.TryGet(a, out _, out _), "a stationary transient failure is retained after cooldown");
        cache.SetVisible([], 2101);
        cache.SetVisible([a], 2102);
        Check(!cache.TryGet(a, out _, out _), "later visible entry permits transient retry");
        cache.Store(b, null, "InvalidMesh", null);
        cache.SetVisible([b], 5000);
        cache.SetVisible([], 5100);
        cache.SetVisible([b], 9000);
        Check(cache.TryGet(b, out _, out var diagnostic) && diagnostic == "InvalidMesh",
            "stable failure remains cached after reentry");
        cache.Suspend("CatalogUnavailable");
        Check(!cache.TryGet(c, out _, out diagnostic) && diagnostic == "CatalogUnavailable",
            "epoch refusal supplies a fallback for uncached mesh rows");
        cache.Clear();
        Check(cache.Count == 0 && cache.RetainedPixelBytes == 0 && cache.Suspended is null &&
            images.All(x => x.Image.Disposals == 1), "epoch reset releases every retained bitmap exactly once");
    }

    private static int RunMeshThumbnailUi(string runtime, string scene, string evidence)
    {
        Directory.CreateDirectory(evidence);
        return AppBuilder.Configure(() => new MeshThumbnailApp(runtime, scene, evidence)).UsePlatformDetect()
            .StartWithClassicDesktopLifetime([]);
    }

    private sealed class MeshThumbnailApp(string runtime, string scene, string evidence) : Application
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
                try
                {
                    await CheckMeshThumbnailUi(window, evidence);
                    exit = 0;
                }
                catch (Exception error) { Console.Error.WriteLine(error); }
                finally
                {
                    SetWindowField(window, "_leaveCloseApproved", true);
                    window.Close();
                    desktop.Shutdown(exit);
                }
            });
            base.OnFrameworkInitializationCompleted();
        }
    }

    private static void SetWindowField(MainWindow window, string field, object value) =>
        typeof(MainWindow).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(window, value);

    private static async Task CheckMeshThumbnailUi(MainWindow window, string evidence)
    {
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        void Verify(bool ok, string name)
        {
            if (!ok) throw new InvalidOperationException(name);
            Console.WriteLine("PASS " + name);
        }
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => Field<RuntimeProcess>("_runtime") is { } process && File.Exists(process.SessionPath) &&
            Field<SessionInfo>("_previewSession") is not null, "hosted Runtime", 120);
        var runtime = Field<RuntimeProcess>("_runtime");
        File.WriteAllText(Path.Combine(evidence, "runtime-workdir.txt"), runtime.Launch.WorkDirectory);
        nint hwnd = window.TryGetPlatformHandle()!.Handle;
        Verify(GetDpiForWindow(hwnd) == 96 && Field<ViewportHost>("_viewport").Window!.RuntimeChildIsHosted(),
            "Windows DPI is 96 and the real Bistro Runtime is embedded");

        var assetTabs = Field<TabControl>("_assetTabs");
        var list = Field<ListBox>("_assetsList");
        var search = Field<TextBox>("_assetSearch");
        var browser = Field<AssetBrowser>("_assetBrowser");
        var cache = Field<AssetThumbnailCache<Bitmap>>("_thumbnailCache");
        var log = Field<EditorLog>("_log");
        var props = new[] {
            (Name: "chair", Id: "31467343-4f1c-8ca1-aa32-14294a280320"),
            (Name: "awning", Id: "5dcf1faa-9602-8cb1-b6c8-e1006465cc0e"),
            (Name: "streetlight", Id: "1997ad25-e61c-8ca1-aa32-0e494312d613")
        };
        search.Text = props[0].Id;
        assetTabs.SelectedIndex = 1;
        await NameUiWait(() => browser.State == "Ready", "Bistro catalog loads", 120);
        Control Slot(AssetGuid guid) => EditorVisuals.VisibleAssetRows(list)
            .Where(row => row.Asset.Guid == guid).SelectMany(row => row.Row.GetVisualDescendants())
            .OfType<EditorVisuals.AssetThumbnailSlot>().Single();
        Bitmap? Painted(AssetGuid guid) => EditorVisuals.VisibleAssetRows(list)
            .Where(row => row.Asset.Guid == guid).SelectMany(row => row.Row.GetVisualDescendants())
            .OfType<EditorVisuals.AssetThumbnailSlot>().Where(slot => slot.Guid == guid)
            .SelectMany(slot => slot.Children.OfType<Image>())
            .Select(image => image.Source).OfType<Bitmap>().SingleOrDefault();
        string[] Sends()
        {
            using var stream = new FileStream(log.Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var reader = new StreamReader(stream);
            return reader.ReadToEnd().Split('\n').Where(line => line.Contains("assets thumbnail send ")).ToArray();
        }
        int SendsFor(AssetGuid guid) => Sends().Count(line => line.Contains(guid.ToString()));
        static byte[] Pixels(Bitmap image)
        {
            using var output = new MemoryStream();
            image.Save(output, PngBitmapEncoderOptions.Default);
            return output.ToArray();
        }

        var preview = Field<Button>("_previewMesh");
        var previewImage = Field<Image>("_previewImage");
        var examples = new Dictionary<AssetGuid, byte[]>();
        var fullPreviews = new Dictionary<AssetGuid, byte[]>();
        foreach (var prop in props)
        {
            var guid = AssetGuid.Parse(prop.Id);
            search.Text = prop.Id;
            await NameUiWait(() => Painted(guid) is not null, prop.Name + " thumbnail visible", 40);
            var image = Painted(guid)!;
            Verify(image.PixelSize == new PixelSize(64, 64) && SendsFor(guid) == 1, prop.Name + " exactly one automatic request " + guid);
            list.SelectedItem = browser.Visible.Single(asset => asset.Guid == guid);
            await NameUiWait(() => preview.IsEnabled, prop.Name + " manual preview available");
            Click(preview);
            await NameUiWait(() => previewImage.Source is Bitmap, prop.Name + " explicit geometry preview", 45);
            fullPreviews.Add(guid, Pixels((Bitmap)previewImage.Source!));
            using var stream = new MemoryStream(fullPreviews[guid]);
            using var scaled = Bitmap.DecodeToWidth(stream, 64, BitmapInterpolationMode.HighQuality);
            byte[] pixels = Pixels(image);
            Verify(pixels.SequenceEqual(Pixels(scaled)), prop.Name + " thumbnail matches downscaled explicit geometry PNG");
            examples.Add(guid, pixels);
            File.WriteAllBytes(Path.Combine(evidence, prop.Name + "-thumbnail.png"), pixels);
            await NativeCapture(window, Path.Combine(evidence, prop.Name + "-windows100.png"));
        }
        Verify(examples.Values.Select(p => Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(p))).Distinct().Count() == 3,
            "the three cooked props have distinct geometry stills");

        var chair = AssetGuid.Parse(props[0].Id);
        var awning = AssetGuid.Parse(props[1].Id);
        search.Text = "#mesh/";
        await NameUiWait(() => list.GetVisualDescendants().OfType<ScrollViewer>().Any(scroll => scroll.Extent.Height > scroll.Viewport.Height * 3),
            "long mesh list scrolls");
        var item = browser.Visible.Single(asset => asset.Guid == chair);
        list.ScrollIntoView(item);
        await NameUiWait(() => Painted(chair) is not null, "cached chair scrolled into view");
        var scroller = list.GetVisualDescendants().OfType<ScrollViewer>().First();
        scroller.ScrollToHome();
        await NameUiWait(() => Painted(chair) is null, "actual scroll removes chair from the clipped viewport");
        list.ScrollIntoView(item);
        await NameUiWait(() => Painted(chair) is not null, "actual scroll back paints cached chair");
        Verify(SendsFor(chair) == 1 && Pixels(Painted(chair)!).SequenceEqual(examples[chair]), "scroll back has zero repeat requests");

        var beforeRapid = SendsFor(chair);
        search.Text = props[0].Id;
        search.Text = props[2].Id;
        search.Text = props[1].Id;
        var sort = Field<ComboBox>("_assetSort");
        sort.SelectedIndex = 1;
        Field<ComboBox>("_assetGroup").SelectedIndex = 1;
        await NameUiWait(() => Painted(awning) is not null, "rapid filters/sort/group settle");
        Verify(SendsFor(chair) == beforeRapid && Pixels(Painted(awning)!).SequenceEqual(examples[awning]) && Painted(chair) is null,
            "fast filters, sorting and grouping retain GUID-to-image matching");

        var firstSession = Field<SessionInfo>("_previewSession");
        var onSession = typeof(MainWindow).GetMethod("OnSession", BindingFlags.Instance | BindingFlags.NonPublic)!;
        var reset = typeof(MainWindow).GetMethod("ResetThumbnails", BindingFlags.Instance | BindingFlags.NonPublic)!;
        using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(3));

        // A disposable preview transport supplies one refusal while the real catalog remains connected.
        using (var listener = new TcpListener(IPAddress.Loopback, 0))
        {
            listener.Start();
            var sent = new TaskCompletionSource<AssetGuid>(TaskCreationOptions.RunContinuationsAsynchronously);
            var refusal = ServeThumbnailPeer(listener, firstSession.Scene, sent, Task.CompletedTask, null, timeout.Token);
            SetWindowField(window, "_previewSession", firstSession with { Port = (ushort)((IPEndPoint)listener.LocalEndpoint).Port });
            reset.Invoke(window, null);
            search.Text = props[0].Id;
            Verify(await sent.Task.WaitAsync(TimeSpan.FromSeconds(35)) == chair, "disposable failure requested exact chair GUID");
            await refusal;
            await NameUiWait(() => AutomationProperties.GetHelpText(Slot(chair))?.Contains("InvalidMesh") == true,
                "failed thumbnail shows accessible row fallback");
            var failedRow = EditorVisuals.VisibleAssetRows(list).Single(row => row.Asset.Guid == chair).Row;
            await NativeCapture(window, Path.Combine(evidence, "thumbnail-fallback-windows100.png"));
            window.Activate();
            await NameUiWait(() => GetForegroundWindow() == hwnd, "foreground for native OLE drag");
            var from = failedRow.PointToScreen(new Point(55, failedRow.Bounds.Height / 2));
            await Task.Run(async () =>
            {
                if (!SetCursorPos(from.X, from.Y)) throw new InvalidOperationException("Native pointer unavailable");
                await Task.Delay(90);
                mouse_event(2, 0, 0, 0, 0);
                try
                {
                    await Task.Delay(100);
                    SetCursorPos(from.X + 15, from.Y);
                    await NameUiWait(() => Field<AssetDragIntent>("_assetDrag")?.Asset.Guid == chair,
                        "Windows OLE drag holds the selected chair GUID", 10);
                }
                finally { mouse_event(4, 0, 0, 0, 0); }
            });
            await NameUiWait(() => Field<AssetDragIntent>("_assetDrag") is null && browser.Selected == chair, "drag cancellation settles and keeps selection");
            Verify(browser.State == "Ready" && Painted(chair) is null, "failed thumbnail preserves catalog search, selection and native drag");
        }

        using (var listener = new TcpListener(IPAddress.Loopback, 0))
        {
            listener.Start();
            var stale = new TaskCompletionSource<AssetGuid>(TaskCreationOptions.RunContinuationsAsynchronously);
            var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var delivery = ServeThumbnailPeer(listener, firstSession.Scene, stale, release.Task, null, timeout.Token);
            SetWindowField(window, "_previewSession", firstSession with { Port = (ushort)((IPEndPoint)listener.LocalEndpoint).Port });
            reset.Invoke(window, null);
            search.Text = props[2].Id;
            Verify(await stale.Task.WaitAsync(TimeSpan.FromSeconds(25)) == AssetGuid.Parse(props[2].Id),
                "streetlight preview is held while replacing filtered row");
            search.Text = props[0].Id;
            await NameUiWait(() => EditorVisuals.VisibleAssetRows(list).SingleOrDefault().Asset?.Guid == chair,
                "chair replaces the streetlight in the virtualized list");
            release.TrySetResult();
            await delivery;
            var fresh = new TaskCompletionSource<AssetGuid>(TaskCreationOptions.RunContinuationsAsynchronously);
            await ServeThumbnailPeer(listener, firstSession.Scene, fresh, Task.CompletedTask, fullPreviews[chair], timeout.Token);
            Verify(await fresh.Task == chair, "the unchanged preview connection next requests the current chair");
            await NameUiWait(() => Painted(chair) is not null, "latest row renders after the held reply settles");
            SetWindowField(window, "_previewSession", firstSession);
            Verify(!cache.TryGet(AssetGuid.Parse(props[2].Id), out _, out _) &&
                Pixels(Painted(chair)!).SequenceEqual(examples[chair]), "late filtered response cannot enter cache or paint replacement row");
        }

        // Keep an old valid reply outstanding across a real second Runtime attachment.
        using (var listener = new TcpListener(IPAddress.Loopback, 0))
        {
            listener.Start();
            var sent = new TaskCompletionSource<AssetGuid>(TaskCreationOptions.RunContinuationsAsynchronously);
            var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            byte[] oldPng;
            using (var peer = await Task.Run(() => RemoteSession.Connect(firstSession)))
                oldPng = (await Task.Run(() => peer.Assets.PreviewMesh(chair))).Value.PngBytes;
            var delivery = ServeThumbnailPeer(listener, firstSession.Scene, sent, release.Task, oldPng, timeout.Token);
            SetWindowField(window, "_previewSession", firstSession with { Port = (ushort)((IPEndPoint)listener.LocalEndpoint).Port });
            reset.Invoke(window, null);
            search.Text = props[1].Id;
            Verify(await sent.Task.WaitAsync(TimeSpan.FromSeconds(35)) == awning, "old preview pending during Runtime replacement");
            try
            {
                var replacement = RuntimeProcess.Start(runtime.Launch with { WorkDirectory = RuntimeProcess.NewWorkDirectory() });
                try
                {
                    var secondSession = await Task.Run(() => replacement.WaitForSession(TimeSpan.FromSeconds(90)));
                    Verify(secondSession.Pid != firstSession.Pid, "independent second Bistro Runtime started");
                    SetWindowField(window, "_runtime", replacement);
                    onSession.Invoke(window, [replacement, secondSession, 0d, replacement.SessionPath]);
                    await Task.Run(runtime.Dispose);
                    await NameUiWait(() => ReferenceEquals(Field<SessionInfo>("_previewSession"), secondSession) &&
                        browser.State == "Ready", "reconnected Editor shows fresh catalog", 90);
                    var before = SendsFor(awning);
                    Verify(cache.Count == 0 && Painted(awning) is null, "new epoch emptied the cache even for same GUID");
                    release.TrySetResult();
                    await delivery;
                    await NameUiWait(() => Painted(awning) is not null, "second Runtime serves fresh thumbnail", 45);
                    // Sends are logged on completion. One old send drains and one new send renders.
                    Verify(SendsFor(awning) == before + 2 && Pixels(Painted(awning)!).SequenceEqual(examples[awning]),
                        "late matching-GUID reply cannot satisfy the next Runtime epoch");
                    await NativeCapture(window, Path.Combine(evidence, "replacement-runtime-windows100.png"));
                    var reload = window.GetVisualDescendants().OfType<Button>().Single(button => Equals(button.Content, "Reload view"));
                    var beforeReload = SendsFor(awning);
                    Click(reload);
                    Verify(cache.Count == 0, "Reload view invalidates the cache before listing completes");
                    await NameUiWait(() => Painted(awning) is not null, "Reload view renders again", 45);
                    Verify(SendsFor(awning) == beforeReload + 1 && Pixels(Painted(awning)!).SequenceEqual(examples[awning]),
                        "Reload view refreshes the same Runtime snapshot with one request");
                    File.WriteAllText(Path.Combine(evidence, "replacement-workdir.txt"), replacement.Launch.WorkDirectory);
                    replacement = null!; // MainWindow owns it after attachment.
                }
                finally { replacement?.Dispose(); }
            }
            finally { release.TrySetResult(); }
        }

        long[] sendTimes = Sends().Select(s => long.Parse(s.Trim().Split(' ')[^1], System.Globalization.CultureInfo.InvariantCulture)).ToArray();
        Verify(sendTimes.Length >= 5 && sendTimes.Zip(sendTimes.Skip(1), (a, b) => b - a).All(delta => delta >= 500),
            "native smoke automatic request gaps are all at least 500 ms");
        int lastCount = Sends().Length;
        await Task.Delay(800);
        Verify(Sends().Length == lastCount, "stationary settled list produces zero further requests");
        var currentSession = Field<SessionInfo>("_previewSession");
        var reloadButton = window.GetVisualDescendants().OfType<Button>().Single(button => Equals(button.Content, "Reload view"));
        foreach (var (failure, hint) in new[]
        {
            ("Busy", "Busy"),
            ("legacy", "updated Runtime"),
            ("CatalogUnavailable", "catalog is unavailable"),
            ("malformed", "RenderFailed"),
            ("disconnect", "RenderFailed"),
            ("decode", "could not be decoded")
        })
        {
            using var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            var observed = new TaskCompletionSource<AssetGuid>(TaskCreationOptions.RunContinuationsAsynchronously);
            var response = ServeThumbnailPeer(listener, currentSession.Scene, observed, Task.CompletedTask, null, timeout.Token, failure);
            SetWindowField(window, "_previewSession", currentSession with { Port = (ushort)((IPEndPoint)listener.LocalEndpoint).Port });
            int previousSends = SendsFor(awning);
            Click(reloadButton);
            Verify(await observed.Task.WaitAsync(TimeSpan.FromSeconds(25)) == awning, failure + " injected for the visible awning");
            await response;
            await NameUiWait(() => Painted(awning) is null && AutomationProperties.GetHelpText(Slot(awning))?.Contains(hint) == true,
                failure + " renders a row-local fallback");
            await Task.Delay(failure == "Busy" ? 2200 : 550);
            Verify(SendsFor(awning) == previousSends + 1 && browser.State == "Ready", failure + " does not retry or disrupt the catalog");
        }
        SetWindowField(window, "_previewSession", currentSession);
        Click(reloadButton);
        await NameUiWait(() => Painted(awning) is not null, "healthy preview after disposable failures", 45);
        File.WriteAllLines(Path.Combine(evidence, "thumbnail-sends.log"), Sends());
    }

    private static async Task ServeThumbnailPeer(TcpListener listener, AssetGuid scene, TaskCompletionSource<AssetGuid> sent,
        Task release, byte[]? png, CancellationToken token, string failure = "InvalidMesh")
    {
        using var socket = await listener.AcceptTcpClientAsync(token);
        using var stream = socket.GetStream();
        using var reader = new StreamReader(stream);
        using var writer = new StreamWriter(stream) { AutoFlush = true, NewLine = "\n" };
        while (await reader.ReadLineAsync(token) is string line)
        {
            var request = Json.Parse(line);
            string method = request.Find("method")!.AsString;
            var response = JsonValue.NewObject().Set("id", request.Find("id")!);
            if (method == "hello") response.Set("result", JsonValue.NewObject().Set("scene", Codec.Encode(scene)));
            else if (method == "connection.schema") response.Set("result", JsonValue.NewArray());
            else if (method == "assets.previewMesh")
            {
                var guid = AssetGuid.Parse(request.Find("params")!.Find("guid")!.AsString);
                sent.TrySetResult(guid);
                await release.WaitAsync(token);
                if (failure == "disconnect") return;
                if (failure == "legacy")
                {
                    response.Set("error", JsonValue.NewObject().Set("code", JsonValue.String("BadRequest"))
                        .Set("message", JsonValue.String("unknown or malformed asset query")));
                }
                else
                {
                    // A wire-valid 256×256 IHDR without pixel data reaches the actual decoder.
                    if (failure == "decode") png = [137, 80, 78, 71, 13, 10, 26, 10,
                        0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 1, 0, 0, 0, 1, 0, 8, 6, 0, 0, 0, 0, 0, 0, 0];
                    var outcome = png is null ? JsonValue.NewObject().Set("err", JsonValue.String(failure)) :
                        JsonValue.NewObject().Set("ok", JsonValue.NewObject().Set("guid", Codec.Encode(guid))
                            .Set("width", JsonValue.Number(256L)).Set("height", JsonValue.Number(256L))
                            .Set("mimeType", JsonValue.String("image/png")).Set("pngBase64", JsonValue.String(Convert.ToBase64String(png))));
                    response.Set("result", outcome);
                }
            }
            else throw new InvalidOperationException("Unexpected preview method: " + method);
            await writer.WriteLineAsync(Json.Write(response).AsMemory(), token);
        }
    }
}
