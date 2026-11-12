using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Atlantis.Gameplay.Assets;

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

    internal static string AssetName(AssetSummary asset)
    {
        string source = asset.Source;
        int separator = source.IndexOf(':');
        if (separator >= 0) source = source[(separator + 1)..];
        int fragment = source.IndexOf('#');
        string file = fragment < 0 ? source : source[..fragment];
        file = file[(file.LastIndexOf('/') + 1)..];
        if (fragment >= 0) return file + " · " + source[(fragment + 1)..];
        if (file.EndsWith(".txt", StringComparison.Ordinal)) file = file[..^4];
        int extension = file.LastIndexOf('.');
        return extension > 0 ? file[..extension] : file;
    }

    internal static Control AssetRow(AssetSummary asset)
    {
        var label = Ellipsis(AssetName(asset));
        label.Margin = new Thickness(6, 0);
        var kind = Secondary(asset.Type.ToString());
        kind.VerticalAlignment = VerticalAlignment.Center;
        var row = Columns("Auto,*,Auto", LineIcon(asset.Type switch
        {
            AssetType.Mesh => Cube,
            AssetType.Material => Material,
            _ => File
        }), label, kind);
        ToolTip.SetTip(row, asset.Source + "\n" + asset.Guid);
        AutomationProperties.SetName(row, AssetName(asset) + ", " + asset.Type);
        return row;
    }
}
