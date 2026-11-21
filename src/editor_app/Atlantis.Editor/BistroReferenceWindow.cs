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
    private readonly MenuItem _bistroExit = new() { Header = "Leave reference view" };
    private readonly MenuItem _bistroEnable = new() { Header = "Use reference views for this Bistro copy" };
    private readonly DispatcherTimer _bistroLayoutTimer = new() { Interval = TimeSpan.FromMilliseconds(40) };
    private BistroReferenceView? _bistroView;
    private BistroReferenceWindow? _bistroCompare;
    private long? _bistroOptIn;
    private long _bistroRevision;
    private long _bistroLayoutStarted;
    private string? _bistroUnknownPath;
    private bool _bistroBuiltIn, _bistroSent, _bistroReady, _bistroCapturing, _bistroResizeRecovery, _bistroApplyBlocked;

    private void InitializeBistroViews()
    {
        _bistroViews.Flyout = new MenuFlyout
        {
            Items = { _bistroEnable, _bistroStreet, _bistroTerrace, new Separator(), _bistroCapture, _bistroExit },
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
        _worker is { } worker && (_bistroBuiltIn || (_canAddMeshToDocument && _bistroOptIn == worker.MeshDocumentLifetime));

    private void UpdateBistroActions(bool navigation)
    {
        bool hosted = _options.AttachSession is null && _viewport.Window?.RuntimeChildIsHosted() == true;
        _bistroViews.IsEnabled = navigation && hosted && (BistroAllowed || _canAddMeshToDocument);
        _bistroEnable.IsVisible = !BistroAllowed && _canAddMeshToDocument;
        _bistroStreet.IsEnabled = _bistroTerrace.IsEnabled = BistroAllowed;
        _bistroExit.IsEnabled = _bistroView is not null;
        _bistroCapture.IsEnabled = _bistroReady && !_navigationPending && !_bistroCapturing &&
            !_bistroApplyBlocked && !_play.IsEnabled && navigation;
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

    private void CaptureBistroView()
    {
        if (!_bistroCapture.IsEnabled || _worker is not { } worker || _bistroView is not { } view) return;
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
                if (!current) return;
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
                if (_bistroCompare is null)
                {
                    var window = new BistroReferenceWindow();
                    window.Closed += (_, _) => _bistroCompare = null;
                    _bistroCompare = window;
                    window.SetCapture(capture, bitmap, png);
                    window.Show(this);
                }
                else _bistroCompare.SetCapture(capture, bitmap, png);
                ShowNavigationMessage(view.Name + " captured.");
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
        });
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

    public BistroReferenceWindow()
    {
        Width = 1090;
        Height = 600;
        MinWidth = 650;
        MinHeight = 440;
        ShowActivated = false;
        var source = new Button { Content = "Open official PNG" };
        source.Click += (_, _) => { if (Latest is { } capture) OpenSource(capture.View.ReferenceUrl); };
        var gallery = new Button { Content = "ORCA source" };
        gallery.Click += (_, _) => OpenSource(BistroReferenceView.Gallery);
        var local = new Button { Content = "Choose local PNG…" };
        local.Click += async (_, _) =>
        {
            if (Latest?.View is not { } view) return;
            try
            {
                var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
                {
                    Title = "Select " + view.ReferenceFile,
                    FileTypeFilter = [new FilePickerFileType("PNG") { Patterns = ["*.png"] }],
                });
                if (Latest?.View == view && files.FirstOrDefault()?.TryGetLocalPath() is { } path)
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
        Content = new ScrollViewer { Content = columns };
        Closed += (_, _) =>
        {
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

    internal void SetCapture(BistroCapture capture, Bitmap bitmap, byte[] png)
    {
        if (Latest?.View != capture.View)
        {
            _orca.Source = null;
            _referenceBitmap?.Dispose();
            _referenceBitmap = null;
            _referenceStatus.Text = "Choose the original " + capture.View.ReferenceFile + " to show the reference.";
        }
        Latest = capture;
        CapturedPng = png;
        Title = capture.View.Name + " · Reference comparison";
        _atlantis.Source = bitmap;
        _captureBitmap?.Dispose();
        _captureBitmap = bitmap;
        string ev = capture.Report.Data.ExposureCompensationEv is float exposure
            ? exposure.ToString("G", CultureInfo.InvariantCulture) + " EV" : "Unavailable on this Runtime";
        _atlantisDetails.Text = "960 × 540 · Frame " + capture.Report.Frame + " · Exposure " + ev +
            "\nRuntime scene " + capture.Status.Scene + " · Generation " + capture.Status.SceneGeneration +
            (capture.PreviousProjection ? " · Last visible projection; document changes await Apply" : "") +
            "\n" + BistroReferenceView.Attribution + " · NVIDIA RTXDI asset conversion";
        _referenceDetails.Text = BistroReferenceView.Attribution + " · 1920 × 1061\n" +
            "Pose, field of view, exposure, direct and ambient lighting, and color pipeline: Unknown";
        var lights = new StringBuilder("Reported direct lights\n");
        foreach (var light in capture.Report.Data.DirectionalLights)
            lights.AppendLine(FormattableString.Invariant($"Directional {light.Direction} · color {light.Color} · intensity {light.Intensity}"));
        foreach (var light in capture.Report.Data.PointLights)
            lights.AppendLine(FormattableString.Invariant($"Point {light.Position} · color {light.Color} · intensity {light.Intensity} · range {light.Range}"));
        _lighting.Text = lights.ToString();
    }

    internal bool LoadReference(BistroReferenceView view, string path)
    {
        if (Latest?.View != view) return false;
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
