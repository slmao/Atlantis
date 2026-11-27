using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Microsoft.Win32.SafeHandles;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor;

internal sealed record RenderLookLibrary(string Revision, IReadOnlyList<RenderLookPreset> Presets);

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal sealed class RenderLookStore
{
    private const int Limit = 512 * 1024;
    private readonly string _scope;
    private readonly string _directory;
    private readonly string _path;

    internal RenderLookStore(DocumentState session)
    {
        _scope = ScopeFor(session);
        _directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Atlantis", "RenderLooks", "v1");
        _path = Path.Combine(_directory, _scope + ".json");
    }

    internal static string ScopeFor(DocumentState session)
    {
        if (session.Provenance != Provenance.HandAuthored || !Path.IsPathFullyQualified(session.Source))
            throw new IOException("A verified manual scene source is required.");
        var source = Path.GetFullPath(session.Source);
        SavedSceneWorkspace.Ordinary(source, directory: false);
        using var handle = File.OpenHandle(source, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        var normalized = new StringBuilder(32768);
        uint length = GetFinalPathNameByHandle(handle, normalized, (uint)normalized.Capacity, 0);
        if (length == 0 || length >= normalized.Capacity)
            throw new IOException("The physical scene source could not be verified.");
        return Convert.ToHexString(SHA256.HashData(
            Encoding.UTF8.GetBytes(session.Scene + "\n" + normalized.ToString().ToUpperInvariant())));
    }

    internal RenderLookLibrary Read()
    {
        try { SavedSceneWorkspace.Ordinary(_path, directory: false); }
        catch (FileNotFoundException) { return new("", []); }
        catch (DirectoryNotFoundException) { return new("", []); }
        using var stream = new FileStream(_path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        if (stream.Length is <= 0 or > Limit) throw new IOException("The render look store has an invalid size.");
        var bytes = new byte[(int)stream.Length];
        stream.ReadExactly(bytes);
        JsonValue value;
        try { value = Json.Parse(new UTF8Encoding(false, true).GetString(bytes)); }
        catch (DecoderFallbackException e) { throw new IOException("The render look store is not UTF-8.", e); }
        if (!value.IsObject || value.Members.Count != 3 ||
            value.Find("version") is not { } version || Codec.DecodeUInt64(version) != 1 ||
            value.Find("scope") is not { IsString: true } scope || scope.AsString != _scope ||
            value.Find("looks") is not { IsArray: true } looks || looks.Items.Count > 16)
            throw new IOException("The render look store version or scope could not be verified.");
        var presets = new List<RenderLookPreset>(looks.Items.Count);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in looks.Items)
        {
            RenderLookPreset? preset = Decode(entry);
            if (preset is null || !names.Add(preset.Name)) throw new IOException("The render look store contains an invalid entry.");
            presets.Add(preset);
        }
        return new(Convert.ToHexString(SHA256.HashData(bytes)), presets);
    }

    internal void Write(string expectedRevision, RenderLookPreset preset, bool replace)
    {
        ValidateName(preset.Name);
        EnsureDirectory(_directory);
        var lockPath = _path + ".lock";
        if (File.Exists(lockPath)) SavedSceneWorkspace.Ordinary(lockPath, directory: false);
        using var lease = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        var current = Read();
        if (current.Revision != expectedRevision) throw new IOException("The render look store changed. Refresh before saving.");
        var entries = current.Presets.ToList();
        int i = entries.FindIndex(e => string.Equals(e.Name, preset.Name, StringComparison.OrdinalIgnoreCase));
        if (i >= 0 && !replace) throw new IOException("This look name already exists. Confirm replacement before saving.");
        if (i >= 0) entries[i] = preset;
        else entries.Add(preset);
        if (entries.Count > 16) throw new IOException("The scene already has 16 saved looks.");
        var array = JsonValue.NewArray();
        foreach (var entry in entries) array.Push(Encode(entry));
        var bytes = Encoding.UTF8.GetBytes(Json.Write(JsonValue.NewObject().Set("version", JsonValue.Number(1UL))
            .Set("scope", JsonValue.String(_scope)).Set("looks", array)));
        if (bytes.Length > Limit) throw new IOException("The render look store exceeds 512 KiB.");
        var stage = _path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var stream = new FileStream(stage, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                stream.Write(bytes);
                stream.Flush(flushToDisk: true);
            }
            if (current.Revision.Length != 0) SavedSceneCleanupWindows.ReplaceReceipt(stage, _path);
            else File.Move(stage, _path);
        }
        catch (Win32Exception error)
        {
            throw new IOException("Could not replace the saved render looks. Refresh before retrying.", error);
        }
        finally
        {
            try { File.Delete(stage); }
            catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { }
        }
    }

    internal static void EnsureDirectory(string directory)
    {
        try { SavedSceneWorkspace.Ordinary(directory, directory: true); }
        catch (Exception e) when (e is DirectoryNotFoundException or FileNotFoundException)
        {
            EnsureDirectory(Path.GetDirectoryName(directory) ?? throw new IOException("The local data path is unavailable."));
            Directory.CreateDirectory(directory);
            SavedSceneWorkspace.Ordinary(directory, directory: true);
        }
    }

    private static void ValidateName(string name)
    {
        if (name.Length == 0 || name != name.Trim() || !NodeNames.IsValid(name) ||
            Encoding.UTF8.GetByteCount(name) > 64) throw new IOException("Enter a look name of up to 64 UTF-8 bytes.");
    }

    private static JsonValue Encode(RenderLookPreset preset)
    {
        var roster = JsonValue.NewArray();
        foreach (var light in preset.Basis.LightRoster) roster.Push(Codec.Encode(light));
        var values = JsonValue.NewArray();
        foreach (var write in preset.Values) values.Push(Codec.Encode(write));
        return JsonValue.NewObject().Set("name", JsonValue.String(preset.Name))
            .Set("activeCamera", Codec.Encode(preset.Basis.ActiveCamera)).Set("lightRoster", roster)
            .Set("placementSignature", JsonValue.String(preset.Basis.PlacementSignature)).Set("values", values);
    }

    private static RenderLookPreset? Decode(JsonValue entry)
    {
        if (!entry.IsObject || entry.Members.Count != 5 || entry.Find("name") is not { IsString: true } name ||
            entry.Find("activeCamera") is not { } id || Codec.DecodeEntity(id) is not EntityGuid camera ||
            entry.Find("lightRoster") is not { IsArray: true } roster || roster.Items.Count > RequireRenderLookDocument.MaxLights ||
            entry.Find("placementSignature") is not { IsString: true } placement ||
            placement.AsString.Length is 0 or > RequireRenderLookDocument.MaxPlacementBytes ||
            entry.Find("values") is not { IsArray: true } values || values.Items.Count != 8 + 3 * roster.Items.Count) return null;
        ValidateName(name.AsString);
        var active = new List<EntityGuid>();
        string? prior = null;
        foreach (var item in roster.Items)
        {
            if (Codec.DecodeEntity(item) is not EntityGuid light || light == camera) return null;
            var key = light.ToString();
            if (prior is not null && string.CompareOrdinal(prior, key) >= 0) return null;
            active.Add(light);
            prior = key;
        }
        var writes = new List<SetProperty>();
        var addresses = new HashSet<SceneAddress>();
        foreach (var item in values.Items)
        {
            if (Codec.DecodeAuthoringCommand(item) is not SetProperty write ||
                !RenderLookSnapshot.Finite(write.Value) || !addresses.Add(write.Address) ||
                (write.Address.Node != camera && !active.Contains(write.Address.Node))) return null;
            writes.Add(write);
        }
        return new(name.AsString, new(camera, active, placement.AsString), writes);
    }

    [DllImport("kernel32.dll", EntryPoint = "GetFinalPathNameByHandleW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetFinalPathNameByHandle(SafeFileHandle file, StringBuilder path, uint length, uint flags);
}
