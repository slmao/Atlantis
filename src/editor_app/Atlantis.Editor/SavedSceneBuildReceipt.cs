using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using Microsoft.Win32;
using Atlantis.Editor.Model;

namespace Atlantis.Editor;

internal sealed record SavedSceneEvidence(string Scene, string Assets, string Configuration);
internal sealed record SavedSceneBuildReceipt(int Version, string Checkout, SavedSceneBuild Build,
    SavedSceneEvidence Inputs, string ConfiguredInputs, string Outputs);
internal sealed record SavedSceneEligibility(SavedSceneBuild? Build, string Detail);

// Private to the local developer launcher. The receipt supplies evidence, never launch authority.
internal sealed class SavedSceneReceiptStore(string root, string attempts, string checkout)
{
    internal const int Version = 1;
    private const int MaxReceiptBytes = 16 * 1024;
    private sealed record SlotState(string Attempt, SavedSceneBuildReceipt? Receipt);
    internal static string Scope(string checkout) => Convert.ToHexString(
        SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(checkout).ToUpperInvariant())));

    private string Slot => Path.Combine(root, "last-build.json");

    internal FileStream Lease()
    {
        EnsureDirectory(root);
        string path = Path.Combine(root, "lease");
        if (File.Exists(path)) SavedSceneWorkspace.Ordinary(path, directory: false);
        return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }

    internal string ReserveAttempt()
    {
        EnsureDirectory(attempts);
        // MSBuild's native tracking paths are relative to this directory; keep it short.
        string leaf = Convert.ToHexString(RandomNumberGenerator.GetBytes(8));
        string attempt = Path.Combine(attempts, leaf);
        using var reservation = new FileStream(attempt + ".claim", FileMode.CreateNew, FileAccess.Write, FileShare.None);
        if (Directory.Exists(attempt)) throw new IOException("Build attempt directory already exists.");
        EnsureDirectory(attempt);
        return attempt;
    }

    private static void EnsureDirectory(string path)
    {
        try { SavedSceneWorkspace.Ordinary(path, directory: true); }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException)
        {
            EnsureDirectory(Path.GetDirectoryName(path) ?? throw new IOException("The local build root is unavailable."));
            Directory.CreateDirectory(path);
            SavedSceneWorkspace.Ordinary(path, directory: true);
        }
    }

    internal void Write(string attempt, SavedSceneBuildReceipt? receipt)
    {
        string stage = Path.Combine(root, "receipt-" + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(new SlotState(attempt, receipt));
            if (bytes.Length > MaxReceiptBytes) throw new IOException("The build receipt exceeds its size limit.");
            using (var stream = new FileStream(stage, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                stream.Write(bytes);
                stream.Flush(flushToDisk: true);
            }
            if (File.Exists(Slot)) SavedSceneWorkspace.Ordinary(Slot, directory: false);
            File.Move(stage, Slot, overwrite: true);
        }
        finally
        {
            try { File.Delete(stage); }
            catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { }
        }
    }

    internal SavedSceneBuildReceipt? Read() => ReadSlot()?.Receipt;
    internal bool IsCurrentAttempt(string attempt) => ReadSlot()?.Attempt == attempt;

    private SlotState? ReadSlot()
    {
        try { SavedSceneWorkspace.Ordinary(Slot, directory: false); }
        catch (FileNotFoundException) { return null; }
        catch (DirectoryNotFoundException) { return null; }
        using var stream = new FileStream(Slot, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        if (stream.Length > MaxReceiptBytes) throw new IOException("The recorded build receipt is too large.");
        var state = JsonSerializer.Deserialize<SlotState>(stream);
        if (state is null) return null;
        if (state.Attempt is not { } attempt) throw new IOException("Missing attempt in the recorded build slot.");
        string leaf = Path.GetFileName(attempt);
        if (Path.GetDirectoryName(attempt) != attempts || leaf.Length != 16 || !leaf.All(Uri.IsHexDigit))
            throw new IOException("The recorded attempt is outside the local build area.");
        if (state.Receipt is not { } receipt) return state;
        if (receipt.Version != Version || receipt.Checkout != checkout || receipt.Inputs is null ||
            receipt.Build is not { Recorded: true, Input: not null, Files: not null, Directory: not null, Executable: not null } build ||
            string.IsNullOrWhiteSpace(receipt.ConfiguredInputs) || string.IsNullOrWhiteSpace(receipt.Outputs))
            throw new IOException("The recorded build receipt has an unsupported version, checkout or shape.");
        if (build.Directory != attempt) throw new IOException("The completed build does not match the current attempt.");
        return state;
    }
}

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal sealed class SavedSceneBuildEvidence(string checkout, string cmake)
{
    private const int MaxEntries = 100_000;
    private static readonly string[] OverrideVariables = ["CMAKE_TOOLCHAIN_FILE", "CMAKE_PREFIX_PATH",
        "CMAKE_GENERATOR_INSTANCE", "CMAKE_GENERATOR_TOOLSET", "CMAKE_GENERATOR_PLATFORM", "CMAKE_INCLUDE_PATH", "CMAKE_LIBRARY_PATH",
        "CMAKE_PROGRAM_PATH", "CC", "CXX", "CL", "_CL_", "LINK", "_LINK_", "INCLUDE", "LIB",
        "LIBPATH", "WindowsSDKVersion", "WindowsSdkDir", "VCToolsInstallDir", "VCTargetsPath",
        "MSBuildExtensionsPath", "MSBuildUserExtensionsPath", "VULKAN_SDK_PATH"];
    private static readonly string[] EnvironmentInputs = ["PATH", "PATHEXT", "VULKAN_SDK", "SystemRoot",
        "ProgramFiles", "ProgramFiles(x86)", "ProgramData", "DOTNET_ROOT", "DOTNET_ROOT_X64", "CFLAGS", "CXXFLAGS", "LDFLAGS",
        "CMAKE_BUILD_PARALLEL_LEVEL", "PROCESSOR_ARCHITECTURE", "CMAKE_GENERATOR", "CMAKE_CONFIGURATION_TYPES"];
    private static readonly string[] ProjectTrees = ["cmake", "src", "shaders", "tests", "examples", "tools"];
    private static readonly string[] ProjectFiles = ["CMakeLists.txt", "Directory.Build.props", "global.json", "NuGet.config"];
    private static readonly string[] MsbuildOverrides = ["Directory.Build.props", "Directory.Build.targets",
        "Directory.Build.rsp", "Directory.Solution.props", "Directory.Solution.targets", "MSBuild.rsp"];

    private sealed record Tools(string VisualStudio, string VisualStudioVersion, string Msvc,
        string Kits, string Sdk, string Vulkan, string CMakeRoot, string VsWhere);

    private static string RunTool(string tool, string workingDirectory, params string[] arguments)
    {
        SavedSceneWorkspace.Ordinary(tool, directory: false);
        var start = new ProcessStartInfo(tool) { WorkingDirectory = workingDirectory, UseShellExecute = false,
            CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string argument in arguments) start.ArgumentList.Add(argument);
        using var process = Process.Start(start) ?? throw new IOException("Cannot start " + Path.GetFileName(tool) + ".");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(10_000)) { process.Kill(); throw new IOException("Tool discovery timed out: " + tool); }
        if (process.ExitCode != 0) throw new IOException("Tool discovery failed: " + tool);
        _ = stderr.GetAwaiter().GetResult();
        return stdout.GetAwaiter().GetResult();
    }

    private static string OrdinaryRoot(string path)
    {
        string full = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        SavedSceneWorkspace.Ordinary(full, directory: true);
        var drive = new DriveInfo(Path.GetPathRoot(full)!);
        if (drive.DriveType != DriveType.Fixed || drive.DriveFormat != "NTFS")
            throw new IOException("Expected a local NTFS input root: " + full);
        return full;
    }

    private Tools CurrentTools()
    {
        foreach (var name in OverrideVariables)
            if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable(name)))
                throw new IOException("Cannot verify the build environment override " + name + ".");

        string pf86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
        string vswhere = Path.Combine(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe");
        using var document = JsonDocument.Parse(RunTool(vswhere, checkout, "-products", "*", "-prerelease", "-version", "[18.0,19.0)",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json", "-utf8"));
        var instances = document.RootElement.EnumerateArray().ToArray();
        if (instances.Length != 1) throw new IOException("Expected one eligible Visual Studio 18 installation.");
        string Required(string key) => instances[0].GetProperty(key).GetString() ?? throw new IOException("Incomplete Visual Studio discovery.");
        string visualStudio = OrdinaryRoot(Required("installationPath"));
        string vc = Path.Combine(visualStudio, "VC");
        string msvc = OrdinaryRoot(Path.Combine(vc, "Tools", "MSVC", File.ReadAllText(Path.Combine(vc, "Auxiliary", "Build",
            "Microsoft.VCToolsVersion.default.txt")).Trim()));
        string kits = OrdinaryRoot(Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows Kits\Installed Roots",
            "KitsRoot10", null) as string ?? throw new IOException("Cannot resolve Windows SDK installation."));
        var sdkVersions = Directory.EnumerateDirectories(Path.Combine(kits, "Include"))
            .Select(Path.GetFileName).Where(name => System.Version.TryParse(name, out _)).ToArray();
        if (sdkVersions.Length != 1) throw new IOException("Expected one Windows SDK for the default profile.");
        string vulkan = OrdinaryRoot(Environment.GetEnvironmentVariable("VULKAN_SDK") ??
            throw new IOException("VULKAN_SDK is required to identify shader and Vulkan inputs."));
        string cmakeRoot = OrdinaryRoot(Path.GetDirectoryName(Path.GetDirectoryName(cmake)) ??
            throw new IOException("Cannot resolve CMake installation."));
        return new(visualStudio, Required("installationVersion"), msvc, kits, sdkVersions[0]!, vulkan, cmakeRoot, vswhere);
    }

    private sealed class Inventory(CancellationToken cancellation = default) : IDisposable
    {
        private readonly IncrementalHash _hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        private int _entries;

        internal void Add(string? value)
        {
            // NUL cannot occur in a path or environment value. Keep null distinct from empty.
            _hash.AppendData(Encoding.UTF8.GetBytes(value is null ? "-\0" : "+" + value + "\0"));
        }

        internal void File(string path)
        {
            cancellation.ThrowIfCancellationRequested();
            if (++_entries > MaxEntries) throw new IOException("The build evidence file count exceeds its limit.");
            Add(path);
            Add(SavedSceneWorkspace.Fingerprint(path));
        }

        internal void OptionalFile(string path)
        {
            try { File(path); }
            catch (FileNotFoundException) { Add("absent"); }
            catch (DirectoryNotFoundException) { Add("absent"); }
        }

        internal void Tree(string root, Func<string, bool>? skip = null)
        {
            cancellation.ThrowIfCancellationRequested();
            SavedSceneWorkspace.Ordinary(root, directory: true);
            Add(root);
            foreach (string path in Directory.EnumerateFileSystemEntries(root).Order(StringComparer.Ordinal))
            {
                if (skip?.Invoke(path) == true) continue;
                FileAttributes attributes = System.IO.File.GetAttributes(path);
                if (attributes.HasFlag(FileAttributes.ReparsePoint)) throw new IOException("Unsafe build input: " + path);
                if (attributes.HasFlag(FileAttributes.Directory)) Tree(path, skip);
                else File(path);
            }
        }

        internal void OptionalTree(string path)
        {
            try { SavedSceneWorkspace.Ordinary(path, directory: true); }
            catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException)
            { Add(path); Add("absent"); return; }
            Tree(path);
        }

        internal string Digest() => Convert.ToHexString(_hash.GetHashAndReset());
        public void Dispose() => _hash.Dispose();
    }

    internal string Scene(SavedSceneFiles files) => files.Source + ":" + files.Catalog;

    internal string Assets(SavedSceneInput input, CancellationToken cancellation = default)
    {
        using var inventory = new Inventory(cancellation);
        string assets = Path.Combine(checkout, "assets");
        // Other saved units are not read by this slot's configure/cook recipe.
        inventory.Tree(assets, path => SamePath(path, Path.Combine(assets, "scenes", "saved")));
        string bistro = Path.Combine(checkout, "content", "bistro");
        inventory.OptionalTree(bistro);
        if (input.ManagedRoot is { } managed) inventory.Tree(Path.Combine(OrdinaryRoot(managed), "registered"));
        return inventory.Digest();
    }

    internal string Configuration(CancellationToken cancellation = default)
    {
        cancellation.ThrowIfCancellationRequested();
        using var inventory = new Inventory(cancellation);
        foreach (string leaf in ProjectFiles) inventory.File(Path.Combine(checkout, leaf));
        var tools = CurrentTools();
        inventory.Add(SavedSceneWorkspace.Generator);
        inventory.Add("x64;Debug;atlantis_runtime");
        inventory.Add(tools.VisualStudioVersion);
        foreach (string key in EnvironmentInputs) { inventory.Add(key); inventory.Add(Environment.GetEnvironmentVariable(key)); }
        inventory.File(cmake);
        inventory.File(tools.VsWhere);
        inventory.Tree(Path.Combine(tools.CMakeRoot, "share"));
        inventory.Tree(Path.Combine(tools.VisualStudio, "MSBuild"));
        inventory.Tree(Path.Combine(tools.VisualStudio, "VC", "Auxiliary", "Build"));
        inventory.Tree(Path.Combine(tools.VisualStudio, "VC", "Tools", "MSVC"));
        foreach (string leaf in new[] { "Include", "Lib", "bin" }) inventory.Tree(Path.Combine(tools.Kits, leaf, tools.Sdk));
        foreach (string leaf in new[] { "Include", "Lib", "Bin" }) inventory.Tree(Path.Combine(tools.Vulkan, leaf));
        string git = SearchPath("git.exe") ?? throw new IOException("Cannot verify Git revision selection.");
        inventory.File(git);
        inventory.Add(RunTool(git, checkout, "rev-parse", "--short=12", "HEAD").Trim());
        inventory.Add(RunTool(git, checkout, "status", "--porcelain", "--untracked-files=no").Length == 0 ? "clean" : "dirty");
        if (SearchPath("dotnet.exe") is { } dotnet) inventory.File(dotnet);

        // Native target inputs live in these checkout trees. Ignore the managed projects' local bin/obj products.
        string[] managedProjects = [Path.Combine(checkout, "src", "editor_app"), Path.Combine(checkout, "src", "csharp"),
            Path.Combine(checkout, "src", "mcp")];
        foreach (string leaf in ProjectTrees)
            inventory.Tree(Path.Combine(checkout, leaf), path => managedProjects.Any(root => IsUnder(root, path)) &&
                (Path.GetFileName(path) is "bin" or "obj" or ".avalonia-build-tasks"));
        inventory.OptionalFile(Path.Combine(checkout, "Directory.Build.targets"));
        return inventory.Digest();
    }

    private static string? SearchPath(string name) => (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator)
        .Where(Path.IsPathFullyQualified).Select(path => Path.Combine(path, name)).FirstOrDefault(File.Exists);

    private static bool SamePath(string a, string b) => string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(a)),
        Path.TrimEndingDirectorySeparator(Path.GetFullPath(b)), StringComparison.OrdinalIgnoreCase);

    private static bool IsUnder(string root, string path) => Path.GetFullPath(path).StartsWith(
        Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    internal string Configured(string attempt, SavedSceneInput input, CancellationToken cancellation = default)
    {
        cancellation.ThrowIfCancellationRequested();
        Tools tools = CurrentTools();
        string cachePath = Path.Combine(attempt, "CMakeCache.txt");
        SavedSceneWorkspace.Ordinary(cachePath, directory: false);
        var cache = File.ReadLines(cachePath).Where(line => line.Length > 0 && line[0] != '#' && !line.StartsWith("//", StringComparison.Ordinal))
            .Select(line => line.Split('=', 2)).Where(parts => parts.Length == 2)
            .ToDictionary(parts => parts[0].Split(':', 2)[0], parts => parts[1], StringComparer.Ordinal);
        string Value(string name) => cache.GetValueOrDefault(name) ?? throw new IOException("Missing CMake setting " + name + ".");
        void RequirePath(string name, string path)
        {
            if (!SamePath(Value(name), path)) throw new IOException("Unexpected resolved CMake setting " + name + ".");
        }
        RequirePath("CMAKE_HOME_DIRECTORY", checkout);
        RequirePath("CMAKE_CACHEFILE_DIR", attempt);
        RequirePath("CMAKE_COMMAND", cmake);
        RequirePath("CMAKE_GENERATOR_INSTANCE", tools.VisualStudio);
        RequirePath("CMAKE_LINKER", Path.Combine(tools.Msvc, "bin", "Hostx64", "x64", "link.exe"));
        RequirePath("ATLANTIS_SLANGC_EXECUTABLE", Path.Combine(tools.Vulkan, "Bin", "slangc.exe"));
        RequirePath("ATLANTIS_SPIRV_VAL_EXECUTABLE", Path.Combine(tools.Vulkan, "Bin", "spirv-val.exe"));
        RequirePath("Vulkan_INCLUDE_DIR", Path.Combine(tools.Vulkan, "Include"));
        RequirePath("Vulkan_LIBRARY", Path.Combine(tools.Vulkan, "Lib", "vulkan-1.lib"));
        RequirePath("ATLANTIS_BISTRO_CONTENT_DIR", Path.Combine(checkout, "content", "bistro"));
        if (input.ManagedRoot is { } managed) RequirePath("ATLANTIS_MANAGED_IMPORT_ROOT", managed);
        else if (Value("ATLANTIS_MANAGED_IMPORT_ROOT").Length != 0) throw new IOException("Unexpected managed root.");
        if (Value("CMAKE_GENERATOR") != SavedSceneWorkspace.Generator || Value("CMAKE_GENERATOR_PLATFORM") != "x64" ||
            Value("CMAKE_GENERATOR_TOOLSET").Length != 0 || Value("ATLANTIS_SAVED_SCENE") != input.Name ||
            cache.Any(entry => entry.Key.StartsWith("FETCHCONTENT_SOURCE_DIR_", StringComparison.Ordinal) && entry.Value.Length != 0))
            throw new IOException("The CMake cache does not match the supported saved-slot profile.");

        using var inventory = new Inventory(cancellation);
        inventory.File(cachePath);
        string deps = Path.Combine(attempt, "_deps");
        foreach (string name in new[] { "stb", "imgui", "cgltf", "catch2" }) inventory.Tree(Path.Combine(deps, name + "-src"));
        string cmakeFiles = Path.Combine(attempt, "CMakeFiles", Value("CMAKE_CACHE_MAJOR_VERSION") + "." +
            Value("CMAKE_CACHE_MINOR_VERSION") + "." + Value("CMAKE_CACHE_PATCH_VERSION"));
        foreach (string file in new[] { "CMakeSystem.cmake", "CMakeCCompiler.cmake", "CMakeCXXCompiler.cmake", "VCTargetsPath.txt" })
            inventory.File(Path.Combine(cmakeFiles, file));
        foreach (var ancestor in Ancestors(attempt))
            foreach (string name in MsbuildOverrides) inventory.OptionalFile(Path.Combine(ancestor, name));
        string userMsbuild = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Microsoft", "MSBuild");
        inventory.OptionalTree(userMsbuild);
        string producers = Path.Combine(attempt, "assets", "producers-Debug.txt");
        inventory.File(producers);

        string reply = Path.Combine(attempt, ".cmake", "api", "v1", "reply");
        using var index = SavedSceneWorkspace.ReadJson(reply, SavedSceneWorkspace.FileApiIndex(reply));
        string inputsPath = SavedSceneWorkspace.FileApiPath(index.RootElement.GetProperty("reply").GetProperty("cmakeFiles-v1"),
            "jsonFile", "configure inputs path");
        using var inputs = SavedSceneWorkspace.ReadJson(reply, inputsPath);
        foreach (var entry in inputs.RootElement.GetProperty("inputs").EnumerateArray())
        {
            string path = Path.GetFullPath(SavedSceneWorkspace.FileApiPath(entry, "path", "configure input"), checkout);
            if (!IsUnder(checkout, path) && !IsUnder(attempt, path) && !IsUnder(tools.CMakeRoot, path) &&
                (input.ManagedRoot is not { } root || !IsUnder(Path.Combine(root, "registered"), path)))
                throw new IOException("Uncovered configure input: " + path);
            if (File.Exists(path)) inventory.File(path);
            else if (!entry.TryGetProperty("isGenerated", out var generated) || generated.ValueKind != JsonValueKind.True)
                throw new IOException("Cannot read configure input: " + path);
        }
        return inventory.Digest();
    }

    internal static string Outputs(string attempt, string executable, CancellationToken cancellation = default)
    {
        using var inventory = new Inventory(cancellation);
        if (!SamePath(executable, SavedSceneWorkspace.ResolveExecutable(attempt)))
            throw new IOException("The resolved Debug Runtime path changed.");
        inventory.File(executable);
        foreach (string dll in Directory.EnumerateFiles(Path.GetDirectoryName(executable)!, "*.dll").Order(StringComparer.Ordinal))
            inventory.File(dll);
        inventory.File(Path.Combine(attempt, "asset_catalog.txt"));
        inventory.Tree(Path.Combine(attempt, "assets"));
        inventory.Tree(Path.Combine(attempt, "shaders"));
        foreach (string imported in Directory.EnumerateDirectories(attempt, "managed_*").Order(StringComparer.Ordinal))
            inventory.Tree(imported);
        string bistro = Path.Combine(attempt, "bistro");
        inventory.OptionalTree(bistro);
        return inventory.Digest();
    }

    private static IEnumerable<string> Ancestors(string path)
    {
        for (string? current = path; current is not null; current = Path.GetDirectoryName(current)) yield return current;
    }
}
