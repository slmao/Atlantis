using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using static Atlantis.Editor.SavedSceneCleanupWindows;

namespace Atlantis.Editor;

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal sealed class SavedSceneBuildCleanup(string checkout, string scope, string attempts)
{
    private const int Version = 1;
    private const int MaxRecordBytes = 4096;
    private const string Building = "Building", Succeeded = "Succeeded", Failed = "Failed", Protected = "Protected";
    private readonly string _records = Path.Combine(scope, "cleanup");

    internal sealed record Ownership(int Version, string Checkout, FileIdentity CheckoutIdentity, string Leaf,
                                     FileIdentity DirectoryIdentity, string State);
    internal sealed record Candidate(Ownership Record, string Path, Entry[] Inventory, long Size, string? CurrentAttempt)
    {
        internal string Reason => Record.State == Succeeded ? "Superseded successful build" : "Superseded settled unsuccessful build";
    }
    internal sealed record Listing(IReadOnlyList<Candidate> Candidates, string? Issue = null);
    internal sealed record Removal(bool Removed, string Detail);

    private static string Leaf(string attempt)
    {
        string leaf = Path.GetFileName(attempt);
        if (leaf.Length != 16 || leaf.Any(c => !Uri.IsHexDigit(c)))
            throw new IOException("Unrecognized Saved Scenes attempt.");
        return leaf;
    }

    private string RecordPath(string attempt)
    {
        if (Path.GetDirectoryName(attempt) != attempts) throw new IOException("Attempt is outside Saved Scenes.");
        return Path.Combine(_records, Leaf(attempt) + ".json");
    }

    internal bool Reserve(string attempt)
    {
        try
        {
            using var pinned = new PinnedDirectories();
            FileIdentity owner = pinned.Add(SavedSceneWorkspace.CheckoutPath(checkout));
            FileIdentity directory = pinned.Add(attempt);
            pinned.Add(scope);
            Directory.CreateDirectory(_records);
            pinned.Add(_records);
            Write(RecordPath(attempt), new(Version, checkout, owner, Leaf(attempt), directory, Building));
            return true;
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { return false; }
    }

    internal void Settle(string attempt, bool succeeded)
    {
        try { Update(attempt, succeeded ? Succeeded : Failed, onlyBuilding: true); }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { }
    }

    internal void Protect(string attempt) => Update(attempt, Protected, onlyBuilding: false);

    private void Update(string attempt, string state, bool onlyBuilding)
    {
        using var pinned = new PinnedDirectories();
        FileIdentity owner = pinned.Add(SavedSceneWorkspace.CheckoutPath(checkout));
        FileIdentity directory = pinned.Add(attempt);
        pinned.Add(_records);
        string recordPath = RecordPath(attempt);
        Ownership record = Read(recordPath, Leaf(attempt));
        if (record.CheckoutIdentity != owner || record.DirectoryIdentity != directory)
            throw new IOException("The build ownership changed.");
        if (record.State != state && (!onlyBuilding || record.State == Building))
            Write(recordPath, record with { State = state });
    }

    internal Listing List(Func<string?> readCurrent)
    {
        try
        {
            using var pinned = new PinnedDirectories();
            FileIdentity owner = pinned.Add(SavedSceneWorkspace.CheckoutPath(checkout));
            string? current = readCurrent();
            try { _ = File.GetAttributes(_records); }
            catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { return new([]); }
            pinned.Add(_records);
            pinned.Add(attempts);
            var candidates = new List<Candidate>();
            foreach (string path in Directory.EnumerateFiles(_records, "*.json").Order(StringComparer.Ordinal))
            {
                try
                {
                    string leaf = Leaf(Path.GetFileNameWithoutExtension(path));
                    string attempt = Path.Combine(attempts, leaf);
                    if (string.Equals(attempt, current, StringComparison.OrdinalIgnoreCase)) continue;
                    Ownership record = Read(RecordPath(attempt), leaf);
                    if (!Eligible(record, owner)) continue;
                    VerifyProcessUse(attempt);
                    using var tree = new BuildTree(attempt, deleting: true);
                    if (tree.Inventory[0].Identity != record.DirectoryIdentity) continue;
                    candidates.Add(new(record, attempt, tree.Inventory, tree.Inventory.Sum(item => item.Length), current));
                }
                catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e) || e is OverflowException) { }
            }
            return readCurrent() == current ? new(candidates) : new([], "The current build changed. Refresh the list.");
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e))
        { return new([], "Cannot verify build cleanup. " + e.Message); }
    }

    internal Removal Remove(Candidate candidate, SavedSceneReceiptStore store)
    {
        bool started = false, removed = false;
        try
        {
            using var pinned = new PinnedDirectories();
            FileIdentity owner = pinned.Add(SavedSceneWorkspace.CheckoutPath(checkout));
            pinned.Add(scope);
            pinned.Add(_records);
            pinned.Add(attempts);
            using var lease = store.Lease();
            string recordPath = RecordPath(candidate.Path);
            Ownership record = Read(recordPath, Leaf(candidate.Path));
            string? current = store.CurrentAttempt();
            if (current != candidate.CurrentAttempt || string.Equals(current, candidate.Path, StringComparison.OrdinalIgnoreCase) ||
                record != candidate.Record || !Eligible(record, owner))
                throw new IOException("Build ownership, protection, or the current build changed. Refresh the list.");
            VerifyProcessUse(candidate.Path);
            using var tree = new BuildTree(candidate.Path, deleting: true);
            if (tree.Inventory[0].Identity != record.DirectoryIdentity || !tree.Inventory.SequenceEqual(candidate.Inventory))
                throw new IOException("The confirmed directory or its contents changed. Refresh the list.");
            VerifyProcessUse(candidate.Path);
            tree.VerifyMembership();
            tree.Delete(() => started = true);
            try
            {
                _ = File.GetAttributes(candidate.Path);
                throw new IOException("Windows has not finished removing the directory.");
            }
            catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { removed = true; }
            File.Delete(recordPath);
            return new(true, "Removed " + candidate.Path);
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e))
        {
            string result = removed ? "Removed the directory. Could not remove its cleanup record" :
                started ? "Removal stopped after deleting some entries" : "Nothing was removed";
            return new(removed, $"{result}: {candidate.Path}. {e.Message}");
        }
    }

    private static bool Eligible(Ownership record, FileIdentity owner) =>
        record.CheckoutIdentity == owner && record.State is Succeeded or Failed;

    private Ownership Read(string path, string leaf)
    {
        SavedSceneWorkspace.Ordinary(path, directory: false);
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        if (stream.Length > MaxRecordBytes) throw new IOException("The cleanup record is too large.");
        var record = JsonSerializer.Deserialize<Ownership>(stream);
        if (record is null || record.Version != Version || record.Checkout != checkout || record.Leaf != leaf ||
            record.State is not (Building or Succeeded or Failed or Protected))
            throw new IOException("Build ownership is unknown.");
        return record;
    }

    private static void Write(string path, Ownership record)
    {
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(record);
        if (bytes.Length > MaxRecordBytes) throw new IOException("The cleanup record is too large.");
        string stage = Path.Combine(Path.GetDirectoryName(path)!, Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            using (var stream = new FileStream(stage, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                stream.Write(bytes);
                stream.Flush(flushToDisk: true);
            }
            if (File.Exists(path)) SavedSceneWorkspace.Ordinary(path, directory: false);
            File.Move(stage, path, overwrite: true);
        }
        finally
        {
            try { File.Delete(stage); }
            catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { }
        }
    }
}
