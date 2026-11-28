using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private readonly Button _viewBookmarks = new() { Content = "View Bookmarks", IsEnabled = false };
    private Window? _bookmarkWindow;
    private ViewBookmark? _activeBookmark;
    private BookmarkComparisonWindow? _bookmarkComparison;
    private Button? _bookmarkCapture;
    private Button? _shadowCapture;
    private Window? _directionalShadowComparison;
    private Func<bool>? _directionalShadowCurrent;
    private bool _bookmarkSamplePending;
    private long _bookmarkStoreRevision;
    private ulong? _bookmarkProjection;
    private event Action? BookmarksChanged;

    private void InitializeBookmarks()
    {
        _viewBookmarks.Click += (_, _) => ShowViewBookmarks();
        foreach (var button in new[] { _viewBookmarks, _bistroViews })
            button.AddHandler(PointerPressedEvent, (_, e) =>
            {
                if (!BistroLocatorProtected()) return;
                e.Handled = true;
                ShowNavigationMessage("Finish the current draft or gesture before changing the view.");
            }, RoutingStrategies.Tunnel);
    }

    private bool BookmarkAllowed => OperatingSystem.IsWindows() && _options.AttachSession is null &&
        !_referenceSessionFailed && _viewport.Window?.RuntimeChildIsHosted() == true &&
        _documentState is { Availability: DocumentAvailability.Known, State.Provenance: Provenance.HandAuthored };

    private bool BookmarkProjectionCurrent => _bookmarkProjection is ulong generation && _worker?.NavigationGeneration == generation;

    private const string BookmarkProjectionChanged = "The Runtime projection changed. Return to the bookmark before capturing again.";

    private void UpdateBookmarkActions(bool navigation)
    {
        _viewBookmarks.IsEnabled = navigation && BookmarkAllowed;
        if (_bookmarkCapture is { } capture)
            capture.IsEnabled = _activeBookmark is not null && _bistroReady && BookmarkProjectionCurrent && !_navigationPending &&
                !_bistroCapturing && !_bistroApplyBlocked && !_play.IsEnabled && navigation && BookmarkAllowed;
        if (_shadowCapture is { } shadow)
            shadow.IsEnabled = _bookmarkCapture?.IsEnabled == true && BistroVisibilityAllowed;
        if (_directionalShadowCurrent?.Invoke() == false) _directionalShadowComparison?.Close();
    }

    private void ObserveBookmarkDocument(DocumentObservation previous, DocumentObservation observation)
    {
        if (previous.State?.Token.DocumentId != observation.State?.Token.DocumentId ||
            previous.State?.Scene != observation.State?.Scene || previous.State?.Source != observation.State?.Source)
            RetireBookmarks();
    }

    private void RetireBookmarks()
    {
        ++_bookmarkStoreRevision;
        BookmarksChanged?.Invoke();
        _bookmarkWindow?.Close();
        if (_activeBookmark is not null) LeaveBistroView("The document changed. Return to a bookmark again.");
        CloseBookmarkComparison();
    }

    private void CloseBookmarkComparison()
    {
        CloseMaterialDiagnostic();
        _directionalShadowComparison?.Close();
        _bookmarkComparison?.Close();
        _bookmarkComparison = null;
    }

    private void ShowViewBookmarks()
    {
        if (!OperatingSystem.IsWindows() || !BookmarkAllowed || _worker is not { } worker ||
            _documentState.State is not { } opened) return;
        if (BistroLocatorProtected())
        {
            ShowNavigationMessage("Finish the current draft or gesture before opening View Bookmarks.");
            return;
        }
        if (_bookmarkWindow is { } existing) { existing.Activate(); return; }
        var entries = new ListBox { MinHeight = 145 };
        var name = new TextBox { PlaceholderText = "Bookmark name", MinWidth = 180 };
        var save = new Button { Content = "Save Current View" };
        var go = new Button { Content = "Return to View" };
        var remove = new Button { Content = "Remove" };
        var refresh = new Button { Content = "Refresh" };
        var capture = new Button { Content = "Capture and Compare", IsEnabled = false };
        var shadows = new Button { Content = "Compare Directional Shadows", IsEnabled = false };
        _bookmarkCapture = capture;
        _shadowCapture = shadows;
        var notice = new TextBlock { TextWrapping = TextWrapping.Wrap };
        shadows.AddHandler(PointerPressedEvent, (_, e) =>
        {
            if (!BistroLocatorProtected()) return;
            e.Handled = true;
            notice.Text = "Finish the current draft or gesture before comparing directional shadows.";
        }, RoutingStrategies.Tunnel);
        var window = new Window
        {
            Title = "View Bookmarks", Width = 540, Height = 360, MinWidth = 440, MinHeight = 310,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Content = new ScrollViewer { Content = new StackPanel
            {
                Margin = new Thickness(14), Spacing = 8,
                Children = { entries, EditorVisuals.Actions(name, save), EditorVisuals.Actions(go, capture, remove, refresh),
                    EditorVisuals.Actions(shadows), notice }
            } }
        };
        _bookmarkWindow = window;
        ViewBookmarkLibrary? library = null;
        ViewBookmarkStore? store = null;
        ViewBookmark? Selected() => library is not null && entries.SelectedIndex >= 0 &&
            entries.SelectedIndex < library.Entries.Count ? library.Entries[entries.SelectedIndex] : null;
        bool Current() => ReferenceEquals(_bookmarkWindow, window) && ReferenceEquals(_worker, worker) &&
            BookmarkAllowed && _documentState.State?.Token.DocumentId == opened.Token.DocumentId;
        void Controls()
        {
            go.IsEnabled = remove.IsEnabled = Selected() is not null && !_bookmarkSamplePending && Current();
            save.IsEnabled = library is not null && !_bookmarkSamplePending && Current();
        }
        bool CanAct()
        {
            if (Current() && !BistroLocatorProtected()) return true;
            notice.Text = "Finish the current draft or gesture and verify the document before using bookmarks.";
            return false;
        }
        void Refresh(Guid? selected = null)
        {
            if (!Current()) return;
            try
            {
                var verified = new ViewBookmarkStore(_documentState.State!);
                library = verified.Read();
                store = verified;
                var items = library.Entries;
                entries.ItemsSource = items.Select(entry =>
                    $"{entry.View.Name} · {entry.View.Extent.Width} × {entry.View.Extent.Height}").ToArray();
                entries.SelectedIndex = selected is Guid id ? items.ToList().FindIndex(b => b.Id == id)
                    : items.Count == 0 ? -1 : 0;
                notice.Text = items.Count == 0 ? "Navigate to a view and save it with a name." : "";
                if (_activeBookmark is { } active && !items.Contains(active))
                    LeaveBistroView("The saved bookmark changed. Return to the updated view.");
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException)
            {
                library = null;
                store = null;
                entries.ItemsSource = Array.Empty<string>();
                notice.Text = error.Message;
            }
            Controls();
        }
        bool VerifyRevision()
        {
            try
            {
                if (library is { } reviewed && store?.Read().Revision == reviewed.Revision) return true;
                notice.Text = "Bookmarks changed. Refresh and try again.";
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { notice.Text = error.Message; }
            return false;
        }
        void Published()
        {
            ++_bookmarkStoreRevision;
            BookmarksChanged?.Invoke();
        }
        window.Closed += (_, _) =>
        {
            _bookmarkWindow = null;
            _bookmarkCapture = null;
            _shadowCapture = null;
            _bookmarkSamplePending = false;
        };
        entries.SelectionChanged += (_, _) => Controls();
        refresh.Click += (_, _) => Refresh(Selected()?.Id);
        go.Click += (_, _) =>
        {
            if (!CanAct() || !VerifyRevision() || Selected() is not { } bookmark) return;
            if (StartFixedView(bookmark.View, null, bookmark)) notice.Text = "Settling " + bookmark.View.Name + "…";
            else notice.Text = FitMessage(bookmark.View);
        };
        capture.Click += (_, _) => { if (CanAct() && VerifyRevision()) CaptureBookmarkView(); };
        shadows.Click += (_, _) => { if (CanAct() && VerifyRevision()) CaptureDirectionalShadowView(); };
        remove.Click += async (_, _) =>
        {
            if (!CanAct() || library is not { } reviewed || store is not { } destination ||
                Selected() is not { } bookmark || !await ConfirmRenderLook(window, "Remove " + bookmark.View.Name + "?", "Remove") ||
                !CanAct() || library != reviewed || Selected() != bookmark) return;
            try { destination.Remove(reviewed.Revision, bookmark.Id); Published(); Refresh(); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { notice.Text = error.Message; }
        };
        save.Click += async (_, _) =>
        {
            if (!CanAct() || _bookmarkSamplePending || library is not { } reviewed || store is not { } destination ||
                _documentState.State is not { } document || _viewport.Window is not { } drawable ||
                _navigationPending || !_navigationReady || !BistroProjectionReady(drawable) ||
                !drawable.RuntimeChildHasExtent(drawable.Width, drawable.Height)) return;
            var size = new RenderLookExtent((uint)drawable.Width, (uint)drawable.Height);
            if (!size.IsValid)
            {
                notice.Text = "Resize the viewport to 128–1920 pixels per axis and at most 2,073,600 pixels.";
                return;
            }
            string label = name.Text?.Trim() ?? "";
            if (label.Length == 0) { notice.Text = "Enter a bookmark name."; return; }
            var duplicate = reviewed.Entries.FirstOrDefault(b =>
                string.Equals(b.View.Name, label, StringComparison.OrdinalIgnoreCase));
            long camera = _viewportCameraIntent;
            long revision = _bistroRevision;
            if (duplicate is not null && !await ConfirmRenderLook(window, "Replace " + duplicate.View.Name + "?", "Replace"))
                return;
            if (!CanAct() || library != reviewed || _documentState.State?.Token != document.Token ||
                _viewportCameraIntent != camera || _bistroRevision != revision) return;
            _bookmarkSamplePending = true;
            Controls();
            worker.ObserveBookmarkView(document.Token, camera, size, null, null, (frame, error) =>
            {
                if (!Current()) return;
                _bookmarkSamplePending = false;
                Controls();
                if (frame is null || library != reviewed || _documentState.State?.Token != document.Token ||
                    _viewportCameraIntent != camera || _bistroRevision != revision || !CanAct())
                {
                    notice.Text = error ?? "The view changed. Save again from a settled viewport.";
                    return;
                }
                try
                {
                    destination.Save(reviewed.Revision, frame.View with { Name = label }, duplicate?.Id);
                    Published();
                    Refresh(duplicate?.Id);
                    notice.Text = "Saved " + label + ".";
                }
                catch (Exception failure) when (failure is IOException or UnauthorizedAccessException) { notice.Text = failure.Message; }
            });
        };
        window.Show(this);
        Refresh(_activeBookmark?.Id);
        UpdateViewportNavigationActions();
    }

    private void VerifyRestoredBookmark(ViewportObservation view)
    {
        if (_worker is not { } worker || _documentState.State is not { } document ||
            _activeBookmark is not { } bookmark) return;
        long revision = _bistroRevision;
        long storeRevision = _bookmarkStoreRevision;
        worker.ObserveBookmarkView(document.Token, _viewportCameraIntent, view.Extent, view, null, (frame, error) =>
        {
            if (!ReferenceEquals(_worker, worker) || _bistroRevision != revision || bookmark != _activeBookmark) return;
            if (frame is null || _documentState.State?.Token != document.Token || _bookmarkStoreRevision != storeRevision ||
                !BookmarkStillSaved(document, bookmark))
            {
                LeaveBistroView(error ?? "The restored bookmark could not be verified.");
                return;
            }
            if (frame.Status.SceneGeneration != worker.NavigationGeneration)
            {
                LeaveBistroView(BookmarkProjectionChanged);
                return;
            }
            _bookmarkProjection = frame.Status.SceneGeneration;
            _bistroReady = true;
            ++_pickRequest;
            ShowNavigationMessage(view.Name + " · ready");
            UpdateViewportNavigationActions();
        });
    }

    private static bool BookmarkStillSaved(DocumentState document, ViewBookmark bookmark)
    {
        if (!OperatingSystem.IsWindows()) return false;
        try { return new ViewBookmarkStore(document).Read().Entries.Contains(bookmark); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException) { return false; }
    }

    private void CaptureBookmarkView()
    {
        if (_worker is not { } worker || _documentState.State is not { } document || _activeBookmark is not { } bookmark ||
            !_bistroReady || _bistroCapturing || _bookmarkProjection is not ulong projection || BistroLocatorProtected()) return;
        if (!BookmarkProjectionCurrent)
        {
            LeaveBistroView(BookmarkProjectionChanged);
            return;
        }
        if (!BookmarkStillSaved(document, bookmark))
        {
            LeaveBistroView("The bookmark changed. Refresh and return to the updated view.");
            return;
        }
        long revision = _bistroRevision;
        long camera = _viewportCameraIntent;
        bool Current() => !_shuttingDown && ReferenceEquals(_worker, worker) && bookmark == _activeBookmark &&
            _bistroReady && _bistroRevision == revision && _viewportCameraIntent == camera &&
            _documentState.State?.Token.DocumentId == document.Token.DocumentId;
        string path = Path.Combine(_workDirectory, "bookmark-" + Guid.NewGuid().ToString("N") + ".png");
        _bistroCapturing = true;
        UpdateViewportNavigationActions();
        worker.ObserveBookmarkView(document.Token, camera, bookmark.View.Extent, bookmark.View, path, (frame, error) =>
        {
            try
            {
                if (!Current()) return;
                if (!BookmarkProjectionCurrent)
                {
                    LeaveBistroView(BookmarkProjectionChanged);
                    return;
                }
                if (_documentState.State?.Token != document.Token) return;
                if (!BookmarkStillSaved(document, bookmark))
                {
                    LeaveBistroView("The bookmark changed. Refresh before capturing again.");
                    return;
                }
                if (frame is null) { ShowNavigationMessage(error ?? "Capture unavailable."); return; }
                var bitmap = BookmarkComparisonWindow.ReadImage(path, frame.View.Extent, reference: false);
                if (!BookmarkProjectionCurrent)
                {
                    bitmap.Dispose();
                    LeaveBistroView(BookmarkProjectionChanged);
                    return;
                }
                if (_bookmarkComparison is null)
                {
                    _bookmarkComparison = new BookmarkComparisonWindow(() => Current() && BookmarkProjectionCurrent);
                    _bookmarkComparison.Closed += (_, _) => _bookmarkComparison = null;
                    _bookmarkComparison.Show(this);
                }
                _bookmarkComparison.ShowCapture(frame, bitmap);
                ShowNavigationMessage(bookmark.View.Name + " captured.");
            }
            catch (Exception failure) when (failure is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException)
            { ShowNavigationMessage("Capture unavailable. " + failure.Message); }
            finally
            {
                if (error?.Contains("outcome unknown", StringComparison.Ordinal) != true) DeleteBistroOutput(path);
                if (Current()) { _bistroCapturing = false; UpdateViewportNavigationActions(); }
            }
        }, settledGeneration: projection);
    }

    private void CaptureDirectionalShadowView()
    {
        if (_worker is not { } worker || _documentState.State is not { } document || _activeBookmark is not { } bookmark ||
            !_bistroReady || _bistroCapturing || _bistroApplyBlocked || !BookmarkProjectionCurrent ||
            _bookmarkProjection is not ulong generation || !BistroVisibilityAllowed || BistroLocatorProtected() ||
            _viewport.Window is not { } view || !BistroFixedDrawable(view, bookmark.View) ||
            !BookmarkStillSaved(document, bookmark))
        {
            ShowNavigationMessage("Return to the bookmark after settling Apply and restore default viewport visibility.");
            return;
        }
        _directionalShadowComparison?.Close();
        long revision = _bistroRevision;
        long camera = _viewportCameraIntent;
        bool SameView() => !_shuttingDown && ReferenceEquals(worker, _worker) &&
            _activeBookmark == bookmark && _bistroRevision == revision && _viewportCameraIntent == camera;
        bool Current() => SameView() && _documentState.State?.Token == document.Token &&
            _bistroReady && _bookmarkProjection == generation && BookmarkProjectionCurrent && BistroVisibilityAllowed &&
            _viewport.Window is { } drawable && BistroFixedDrawable(drawable, bookmark.View);
        string stem = Path.Combine(_workDirectory, "directional-shadow-" + Guid.NewGuid().ToString("N"));
        string normalPath = stem + "-normal.png", unshadowedPath = stem + "-unshadowed.png";
        _bistroCapturing = true;
        ShowNavigationMessage("Capturing directional shadow comparison…");
        UpdateViewportNavigationActions();
        worker.CaptureDirectionalShadowDiagnostic(document.Token, camera, generation, bookmark.View, normalPath,
            unshadowedPath, (pair, message) =>
        {
            Bitmap? normal = null, unshadowed = null;
            try
            {
                if (!SameView()) return;
                if (!Current() || BistroLocatorProtected() || !BookmarkStillSaved(document, bookmark))
                {
                    ShowNavigationMessage("The comparison expired. Return to the current settled bookmark.");
                    return;
                }
                if (pair is null) { ShowNavigationMessage(message); return; }
                normal = BookmarkComparisonWindow.ReadImage(normalPath, bookmark.View.Extent, reference: false);
                unshadowed = BookmarkComparisonWindow.ReadImage(unshadowedPath, bookmark.View.Extent, reference: false);
                if (!Current() || !BookmarkStillSaved(document, bookmark)) return;
                var sun = pair.Normal.Data.DirectionalLights.Single();
                var identity = EditorVisuals.Secondary(FormattableString.Invariant(
                    $"{bookmark.View.Name} · Projection {pair.Generation} · {bookmark.View.Extent.Width} × {bookmark.View.Extent.Height}"));
                var lighting = EditorVisuals.Secondary(FormattableString.Invariant(
                    $"Directional light {sun.Intensity:G4} · direction {sun.Direction} · color {sun.Color} · exposure {pair.Normal.Data.ExposureCompensationEv?.ToString("G") ?? "unknown"} EV"));
                lighting.TextWrapping = TextWrapping.Wrap;
                var treatment = EditorVisuals.Secondary("Both images retain directional illumination and the scene's camera effects.");
                var summary = EditorVisuals.Stack(6, identity, lighting, treatment);
                var dialog = BuildDiagnosticComparison("Directional Shadow Diagnostic", "Without directional shadows",
                    summary, normal, unshadowed);
                _directionalShadowComparison = dialog;
                _directionalShadowCurrent = Current;
                dialog.Closed += (_, _) =>
                {
                    _directionalShadowComparison = null;
                    _directionalShadowCurrent = null;
                };
                dialog.Show(this);
                normal = unshadowed = null;
                ShowNavigationMessage(message);
            }
            catch (Exception failure) when (failure is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException)
            { ShowNavigationMessage("Directional shadow comparison unavailable. " + failure.Message); }
            finally
            {
                normal?.Dispose();
                unshadowed?.Dispose();
                if (!message.Contains("outcome unknown", StringComparison.OrdinalIgnoreCase))
                {
                    DeleteBistroOutput(normalPath);
                    DeleteBistroOutput(unshadowedPath);
                }
                if (SameView()) { _bistroCapturing = false; UpdateViewportNavigationActions(); }
            }
        });
    }
}

internal sealed class BookmarkComparisonWindow : Window
{
    private readonly Image _capture = new() { Stretch = Stretch.Uniform, Height = 300 };
    private readonly Image _reference = new() { Stretch = Stretch.Uniform, Height = 300 };
    private readonly TextBlock _captureDetails = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _referenceDetails = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _notice = new() { TextWrapping = TextWrapping.Wrap };
    private bool _closed;
    private long _intent;
    internal BookmarkFrame? Latest { get; private set; }

    internal BookmarkComparisonWindow(Func<bool> current)
    {
        Title = "Bookmark Reference Comparison";
        Width = 1050;
        Height = 540;
        MinWidth = 640;
        var choose = new Button { Content = "Choose Local PNG…" };
        choose.Click += async (_, _) =>
        {
            long intent = ++_intent;
            try
            {
                var picked = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
                {
                    Title = "Choose a local reference PNG", AllowMultiple = false,
                    FileTypeFilter = [new FilePickerFileType("PNG") { Patterns = ["*.png"] }]
                });
                if (_closed || !current() || intent != _intent || picked.FirstOrDefault()?.TryGetLocalPath() is not { } path)
                    return;
                LoadReference(path);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException)
            {
                if (!_closed && current() && intent == _intent) _notice.Text = error.Message;
            }
        };
        var left = new StackPanel { Spacing = 8, Children = { EditorVisuals.Heading("Atlantis capture"), _capture, _captureDetails } };
        var right = new StackPanel { Spacing = 8, Children = { EditorVisuals.Heading("Visual reference"), _reference, _referenceDetails, choose, _notice } };
        var columns = EditorVisuals.Columns("*,*", left, right);
        columns.ColumnSpacing = 14;
        columns.Margin = new Thickness(14);
        Content = new ScrollViewer { Content = columns };
        Closed += (_, _) =>
        {
            _closed = true;
            SetBitmap(_capture, null);
            SetBitmap(_reference, null);
        };
    }

    internal void ShowCapture(BookmarkFrame frame, Bitmap bitmap)
    {
        Latest = frame;
        SetBitmap(_capture, bitmap);
        _captureDetails.Text = $"{frame.View.Name} · {frame.View.Extent.Width} × {frame.View.Extent.Height} · frame {frame.Report.Frame}" +
            $"\nProjection {frame.Status.SceneGeneration} · exposure {frame.Report.Data.ExposureCompensationEv?.ToString("G") ?? "Unavailable"} EV" +
            (frame.PreviousProjection ? "\nLast visible projection" : "");
    }

    internal void LoadReference(string path)
    {
        var bitmap = ReadImage(path, default, reference: true);
        SetBitmap(_reference, bitmap);
        _referenceDetails.Text = $"{Path.GetFileName(path)} · {bitmap.PixelSize.Width} × {bitmap.PixelSize.Height}\nCapture conditions unknown";
        _notice.Text = "";
    }

    internal static Bitmap ReadImage(string path, RenderLookExtent extent, bool reference)
    {
        int limit = reference ? 8 * 1024 * 1024 : checked((int)(extent.Width * extent.Height * 8));
        byte[] bytes = BistroReferenceWindow.ReadPng(path, limit);
        if (bytes.Length < 33 || !bytes.AsSpan(0, 8).SequenceEqual(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }) ||
            BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(8, 4)) != 13 ||
            !bytes.AsSpan(12, 4).SequenceEqual("IHDR"u8)) throw new IOException("Expected a PNG image.");
        uint w = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(16, 4));
        uint h = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(20, 4));
        if (reference ? w is 0 or > 4096 || h is 0 or > 4096 : w != extent.Width || h != extent.Height)
            throw new IOException(reference ? "Reference PNG exceeds the 4096-pixel axis limit." : "Unexpected capture dimensions.");
        using var input = new MemoryStream(bytes, writable: false);
        var image = new Bitmap(input);
        if (image.PixelSize == new PixelSize((int)w, (int)h)) return image;
        image.Dispose();
        throw new IOException("The decoded PNG dimensions do not match its header.");
    }

    private static void SetBitmap(Image target, Bitmap? replacement)
    {
        var old = target.Source as Bitmap;
        target.Source = replacement;
        old?.Dispose();
    }
}
