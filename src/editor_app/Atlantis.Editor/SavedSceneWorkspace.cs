using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Runtime.CompilerServices;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;

[assembly: InternalsVisibleTo("Atlantis.Editor.Tests")]

namespace Atlantis.Editor;

internal sealed record SavedSceneBuildReport(string Phase, string Detail, string Directory, string Log,
                                             SavedSceneBuild? Result = null);
internal sealed record SavedSceneOpenResult(Process? Child, string? Refusal, string? HistoryWarning = null);

[SupportedOSPlatform("windows")]
internal sealed class SavedSceneWorkspace(string checkout, string cmake, string workDirectory, string? receiptRoot = null)
{
    internal const string Generator = "Visual Studio 18 2026";
    private const string SourceLeaf = "scene.scene.txt", CatalogLeaf = "catalog-source.txt";
    private static readonly string BuildArea = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AtlB");
    private readonly string _root = Path.Combine(checkout, "assets", "scenes", "saved");
    private readonly SavedSceneReceiptStore _store = new(receiptRoot ?? Path.Combine(BuildArea, SavedSceneReceiptStore.Scope(checkout)),
        Path.Combine(receiptRoot ?? BuildArea, "a"), checkout);
    private readonly SavedSceneBuildEvidence _evidence = new(checkout, cmake);
    private readonly SavedSceneRecentStore _recent = new(receiptRoot ?? Path.Combine(BuildArea, SavedSceneReceiptStore.Scope(checkout)));
    private readonly SavedSceneBuildCleanup _cleanup = new(checkout,
        receiptRoot ?? Path.Combine(BuildArea, SavedSceneReceiptStore.Scope(checkout)), Path.Combine(receiptRoot ?? BuildArea, "a"));

    internal SavedSceneBuildCleanup.Listing CleanupCandidates() => _cleanup.List(_store.CurrentAttempt);
    internal SavedSceneBuildCleanup.Removal RemoveBuild(SavedSceneBuildCleanup.Candidate candidate) => _cleanup.Remove(candidate, _store);

    private sealed class ChangedSceneRegistration() : IOException("The saved scene registration changed after verification.");
    private sealed class UnidentifiedSceneRegistration(string message) : IOException(message);

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

    internal static void Ordinary(string path, bool directory)
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
        Ordinary(Path.Combine(unit, SourceLeaf), directory: false);
        Ordinary(Path.Combine(unit, CatalogLeaf), directory: false);
        // Check exact spellings, reject case aliases and require ordinary NTFS case-insensitive lookups.
        CanonicalEntry(_root, leaf, "SAVE_" + name);
        CanonicalEntry(unit, SourceLeaf, "SCENE.scene.txt");
        CanonicalEntry(unit, CatalogLeaf, "CATALOG-source.txt");
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

    internal static string Fingerprint(string path)
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

    internal SavedSceneRecentEntry[] ReadRecent() => _recent.Read();
    internal bool RemoveRecent(SavedSceneRecentEntry observed) => _recent.Remove(observed);

    private SavedSceneIdentity ReadSceneIdentity(string name, string? checkedCatalog = null)
    {
        // Check each ancestor before deciding that the expected location is absent.
        string parent = checkout;
        foreach (string segment in new[] { "assets", "scenes", "saved" })
        {
            parent = Path.Combine(parent, segment);
            Ordinary(parent, directory: true);
        }
        string unit = Unit(name);
        using var file = File.OpenRead(Path.Combine(unit, CatalogLeaf));
        if (file.Length > 4096)
        {
            if (checkedCatalog is not null && Convert.ToHexString(SHA256.HashData(file)) != checkedCatalog)
                throw new ChangedSceneRegistration();
            throw new UnidentifiedSceneRegistration("The saved scene registration is too large.");
        }
        var bytes = new byte[(int)file.Length];
        file.ReadExactly(bytes);
        if (checkedCatalog is not null && Convert.ToHexString(SHA256.HashData(bytes)) != checkedCatalog)
            throw new ChangedSceneRegistration();

        using var lines = new StringReader(new UTF8Encoding(false, true).GetString(bytes));
        const string prefix = "asset: guid=";
        string suffix = $" type=scene root=assets path=scenes/saved/save_{name}/{SourceLeaf}";
        if (lines.ReadLine() != "atlantis_asset_catalog_source_version: 1" || lines.ReadLine() != "entry_count: 1" ||
            lines.ReadLine() is not { } entry || lines.ReadLine() is not null ||
            !entry.StartsWith(prefix, StringComparison.Ordinal) || !entry.EndsWith(suffix, StringComparison.Ordinal) ||
            entry.Length != prefix.Length + 36 + suffix.Length ||
            !AssetGuid.TryParse(entry.Substring(prefix.Length, 36), out var guid) || guid.IsNil)
            throw new UnidentifiedSceneRegistration("The saved scene registration does not identify this unit.");
        return new(name, guid.Value);
    }

    internal string? CheckRecent(SavedSceneIdentity expected, string? checkedCatalog = null)
    {
        try { _ = CheckoutPath(checkout); }
        catch (Exception e) when (IsLocalFailure(e)) { return "Cannot verify the checkout. " + e.Message; }
        try
        {
            return ReadSceneIdentity(expected.Name, checkedCatalog) == expected ? null :
                "This location now contains a different scene.";
        }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException)
        { return "The scene is missing, incomplete, or may have moved."; }
        catch (Exception e) when (IsLocalFailure(e)) { return "Cannot verify this scene location. " + e.Message; }
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

    private SavedSceneInput Normalize(SavedSceneInput input) => input.ManagedRoot is { } managed
        ? input with { ManagedRoot = ManagedPath(managed) } : input;

    internal SavedSceneEligibility CheckStored(SavedSceneInput input, CancellationToken cancellation = default)
    {
        try { return VerifyStored(Normalize(input), cancellation); }
        catch (Exception e) when (IsLocalFailure(e)) { return Unavailable("Cannot verify the previous build. " + e.Message); }
    }

    internal SavedSceneEligibility CheckFresh(SavedSceneBuild result)
    {
        try
        {
            if (!_store.IsCurrentAttempt(result.Directory)) return Unavailable("The build was superseded.");
            return IsCurrent(result) ? new(result, "Ready to open the build from this session.") :
                Unavailable("The saved unit or executable changed or is unavailable.");
        }
        catch (Exception e) when (IsLocalFailure(e)) { return Unavailable("Cannot verify the current build. " + e.Message); }
    }

    private SavedSceneEligibility VerifyStored(SavedSceneInput input, CancellationToken cancellation = default)
    {
        var receipt = _store.Read();
        if (receipt is null || receipt.Build.Input.Name != input.Name)
            return Unavailable("No recorded build for the selected scene.");
        var build = receipt.Build;
        if (!string.Equals(build.Input.ManagedRoot, input.ManagedRoot, StringComparison.OrdinalIgnoreCase))
            return Unavailable("The committed managed input setting changed.");
        string phase = "saved scene inputs";
        try
        {
            var files = Files(input.Name);
            if (files != build.Files || _evidence.Scene(files) != receipt.Inputs.Scene)
                return Unavailable("The saved scene source or catalog fragment changed.");
            phase = "asset inputs";
            if (_evidence.Assets(input, cancellation) != receipt.Inputs.Assets) return Unavailable("Asset inputs changed.");
            phase = "build configuration";
            if (_evidence.Configuration(cancellation) != receipt.Inputs.Configuration ||
                _evidence.Configured(build.Directory, input, cancellation) != receipt.ConfiguredInputs)
                return Unavailable("Build configuration or tool inputs changed.");
            phase = "Runtime executable";
            if (Fingerprint(Under(build.Directory, build.Executable)) != build.ExecutableFingerprint)
                return Unavailable("The Runtime executable changed.");
            phase = "Runtime sidecar outputs";
            if (SavedSceneBuildEvidence.Outputs(build.Directory, build.Executable, cancellation) != receipt.Outputs)
                return Unavailable("Runtime assets or shader outputs changed.");
            phase = "current build receipt";
            if (_store.Read() != receipt) return Unavailable("The build was superseded.");
            return new(build, "Ready to open the recorded build.");
        }
        catch (Exception e) when (IsLocalFailure(e)) { return Unavailable($"Cannot verify {phase}. {e.Message}"); }
    }

    private static SavedSceneEligibility Unavailable(string reason) => new(null, reason + " Build again.");

    internal SavedSceneOpenResult Open(SavedSceneBuild result, string apphost, SavedSceneIdentity? recent = null,
        Action? onBuildVerified = null, bool allowPrefabRegistration = false, bool projectAssets = false)
    {
        var started = StartChecked(result, apphost, recent, onBuildVerified, allowPrefabRegistration, projectAssets, out var identity);
        if (started.Child is null || identity is null) return started;
        try { _recent.Record(identity); }
        catch (Exception e) when (IsLocalFailure(e))
        { return started with { HistoryWarning = "Recent history could not be updated. " + e.Message }; }
        return started;
    }

    private SavedSceneOpenResult StartChecked(SavedSceneBuild result, string apphost, SavedSceneIdentity? recent, Action? onBuildVerified,
        bool allowPrefabRegistration, bool projectAssets, out SavedSceneIdentity? identity)
    {
        identity = null;
        FileStream lease;
        try { lease = _store.Lease(); }
        catch (Exception e) when (IsLocalFailure(e))
        { return new(null, "Cannot verify the current build. Another launcher may be using the build slot. " + e.Message); }
        using var held = lease;
        SavedSceneBuildReceipt? verifiedReceipt = null;
        if (result.Recorded)
        {
            verifiedReceipt = _store.Read();
            var eligibility = VerifyStored(result.Input);
            if (eligibility.Build != result || verifiedReceipt?.Build != result)
                return new(null, eligibility.Build is null ? eligibility.Detail : "The build was superseded. Build again.");
        }
        else if (CheckFresh(result) is { Build: null } ineligible) return new(null, ineligible.Detail);

        onBuildVerified?.Invoke();
        string? warning = null;
        try { identity = ReadSceneIdentity(result.Input.Name, result.Files.Catalog); }
        catch (ChangedSceneRegistration e) { return new(null, e.Message + " Build again."); }
        catch (Exception e) when (e is UnidentifiedSceneRegistration or DecoderFallbackException)
        {
            if (recent is not null) return new(null, "Cannot confirm the selected recent scene. " + e.Message);
            warning = "The scene identity could not be verified for Recent. " + e.Message;
        }
        catch (Exception e) when (IsLocalFailure(e))
        { return new(null, "Cannot verify the saved scene files before opening. " + e.Message + " Build again."); }
        if (recent is not null && identity != recent) return new(null, "This location now contains a different scene.");
        try
        {
            if (verifiedReceipt is not null && _store.Read() != verifiedReceipt)
                return new(null, "The build was superseded. Build again.");
            if (verifiedReceipt?.Version == SavedSceneReceiptStore.CleanupVersion)
                _cleanup.Protect(result.Directory);
        }
        catch (Exception e) when (IsLocalFailure(e))
        { return new(null, "Cannot record build protection before opening. " + e.Message); }
        return new(Process.Start(ChildStart(apphost, result.Executable, allowPrefabRegistration, projectAssets)) ??
            throw new IOException("The Editor process did not start."),
            null, warning);
    }

    internal async Task<SavedSceneBuildReport> BuildAsync(SavedSceneInput input, Action<SavedSceneBuildReport> progress,
        SavedSceneIdentity? recent = null)
    {
        string phase = "Admitting Build";
        string? retainedLog = null;
        string attempt = workDirectory;
        try
        {
            using var lease = _store.Lease();
            SavedSceneFiles? recentFiles = null;
            if (recent is not null)
            {
                if (recent.Name != input.Name) return new("Stale", "The recent selection changed.", workDirectory, "");
                recentFiles = Files(input.Name);
                if (CheckRecent(recent, recentFiles.Catalog) is { } refusal)
                    return new("Stale", refusal, workDirectory, "");
            }
            attempt = _store.ReserveAttempt();
            _store.Write(attempt, null);
            bool tracked = _cleanup.Reserve(attempt);
            using var job = tracked ? new SavedSceneCleanupWindows.BuildJob() : null;
            bool succeeded = false;
            phase = "Preparing";
            string logPath = Path.Combine(attempt, "build.log");
            Directory.CreateDirectory(attempt);
            Ordinary(attempt, directory: true);
            using var writer = new StreamWriter(logPath) { AutoFlush = true };
            TextWriter log = TextWriter.Synchronized(writer);
            retainedLog = logPath;
            try
            {
                input = Normalize(input);
                SavedSceneFiles files = recentFiles ?? Files(input.Name);
                string? configuration = Capture(() => _evidence.Configuration(), out string? reason);
                string? assets = Capture(() => _evidence.Assets(input), out string? assetReason);
                reason ??= assetReason;
                Ordinary(cmake, directory: false);
                string query = Path.Combine(attempt, ".cmake", "api", "v1", "query");
                Directory.CreateDirectory(query);
                File.WriteAllText(Path.Combine(query, "codemodel-v2"), "");
                File.WriteAllText(Path.Combine(query, "cmakeFiles-v1"), "");
                string[] configure = ["-S", checkout, "-B", attempt, "-G", Generator, "-A", "x64",
                                      "-DATLANTIS_SAVED_SCENE=" + input.Name];
                if (input.ManagedRoot is { } managed)
                    configure = [.. configure, "-DATLANTIS_MANAGED_IMPORT_ROOT=" + ManagedPath(managed)];

                phase = "Configuring";
                progress(new(phase, "Configuring the saved slot", attempt, logPath));
                int configured = await RunAsync(configure, log, job);
                if (configured != 0) return new("Failed", $"Configure exited with code {configured}.", attempt, logPath);

                string? VerifyInputs() =>
                    Files(input.Name) != files ? "The saved unit changed during the build." :
                    assets is not null && _evidence.Assets(input) != assets ? "Asset inputs changed during the build." :
                    configuration is not null && _evidence.Configuration() != configuration ? "Build configuration changed during the build." : null;

                SavedSceneBuildReport Stale(string message) => new("Stale", message + " Build again.", attempt, logPath);
                string? CheckInputs()
                {
                    try { return VerifyInputs(); }
                    catch (Exception e) when (IsLocalFailure(e))
                    { return "Build inputs became unverifiable. " + e.Message; }
                }
                if (CheckInputs() is { } prebuildFailure) return Stale(prebuildFailure);
                string? resolved = Capture(() => _evidence.Configured(attempt, input), out string? resolvedReason);
                reason ??= resolvedReason;

                phase = "Building";
                progress(new(phase, "Building atlantis_runtime (Debug)", attempt, logPath));
                int built = await RunAsync(["--build", attempt, "--config", "Debug", "--target", "atlantis_runtime"], log, job);
                if (built != 0) return new("Failed", $"Build exited with code {built}.", attempt, logPath);

                phase = "Checking output";
                string executable = ResolveExecutable(attempt);
                string fingerprint = Fingerprint(executable);
                if (CheckInputs() is { } postbuildFailure) return Stale(postbuildFailure);
                if (resolved is not null && Capture(() => _evidence.Configured(attempt, input), out _) != resolved)
                    return Stale("Effective build configuration changed during the build.");

                var build = new SavedSceneBuild(input, attempt, executable, files, fingerprint);
                if (configuration is not null && assets is not null && resolved is not null)
                {
                    string? outputs = Capture(() => SavedSceneBuildEvidence.Outputs(attempt, executable), out reason);
                    if (outputs is null) return Stale("Cannot verify the required Runtime outputs. " + reason);
                    try
                    {
                        var recorded = build with { Recorded = true };
                        int version = tracked ? SavedSceneReceiptStore.CleanupVersion : SavedSceneReceiptStore.Version;
                        _store.Write(attempt, new(version, checkout, recorded,
                            new(_evidence.Scene(files), assets, configuration), resolved, outputs));
                        build = recorded;
                    }
                    catch (Exception e) when (IsLocalFailure(e)) { reason = e.Message; }
                }
                if (tracked && !build.Recorded) _cleanup.Protect(attempt);
                string detail = build.Recorded ? "Build succeeded and was recorded for reuse." :
                    "Build succeeded for this session. Reuse after restart unavailable. " + reason;
                log.WriteLine(detail);
                succeeded = true;
                return new("Succeeded", detail, attempt, logPath, build);
            }
            catch (Exception e) when (IsLocalFailure(e))
            {
                log.WriteLine(e.Message);
                throw;
            }
            finally
            {
                if (job is not null && await job.ObserveIdleAsync()) _cleanup.Settle(attempt, succeeded);
            }
        }
        catch (Exception e) when (IsLocalFailure(e))
        {
            return new("Failed", phase + ": " + e.Message, retainedLog is null ? workDirectory : attempt, retainedLog ?? "");
        }
    }

    private static string? Capture(Func<string> read, out string? failure)
    {
        try { failure = null; return read(); }
        catch (Exception e) when (IsLocalFailure(e)) { failure = e.Message; return null; }
    }

    private async Task<int> RunAsync(string[] arguments, TextWriter log, SavedSceneCleanupWindows.BuildJob? job)
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
        async Task Drain(StreamReader reader, string prefix)
        {
            while (await reader.ReadLineAsync() is { } line) log.WriteLine(prefix + line);
        }
        using var tracked = job?.TryStart(start);
        int exitCode;
        if (tracked is not null)
        {
            Task<int> exit = tracked.ExitCodeAsync();
            await Task.WhenAll(Task.Run(() => Drain(tracked.Output, "out ")), Task.Run(() => Drain(tracked.Error, "err ")), exit);
            exitCode = await exit;
        }
        else
        {
            using Process process = Process.Start(start) ?? throw new IOException("CMake did not start.");
            await Task.WhenAll(Drain(process.StandardOutput, "out "), Drain(process.StandardError, "err "), process.WaitForExitAsync());
            exitCode = process.ExitCode;
        }
        log.WriteLine("# exit " + exitCode);
        return exitCode;
    }

    private static string Under(string parent, string path)
    {
        string full = Path.GetFullPath(path, parent);
        string prefix = Path.TrimEndingDirectorySeparator(parent) + Path.DirectorySeparatorChar;
        if (!full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Build output is outside its attempt directory: " + full);
        return full;
    }

    internal static JsonDocument ReadJson(string directory, string path)
    {
        string full = Under(directory, path);
        Ordinary(full, directory: false);
        return JsonDocument.Parse(File.ReadAllBytes(full));
    }

    internal static string FileApiPath(JsonElement entry, string property, string description)
    {
        if (entry.ValueKind == JsonValueKind.Object && entry.TryGetProperty(property, out var value) &&
            value.ValueKind == JsonValueKind.String && value.GetString() is { } path &&
            !string.IsNullOrWhiteSpace(path) && path.IndexOfAny(Path.GetInvalidPathChars()) < 0)
            return path;
        throw new IOException("Invalid CMake File API " + description + ".");
    }

    internal static string FileApiIndex(string reply)
    {
        Ordinary(reply, directory: true);
        return Directory.EnumerateFiles(reply, "index-*.json").OrderDescending(StringComparer.Ordinal).FirstOrDefault()
            ?? throw new IOException("CMake produced no File API reply.");
    }

    internal static string ResolveExecutable(string attempt)
    {
        string reply = Path.Combine(attempt, ".cmake", "api", "v1", "reply");
        using var index = ReadJson(reply, FileApiIndex(reply));
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

    internal static ProcessStartInfo ChildStart(string editor, string runtime, bool allowPrefabRegistration = false, bool projectAssets = false)
    {
        var start = new ProcessStartInfo(editor) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(editor)! };
        start.ArgumentList.Add("--runtime");
        start.ArgumentList.Add(runtime);
        start.ArgumentList.Add("--scene");
        start.ArgumentList.Add("saved_scene");
        if (projectAssets) start.ArgumentList.Add("--project-assets");
        if (allowPrefabRegistration) start.ArgumentList.Add("--allow-prefab-registration");
        return start;
    }

    internal static bool IsLocalFailure(Exception e) => e is IOException or UnauthorizedAccessException or
        ArgumentException or Win32Exception or JsonException or InvalidOperationException or System.Security.SecurityException or
        System.Collections.Generic.KeyNotFoundException;
}
