using System;
using System.IO;
using System.Runtime.Versioning;
using System.Text.Json;
using Atlantis.Editor.Model;

namespace Atlantis.Editor;

[SupportedOSPlatform("windows")]
internal sealed class WorkspaceLayoutStore(string directory)
{
    private const int SchemaVersion = 1, MaxBytes = 4096;
    internal string Pathname => Path.Combine(directory, "workspace-layout.json");

    internal static WorkspaceLayoutStore CurrentUser()
    {
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return new(string.IsNullOrEmpty(local) ? "" : Path.Combine(local, "Atlantis", "Editor"));
    }

    internal WorkspaceLayout Read()
    {
        CheckDirectory();
        if (!OrdinaryIfPresent(Pathname, directory: false)) return default;
        using var stream = new FileStream(Pathname, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        long length = stream.Length;
        if (length > MaxBytes) throw new IOException("The workspace layout record is too large.");
        // Keep parsing bounded even if an external writer retained a handle.
        byte[] contents = new byte[(int)length];
        stream.ReadExactly(contents);
        if (stream.ReadByte() != -1) throw new IOException("The workspace layout changed while reading.");
        using var document = JsonDocument.Parse(contents);
        if (document.RootElement.ValueKind != JsonValueKind.Object) throw Invalid();

        int version = 0, seen = 0;
        double? left = null, right = null;
        WorkspaceTab? tab = null;
        foreach (var field in document.RootElement.EnumerateObject())
        {
            int bit;
            switch (field.Name)
            {
                case "version" when field.Value.ValueKind == JsonValueKind.Number && field.Value.TryGetInt32(out version):
                    bit = 1;
                    break;
                case "hierarchyDip" when field.Value.ValueKind == JsonValueKind.Number && field.Value.TryGetDouble(out double hierarchy):
                    left = hierarchy;
                    bit = 2;
                    break;
                case "rightDip" when field.Value.ValueKind == JsonValueKind.Number && field.Value.TryGetDouble(out double workspace):
                    right = workspace;
                    bit = 4;
                    break;
                case "tab" when field.Value.ValueKind == JsonValueKind.String:
                    tab = field.Value.GetString() switch
                    {
                        "inspector" => WorkspaceTab.Inspector,
                        "assets" => WorkspaceTab.Assets,
                        "saved-scenes" => WorkspaceTab.SavedScenes,
                        _ => throw Invalid(),
                    };
                    bit = 8;
                    break;
                default: throw Invalid();
            }
            if ((seen & bit) != 0) throw Invalid();
            seen |= bit;
        }
        if (version != SchemaVersion || left.HasValue != right.HasValue) throw Invalid();
        WorkspaceSideWidths? widths = left.HasValue ? new(left.Value, right!.Value) : null;
        if (widths is { IsValid: false }) throw Invalid();
        return new(widths, tab);
    }

    internal void SetWidths(WorkspaceSideWidths widths)
    {
        if (!widths.IsValid) throw Invalid();
        using var lease = Lease();
        var current = Read();
        if (current.Preferred != widths) Publish(current with { Preferred = widths });
    }

    internal void SetTab(WorkspaceTab tab)
    {
        using var lease = Lease();
        var current = Read();
        if (current.Tab != tab) Publish(current with { Tab = tab });
    }

    internal void Reset()
    {
        using var lease = Lease();
        Publish(default);
    }

    private static IOException Invalid() => new("The workspace layout record is invalid or unsupported.");

    private void CheckDirectory()
    {
        if (!Path.IsPathFullyQualified(directory) || new DriveInfo(Path.GetPathRoot(directory)!).DriveType != DriveType.Fixed)
            throw new IOException("Workspace layout requires a local settings folder.");
        _ = OrdinaryIfPresent(directory, directory: true);
    }

    private FileStream Lease()
    {
        CheckDirectory();
        EnsureDirectory(directory);
        string path = Path.Combine(directory, "workspace-layout.lease");
        _ = OrdinaryIfPresent(path, directory: false);
        return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }

    private void Publish(WorkspaceLayout layout)
    {
        string stage = Path.Combine(directory, "layout-" + Guid.NewGuid().ToString("N") + ".tmp");
        bool staged = false;
        try
        {
            using (var stream = new FileStream(stage, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                staged = true;
                using (var json = new Utf8JsonWriter(stream))
                {
                    json.WriteStartObject();
                    json.WriteNumber("version", SchemaVersion);
                    if (layout.Preferred is { } widths)
                    {
                        json.WriteNumber("hierarchyDip", widths.Hierarchy);
                        json.WriteNumber("rightDip", widths.Right);
                    }
                    if (layout.Tab is { } tab)
                        json.WriteString("tab", tab switch
                        {
                            WorkspaceTab.Inspector => "inspector",
                            WorkspaceTab.Assets => "assets",
                            WorkspaceTab.SavedScenes => "saved-scenes",
                            _ => throw Invalid(),
                        });
                    json.WriteEndObject();
                }
                stream.Flush(flushToDisk: true);
            }
            _ = OrdinaryIfPresent(Pathname, directory: false);
            File.Move(stage, Pathname, overwrite: true);
            staged = false;
        }
        finally
        {
            if (staged)
            {
                try { File.Delete(stage); }
                catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { }
            }
        }
    }

    private static bool OrdinaryIfPresent(string path, bool directory)
    {
        try { SavedSceneWorkspace.Ordinary(path, directory); return true; }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { return false; }
    }

    private static void EnsureDirectory(string path)
    {
        if (OrdinaryIfPresent(path, directory: true)) return;
        EnsureDirectory(Path.GetDirectoryName(path) ?? throw new IOException("The local settings root is unavailable."));
        Directory.CreateDirectory(path);
        SavedSceneWorkspace.Ordinary(path, directory: true);
    }
}
