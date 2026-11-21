using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Remote;
using Avalonia.Controls;
using Avalonia.Interactivity;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void SavedSceneChecks()
    {
        var attempts = new SavedSceneAttempts();
        var first = new SavedSceneInput("first", null);
        var second = new SavedSceneInput("second", null);
        var built = new SavedSceneBuild(first, "attempt", "runtime", new("source", "catalog"), "exe");
        Check(attempts.BeginBuild(first) && attempts.Building && !attempts.BeginBuild(second),
            "saved scenes admit only one active build and disallow Open");
        Check(attempts.BeginOpen(first) is null, "saved scenes do not open while building");
        attempts.FinishBuild(built);
        Check(attempts.BeginOpen(first) == built && attempts.Starting && !attempts.CanBuild(first) &&
            attempts.BeginOpen(first) is null, "saved scenes reserve the source before child process start");
        attempts.FinishOpen(first.Name, false);
        Check(attempts.CanOpen(first), "saved scene child start failure preserves a retryable result");
        _ = attempts.BeginOpen(first);
        attempts.FinishOpen(first.Name, true);
        Check(!attempts.CanOpen(first) && !attempts.CanBuild(first) && attempts.CanBuild(second),
            "live child guard applies to its source even after selecting another");
        attempts.ChildExited(first.Name);
        Check(attempts.CanOpen(first) && !attempts.CanOpen(first with { ManagedRoot = "other" }),
            "child exit releases its source and the result stays bound to its inputs");
        Check(attempts.BeginBuild(second) && attempts.Result is null, "admission clears an older eligible artifact");
        attempts.FinishBuild(null);
        Check(!attempts.CanOpen(first) && !attempts.CanOpen(second), "failed build has no fallback artifact");
        long obsolete = attempts.BeginCheck(), latest = attempts.BeginCheck();
        Check(!attempts.FinishCheck(obsolete, built) && attempts.FinishCheck(latest, built) && attempts.CanOpen(first),
            "only the latest saved-scene eligibility check can enable Open");
        long beforeBuild = attempts.BeginCheck();
        Check(attempts.BeginBuild(second) && !attempts.FinishCheck(beforeBuild, built),
            "Build invalidates an older selection check and its receipt");
        attempts.FinishBuild(built);
        var recentIdentity = new SavedSceneIdentity(first.Name, Guid.NewGuid());
        long pendingRecent = attempts.BeginRecentSelection();
        Check(!attempts.CanBuild(first) && !attempts.CanOpen(first), "recent selection checks cannot start Build or Open");
        Check(attempts.FinishRecentSelection(pendingRecent, null) && attempts.CanOpen(first) && attempts.RecentTarget is null,
            "a refused recent selection preserves the previous target");
        long oldRecent = attempts.BeginRecentSelection(), currentRecent = attempts.BeginRecentSelection();
        Check(!attempts.FinishRecentSelection(oldRecent, recentIdentity) && attempts.FinishRecentSelection(currentRecent, recentIdentity) &&
            attempts.RecentTarget == recentIdentity && !attempts.CanOpen(first), "only the current recent selection sets the identity guard");
        long beforeOrdinary = attempts.BeginRecentSelection();
        attempts.SelectOrdinary();
        Check(!attempts.FinishRecentSelection(beforeOrdinary, recentIdentity) && attempts.RecentTarget is null,
            "explicit ordinary selection cancels a late recent result and its old target guard");

        if (!OperatingSystem.IsWindows()) return;
        string root = Path.Combine(Path.GetTempPath(), "atlantis_saved_scene_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            string checkout = Path.Combine(root, "source checkout"), work = Path.Combine(root, "isolated work");
            string scenes = Path.Combine(checkout, "assets", "scenes"), saved = Path.Combine(scenes, "saved");
            Directory.CreateDirectory(scenes);
            File.WriteAllText(Path.Combine(checkout, "CMakeLists.txt"), "");
            var workspace = new SavedSceneWorkspace(SavedSceneWorkspace.CheckoutPath(checkout), "unused", work);
            Check(workspace.Discover().Missing, "missing saved directory differs from an empty or inaccessible list");
            Directory.CreateDirectory(saved);
            void Unit(string name, bool complete)
            {
                string path = Directory.CreateDirectory(Path.Combine(saved, name)).FullName;
                File.WriteAllText(Path.Combine(path, "scene.scene.txt"), "source");
                if (complete) File.WriteAllText(Path.Combine(path, "catalog-source.txt"), "catalog");
            }
            Unit("save_zeta", true);
            Unit("save_alpha", true);
            Unit("save_incomplete", false);
            Unit("SAVE_upper", true);
            Unit(".pending-private", true);
            Check(workspace.Discover().Names.SequenceEqual(new[] { "alpha", "zeta" }),
                "discovery sorts only ordinary complete canonical units");

            // The reference Windows machine permits symlinks. Other development machines can
            // run these model checks without elevation or Developer Mode.
            try
            {
                Directory.CreateSymbolicLink(Path.Combine(saved, "save_link"), Path.Combine(saved, "save_alpha"));
                Check(workspace.Discover().Names.SequenceEqual(new[] { "alpha", "zeta" }),
                    "discovery omits a reparse unit even when it resolves to two valid leaves");
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                Console.WriteLine("  skip  saved scene symlink fixture: " + e.Message);
            }

            string reply = Directory.CreateDirectory(Path.Combine(work, ".cmake", "api", "v1", "reply")).FullName;
            string exe = Path.Combine(Directory.CreateDirectory(Path.Combine(work, "Debug")).FullName, "atlantis_runtime.exe");
            File.WriteAllText(exe, "runtime");
            File.WriteAllText(Path.Combine(reply, "index-current.json"), """{"reply":{"codemodel-v2":{"jsonFile":"model.json"}}}""");
            File.WriteAllText(Path.Combine(reply, "model.json"),
                """{"configurations":[{"name":"Release","targets":[]},{"name":"Debug","targets":[{"name":"atlantis_runtime","jsonFile":"target.json"}]}]}""");
            string target = Path.Combine(reply, "target.json");
            const string valid = """{"type":"EXECUTABLE","artifacts":[{"path":"Debug/atlantis_runtime.exe"}]}""";
            File.WriteAllText(target, valid);
            Check(SavedSceneWorkspace.ResolveExecutable(work) == exe, "File API resolves the exact Debug Runtime artifact");
            foreach (string invalid in new[]
            {
                """{"type":"EXECUTABLE","artifacts":[{"path":"../atlantis_runtime.exe"}]}""",
                """{"type":"EXECUTABLE","artifacts":[{"path":"Debug/atlantis_runtime.exe"},{"path":"Other/atlantis_runtime.exe"}]}""",
                """{"type":"EXECUTABLE","artifacts":[{"path":"Missing/atlantis_runtime.exe"}]}""",
                """{"type":"EXECUTABLE","artifacts":[{"path":null}]}""",
                """{"type":"EXECUTABLE","artifacts":[{"path":""}]}""",
                """{"type":"EXECUTABLE","artifacts":[{}]}""",
                """{"type":"EXECUTABLE","artifacts":[{"path":"Debug/atlantis_runtime.exe"},{"path":42}]}""",
            })
            {
                File.WriteAllText(target, invalid);
                bool refused = false;
                try { _ = SavedSceneWorkspace.ResolveExecutable(work); }
                catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { refused = true; }
                Check(refused, "File API refuses an invalid artifact reply " + invalid);
            }
            string Hash(string value) => Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(
                System.Text.Encoding.UTF8.GetBytes(value)));
            var artifact = new SavedSceneBuild(new("alpha", null), work, exe, new(Hash("source"), Hash("catalog")), Hash("runtime"));
            Check(workspace.IsCurrent(artifact), "both source leaves and executable verify");
            File.WriteAllText(exe, "replaced");
            Check(!workspace.IsCurrent(artifact), "a replaced executable is stale");
            File.WriteAllText(exe, "runtime");
            File.WriteAllText(Path.Combine(saved, "save_alpha", "scene.scene.txt"), "saved edit");
            Check(!workspace.IsCurrent(artifact), "saving a source makes the old build stale");
            var child = SavedSceneWorkspace.ChildStart(Path.Combine(root, "Editor App", "Atlantis.Editor.exe"), exe);
            Check(!child.UseShellExecute && child.ArgumentList.SequenceEqual(new[] { "--runtime", exe, "--scene", "saved_scene" }),
                "child launch carries only its built Runtime path and saved scene alias");

            string slot = Path.Combine(root, "receipt"), recordedAttempts = Path.Combine(root, "attempts");
            var store = new SavedSceneReceiptStore(slot, recordedAttempts, checkout);
            var reopenedStore = new SavedSceneReceiptStore(slot, recordedAttempts, checkout);
            string recordedDirectory;
            using (store.Lease())
            {
                recordedDirectory = store.ReserveAttempt();
                var record = new SavedSceneBuildReceipt(SavedSceneReceiptStore.Version, checkout,
                    artifact with { Directory = recordedDirectory, Recorded = true }, new("scene", "assets", "settings"),
                    "configure", "sidecars");
                store.Write(recordedDirectory, record);
                Check(reopenedStore.Read() == record, "a new launcher reads the complete durable build receipt");
                bool busy = false;
                try { using var competing = reopenedStore.Lease(); }
                catch (IOException) { busy = true; }
                Check(busy, "same checkout cannot admit competing Build or Open while the lease is held");

                string slotFile = Path.Combine(slot, "last-build.json");
                using (File.OpenRead(slotFile))
                {
                    bool refused = false;
                    try { store.Write(recordedDirectory, null); }
                    catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { refused = true; }
                    Check(refused && reopenedStore.Read() == record,
                        "failed invalidation leaves the complete previous receipt and refuses Build admission");
                }
                string next = store.ReserveAttempt();
                store.Write(next, null);
                Check(reopenedStore.Read() is null && !reopenedStore.IsCurrentAttempt(recordedDirectory) &&
                    reopenedStore.IsCurrentAttempt(next), "a new admission supersedes even an in-memory fresh build from another launcher");
                store.Write(recordedDirectory, record with { Version = 99 });
                bool unsupported = false;
                try { _ = reopenedStore.Read(); }
                catch (IOException) { unsupported = true; }
                Check(unsupported, "an unknown receipt version cannot become eligible");
            }
            SavedSceneRecentChecks(root, checkout, work);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static void SavedSceneRecentChecks(string root, string checkout, string work)
    {
        string state = Path.Combine(root, "recent-state");
        var workspace = new SavedSceneWorkspace(checkout, "cmake-must-not-start", work, state);
        var history = new SavedSceneRecentStore(state);
        var otherLauncher = new SavedSceneRecentStore(state);
        var isolated = new SavedSceneRecentStore(Path.Combine(root, "another-checkout"));
        Check(history.Read().Length == 0 && !Directory.Exists(state), "reading an empty history does not create state");

        var scene = new SavedSceneIdentity("alpha", Guid.NewGuid());
        var moved = scene with { Name = "moved" };
        var replacement = scene with { Scene = Guid.NewGuid() };
        string saved = Path.Combine(checkout, "assets", "scenes", "saved");
        string Register(SavedSceneIdentity identity)
        {
            string unit = Directory.CreateDirectory(Path.Combine(saved, "save_" + identity.Name)).FullName;
            File.WriteAllText(Path.Combine(unit, "scene.scene.txt"), "source fixture");
            string catalog = Path.Combine(unit, "catalog-source.txt");
            File.WriteAllText(catalog, $"atlantis_asset_catalog_source_version: 1\nentry_count: 1\n" +
                $"asset: guid={identity.Scene:D} type=scene root=assets path=scenes/saved/save_{identity.Name}/scene.scene.txt\n");
            return catalog;
        }
        string catalog = Register(scene);
        Check(workspace.CheckRecent(scene) is null, "recent navigation resolves the registered name and GUID together");
        history.Record(scene);
        var observed = history.Read().Single();
        for (int i = 0; i < SavedSceneRecent.Limit; ++i) otherLauncher.Record(new("scene_" + i, Guid.NewGuid()));
        Check(history.Read().Length == SavedSceneRecent.Limit && history.Read().All(entry => entry.Identity != scene) &&
            history.Read()[0].Identity.Name == "scene_7" && isolated.Read().Length == 0,
            "settled records use newest-first order, evict only the oldest, and stay checkout-scoped");
        history.Record(scene);
        var initialVersion = history.Read()[0];
        otherLauncher.Record(moved);
        var entries = history.Read();
        Check(entries[0].Identity == moved && entries.Count(entry => entry.Identity.Scene == scene.Scene) == 1 &&
            !history.Remove(initialVersion) && !history.Remove(observed), "explicit open at a new location promotes the GUID and fences stale removal");
        Check(workspace.CheckRecent(moved)?.Contains("may have moved", StringComparison.Ordinal) == true,
            "an absent recent location remains unselectable, even when the GUID exists elsewhere");

        string source = Path.Combine(Path.GetDirectoryName(catalog)!, "scene.scene.txt");
        File.Move(source, source + ".tmp");
        Check(workspace.CheckRecent(scene)?.Contains("incomplete", StringComparison.Ordinal) == true,
            "an incomplete unit reports the location as missing or moved");
        File.Move(source + ".tmp", source);
        using (File.Open(catalog, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            Check(workspace.CheckRecent(scene)?.StartsWith("Cannot verify this scene location", StringComparison.Ordinal) == true,
                "an unreadable registration is distinct from a missing scene");

        var receipts = new SavedSceneReceiptStore(state, Path.Combine(state, "a"), checkout);
        string attempt;
        using (receipts.Lease())
        {
            attempt = receipts.ReserveAttempt();
            receipts.Write(attempt, null);
        }
        string slot = Path.Combine(state, "last-build.json");
        byte[] receiptBytes = File.ReadAllBytes(slot);
        Register(replacement);
        Check(workspace.CheckRecent(scene)?.Contains("different scene", StringComparison.Ordinal) == true,
            "the old recent row refuses a same-name replacement");
        var noBuild = workspace.BuildAsync(new(scene.Name, null), _ => throw new InvalidOperationException("Build started"), scene)
            .GetAwaiter().GetResult();
        Check(noBuild.Phase == "Stale" && File.ReadAllBytes(slot).SequenceEqual(receiptBytes),
            "recent Build rechecks identity before superseding the current build slot");
        string exe = Path.Combine(attempt, "atlantis_runtime.exe");
        File.WriteAllText(exe, "fixture executable");
        var current = new SavedSceneBuild(new(scene.Name, null), attempt, exe,
            new(SavedSceneWorkspace.Fingerprint(source), SavedSceneWorkspace.Fingerprint(catalog)), SavedSceneWorkspace.Fingerprint(exe));
        string missingAppHost = Path.Combine(root, "must-not-start.exe");
        var noOpen = workspace.Open(current, missingAppHost, scene);
        Check(noOpen.Child is null && noOpen.Refusal?.Contains("different scene", StringComparison.Ordinal) == true &&
            history.Read().SequenceEqual(entries), "recent Open refuses a replaced target even when its build matches the replacement");
        bool startFailed = false;
        try { _ = workspace.Open(current, missingAppHost); }
        catch (System.ComponentModel.Win32Exception) { startFailed = true; }
        Check(startFailed && history.Read().SequenceEqual(entries), "a failed child start never promotes the scene");

        string file = Path.Combine(state, "recent-scenes.json");
        byte[] complete = File.ReadAllBytes(file);
        using (File.OpenRead(file))
        {
            bool blocked = false;
            try { otherLauncher.Record(replacement); }
            catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { blocked = true; }
            Check(blocked && File.ReadAllBytes(file).SequenceEqual(complete), "failed atomic history publication retains the settled list");
        }
        File.WriteAllText(file, "{\"Version\":99,\"Entries\":[]}");
        byte[] unsupported = File.ReadAllBytes(file);
        bool refused = false;
        try { otherLauncher.Record(replacement); }
        catch (IOException) { refused = true; }
        Check(refused && File.ReadAllBytes(file).SequenceEqual(unsupported) && workspace.IsCurrent(current),
            "an unsupported history is preserved while the ordinary build remains usable");
        File.WriteAllText(file, "{");
        bool truncated = false;
        try { _ = otherLauncher.Read(); }
        catch (System.Text.Json.JsonException) { truncated = true; }
        Check(truncated && File.ReadAllText(file) == "{", "truncated history is reported without repair or eviction");
        File.WriteAllBytes(file, complete);
        using (File.Open(Path.Combine(state, "recent-scenes.lease"), FileMode.Open, FileAccess.ReadWrite, FileShare.None))
        {
            bool competing = false;
            try { otherLauncher.Record(replacement); }
            catch (IOException) { competing = true; }
            Check(competing && File.ReadAllBytes(file).SequenceEqual(complete), "a competing history writer cannot overwrite the settled list");
        }
        Check(workspace.RemoveRecent(entries[0]) && workspace.ReadRecent().All(entry => entry.Identity.Scene != scene.Scene) &&
            File.ReadAllBytes(slot).SequenceEqual(receiptBytes) && File.Exists(source) && File.Exists(exe),
            "Remove from Recent changes history only, leaving source, executable and receipt intact");
    }
    private static void SavedSceneCMakeChecks()
    {
        if (!OperatingSystem.IsWindows() || Environment.GetEnvironmentVariable("ATLANTIS_0090_TEST_CMAKE") is not { } cmake) return;
        string root = Path.Combine(Path.GetTempPath(), "atl_0090_" + Guid.NewGuid().ToString("N")[..10]);
        Directory.CreateDirectory(root);
        try
        {
            string checkout = Directory.CreateDirectory(Path.Combine(root, "fixture checkout")).FullName;
            string work = Directory.CreateDirectory(Path.Combine(root, "fixture work")).FullName;
            File.WriteAllText(Path.Combine(checkout, "CMakeLists.txt"), """
                cmake_minimum_required(VERSION 3.24)
                project(saved_scene_fixture LANGUAGES CXX)
                if(ATLANTIS_SAVED_SCENE STREQUAL "configure_fail")
                  message(FATAL_ERROR "controlled configure failure")
                endif()
                if(ATLANTIS_SAVED_SCENE STREQUAL "build_fail")
                  add_executable(atlantis_runtime broken.cpp)
                else()
                  add_executable(atlantis_runtime main.cpp)
                endif()
                """);
            File.WriteAllText(Path.Combine(checkout, "main.cpp"), "int main() {}");
            File.WriteAllText(Path.Combine(checkout, "broken.cpp"), "#error controlled build failure");
            string AddUnit(string name)
            {
                string unit = Directory.CreateDirectory(Path.Combine(checkout, "assets", "scenes", "saved", "save_" + name)).FullName;
                string source = Path.Combine(unit, "scene.scene.txt");
                File.WriteAllText(source, "original fixture source");
                File.WriteAllText(Path.Combine(unit, "catalog-source.txt"), "fixture catalog");
                return source;
            }
            AddUnit("configure_fail");
            AddUnit("build_fail");
            string changing = AddUnit("changes");
            AddUnit("valid");
            var workspace = new SavedSceneWorkspace(SavedSceneWorkspace.CheckoutPath(checkout), SavedSceneWorkspace.CMakePath(cmake), work,
                Path.Combine(root, "receipt"));
            SavedSceneBuildReport Build(string name, Action<SavedSceneBuildReport>? progress = null) =>
                workspace.BuildAsync(new(name, null), progress ?? (_ => { })).GetAwaiter().GetResult();
            var configured = Build("configure_fail");
            Check(configured.Phase == "Failed" && configured.Detail.Contains("Configure exited", StringComparison.Ordinal) &&
                File.ReadAllText(configured.Log).Contains("controlled configure failure", StringComparison.Ordinal),
                "CMake configure failure keeps its real stderr and prevents build");
            var broken = Build("build_fail");
            Check(broken.Phase == "Failed" && broken.Detail.Contains("Build exited", StringComparison.Ordinal) &&
                File.ReadAllText(broken.Log).Contains("controlled build failure", StringComparison.Ordinal),
                "CMake build failure keeps diagnostics and cannot produce an eligible executable");
            var stale = Build("changes", report =>
            {
                if (report.Phase == "Building") File.WriteAllText(changing, "changed during build");
            });
            Check(stale.Phase == "Stale" && stale.Result is null, "source changed during a real CMake build is stale");
            var clean = Build("valid");
            Check(clean.Result is { } artifact && workspace.IsCurrent(artifact) && File.Exists(clean.Log) &&
                File.ReadAllText(clean.Log).Contains("fixture checkout", StringComparison.Ordinal),
                "fresh CMake build handles paths with spaces and resolves the real executable");
            Check(new[] { configured.Directory, broken.Directory, stale.Directory, clean.Directory }.Distinct().Count() == 4,
                "CMake attempts never reuse a build directory");
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static async Task CheckSavedSceneOwnership(MainWindow window, List<NameUiCheck> checks)
    {
        var tabs = NameUiField<TabControl>(window, "_assetTabs")!;
        var list = NameUiField<ListBox>(window, "_savedList")!;
        var build = NameUiField<Button>(window, "_savedBuild")!;
        var open = NameUiField<Button>(window, "_savedOpen")!;
        var status = NameUiField<TextBlock>(window, "_savedOpenResult")!;
        var model = NameUiField<SavedSceneAttempts>(window, "_savedAttempts")!;
        void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        tabs.SelectedIndex = 2;
        typeof(MainWindow).GetMethod("ShowSavedSceneReceipt", System.Reflection.BindingFlags.Instance |
            System.Reflection.BindingFlags.NonPublic)!.Invoke(window,
            [new SavedSceneReceipt("first", new AssetGuid(Guid.NewGuid()), "scenes/saved/save_first/scene.scene.txt")]);
        checks.Add(new("same-name receipt remains a hint and cannot select a unit in this checkout",
            list.SelectedItem is null && !build.IsEnabled &&
            NameUiField<TextBlock>(window, "_savedHint")!.Text!.Contains("does not verify the checkout", StringComparison.Ordinal)));
        list.SelectedItem = "first";
        if (Environment.GetEnvironmentVariable("ATLANTIS_0090_INVALID_REPLY_MARKER") is { } marker)
        {
            Click(build);
            await NameUiWait(() => !model.Busy, "malformed artifact attempt settled", 90);
            var result = NameUiField<TextBox>(window, "_savedBuildResult")!;
            string failedDirectory = result.Text!.Split('\n').Single(line => line.StartsWith("Build directory: ", StringComparison.Ordinal))[17..];
            string log = File.ReadAllText(Path.Combine(failedDirectory, "build.log"));
            checks.Add(new("null artifact path reports Failed, retains both successful process exits and re-enables Build",
                result.Text.StartsWith("Failed: Checking output", StringComparison.Ordinal) &&
                result.Text.Contains("artifact path", StringComparison.OrdinalIgnoreCase) &&
                log.Contains("artifact path", StringComparison.OrdinalIgnoreCase) &&
                log.Split('\n').Count(line => line.TrimEnd() == "# exit 0") == 2 &&
                model.Result is null && build.IsEnabled && !open.IsEnabled));

            File.Delete(marker);
            Click(build);
            await NameUiWait(() => !model.Busy, "fresh retry settled", 90);
            checks.Add(new("retry of the same scene uses a fresh directory and enables Open",
                model.Result is { } retried && retried.Input.Name == "first" &&
                retried.Directory != failedDirectory && open.IsEnabled));
        }
        else
        {
            Click(build);
            await NameUiWait(() => !model.Busy, "fixture build settled", 90);
        }
        if (model.Result is null)
            throw new InvalidOperationException(NameUiField<TextBox>(window, "_savedBuildResult")?.Text);
        Click(open);
        window.Close();
        checks.Add(new("ordinary close is refused while Open is starting", model.Starting && window.IsVisible));
        await NameUiWait(() => !model.Starting, "failed start settled");
        checks.Add(new("a missing apphost fails to start and preserves Open eligibility",
            open.IsEnabled && status.Text!.Contains("start failed", StringComparison.Ordinal)));

        string apphost = Environment.GetEnvironmentVariable("ATLANTIS_0090_APPHOST")!;
        File.Move(apphost + ".withheld", apphost);
        string processName = Path.GetFileNameWithoutExtension(apphost);
        var oldPids = Process.GetProcessesByName(processName).Select(p => { using (p) return p.Id; }).ToHashSet();
        Click(open);
        await NameUiWait(() => !model.Starting, "successful start settled");
        list.SelectedItem = "second";
        bool otherCanBuild = build.IsEnabled;
        list.SelectedItem = "first";
        checks.Add(new("selecting away and back does not release the live child guard",
            otherCanBuild && !open.IsEnabled && !build.IsEnabled));
        using var child = Process.GetProcessesByName(processName).Single(p => !oldPids.Contains(p.Id));
        string pidPath = Environment.GetEnvironmentVariable("ATLANTIS_0090_CHILD_PID")!;
        File.WriteAllText(pidPath, child.Id.ToString(System.Globalization.CultureInfo.InvariantCulture));
        // The desktop test lifetime closes the launcher. Its runner checks this PID afterwards.
    }

    private static async Task CheckSavedSceneWorkflow(MainWindow window, List<NameUiCheck> checks, Action<string> work)
    {
        if (Environment.GetEnvironmentVariable("ATLANTIS_0124_TEST_PHASE") is { } recentPhase)
        {
            await CheckSavedSceneRecentUi(window, checks, work, recentPhase);
            return;
        }
        if (Environment.GetEnvironmentVariable("ATLANTIS_0123_TEST_PHASE") is { } phase)
        {
            await CheckSavedSceneRestart(window, checks, work, phase);
            return;
        }
        string checkout = Environment.GetEnvironmentVariable("ATLANTIS_0090_CHECKOUT")!;
        string managed = Environment.GetEnvironmentVariable("ATLANTIS_0090_MANAGED")!;
        string generation = Environment.GetEnvironmentVariable("ATLANTIS_0090_GENERATION")!;
        string tag = Environment.GetEnvironmentVariable("ATLANTIS_0090_TAG")!;
        await NameUiWait(() => NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")?.Count > 0,
            "original Editor authoring ready", 90);
        var runtime = NameUiField<RuntimeProcess>(window, "_runtime")!;
        work(runtime.Launch.WorkDirectory);
        using var original = await Task.Run(() => RemoteSession.Connect(SessionFile.Read(runtime.SessionPath)));
        var doc = (await Task.Run(() => original.Authoring.Document()))!;
        byte[] originalSource = File.ReadAllBytes(doc.SourcePath);
        var parent = (await Task.Run(() => original.Authoring.ListNodes())).Value.First(n =>
            original.Authoring.ListComponents(n.Node).Value.Any(type => original.Authoring.Schema
                .Any(s => s.Id == type && s.ShortName == "Renderable")));
        var mesh = (await Task.Run(() => original.Assets.ListAssets())).Value.Single(a =>
            a.Source == "content:managed_sample/source.gltf#mesh/0/0").Guid;
        var address = SceneSchema.AddressOf(original.Authoring.Schema, parent.Node, "Renderable", "meshAsset")!.Value;
        var entities = NameUiField<ListBox>(window, "_entities")!;
        entities.SelectedItem = NameUiField<IReadOnlyList<HierarchyRow>>(window, "_hierarchyRows")!.First(r => r.Node == parent.Node);
        var worker = NameUiField<EditorWorker>(window, "_worker")!;
        worker.CommitName(parent.Node, parent.Name, "Unsaved launcher edit 0090");
        worker.CommitField("Renderable.meshAsset", mesh.ToString());
        var undo = NameUiField<Button>(window, "_undo")!;
        await NameUiWait(async () => undo.IsEnabled &&
            (await Task.Run(() => original.Authoring.GetProperty(address))).Value.AsAsset() == mesh, "original edit with managed mesh");
        var originalStatus = (await Task.Run(() => original.Assets.RebuildStatus())).Value!;
        if (originalStatus.ActiveGeneration?.ToString() != generation)
            throw new InvalidOperationException("The original Editor did not use the intended generation.");

        var tabs = NameUiField<TabControl>(window, "_assetTabs")!;
        tabs.SelectedIndex = 2;
        var list = NameUiField<ListBox>(window, "_savedList")!;
        var refresh = NameUiField<Button>(window, "_savedRefresh")!;
        var build = NameUiField<Button>(window, "_savedBuild")!;
        var open = NameUiField<Button>(window, "_savedOpen")!;
        var hint = NameUiField<TextBlock>(window, "_savedHint")!;
        var resultText = NameUiField<TextBox>(window, "_savedBuildResult")!;
        var openText = NameUiField<TextBlock>(window, "_savedOpenResult")!;
        var model = NameUiField<SavedSceneAttempts>(window, "_savedAttempts")!;
        void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        void CheckUi(string label, bool ok)
        {
            checks.Add(new(label, ok, ok ? "" : resultText.Text + "\n" + openText.Text));
            if (!ok) throw new InvalidOperationException(label);
        }

        async Task Publish(string name, bool starter)
        {
            string prefix = starter ? "_newScene" : "_saveAs";
            NameUiField<TextBox>(window, prefix + "Name")!.Text = name;
            var publish = NameUiField<Button>(window, prefix)!;
            await NameUiWait(() => publish.IsEnabled, "publish action ready");
            Click(publish);
            await NameUiWait(() => hint.Text?.Contains("Receipt for " + name + ":", StringComparison.Ordinal) == true, "typed success hint");
            CheckUi("receipt does not select or start a build for " + name, list.SelectedItem is null && !build.IsEnabled);
            Click(refresh);
            list.SelectedItem = name;
            CheckUi("explicit selection enables Build for " + name, build.IsEnabled && !open.IsEnabled);
        }

        async Task<SavedSceneBuild?> Build(bool checkClose = false)
        {
            Click(build);
            if (checkClose)
            {
                Click(build);
                window.Close();
                CheckUi("duplicate Build, Open and close are guarded during build", model.Building && window.IsVisible &&
                    !build.IsEnabled && !open.IsEnabled && openText.Text!.Contains("close again", StringComparison.Ordinal));
            }
            await NameUiWait(() => !model.Busy, "CMake attempt settled", 420);
            return model.Result;
        }

        static string RuntimeLog(string path)
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var reader = new StreamReader(stream);
            return reader.ReadToEnd();
        }

        static string? FindChildWork(string executable)
        {
            foreach (string dir in Directory.EnumerateDirectories(Path.Combine(Path.GetTempPath(), "atlantis_editor")))
            {
                string log = Path.Combine(dir, "runtime.log");
                if (!File.Exists(log)) continue;
                try { if (RuntimeLog(log).StartsWith("# " + executable + " ", StringComparison.OrdinalIgnoreCase)) return dir; }
                catch (IOException) { }
            }
            return null;
        }

        async Task<Process> Open(SavedSceneBuild artifact)
        {
            int[] before = Process.GetProcessesByName("Atlantis.Editor").Select(p => { using (p) return p.Id; }).ToArray();
            Click(open);
            Click(open);
            await NameUiWait(() => !model.Starting, "child process admission");
            CheckUi("Open leaves a guarded independent child for " + artifact.Input.Name,
                model.HasChild(artifact.Input.Name) && !open.IsEnabled && !build.IsEnabled);
            return Process.GetProcessesByName("Atlantis.Editor").Single(p => !before.Contains(p.Id));
        }

        async Task Close(Process child, string name)
        {
            await NameUiWait(() => { child.Refresh(); return child.MainWindowHandle != IntPtr.Zero || child.HasExited; }, "child window available");
            if (!child.HasExited && !child.CloseMainWindow()) throw new InvalidOperationException("Cannot close child Editor.");
            await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(25));
            await NameUiWait(() => !model.HasChild(name), "child reservation released");
        }

        async Task EditAndSave(SavedSceneBuild artifact, bool starter)
        {
            using var child = await Open(artifact);
            try
            {
                string? childWork = null;
                await NameUiWait(() => (childWork = FindChildWork(artifact.Executable)) is not null &&
                    File.Exists(Path.Combine(childWork, "runtime.session.json")), "independent saved Runtime", 90);
                string sessionPath = Path.Combine(childWork!, "runtime.session.json");
                var info = SessionFile.Read(sessionPath);
                using var session = await Task.Run(() => RemoteSession.Connect(info));
                await NameUiWait(async () => (await Task.Run(() => session.Authoring.Document())) is not null,
                    "ordinary child opens authoring document", 60);
                var savedDoc = (await Task.Run(() => session.Authoring.Document()))!;
                string source = Path.Combine(checkout, "assets", "scenes", "saved", "save_" + artifact.Input.Name, "scene.scene.txt");
                string catalog = File.ReadAllText(Path.Combine(Path.GetDirectoryName(source)!, "catalog-source.txt"));
                string childLog = Path.Combine(childWork!, "runtime.log");
                string childArgs = RuntimeLog(childLog).Split('\n')[0];
                CheckUi("child binds exact saved source and identity with isolated session and no inherited generation",
                    Path.GetFullPath(savedDoc.SourcePath).Equals(Path.GetFullPath(source), StringComparison.OrdinalIgnoreCase) &&
                    catalog.Contains(savedDoc.Scene.ToString(), StringComparison.Ordinal) &&
                    !sessionPath.Equals(runtime.SessionPath, StringComparison.OrdinalIgnoreCase) &&
                    !childArgs.Contains("--asset-generation", StringComparison.Ordinal) &&
                    !childArgs.Contains("--allow-scene-save-as", StringComparison.Ordinal));
                var nodes = (await Task.Run(() => session.Authoring.ListNodes())).Value;
                var camera = (await Task.Run(() => session.Authoring.ActiveCamera())).Value;
                EntityGuid node = starter ? new(Guid.NewGuid()) : parent.Node;
                AssetGuid reference = starter ? (await Task.Run(() => session.Assets.ListAssets())).Value.Single(a =>
                    a.Source == "assets:meshes/minimal_cube.mesh.txt").Guid : mesh;
                string edited = starter ? "Added mesh 0090" : "Saved copy edit 0090";
                await Task.Run(() =>
                {
                    if (starter)
                        session.Authoring.SubmitTransaction(HierarchyCommands.AddMesh(session.Authoring.Schema,
                            (ulong)nodes.Count, node, reference).Commands);
                    session.Authoring.Submit(new SetNodeName(node, edited));
                    if (session.Authoring.DrainFailures().Count != 0 || !session.Authoring.ApplyTracked().IsOk)
                        throw new InvalidOperationException("The child edit or apply was refused.");
                });
                await NameUiWait(async () => (await Task.Run(() => session.Authoring.ApplyStatus())).Value?.Request?.State == ApplyState.Applied,
                    "child edit applies", 60);
                CheckUi("child saves and reopens " + artifact.Input.Name, await Task.Run(() =>
                    session.Authoring.Save() is null && session.Authoring.Open() is null &&
                    session.Authoring.Document()?.Scene == savedDoc.Scene &&
                    session.Authoring.ActiveCamera().Value == camera &&
                    session.Authoring.ListNodes().Value.Any(n => n.Node == node && n.Name == edited) &&
                    session.Authoring.GetProperty(SceneSchema.AddressOf(session.Authoring.Schema, node, "Renderable", "meshAsset")!.Value)
                        .Value.AsAsset() == reference));
                File.WriteAllText(Path.Combine(runtime.Launch.WorkDirectory, artifact.Input.Name + "-child.log"), RuntimeLog(childLog));
            }
            finally { await Close(child, artifact.Input.Name); }
            CheckUi("saving invalidates Open after child exit for " + artifact.Input.Name,
                !open.IsEnabled && build.IsEnabled && openText.Text!.StartsWith("Stale", StringComparison.Ordinal));
            list.SelectedItem = null;
        }

        await Publish(tag + "_starter", true);
        await EditAndSave(await Build(checkClose: true) ?? throw new InvalidOperationException(resultText.Text), true);
        await Publish(tag + "_copy", false);
        string copied = Path.Combine(checkout, "assets", "scenes", "saved", "save_" + tag + "_copy");
        byte[][] beforeFailure = Directory.GetFiles(copied).Order().Select(File.ReadAllBytes).ToArray();
        if (await Build() is { } missing)
        {
            using var child = await Open(missing);
            try
            {
                await NameUiWait(() => FindChildWork(missing.Executable) is { } dir &&
                    RuntimeLog(Path.Combine(dir, "runtime.log")).Contains("SceneDependencyLoadFailed", StringComparison.Ordinal),
                    "missing managed asset rejected by Runtime startup", 90);
                checks.Add(new("missing managed asset is a child startup failure after a successful build", true));
            }
            finally { await Close(child, missing.Input.Name); }
        }
        else
            CheckUi("missing managed asset is reported at the failing build phase", resultText.Text!.StartsWith("Failed:", StringComparison.Ordinal));
        CheckUi("failure preserves both copied files", Directory.GetFiles(copied).Order().Select(File.ReadAllBytes)
            .Zip(beforeFailure).All(pair => pair.First.SequenceEqual(pair.Second)));
        NameUiField<TextBox>(window, "_savedManaged")!.Text = managed;
        await NameUiWait(() => build.IsEnabled, "retry build ready");
        await EditAndSave(await Build() ?? throw new InvalidOperationException(resultText.Text), false);
        CheckUi("original Runtime, generation, source and unsaved history survive both paths",
            undo.IsEnabled && File.ReadAllBytes(doc.SourcePath).SequenceEqual(originalSource) &&
            (await Task.Run(() => original.Authoring.Document())) == doc &&
            (await Task.Run(() => original.Authoring.ListNodes())).Value.Any(n => n.Node == parent.Node && n.Name == "Unsaved launcher edit 0090") &&
            (await Task.Run(() => original.Assets.RebuildStatus())).Value!.ActiveGeneration?.ToString() == generation);
    }

    private static async Task CheckSavedSceneRestart(MainWindow window, List<NameUiCheck> checks, Action<string> work, string phase)
    {
        const string scene = "build_reuse_0123";
        EditorVisuals.Install(Avalonia.Application.Current!);
        string evidence = Environment.GetEnvironmentVariable("ATLANTIS_0123_EVIDENCE")!;
        string managed = Environment.GetEnvironmentVariable("ATLANTIS_0090_MANAGED")!;
        T Field<T>(string name) where T : class => NameUiField<T>(window, name)!;
        void Verify(string message, bool ok, string detail = "")
        {
            checks.Add(new(message, ok, detail));
            if (!ok) throw new InvalidOperationException(message + " " + detail);
        }
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        await NameUiWait(() => Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count > 0, "reference launcher is connected", 60);
        var original = Field<RuntimeProcess>("_runtime");
        work(original.Launch.WorkDirectory);
        using var originalSession = RemoteSession.Connect(SessionFile.Read(original.SessionPath));
        var document = originalSession.Authoring.Document();
        Verify("launcher hosted a separate authoring document", document is not null);
        var model = Field<SavedSceneAttempts>("_savedAttempts");
        var list = Field<ListBox>("_savedList");
        var open = Field<Button>("_savedOpen");
        var build = Field<Button>("_savedBuild");
        var resultText = Field<TextBox>("_savedBuildResult");
        var status = Field<TextBlock>("_savedOpenResult");
        Field<TabControl>("_assetTabs").SelectedIndex = 2;
        Field<TextBox>("_savedManaged").Text = managed;
        list.SelectedItem = scene;
        Verify("same saved unit explicitly selected", build.IsEnabled && !open.IsEnabled);
        string artifactPath = Path.Combine(evidence, "artifact.json");
        if (phase is "build" or "rebuild")
        {
            Click(build);
            await NameUiWait(() => !model.Busy, "recorded CMake build", 960);
            Verify("explicit build produced a reusable receipt", model.Result is { Recorded: true } && open.IsEnabled,
                resultText.Text ?? "");
            var artifact = model.Result!;
            File.WriteAllText(artifactPath, System.Text.Json.JsonSerializer.Serialize(artifact));
            string[] lines = File.ReadAllLines(Path.Combine(artifact.Directory, "build.log"));
            Verify("the explicit attempt ran one configure and one build", lines.Count(line => line.StartsWith("# [", StringComparison.Ordinal)) == 2 &&
                lines.Count(line => line == "# exit 0") == 2);
            if (phase == "build") return;
        }
        else
        {
            await NameUiWait(() => status.Text?.StartsWith("Checking", StringComparison.Ordinal) == false,
                "stored build eligibility", 180);
            if (phase == "stale")
            {
                Verify("changed scene source requires explicit Build", !open.IsEnabled && build.IsEnabled &&
                    status.Text!.Contains("source or catalog fragment changed", StringComparison.Ordinal), status.Text!);
                await WaitForSavedSceneCapture(evidence, phase);
                return;
            }
        }

        var expected = System.Text.Json.JsonSerializer.Deserialize<SavedSceneBuild>(File.ReadAllText(artifactPath))!;
        Verify("verified the exact previous attempt", model.Result == expected && open.IsEnabled, status.Text ?? "");
        if (phase == "reuse") await WaitForSavedSceneCapture(evidence, phase);
        string log = Path.Combine(expected.Directory, "build.log");
        byte[] buildLog = File.ReadAllBytes(log);
        string attempts = Path.GetDirectoryName(expected.Directory)!;
        var beforeAttempts = Directory.GetDirectories(attempts).Order().ToArray();
        var known = Process.GetProcessesByName("Atlantis.Editor").Select(proc => { using (proc) return proc.Id; }).ToHashSet();
        Click(open);
        await NameUiWait(() => !model.Starting, "validated child launch", 180);
        using var child = Process.GetProcessesByName("Atlantis.Editor").Single(proc => !known.Contains(proc.Id));
        try
        {
            string? childSession = null;
            await NameUiWait(() =>
            {
                foreach (string dir in Directory.EnumerateDirectories(Path.Combine(Path.GetTempPath(), "atlantis_editor")))
                {
                    string runtimeLog = Path.Combine(dir, "runtime.log");
                    string sessionPath = Path.Combine(dir, "runtime.session.json");
                    if (!File.Exists(runtimeLog) || !File.Exists(sessionPath)) continue;
                    using var reader = new StreamReader(new FileStream(runtimeLog, FileMode.Open, FileAccess.Read, FileShare.ReadWrite));
                    if (reader.ReadLine()?.StartsWith("# " + expected.Executable + " ", StringComparison.OrdinalIgnoreCase) == true)
                    { childSession = sessionPath; return true; }
                }
                return false;
            }, "the previous Runtime executable starts in an independent child", 120);
            using var session = RemoteSession.Connect(SessionFile.Read(childSession!));
            await NameUiWait(async () => await Task.Run(() => session.Authoring.Document() is not null), "child opened the saved document", 60);
            var reopened = session.Authoring.Document()!;
            var mesh = session.Assets.ListAssets().Value.Single(asset => asset.Source == "content:managed_sample/source.gltf#mesh/0/0").Guid;
            var renderable = session.Authoring.ListNodes().Value.First(node => node.Name == "Build reuse managed mesh");
            var property = SceneSchema.AddressOf(session.Authoring.Schema, renderable.Node, "Renderable", "meshAsset")!.Value;
            Verify("independent Editor opened the expected scene and retained the managed mesh reference",
                reopened.SourcePath.Contains("save_" + scene, StringComparison.Ordinal) &&
                session.Authoring.GetProperty(property).Value.AsAsset() == mesh && childSession != original.SessionPath);
            Verify("Open invoked no further configure or build", File.ReadAllBytes(log).SequenceEqual(buildLog) &&
                Directory.GetDirectories(attempts).Order().SequenceEqual(beforeAttempts));
            Verify("the launcher document and Runtime remain unchanged", originalSession.Authoring.Document()?.Scene == document!.Scene && !original.HasExited);
            string childLog = Path.Combine(Path.GetDirectoryName(childSession!)!, "runtime.log");
            using var stream = new FileStream(childLog, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var logReader = new StreamReader(stream);
            File.WriteAllText(Path.Combine(evidence, phase + "-child-runtime.log"), await logReader.ReadToEndAsync());
        }
        finally
        {
            if (!child.HasExited) child.CloseMainWindow();
            await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(30));
        }
    }

    private static async Task WaitForSavedSceneCapture(string evidence, string phase)
    {
        if (Environment.GetEnvironmentVariable("ATLANTIS_0123_CAPTURE") != "1") return;
        File.WriteAllText(Path.Combine(evidence, phase + ".ready"), Environment.ProcessId.ToString());
        await NameUiWait(() => File.Exists(Path.Combine(evidence, phase + ".captured")), "native screenshot", 90);
    }

    private static async Task CheckSavedSceneRecentUi(MainWindow window, List<NameUiCheck> checks, Action<string> work, string phase)
    {
        const string name = "recent_scene_0124";
        string evidence = Environment.GetEnvironmentVariable("ATLANTIS_0124_EVIDENCE")!;
        EditorVisuals.Install(Avalonia.Application.Current!);
        T Field<T>(string field) where T : class => NameUiField<T>(window, field)!;
        void Verify(string message, bool ok, string detail = "")
        {
            checks.Add(new(message, ok, detail));
            if (!ok) throw new InvalidOperationException(message + " " + detail);
        }
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        static int[] Children() => Process.GetProcessesByName("Atlantis.Editor").Select(p => { using (p) return p.Id; }).Order().ToArray();
        await NameUiWait(() => Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count > 0, "ordinary launcher ready", 90);
        var runtime = Field<RuntimeProcess>("_runtime");
        work(runtime.Launch.WorkDirectory);
        var tabs = Field<TabControl>("_assetTabs");
        var workspace = NameUiField<SavedSceneWorkspace>(window, "_savedWorkspace");
        if (phase == "disabled")
        {
            Verify("no launch authority means no Saved Scenes panel or history", workspace is null && tabs.Items.Count == 2);
            return;
        }
        ArgumentNullException.ThrowIfNull(workspace);
        tabs.SelectedIndex = 2;
        var all = Field<ListBox>("_savedList");
        var recent = Field<ListBox>("_savedRecent");
        var build = Field<Button>("_savedBuild");
        var open = Field<Button>("_savedOpen");
        var status = Field<TextBlock>("_savedOpenResult");
        var model = Field<SavedSceneAttempts>("_savedAttempts");
        var historyStatus = Field<TextBlock>("_savedRecentStatus");
        await NameUiWait(() => recent.ItemCount != 0 || !string.IsNullOrEmpty(historyStatus.Text), "history read settled");

        if (phase == "isolation")
        {
            Verify("a different trusted checkout with the same unit name has its own empty history",
                all.Items.Contains(name) && recent.ItemCount == 0 && historyStatus.Text == "No scenes opened yet.");
            return;
        }

        string checkout = Environment.GetEnvironmentVariable("ATLANTIS_0090_CHECKOUT")!;
        string unit = Path.Combine(checkout, "assets", "scenes", "saved", "save_" + name);
        string source = Path.Combine(unit, "scene.scene.txt");
        string catalog = Path.Combine(unit, "catalog-source.txt");
        string slot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AtlB",
            SavedSceneReceiptStore.Scope(checkout), "last-build.json");
        string artifactFile = Path.Combine(evidence, "artifact.json");
        var managed = Field<TextBox>("_savedManaged");
        string? managedInput = Environment.GetEnvironmentVariable("ATLANTIS_0090_MANAGED");
        if (managed.Text != managedInput)
        {
            var changed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            void ObserveChange(object? sender, TextChangedEventArgs args) => changed.TrySetResult();
            managed.TextChanged += ObserveChange;
            try
            {
                managed.Text = managedInput;
                await changed.Task.WaitAsync(TimeSpan.FromSeconds(10));
            }
            finally { managed.TextChanged -= ObserveChange; }
        }
        if (phase == "unavailable")
        {
            Verify("missing source leaves an unavailable recent entry visible", recent.ItemCount == 1 &&
                recent.Items[0]?.ToString()?.Contains("Unavailable", StringComparison.Ordinal) == true);
            byte[] savedReceipt = File.ReadAllBytes(slot), savedCatalog = File.ReadAllBytes(catalog);
            var pids = Children();
            recent.SelectedIndex = 0;
            await NameUiWait(() => !model.RecentSelectionPending, "missing recent selection refused");
            Verify("selecting the missing recent entry cannot enable actions", model.RecentTarget is null && !build.IsEnabled && !open.IsEnabled &&
                Field<TextBlock>("_savedRecentFeedback").Text?.Contains("may have moved", StringComparison.Ordinal) == true);
            await WaitForSavedSceneCapture(evidence, "unavailable");
            Click(Field<Button>("_savedRecentRemove"));
            await NameUiWait(() => recent.ItemCount == 0, "invalid shortcut removed");
            Verify("Remove changes history only", workspace.ReadRecent().Length == 0 && !File.Exists(source) &&
                File.ReadAllBytes(catalog).SequenceEqual(savedCatalog) && File.ReadAllBytes(slot).SequenceEqual(savedReceipt) && Children().SequenceEqual(pids));
            return;
        }

        SavedSceneBuild artifact;
        var prior = workspace.ReadRecent().SingleOrDefault();
        if (phase == "seed")
        {
            all.SelectedItem = name;
            Verify("ordinary selection leaves Recent empty", prior is null && build.IsEnabled && !open.IsEnabled);
            Click(build);
            await NameUiWait(() => !model.Busy, "saved scene build settled", 960);
            Verify("explicit Build produced a 0123 receipt and no recent entry", model.Result is { Recorded: true } && open.IsEnabled &&
                workspace.ReadRecent().Length == 0, Field<TextBox>("_savedBuildResult").Text ?? "");
            artifact = model.Result!;
            File.WriteAllText(artifactFile, System.Text.Json.JsonSerializer.Serialize(artifact));
        }
        else if (phase == "reseed")
        {
            artifact = System.Text.Json.JsonSerializer.Deserialize<SavedSceneBuild>(File.ReadAllText(artifactFile))!;
            all.SelectedItem = name;
            await NameUiWait(() => open.IsEnabled, "previous build verified from ordinary selection", 180);
            Verify("ordinary selection restores eligibility without writing empty history", model.Result == artifact && prior is null &&
                workspace.ReadRecent().Length == 0);
        }
        else
        {
            artifact = System.Text.Json.JsonSerializer.Deserialize<SavedSceneBuild>(File.ReadAllText(artifactFile))!;
            var before = Children();
            string log = Path.Combine(artifact.Directory, "build.log");
            byte[] buildLog = File.ReadAllBytes(log), oldReceipt = File.ReadAllBytes(slot);
            Verify("history survives launcher restart without activating a scene", recent.ItemCount == 1 && prior?.Identity.Name == name &&
                model.RecentTarget is null && !build.IsEnabled && !open.IsEnabled);
            recent.SelectedIndex = 0;
            await NameUiWait(() => open.IsEnabled, "recent scene and previous build verified", 180);
            Verify("Recent selects the original GUID and verifies the same 0123 result", model.RecentTarget == prior!.Identity && model.Result == artifact);
            Verify("selection starts neither a child nor a build", Children().SequenceEqual(before) && !model.Busy &&
                File.ReadAllBytes(log).SequenceEqual(buildLog) && File.ReadAllBytes(slot).SequenceEqual(oldReceipt) && workspace.ReadRecent().Single() == prior);
            if (phase == "stale")
            {
                byte[] bytes = File.ReadAllBytes(source);
                DateTime time = File.GetLastWriteTimeUtc(source);
                try
                {
                    File.AppendAllText(source, Environment.NewLine);
                    Click(open);
                    await NameUiWait(() => !model.Starting, "launch-time stale input refused", 180);
                    Verify("explicit Open rechecks 0123 after selection and refuses stale content", !open.IsEnabled && build.IsEnabled &&
                        status.Text!.Contains("source or catalog fragment changed", StringComparison.Ordinal) && Children().SequenceEqual(before) &&
                        workspace.ReadRecent().Single() == prior && File.ReadAllBytes(slot).SequenceEqual(oldReceipt), status.Text!);
                }
                finally { File.WriteAllBytes(source, bytes); File.SetLastWriteTimeUtc(source, time); }
                return;
            }
            await WaitForSavedSceneCapture(evidence, "reuse");
        }

        var known = Children();
        string runtimeWork = Path.Combine(Path.GetTempPath(), "atlantis_editor");
        var oldSessions = Directory.GetDirectories(runtimeWork).ToHashSet(StringComparer.OrdinalIgnoreCase);
        Click(open);
        await NameUiWait(() => !model.Starting, "checked child process start", 180);
        int childId = Children().Except(known).Single();
        using var child = Process.GetProcessById(childId);
        try
        {
            var recorded = workspace.ReadRecent().Single();
            Verify("successful explicit child start promotes exactly one history entry",
                recorded.Identity.Name == name && (prior is null || recorded.Identity == prior.Identity && recorded.Version != prior.Version));
            string? childLog = null;
            await NameUiWait(() =>
            {
                foreach (string dir in Directory.GetDirectories(runtimeWork).Where(dir => !oldSessions.Contains(dir)))
                {
                    string log = Path.Combine(dir, "runtime.log");
                    if (!File.Exists(log) || !File.Exists(Path.Combine(dir, "runtime.session.json"))) continue;
                    using var stream = new FileStream(log, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                    using var reader = new StreamReader(stream);
                    if (reader.ReadLine()?.StartsWith("# " + artifact.Executable + " ", StringComparison.OrdinalIgnoreCase) == true)
                    { childLog = log; return true; }
                }
                return false;
            }, "the expected independent Runtime is listening", 120);
            using var remote = RemoteSession.Connect(SessionFile.Read(Path.Combine(Path.GetDirectoryName(childLog!)!, "runtime.session.json")));
            await NameUiWait(async () => await Task.Run(() => remote.Authoring.Document() is not null), "child document opened", 60);
            var document = remote.Authoring.Document()!;
            Verify("independent Editor opened the exact saved source and GUID with the build receipt Runtime",
                string.Equals(Path.GetFullPath(document.SourcePath), Path.GetFullPath(source), StringComparison.OrdinalIgnoreCase) &&
                document.Scene.Value == recorded.Identity.Scene);
            using var logStream = new FileStream(childLog!, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var logReader = new StreamReader(logStream);
            File.WriteAllText(Path.Combine(evidence, phase + "-child-runtime.log"), await logReader.ReadToEndAsync());
        }
        finally
        {
            if (!child.HasExited) child.CloseMainWindow();
            await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(30));
        }
    }

}
