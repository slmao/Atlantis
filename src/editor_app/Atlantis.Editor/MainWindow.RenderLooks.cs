using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
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
    private Window? _renderLooksWindow;

    private void InitializeRenderLooks()
    {
        var entry = new MenuItem { Header = "Render Looks…" };
        entry.Click += (_, _) => ShowRenderLooks();
        if (_bistroViews.Flyout is MenuFlyout menu) menu.Items.Add(entry);
    }

    private void ShowRenderLooks()
    {
        if (!OperatingSystem.IsWindows() || _worker is not { } worker) return;
        if (BistroLocatorProtected())
        {
            ShowNavigationMessage("Finish the current draft or gesture before opening Render Looks.");
            return;
        }
        if (_renderLooksWindow is { } open)
        {
            open.Activate();
            return;
        }
        var name = new TextBox { PlaceholderText = "Look name", MinWidth = 160 };
        var save = new Button { Content = "Save Configuration" };
        var refresh = new Button { Content = "Refresh" };
        var a = new ComboBox { PlaceholderText = "Look A", MinWidth = 160 };
        var b = new ComboBox { PlaceholderText = "Look B", MinWidth = 160 };
        var views = new[] { BistroReferenceView.Street, BistroReferenceView.Terrace };
        var view = new ComboBox { ItemsSource = views.Select(v => v.Name), SelectedIndex = 0 };
        var compare = new Button { Content = "Compare" };
        var sideA = new Button { Content = "Show A" };
        var sideB = new Button { Content = "Show B" };
        var adopt = new Button { Content = "Adopt shown look" };
        var cancel = new Button { Content = "Cancel comparison" };
        var notice = new TextBlock { TextWrapping = TextWrapping.Wrap };
        var image = new Image { Stretch = Stretch.Uniform };
        var layout = new DockPanel { Margin = new Thickness(12), LastChildFill = true };
        void Add(Control control)
        {
            DockPanel.SetDock(control, Dock.Top);
            layout.Children.Add(control);
        }
        Add(EditorVisuals.Actions(name, save, refresh));
        Add(EditorVisuals.Actions(a, b, view, compare));
        Add(EditorVisuals.Actions(sideA, sideB, adopt, cancel));
        Add(notice);
        layout.Children.Add(image);
        var window = new Window { Title = "Render Looks", Width = 1020, Height = 740,
            MinWidth = 560, MinHeight = 360, WindowStartupLocation = WindowStartupLocation.CenterOwner, Content = layout };
        _renderLooksWindow = window;

        EditorWorker.RenderLookReview? review = null;
        RenderLookStore? store = null;
        RenderLookLibrary? library = null;
        Bitmap? pngA = null, pngB = null;
        RenderLookPreset? shown = null;
        string comparisonContext = "";
        long epoch = 0;
        bool closed = false, waiting = false, changed = false;
        RenderLookPreset? Selected(ComboBox box) => library is not null && box.SelectedIndex >= 0 &&
            box.SelectedIndex < library.Presets.Count ? library.Presets[box.SelectedIndex] : null;
        void Controls()
        {
            save.IsEnabled = review is not null && store is not null && !waiting && !changed;
            compare.IsEnabled = review is not null && !waiting && !changed &&
                Selected(a) is { } left && Selected(b) is { } right && left != right;
            sideA.IsEnabled = pngA is not null;
            sideB.IsEnabled = pngB is not null;
            adopt.IsEnabled = shown is not null && !waiting && !changed;
        }
        void Retire()
        {
            Interlocked.Increment(ref epoch);
            shown = null;
            comparisonContext = "";
            image.Source = null;
            pngA?.Dispose();
            pngB?.Dispose();
            pngA = pngB = null;
            Controls();
        }
        void MarkChanged(DocumentObservation observation)
        {
            if (review is null || observation.State?.Token == review.Document.Token) return;
            changed = true;
            Retire();
            notice.Text = "The document changed. Refresh before saving, comparing, or adopting.";
        }
        void ProjectionChanged(ApplyStatus status, string _)
        {
            if (status != ApplyStatus.Applied || (pngA is null && pngB is null && !waiting)) return;
            Retire();
            notice.Text = "The Runtime projection changed. Capture a fresh comparison.";
        }
        void Refresh()
        {
            Retire();
            review = null;
            changed = false;
            long ticket = Interlocked.Read(ref epoch);
            notice.Text = "Reading the scene's camera and lights…";
            Controls();
            worker.RequestRenderLook((ready, message) =>
            {
                if (closed || ticket != Interlocked.Read(ref epoch) || !ReferenceEquals(worker, _worker)) return;
                try
                {
                    review = ready;
                    store = review is null ? null : new RenderLookStore(review.Document);
                    library = store?.Read();
                    var names = library?.Presets.Select(p => p.Name).ToArray() ?? [];
                    a.ItemsSource = names;
                    b.ItemsSource = names;
                    a.SelectedIndex = names.Length > 0 ? 0 : -1;
                    b.SelectedIndex = names.Length > 1 ? 1 : -1;
                    notice.Text = review is null ? message : "Save current committed values or compare two saved looks.";
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException or FormatException)
                {
                    store = null;
                    library = null;
                    notice.Text = e.Message;
                }
                Controls();
            });
        }
        worker.PersistenceChanged += MarkChanged;
        worker.ApplyStateChanged += ProjectionChanged;
        window.Closed += (_, _) =>
        {
            closed = true;
            worker.PersistenceChanged -= MarkChanged;
            worker.ApplyStateChanged -= ProjectionChanged;
            Retire();
            _renderLooksWindow = null;
        };
        refresh.Click += (_, _) => Refresh();
        a.SelectionChanged += (_, _) => Retire();
        b.SelectionChanged += (_, _) => Retire();
        view.SelectionChanged += (_, _) => Retire();
        cancel.Click += (_, _) => { Retire(); notice.Text = "Comparison canceled."; };
        save.Click += async (_, _) =>
        {
            if (review is not { } current || store is not { } destination || library is not { } reviewed || changed) return;
            string label = name.Text?.Trim() ?? "";
            bool replace = reviewed.Presets.Any(p => string.Equals(p.Name, label, StringComparison.OrdinalIgnoreCase));
            if (replace && !await ConfirmRenderLook(window, "Replace " + label + "?", "Replace")) return;
            if (closed || changed || review != current) return;
            waiting = true;
            Controls();
            worker.RequestRenderLook((fresh, failure) =>
            {
                waiting = false;
                if (!closed && !changed && review == current)
                {
                    try
                    {
                        if (fresh is null || fresh.Document.Token != current.Document.Token)
                            throw new IOException(string.IsNullOrEmpty(failure) ? "The scene changed. Refresh before saving." : failure);
                        destination.Write(reviewed.Revision, fresh.Snapshot.Preset(label), replace);
                        Refresh();
                    }
                    catch (Exception e) when (e is IOException or UnauthorizedAccessException or FormatException) { notice.Text = e.Message; }
                }
                Controls();
            });
        };
        compare.Click += (_, _) =>
        {
            if (waiting || changed || review is not { } current || Selected(a) is not { } left ||
                Selected(b) is not { } right || view.SelectedIndex < 0) return;
            Retire();
            long ticket = Interlocked.Read(ref epoch);
            var fixedView = views[view.SelectedIndex];
            string directory;
            try { directory = System.IO.Directory.CreateTempSubdirectory("atlantis-render-looks-").FullName; }
            catch (IOException e) { notice.Text = e.Message; return; }
            var fileA = Path.Combine(directory, "a.png");
            var fileB = Path.Combine(directory, "b.png");
            waiting = true;
            notice.Text = "Capturing " + fixedView.Name + "…";
            Controls();
            worker.CompareRenderLooks(current, left, right, fixedView, fileA, fileB, (pair, message) =>
            {
                waiting = false;
                try
                {
                    if (closed || changed || ticket != Interlocked.Read(ref epoch) || !ReferenceEquals(worker, _worker)) return;
                    if (pair is null) { notice.Text = message; return; }
                    pngA = Load(fileA);
                    pngB = Load(fileB);
                    comparisonContext = $" · {fixedView.Name} · 960 × 540 · projection {pair.Generation} · Apply {pair.Apply}";
                    Display(pngA, left);
                }
                catch (Exception e) when (e is IOException or ArgumentException or UnauthorizedAccessException)
                {
                    Retire();
                    notice.Text = "Comparison image unavailable. " + e.Message;
                }
                finally
                {
                    try
                    {
                        File.Delete(fileA);
                        File.Delete(fileB);
                        System.IO.Directory.Delete(directory);
                    }
                    catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
                    Controls();
                }
            });
        };
        sideA.Click += (_, _) => Display(pngA, Selected(a));
        sideB.Click += (_, _) => Display(pngB, Selected(b));
        adopt.Click += async (_, _) =>
        {
            if (changed || review is not { } current || shown is not { } preset || GroupEditRefusal() is not null) return;
            var prior = current.Snapshot.Targets.ToDictionary(t => t.Address, t => t.Value);
            var changes = preset.Values.Where(p => !prior.TryGetValue(p.Address, out var old) || old != p.Value).ToArray();
            if (changes.Length == 0) { notice.Text = "The look already matches the scene."; return; }
            string diff = string.Join("\n", changes.Select(p => p.Address.Node + " · " + p.Address.Field + "\n  " +
                (prior.TryGetValue(p.Address, out var old) ? old : null) + " → " + p.Value));
            long ticket = Interlocked.Read(ref epoch);
            if (!await ConfirmRenderLook(window, "Adopt " + preset.Name + "\n\n" + diff, "Adopt") ||
                closed || changed || ticket != Interlocked.Read(ref epoch) || GroupEditRefusal() is not null) return;
            waiting = true;
            Controls();
            worker.AdoptRenderLook(current, preset, () => Interlocked.Read(ref epoch) == ticket, message =>
            {
                waiting = false;
                if (!closed) { notice.Text = message; Controls(); }
            });
        };
        window.Show(this);
        Refresh();

        void Display(Bitmap? bitmap, RenderLookPreset? preset)
        {
            if (bitmap is null || preset is null) return;
            image.Source = bitmap;
            shown = preset;
            notice.Text = preset.Name + comparisonContext;
            Controls();
        }
        static Bitmap Load(string path)
        {
            using var stream = new MemoryStream(BistroReferenceWindow.ReadPng(path), writable: false);
            var bitmap = new Bitmap(stream);
            if (bitmap.PixelSize == new PixelSize(960, 540)) return bitmap;
            bitmap.Dispose();
            throw new IOException("Unexpected comparison dimensions.");
        }
    }

    private static Task<bool> ConfirmRenderLook(Window owner, string message, string verb)
    {
        var confirm = new Button { Content = verb };
        var cancel = new Button { Content = "Cancel", IsCancel = true };
        var window = new Window { Title = verb, Width = 670, Height = 440,
            WindowStartupLocation = WindowStartupLocation.CenterOwner };
        var layout = new DockPanel { Margin = new Thickness(12) };
        var actions = EditorVisuals.Actions(confirm, cancel);
        DockPanel.SetDock(actions, Dock.Bottom);
        layout.Children.Add(actions);
        layout.Children.Add(new ScrollViewer { Content = new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap } });
        window.Content = layout;
        confirm.Click += (_, _) => window.Close(true);
        cancel.Click += (_, _) => window.Close(false);
        return window.ShowDialog<bool>(owner);
    }
}
