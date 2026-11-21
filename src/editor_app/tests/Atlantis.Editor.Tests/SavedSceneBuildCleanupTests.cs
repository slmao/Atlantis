using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay.Remote;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    private static void SavedSceneBuildCleanupChecks()
    {
        string root = Path.Combine(Path.GetTempPath(), "atlantis-cleanup-" + Guid.NewGuid().ToString("N"));
        string checkout = Path.Combine(root, "checkout"), scope = Path.Combine(root, "scope"), attempts = Path.Combine(root, "attempts");
        Directory.CreateDirectory(checkout);
        File.WriteAllText(Path.Combine(checkout, "CMakeLists.txt"), "# identity fixture");
        var store = new SavedSceneReceiptStore(scope, attempts, checkout);
        var cleanup = new SavedSceneBuildCleanup(checkout, scope, attempts);
        string Attempt(bool tracked = true, bool settled = true)
        {
            using var lease = store.Lease();
            string attempt = store.ReserveAttempt();
            if (tracked && !cleanup.Reserve(attempt)) throw new InvalidOperationException("Could not record fixture ownership.");
            File.WriteAllText(Path.Combine(attempt, "build.log"), "data");
            if (tracked && settled) cleanup.Settle(attempt, succeeded: true);
            return attempt;
        }
        string Log(string path) => Path.Combine(path, "build.log");
        SavedSceneBuildCleanup.Listing List() => cleanup.List(store.CurrentAttempt);
        SavedSceneBuildCleanup.Candidate Candidate(string attempt) => List().Candidates.Single(row => row.Path == attempt);

        try
        {
            using (var job = new SavedSceneCleanupWindows.BuildJob())
            {
                var start = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe"))
                    { WorkingDirectory = root };
                foreach (string arg in new[] { "-NoProfile", "-Command",
                             "Start-Process ping.exe '-n 7 127.0.0.1' -WindowStyle Hidden -PassThru | ForEach-Object Id" })
                    start.ArgumentList.Add(arg);
                using var tracked = job.TryStart(start);
                int exit = tracked?.ExitCodeAsync().GetAwaiter().GetResult() ?? -1;
                bool active = !job.Settled();
                string diagnostics = exit == 0 ? "" : tracked?.Error.ReadToEnd() ?? "tracked process did not start";
                Check(exit == 0 && active, $"the Windows build job still witnesses a descendant after the initial tool exits ({exit}, {diagnostics})");
                Check(tracked is not null && System.Threading.SpinWait.SpinUntil(job.Settled, TimeSpan.FromSeconds(10)),
                    "the job can settle once its last descendant exits");
            }

            string old = Attempt(), current = Attempt(), legacy = Attempt(tracked: false), running = Attempt(settled: false);
            store.Write(current, null);
            string slot = Path.Combine(scope, "last-build.json");
            byte[] prior = File.ReadAllBytes(slot);
            var initial = List();
            Check(initial.Issue is null && initial.Candidates.Count == 1 && initial.Candidates[0].Path == old &&
                initial.Candidates[0].Size == 4 && File.Exists(Log(current)) && File.Exists(Log(legacy)) && File.Exists(Log(running)),
                "cleanup lists only the owned, settled, superseded tree and reports its file lengths: " + initial.Issue);
            var selected = Candidate(old);
            store.Write(old, null);
            Check(!cleanup.Remove(selected, store).Removed && File.Exists(Log(old)), "cleanup refuses a candidate which became current");
            store.Write(current, null);
            using (var locked = new FileStream(Log(old), FileMode.Open, FileAccess.Read, FileShare.Read))
                Check(!cleanup.Remove(selected, store).Removed && !List().Candidates.Any(row => row.Path == old),
                    "cleanup refuses a file acquired after confirmation and omits a sharing conflict");
            Check(cleanup.Remove(selected, store).Removed && !Directory.Exists(old) && File.Exists(Log(current)) &&
                File.ReadAllBytes(slot).SequenceEqual(prior), "deleting an idle tree preserves the current slot and outputs");

            string inUse = Attempt();
            var beforeUse = Candidate(inUse);
            string editorProbe = Path.Combine(root, "Atlantis.Editor.exe");
            File.Copy(Path.Combine(Environment.SystemDirectory, "cmd.exe"), editorProbe);
            var editorStart = new ProcessStartInfo(editorProbe) { CreateNoWindow = true, UseShellExecute = false };
            foreach (string arg in new[] { "/d", "/c", "ping -n 5 127.0.0.1 >nul & rem --runtime " + Path.Combine(inUse, "atlantis_runtime.exe") })
                editorStart.ArgumentList.Add(arg);
            using (var probe = Process.Start(editorStart)!)
            {
                Check(!probe.HasExited && !cleanup.Remove(beforeUse, store).Removed && File.Exists(Log(inUse)),
                    "an independent Editor process referring to the attempt blocks deletion before any Runtime opens it");
                probe.WaitForExit(6000);
            }

            string aliased = Attempt();
            File.WriteAllText(Path.Combine(aliased, "atlantis_runtime.exe"), "pending Runtime fixture");
            var beforeAliasUse = Candidate(aliased);
            string alias = Path.Combine(root, "runtime-alias");
            using (var linkProcess = Process.Start(new ProcessStartInfo("cmd.exe", $"/d /c mklink /J \"{alias}\" \"{aliased}\"")
                   { CreateNoWindow = true, UseShellExecute = false })!)
            {
                linkProcess.WaitForExit();
                if (linkProcess.ExitCode != 0) throw new InvalidOperationException("Could not create the Runtime junction fixture.");
            }
            try
            {
                Process StartPendingEditor(string runtime) => Process.Start(new ProcessStartInfo(editorProbe,
                    $"/d /c ping.exe -n 6 127.0.0.1 >nul & rem --runtime \"{runtime}\"")
                    { CreateNoWindow = true, UseShellExecute = false })!;
                using (var probe = StartPendingEditor(Path.Combine(alias, "atlantis_runtime.exe")))
                {
                    var result = cleanup.Remove(beforeAliasUse, store);
                    Check(!probe.HasExited && !result.Removed && File.Exists(Log(aliased)) &&
                        result.Detail.Contains("through its Runtime path", StringComparison.Ordinal) &&
                        !List().Candidates.Any(row => row.Path == aliased), "a pending Editor using an absolute junction alias cannot lose its build folder");
                    probe.WaitForExit(8000);
                }
                using (var probe = StartPendingEditor(Path.Combine(alias, "unavailable.exe")))
                {
                    var result = cleanup.Remove(beforeAliasUse, store);
                    Check(!probe.HasExited && !result.Removed && File.Exists(Log(aliased)) &&
                        result.Detail.Contains("Cannot resolve the Runtime", StringComparison.Ordinal),
                        "an unresolvable pending Editor Runtime path refuses cleanup");
                    probe.WaitForExit(8000);
                }
                Check(List().Candidates.Any(row => row.Path == aliased), "a settled build becomes eligible again after the aliased Editor exits");
                using (var probe = StartPendingEditor(Path.Combine(Environment.SystemDirectory, "where.exe")))
                {
                    Check(!probe.HasExited && List().Candidates.Any(row => row.Path == aliased),
                        "a pending Editor with a verified unrelated Runtime keeps the old build eligible");
                    probe.WaitForExit(8000);
                }
            }
            finally { Directory.Delete(alias); }

            string protectedDir = Attempt();
            var beforeOpen = Candidate(protectedDir);
            using (store.Lease()) cleanup.Protect(protectedDir);
            Check(!cleanup.Remove(beforeOpen, store).Removed && File.Exists(Log(protectedDir)) &&
                !List().Candidates.Any(row => row.Path == protectedDir), "durable Open protection survives refresh and fences an older confirmation");

            string replaced = Attempt();
            var beforeReplacement = Candidate(replaced);
            Directory.Move(replaced, replaced + ".held");
            Directory.CreateDirectory(replaced);
            File.WriteAllText(Log(replaced), "data");
            Check(!cleanup.Remove(beforeReplacement, store).Removed && File.Exists(Log(replaced)) && File.Exists(Log(replaced + ".held")),
                "same-path replacement cannot inherit build ownership or be removed");

            string changed = Attempt();
            var beforeChange = Candidate(changed);
            File.WriteAllText(Path.Combine(changed, "extra.txt"), "new");
            Check(!cleanup.Remove(beforeChange, store).Removed && File.Exists(Log(changed)),
                "new directory membership after confirmation refuses before removal");

            string foreignCheckout = Path.Combine(root, "foreign");
            Directory.CreateDirectory(foreignCheckout);
            File.WriteAllText(Path.Combine(foreignCheckout, "CMakeLists.txt"), "# other");
            var foreign = new SavedSceneBuildCleanup(foreignCheckout, scope, attempts);
            Check(foreign.List(store.CurrentAttempt).Candidates.Count == 0 && !foreign.Remove(Candidate(changed), store).Removed,
                "a different checkout cannot list or remove an owned attempt");

            string sentinel = Path.Combine(root, "sentinel");
            Directory.CreateDirectory(sentinel);
            File.WriteAllText(Log(sentinel), "outside");
            var beforeLink = Candidate(changed);
            string link = Path.Combine(changed, "nested");
            using (var linkProcess = Process.Start(new ProcessStartInfo("cmd.exe", $"/d /c mklink /J \"{link}\" \"{sentinel}\"")
                   { CreateNoWindow = true, UseShellExecute = false })!)
            {
                linkProcess.WaitForExit();
                Check(linkProcess.ExitCode == 0 && !cleanup.Remove(beforeLink, store).Removed && File.Exists(Log(changed)) &&
                    File.ReadAllText(Log(sentinel)) == "outside", "nested junction refuses removal and leaves the outside sentinel intact");
                if (linkProcess.ExitCode == 0) Directory.Delete(link);
            }

            File.WriteAllText(slot, "null");
            Check(List().Issue is not null && List().Candidates.Count == 0, "an unreadable current slot disables cleanup");
            File.WriteAllBytes(slot, prior);

            string partial = Attempt();
            string successfulEntry = Path.Combine(partial, "z.txt");
            File.WriteAllText(successfulEntry, "removed first");
            File.SetAttributes(Log(partial), FileAttributes.ReadOnly);
            try
            {
                var outcome = cleanup.Remove(Candidate(partial), store);
                Check(!outcome.Removed && outcome.Detail.Contains("after deleting some entries", StringComparison.Ordinal) &&
                    File.Exists(Log(partial)) && !File.Exists(successfulEntry) && List().Candidates.Any(row => row.Path == partial),
                    "a partial Windows failure retains provenance and requires fresh confirmation");
            }
            finally { if (File.Exists(Log(partial))) File.SetAttributes(Log(partial), FileAttributes.Normal); }
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private sealed record CleanupArtifacts(SavedSceneBuild Older, SavedSceneBuild Current);

    private static async Task CheckSavedSceneBuildCleanupUi(MainWindow window, List<NameUiCheck> checks,
        Action<string> work, string phase)
    {
        const string name = "recent_scene_0124";
        string evidence = Environment.GetEnvironmentVariable("ATLANTIS_0125_EVIDENCE")!;
        string checkout = Environment.GetEnvironmentVariable("ATLANTIS_0090_CHECKOUT")!;
        string artifactFile = Path.Combine(evidence, "artifacts.json");
        T Field<T>(string field) where T : class => NameUiField<T>(window, field)!;
        void Verify(string description, bool condition, string detail = "")
        {
            checks.Add(new(description, condition, detail));
            if (!condition) throw new InvalidOperationException(description + " " + detail);
        }
        static void Click(Button button) => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        static int[] Children() => Process.GetProcessesByName("Atlantis.Editor").Select(p => { using (p) return p.Id; }).ToArray();
        await NameUiWait(() => Field<IReadOnlyList<HierarchyRow>>("_hierarchyRows").Count > 0, "launcher ready", 90);
        work(Field<RuntimeProcess>("_runtime").Launch.WorkDirectory);
        Field<TabControl>("_assetTabs").SelectedIndex = 2;
        var scenes = Field<ListBox>("_savedList");
        var model = Field<SavedSceneAttempts>("_savedAttempts");
        var build = Field<Button>("_savedBuild");
        var open = Field<Button>("_savedOpen");
        var workspace = Field<SavedSceneWorkspace>("_savedWorkspace");
        Field<TextBox>("_savedManaged").Text = Environment.GetEnvironmentVariable("ATLANTIS_0090_MANAGED");
        scenes.SelectedItem = name;
        await NameUiWait(() => build.IsEnabled, "saved scene available", 180);
        if (phase == "seed")
        {
            async Task<SavedSceneBuild> Build()
            {
                Click(build);
                await NameUiWait(() => !model.Building, "CMake configure and build settled", 1200);
                Verify("explicit Build published an eligible recorded result", model.Result is { Recorded: true },
                    Field<TextBlock>("_savedBuildSummary").Text ?? "");
                return model.Result!;
            }
            var older = await Build();
            File.Copy(Path.Combine(older.Directory, "build.log"), Path.Combine(evidence, "older-build.log"));
            var current = await Build();
            File.Copy(Path.Combine(current.Directory, "build.log"), Path.Combine(evidence, "current-build.log"));
            Verify("superseded build has durable idle evidence", workspace.CleanupCandidates().Candidates.Any(item => item.Path == older.Directory));
            File.WriteAllText(artifactFile, JsonSerializer.Serialize(new CleanupArtifacts(older, current)));
            return;
        }

        var artifacts = JsonSerializer.Deserialize<CleanupArtifacts>(File.ReadAllText(artifactFile))!;
        string slot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AtlB",
            SavedSceneReceiptStore.Scope(checkout), "last-build.json");
        byte[] originalSlot = File.ReadAllBytes(slot);
        await NameUiWait(() => open.IsEnabled, "current result reused after restart", 180);
        Verify("the current build is eligible after launcher restart", model.Result == artifacts.Current);
        Click(Field<Button>("_savedCleanup"));
        Window CleanupWindow() => ((Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)
            Avalonia.Application.Current!.ApplicationLifetime!).Windows.Single(win => win.Title == "Clean Up Builds");
        var dialog = CleanupWindow();
        var rows = dialog.GetVisualDescendants().OfType<ListBox>().Single(box => AutomationProperties.GetName(box) == "Builds eligible for cleanup");
        Button Action(string label) => dialog.GetVisualDescendants().OfType<Button>().Single(button => Equals(button.Content, label));
        await NameUiWait(() => rows.ItemCount > 0 && rows.IsEnabled, "cleanup scan settled", 90);
        var row = rows.Items.OfType<SavedSceneBuildCleanup.Candidate>().Single(item => item.Path == artifacts.Older.Directory);
        Verify("cleanup lists the older path, size and reason and omits the current build", row.Size > 0 &&
            row.Reason == "Superseded successful build" && !rows.Items.OfType<SavedSceneBuildCleanup.Candidate>().Any(item => item.Path == artifacts.Current.Directory),
            row.Path + " · " + row.Size + " bytes");
        rows.SelectedItem = row;
        Click(Action("Review Deletion…"));
        await WaitForSavedSceneCapture(evidence, "confirm");
        string addedAfterConfirmation = Path.Combine(row.Path, "added-during-confirmation.txt");
        File.WriteAllText(addedAfterConfirmation, "kept on refusal");
        Click(Action("Delete Build"));
        await NameUiWait(() => rows.IsEnabled && rows.SelectedIndex < 0, "changed confirmation refused", 90);
        Verify("changed confirmation leaves the entire attempt and current receipt intact", File.Exists(addedAfterConfirmation) &&
            File.Exists(Path.Combine(row.Path, "build.log")) && File.ReadAllBytes(slot).SequenceEqual(originalSlot) &&
            dialog.GetVisualDescendants().OfType<SelectableTextBlock>().Any(label => label.Text?.Contains("Nothing was removed", StringComparison.Ordinal) == true));
        await WaitForSavedSceneCapture(evidence, "refused");
        rows.SelectedItem = rows.Items.OfType<SavedSceneBuildCleanup.Candidate>().Single(item => item.Path == row.Path);
        Click(Action("Review Deletion…"));
        Click(Action("Delete Build"));
        await NameUiWait(() => rows.IsEnabled && !Directory.Exists(row.Path), "fresh confirmation removed the old directory", 90);
        Verify("confirmed removal preserves the current receipt and Runtime executable", File.ReadAllBytes(slot).SequenceEqual(originalSlot) &&
            File.Exists(artifacts.Current.Executable));
        await WaitForSavedSceneCapture(evidence, "removed");
        Click(Action("Close"));

        string runtimeWork = Path.Combine(Path.GetTempPath(), "atlantis_editor");
        var before = Children();
        var sessions = Directory.GetDirectories(runtimeWork).ToHashSet(StringComparer.OrdinalIgnoreCase);
        Click(open);
        await NameUiWait(() => !model.Starting, "explicit Open settled", 160);
        using var child = Process.GetProcessById(Children().Except(before).Single());
        try
        {
            string? childSession = null;
            await NameUiWait(() =>
            {
                foreach (string directory in Directory.GetDirectories(runtimeWork).Where(dir => !sessions.Contains(dir)))
                {
                    string log = Path.Combine(directory, "runtime.log"), session = Path.Combine(directory, "runtime.session.json");
                    if (!File.Exists(log) || !File.Exists(session)) continue;
                    using var stream = new FileStream(log, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                    using var reader = new StreamReader(stream);
                    if (reader.ReadLine()?.StartsWith("# " + artifacts.Current.Executable + " ", StringComparison.OrdinalIgnoreCase) != true) continue;
                    childSession = session;
                    return true;
                }
                return false;
            }, "the retained Runtime started", 120);
            using var remote = RemoteSession.Connect(SessionFile.Read(childSession!));
            await NameUiWait(async () => await Task.Run(() => remote.Authoring.Document() is not null), "retained scene opened", 60);
            Verify("independent Editor opened the retained scene and receipt without changing its build log",
                workspace.ReadRecent().Single().Identity.Name == name && File.ReadAllBytes(slot).SequenceEqual(originalSlot) &&
                File.ReadAllBytes(Path.Combine(artifacts.Current.Directory, "build.log")).SequenceEqual(File.ReadAllBytes(Path.Combine(evidence, "current-build.log"))));
            using var logStream = new FileStream(Path.Combine(Path.GetDirectoryName(childSession!)!, "runtime.log"), FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var logReader = new StreamReader(logStream);
            File.WriteAllText(Path.Combine(evidence, "child-runtime.log"), await logReader.ReadToEndAsync());
        }
        finally
        {
            if (!child.HasExited) child.CloseMainWindow();
            await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(30));
        }
    }
}
