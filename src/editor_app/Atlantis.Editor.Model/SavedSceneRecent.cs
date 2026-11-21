using System;
using System.Collections.Generic;
using System.Linq;

namespace Atlantis.Editor.Model;

public sealed record SavedSceneIdentity(string Name, Guid Scene);
public sealed record SavedSceneRecentEntry(SavedSceneIdentity Identity, Guid Version);

public static class SavedSceneRecent
{
    public const int Limit = 8;

    public static SavedSceneRecentEntry[] Promote(IReadOnlyList<SavedSceneRecentEntry> entries, SavedSceneIdentity identity) =>
        [new(identity, Guid.NewGuid()), .. entries.Where(entry => entry.Identity.Scene != identity.Scene).Take(Limit - 1)];

    public static SavedSceneRecentEntry[]? Remove(IReadOnlyList<SavedSceneRecentEntry> entries, SavedSceneRecentEntry observed) =>
        entries.Contains(observed) ? entries.Where(entry => entry != observed).ToArray() : null;
}
