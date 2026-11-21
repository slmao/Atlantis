using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;

namespace Atlantis.Editor.Model;

public sealed record ProjectSourceKey(ProjectRootKind Root, string Path);

public static class ProjectNavigation
{
    public static bool InDirectory(string path, string? directory) => directory is null ||
        path == directory || path.StartsWith(directory + "/", StringComparison.Ordinal);

    public static ProjectSource? Find(ProjectScan? scan, ProjectSourceKey key) =>
        scan?.Roots.FirstOrDefault(root => root.Kind == key.Root && root.Coverage == ProjectCoverage.Complete)
            ?.Sources.FirstOrDefault(row => row.Path == key.Path);

    public static ProjectSourceKey? SourceOf(ProjectScan? scan, AssetGuid guid)
    {
        if (scan is not { Pending: false, CatalogReady: true }) return null;
        ProjectSourceKey? found = null;
        foreach (var root in scan.Roots.Where(root => root.Coverage == ProjectCoverage.Complete))
            foreach (var row in root.Sources.Where(row => row.Outputs.Contains(guid)))
            {
                if (found is not null) return null;
                found = new(root.Kind, row.Path);
            }
        return found;
    }

    public static bool Matches(ProjectScan after, ProjectSourceKey key, ProjectSource before, AssetGuid? output)
    {
        if (after.Pending || Find(after, key) is not { } current) return false;
        if (current.Kind != before.Kind || current.ImportName != before.ImportName ||
            current.Evidence != before.Evidence || current.Registration != before.Registration ||
            current.Producer != before.Producer || !current.Outputs.SequenceEqual(before.Outputs)) return false;
        return output is not AssetGuid guid || SourceOf(after, guid) == key;
    }
}

