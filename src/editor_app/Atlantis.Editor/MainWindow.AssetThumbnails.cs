using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Remote;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private readonly AssetThumbnailCache<Bitmap> _thumbnailCache = new();
    private readonly HashSet<EditorVisuals.AssetThumbnailSlot> _shownThumbnails = new();
    private readonly DispatcherTimer _thumbnailWake = new(DispatcherPriority.Loaded);
    private bool _thumbnailObservationPending;

    private void ObserveAssetThumbnails()
    {
        _thumbnailWake.Tick += (_, _) =>
        {
            _thumbnailWake.Stop();
            RefreshThumbnailDemand();
            StartNextPreview();
        };
        _assetsList.AddHandler(ScrollViewer.ScrollChangedEvent, (_, _) => QueueThumbnailObservation());
        _assetsList.SizeChanged += (_, _) => QueueThumbnailObservation();
        _assetsList.ContainerPrepared += (_, _) => QueueThumbnailObservation();
        _assetsList.ContainerClearing += (_, e) =>
        {
            foreach (var slot in e.Container.GetVisualDescendants().OfType<EditorVisuals.AssetThumbnailSlot>())
                if (_shownThumbnails.Remove(slot)) slot.Show(null, null);
            QueueThumbnailObservation();
        };
    }

    private bool ThumbnailSurfaceVisible => _previewSession is not null && _assetTabs.SelectedIndex == 1 &&
        !_projectVisible && !_referenceSessionFailed && _assetsList.IsVisible && _assetBrowser.State == "Ready";

    private void UpdateThumbnailView()
    {
        if (!ThumbnailSurfaceVisible || _assetDragSourceRunning || _referenceWindow is not null || _materialWindow is not null)
            RetireThumbnailDemand();
        QueueThumbnailObservation();
    }

    private void RetireThumbnailDemand()
    {
        _thumbnailWake.Stop();
        _previewRequests.InvalidateThumbnails(Environment.TickCount64);
    }

    private void ResetThumbnails()
    {
        _thumbnailWake.Stop();
        _previewRequests.InvalidateThumbnails(Environment.TickCount64, catalog: true);
        foreach (var slot in _shownThumbnails) slot.Show(null, null);
        _shownThumbnails.Clear();
        _thumbnailCache.Clear();
    }

    private void QueueThumbnailObservation()
    {
        if (_thumbnailObservationPending) return;
        _thumbnailObservationPending = true;
        _previewRequests.SetThumbnailLayoutPending(true);
        Dispatcher.UIThread.Post(() =>
        {
            RefreshThumbnailDemand();
            _thumbnailObservationPending = false;
            _previewRequests.SetThumbnailLayoutPending(false);
            StartNextPreview();
        }, DispatcherPriority.Loaded);
    }

    private void RefreshThumbnailDemand()
    {
        var shown = new Dictionary<AssetGuid, EditorVisuals.AssetThumbnailSlot>();
        if (ThumbnailSurfaceVisible)
        {
            foreach (var (row, asset) in EditorVisuals.VisibleAssetRows(_assetsList))
                if (asset.Type == AssetType.Mesh &&
                    row.GetVisualDescendants().OfType<EditorVisuals.AssetThumbnailSlot>().FirstOrDefault() is { } slot &&
                    slot.Guid == asset.Guid)
                    shown.TryAdd(asset.Guid, slot);
        }

        var currentSlots = shown.Values.ToHashSet();
        foreach (var slot in _shownThumbnails)
            if (!currentSlots.Contains(slot)) slot.Show(null, null);
        _shownThumbnails.Clear();
        _shownThumbnails.UnionWith(currentSlots);
        long now = Environment.TickCount64;
        var visible = shown.Keys.ToArray();
        _thumbnailCache.SetVisible(visible, now);

        var missing = new List<AssetGuid>();
        foreach (var (guid, slot) in shown)
        {
            if (!_thumbnailCache.TryGet(guid, out var image, out var failure)) missing.Add(guid);
            slot.Show(image, failure);
        }

        bool blocked = !ThumbnailSurfaceVisible || _assetDragSourceRunning || _referenceWindow is not null ||
            _materialWindow is not null || _thumbnailCache.Suspended is not null || !_thumbnailCache.CanInsert;
        _previewRequests.SetThumbnailDemand(visible, missing, blocked, now);
    }

    private void ScheduleThumbnail()
    {
        _thumbnailWake.Stop();
        if (_thumbnailObservationPending || _previewSession is not { } session) return;
        long now = Environment.TickCount64;
        if (_previewRequests.ThumbnailDelay(now) is not long delay) return;
        if (delay > 0)
        {
            _thumbnailWake.Interval = TimeSpan.FromMilliseconds(delay);
            _thumbnailWake.Start();
        }
        else if (_previewRequests.TakeThumbnail(now) is { } intent)
            RunThumbnail(session, intent);
    }

    private async void RunThumbnail(SessionInfo session, AssetThumbnailIntent intent)
    {
        try
        {
            var reply = await Task.Run(() => ReadThumbnail(session, intent));
            RefreshThumbnailDemand();
            if (!ReferenceEquals(session, _previewSession) || !_previewRequests.IsThumbnailCurrent(intent)) return;
            if (reply.Png is { } png)
            {
                Bitmap? image = null;
                try
                {
                    using var stream = new MemoryStream(png, writable: false);
                    image = Bitmap.DecodeToWidth(stream, AssetThumbnailCache<Bitmap>.Side, BitmapInterpolationMode.HighQuality);
                    if (image.PixelSize.Width != AssetThumbnailCache<Bitmap>.Side ||
                        image.PixelSize.Height != AssetThumbnailCache<Bitmap>.Side)
                        throw new InvalidDataException("Expected a square thumbnail.");
                    if (_thumbnailCache.Store(intent.Guid, image, null, null)) image = null;
                }
                catch (Exception error)
                {
                    _log.Write("assets thumbnail decode " + intent.Guid + ": " + error.Message);
                    StoreThumbnailFailure(intent.Guid, "Thumbnail could not be decoded.", retry: true);
                }
                finally { image?.Dispose(); }
            }
            else if (reply.Error is { } error)
            {
                const string Prefix = "Thumbnail unavailable: ";
                if (error is PreviewError.Unsupported or PreviewError.CatalogUnavailable)
                    _thumbnailCache.Suspend(error == PreviewError.Unsupported
                        ? "Thumbnails require an updated Runtime." : "Thumbnails unavailable because the catalog is unavailable.");
                else
                    StoreThumbnailFailure(intent.Guid, Prefix + error, retry: error is
                        PreviewError.Busy or PreviewError.Unavailable or PreviewError.RenderFailed);
            }
        }
        finally
        {
            _previewRequests.SettleThumbnail(intent);
            RefreshThumbnailDemand();
            StartNextPreview();
        }
    }

    private void StoreThumbnailFailure(AssetGuid guid, string message, bool retry) =>
        _thumbnailCache.Store(guid, null, message, retry ? Environment.TickCount64 + 2000 : null);

    private (byte[]? Png, PreviewError? Error) ReadThumbnail(SessionInfo session, AssetThumbnailIntent intent)
    {
        try
        {
            if (!_previewRequests.CanSetUpThumbnail(intent)) return default;
            using var remote = RemoteSession.Connect(session);
            long now = Environment.TickCount64;
            if (!_previewRequests.AdmitThumbnail(intent, now)) return default;
            try
            {
                var reply = remote.Assets.PreviewMesh(intent.Guid);
                return reply.IsOk ? (reply.Value.PngBytes, null) : (null, reply.Error);
            }
            finally { _log.Write($"assets thumbnail send {intent.Guid} at {now}"); }
        }
        catch (RemoteException error)
        {
            _log.Write("assets thumbnail connection " + intent.Guid + ": " + error.Message);
            return (null, PreviewError.RenderFailed);
        }
    }
}
