using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Remote;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace Atlantis.Editor;

internal sealed class MaterialEditorWindow : Window
{
    private readonly MaterialDraft _draft;
    private readonly TextBox _name = new() { PlaceholderText = "e.g. brushed_metal" };
    private readonly ComboBox _texture = AssetChoice("Base texture"), _mesh = AssetChoice("Preview mesh");
    private readonly TextBox[] _values = new TextBox[5];
    private readonly Button _save = new() { Content = "Save & Publish" };
    private readonly Button _preview = new() { Content = "Preview draft" };
    private readonly Button _resume = new() { Content = "Resume publication", IsVisible = false };
    private readonly SelectableTextBlock _state = EditorVisuals.Details("");
    private readonly TextBlock _previewState = EditorVisuals.Secondary("");
    private readonly Image _image = new() { Height = 258, Stretch = Stretch.Uniform, IsVisible = false };
    private readonly StackPanel _form;
    private bool _saveBlocked;
    private readonly string _assetName;
    internal long Surface { get; }
    internal event Action? DraftChanged;
    internal event Action? PreviewRequested;
    internal event Action<MaterialSave>? SaveRequested;
    internal event Action? ResumeRequested;

    internal MaterialEditorWindow(long surface, MaterialDraft draft, string name, IReadOnlyList<AssetSummary> catalog)
    {
        Surface = surface; _draft = draft; _assetName = name;
        Title = draft.Mode switch { MaterialMode.Create => "New material", MaterialMode.Copy => "Duplicate material", _ => "Edit material" };
        Width = 760; Height = 610; MinWidth = 410; MinHeight = 420;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        _save.Classes.Add("editor-primary");
        AutomationProperties.SetName(_name, "Material name");
        _name.IsReadOnly = draft.Mode == MaterialMode.Edit;
        _name.Text = draft.Mode == MaterialMode.Create ? "" : _name.IsReadOnly ? name : "";
        _texture.ItemsSource = catalog.Where(x => x.Type == AssetType.Texture).ToArray();
        _mesh.ItemsSource = catalog.Where(x => x.Type == AssetType.Mesh).ToArray();
        _texture.SelectedItem = ((IEnumerable<AssetSummary>)_texture.ItemsSource).FirstOrDefault(x => x.Guid == draft.Initial.Texture);
        _texture.SelectionChanged += (_, _) => Changed();
        _mesh.SelectionChanged += (_, _) => Changed();
        string[] labels = ["Red", "Green", "Blue", "Metallic", "Roughness"];
        float[] initial = [draft.Initial.Red, draft.Initial.Green, draft.Initial.Blue, draft.Initial.Metallic, draft.Initial.Roughness];
        var inputs = new Control[5];
        for (int i = 0; i < _values.Length; ++i)
        {
            var input = new TextBox { Text = initial[i].ToString("G", CultureInfo.InvariantCulture) };
            AutomationProperties.SetName(input, labels[i]);
            input.TextChanged += (_, _) => Changed();
            _values[i] = input;
            inputs[i] = EditorVisuals.Stack(3, EditorVisuals.Secondary(labels[i]), input);
            inputs[i].Margin = new Thickness(0, 0, i is 2 or 4 ? 0 : 6, 0);
        }
        _name.TextChanged += (_, _) => Refresh();
        _save.Click += (_, _) => { if (TryValues(out var values)) SaveRequested?.Invoke(_draft.Save(_name.Text ?? "", values)); };
        _preview.Click += (_, _) => PreviewRequested?.Invoke();
        _resume.Click += (_, _) => ResumeRequested?.Invoke();
        var close = new Button { Content = "Close" };
        close.Click += (_, _) => Close();
        _form = EditorVisuals.Stack(10,
            EditorVisuals.Group(EditorVisuals.Heading("Material"), EditorVisuals.Stack(5,
                EditorVisuals.Secondary("Name"), _name, EditorVisuals.Secondary("Base texture"), _texture)),
            EditorVisuals.Group(EditorVisuals.Heading("Surface"), EditorVisuals.Stack(6,
                EditorVisuals.Secondary("Linear base color"), EditorVisuals.Columns("*,*,*", inputs[0], inputs[1], inputs[2]),
                EditorVisuals.Columns("*,*", inputs[3], inputs[4]), EditorVisuals.Secondary("Factors accept values from 0 to 1."))),
            EditorVisuals.Disclosure("Details", EditorVisuals.Details(
                (draft.Original is { } origin ? origin.Material + "\n" : "") +
                $"{draft.Recipe(draft.Initial).Filter} · {draft.Recipe(draft.Initial).Address}")));
        var preview = EditorVisuals.Group(EditorVisuals.Heading("Draft preview"), EditorVisuals.Stack(6,
            _mesh, _preview, _image, _previewState, EditorVisuals.Secondary("Fixed lighting. The scene is unchanged.")));
        var content = new Grid { Margin = new Thickness(12), ColumnSpacing = 10, RowSpacing = 10 };
        content.Children.Add(_form); content.Children.Add(preview);
        void Arrange(bool compact)
        {
            content.ColumnDefinitions = new ColumnDefinitions(compact ? "*" : "*,*");
            content.RowDefinitions = new RowDefinitions(compact ? "Auto,Auto" : "Auto");
            Grid.SetColumn(preview, compact ? 0 : 1); Grid.SetRow(preview, compact ? 1 : 0);
        }
        Arrange(false);
        SizeChanged += (_, args) => Arrange(args.NewSize.Width < 640);
        var footer = EditorVisuals.Bar(EditorVisuals.Stack(5, _state, EditorVisuals.Actions(_save, _resume, close)));
        var layout = new DockPanel();
        DockPanel.SetDock(footer, Dock.Bottom); layout.Children.Add(footer); layout.Children.Add(EditorVisuals.Scroll(content));
        Content = layout;
        Opened += (_, _) => { if (_name.IsReadOnly) _texture.Focus(); else _name.Focus(); };
        Closed += (_, _) => ClearPreview();
        Refresh();
    }

    private static ComboBox AssetChoice(string label)
    {
        var box = new ComboBox { PlaceholderText = "Select " + label.ToLowerInvariant(), HorizontalAlignment = HorizontalAlignment.Stretch,
            ItemTemplate = new FuncDataTemplate<AssetSummary>((item, _) => item is null ? new TextBlock() : EditorVisuals.AssetRow(item)) };
        AutomationProperties.SetName(box, label);
        return box;
    }

    internal AssetPreviewInputs? Inputs => _mesh.SelectedItem is AssetSummary mesh && TryValues(out var values)
        ? new(mesh.Guid, null, Surface, _draft.Recipe(values)) : null;

    private bool TryValues(out MaterialValues values)
    {
        values = default;
        if (_texture.SelectedItem is not AssetSummary texture) return false;
        Span<float> numbers = stackalloc float[5];
        for (int i = 0; i < numbers.Length; ++i)
            if (!float.TryParse(_values[i].Text, NumberStyles.Float, CultureInfo.InvariantCulture, out numbers[i]) ||
                !float.IsFinite(numbers[i]) || numbers[i] < 0 || numbers[i] > 1) return false;
        values = new(texture.Guid, numbers[0], numbers[1], numbers[2], numbers[3], numbers[4]);
        return true;
    }

    private void Changed()
    {
        if (_values.Any(x => x is null)) return;
        Refresh(); DraftChanged?.Invoke();
    }

    private void Refresh()
    {
        _save.IsEnabled = !_saveBlocked && TryValues(out var values) && (_name.IsReadOnly
            ? values != _draft.Initial : Codec.ValidImportName(_name.Text));
        _preview.IsEnabled = Inputs is not null;
    }

    internal void ShowSettlement(string text, bool blocked, bool resume, bool error)
    {
        _saveBlocked = blocked;
        _state.Text = text;
        _state.Foreground = error ? EditorVisuals.Error : EditorVisuals.Muted;
        _resume.IsVisible = resume;
        Refresh();
    }

    internal void SetPreviewEnabled(bool enabled) => _preview.IsEnabled = enabled && Inputs is not null;
    internal void ClearPreview()
    {
        var previous = _image.Source as Bitmap;
        _image.Source = null; _image.IsVisible = false; previous?.Dispose();
        _previewState.Text = "";
    }

    internal void ShowPreview(byte[]? png, string? message)
    {
        ClearPreview();
        if (png is null) { _previewState.Text = message; return; }
        try
        {
            using var stream = new MemoryStream(png, writable: false);
            _image.Source = new Bitmap(stream);
            _image.IsVisible = true;
            _previewState.Text = "Draft material - fixed lighting";
        }
        catch (Exception error) { _previewState.Text = "Could not decode the preview: " + error.Message; }
    }
}
