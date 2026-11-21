using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.AccessControl;
using System.Text;
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
            var idle = cleanup.Remove(selected, store);
            Check(idle.Removed && !Directory.Exists(old) && File.Exists(Log(current)) &&
                File.ReadAllBytes(slot).SequenceEqual(prior), "deleting an idle tree preserves the current slot and outputs: " + idle.Detail);

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

            string Staged() => Directory.EnumerateDirectories(attempts, "cleanup-*").Single();
            byte[] Permissions(string path) => new DirectoryInfo(path).GetAccessControl(
                AccessControlSections.Access | AccessControlSections.Owner | AccessControlSections.Group).GetSecurityDescriptorBinaryForm();
            void WriteFromProcess(string path) => Check(CompeteWithCleanup("write", path) == 0, "independent writer added " + Path.GetFileName(path));
            void RestoreExpected(string path, byte[] acl)
            {
                var listing = List();
                Check(!Directory.Exists(path) && listing.Restore is not null && File.Exists(Log(Staged())),
                    "refresh preserves the isolated directory and exposes explicit recovery");
                string result = listing.Restore!().Detail;
                Check(File.Exists(Log(path)) && Permissions(path).SequenceEqual(acl) && List().Restore is null,
                    "explicit recovery restores path and exact descriptor: " + result);
            }

            foreach (var boundary in new[] { SavedSceneBuildCleanup.Checkpoint.OriginalVerified, SavedSceneBuildCleanup.Checkpoint.Isolated })
            {
                string late = Attempt();
                Directory.CreateDirectory(Path.Combine(late, "nested"));
                File.WriteAllText(Path.Combine(late, "nested", "reviewed.txt"), "reviewed");
                var confirmed = Candidate(late);
                byte[] acl = Permissions(late);
                int dispositions = 0;
                var refused = cleanup.Remove(confirmed, store, point =>
                {
                    if (point != boundary) return;
                    string actual = point == SavedSceneBuildCleanup.Checkpoint.Isolated ? Staged() : late;
                    WriteFromProcess(Path.Combine(actual, "nested", "new.txt"));
                }, deleting: () => ++dispositions);
                Check(!refused.Removed && dispositions == 0 && File.Exists(Log(late)) && File.ReadAllText(Log(late)) == "data" &&
                    File.ReadAllText(Path.Combine(late, "nested", "reviewed.txt")) == "reviewed" &&
                    File.ReadAllText(Path.Combine(late, "nested", "new.txt")) == "competing writer" &&
                    Permissions(late).SequenceEqual(acl) && File.ReadAllBytes(slot).SequenceEqual(prior),
                    $"membership change at {boundary} refused with zero dispositions and all bytes preserved: " + refused.Detail);
                if (!Directory.Exists(late)) throw new InvalidOperationException(refused.Detail);
                using var held = new SavedSceneCleanupWindows.PinnedDirectories();
                Check(held.Add(late) == confirmed.Record.DirectoryIdentity, "late membership refusal restored the same root object");
            }

            string guarded = Attempt();
            Directory.CreateDirectory(Path.Combine(guarded, "nested"));
            File.WriteAllText(Path.Combine(guarded, "nested", "reviewed.txt"), "reviewed");
            int guardedDispositions = 0;
            var guardedResult = cleanup.Remove(Candidate(guarded), store, afterGuard: _ =>
            {
                string staged = Staged(), child = Path.Combine(staged, "nested", "reviewed.txt");
                string outside = Path.Combine(root, "move-in.txt");
                File.WriteAllText(outside, "outside");
                string junction = Path.Combine(root, "guard-alias");
                using var mklink = Process.Start(new ProcessStartInfo("cmd.exe", $"/d /c mklink /J \"{junction}\" \"{staged}\"")
                    { CreateNoWindow = true, UseShellExecute = false })!;
                mklink.WaitForExit();
                if (mklink.ExitCode != 0) throw new InvalidOperationException("Could not create guarded alias fixture.");
                try
                {
                    Check(CompeteWithCleanup("write", Path.Combine(staged, "nested", "late.txt")) == 7 &&
                        CompeteWithCleanup("mkdir", Path.Combine(junction, "alias-child")) == 7 &&
                        CompeteWithCleanup("move", outside, Path.Combine(staged, "arriving.txt")) == 7 &&
                        CompeteWithCleanup("delete", child) == 7 &&
                        CompeteWithCleanup("move", child, Path.Combine(root, "escape.txt")) == 7 && File.ReadAllText(outside) == "outside",
                        "live exclusion denies independent create, alias mkdir, move-in, deletion and move-out after guarded recheck");
                }
                finally { Directory.Delete(junction); }
            }, deleting: () =>
            {
                if (guardedDispositions++ == 0)
                    Check(CompeteWithCleanup("mkdir", guarded) == 7, "the original name remains reserved through the first disposition");
            });
            Check(guardedResult.Removed && !Directory.Exists(guarded), "unchanged guarded control deletes only the approved tree: " + guardedResult.Detail);

            string lost = Attempt();
            int lostDispositions = 0;
            var lostResult = cleanup.Remove(Candidate(lost), store, afterGuard: release =>
            {
                string staged = Staged();
                release();
                WriteFromProcess(Path.Combine(staged, "after-loss.txt"));
            }, deleting: () => ++lostDispositions);
            Check(!lostResult.Removed && lostDispositions == 0 && File.Exists(Log(lost)) && File.Exists(Path.Combine(lost, "after-loss.txt")),
                "a lost guard refuses before any disposition: " + lostResult.Detail);

            foreach (var boundary in new[] { SavedSceneBuildCleanup.Checkpoint.Recorded, SavedSceneBuildCleanup.Checkpoint.Isolated,
                         SavedSceneBuildCleanup.Checkpoint.PermissionChanged, SavedSceneBuildCleanup.Checkpoint.Guarded })
            {
                string interrupted = Attempt();
                Directory.CreateDirectory(Path.Combine(interrupted, "nested"));
                byte[] rootAcl = Permissions(interrupted), nestedAcl = Permissions(Path.Combine(interrupted, "nested"));
                int dispositions = 0;
                try
                {
                    _ = cleanup.Remove(Candidate(interrupted), store, point =>
                    {
                        if (point == boundary) throw new OperationCanceledException("disposable interruption fixture");
                    }, deleting: () => ++dispositions);
                    Check(false, "interrupted cleanup did not reach " + boundary);
                    continue;
                }
                catch (OperationCanceledException) { }
                if (boundary == SavedSceneBuildCleanup.Checkpoint.Recorded)
                {
                    Check(File.Exists(Log(interrupted)) && List().Restore is { } && List().Restore!().Detail.StartsWith("Restored", StringComparison.Ordinal),
                        "a recorded operation interrupted before rename can clear safely");
                }
                else RestoreExpected(interrupted, rootAcl);
                Check(dispositions == 0 && Permissions(Path.Combine(interrupted, "nested")).SequenceEqual(nestedAcl),
                    "interruption at " + boundary + " preserves the nested ACL and issues no deletion");
            }

            string conflict = Attempt();
            byte[] conflictAcl = Permissions(conflict);
            var refusedConflict = cleanup.Remove(Candidate(conflict), store, point =>
            {
                if (point != SavedSceneBuildCleanup.Checkpoint.Isolated) return;
                Directory.CreateDirectory(conflict);
                File.WriteAllText(Log(conflict), "replacement");
            });
            string pendingPath = Staged();
            Check(!refusedConflict.Removed && List().Restore is { } && List().Restore!().Detail.Contains("occupied", StringComparison.Ordinal) &&
                File.ReadAllText(Log(conflict)) == "replacement" && File.ReadAllText(Log(pendingPath)) == "data",
                "reused original name preserves both identities without overwrite or deletion");
            Directory.Move(conflict, conflict + "-replacement");
            RestoreExpected(conflict, conflictAcl);

            string uncertain = Attempt();
            byte[] uncertainAcl = Permissions(uncertain);
            int uncertainDeletes = 0;
            var uncertainResult = cleanup.Remove(Candidate(uncertain), store, point =>
            {
                if (point != SavedSceneBuildCleanup.Checkpoint.Isolated) return;
                string isolated = Staged();
                Directory.Move(isolated, Path.Combine(root, "moved-isolation"));
                Directory.CreateDirectory(isolated);
                File.WriteAllText(Log(isolated), "unknown replacement");
            }, deleting: () => ++uncertainDeletes);
            string uncertainName = Staged();
            Check(!uncertainResult.Removed && uncertainDeletes == 0 && !Directory.Exists(uncertain) &&
                List().Restore!().Detail.Contains("identity", StringComparison.Ordinal) && File.ReadAllText(Log(uncertainName)) == "unknown replacement" &&
                File.ReadAllText(Log(Path.Combine(root, "moved-isolation"))) == "data", "uncertain isolated identity stays retained without deletion or overwrite");
            Directory.Move(uncertainName, Path.Combine(root, "unknown-replacement"));
            Directory.Move(Path.Combine(root, "moved-isolation"), uncertainName);
            RestoreExpected(uncertain, uncertainAcl);

            string interruptedDelete = Attempt();
            File.WriteAllText(Path.Combine(interruptedDelete, "z.txt"), "first disposition");
            try
            {
                _ = cleanup.Remove(Candidate(interruptedDelete), store, point =>
                {
                    if (point == SavedSceneBuildCleanup.Checkpoint.DisposedEntry) throw new OperationCanceledException("post-disposition fixture");
                });
                Check(false, "post-disposition interruption did not run");
            }
            catch (OperationCanceledException) { }
            string interruptedStaging = Staged();
            var afterInterruption = List();
            Check(!Directory.Exists(interruptedDelete) && File.ReadAllText(Log(interruptedStaging)) == "data" &&
                !File.Exists(Path.Combine(interruptedStaging, "z.txt")) && afterInterruption.Candidates.Count == 1,
                "interrupting after an observed disposition leaves a verified remainder and refresh performs no delete");
            var continued = cleanup.Remove(afterInterruption.Candidates.Single(), store);
            Check(continued.Removed && !Directory.Exists(interruptedStaging), "explicit confirmation resumes interrupted deletion: " + continued.Detail);

            string partial = Attempt();
            string successfulEntry = Path.Combine(partial, "z.txt");
            File.WriteAllText(successfulEntry, "removed first");
            File.SetAttributes(Log(partial), FileAttributes.ReadOnly);
            try
            {
                var outcome = cleanup.Remove(Candidate(partial), store);
                string retained = Staged();
                Check(!outcome.Removed && outcome.Detail.Contains("after a delete request", StringComparison.Ordinal) &&
                    File.Exists(Log(retained)) && !File.Exists(Path.Combine(retained, "z.txt")) && List().Candidates.Single().Path == retained &&
                    List().Restore!().Detail.Contains("incomplete", StringComparison.Ordinal),
                    "a partial Windows failure retains provenance and requires fresh confirmation: " + outcome.Detail);
                File.SetAttributes(Log(retained), FileAttributes.Normal);
                var retry = cleanup.Remove(List().Candidates.Single(), store);
                Check(retry.Removed && !Directory.Exists(retained) && List().Restore is null, "confirmed verified remainder can be deleted: " + retry.Detail);
            }
            finally
            {
                if (File.Exists(Log(partial))) File.SetAttributes(Log(partial), FileAttributes.Normal);
                foreach (string retained in Directory.EnumerateDirectories(attempts, "cleanup-*"))
                {
                    if (File.Exists(Log(retained))) File.SetAttributes(Log(retained), FileAttributes.Normal);
                }
            }
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static int CompeteWithCleanup(string verb, string path, string? destination = null)
    {
        const string script = "try { switch ($env:ATLANTIS_CLEANUP_VERB) { " +
            "'write' { [IO.File]::WriteAllText($env:ATLANTIS_CLEANUP_PATH, 'competing writer') } " +
            "'mkdir' { [void][IO.Directory]::CreateDirectory($env:ATLANTIS_CLEANUP_PATH) } " +
            "'move' { [IO.File]::Move($env:ATLANTIS_CLEANUP_PATH, $env:ATLANTIS_CLEANUP_DESTINATION) } " +
            "'delete' { [IO.File]::Delete($env:ATLANTIS_CLEANUP_PATH) } }; exit 0 } " +
            "catch { if ($_.Exception.InnerException -is [UnauthorizedAccessException] -or $_.Exception.InnerException -is [IO.IOException]) { exit 7 }; exit 9 }";
        var start = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe"))
            { CreateNoWindow = true, UseShellExecute = false };
        foreach (string arg in new[] { "-NoProfile", "-EncodedCommand", Convert.ToBase64String(Encoding.Unicode.GetBytes(script)) })
            start.ArgumentList.Add(arg);
        start.Environment["ATLANTIS_CLEANUP_VERB"] = verb;
        start.Environment["ATLANTIS_CLEANUP_PATH"] = path;
        if (destination is not null) start.Environment["ATLANTIS_CLEANUP_DESTINATION"] = destination;
        using var process = Process.Start(start)!;
        if (!process.WaitForExit(15000)) throw new InvalidOperationException("The competing Windows process did not settle.");
        return process.ExitCode;
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
        string localBuild = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AtlB");
        string scope = Path.Combine(localBuild, SavedSceneReceiptStore.Scope(checkout)), attempts = Path.Combine(localBuild, "a");
        string slot = Path.Combine(scope, "last-build.json");
        var store = new SavedSceneReceiptStore(scope, attempts, checkout);
        var cleanup = new SavedSceneBuildCleanup(checkout, scope, attempts);
        string MakeDisposableAttempt()
        {
            // Only the superseded candidate is disposable. Keep the actual
            // current receipt and its configured Runtime from the original run.
            using var lease = store.Lease();
            string path = store.ReserveAttempt();
            Verify("owned disposable cleanup fixture reserved", cleanup.Reserve(path));
            Directory.CreateDirectory(Path.Combine(path, "nested"));
            File.WriteAllText(Path.Combine(path, "build.log"), "disposable build log");
            File.WriteAllText(Path.Combine(path, "nested", "output"), "disposable output");
            cleanup.Settle(path, succeeded: true);
            return path;
        }
        string olderPath = phase == "admission" ? MakeDisposableAttempt() : artifacts.Older.Directory;
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
        var row = rows.Items.OfType<SavedSceneBuildCleanup.Candidate>().Single(item => item.Path == olderPath);
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
        if (phase == "admission")
        {
            string disputed = MakeDisposableAttempt();
            var observed = workspace.CleanupCandidates().Candidates.Single(item => item.Path == disputed);
            int dispositions = 0;
            HashSet<string> existingStaging = Directory.EnumerateDirectories(attempts, "cleanup-*").ToHashSet(StringComparer.OrdinalIgnoreCase);
            var refusal = cleanup.Remove(observed, store, point =>
            {
                if (point != SavedSceneBuildCleanup.Checkpoint.Isolated) return;
                Directory.CreateDirectory(disputed);
                File.WriteAllText(Path.Combine(disputed, "replacement"), "unrelated directory");
            }, deleting: () => ++dispositions);
            string isolated = Directory.EnumerateDirectories(attempts, "cleanup-*").Single(path => !existingStaging.Contains(path));
            Verify("occupied original path leaves the isolated folder available for recovery", !refusal.Removed && dispositions == 0 &&
                File.Exists(Path.Combine(isolated, "build.log")) && File.Exists(Path.Combine(disputed, "replacement")), refusal.Detail);
            Click(Action("Refresh List"));
            await NameUiWait(() => Action("Restore Build Folder").IsVisible && Action("Restore Build Folder").IsEnabled, "pending recovery shown");
            Click(Action("Restore Build Folder"));
            bool Shows(string text) => dialog.GetVisualDescendants().OfType<SelectableTextBlock>().Any(label =>
                label.Text?.Contains(text, StringComparison.Ordinal) == true);
            await NameUiWait(() => Action("Restore Build Folder").IsEnabled && Shows("retained"), "recovery conflict shown");
            Verify("restore action refuses to overwrite the replacement", File.Exists(Path.Combine(disputed, "replacement")) &&
                File.Exists(Path.Combine(isolated, "build.log")));
            await WaitForSavedSceneCapture(evidence, "recovery-conflict");
            Directory.Move(disputed, disputed + "-replacement");
            Click(Action("Restore Build Folder"));
            await NameUiWait(() => rows.IsEnabled && !Action("Restore Build Folder").IsVisible && Shows("Restored"), "explicit restore settled");
            Verify("explicit recovery returns the isolated build and preserves the replacement", !Directory.Exists(isolated) &&
                File.Exists(Path.Combine(disputed, "build.log")) && File.Exists(Path.Combine(disputed + "-replacement", "replacement")));
            await WaitForSavedSceneCapture(evidence, "restored");
        }
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
