using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Styling;
using Avalonia.VisualTree;
using Avalonia.Threading;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor;

// Spec 0093: presentation resources private to the Windows Editor App.
internal static class EditorVisuals
{
    internal static readonly IBrush Canvas = Brush(0x181819);
    internal static readonly IBrush Pane = Brush(0x202022);
    internal static readonly IBrush Header = Brush(0x262628);
    internal static readonly IBrush Raised = Brush(0x2D2D30);
    internal static readonly IBrush Inset = Brush(0x1B1B1D);
    internal static readonly IBrush Line = Brush(0x39393D);
    internal static readonly IBrush Edge = Brush(0x74747B);
    internal static readonly IBrush Ink = Brush(0xEFF0F2);
    internal static readonly IBrush Muted = Brush(0xAFB0B5);
    internal static readonly IBrush Selected = Brush(0x283B54);
    internal static readonly IBrush Accent = Brush(0xA5CAFF);
    internal static readonly IBrush Focus = Brush(0xB0D4FF);
    internal static readonly IBrush Success = Brush(0xA2D3BB);
    internal static readonly IBrush Warning = Brush(0xF1C990);
    internal static readonly IBrush Error = Brush(0xFFC0BF);

    private static IBrush Brush(uint rgb) => new SolidColorBrush(Color.FromUInt32(0xff000000 | rgb));

    internal static void Install(Application app)
    {
        app.RequestedThemeVariant = ThemeVariant.Dark;
        app.Resources["SystemAccentColor"] = Color.FromUInt32(0xffa5caff);
        app.Resources["ButtonBackground"] = Raised;
        app.Resources["ButtonBackgroundPointerOver"] = Brush(0x38383c);
        app.Resources["ButtonBackgroundPressed"] = Selected;
        app.Resources["ExpanderHeaderBackground"] = Header;
        app.Resources["ExpanderHeaderBackgroundPointerOver"] = Raised;
        app.Resources["TextControlBackground"] = Inset;
        app.Resources["TextControlBackgroundPointerOver"] = Inset;
        app.Resources["TextControlBackgroundFocused"] = Selected;
        app.Resources["TextControlBorderBrush"] = Line;
        app.Resources["TextControlBorderBrushPointerOver"] = Edge;
        app.Resources["TextControlBorderBrushFocused"] = Focus;
        app.Resources["SystemControlHighlightListAccentLowBrush"] = Selected;
        app.Resources["SystemControlHighlightListAccentMediumBrush"] = Selected;

        app.Styles.Add(new Style(s => s.OfType<Window>())
        {
            Setters =
            {
                new Setter(Window.BackgroundProperty, Canvas),
                new Setter(Window.ForegroundProperty, Ink),
                new Setter(Window.FontFamilyProperty, new FontFamily("Segoe UI, $Default")),
                new Setter(Window.FontSizeProperty, 13d),
            }
        });
        app.Styles.Add(new Style(s => s.OfType<Button>())
        {
            Setters = { new Setter(Button.MinHeightProperty, 30d), new Setter(Button.PaddingProperty, new Thickness(8, 4)),
                        new Setter(Button.BackgroundProperty, Raised) }
        });
        app.Styles.Add(new Style(s => s.OfType<Button>().Class("editor-primary"))
        {
            Setters = { new Setter(Button.BackgroundProperty, Accent), new Setter(Button.ForegroundProperty, Brush(0x142032)) }
        });
        app.Styles.Add(new Style(s => s.OfType<ToggleButton>())
        {
            Setters = { new Setter(ToggleButton.MinHeightProperty, 30d),
                        new Setter(ToggleButton.PaddingProperty, new Thickness(8, 4)),
                        new Setter(ToggleButton.BackgroundProperty, Raised) }
        });
        app.Styles.Add(new Style(s => s.OfType<TabItem>())
        {
            Setters = { new Setter(TabItem.FontSizeProperty, 13d), new Setter(TabItem.PaddingProperty, new Thickness(6, 4)) }
        });
        app.Styles.Add(new Style(s => s.OfType<TextBox>().Class("editor-search"))
        {
            Setters = { new Setter(TextBox.BorderBrushProperty, Edge), new Setter(TextBox.MinHeightProperty, 32d) }
        });
        app.Styles.Add(new Style(s => s.OfType<ListBox>())
        {
            Setters = { new Setter(ListBox.BackgroundProperty, Inset), new Setter(ListBox.BorderBrushProperty, Line),
                        new Setter(ListBox.BorderThicknessProperty, new Thickness(1)) }
        });
        app.Styles.Add(new Style(s => s.OfType<ListBoxItem>())
        {
            Setters = { new Setter(ListBoxItem.MinHeightProperty, 30d), new Setter(ListBoxItem.PaddingProperty, new Thickness(7, 3)) }
        });
        app.Styles.Add(new Style(s => s.OfType<ListBoxItem>().Class(":selected"))
        {
            Setters = { new Setter(ListBoxItem.BackgroundProperty, Selected),
                        new Setter(ListBoxItem.BorderBrushProperty, Accent),
                        new Setter(ListBoxItem.BorderThicknessProperty, new Thickness(2, 0, 0, 0)) }
        });
    }

    internal static TextBlock Heading(string text, double size = 13) =>
        new() { Text = text, FontSize = size, FontWeight = FontWeight.SemiBold, VerticalAlignment = VerticalAlignment.Center };

    internal static TextBlock Secondary(string text) =>
        new() { Text = text, Foreground = Muted, FontSize = 12, TextWrapping = TextWrapping.Wrap };

    internal static SelectableTextBlock Details(string text) =>
        new() { Text = text, Foreground = Muted, FontSize = 12, TextWrapping = TextWrapping.Wrap };

    internal static StackPanel Stack(double spacing, params Control[] children)
    {
        var stack = new StackPanel { Spacing = spacing };
        foreach (Control child in children) stack.Children.Add(child);
        return stack;
    }

    internal static WrapPanel Actions(params Control[] children)
    {
        var panel = new WrapPanel();
        foreach (Control child in children)
        {
            child.Margin = new Thickness(0, 0, 5, 4);
            panel.Children.Add(child);
        }
        return panel;
    }

    internal static Border Bar(Control content) => new()
    {
        Background = Header, BorderBrush = Line, BorderThickness = new Thickness(0, 0, 0, 1),
        Padding = new Thickness(9, 4), Child = content
    };

    internal static ScrollViewer Scroll(Control content) => new()
    {
        Content = content, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
    };

    internal static Border Group(Control header, Control content) => new()
    {
        Background = Pane, BorderBrush = Line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(6),
        Child = Stack(0, Bar(header), new Border { Child = content, Padding = new Thickness(8) }),
    };

    internal static Expander Disclosure(string title, Control content, bool expanded = false) => new()
    {
        Header = title, Content = content, IsExpanded = expanded, HorizontalAlignment = HorizontalAlignment.Stretch,
        HorizontalContentAlignment = HorizontalAlignment.Stretch, Padding = new Thickness(4),
    };

    internal static Grid Columns(string columns, params Control[] children)
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions(columns) };
        for (int index = 0; index < children.Length; ++index)
        {
            Grid.SetColumn(children[index], index);
            grid.Children.Add(children[index]);
        }
        return grid;
    }

    internal static Control TaskIssueLine()
    {
        var outcome = Ellipsis("");
        outcome.Bind(TextBlock.TextProperty, new Binding(nameof(TaskIssueItem.Outcome)));
        outcome.Bind(TextBlock.ForegroundProperty, new Binding(nameof(TaskIssueItem.Tone)));
        var target = Ellipsis("");
        target.Bind(TextBlock.TextProperty, new Binding(nameof(TaskIssueItem.Label)));
        target.Margin = new Thickness(0, 0, 8, 0);
        return Columns("3*,2*", target, outcome);
    }

    internal static TextBlock Ellipsis(string text) => new()
    {
        Text = text, TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center,
    };

    private static readonly Dictionary<string, Geometry> Icons = new();

    private static PathIcon LineIcon(string path)
    {
        if (!Icons.TryGetValue(path, out Geometry? geometry))
        {
            geometry = Geometry.Parse(path).GetWidenedGeometry(new Pen(Muted, 1.2, lineCap: PenLineCap.Round));
            Icons.Add(path, geometry);
        }
        return new PathIcon { Data = geometry, Width = 16, Height = 16, Foreground = Muted,
            VerticalAlignment = VerticalAlignment.Center };
    }

    internal const string Cube = "M8,2 L14,5.5 14,11 8,14.5 2,11 2,5.5 Z M2,5.5 L8,9 14,5.5 M8,9 L8,14.5";
    private const string Material = "M8,2 L14,6 8,10 2,6 Z M2,9 L8,13 14,9";
    internal const string File = "M4,2 H9 L13,6 V14 H4 Z M9,2 V6 H13 M6,10 H11";
    internal const string Save = "M3,2 H10.5 L13,4.5 V14 H3 Z M5,2 V6 H11 V2.5 M5,14 V9 H11 V14";
    internal const string Play = "M5,3 L13,8 5,13 Z";
    internal const string Pause = "M5,3 V13 M11,3 V13";
    internal const string Step = "M3,3 L10,8 3,13 Z M13,3 V13";
    internal const string Undo = "M6,4 L2.5,7.5 6,11 M3,7.5 H9 A4,4 0 0 1 13,11.5";
    internal const string Redo = "M10,4 L13.5,7.5 10,11 M13,7.5 H7 A4,4 0 0 0 3,11.5";
    internal const string Plus = "M8,3 V13 M3,8 H13";
    internal const string Sliders = "M2,4 H14 M2,8 H14 M2,12 H14 M5,2.5 V5.5 M10,6.5 V9.5 M7,10.5 V13.5";
    internal const string Catalog = "M2,4 H14 V13 H2 Z M5,2 V4 M11,2 V4 M2,7 H14";

    internal static IDataTemplate LabelTemplate(string path, string? shortLabel = null) =>
        new FuncDataTemplate<string>((text, _) => Columns("Auto,*", LineIcon(path), new TextBlock
        {
            Text = shortLabel ?? text, FontSize = 13, Margin = new Thickness(6, 0, 0, 0),
            VerticalAlignment = VerticalAlignment.Center,
        }));

    internal static void Glyph(Button button, string path, string? shortLabel = null) =>
        button.ContentTemplate = LabelTemplate(path, shortLabel);

    internal static string AssetName(AssetSummary asset) => AssetBrowseProjection.AssetName(asset);
    internal static string? ManagedTextureName(AssetSummary asset, string suffix) =>
        AssetBrowseProjection.ManagedTextureName(asset, suffix);

    internal static void ConfigureAssetList(ListBox list, bool thumbnails = false)
    {
        // Selection and projection have different scroll policies (Spec 0114 R3).
        // Avalonia also scrolls when AutoScrollToSelectedItem is re-enabled, so
        // keep it off and reveal actual selection changes explicitly.
        list.AutoScrollToSelectedItem = false;
        list.SelectionChanged += RevealAssetSelection;
        list.ItemsPanel = new FuncTemplate<Panel?>(() => new VirtualizingStackPanel());
        list.ItemTemplate = new FuncDataTemplate<object>((value, _) => value switch
        {
            AssetSummary asset => AssetRow(asset, thumbnails),
            AssetGroupHeader group => Heading(group.Label + " (" + group.Count + ")"),
            _ => new TextBlock()
        });
        list.ContainerPrepared += (_, e) =>
        {
            bool asset = list.Items[e.Index] is AssetSummary;
            e.Container.IsEnabled = asset;
            e.Container.Focusable = asset;
            AutomationProperties.SetName(e.Container, list.Items[e.Index] is AssetGroupHeader group
                ? group.Label + ", " + group.Count + " assets" : null);
        };
    }

    private static void RevealAssetSelection(object? sender, SelectionChangedEventArgs e)
    {
        if (sender is ListBox list && ReferenceEquals(e.Source, list) && list.SelectedItem is AssetSummary asset)
            list.ScrollIntoView(asset);
    }

    internal static void BindAssetList(ListBox list, IReadOnlyList<object> rows, AssetGuid? selected)
    {
        if (ReferenceEquals(list.ItemsSource, rows))
        {
            if ((list.SelectedItem as AssetSummary)?.Guid != selected)
                list.SelectedItem = rows.OfType<AssetSummary>().FirstOrDefault(asset => asset.Guid == selected);
            return;
        }
        var inView = VisibleAssetRows(list).Select(row => row.Asset).ToArray();
        AssetGuid? anchor = inView.FirstOrDefault(asset => asset.Guid == selected)?.Guid ??
            inView.FirstOrDefault()?.Guid;
        if ((list.SelectedItem as AssetSummary)?.Guid != selected && selected is not null) anchor = selected;
        list.SelectionChanged -= RevealAssetSelection;
        try
        {
            list.ItemsSource = rows;
            list.SelectedItem = rows.OfType<AssetSummary>().FirstOrDefault(asset => asset.Guid == selected);
        }
        finally { list.SelectionChanged += RevealAssetSelection; }
        var target = rows.OfType<AssetSummary>().FirstOrDefault(asset => asset.Guid == anchor) ?? rows.OfType<AssetSummary>().FirstOrDefault();
        if (target is null) return;
        Dispatcher.UIThread.Post(() =>
        {
            if (ReferenceEquals(list.ItemsSource, rows) && (list.SelectedItem as AssetSummary)?.Guid == selected)
                list.ScrollIntoView(target);
        }, DispatcherPriority.Loaded);
    }

    internal static IEnumerable<(ListBoxItem Row, AssetSummary Asset)> VisibleAssetRows(ListBox list)
    {
        var scroll = list.GetVisualDescendants().OfType<ScrollViewer>().FirstOrDefault();
        if (scroll is null || scroll.Viewport.Height <= 0 || scroll.Viewport.Width <= 0) yield break;
        var rows = scroll.GetVisualDescendants().OfType<ListBoxItem>()
            .Where(row => row.IsVisible && row.Bounds.Height > 0 && row.DataContext is AssetSummary)
            .Select(row => (Row: row, Point: row.TranslatePoint(default, scroll)))
            .Where(row => row.Point is Point p && p.Y + row.Row.Bounds.Height > 0 && p.Y < scroll.Viewport.Height &&
                p.X + row.Row.Bounds.Width > 0 && p.X < scroll.Viewport.Width)
            .OrderBy(row => row.Point!.Value.Y);
        foreach (var row in rows) yield return (row.Row, (AssetSummary)row.Row.DataContext!);
    }

    internal sealed class AssetThumbnailSlot : Grid
    {
        internal AssetGuid Guid { get; }
        private readonly Control _icon = LineIcon(Cube);
        private readonly Image _image = new() { Stretch = Stretch.Uniform, IsVisible = false };

        internal AssetThumbnailSlot(AssetGuid guid)
        {
            Guid = guid;
            Width = Height = 40;
            IsHitTestVisible = false;
            _icon.HorizontalAlignment = HorizontalAlignment.Center;
            _icon.VerticalAlignment = VerticalAlignment.Center;
            Children.Add(_icon);
            Children.Add(_image);
        }

        internal void Show(Bitmap? image, string? failure)
        {
            _image.Source = image;
            _image.IsVisible = image is not null;
            _icon.IsVisible = image is null;
            AutomationProperties.SetHelpText(this, failure);
            if (Parent is Control row) AutomationProperties.SetHelpText(row, failure);
        }
    }

    internal static Control AssetRow(AssetSummary asset, bool thumbnails = false)
    {
        var label = Ellipsis(AssetName(asset));
        label.Margin = new Thickness(6, 0);
        string kindText = AssetBrowseProjection.AssetKind(asset);
        var kind = Secondary(kindText);
        kind.VerticalAlignment = VerticalAlignment.Center;
        Control icon = thumbnails && asset.Type == AssetType.Mesh
            ? new AssetThumbnailSlot(asset.Guid)
            : LineIcon(asset.Type switch
            {
                AssetType.Mesh => Cube,
                AssetType.Material => Material,
                _ => File
            });
        var row = Columns("Auto,*,Auto", icon, label, kind);
        ToolTip.SetTip(row, asset.Source + "\n" + asset.Guid);
        AutomationProperties.SetName(row, AssetName(asset) + ", " + kindText);
        return row;
    }

    internal static void ConfigureReferenceResults(ListBox list, Action<AssetReferenceRow> locate)
    {
        list.ItemsPanel = new FuncTemplate<Panel?>(() => new VirtualizingStackPanel());
        list.ItemTemplate = new FuncDataTemplate<AssetReferenceRow>((reference, _) =>
        {
            if (reference is null) return new TextBlock();
            string name = ReferenceMaterialName(reference.Material);
            string location = reference.Material.Source + "\n" + reference.Material.Guid;
            if (reference.Slot is not TextureReferenceSlot slot)
                return Stack(2, Ellipsis(name), Details("Slot unavailable · " + reference.Diagnostic), Details(location));

            string label = AssetReferenceRow.Label(slot);
            var button = new Button { Content = Ellipsis(name + " · " + label), HorizontalAlignment = HorizontalAlignment.Stretch };
            AutomationProperties.SetName(button, "Locate " + name + " · " + label);
            AutomationProperties.SetHelpText(button, location);
            ToolTip.SetTip(button, location);
            button.Click += (_, _) => locate(reference);
            return button;
        });
        list.AddHandler(InputElement.KeyDownEvent, (_, e) =>
        {
            if (e.Key != Key.Enter || e.Source is not ListBoxItem { DataContext: AssetReferenceRow { Slot: not null } row }) return;
            e.Handled = true;
            locate(row);
        }, RoutingStrategies.Tunnel);
    }

    internal static string ReferenceMaterialName(AssetSummary asset)
    {
        const string prefix = "content:material_", suffix = "/source.material.txt";
        return asset.DisplayName is null && asset.Source.StartsWith(prefix, StringComparison.Ordinal) &&
            asset.Source.EndsWith(suffix, StringComparison.Ordinal) && asset.Source.Length > prefix.Length + suffix.Length
            ? asset.Source[prefix.Length..^suffix.Length] : AssetName(asset);
    }

    internal static TextBox ReadOnlyTextureSlot(TextureReferenceSlot slot, string name, AssetGuid texture)
    {
        string label = AssetReferenceRow.Label(slot);
        var row = new TextBox { Text = label + " · " + name, IsReadOnly = true, Tag = slot };
        AutomationProperties.SetName(row, label + " texture slot");
        AutomationProperties.SetHelpText(row, texture.ToString());
        ToolTip.SetTip(row, texture.ToString());
        return row;
    }
}
