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
using Avalonia.Input.Platform;

namespace Atlantis.Editor;

internal sealed class MaterialEditorWindow : Window
{
    private readonly MaterialDraft _draft;
    private readonly TextBox _name = new() { PlaceholderText = "e.g. brushed_metal" };
    private readonly ComboBox _texture = AssetChoice("Base texture"), _mesh = AssetChoice("Preview mesh");
    private readonly ComboBox _normal = AssetChoice("Normal map"), _emissive = AssetChoice("Emissive texture");
    private readonly ComboBox _packed = AssetChoice("Metallic-roughness map");
    private readonly Button _clearPacked = new() { Content = "Clear" };
    private readonly Control _packedRow, _occlusionRow;
    private readonly ComboBox _occlusion = AssetChoice("Occlusion map");
    private readonly Button _clearOcclusion = new() { Content = "Clear" };
    private readonly TextBlock _occlusionHelp = EditorVisuals.Secondary("");
    private readonly TextBlock _lighting = EditorVisuals.Secondary("");
    private readonly TextBox[] _values = new TextBox[8];
    private readonly Button _clearNormal = new() { Content = "Clear" }, _clearEmissive = new() { Content = "Clear" };
    private readonly Button _basic = new() { Content = "Use basic editing", IsVisible = false };
    private readonly TextBlock _emissiveHint = EditorVisuals.Secondary("");
    private readonly Control _maps;
    private readonly Button _save = new() { Content = "Save & Publish" };
    private readonly Button _preview = new() { Content = "Preview draft" };
    private readonly Button _resume = new() { Content = "Resume publication", IsVisible = false };
    private readonly TextBox _bindingReview = new() { IsReadOnly = true };
    private readonly Button _copyBindingReview = new() { Content = "Copy binding review note" };
    private readonly StackPanel _bindingReviewActions = new() { IsVisible = false, Spacing = 4 };
    private readonly SelectableTextBlock _state = EditorVisuals.Details("");
    private readonly TextBlock _previewState = EditorVisuals.Secondary("");
    private readonly Image _image = new() { Height = 258, Stretch = Stretch.Uniform, IsVisible = false };
    private readonly StackPanel _form;
    private bool _saveBlocked;
    private readonly string _assetName;
    internal long Surface { get; }
    internal void SelectPreviewMesh(AssetGuid mesh) =>
        _mesh.SelectedItem = _mesh.Items.OfType<AssetSummary>().FirstOrDefault(asset => asset.Guid == mesh);

    internal void ShowBindingReview(string review)
    {
        _bindingReview.Text = review;
        _bindingReviewActions.IsVisible = true;
    }
    internal bool WithMaps { get; private set; }
    internal bool WithMetallicRoughness { get; private set; }
    internal bool WithOcclusion { get; private set; }
    internal bool WithCombinedMaps { get; private set; }
    internal event Action? DraftChanged;
    internal event Action? PreviewRequested;
    internal event Action<MaterialSave>? SaveRequested;
    internal event Action? ResumeRequested;

    internal MaterialEditorWindow(long surface, MaterialDraft draft, string name, IReadOnlyList<AssetSummary> catalog, bool withMaps = true,
        bool withMetallicRoughness = false, bool withOcclusion = false, bool withCombinedMaps = false,
        string? bindingScope = null)
    {
        WithMaps = withMaps;
        WithMetallicRoughness = withMetallicRoughness;
        WithOcclusion = withOcclusion;
        WithCombinedMaps = withCombinedMaps;
        Surface = surface; _draft = draft; _assetName = name;
        Title = draft.Mode switch { MaterialMode.Create => "New material", MaterialMode.Copy => "Duplicate material", _ => "Edit material" };
        Width = 760; Height = 610; MinWidth = 410; MinHeight = 420;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        _save.Classes.Add("editor-primary");
        AutomationProperties.SetName(_name, "Material name");
        _name.IsReadOnly = draft.Mode == MaterialMode.Edit;
        _name.Text = draft.Mode == MaterialMode.Create ? "" : _name.IsReadOnly ? name : "";
        static bool IsPacked(AssetSummary asset) => EditorVisuals.ManagedTextureName(asset, "/metallic-roughness.png") is not null;
        static bool IsOcclusion(AssetSummary asset) => EditorVisuals.ManagedTextureName(asset, "/occlusion.png") is not null;
        var textures = catalog.Where(x => x.Type == AssetType.Texture && !IsPacked(x) && !IsOcclusion(x)).ToArray();
        var packed = catalog.Where(IsPacked).ToArray();
        var occlusion = catalog.Where(IsOcclusion).ToArray();
        _occlusion.ItemsSource = occlusion;
        _occlusion.SelectedItem = occlusion.FirstOrDefault(x => x.Guid == draft.Initial.OcclusionMap);
        _occlusion.SelectionChanged += (_, _) => Changed();
        AutomationProperties.SetName(_clearOcclusion, "Clear occlusion map");
        _clearOcclusion.Click += (_, _) => { _occlusion.SelectedItem = null; _occlusion.Focus(); };
        _texture.ItemsSource = _normal.ItemsSource = _emissive.ItemsSource = textures;
        _packed.ItemsSource = packed;
        _packed.SelectedItem = packed.FirstOrDefault(x => x.Guid == draft.Initial.MetallicRoughnessMap);
        _packed.SelectionChanged += (_, _) => Changed();
        AutomationProperties.SetName(_clearPacked, "Clear metallic-roughness map");
        _clearPacked.Click += (_, _) => { _packed.SelectedItem = null; _packed.Focus(); };
        _mesh.ItemsSource = catalog.Where(x => x.Type == AssetType.Mesh).ToArray();
        _texture.SelectedItem = ((IEnumerable<AssetSummary>)_texture.ItemsSource).FirstOrDefault(x => x.Guid == draft.Initial.Texture);
        _normal.SelectedItem = textures.FirstOrDefault(x => x.Guid == draft.Initial.NormalMap);
        _emissive.SelectedItem = textures.FirstOrDefault(x => x.Guid == draft.Initial.EmissiveTexture);
        _normal.SelectionChanged += (_, _) => Changed();
        _emissive.SelectionChanged += (_, _) => Changed();
        AutomationProperties.SetName(_clearNormal, "Clear normal map");
        AutomationProperties.SetName(_clearEmissive, "Clear emissive texture");
        _clearNormal.Click += (_, _) => { _normal.SelectedItem = null; _normal.Focus(); };
        _clearEmissive.Click += (_, _) => { _emissive.SelectedItem = null; _emissive.Focus(); };
        _texture.SelectionChanged += (_, _) => Changed();
        _mesh.SelectionChanged += (_, _) => Changed();
        string[] labels = ["Red", "Green", "Blue", "Metallic", "Roughness",
            "Emissive red", "Emissive green", "Emissive blue"];
        float[] initial = [draft.Initial.Red, draft.Initial.Green, draft.Initial.Blue, draft.Initial.Metallic, draft.Initial.Roughness,
            draft.Initial.EmissiveRed, draft.Initial.EmissiveGreen, draft.Initial.EmissiveBlue];
        var inputs = new Control[_values.Length];
        for (int i = 0; i < _values.Length; ++i)
        {
            var input = new TextBox { Text = initial[i].ToString("G", CultureInfo.InvariantCulture) };
            AutomationProperties.SetName(input, labels[i]);
            input.TextChanged += (_, _) => Changed();
            _values[i] = input;
            inputs[i] = EditorVisuals.Stack(3, EditorVisuals.Secondary(labels[i]), input);
            inputs[i].Margin = new Thickness(0, 0, i is 2 or 4 or 7 ? 0 : 6, 0);
        }
        _name.TextChanged += (_, _) => Refresh();
        _save.Click += (_, _) => { if (TryValues(out var values)) SaveRequested?.Invoke(_draft.Save(_name.Text ?? "", values)); };
        _preview.Click += (_, _) => PreviewRequested?.Invoke();
        _resume.Click += (_, _) => ResumeRequested?.Invoke();
        var close = new Button { Content = "Close" };
        close.Click += (_, _) => Close();
        _packedRow = EditorVisuals.Stack(5,
            EditorVisuals.Secondary("Metallic-roughness \u00b7 linear \u00b7 G roughness, B metallic"),
            EditorVisuals.Columns("*,Auto", _packed, _clearPacked));
        _packedRow.IsVisible = WithMetallicRoughness;
        _occlusionRow = EditorVisuals.Stack(5,
            _occlusionHelp,
            EditorVisuals.Columns("*,Auto", _occlusion, _clearOcclusion));
        _occlusionRow.IsVisible = WithOcclusion;
        _maps = EditorVisuals.Group(EditorVisuals.Heading("Texture maps"), EditorVisuals.Stack(5,
            EditorVisuals.Secondary("Normal map · linear UNORM, +Y"), EditorVisuals.Columns("*,Auto", _normal, _clearNormal),
            EditorVisuals.Secondary("Emissive texture · sRGB"), EditorVisuals.Columns("*,Auto", _emissive, _clearEmissive),
            EditorVisuals.Secondary("Linear emissive RGB · 0–65504"),
            EditorVisuals.Columns("*,*,*", inputs[5], inputs[6], inputs[7]), _emissiveHint, _packedRow, _occlusionRow));
        _maps.IsVisible = WithMaps;
        _basic.Click += (_, _) =>
        {
            if (!TryValues(out var values)) return;
            if (WithCombinedMaps)
            {
                if (UsesCombinedMaps(values) || UsesCombinedMaps(_draft.Initial)) return;
                WithCombinedMaps = false;
                ShowSettlement("Occlusion editing", false, false, false);
            }
            else if (WithOcclusion)
            {
                if (values.OcclusionMap is not null || _draft.Initial.OcclusionMap is not null) return;
                WithOcclusion = false;
                _occlusionRow.IsVisible = false;
                ShowSettlement("Metallic-roughness editing", false, false, false);
            }
            else if (WithMetallicRoughness)
            {
                if (values.MetallicRoughnessMap is not null || _draft.Initial.MetallicRoughnessMap is not null) return;
                WithMetallicRoughness = false;
                _packedRow.IsVisible = false;
                ShowSettlement("Material map editing", false, false, false);
            }
            else
            {
                if (!IsBasic(values)) return;
                WithMaps = false;
                _maps.IsVisible = false;
                ShowSettlement("Basic material editing", false, false, false);
            }
            _basic.IsVisible = false;
            Changed();
            if (_name.IsReadOnly) _texture.Focus(); else _name.Focus();
        };
        var scope = EditorVisuals.Secondary(bindingScope ?? "");
        scope.IsVisible = bindingScope is not null;
        scope.TextWrapping = TextWrapping.Wrap;
        _copyBindingReview.Click += async (_, _) =>
        {
            if (Clipboard is { } clipboard) await clipboard.SetTextAsync(_bindingReview.Text ?? "");
        };
        _bindingReviewActions.Children.Add(_bindingReview);
        _bindingReviewActions.Children.Add(_copyBindingReview);
        _form = EditorVisuals.Stack(10, scope, _bindingReviewActions,
            EditorVisuals.Group(EditorVisuals.Heading("Material"), EditorVisuals.Stack(5,
                EditorVisuals.Secondary("Name"), _name, EditorVisuals.Secondary("Base texture"), _texture)),
            EditorVisuals.Group(EditorVisuals.Heading("Surface"), EditorVisuals.Stack(6,
                EditorVisuals.Secondary("Linear base color"), EditorVisuals.Columns("*,*,*", inputs[0], inputs[1], inputs[2]),
                EditorVisuals.Columns("*,*", inputs[3], inputs[4]), EditorVisuals.Secondary("Factors accept values from 0 to 1."))),
            _maps,
            EditorVisuals.Disclosure("Details", EditorVisuals.Details(
                (draft.Original is { } origin ? origin.Material + "\n" : "") +
                $"{draft.Recipe(draft.Initial).Filter} · {draft.Recipe(draft.Initial).Address}")));
        var preview = EditorVisuals.Group(EditorVisuals.Heading("Draft preview"), EditorVisuals.Stack(6,
            _mesh, _preview, _image, _previewState, _lighting));
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
        var footer = EditorVisuals.Bar(EditorVisuals.Stack(5, _state, EditorVisuals.Actions(_save, _resume, _basic, close)));
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
        ? new(mesh.Guid, null, Surface, _draft.Recipe(values), WithMaps, WithMetallicRoughness, WithOcclusion, WithCombinedMaps) : null;

    private bool TryValues(out MaterialValues values)
    {
        values = default;
        if (_texture.SelectedItem is not AssetSummary texture) return false;
        Span<float> numbers = stackalloc float[8];
        numbers.Clear();
        int limit = WithMaps ? numbers.Length : 5;
        for (int i = 0; i < limit; ++i)
            if (!float.TryParse(_values[i].Text, NumberStyles.Float, CultureInfo.InvariantCulture, out numbers[i]) ||
                !float.IsFinite(numbers[i]) || numbers[i] < 0 || numbers[i] > (i < 5 ? 1 : 65504)) return false;
        values = new(texture.Guid, numbers[0], numbers[1], numbers[2], numbers[3], numbers[4])
        {
            MetallicRoughnessMap = WithMetallicRoughness ? (_packed.SelectedItem as AssetSummary)?.Guid : null,
            OcclusionMap = WithOcclusion ? (_occlusion.SelectedItem as AssetSummary)?.Guid : null,
            NormalMap = WithMaps ? (_normal.SelectedItem as AssetSummary)?.Guid : null,
            EmissiveTexture = WithMaps ? (_emissive.SelectedItem as AssetSummary)?.Guid : null,
            EmissiveRed = numbers[5], EmissiveGreen = numbers[6], EmissiveBlue = numbers[7]
        };
        return WithCombinedMaps || !UsesCombinedMaps(values);
    }

    private void Changed()
    {
        if (_values.Any(x => x is null)) return;
        Refresh(); DraftChanged?.Invoke();
    }

    private void Refresh()
    {
        bool valid = TryValues(out var values);
        _clearNormal.IsEnabled = _normal.SelectedItem is not null;
        _clearEmissive.IsEnabled = _emissive.SelectedItem is not null;
        _clearPacked.IsEnabled = _packed.SelectedItem is not null;
        _clearOcclusion.IsEnabled = _occlusion.SelectedItem is not null;
        _occlusion.IsEnabled = WithCombinedMaps || (_normal.SelectedItem is null && _packed.SelectedItem is null);
        _normal.IsEnabled = _packed.IsEnabled = WithCombinedMaps || _occlusion.SelectedItem is null;
        _occlusionHelp.Text = WithCombinedMaps
            ? _packed.SelectedItem is null
                ? "Occlusion · linear R · indirect light only. No effect without scene IBL."
                : "Occlusion · linear R · indirect light only. Clearing it restores direct lighting."
            : "Occlusion · linear R · indirect light only. Clear normal and metallic-roughness maps first.";
        _lighting.Text = WithOcclusion && ((WithCombinedMaps && _occlusion.SelectedItem is not null) ||
            (_normal.SelectedItem is null && _packed.SelectedItem is null))
            ? "Fixed studio IBL · preview only" : "Fixed directional lighting · preview only";
        _basic.IsEnabled = valid && (WithCombinedMaps
            ? !UsesCombinedMaps(values) && !UsesCombinedMaps(_draft.Initial)
            : WithOcclusion
            ? values.OcclusionMap is null && _draft.Initial.OcclusionMap is null
            : WithMetallicRoughness
            ? values.MetallicRoughnessMap is null && _draft.Initial.MetallicRoughnessMap is null
            : IsBasic(values));
        _emissiveHint.Text = valid && values.EmissiveRed == 0 && values.EmissiveGreen == 0 && values.EmissiveBlue == 0
            ? "The current factor produces no emission."
            : "Clearing the texture retains the factor and uses white.";
        _save.IsEnabled = !_saveBlocked && valid && (_name.IsReadOnly
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

    private static bool UsesCombinedMaps(MaterialValues values) =>
        values.OcclusionMap is not null && (values.NormalMap is not null || values.MetallicRoughnessMap is not null);

    private static bool IsBasic(MaterialValues values) => values.NormalMap is null && values.EmissiveTexture is null &&
        values.EmissiveRed == 0 && values.EmissiveGreen == 0 && values.EmissiveBlue == 0;

    internal void OfferLegacyEditing()
    {
        _basic.Content = WithCombinedMaps ? "Use occlusion editing" : WithOcclusion ? "Use metallic-roughness editing" : WithMetallicRoughness ? "Use normal and emissive editing" : "Use basic editing";
        _basic.IsVisible = WithMaps;
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
