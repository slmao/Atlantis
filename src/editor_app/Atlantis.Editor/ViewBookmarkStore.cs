using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Atlantis.Editor.Model;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor;

internal sealed record ViewBookmarkLibrary(string Revision, IReadOnlyList<ViewBookmark> Entries);

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal sealed class ViewBookmarkStore
{
    private const int FileLimit = 64 * 1024;
    private const int EntryLimit = 16;
    private readonly string _scope;
    private readonly DocumentState _document;
    private readonly string _directory;
    private readonly string _path;

    internal ViewBookmarkStore(DocumentState document, string? directory = null)
    {
        _document = document;
        _scope = RenderLookStore.ScopeFor(document);
        _directory = directory ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Atlantis", "ViewBookmarks", "v1");
        _path = Path.Combine(_directory, _scope + ".json");
    }

    internal ViewBookmarkLibrary Read()
    {
        if (RenderLookStore.ScopeFor(_document) != _scope) throw Invalid();
        try { SavedSceneWorkspace.Ordinary(_path, directory: false); }
        catch (FileNotFoundException) { return new("", []); }
        catch (DirectoryNotFoundException) { return new("", []); }
        using var input = new FileStream(_path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        if (input.Length is <= 0 or > FileLimit) throw Invalid();
        var bytes = new byte[(int)input.Length];
        input.ReadExactly(bytes);
        try
        {
            using var json = JsonDocument.Parse(bytes);
            var root = json.RootElement;
            if (!Exact(root, 3) || !root.TryGetProperty("version", out var version) ||
                version.ValueKind != JsonValueKind.Number || !version.TryGetInt32(out int v) || v != 1 ||
                Text(root, "scope") != _scope || !root.TryGetProperty("bookmarks", out var entries) ||
                entries.ValueKind != JsonValueKind.Array || entries.GetArrayLength() > EntryLimit) throw Invalid();
            var result = new List<ViewBookmark>(entries.GetArrayLength());
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var ids = new HashSet<Guid>();
            foreach (var entry in entries.EnumerateArray())
            {
                if (!Exact(entry, 7) || !Guid.TryParseExact(Text(entry, "id"), "D", out var id) || id == Guid.Empty ||
                    Text(entry, "name") is not { } name || !ValidName(name) ||
                    !Vector(entry, "eye", out var eye) || !Vector(entry, "forward", out var forward) ||
                    !Vector(entry, "lens", out var lens) || !entry.TryGetProperty("width", out var w) ||
                    !entry.TryGetProperty("height", out var h) || w.ValueKind != JsonValueKind.Number ||
                    h.ValueKind != JsonValueKind.Number || !w.TryGetUInt32(out uint width) ||
                    !h.TryGetUInt32(out uint height)) throw Invalid();
                var view = new ViewportObservation(name, eye, forward, new(lens.X, lens.Y, lens.Z), new(width, height));
                if (!view.IsValid || !names.Add(name) || !ids.Add(id)) throw Invalid();
                result.Add(new(id, view));
            }
            return new(Convert.ToHexString(SHA256.HashData(bytes)), result);
        }
        catch (Exception error) when (error is JsonException or InvalidOperationException)
        { throw new IOException("The bookmark store is unreadable.", error); }
    }

    internal void Save(string expectedRevision, ViewportObservation view, Guid? replace) =>
        Write(expectedRevision, entries =>
        {
            if (!view.IsValid || !ValidName(view.Name))
                throw new IOException("Use a valid view and a name of up to 64 UTF-8 bytes.");
            var duplicate = entries.FindIndex(b => string.Equals(b.View.Name, view.Name, StringComparison.OrdinalIgnoreCase));
            if (replace is { } id)
            {
                if (duplicate < 0 || entries[duplicate].Id != id)
                    throw new IOException("The bookmark changed. Refresh before replacing it.");
                entries[duplicate] = new(id, view);
            }
            else
            {
                if (duplicate >= 0) throw new IOException("Confirm replacement of the existing bookmark.");
                if (entries.Count == EntryLimit) throw new IOException("This scene already has 16 bookmarks.");
                entries.Add(new(Guid.NewGuid(), view));
            }
        });

    internal void Remove(string expectedRevision, Guid id) => Write(expectedRevision, entries =>
    {
        if (entries.RemoveAll(entry => entry.Id == id) != 1)
            throw new IOException("The bookmark changed. Refresh before removing it.");
    });

    private void Write(string expectedRevision, Action<List<ViewBookmark>> mutate)
    {
        RenderLookStore.EnsureDirectory(_directory);
        var lockPath = _path + ".lock";
        if (File.Exists(lockPath)) SavedSceneWorkspace.Ordinary(lockPath, directory: false);
        using var lease = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.None);
        var current = Read();
        if (current.Revision != expectedRevision) throw new IOException("Bookmarks changed. Refresh before saving.");
        var entries = current.Entries.ToList();
        mutate(entries);
        var bytes = JsonSerializer.SerializeToUtf8Bytes(new
        {
            version = 1, scope = _scope,
            bookmarks = entries.Select(entry => new
            {
                id = entry.Id, name = entry.View.Name, eye = Components(entry.View.Eye), forward = Components(entry.View.Forward),
                lens = new[] { entry.View.Lens.FovYRadians, entry.View.Lens.NearZ, entry.View.Lens.FarZ },
                width = entry.View.Extent.Width, height = entry.View.Extent.Height
            })
        });
        if (bytes.Length > FileLimit) throw Invalid();
        var temporary = _path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var file = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                file.Write(bytes);
                file.Flush(flushToDisk: true);
            }
            if (current.Revision.Length == 0) File.Move(temporary, _path);
            else SavedSceneCleanupWindows.ReplaceReceipt(temporary, _path);
        }
        catch (Win32Exception error) { throw new IOException("Could not replace the saved bookmarks.", error); }
        finally
        {
            try { File.Delete(temporary); }
            catch (Exception error) when (SavedSceneWorkspace.IsLocalFailure(error)) { }
        }
    }

    private static bool ValidName(string name) => name.Length != 0 && name == name.Trim() &&
        NodeNames.IsValid(name) && Encoding.UTF8.GetByteCount(name) <= 64;

    private static bool Exact(JsonElement item, int count) =>
        item.ValueKind == JsonValueKind.Object && item.EnumerateObject().Count() == count;

    private static string? Text(JsonElement item, string name) =>
        item.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    private static bool Vector(JsonElement entry, string key, out Vector3 result)
    {
        result = default;
        if (!entry.TryGetProperty(key, out var item) || item.ValueKind != JsonValueKind.Array ||
            item.GetArrayLength() != 3) return false;
        Span<float> coordinates = stackalloc float[3];
        for (int i = 0; i < coordinates.Length; ++i)
            if (item[i].ValueKind != JsonValueKind.Number || !item[i].TryGetSingle(out coordinates[i])) return false;
        result = new(coordinates[0], coordinates[1], coordinates[2]);
        return true;
    }

    private static float[] Components(Vector3 vector) => [vector.X, vector.Y, vector.Z];

    private static IOException Invalid() => new("The bookmark store has an invalid version, scene scope or view.");
}
