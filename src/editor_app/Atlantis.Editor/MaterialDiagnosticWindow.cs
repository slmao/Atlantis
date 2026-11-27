using System;
using System.IO;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private Window? _materialDiagnosticWindow;
    private long _materialDiagnosticIntent;

    private void CloseMaterialDiagnostic()
    {
        ++_materialDiagnosticIntent;
        _materialDiagnosticWindow?.Close();
    }

    private void CompareMaterialDiagnostic(LocatedMaterialBinding binding, Button action)
    {
        if (!CanUseLocatedMaterial(binding) || _worker is not { } worker || _activeBookmark is not { } bookmark ||
            !_bistroReady || !BookmarkProjectionCurrent || _bookmarkProjection != binding.Request.SceneGeneration ||
            _documentState.State is not { } document || !BookmarkStillSaved(document, bookmark))
        {
            _editLine.Text = "Return to a settled bookmark, then use Locate Material again.";
            return;
        }

        CloseMaterialDiagnostic();
        long ticket = _materialDiagnosticIntent;
        long viewRevision = _bistroRevision;
        bool Current() => !_shuttingDown && ReferenceEquals(worker, _worker) &&
            ticket == _materialDiagnosticIntent && viewRevision == _bistroRevision && _activeBookmark == bookmark &&
            _documentState.State?.Token == document.Token &&
            CanUseLocatedMaterial(binding) && _bistroReady && BookmarkProjectionCurrent;

        string stem = Path.Combine(_workDirectory, "material-diagnostic-" + Guid.NewGuid().ToString("N"));
        string normalPath = stem + "-normal.png";
        string colorPath = stem + "-base-color.png";
        action.IsEnabled = false;
        _editLine.Text = "Capturing the settled material binding…";
        worker.CaptureMaterialDiagnostic(binding, bookmark.View, normalPath, colorPath, (pair, message) =>
        {
            Bitmap? normal = null;
            Bitmap? color = null;
            try
            {
                if (!Current() || !BookmarkStillSaved(document, bookmark))
                {
                    if (ticket == _materialDiagnosticIntent && !_shuttingDown)
                        _editLine.Text = "The binding or projection changed. Return to the bookmark and pick again.";
                    return;
                }
                if (pair is null) { _editLine.Text = message; return; }
                normal = BookmarkComparisonWindow.ReadImage(normalPath, bookmark.View.Extent, reference: false);
                color = BookmarkComparisonWindow.ReadImage(colorPath, bookmark.View.Extent, reference: false);
                if (!Current() || !BookmarkStillSaved(document, bookmark))
                {
                    _editLine.Text = "The diagnostic expired. Return to the bookmark and pick again.";
                    return;
                }
                var dialog = BuildMaterialDiagnostic(binding, bookmark.View, pair, normal, color);
                _materialDiagnosticWindow = dialog;
                dialog.Closed += (_, _) => _materialDiagnosticWindow = null;
                dialog.Show(this);
                normal = color = null;
                _editLine.Text = "Material diagnostic captured.";
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException)
            { _editLine.Text = "Material diagnostic unavailable. " + error.Message; }
            finally
            {
                normal?.Dispose();
                color?.Dispose();
                if (!message.Contains("outcome unknown", StringComparison.OrdinalIgnoreCase))
                {
                    DeleteBistroOutput(normalPath);
                    DeleteBistroOutput(colorPath);
                }
                action.IsEnabled = true;
            }
        });
    }

    private static Window BuildMaterialDiagnostic(LocatedMaterialBinding binding, ViewportObservation view,
        MaterialDiagnosticPair pair, Bitmap normal, Bitmap color)
    {
        var normalView = new Image { Source = normal, Stretch = Stretch.Uniform, Height = 350 };
        var colorView = new Image { Source = color, Stretch = Stretch.Uniform, Height = 350 };
        var columns = EditorVisuals.Columns("*,*",
            EditorVisuals.Stack(6, EditorVisuals.Heading("Normal shading"), normalView),
            EditorVisuals.Stack(6, EditorVisuals.Heading("Base color · isolated surface"),
                new Border { Background = Brushes.DimGray, Child = colorView }));
        columns.ColumnSpacing = 12;
        var identity = EditorVisuals.Secondary($"{binding.Name} · {MaterialBindingPick.Slot} · {pair.Target.Material}");
        identity.TextWrapping = TextWrapping.Wrap;
        var texture = EditorVisuals.Secondary($"Node {pair.Target.Entity} · Texture {pair.Texture}");
        texture.TextWrapping = TextWrapping.Wrap;
        var conditions = EditorVisuals.Secondary(FormattableString.Invariant(
            $"Linear RGB factor {pair.Factor.X:G4}, {pair.Factor.Y:G4}, {pair.Factor.Z:G4} · Projection {pair.Generation} · {view.Extent.Width} × {view.Extent.Height}"));
        var treatment = EditorVisuals.Secondary(
            $"Normal shading uses scene lights and camera effects, exposure {pair.Normal.Data.ExposureCompensationEv?.ToString("G") ?? "unknown"} EV. " +
            "Base color omits lighting and camera effects. Other objects do not occlude it.");
        treatment.TextWrapping = TextWrapping.Wrap;
        var summary = EditorVisuals.Stack(6, identity, texture, conditions, treatment, columns);
        summary.Margin = new Thickness(14);
        var window = new Window
        {
            Title = "Material Diagnostic", Width = 1100, Height = 540, MinWidth = 650,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Content = new ScrollViewer { Content = summary }
        };
        window.Closed += (_, _) =>
        {
            normalView.Source = colorView.Source = null;
            normal.Dispose();
            color.Dispose();
        };
        return window;
    }
}
