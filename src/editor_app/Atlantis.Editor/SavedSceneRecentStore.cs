using System;
using System.IO;
using System.Linq;
using System.Runtime.Versioning;
using System.Text.Json;
using Atlantis.Editor.Model;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor;

[SupportedOSPlatform("windows")]
internal sealed class SavedSceneRecentStore(string root)
{
    private const int Version = 1;
    private const int MaxBytes = 16 * 1024;
    private sealed record History(int Version, SavedSceneRecentEntry[] Entries);

    private string HistoryPath => Path.Combine(root, "recent-scenes.json");

    internal SavedSceneRecentEntry[] Read()
    {
        CheckDrive();
        try { SavedSceneWorkspace.Ordinary(root, directory: true); }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { return []; }
        if (!ExistsOrdinary(HistoryPath)) return [];
        using var stream = new FileStream(HistoryPath, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        if (stream.Length > MaxBytes) throw new IOException("Recent Saved Scenes history is too large.");
        var history = JsonSerializer.Deserialize<History>(stream);
        if (history is not { Version: Version, Entries: { Length: <= SavedSceneRecent.Limit } entries } ||
            entries.Any(entry => entry?.Identity is not { Scene: var scene, Name: var name } ||
                scene == Guid.Empty || !Codec.ValidSaveAsName(name) || entry.Version == Guid.Empty) ||
            entries.Select(entry => entry.Identity.Scene).Distinct().Count() != entries.Length ||
            entries.Select(entry => entry.Version).Distinct().Count() != entries.Length)
            throw new IOException("Recent Saved Scenes history has an unsupported version or invalid entries.");
        return entries;
    }

    internal void Record(SavedSceneIdentity identity)
    {
        using var lease = Lease();
        Publish(SavedSceneRecent.Promote(Read(), identity));
    }

    internal bool Remove(SavedSceneRecentEntry observed)
    {
        using var lease = Lease();
        var remaining = SavedSceneRecent.Remove(Read(), observed);
        if (remaining is null) return false;
        Publish(remaining);
        return true;
    }

    private FileStream Lease()
    {
        CheckDrive();
        EnsureDirectory(root);
        string path = Path.Combine(root, "recent-scenes.lease");
        _ = ExistsOrdinary(path);
        return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }

    private void CheckDrive()
    {
        var drive = new DriveInfo(Path.GetPathRoot(root)!);
        if (drive.DriveType != DriveType.Fixed || drive.DriveFormat != "NTFS")
            throw new IOException("Recent Saved Scenes requires ordinary local NTFS storage.");
    }

    private void Publish(SavedSceneRecentEntry[] entries)
    {
        string stage = Path.Combine(root, "recent-" + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            using (var stream = new FileStream(stage, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                JsonSerializer.Serialize(stream, new History(Version, entries));
                stream.Flush(flushToDisk: true);
            }
            _ = ExistsOrdinary(HistoryPath);
            File.Move(stage, HistoryPath, overwrite: true);
        }
        finally
        {
            try { File.Delete(stage); }
            catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { }
        }
    }

    private static bool ExistsOrdinary(string path)
    {
        try { SavedSceneWorkspace.Ordinary(path, directory: false); return true; }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { return false; }
    }

    private static void EnsureDirectory(string path)
    {
        try { SavedSceneWorkspace.Ordinary(path, directory: true); }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException)
        {
            EnsureDirectory(Path.GetDirectoryName(path) ?? throw new IOException("The local history root is unavailable."));
            Directory.CreateDirectory(path);
            SavedSceneWorkspace.Ordinary(path, directory: true);
        }
    }
}
