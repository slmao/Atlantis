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
    private const int MaxPendingBytes = 64 * 1024 * 1024;
    private const string Building = "Building", Succeeded = "Succeeded", Failed = "Failed", Protected = "Protected";
    private readonly string _records = Path.Combine(scope, "cleanup");
    private string PendingPath => Path.Combine(_records, "pending");

    internal sealed record Ownership(int Version, string Checkout, FileIdentity CheckoutIdentity, string Leaf,
                                     FileIdentity DirectoryIdentity, string State);
    internal sealed record Candidate(Ownership Record, string Path, Entry[] Inventory, long Size, string? CurrentAttempt, string? PendingId = null)
    {
        internal string Reason => PendingId is not null ? "Verified remainder of interrupted cleanup" :
            Record.State == Succeeded ? "Superseded successful build" : "Superseded settled unsuccessful build";
    }
    internal sealed record Listing(IReadOnlyList<Candidate> Candidates, string? Issue = null, Func<Removal>? Restore = null);
    internal sealed record Removal(bool Removed, string Detail);
    internal enum Checkpoint { OriginalVerified, Recorded, Isolated, PermissionChanged, Guarded, DisposedEntry }

    private sealed record Pending(int Version, string Id, FileIdentity Attempts, Ownership Record, Entry[] Confirmed,
                                  Permission[] Permissions, bool DeletionPossible);

    private string Original(Pending pending) => Path.Combine(attempts, pending.Record.Leaf);
    private string Isolated(Pending pending) => Path.Combine(attempts, "cleanup-" + pending.Id);

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
            Write(RecordPath(attempt), new Ownership(Version, checkout, owner, Leaf(attempt), directory, Building));
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
            FileIdentity area = pinned.Add(attempts);
            if (ReadPending(owner, area) is { } pending) return PendingListing(pending, current);
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
                    VerifyProcessUse(attempt, record.DirectoryIdentity);
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

    internal Removal Remove(Candidate candidate, SavedSceneReceiptStore store, Action<Checkpoint>? observe = null,
        Action<Action>? afterGuard = null, Action? deleting = null)
    {
        try
        {
            using var pinned = new PinnedDirectories();
            FileIdentity owner = pinned.Add(SavedSceneWorkspace.CheckoutPath(checkout));
            pinned.Add(scope);
            pinned.Add(_records);
            FileIdentity area = pinned.Add(attempts);
            using var lease = store.Lease();
            Pending? active = ReadPending(owner, area), operation = null;
            string? current = store.CurrentAttempt();
            if (current != candidate.CurrentAttempt)
                throw new IOException("Build ownership, protection, or the current build changed. Refresh the list.");
            bool started = false, removed = false;
            try
            {
                if (candidate.PendingId is null)
                {
                    if (active is not null) throw new IOException("Resolve the pending cleanup before starting another one.");
                    string source = candidate.Path;
                    Ownership record = Read(RecordPath(source), Leaf(source));
                    VerifyOwnership(record, candidate, owner, current, source);
                    VerifyProcessUse(source, record.DirectoryIdentity);
                    using (var original = new BuildTree(source, deleting: true))
                    {
                        if (original.Inventory[0].Identity != record.DirectoryIdentity || !original.Inventory.SequenceEqual(candidate.Inventory))
                            throw new IOException("The confirmed directory or its contents changed. Refresh the list.");
                        original.VerifyMembership();
                    }
                    observe?.Invoke(Checkpoint.OriginalVerified);
                    var preparing = new Pending(Version, Guid.NewGuid().ToString("N"), area, record, candidate.Inventory, [], false);
                    SavePending(preparing);
                    operation = preparing;
                    observe?.Invoke(Checkpoint.Recorded);
                    MoveAttempt(source, Isolated(operation), record.DirectoryIdentity);
                    observe?.Invoke(Checkpoint.Isolated);
                }
                else
                {
                    if (active is null || active.Id != candidate.PendingId || !active.DeletionPossible || candidate.Path != Isolated(active))
                        throw new IOException("The pending cleanup changed. Refresh the list.");
                    string source = Original(active);
                    VerifyOwnership(Read(RecordPath(source), Leaf(source)), candidate, owner, current, source);
                    if (active.Record != candidate.Record) throw new IOException("Pending build ownership changed.");
                    operation = active;
                }

                string isolated = Isolated(operation);
                RequireVacant(Original(operation));
                VerifyProcessUse(isolated, operation.Record.DirectoryIdentity);
                using var tree = new IsolatedTree(isolated, operation.Record.DirectoryIdentity);
                Entry[] inventory = tree.Inventory;
                if (!inventory.SequenceEqual(candidate.Inventory) || !IsSubset(inventory, operation.Confirmed))
                    throw new IOException("The confirmed directory or its contents changed. Refresh the list.");

                Permission[] guards;
                if (active is null)
                {
                    guards = tree.CapturePermissions();
                    var prepared = operation with { Permissions = guards };
                    SavePending(prepared);
                    operation = prepared;
                }
                else
                {
                    var present = inventory.Where(item => item.Directory).Select(item => item.RelativePath).ToHashSet(StringComparer.Ordinal);
                    guards = operation.Permissions.Where(permission => present.Contains(permission.Directory.RelativePath)).ToArray();
                }

                tree.Seal(guards, () => observe?.Invoke(Checkpoint.PermissionChanged));
                tree.VerifySealed(guards);
                observe?.Invoke(Checkpoint.Guarded);
                afterGuard?.Invoke(() => { tree.Restore(guards); tree.Dispose(); });
                RequireVacant(Original(operation));
                VerifyProcessUse(isolated, operation.Record.DirectoryIdentity);
                tree.VerifySealed(guards);
                using var originalName = HoldVacantOriginal(Original(operation));
                if (!operation.DeletionPossible)
                {
                    var committing = operation with { DeletionPossible = true };
                    SavePending(committing);
                    operation = committing;
                }
                tree.Delete(guards, () => { started = true; deleting?.Invoke(); }, () => observe?.Invoke(Checkpoint.DisposedEntry));
                RequireVacant(isolated);
                removed = true;
                File.Delete(PendingPath);
                File.Delete(RecordPath(Original(operation)));
                return new(true, "Removed " + Original(operation));
            }
            catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e))
            {
                string result = removed ? "Removed the directory. Could not finish recording cleanup" :
                    started ? "Removal stopped after a delete request" : "Nothing was removed";
                string recovery = "";
                if (operation is not null && !removed)
                {
                    if (!started && !operation.DeletionPossible)
                    {
                        try { RestoreCore(operation, current); recovery = " The original path and permissions were restored."; }
                        catch (Exception restore) when (SavedSceneWorkspace.IsLocalFailure(restore))
                        { recovery = " Pending isolation: " + Isolated(operation) + ". " + restore.Message; }
                    }
                    else recovery = " Pending isolation: " + Isolated(operation) + ".";
                }
                return new(removed, $"{result}: {candidate.Path}. {e.Message}{recovery}");
            }
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e))
        { return new(false, $"Nothing was removed: {candidate.Path}. {e.Message}"); }
    }

    private static void VerifyOwnership(Ownership record, Candidate candidate, FileIdentity owner, string? current, string original)
    {
        if (record != candidate.Record || !Eligible(record, owner) || string.Equals(original, current, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Build ownership, protection, or the current build changed. Refresh the list.");
    }

    private Listing PendingListing(Pending pending, string? current)
    {
        string source = Original(pending), isolated = Isolated(pending);
        string detail = $"Pending cleanup\nOriginal: {source}\nIsolated: {isolated}";
        Func<Removal> restore = () => RestorePending(pending.Id);
        try
        {
            Ownership record = Read(RecordPath(source), Leaf(source));
            if (record != pending.Record || !Eligible(record, pending.Record.CheckoutIdentity))
                throw new IOException("Pending build ownership changed.");
            if (!pending.DeletionPossible || !Exists(isolated)) return new([], detail, restore);
            RequireVacant(source);
            if (string.Equals(source, current, StringComparison.OrdinalIgnoreCase)) throw new IOException("The build is current.");
            VerifyProcessUse(isolated, record.DirectoryIdentity);
            using var tree = new BuildTree(isolated, deleting: true);
            Entry[] inventory = tree.Inventory;
            if (inventory[0].Identity != record.DirectoryIdentity || !IsSubset(inventory, pending.Confirmed))
                throw new IOException("The isolated directory contains an unverified remainder.");
            return new([new(record, isolated, inventory, inventory.Sum(item => item.Length), current, pending.Id)], detail, restore);
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e) || e is OverflowException)
        { return new([], detail + "\n" + e.Message, restore); }
    }

    private Removal RestorePending(string id)
    {
        try
        {
            using var pinned = new PinnedDirectories();
            FileIdentity owner = pinned.Add(SavedSceneWorkspace.CheckoutPath(checkout));
            pinned.Add(scope);
            pinned.Add(_records);
            FileIdentity area = pinned.Add(attempts);
            var store = new SavedSceneReceiptStore(scope, attempts, checkout);
            using var lease = store.Lease();
            var pending = ReadPending(owner, area);
            if (pending is null || pending.Id != id) throw new IOException("The pending cleanup changed. Refresh the list.");
            RestoreCore(pending, store.CurrentAttempt());
            return new(false, "Restored " + Original(pending));
        }
        catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e))
        { return new(false, "The pending build was retained. " + e.Message); }
    }

    private void RestoreCore(Pending operation, string? current)
    {
        string source = Original(operation), isolated = Isolated(operation);
        if (Read(RecordPath(source), Leaf(source)) != operation.Record || string.Equals(source, current, StringComparison.OrdinalIgnoreCase))
            throw new IOException("The pending build's ownership or protection changed.");
        bool moved = Exists(isolated);
        if (moved) RequireVacant(source);
        string path = moved ? isolated : source;
        VerifyProcessUse(path, operation.Record.DirectoryIdentity);
        using (var tree = new IsolatedTree(path, operation.Record.DirectoryIdentity))
        {
            Entry[] inventory = tree.Inventory;
            if (operation.DeletionPossible && (inventory.Length != operation.Confirmed.Length || !IsSubset(inventory, operation.Confirmed)))
                throw new IOException("The original build is incomplete. Review the verified remainder for a fresh confirmation.");
            tree.Restore(operation.Permissions);
        }
        if (moved) MoveAttempt(isolated, source, operation.Record.DirectoryIdentity);
        File.Delete(PendingPath);
    }

    private static bool IsSubset(Entry[] inventory, Entry[] confirmed)
    {
        var expected = confirmed.ToDictionary(entry => entry.RelativePath, StringComparer.Ordinal);
        return inventory.All(entry => expected.TryGetValue(entry.RelativePath, out var prior) &&
            (entry == prior || (entry.Directory && prior.Directory && entry.Identity == prior.Identity)));
    }

    private static bool Exists(string path)
    {
        try { _ = File.GetAttributes(path); return true; }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { return false; }
    }

    private static void RequireVacant(string path)
    {
        if (Exists(path)) throw new IOException("A path that must be vacant is occupied: " + path);
    }

    private Pending? ReadPending(FileIdentity owner, FileIdentity area)
    {
        if (!Exists(PendingPath)) return null;
        SavedSceneWorkspace.Ordinary(PendingPath, directory: false);
        using var stream = new FileStream(PendingPath, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        if (stream.Length > MaxPendingBytes) throw new IOException("The pending cleanup record is too large.");
        var pending = JsonSerializer.Deserialize<Pending>(stream);
        if (pending is not { Version: Version, Id: { Length: 32 }, Record: not null, Confirmed.Length: > 0 and <= 100_000, Permissions: not null } ||
            !Guid.TryParseExact(pending.Id, "N", out _) || pending.Attempts != area || pending.Record.Checkout != checkout ||
            !Eligible(pending.Record, owner) || pending.Record.Leaf is not { Length: 16 } || pending.Record.Leaf.Any(c => !Uri.IsHexDigit(c)) ||
            pending.Confirmed[0].RelativePath != "" || !pending.Confirmed[0].Directory ||
            pending.Confirmed[0].Identity != pending.Record.DirectoryIdentity || pending.Permissions.Length > pending.Confirmed.Length ||
            pending.Permissions.Any(permission => permission is null || permission.Original is null || permission.Sealed is null) ||
            (pending.DeletionPossible && pending.Permissions.Length == 0))
            throw new IOException("The pending cleanup record could not be verified.");
        return pending;
    }

    private void SavePending(Pending pending) => Write(PendingPath, pending, MaxPendingBytes);

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

    private static void Write<T>(string path, T record, int maxBytes = MaxRecordBytes)
    {
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(record);
        if (bytes.Length > maxBytes) throw new IOException("The cleanup record is too large.");
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
