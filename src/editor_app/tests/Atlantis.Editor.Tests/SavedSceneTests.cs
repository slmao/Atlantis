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
            })
            {
                File.WriteAllText(target, invalid);
                bool refused = false;
                try { _ = SavedSceneWorkspace.ResolveExecutable(work); }
                catch (Exception e) when (SavedSceneWorkspace.IsLocalFailure(e)) { refused = true; }
                Check(refused, "File API refuses out-of-tree, ambiguous or missing executable " + invalid);
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
        }
        finally { Directory.Delete(root, recursive: true); }
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
            var workspace = new SavedSceneWorkspace(SavedSceneWorkspace.CheckoutPath(checkout), SavedSceneWorkspace.CMakePath(cmake), work);
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
        list.SelectedItem = "first";
        Click(build);
        await NameUiWait(() => !model.Busy, "fixture build settled", 90);
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

        static string? FindChildWork(string executable)
        {
            foreach (string dir in Directory.EnumerateDirectories(Path.Combine(Path.GetTempPath(), "atlantis_editor")))
            {
                string log = Path.Combine(dir, "runtime.log");
                if (!File.Exists(log)) continue;
                try { if (File.ReadLines(log).FirstOrDefault()?.StartsWith("# " + executable + " ", StringComparison.OrdinalIgnoreCase) == true) return dir; }
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
                string childArgs = File.ReadLines(Path.Combine(childWork!, "runtime.log")).First();
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
                File.Copy(Path.Combine(childWork!, "runtime.log"), Path.Combine(runtime.Launch.WorkDirectory, artifact.Input.Name + "-child.log"));
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
                    File.ReadAllText(Path.Combine(dir, "runtime.log")).Contains("SceneDependencyLoadFailed", StringComparison.Ordinal),
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

}
