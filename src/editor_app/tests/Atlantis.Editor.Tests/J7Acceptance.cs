// Plan 0068 M7 (J7): the owner's acceptance run, executed as a scripted
// session through the product's own session layer -- EditorWorker, the exact
// methods the toolbar, the Hierarchy, the Inspector and the Viewport call --
// against a real atlantis_runtime started by the app's own RuntimeProcess,
// with the second client being the real atlantis CLI (scene set, runtime
// step --capture) and the "same frame" check comparing the captures before
// and after a restart on the saved file. The Avalonia panels' own rendering
// is not mouse-driven; the method is recorded with the result. Prints one
// JSON object; exit 0 when every step passed. GPU.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using Command = Atlantis.Gameplay.Authoring.Command;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;

public static class J7Acceptance
{
    public static int Run(string runtimePath, string cliPath, string scene)
    {
        var steps = new List<(string Name, bool Ok, string Detail)>();
        void Step(string name, bool ok, string detail = "") => steps.Add((name, ok, detail));
        string work = RuntimeProcess.NewWorkDirectory();
        try
        {
            using RuntimeProcess runtime = RuntimeProcess.Start(new RuntimeLaunch(runtimePath, scene, null, work));
            SessionInfo info = runtime.WaitForSession(TimeSpan.FromSeconds(300));
            var ui = new Ui();
            using var log = new EditorLog(Path.Combine(work, "editor.log"));
            using var worker = new EditorWorker(info, ui.Post, log);
            ui.Wire(worker);
            worker.Start();
            if (!ui.Await(() => ui.Opened is not null && ui.Apply.Length > 0, "the editor connects and opens the document", 60000))
                throw new InvalidOperationException("the worker did not open the document: " + ui.Apply);
            using Subjects subjects = Subjects.Open(info);
            Step("0. the editor opens the document", true, ui.Opened!.NodeCount + " nodes (" + ui.Opened.Provenance + ")");

            // 1. Pick: a ray at a Renderable selects it; the overlay (outline + Gizmo) follows.
            Vector3 eye = subjects.Eye;
            worker.Raycast(new PickRay(eye, Vector3.Normalize(subjects.LeafPosition - eye)), Stopwatch.GetTimestamp());
            bool picked = ui.Await(() => ui.Picks > 0 && ui.Pick!.Result.Hit is not null, "the pick answers", 30000);
            if (picked)
            {
                worker.ShowSelection(ui.Pick!.Result.Hit!.Entity);
                worker.ShowPick(ui.Pick.Result.Hit!.Entity);
            }
            Step("1. pick: the entity is selected with its Gizmo",
                 picked && ui.Await(() => ui.Gizmo?.Node == ui.Pick!.Result.Hit!.Entity, "the Gizmo appears", 30000),
                 ui.Pick?.Result.Hit?.ToString() ?? "miss");

            // 2. Drag-translate and release: applied. A refusal (J4) retries once on another leaf.
            worker.ShowSelection(subjects.Leaf);
            float release2 = DragAndRelease(worker, ui, GizmoMode.Translate, GizmoSpace.World, snap: false, subjects.Leaf, subjects.Alternative);
            Step("2. drag-translate: released and applied", release2 >= 0 && ui.Apply.StartsWith("applied", StringComparison.Ordinal), Applied(ui, release2));

            // 3. Rotate with snapping: applied (the same retry).
            float release3 = DragAndRelease(worker, ui, GizmoMode.Rotate, GizmoSpace.World, snap: true, subjects.Leaf, subjects.Alternative);
            Step("3. rotate with snapping: released and applied", release3 >= 0 && ui.Apply.StartsWith("applied", StringComparison.Ordinal), Applied(ui, release3));

            // 4. A light's intensity in the Inspector, committed as one edit: applied.
            worker.ShowSelection(subjects.Light);
            ui.Await(() => ui.Inspector?.Subject == subjects.Light, "the Inspector shows the light", 30000);
            float intensity = subjects.Field(subjects.Light, "Light.intensity") ?? 1.0f;
            int notices = ui.Notices;
            worker.CommitField("Light.intensity", (intensity + 0.5f).ToString("R", CultureInfo.InvariantCulture));
            bool edited = ui.Await(() => ui.Notices > notices, "the Inspector commit applies", 30000);
            Step("4. Inspector: the intensity is committed and applied",
                 edited && ui.Apply.StartsWith("applied", StringComparison.Ordinal) &&
                 subjects.Field(subjects.Light, "Light.intensity") == intensity + 0.5f,
                 Applied(ui, -1));

            // 5. Create under the camera, reparent, destroy the new parent's subtree, undo, redo.
            EntityGuid underCamera = subjects.Camera ?? subjects.Leaf;
            int created = ui.Created;
            worker.CreateChild(underCamera);
            bool isCreated = ui.Await(() => ui.Created > created, "the node is created", 30000) &&
                             ui.Apply.StartsWith("applied", StringComparison.Ordinal);
            EntityGuid node = ui.CreatedNode;
            EntityGuid destroyTarget = underCamera;
            bool reparented = false;
            if (isCreated && subjects.Alternative is EntityGuid target && target != underCamera)
            {
                int moves = ui.Notices;
                worker.Reparent(node, target);
                reparented = ui.Await(() => ui.Notices > moves, "the reparent answers", 30000) &&
                             !ui.LatestNotice.Contains("refused");
                if (reparented) destroyTarget = target;
            }
            Step("5a. create under the camera and reparent it", isCreated,
                 "created under " + underCamera + (reparented ? ", reparented under " + destroyTarget : "; the reparent was refused (J4)"));
            int destroyed = ui.Destroyed;
            worker.DestroySubtree(destroyTarget);
            bool isDestroyed = ui.Await(() => ui.Destroyed > destroyed, "the subtree is destroyed", 30000) &&
                               ui.Apply.StartsWith("applied", StringComparison.Ordinal);
            Step("5b. destroy the parent's subtree", isDestroyed, ui.Apply);
            int undone = ui.Notices;
            worker.Undo();
            bool isUndone = ui.Await(() => ui.Notices > undone && ui.LatestNotice.Contains("undo"), "the undo answers", 30000);
            Step("5c. undo: the subtree is back", isUndone, ui.LatestNotice);
            int redone = ui.Notices;
            worker.Redo();
            Step("5d. redo: the subtree is gone again",
                 ui.Await(() => ui.Notices > redone && ui.LatestNotice.Contains("redo"), "the redo answers", 30000), ui.LatestNotice);

            if (subjects.Imported)
            {
                // Bistro: the same edits; the save is refused with its reason.
                int save = ui.Notices;
                worker.Save();
                bool refused = ui.Await(() => ui.Notices > save, "the save answers", 30000) &&
                               ui.LatestNotice.Contains("ImportedSceneNotSavable");
                Step("6. save: an imported scene is refused with the reason", refused, ui.LatestNotice);
            }
            else
            {
                // 6. Save, restart the Runtime on the saved file, and see the same frame. The committed source is
                // restored afterwards, in a finally, so the tree stays as committed whatever happens in between.
                string source = ui.Opened.SourcePath;
                byte[]? backup = File.Exists(source) ? File.ReadAllBytes(source) : null;
                try
                {
                int save = ui.Notices;
                worker.Save();
                bool saved = ui.Await(() => ui.Notices > save && ui.LatestNotice.StartsWith("saved ", StringComparison.Ordinal), "the save answers", 30000);
                bool changed = saved && (backup is null || !File.ReadAllBytes(source).SequenceEqual(backup));
                Step("6a. save: the source is written with the edits", saved && changed, ui.LatestNotice);
                int paused = ui.Plays;
                worker.Pause();
                bool isPaused = ui.Await(() => ui.Plays > paused && !ui.Playing, "the Runtime pauses", 30000);
                (int exitX, string _) = Cli(cliPath, sessionFile(work), "runtime", "step", "--frames", "1", "--capture", Path.Combine(work, "before.png"));
                byte[]? before = isPaused && exitX == 0 && File.Exists(Path.Combine(work, "before.png"))
                    ? File.ReadAllBytes(Path.Combine(work, "before.png")) : null;
                Step("6b. the frame before the restart is captured", before is not null, exitX == 0 ? "before.png" : "the CLI refused");
                // Spec 0065's north star: "save, rebuild, restart". The Runtime loads the scene's cooked asset, not
                // the text source, so the rebuild re-cooks it from the saved file; the finally restores and re-cooks
                // the committed source afterwards.
                string buildDir = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(runtimePath)!, "..", "..", ".."));
                (int rebuilt, string rebuildOut) = Run("cmake", "--build \"" + buildDir + "\" --target " + scene + "_scene_asset --config Release");
                Step("6b2. the saved file is rebuilt into the scene's cooked asset", rebuilt == 0, rebuildOut.Length > 200 ? "" : rebuildOut);
                worker.Dispose();
                runtime.CloseMainWindow();
                runtime.WaitForExit(TimeSpan.FromSeconds(30));

                string work2 = RuntimeProcess.NewWorkDirectory();  // a dir of its own: the first Runtime's log is still held
                using var runtime2 = RuntimeProcess.Start(new RuntimeLaunch(runtimePath, scene, null, work2));
                SessionInfo info2 = runtime2.WaitForSession(TimeSpan.FromSeconds(300));
                string session2 = sessionFile(work2);
                (_, _) = Cli(cliPath, session2, "runtime", "pause");
                (int exitZ, string _) = Cli(cliPath, session2, "runtime", "step", "--frames", "1", "--capture", Path.Combine(work2, "after.png"));
                byte[]? after = exitZ == 0 && File.Exists(Path.Combine(work2, "after.png")) ? File.ReadAllBytes(Path.Combine(work2, "after.png")) : null;
                Step("6c. the Runtime restarts on the saved file and shows the same frame",
                      before is not null && after is not null && before.SequenceEqual(after),
                      before is null || after is null ? "no capture" :
                      before.SequenceEqual(after) ? before.Length + " bytes, identical" :
                      before.Length + " vs " + after.Length + " bytes");

                // 7. A second client (the real CLI): it edits the selected field; the Inspector follows, the undo is refused.
                var ui2 = new Ui();
                using var log2 = new EditorLog(Path.Combine(work, "editor2.log"));
                using var worker2 = new EditorWorker(info2, ui2.Post, log2);
                ui2.Wire(worker2);
                worker2.Start();
                if (!ui2.Await(() => ui2.Opened is not null, "the editor reconnects", 60000))
                    throw new InvalidOperationException("the second worker did not open the document");
                using Subjects second = Subjects.Open(info2);
                worker2.ShowSelection(second.Light);
                ui2.Await(() => ui2.Inspector?.Subject == second.Light, "the Inspector shows the light", 30000);
                float now = second.Field(second.Light, "Light.intensity") ?? 1.0f;
                int committed = ui2.Notices;
                worker2.CommitField("Light.intensity", (now + 1.0f).ToString("R", CultureInfo.InvariantCulture));
                ui2.Await(() => ui2.Notices > committed, "the editor's edit applies", 30000);
                float overwritten = now + 6.0f;
                (int cliExit, string cliOut) = Cli(cliPath, session2, "scene", "set", second.Light.ToString(), "Light.intensity",
                                                   overwritten.ToString("R", CultureInfo.InvariantCulture));
                bool followed = cliExit == 0 && ui2.Await(() => second.Field(second.Light, "Light.intensity") == overwritten, "the Inspector follows", 30000);
                int undo = ui2.Notices;
                worker2.Undo();
                bool refused2 = ui2.Await(() => ui2.Notices > undo, "the undo answers", 30000) &&
                                ui2.LatestNotice.Contains("changed elsewhere");
                Step("7. a second client: the Inspector follows and the undo is refused \"changed elsewhere\"",
                     followed && refused2, ui2.LatestNotice + (cliExit == 0 ? "" : "; the CLI: " + cliOut));
                worker2.Dispose();
                runtime2.CloseMainWindow();
                runtime2.WaitForExit(TimeSpan.FromSeconds(30));
                }
                finally
                {
                    if (backup is not null) File.WriteAllBytes(source, backup);  // the tree stays as committed
                    if (backup is not null) _ = Run("cmake", "--build \"" + Path.GetFullPath(Path.Combine(Path.GetDirectoryName(runtimePath)!, "..", "..", "..")) +
                                                         "\" --target " + scene + "_scene_asset --config Release");  // and the cook with it
                }
            }
        }
        catch (Exception e)
        {
            Step("the acceptance session ran", false, e.GetType().Name + ": " + e.Message);
        }
        var report = new StringBuilder("{\"j7\":\"").Append(scene).Append("\",\"steps\":[");
        for (int i = 0; i < steps.Count; ++i)
        {
            if (i > 0) report.Append(',');
            report.Append("{\"name\":\"").Append(steps[i].Name.Replace("\r", " ").Replace("\n", " ").Replace("\\", "\\\\").Replace("\"", "\\\"")).Append("\",\"ok\":").Append(steps[i].Ok ? "true" : "false")
                  .Append(",\"detail\":\"").Append(steps[i].Detail.Replace("\r", " ").Replace("\n", " ").Replace("\\", "\\\\").Replace("\"", "\\\"")).Append("\"}");
        }
        int failed = steps.Count(s => !s.Ok);
        report.Append("],\"passed\":").Append(steps.Count - failed).Append(",\"failed\":").Append(failed).Append('}');
        Console.WriteLine(report.ToString());
        return failed == 0 && steps.Count > 0 ? 0 : 1;
    }

    private static string sessionFile(string work) => Path.Combine(work, "runtime.session.json");

    private static string Applied(Ui ui, float ms) =>
        ui.Apply + (ms >= 0 ? " (commit->applied " + ms.ToString("F0", CultureInfo.InvariantCulture) + " ms)" : "");

    /// <summary>One Gizmo drag as the Viewport runs it: select, grab, batches, release. The commit->applied milliseconds,
    /// or -1 when nothing committed (a J4 refusal retries once on the alternative node).</summary>
    private static float DragAndRelease(EditorWorker worker, Ui ui, GizmoMode mode, GizmoSpace space, bool snap,
                                        EntityGuid node, EntityGuid alternative)
    {
        foreach (EntityGuid subject in new[] { node, alternative })
        {
            worker.ShowSelection(subject);
            Thread.Sleep(150);  // let the worker's poll settle the selection before the grab
            worker.SetGizmo(mode, space, snap);
            int before = ui.Commits;
            worker.BeginGizmoDrag(GizmoAxis.X);
            for (int b = 1; b <= 4; ++b) worker.GizmoDragTo(3.0f * b, Stopwatch.GetTimestamp());
            worker.EndGizmoDrag(15.0f, Stopwatch.GetTimestamp());
            if (!ui.Await(() => ui.Commits > before, "the release commits", 30000)) continue;  // refused: try the other node
            ui.Await(() => ui.Apply.StartsWith("applied", StringComparison.Ordinal), "the apply settles", 30000);
            return ui.LastCommitMs;
        }
        return -1.0f;
    }

    /// <summary>The UI side of the worker: the panels' event contract, recorded as posted.</summary>
    private sealed class Ui
    {
        public volatile string Apply = "";
        public volatile string LatestNotice = "";
        public volatile bool Playing = true;
        public volatile float LastCommitMs;
        public DocumentInfo? Opened;
        public PickResult? Pick;
        public InspectorSnapshot? Inspector;
        public GizmoSnapshot? Gizmo;
        public EntityGuid CreatedNode;
        public int Notices, Commits, Picks, Created, Destroyed, Plays;

        public void Post(Action action) => action();  // inline: the panels' handlers run on the worker thread

        public void Wire(EditorWorker worker)
        {
            worker.ApplyStateChanged += text => Apply = text;
            worker.EditNotice += text => { LatestNotice = text; Interlocked.Increment(ref Notices); };
            worker.PlayStateChanged += playing => { Playing = playing; Interlocked.Increment(ref Plays); };
            worker.DocumentOpened += info => Opened = info;
            worker.Picked += pick => { Pick = pick; Interlocked.Increment(ref Picks); };
            worker.InspectorChanged += snapshot => Inspector = snapshot;
            worker.GizmoChanged += snapshot => Gizmo = snapshot;
            worker.NodeCreatedByEditor += created => { CreatedNode = created; Interlocked.Increment(ref Created); };
            worker.NodesDestroyed += _ => Interlocked.Increment(ref Destroyed);
            worker.CommitApplied += (ms, _) => { LastCommitMs = (float)ms; Interlocked.Increment(ref Commits); };
        }

        public bool Await(Func<bool> condition, string what, int timeoutMs)
        {
            var until = DateTime.UtcNow + TimeSpan.FromMilliseconds(timeoutMs);
            while (DateTime.UtcNow < until)
            {
                if (condition()) return true;
                Thread.Sleep(20);
            }
            return condition();
        }
    }

    /// <summary>A read-only second client for discovery and value reads, held for the session.</summary>
    private sealed class Subjects : IDisposable
    {
        public EntityGuid Leaf, Light, Alternative;
        public Vector3 LeafPosition;
        public Vector3 Eye;
        public EntityGuid? Camera;
        public bool Imported;

        private readonly RemoteSession _session;
        private readonly RemoteAuthoringDocument _document;
        private readonly DocumentTree _tree;

        private Subjects(RemoteSession session, RemoteAuthoringDocument document, DocumentTree tree)
        {
            _session = session;
            _document = document;
            _tree = tree;
        }

        public static Subjects Open(SessionInfo info)
        {
            RemoteSession session = RemoteSession.Connect(info);
            if (session.Authoring.Document() is null) session.Authoring.Open();
            var document = new RemoteAuthoringDocument(session.Authoring, session);
            var tree = new DocumentTree();
            tree.Rebuild(document);
            IReadOnlyList<SchemaType> schema = document.Schema;
            TypeId? renderable = SceneSchema.TypeOf(schema, "Renderable"), light = SceneSchema.TypeOf(schema, "Light"),
                    camera = SceneSchema.TypeOf(schema, "Camera");
            var leaves = tree.Order.Where(n => tree.DescendantsOf(n).Count == 0 && renderable is TypeId r && tree.ComponentsOf(n).Contains(r)).ToList();
            if (leaves.Count == 0) throw new InvalidOperationException("the scene has no leaf Renderable");
            var subjects = new Subjects(session, document, tree)
            {
                Leaf = leaves[0],
                Alternative = leaves.Count > 1 ? leaves[1] : leaves[0],
                Light = tree.Order.FirstOrDefault(n => light is TypeId l && tree.ComponentsOf(n).Contains(l)),
                Camera = tree.Order.Cast<EntityGuid?>().FirstOrDefault(n => camera is TypeId c && tree.ComponentsOf(n!.Value).Contains(c)),
                Imported = document.Info()?.Provenance == Provenance.Imported,
            };
            subjects.LeafPosition = new Vector3(TransformMath.WorldOf(document, tree, subjects.Leaf)[12],
                                                 TransformMath.WorldOf(document, tree, subjects.Leaf)[13],
                                                 TransformMath.WorldOf(document, tree, subjects.Leaf)[14]);
            // The pick's eye: the scene camera's, or above the scene looking at the subject when there is none read.
            SceneSnapshot snapshot = SceneReader.Read(new GameplayWorld(session));
            if (snapshot.ScenePose is CameraPose pose)
            {
                subjects.Eye = new Vector3(pose.WorldMatrix[12], pose.WorldMatrix[13], pose.WorldMatrix[14]);
            }
            else
            {
                subjects.Eye = subjects.LeafPosition + new Vector3(0.0f, 40.0f, 40.0f);
            }
            return subjects;
        }

        public float? Field(EntityGuid node, string path)
        {
            string[] parts = path.Split('.');
            SceneAddress? address = SceneSchema.AddressOf(_document.Schema, node, parts[0], parts[1]);
            return address is SceneAddress a && _document.GetProperty(a) is PropertyValue v && v.TryGetFloat32(out float f) ? f : null;
        }

        public void Dispose() => _session.Dispose();
    }

    /// <summary>The real CLI as the second client; its session file is the Runtime's own.</summary>
    private static (int Exit, string Output) Cli(string cliPath, string sessionFile, params string[] args)
    {
        var start = new ProcessStartInfo(cliPath)
        {
            Arguments = "--session \"" + sessionFile + "\" " + string.Join(" ", args),
            RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true,
        };
        using Process process = Process.Start(start)!;
        string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
        process.WaitForExit(30000);
        return (process.HasExited ? process.ExitCode : 3, output.Trim());
    }

    /// <summary>A build step (the scene's cooked asset), for the save-rebuild-restart of the north star.</summary>
    private static (int Exit, string Output) Run(string program, string arguments)
    {
        var start = new ProcessStartInfo(program)
        {
            Arguments = arguments, RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false,
            CreateNoWindow = true,
        };
        using Process process = Process.Start(start)!;
        string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
        process.WaitForExit(300000);
        return (process.HasExited ? process.ExitCode : 3, output.Trim());
    }
}
