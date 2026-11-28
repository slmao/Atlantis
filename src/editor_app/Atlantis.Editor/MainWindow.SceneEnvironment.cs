using System;
using System.Linq;
using System.Threading;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace Atlantis.Editor;

public sealed partial class MainWindow
{
    private Window? _sceneEnvironmentWindow;

    private void InitializeSceneEnvironment()
    {
        var item = new MenuItem { Header = "Scene Environment…" };
        item.Click += (_, _) => ShowSceneEnvironment();
        if (_bistroViews.Flyout is MenuFlyout menu) menu.Items.Add(item);
    }

    private void ShowSceneEnvironment()
    {
        if (_worker is not { } worker || !OperatingSystem.IsWindows()) return;
        if (BistroLocatorProtected())
        {
            ShowNavigationMessage("Finish the current draft or gesture before opening Scene Environment.");
            return;
        }
        if (_sceneEnvironmentWindow is { } active) { active.Activate(); return; }

        var authored = new TextBlock { TextWrapping = TextWrapping.Wrap };
        var effective = new TextBlock { TextWrapping = TextWrapping.Wrap };
        var choice = new ComboBox { MinWidth = 280 };
        var confirm = new Button { Content = "Set Environment", IsEnabled = false };
        var refresh = new Button { Content = "Refresh" };
        var notice = new TextBlock { TextWrapping = TextWrapping.Wrap };
        var panel = new StackPanel { Margin = new Thickness(16), Spacing = 10 };
        panel.Children.Add(authored);
        panel.Children.Add(effective);
        panel.Children.Add(choice);
        panel.Children.Add(EditorVisuals.Actions(confirm, refresh));
        panel.Children.Add(notice);
        var window = new Window { Title = "Scene Environment", Width = 600, MinWidth = 360, SizeToContent = SizeToContent.Height,
            WindowStartupLocation = WindowStartupLocation.CenterOwner, Content = panel };
        _sceneEnvironmentWindow = window;
        EditorWorker.SceneEnvironmentReview? review = null;
        AssetGuid?[] identities = [];
        long epoch = 0;
        bool waiting = false, stale = false;
        string lastAction = "";

        string Name(AssetGuid? guid)
        {
            if (guid is null) return "Inherit startup default";
            var asset = review?.Choices.FirstOrDefault(item => item.Guid == guid);
            string label = asset?.DisplayName ?? asset?.Source ?? "Unavailable catalog asset";
            if (label.EndsWith("/ibl_studio_source.hdr", StringComparison.Ordinal)) label = "Studio";
            if (label.EndsWith("/warehouse_interior_source.hdr", StringComparison.Ordinal)) label = "Warehouse Interior";
            return $"{label} ({guid})";
        }
        void Controls()
        {
            choice.IsEnabled = !stale && !waiting && review?.Document.Provenance == Provenance.HandAuthored;
            confirm.IsEnabled = choice.IsEnabled && choice.SelectedIndex >= 0 && choice.SelectedIndex < identities.Length &&
                review?.Document.Environment?.Asset != identities[choice.SelectedIndex];
            refresh.IsEnabled = !waiting;
        }
        void Read()
        {
            long request = Interlocked.Increment(ref epoch);
            waiting = true;
            stale = false;
            Controls();
            notice.Text = "Reading scene environment…";
            worker.RequestSceneEnvironment((result, message) =>
            {
                if (request != Interlocked.Read(ref epoch) || !ReferenceEquals(_worker, worker)) return;
                waiting = false;
                review = result;
                identities = result is null ? [] : new AssetGuid?[] { null }
                    .Concat(result.Choices.Select(asset => (AssetGuid?)asset.Guid)).ToArray();
                authored.Text = result is null ? "Document environment unavailable" :
                    "Document environment · " + Name(result.Document.Environment?.Asset);
                effective.Text = result is null ? "" : "Effective Runtime environment · " +
                    (result.Effective.Enabled ? result.Effective.Asset is { } id ? Name(id) : "Startup default" : "None");
                choice.ItemsSource = identities.Select(Name).ToArray();
                choice.SelectedIndex = Array.IndexOf(identities, result?.Document.Environment?.Asset);
                notice.Text = string.Join(" ", new[] { lastAction, message }.Where(value => value.Length > 0));
                Controls();
            });
        }
        void PersistenceChanged(DocumentObservation observation)
        {
            if (waiting || review is null || observation.State?.Token == review.Document.Token) return;
            stale = true;
            notice.Text = "The document changed. Refresh before choosing an environment.";
            Controls();
        }
        void ProjectionChanged(ApplyStatus status, string message)
        {
            if (status != ApplyStatus.Applied && status != ApplyStatus.Failed) return;
            lastAction = message;
            if (!waiting && !stale) Read();
        }
        worker.PersistenceChanged += PersistenceChanged;
        worker.ApplyStateChanged += ProjectionChanged;
        window.Closed += (_, _) =>
        {
            Interlocked.Increment(ref epoch);
            worker.PersistenceChanged -= PersistenceChanged;
            worker.ApplyStateChanged -= ProjectionChanged;
            _sceneEnvironmentWindow = null;
        };
        choice.SelectionChanged += (_, _) => Controls();
        refresh.Click += (_, _) => { lastAction = ""; Read(); };
        confirm.Click += (_, _) =>
        {
            if (!confirm.IsEnabled || review is not { } selected || BistroLocatorProtected()) return;
            long request = Interlocked.Increment(ref epoch);
            var next = new SceneEnvironmentChoice(identities[choice.SelectedIndex]);
            waiting = true;
            Controls();
            notice.Text = "Committing scene environment…";
            worker.ChooseSceneEnvironment(selected, next, () => request == Interlocked.Read(ref epoch), message =>
            {
                if (request != Interlocked.Read(ref epoch) || !ReferenceEquals(_worker, worker)) return;
                lastAction = message;
                Read();
            });
        };
        _ = window.ShowDialog(this);
        Read();
    }
}
