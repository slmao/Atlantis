using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay.Remote;

[assembly: InternalsVisibleTo("Atlantis.Editor.Tests")]

namespace Atlantis.Editor;

internal sealed record SavedSceneBuildReport(string Phase, string Detail, string Directory, string Log,
                                             SavedSceneBuild? Result = null);

internal sealed class SavedSceneWorkspace(string checkout, string cmake, string workDirectory)
{
    private const string Generator = "Visual Studio 18 2026";
    private const string SourceLeaf = "scene.scene.txt", CatalogLeaf = "catalog-source.txt";
    private readonly string _root = Path.Combine(checkout, "assets", "scenes", "saved");

    internal static string CheckoutPath(string path)
    {
        string full = Absolute(path);
        var drive = new DriveInfo(Path.GetPathRoot(full)!);
        if (drive.DriveType != DriveType.Fixed || drive.DriveFormat != "NTFS")
            throw new IOException("Select a local NTFS checkout.");
        Ordinary(full, directory: true);
        CanonicalEntry(full, "CMakeLists.txt", "CMAKELISTS.TXT");
        Ordinary(Path.Combine(full, "CMakeLists.txt"), directory: false);
        return full;
    }

    internal static string CMakePath(string path)
    {
        string full = Absolute(path);
        Ordinary(full, directory: false);
        if (new DriveInfo(Path.GetPathRoot(full)!).DriveType != DriveType.Fixed || !Path.GetExtension(full).Equals(".exe", StringComparison.OrdinalIgnoreCase))
            throw new IOException("Select a local CMake executable.");
        return full;
    }

    internal static string ManagedPath(string path)
    {
        string full = Absolute(path);
        Ordinary(full, directory: true);
        return full;
    }

    private static string Absolute(string path) => Path.IsPathFullyQualified(path)
        ? Path.TrimEndingDirectorySeparator(Path.GetFullPath(path))
        : throw new IOException("An absolute local path is required.");

    private static void Ordinary(string path, bool directory)
    {
        string? current = path;
        while (current is not null)
        {
            FileAttributes attributes = File.GetAttributes(current);
            if (attributes.HasFlag(FileAttributes.ReparsePoint) ||
                attributes.HasFlag(FileAttributes.Directory) != directory)
                throw new IOException("Expected an ordinary " + (directory ? "directory" : "file") + ": " + current);
            current = Path.GetDirectoryName(current);
            directory = true;
        }
    }

    internal (string[] Names, bool Missing) Discover()
    {
        Ordinary(Path.GetDirectoryName(_root)!, directory: true);
        try { Ordinary(_root, directory: true); }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { return ([], true); }

        var names = Directory.EnumerateFileSystemEntries(_root)
            .Select(Path.GetFileName)
            .Where(leaf => leaf is not null && leaf.StartsWith("save_", StringComparison.Ordinal) &&
                           Codec.ValidSaveAsName(leaf[5..]))
            .Select(leaf => leaf![5..])
            .Where(name =>
            {
                try { _ = Unit(name); return true; }
                catch (IOException) { return false; }
            })
            .Order(StringComparer.Ordinal)
            .ToArray();
        return (names, false);
    }

    private string Unit(string name)
    {
        if (!Codec.ValidSaveAsName(name)) throw new IOException("Invalid saved scene name.");
        string leaf = "save_" + name;
        string unit = Path.Combine(_root, leaf);
        Ordinary(unit, directory: true);
        // Check exact spellings, reject case aliases and require ordinary NTFS case-insensitive lookups.
        CanonicalEntry(_root, leaf, "SAVE_" + name);
        CanonicalEntry(unit, SourceLeaf, "SCENE.scene.txt");
        CanonicalEntry(unit, CatalogLeaf, "CATALOG-source.txt");
        Ordinary(Path.Combine(unit, SourceLeaf), directory: false);
        Ordinary(Path.Combine(unit, CatalogLeaf), directory: false);
        return unit;
    }

    private static void CanonicalEntry(string parent, string leaf, string alternate)
    {
        var matches = Directory.EnumerateFileSystemEntries(parent)
            .Select(Path.GetFileName).Where(entry => string.Equals(entry, leaf, StringComparison.OrdinalIgnoreCase))
            .Take(2).ToArray();
        if (matches.Length != 1 || matches[0] != leaf)
            throw new IOException("Missing or noncanonical entry: " + Path.Combine(parent, leaf));
        _ = File.GetAttributes(Path.Combine(parent, alternate));
    }

    private static string Fingerprint(string path)
    {
        Ordinary(path, directory: false);
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    private SavedSceneFiles Files(string name)
    {
        string unit = Unit(name);
        return new(Fingerprint(Path.Combine(unit, SourceLeaf)), Fingerprint(Path.Combine(unit, CatalogLeaf)));
    }

    internal bool IsCurrent(SavedSceneBuild result)
    {
        try
        {
            return Files(result.Input.Name) == result.Files &&
                Fingerprint(Under(result.Directory, result.Executable)) == result.ExecutableFingerprint;
        }
        catch (Exception e) when (IsLocalFailure(e)) { return false; }
    }

    internal async Task<SavedSceneBuildReport> BuildAsync(SavedSceneInput input, Action<SavedSceneBuildReport> progress)
    {
        string attempt = Path.Combine(workDirectory, "saved-build-" + Guid.NewGuid().ToString("N"));
        string logPath = Path.Combine(attempt, "build.log");
        string phase = "Preparing";
        string? retainedLog = null;
        try
        {
            Directory.CreateDirectory(attempt);
            using var writer = new StreamWriter(logPath) { AutoFlush = true };
            TextWriter log = TextWriter.Synchronized(writer);
            retainedLog = logPath;
            try
            {
                SavedSceneFiles files = Files(input.Name);
                Ordinary(cmake, directory: false);
                string query = Path.Combine(attempt, ".cmake", "api", "v1", "query");
                Directory.CreateDirectory(query);
                File.WriteAllText(Path.Combine(query, "codemodel-v2"), "");
                string[] configure = ["-S", checkout, "-B", attempt, "-G", Generator, "-A", "x64",
                                      "-DATLANTIS_SAVED_SCENE=" + input.Name];
                if (input.ManagedRoot is { } managed)
                    configure = [.. configure, "-DATLANTIS_MANAGED_IMPORT_ROOT=" + ManagedPath(managed)];

                phase = "Configuring";
                progress(new(phase, "Configuring the saved slot", attempt, logPath));
                int configured = await RunAsync(configure, log);
                if (configured != 0) return new("Failed", $"Configure exited with code {configured}.", attempt, logPath);

                phase = "Building";
                progress(new(phase, "Building atlantis_runtime (Debug)", attempt, logPath));
                int built = await RunAsync(["--build", attempt, "--config", "Debug", "--target", "atlantis_runtime"], log);
                if (built != 0) return new("Failed", $"Build exited with code {built}.", attempt, logPath);

                phase = "Checking output";
                string executable = ResolveExecutable(attempt);
                string fingerprint = Fingerprint(executable);
                try
                {
                    if (Files(input.Name) != files)
                        return new("Stale", "The saved unit changed during the build. Build again.", attempt, logPath);
                }
                catch (Exception e) when (IsLocalFailure(e))
                {
                    log.WriteLine(e.Message);
                    return new("Stale", "The saved unit is unavailable after the build. Build again.", attempt, logPath);
                }
                return new("Succeeded", "Configure and build exited with code 0.", attempt, logPath,
                    new(input, attempt, executable, files, fingerprint));
            }
            catch (Exception e) when (IsLocalFailure(e))
            {
                log.WriteLine(e.Message);
                throw;
            }
        }
        catch (Exception e) when (IsLocalFailure(e))
        {
            return new("Failed", phase + ": " + e.Message, attempt, retainedLog ?? "");
        }
    }

    private async Task<int> RunAsync(string[] arguments, TextWriter log)
    {
        var start = new ProcessStartInfo(cmake)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            WorkingDirectory = checkout,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (string arg in arguments) start.ArgumentList.Add(arg);
        log.WriteLine("# " + JsonSerializer.Serialize(new[] { cmake }.Concat(arguments)));
        using Process process = Process.Start(start) ?? throw new IOException("CMake did not start.");
        async Task Drain(StreamReader reader, string prefix)
        {
            while (await reader.ReadLineAsync() is { } line) log.WriteLine(prefix + line);
        }
        await Task.WhenAll(Drain(process.StandardOutput, "out "), Drain(process.StandardError, "err "), process.WaitForExitAsync());
        log.WriteLine("# exit " + process.ExitCode);
        return process.ExitCode;
    }

    private static string Under(string parent, string path)
    {
        string full = Path.GetFullPath(path, parent);
        string prefix = Path.TrimEndingDirectorySeparator(parent) + Path.DirectorySeparatorChar;
        if (!full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Build output is outside its attempt directory: " + full);
        return full;
    }

    private static JsonDocument ReadJson(string directory, string path)
    {
        string full = Under(directory, path);
        Ordinary(full, directory: false);
        return JsonDocument.Parse(File.ReadAllBytes(full));
    }

    private static string FileApiPath(JsonElement entry, string property, string description)
    {
        if (entry.ValueKind == JsonValueKind.Object && entry.TryGetProperty(property, out var value) &&
            value.ValueKind == JsonValueKind.String && value.GetString() is { } path &&
            !string.IsNullOrWhiteSpace(path) && path.IndexOfAny(Path.GetInvalidPathChars()) < 0)
            return path;
        throw new IOException("Invalid CMake File API " + description + ".");
    }

    internal static string ResolveExecutable(string attempt)
    {
        string reply = Path.Combine(attempt, ".cmake", "api", "v1", "reply");
        Ordinary(reply, directory: true);
        string indexPath = Directory.EnumerateFiles(reply, "index-*.json").OrderDescending(StringComparer.Ordinal).FirstOrDefault()
            ?? throw new IOException("CMake produced no File API reply.");
        using var index = ReadJson(reply, indexPath);
        string modelPath = FileApiPath(index.RootElement.GetProperty("reply").GetProperty("codemodel-v2"),
                                       "jsonFile", "codemodel path");
        using var model = ReadJson(reply, modelPath);
        var debug = model.RootElement.GetProperty("configurations").EnumerateArray()
            .Single(config => config.GetProperty("name").GetString() == "Debug");
        var target = debug.GetProperty("targets").EnumerateArray()
            .Single(item => item.GetProperty("name").GetString() == "atlantis_runtime");
        using var metadata = ReadJson(reply, FileApiPath(target, "jsonFile", "target reply path"));
        if (metadata.RootElement.GetProperty("type").GetString() != "EXECUTABLE")
            throw new IOException("CMake did not report an executable target.");
        if (!metadata.RootElement.TryGetProperty("artifacts", out var artifacts) || artifacts.ValueKind != JsonValueKind.Array)
            throw new IOException("CMake File API artifacts must be an array.");
        var runtimeArtifacts = artifacts.EnumerateArray()
            .Select(item => FileApiPath(item, "path", "artifact path"))
            .Where(path => string.Equals(Path.GetFileName(path), "atlantis_runtime.exe", StringComparison.OrdinalIgnoreCase))
            .ToArray();
        if (runtimeArtifacts.Length != 1)
            throw new IOException("CMake File API must report exactly one atlantis_runtime.exe artifact.");
        string executable = Under(attempt, runtimeArtifacts[0]);
        Ordinary(executable, directory: false);
        return executable;
    }

    internal static ProcessStartInfo ChildStart(string editor, string runtime)
    {
        var start = new ProcessStartInfo(editor) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(editor)! };
        start.ArgumentList.Add("--runtime");
        start.ArgumentList.Add(runtime);
        start.ArgumentList.Add("--scene");
        start.ArgumentList.Add("saved_scene");
        return start;
    }

    internal static bool IsLocalFailure(Exception e) => e is IOException or UnauthorizedAccessException or
        ArgumentException or Win32Exception or JsonException or InvalidOperationException or System.Collections.Generic.KeyNotFoundException;
}
