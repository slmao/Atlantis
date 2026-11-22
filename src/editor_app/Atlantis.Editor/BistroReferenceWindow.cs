using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using Atlantis.Editor.Model;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using Avalonia.Threading;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private readonly Grid _bistroViewportArea = new();
    private readonly Button _bistroViews = new() { Content = "Bistro views", IsEnabled = false };
    private readonly MenuItem _bistroStreet = new() { Header = "Street Corner" };
    private readonly MenuItem _bistroTerrace = new() { Header = "Storefront Terrace" };
    private readonly MenuItem _bistroCapture = new() { Header = "Capture and compare" };
    private readonly MenuItem _bistroBefore = new() { Header = "Capture Before" };
    private readonly MenuItem _bistroAfter = new() { Header = "Capture After" };
    private readonly MenuItem _bistroLocateCamera = new() { Header = "Locate active scene camera" };
    private readonly MenuItem _bistroExit = new() { Header = "Leave reference view" };
    private readonly MenuItem _bistroEnable = new() { Header = "Use reference views for this Bistro copy" };
    private readonly DispatcherTimer _bistroLayoutTimer = new() { Interval = TimeSpan.FromMilliseconds(40) };
    private BistroReferenceView? _bistroView;
    private BistroReferenceWindow? _bistroCompare;
    private long? _bistroOptIn;
    private long _bistroRevision;
    private long _bistroLocatorIntent;
    private long _bistroLayoutStarted;
    private string? _bistroUnknownPath;
    private bool _bistroBuiltIn, _bistroSent, _bistroReady, _bistroCapturing, _bistroResizeRecovery, _bistroApplyBlocked;

    private void InitializeBistroViews()
    {
        _bistroViews.Flyout = new MenuFlyout
        {
            Items = { _bistroEnable, _bistroStreet, _bistroTerrace, new Separator(), _bistroLocateCamera,
                _bistroBefore, _bistroAfter, _bistroCapture, _bistroExit },
        };
        _bistroEnable.Click += (_, _) =>
        {
            if (_worker is not { } worker || !_canAddMeshToDocument) return;
            _bistroOptIn = worker.MeshDocumentLifetime;
            ShowNavigationMessage("Bistro views enabled for this document. Use the original Bistro world coordinates.");
            UpdateViewportNavigationActions();
        };
        _bistroStreet.Click += (_, _) => SelectBistroView(BistroReferenceView.Street);
        _bistroTerrace.Click += (_, _) => SelectBistroView(BistroReferenceView.Terrace);
        _bistroCapture.Click += (_, _) => CaptureBistroView();
        _bistroBefore.Click += (_, _) => CaptureBistroView(paired: true);
        _bistroAfter.Click += (_, _) => CaptureBistroView(paired: true, after: true);
        _bistroLocateCamera.Click += (_, _) => LocateBistroCamera();
        _bistroExit.Click += (_, _) => LeaveBistroView("Reference view closed.");
        _bistroViewportArea.SizeChanged += (_, _) => OnBistroAreaChanged();
        _bistroLayoutTimer.Tick += (_, _) => SettleBistroDrawable();
        ScalingChanged += (_, _) =>
        {
            if (_bistroView is null) return;
            if (!BistroAreaFits()) { LeaveBistroView("Enlarge the viewport to at least 960 × 540 physical pixels."); return; }
            _viewport.SetPhysicalSize((BistroReferenceView.Width, BistroReferenceView.Height), RenderScaling);
            OnBistroDrawableChanged();
        };
    }

    private bool BistroAllowed =>
        BistroVisibilityAllowed && _worker is { } worker &&
        (_bistroBuiltIn || (_canAddMeshToDocument && _bistroOptIn == worker.MeshDocumentLifetime));

    private bool BistroTuningView => _bistroView == BistroReferenceView.Terrace &&
        _worker is { } worker && _canAddMeshToDocument && _bistroOptIn == worker.MeshDocumentLifetime;

    private bool BistroTuningAllowed => BistroTuningView && BistroVisibilityAllowed;

    private void UpdateBistroActions(bool navigation)
    {
        bool hosted = _options.AttachSession is null && _viewport.Window?.RuntimeChildIsHosted() == true;
        _bistroViews.IsEnabled = navigation && hosted && BistroVisibilityAllowed && (BistroAllowed || _canAddMeshToDocument);
        _bistroEnable.IsVisible = !BistroAllowed && _canAddMeshToDocument;
        _bistroStreet.IsEnabled = _bistroTerrace.IsEnabled = BistroAllowed;
        _bistroExit.IsEnabled = _bistroView is not null;
        _bistroCapture.IsEnabled = _bistroReady && !_navigationPending && !_bistroCapturing &&
            !_bistroApplyBlocked && !_play.IsEnabled && navigation;
        _bistroBefore.IsVisible = _bistroAfter.IsVisible = _bistroLocateCamera.IsVisible = BistroTuningAllowed;
        _bistroLocateCamera.IsEnabled = BistroTuningAllowed && _bistroReady && navigation;
        _bistroBefore.IsEnabled = BistroTuningAllowed && _bistroCapture.IsEnabled;
        _bistroAfter.IsEnabled = _bistroBefore.IsEnabled && _bistroCompare?.Before is not null;
    }

    private bool BistroAreaFits() =>
        _bistroViewportArea.Bounds.Width * RenderScaling >= BistroReferenceView.Width &&
        _bistroViewportArea.Bounds.Height * RenderScaling >= BistroReferenceView.Height;

    private void SelectBistroView(BistroReferenceView view)
    {
        if (!_bistroViews.IsEnabled || !BistroAllowed) return;
        if (!BistroAreaFits())
        {
            LeaveBistroView();
            ShowNavigationMessage("Enlarge the viewport to at least 960 × 540 physical pixels.");
            return;
        }
        if (_bistroView != view) _bistroCompare?.ClearPair();
        _bistroView = view;
        RetireViewportOleTarget(expireRelease: true);
        _viewport.SetPhysicalSize((BistroReferenceView.Width, BistroReferenceView.Height), RenderScaling);
        OnBistroDrawableChanged();
    }

    private void OnBistroAreaChanged()
    {
        if (_bistroView is null) return;
        if (!BistroAreaFits())
            LeaveBistroView("Reference view closed. Enlarge the viewport to 960 × 540 physical pixels to return.");
        else OnBistroDrawableChanged();
    }

    private void OnBistroDrawableChanged()
    {
        if (_bistroView is null) return;
        ++_bistroRevision;
        ++_pickRequest;
        _bistroReady = _bistroSent = _bistroCapturing = false;
        _navigationPending = true;
        _bistroLayoutStarted = Stopwatch.GetTimestamp();
        _bistroLayoutTimer.Start();
        ShowNavigationMessage("Settling the fixed viewport…");
        UpdateViewportNavigationActions();
    }

    private void SettleBistroDrawable()
    {
        if (_bistroView is not { } view || _worker is not { } worker ||
            Stopwatch.GetElapsedTime(_bistroLayoutStarted) > TimeSpan.FromSeconds(10))
        {
            LeaveBistroView("The fixed viewport did not settle. Select the reference view again.");
            return;
        }
        if (_viewport.Window is not { } window || !BistroFixedDrawable(window)) return;
        _bistroLayoutTimer.Stop();
        _bistroSent = true;
        _viewportCameraIntent = worker.Navigate(null, _hierarchyRows, _selection.Generation, null, view);
    }

    private static bool BistroFixedDrawable(ViewportWindow window) =>
        window.Width == BistroReferenceView.Width && window.Height == BistroReferenceView.Height &&
        window.RuntimeChildHasExtent(window.Width, window.Height);

    private void ObserveBistroNavigation(ViewportNavigationState state)
    {
        if (_bistroView is not { } view || !_bistroSent) return;
        if (!state.ProjectionReady || state.Message != view.Name ||
            _viewport.Window is not { } window || !BistroFixedDrawable(window))
        {
            LeaveBistroView(state.Message ?? "Return to the reference view after the Runtime scene changes.");
            return;
        }
        _bistroReady = true;
    }

    private void LeaveBistroView(string? message = null)
    {
        if (_bistroView is null) return;
        ++_bistroRevision;
        ++_pickRequest;
        if (!_bistroSent) _navigationPending = false;
        _bistroView = null;
        _bistroCompare?.ClearPair();
        _bistroReady = _bistroSent = _bistroCapturing = false;
        _bistroResizeRecovery = true;
        _bistroLayoutTimer.Stop();
        _viewport.SetPhysicalSize(null, RenderScaling);
        if (message is not null) ShowNavigationMessage(message);
        UpdateViewportNavigationActions();
    }

    private bool BistroProjectionReady(ViewportWindow window)
    {
        if (_bistroView is not null) return _bistroReady && BistroFixedDrawable(window);
        if (!_bistroResizeRecovery) return true;
        _bistroResizeRecovery = !window.RuntimeChildHasExtent(window.Width, window.Height);
        return !_bistroResizeRecovery;
    }

    private bool BistroLocatorProtected() => CurrentSceneInput() is not null || _batchDraft is not null ||
        _draftProtection.Stale is not null || _pendingInspectorBlur is not null || _pendingSceneFieldEdits != 0 ||
        _referenceBusy || _leaveArmed || _leaveInProgress || _recoveryDecision || _gizmoGrab is not null || _groupGrab is not null ||
        _hierarchyDrag is not null || _assetDrag is not null || _alignmentId is not null;

    private void LocateBistroCamera()
    {
        if (!_bistroLocateCamera.IsEnabled || _worker is not { } worker) return;
        if (BistroLocatorProtected())
        {
            ShowNavigationMessage("Finish the current draft or gesture before locating the active camera.");
            return;
        }
        long request = ++_bistroLocatorIntent;
        long revision = _bistroRevision;
        ulong selection = _selection.Generation;
        worker.LocateBistroCamera(_hierarchyProjection.Rows, selection, _viewportCameraIntent, (row, error) =>
        {
            if (!ReferenceEquals(_worker, worker) || request != _bistroLocatorIntent ||
                revision != _bistroRevision || !BistroTuningAllowed) return;
            if (selection != _selection.Generation || BistroLocatorProtected() ||
                row is null || !_hierarchyProjection.Rows.Any(current =>
                    current.Node == row.Node && ReferenceEquals(current.Incarnation, row.Incarnation)))
            {
                ShowNavigationMessage(error ?? "The selection, draft or camera changed. Locate the camera again.");
                return;
            }
            SetSelection(row.Node, scroll: true);
            ShowNavigationMessage("Active scene camera selected.");
        });
    }

    private void CaptureBistroView(bool paired = false, bool after = false)
    {
        MenuItem action = !paired ? _bistroCapture : after ? _bistroAfter : _bistroBefore;
        if (!action.IsEnabled || _worker is not { } worker || _bistroView is not { } view) return;
        var comparison = _bistroCompare;
        var baseline = after ? comparison?.Before?.Capture.Projection : null;
        if (after && baseline is null) return;
        long revision = _bistroRevision;
        long lifetime = worker.MeshDocumentLifetime;
        string path = Path.Combine(_workDirectory, "bistro-" + Guid.NewGuid().ToString("N") + ".png");
        _bistroCapturing = true;
        ShowNavigationMessage("Capturing " + view.Name + "…");
        UpdateViewportNavigationActions();
        worker.CaptureBistroView(view, _viewportCameraIntent, path, (capture, error) =>
        {
            bool current = ReferenceEquals(_worker, worker) && worker.MeshDocumentLifetime == lifetime &&
                revision == _bistroRevision && !_shuttingDown && _bistroReady;
            if (current)
            {
                _bistroCapturing = false;
                UpdateViewportNavigationActions();
            }
            try
            {
                if (!current || (paired && (!BistroTuningAllowed || _bistroCompare != comparison))) return;
                if (capture is null)
                {
                    ShowNavigationMessage(error ?? "Capture unavailable.");
                    return;
                }
                if (!string.Equals(capture.Report.Image?.Path, path, StringComparison.OrdinalIgnoreCase))
                    throw new IOException("The capture receipt named an unexpected path.");
                byte[] png = BistroReferenceWindow.ReadPng(path);
                using var stream = new MemoryStream(png, writable: false);
                var bitmap = new Bitmap(stream);
                if (bitmap.PixelSize != new PixelSize(BistroReferenceView.Width, BistroReferenceView.Height))
                {
                    bitmap.Dispose();
                    throw new IOException("The PNG does not have the reference dimensions.");
                }
                var comparisonWindow = _bistroCompare;
                if (comparisonWindow is null)
                {
                    comparisonWindow = new BistroReferenceWindow();
                    comparisonWindow.Closed += (_, _) =>
                    {
                        _bistroCompare = null;
                        UpdateViewportNavigationActions();
                    };
                    _bistroCompare = comparisonWindow;
                }
                if (paired) comparisonWindow.SetPairCapture(capture, bitmap, png, after);
                else comparisonWindow.SetCapture(capture, bitmap, png);
                RememberSceneStatisticsFrame(worker, capture);
                if (!comparisonWindow.IsVisible) comparisonWindow.Show(this);
                UpdateViewportNavigationActions();
                ShowNavigationMessage(paired ? (after ? "After captured." : "Before captured.") : view.Name + " captured.");
            }
            catch (Exception problem) when (problem is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException)
            {
                ShowNavigationMessage("Capture unavailable: " + problem.Message);
            }
            finally
            {
                // The Runtime may still be writing if the reply was lost. Clean up when it exits.
                if (error?.Contains("outcome unknown", StringComparison.Ordinal) == true) _bistroUnknownPath = path;
                else DeleteBistroOutput(path);
            }
        }, paired, baseline);
    }

    private void DeleteBistroOutput(string path)
    {
        try { File.Delete(path); }
        catch (Exception problem) when (problem is IOException or UnauthorizedAccessException)
        { _log.Write("reference capture cleanup: " + problem.Message); }
    }
}

internal sealed class BistroReferenceWindow : Window
{
    private readonly Image _atlantis = new() { Stretch = Stretch.Uniform, Height = 285 };
    private readonly Image _orca = new() { Stretch = Stretch.Uniform, Height = 285 };
    private readonly TextBlock _atlantisDetails = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _referenceDetails = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _referenceStatus = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBox _lighting = new() { IsReadOnly = true, AcceptsReturn = true, Height = 86 };
    private Bitmap? _captureBitmap, _referenceBitmap;
    internal BistroCapture? Latest { get; private set; }
    internal ReadOnlyMemory<byte> CapturedPng { get; private set; }
    private BistroReferenceView? _referenceView;
    private readonly TabControl _tabs = new();
    private readonly TabItem _pairTab;
    private readonly TabItem _referenceTab;
    private CapturePane BeforePane { get; } = new("Before");
    private CapturePane AfterPane { get; } = new("After");
    internal PairImage? Before => BeforePane.Current;
    internal PairImage? After => AfterPane.Current;

    internal sealed record PairImage(BistroCapture Capture, Bitmap Bitmap, byte[] Png);

    private sealed class CapturePane
    {
        private readonly Image _image = new() { Stretch = Stretch.Uniform, Height = 260 };
        private readonly TextBlock _details = new() { TextWrapping = TextWrapping.Wrap };
        private readonly TextBox _lights = new() { IsReadOnly = true, AcceptsReturn = true, Height = 100 };
        internal StackPanel Content { get; }
        internal PairImage? Current { get; private set; }

        internal CapturePane(string title)
        {
            Content = new StackPanel { Spacing = 8, Children = {
                EditorVisuals.Heading(title), _image, _details, _lights,
            } };
            Set(null);
        }

        internal void Set(PairImage? image)
        {
            var previous = Current;
            Current = image;
            _image.Source = image?.Bitmap;
            previous?.Bitmap.Dispose();
            if (image is { Capture: { Projection: { } proof } capture })
            {
                _details.Text = Describe(capture) + "\nDocument revision " + proof.Document.DocumentRevision +
                    " · Apply " + proof.Apply + "\nActive scene camera " + proof.Camera;
                _lights.Text = DescribeLights(capture);
            }
            else
            {
                _details.Text = "Capture at the settled Storefront Terrace view.";
                _lights.Text = "";
            }
        }
    }

    public BistroReferenceWindow()
    {
        Width = 1090;
        Height = 600;
        MinWidth = 650;
        MinHeight = 440;
        ShowActivated = false;
        var source = new Button { Content = "Open official PNG" };
        source.Click += (_, _) => { if (_referenceView is { } view) OpenSource(view.ReferenceUrl); };
        var gallery = new Button { Content = "ORCA source" };
        gallery.Click += (_, _) => OpenSource(BistroReferenceView.Gallery);
        var local = new Button { Content = "Choose local PNG…" };
        local.Click += async (_, _) =>
        {
            if (_referenceView is not { } view) return;
            try
            {
                var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
                {
                    Title = "Select " + view.ReferenceFile,
                    FileTypeFilter = [new FilePickerFileType("PNG") { Patterns = ["*.png"] }],
                });
                if (_referenceView == view && files.FirstOrDefault()?.TryGetLocalPath() is { } path)
                    LoadReference(view, path);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException)
            {
                _referenceStatus.Text = "Reference unavailable: " + error.Message;
            }
        };
        var left = new StackPanel { Spacing = 8, Children = {
            EditorVisuals.Heading("Atlantis capture"), _atlantis, _atlantisDetails, _lighting,
        } };
        var right = new StackPanel { Spacing = 8, Children = {
            EditorVisuals.Heading("ORCA visual reference"), _orca, _referenceDetails,
            EditorVisuals.Actions(source, gallery, local), _referenceStatus,
        } };
        var columns = EditorVisuals.Columns("*,*", left, right);
        columns.ColumnSpacing = 14;
        columns.Margin = new Thickness(14);
        _referenceTab = new TabItem { Header = "Reference comparison", Content = new ScrollViewer { Content = columns } };
        var pair = EditorVisuals.Columns("*,*", BeforePane.Content, AfterPane.Content);
        pair.ColumnSpacing = 14;
        pair.Margin = new Thickness(14);
        _pairTab = new TabItem { Header = "Before / After", Content = new ScrollViewer { Content = pair }, IsVisible = false };
        _tabs.Items.Add(_referenceTab);
        _tabs.Items.Add(_pairTab);
        _tabs.SelectedItem = _referenceTab;
        Content = _tabs;
        Closed += (_, _) =>
        {
            ClearPair();
            Latest = null;
            CapturedPng = default;
            _atlantis.Source = _orca.Source = null;
            _captureBitmap?.Dispose();
            _referenceBitmap?.Dispose();
        };
    }

    private void OpenSource(string url)
    {
        try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); }
        catch (System.ComponentModel.Win32Exception) { _referenceStatus.Text = url; }
    }

    private void SelectReference(BistroReferenceView view)
    {
        if (_referenceView == view) return;
        _referenceView = view;
        _orca.Source = null;
        _referenceBitmap?.Dispose();
        _referenceBitmap = null;
        _referenceStatus.Text = "Choose the original " + view.ReferenceFile + " to show the reference.";
        _referenceDetails.Text = BistroReferenceView.Attribution + " · 1920 × 1061\n" +
            "Pose, field of view, exposure, direct and ambient lighting, and color pipeline: Unknown";
    }

    internal void ClearPair()
    {
        BeforePane.Set(null);
        AfterPane.Set(null);
        if (_tabs.SelectedItem == _pairTab) _tabs.SelectedItem = _referenceTab;
        _pairTab.IsVisible = false;
    }

    internal void SetPairCapture(BistroCapture capture, Bitmap bitmap, byte[] png, bool after)
    {
        SelectReference(capture.View);
        if (Latest is { } ordinary && ordinary.View != capture.View)
        {
            Latest = null;
            CapturedPng = default;
            _atlantis.Source = null;
            _captureBitmap?.Dispose();
            _captureBitmap = null;
            _atlantisDetails.Text = "";
            _lighting.Text = "";
        }
        if (!after) AfterPane.Set(null);
        (after ? AfterPane : BeforePane).Set(new(capture, bitmap, png));
        Title = capture.View.Name + " · Lighting and exposure";
        _pairTab.IsVisible = true;
        _tabs.SelectedItem = _pairTab;
    }

    internal void SetCapture(BistroCapture capture, Bitmap bitmap, byte[] png)
    {
        SelectReference(capture.View);
        Latest = capture;
        CapturedPng = png;
        Title = capture.View.Name + " · Reference comparison";
        _atlantis.Source = bitmap;
        _captureBitmap?.Dispose();
        _captureBitmap = bitmap;
        _atlantisDetails.Text = Describe(capture) + "\n" + BistroReferenceView.Attribution + " · NVIDIA RTXDI asset conversion";
        _lighting.Text = DescribeLights(capture);
        _tabs.SelectedItem = _referenceTab;
    }

    private static string Describe(BistroCapture capture)
    {
        string ev = capture.Report.Data.ExposureCompensationEv is float exposure
            ? exposure.ToString("G", CultureInfo.InvariantCulture) + " EV" : "Unavailable on this Runtime";
        return "960 × 540 · Frame " + capture.Report.Frame + " · Exposure " + ev +
            "\nRuntime scene " + capture.Status.Scene + " · Generation " + capture.Status.SceneGeneration +
            (capture.PreviousProjection ? " · Last visible projection; document changes await Apply" : "");
    }

    private static string DescribeLights(BistroCapture capture)
    {
        var lights = new StringBuilder("Reported direct lights\n");
        foreach (var light in capture.Report.Data.DirectionalLights)
            lights.AppendLine(FormattableString.Invariant($"Directional {light.Direction} · color {light.Color} · intensity {light.Intensity}"));
        foreach (var light in capture.Report.Data.PointLights)
            lights.AppendLine(FormattableString.Invariant($"Point {light.Position} · color {light.Color} · intensity {light.Intensity} · range {light.Range}"));
        return lights.ToString();
    }

    internal bool LoadReference(BistroReferenceView view, string path)
    {
        if (_referenceView != view) return false;
        byte[] bytes = ReadPng(path);
        if (!string.Equals(Convert.ToHexString(SHA256.HashData(bytes)), view.Sha256, StringComparison.OrdinalIgnoreCase))
        {
            _referenceStatus.Text = "Reference bytes do not match the official " + view.ReferenceFile + ".";
            return false;
        }
        using var source = new MemoryStream(bytes, writable: false);
        var bitmap = new Bitmap(source);
        _orca.Source = bitmap;
        _referenceBitmap?.Dispose();
        _referenceBitmap = bitmap;
        _referenceStatus.Text = view.ReferenceFile + " · Original bytes verified";
        return true;
    }

    internal static byte[] ReadPng(string path)
    {
        using var stream = File.OpenRead(path);
        if (stream.Length is 0 or > 5 * 1024 * 1024)
            throw new IOException("The PNG is empty or exceeds 5 MiB.");
        byte[] bytes = new byte[(int)stream.Length];
        stream.ReadExactly(bytes);
        return bytes;
    }
}
