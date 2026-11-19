using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor.Model;

public enum AssetBrowseSort { NameAscending, NameDescending }
public enum AssetBrowseGroup { None, Type, Source }
public sealed record AssetGroupHeader(string Label, int Count);

/// <summary>Display keys for one catalog summary, independent of row position.</summary>
public sealed class AssetBrowseProjection
{
    public AssetBrowseProjection(AssetSummary asset)
    {
        Asset = asset;
        Name = AssetName(asset);
        Guid = asset.Guid.ToString();
        CompactGuid = Guid.Replace("-", "", StringComparison.Ordinal);
        SourceGroup = asset.Source.StartsWith("assets:", StringComparison.Ordinal) ? "assets:" :
            asset.Source.StartsWith("content:", StringComparison.Ordinal) ? "content:" : "Other";
    }

    public AssetSummary Asset { get; }
    public string Name { get; }
    public string Guid { get; }
    public string CompactGuid { get; }
    public string SourceGroup { get; }

    public bool Matches(string query, string path, string? compact) =>
        Name.Contains(query, StringComparison.OrdinalIgnoreCase) ||
        Asset.Source.Contains(path, StringComparison.OrdinalIgnoreCase) ||
        Guid.Contains(query, StringComparison.OrdinalIgnoreCase) ||
        compact is not null && CompactGuid.Contains(compact, StringComparison.OrdinalIgnoreCase);

    public static string? CompactQuery(string query) =>
        query.Any(Uri.IsHexDigit) && query.All(c => c == '-' || Uri.IsHexDigit(c))
            ? query.Replace("-", "", StringComparison.Ordinal) : null;

    public static IReadOnlyList<AssetBrowseProjection> Order(IEnumerable<AssetBrowseProjection> keys, AssetBrowseSort sort) =>
        (sort == AssetBrowseSort.NameAscending
            ? keys.OrderBy(k => k.Name, StringComparer.OrdinalIgnoreCase)
            : keys.OrderByDescending(k => k.Name, StringComparer.OrdinalIgnoreCase))
            .ThenBy(k => k.Asset.Source, StringComparer.Ordinal)
            .ThenBy(k => k.Guid, StringComparer.Ordinal).ToArray();

    public static IReadOnlyList<object> Rows(IReadOnlyList<AssetBrowseProjection> ordered, AssetBrowseGroup group)
    {
        if (group == AssetBrowseGroup.None) return ordered.Select(key => (object)key.Asset).ToArray();
        var groups = ordered.GroupBy(key => group == AssetBrowseGroup.Type ? key.Asset.Type.ToString() : key.SourceGroup);
        string[] order = group == AssetBrowseGroup.Type ? Enum.GetNames<AssetType>() : ["assets:", "content:", "Other"];
        return groups.OrderBy(g => Array.IndexOf(order, g.Key))
            .SelectMany(g => new object[] { new AssetGroupHeader(g.Key, g.Count()) }.Concat(g.Select(k => (object)k.Asset))).ToArray();
    }

    public static string? ManagedTextureName(AssetSummary asset, string suffix)
    {
        const string prefix = "content:texture_";
        string source = asset.Source;
        if (asset.Type != AssetType.Texture || source.Length <= prefix.Length + suffix.Length ||
            !source.StartsWith(prefix, StringComparison.Ordinal) || !source.EndsWith(suffix, StringComparison.Ordinal))
            return null;

        string name = source[prefix.Length..^suffix.Length];
        return Codec.ValidImportName(name) ? name : null;
    }

    public static string AssetName(AssetSummary asset)
    {
        if (asset.DisplayName is string displayName) return displayName;
        if ((ManagedTextureName(asset, "/source.png") ?? ManagedTextureName(asset, "/occlusion.png")) is string name)
            return name;
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

}
