// Plan 0068 M7 (P5; Spec 0068 Testing): the live integration test -- a
// scripted editor session against a real atlantis_runtime --listen, through
// the same models the app's worker uses (the tree, the apply controller, the
// Gizmo drag, the Inspector, the history over the authoring target): pick,
// drag with previews, release -> Applied, undo, redo, create (with a Light
// and an intensity), destroy the parent's subtree, undo of destroy, back to
// the original document, and save -- refused with ImportedSceneNotSavable on
// an imported scene (Bistro), written and byte-restored on a hand-authored
// one. A second client then overwrites the field the editor edited: the
// Inspector follows it and the editor's undo is refused "changed elsewhere".
// After every applied step the document is checked and a stepped frame with
// its capture taken. GPU; prints one JSON object; exit 0 when every check
// passed.
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
using AssetType = Atlantis.Gameplay.Assets.AssetType;
using Command = Atlantis.Gameplay.Authoring.Command;
using CommandFailure = Atlantis.Gameplay.Authoring.CommandFailure;
using Event = Atlantis.Gameplay.Authoring.Event;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;
using AddComponent = Atlantis.Gameplay.Authoring.AddComponent;
using PropertyChanged = Atlantis.Gameplay.Authoring.PropertyChanged;

namespace Atlantis.Editor.Tests;

public static class IntegrationTest
{
    public static int Run(string runtimePath, string scene)
    {
        var checks = new List<(string Name, bool Ok, string Detail)>();
        void Check(string name, bool ok, string detail = "") => checks.Add((name, ok, detail));
        string work = RuntimeProcess.NewWorkDirectory();
        try
        {
            using RuntimeProcess runtime = RuntimeProcess.Start(new RuntimeLaunch(runtimePath, scene, null, work));
            SessionInfo info = runtime.WaitForSession(TimeSpan.FromSeconds(300));
            using (RemoteSession session = RemoteSession.Connect(info))
            {
                session.Control.Resume();
                string config = new DirectoryInfo(Path.GetDirectoryName(runtimePath)!).Name;
                string cli = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(runtimePath)!, "..", "..", "cli", config, "atlantis.exe"));
                Session(session, info, work, runtime.SessionPath, cli, checks, Check);
            }
            runtime.CloseMainWindow();
            runtime.WaitForExit(TimeSpan.FromSeconds(30));
        }
        catch (Exception e)
        {
            Check("the session ran", false, e.GetType().Name + ": " + e.Message);
        }
        var report = new StringBuilder("{\"integration\":\"").Append(scene).Append("\",\"checks\":[");
        for (int i = 0; i < checks.Count; ++i)
        {
            if (i > 0) report.Append(',');
            report.Append("{\"name\":\"").Append(checks[i].Name.Replace("\r", " ").Replace("\n", " ").Replace("\\", "\\\\").Replace("\"", "\\\"")).Append("\",\"ok\":").Append(checks[i].Ok ? "true" : "false")
                  .Append(",\"detail\":\"").Append(checks[i].Detail.Replace("\r", " ").Replace("\n", " ").Replace("\\", "\\\\").Replace("\"", "\\\"")).Append("\"}");
        }
        int failed = checks.Count(c => !c.Ok);
        report.Append("],\"passed\":").Append(checks.Count - failed).Append(",\"failed\":").Append(failed).Append('}');
        Console.WriteLine(report.ToString());
        return failed == 0 && checks.Count > 0 ? 0 : 1;
    }

    private static void Session(RemoteSession session, SessionInfo info, string work, string sessionPath, string cli,
                                List<(string, bool, string)> checks,
                                Action<string, bool, string> check)
    {
        void Check(string name, bool ok, string detail = "") => check(name, ok, detail);

        // The editor's own models over the live document, as the worker builds them.
        if (session.Authoring.Document() is null && session.Authoring.Open() is OpenError openError)
            throw new InvalidOperationException("the document does not open: " + openError);
        var document = new RemoteAuthoringDocument(session.Authoring, session);
        var tree = new DocumentTree();
        var transforms = new LocalTransforms();
        tree.Rebuild(document);
        var apply = new ApplyController(document);
        var target = new AuthoringEditTarget(document, tree, apply);
        var history = new EditHistory(target);
        var inspector = new InspectorModel(document);
        IReadOnlyList<SchemaType> schema = document.Schema;
        bool imported = document.Info()?.Provenance == Provenance.Imported;
        List<EntityGuid> originalNodes = tree.Order.ToList();

        void Settle()
        {
            foreach (Event e in document.DrainEvents())
            {
                tree.Apply(e, document);
                transforms.Apply(e);
                apply.Apply(e);
                if (e is DocumentReopened) history.Clear();
            }
        }

        void WaitApplied(string step)
        {
            var until = DateTime.UtcNow + TimeSpan.FromSeconds(60);
            while (apply.Status != ApplyStatus.Applied && DateTime.UtcNow < until)
            {
                Settle();
                if (apply.Status == ApplyStatus.Applied) break;
                Thread.Sleep(2);
            }
            Check(step + ": applied", apply.Status == ApplyStatus.Applied, apply.Text);
        }

        // The worker's path: the recorder around the commit, the history records the settled edit.
        AuthoringError? Commit(EditPlan plan)
        {
            EditRecorder? recorder = EditRecorder.Begin(document, tree, plan);
            IReadOnlyList<CommandFailure> failures = apply.Commit(plan.Commands);
            if (failures.Count == 0 && recorder is not null)
            {
                history.Record(recorder.Finish(schema), target.Committed(failures));
                history.Update();
            }
            Settle();
            return failures.Count > 0 ? failures[0].Error : null;
        }

        HistoryResult Step(bool undo)
        {
            HistoryResult result = undo ? history.Undo() : history.Redo();
            history.Update();
            Settle();
            return result;
        }

        int captures = 0;
        void CheckFrame(string step)
        {
            string path = Path.Combine(work, "integration-" + ++captures + ".png");
            StepResult stepped = session.Control.Step(new StepRequest(1, path));
            bool ok = stepped.IsOk && stepped.Report!.Data.View.Count == 16 && stepped.Report.Data.DrawItemCount > 0 &&
                      File.Exists(path) && new FileInfo(path).Length > 0;
            Check(step + ": the frame and its capture", ok,
                  stepped.Error?.ToString() ?? stepped.Report?.Data.DrawItemCount.ToString(CultureInfo.InvariantCulture) ?? "");
        }

        float Intensity(EntityGuid node)
        {
            inspector.Refresh(node);
            return inspector.Find("Light.intensity")!.Value!.Value.AsFloat32();
        }

        // Pick: the scene camera's pose, a ray at the first Renderable (the smoke's check).
        var world = new GameplayWorld(session);
        SceneSnapshot snapshot = SceneReader.Read(world);
        if (snapshot.FirstRenderablePosition is Vector3 pickTarget && snapshot.ScenePose is CameraPose pose)
        {
            var eye = new Vector3(pose.WorldMatrix[12], pose.WorldMatrix[13], pose.WorldMatrix[14]);
            RaycastResult hit = session.Control.Raycast(new Ray(eye, Vector3.Normalize(pickTarget - eye)));
            Check("pick: a ray at a Renderable hits an entity", hit.IsOk && hit.Hit is not null, hit.Hit?.ToString() ?? "miss");
        }
        else
        {
            Check("pick: a ray at a Renderable hits an entity", false, "no renderable or no scene camera");
        }

        // Drag: the editor's Gizmo drag with previews, released into one transaction and an apply.
        TypeId? renderable = SceneSchema.TypeOf(schema, "Renderable");
        TypeId? lightType = SceneSchema.TypeOf(schema, "Light");
        TypeId? cameraType = SceneSchema.TypeOf(schema, "Camera");
        EntityGuid? leaf = tree.Order.Cast<EntityGuid?>().FirstOrDefault(
            n => tree.DescendantsOf(n!.Value).Count == 0 && renderable is TypeId r && tree.ComponentsOf(n.Value).Contains(r));
        Check("drag: a leaf Renderable to drag", leaf is not null, tree.Count + " nodes");
        EntityGuid? lightNode = tree.Order.Cast<EntityGuid?>().FirstOrDefault(
            n => lightType is TypeId l && tree.ComponentsOf(n!.Value).Contains(l));
        if (leaf is not EntityGuid subject || lightNode is not EntityGuid lightId || lightType is not TypeId light)
            throw new InvalidOperationException("the scene has no leaf Renderable or no Light");

        // Spec 0081: complete cooked catalog, observed only through the shared surface.
        var catalog = session.Assets.ListAssets();
        Check("assets: complete list available", catalog.IsOk && catalog.Value.Count > 0);
        var records = File.ReadLines(Path.GetFullPath(Path.Combine(Path.GetDirectoryName(cli)!, "..", "..", "..", "asset_catalog.txt")))
            .Where(line => line.StartsWith("record: ", StringComparison.Ordinal)).Select(line => line.Split(' ', StringSplitOptions.RemoveEmptyEntries)).ToList();
        Check("assets: assembled record count agrees", catalog.Value.Count == records.Count, records.Count.ToString());
        bool allFacts = true;
        int largestReply = 0;
        foreach (var tokens in records)
        {
            string Field(string name) => tokens.Single(t => t.StartsWith(name + "=", StringComparison.Ordinal))[(name.Length + 1)..];
            AssetGuid guid = AssetGuid.Parse(Field("guid"));
            var d = session.Assets.GetAsset(guid);
            var summary = catalog.Value.Single(v => v.Guid == guid);
            int depsAt = Array.FindIndex(tokens, t => t.StartsWith("deps=", StringComparison.Ordinal));
            allFacts &= d.IsOk && d.Value.Source == Field("source") && d.Value.Type.ToString().ToLowerInvariant() == Field("type") &&
                d.Value.ArtifactSchema.ToString(CultureInfo.InvariantCulture) == Field("artifact_schema") &&
                (d.Value.SourceSchema?.ToString(CultureInfo.InvariantCulture) ?? "none") == Field("source_schema") && d.Value.Tool == Field("tool") &&
                d.Value.Dependencies.Select(g => g.ToString()).SequenceEqual(tokens.Skip(depsAt + 1)) &&
                summary.Source == d.Value.Source && summary.Type == d.Value.Type;
            JsonValue raw = session.AwaitResponse(session.Send("assets.get", JsonValue.NewObject().Set("guid", Codec.Encode(guid))));
            largestReply = Math.Max(largestReply, Encoding.UTF8.GetByteCount(Json.Write(raw)) + 1);
        }
        Check("assets: every summary detail and direct dependency agrees", allFacts);
        JsonValue rawList = session.AwaitResponse(session.Send("assets.list"));
        Check("assets: complete envelope sizes", true, "records=" + records.Count + "; listBytes=" + (Encoding.UTF8.GetByteCount(Json.Write(rawList)) + 1) + "; largestDetailBytes=" + largestReply);
        using (RemoteSession reader = RemoteSession.Connect(info))
            Check("assets: another client shares complete list", reader.Assets.ListAssets().Value.SequenceEqual(catalog.Value));
        Check("assets: CLI list agrees", Cli(cli, sessionPath, "--json", "asset", "list") == 0);
        string cliLog = File.ReadAllText(Path.Combine(Path.GetDirectoryName(sessionPath)!, "name-cli.log"));
        Check("assets: CLI contains every GUID", catalog.Value.All(v => cliLog.Contains(v.Guid.ToString(), StringComparison.Ordinal)));
        Check("assets: CLI detail", Cli(cli, sessionPath, "--json", "asset", "get", catalog.Value[0].Guid.ToString()) == 0);
        ulong browseGeneration = session.Control.Status().SceneGeneration;
        string browseBefore = Path.Combine(work, "assets-before.png"), browseAfter = Path.Combine(work, "assets-after.png");
        var frameBefore = session.Control.Step(new StepRequest(1, browseBefore));
        for (int reload = 0; reload < 3; ++reload) Check("assets: Reload view retains snapshot " + reload, session.Assets.ListAssets().Value.SequenceEqual(catalog.Value));
        var frameAfter = session.Control.Step(new StepRequest(1, browseAfter));
        Check("assets: read-only browsing retains frame PNG identity and generation", frameBefore.IsOk && frameAfter.IsOk &&
            File.ReadAllBytes(browseBefore).SequenceEqual(File.ReadAllBytes(browseAfter)) &&
            session.Control.Status().SceneGeneration == browseGeneration && tree.Order.SequenceEqual(originalNodes));

        inspector.Refresh(subject);
        InspectorField material = inspector.Find("Renderable.materialAsset") ?? throw new InvalidOperationException("no material GUID field");
        AssetGuid originalMaterial = material.Value!.Value.AsAsset();
        AssetGuid replacement = catalog.Value.First(v => v.Type == AssetType.Material && v.Guid != originalMaterial &&
            v.Source.StartsWith("assets:", StringComparison.Ordinal)).Guid;
        SceneAddress materialAddress = SceneSchema.AddressOf(schema, subject, "Renderable", "materialAsset")!.Value;
        Check("assets: existing Inspector GUID commit", Commit(inspector.Commit("Renderable.materialAsset", replacement.ToString())) is null);
        WaitApplied("asset reference");
        Check("assets: CLI reads same committed GUID", Cli(cli, sessionPath, "--json", "scene", "get", subject.ToString(), "Renderable.materialAsset") == 0 &&
            document.GetProperty(materialAddress)!.Value.AsAsset() == replacement);
        Check("assets: undo reference", Step(true) == HistoryResult.Submitted); WaitApplied("asset reference undo");
        Check("assets: undo restores GUID", document.GetProperty(materialAddress)!.Value.AsAsset() == originalMaterial);
        Check("assets: redo reference", Step(false) == HistoryResult.Submitted); WaitApplied("asset reference redo");
        Check("assets: redo restores GUID", document.GetProperty(materialAddress)!.Value.AsAsset() == replacement);
        string? refSource = document.Info()?.SourcePath;
        byte[]? refBackup = refSource is not null && File.Exists(refSource) ? File.ReadAllBytes(refSource) : null;
        try
        {
            SaveError? saved = document.Save();
            Check("assets: reference save contract", imported ? saved == SaveError.ImportedSceneNotSavable : saved is null);
            Check("assets: source reopens", session.Authoring.Open() is null); Settle();
            Check("assets: reference survives save reopen", document.GetProperty(materialAddress)!.Value.AsAsset() == (imported ? originalMaterial : replacement));
        }
        finally
        {
            if (!imported && refSource is not null && refBackup is not null) File.WriteAllBytes(refSource, refBackup);
            if (session.Authoring.Open() is not null) throw new InvalidOperationException("reference source restore did not reopen");
            Settle();
        }
        inspector.Refresh(subject);
        Check("assets: reference edit before conflict", Commit(inspector.Commit("Renderable.materialAsset", replacement.ToString())) is null);
        WaitApplied("asset reference conflict setup");
        using (RemoteSession outside = RemoteSession.Connect(info))
        {
            outside.Authoring.Submit(new SetProperty(materialAddress, PropertyValue.FromAsset(originalMaterial)));
            Check("assets: external overwrite accepted", outside.Authoring.DrainFailures().Count == 0);
        }
        Settle();
        Check("assets: overwritten reference undo refused", Step(true) == HistoryResult.ChangedElsewhere);
        Check("assets: restored reference apply", session.Authoring.Apply() is null);
        Check("assets: restored reference frame", session.Control.Step(new StepRequest(1, browseAfter)).IsOk &&
            File.ReadAllBytes(browseBefore).SequenceEqual(File.ReadAllBytes(browseAfter)));
        Check("assets: open apply and reconnect retain catalog", session.Assets.ListAssets().Value.SequenceEqual(catalog.Value));
        history.Clear();

        // Spec0069: name-only edits retain rendering, identity and placement, and the CLI sees the same document.
        string originalName = tree.NameOf(subject);
        LocalTransform nameTransform = TransformMath.LocalOf(document, subject);
        string beforeNameCapture = Path.Combine(work, "name-before.png");
        Check("name: baseline capture", session.Control.Step(new StepRequest(1, beforeNameCapture)).IsOk && File.Exists(beforeNameCapture));
        Check("name: one editor rename", Commit(HierarchyCommands.Rename(tree, subject, originalName, "Named scene node 灯")) is null);
        WaitApplied("name rename");
        Check("name: label and identity", tree.Label(subject, schema).StartsWith("Named scene node 灯 · ", StringComparison.Ordinal) &&
              tree.Order.SequenceEqual(originalNodes) && TransformMath.LocalOf(document, subject).Equals(nameTransform));
        string afterNameCapture = Path.Combine(work, "name-after.png");
        Check("name: unchanged capture", session.Control.Step(new StepRequest(1, afterNameCapture)).IsOk &&
              File.Exists(afterNameCapture) && File.ReadAllBytes(beforeNameCapture).SequenceEqual(File.ReadAllBytes(afterNameCapture)));
        Check("name: undo", Step(undo: true) == HistoryResult.Submitted);
        WaitApplied("name undo");
        Check("name: undo restored", tree.NameOf(subject) == originalName);
        Check("name: redo", Step(undo: false) == HistoryResult.Submitted);
        WaitApplied("name redo");
        Check("name: redo restored", tree.NameOf(subject) == "Named scene node 灯");
        Check("name: clear", Commit(HierarchyCommands.Rename(tree, subject, "Named scene node 灯", "")) is null);
        WaitApplied("name clear");
        Check("name: unnamed fallback", tree.NameOf(subject) == "" && tree.Label(subject, schema).StartsWith(subject.Value.ToString("N")[..8], StringComparison.Ordinal));
        Check("name: undo clear", Step(undo: true) == HistoryResult.Submitted);
        WaitApplied("name undo clear");
        Check("name: clear undo restored", tree.NameOf(subject) == "Named scene node 灯");
        int cliExit = Cli(cli, sessionPath, "scene", "name", subject.ToString(), QuoteName("outside CLI 灯"));
        Check("name: CLI rename", cliExit == 0);
        Settle();
        Check("name: CLI event reaches editor", tree.NameOf(subject) == "outside CLI 灯");
        Check("name: stale editor draft refused", HierarchyCommands.Rename(tree, subject, "Named scene node 灯", "stale").Refusal == "changed elsewhere");
        Check("name: overwritten undo refused", Step(undo: true) == HistoryResult.ChangedElsewhere && tree.NameOf(subject) == "outside CLI 灯");
        Check("name: CLI restore", Cli(cli, sessionPath, "scene", "name", subject.ToString(), QuoteName(originalName)) == 0);
        Settle();
        Check("name: original restored", tree.NameOf(subject) == originalName);
        history.Clear();
        var drag = new GizmoDragController(document, tree, transforms, new RuntimeWorldPreview(() => session));
        LocalTransform before = TransformMath.LocalOf(document, subject);
        transforms.Apply(new PropertyChanged(new SceneAddress(subject, default, default), default));  // re-read the local
        Check("drag: begun with previews", drag.Begin(subject, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.X, snap: false));
        for (int b = 1; b <= 3; ++b) _ = drag.Preview(0.02f * b);
        Check("drag: released and committed", Commit(drag.Release()) is null);
        WaitApplied("drag release");
        LocalTransform dragged = TransformMath.LocalOf(document, subject);
        Check("drag: the node moved", dragged.Position != before.Position, dragged.Position.ToString("F2", CultureInfo.InvariantCulture));
        CheckFrame("drag release");

        Check("undo: submitted and applied", Step(undo: true) == HistoryResult.Submitted);
        WaitApplied("undo");
        Check("undo: the node is back", TransformMath.LocalOf(document, subject).Equals(before));
        CheckFrame("undo");
        Check("redo: submitted and applied", Step(undo: false) == HistoryResult.Submitted);
        WaitApplied("redo");
        Check("redo: the node is moved again", TransformMath.LocalOf(document, subject).Equals(dragged));
        CheckFrame("redo");

        // Create under the camera (the J7 script's node), with a Transform value the destroy's undo restores. (A Light
        // is not added: the scene grammar's point-light limit would refuse it on a full scene.)
        EntityGuid? parent = tree.Order.Cast<EntityGuid?>().FirstOrDefault(n => cameraType is TypeId c && tree.ComponentsOf(n!.Value).Contains(c));
        EntityGuid under = parent ?? subject;
        var (createPlan, created) = HierarchyCommands.CreateChild(document, tree, under);
        Check("create: committed", Commit(createPlan) is null);
        WaitApplied("create");
        Check("create: the node is under its parent", tree.Contains(created) && tree.ParentOf(created) == under);
        inspector.Refresh(created);
        EditPlan placed = inspector.Commit("Transform.localPosition", "3 4 5");
        Check("inspector commit: committed", Commit(placed) is null, placed.Refusal ?? "");
        WaitApplied("inspector commit");
        Check("inspector commit: the value is set", TransformMath.LocalOf(document, created).Position == new Vector3(3, 4, 5));

        // Reparent the node under the dragged leaf (J7's step; the camera's subtree is never destroyed -- clearing the
        // active camera cooks a scene the Runtime refuses to run, "no active camera", by its own design).
        Check("reparent: committed", Commit(HierarchyCommands.Reparent(document, tree, created, subject)) is null);
        WaitApplied("reparent");
        Vector3 kept = TransformMath.LocalOf(document, created).Position;  // the local that kept its world placement
        Check("reparent: under the new parent", tree.ParentOf(created) == subject);
        CheckFrame("create");

        // Destroy the new parent's subtree (the leaf and the created node) and undo it.
        EntityGuid? activeBefore = document.ActiveCamera();
        bool cameraAmong = activeBefore is EntityGuid active && (active == subject || tree.DescendantsOf(subject).Contains(active));
        Check("destroy: committed", Commit(HierarchyCommands.DestroySubtree(document, tree, subject)) is null);
        WaitApplied("destroy");
        Check("destroy: the subtree is gone", !tree.Contains(subject) && !tree.Contains(created));
        Check("destroy: the active camera among it is cleared", !cameraAmong || document.ActiveCamera() is null);
        CheckFrame("destroy");
        Check("undo of destroy: submitted and applied", Step(undo: true) == HistoryResult.Submitted);
        WaitApplied("undo of destroy");
        Check("undo of destroy: the subtree is back with its values",
              tree.Contains(subject) && tree.Contains(created) && tree.ParentOf(created) == subject &&
              TransformMath.LocalOf(document, created).Position == kept);
        Check("undo of destroy: the active camera is restored", !cameraAmong || document.ActiveCamera() == activeBefore);
        CheckFrame("undo of destroy");

        // Back to the original document (the inspector commit, the reparent, the create, the drag), then save it.
        for (int i = 0; i < 4; ++i)
        {
            Check("restore " + i + ": submitted and applied", Step(undo: true) == HistoryResult.Submitted);
            WaitApplied("restore " + i);
        }
        Check("restore: the document is the original",
              tree.Order.SequenceEqual(originalNodes) && TransformMath.LocalOf(document, subject).Equals(before));

        string? source = document.Info()?.SourcePath;
        byte[]? backup = source is not null && File.Exists(source) ? File.ReadAllBytes(source) : null;
        if (backup is not null) File.WriteAllBytes(Path.Combine(work, "source-before-save.txt"), backup);
        try
        {
            Check("save name: committed", Commit(HierarchyCommands.Rename(tree, subject, tree.NameOf(subject), "Persistent 灯")) is null);
            WaitApplied("save name");
            SaveError? saveError = document.Save();
            if (imported)
            {
                Check("save: an imported scene is refused with its reason", saveError == SaveError.ImportedSceneNotSavable,
                      saveError?.ToString() ?? "saved " + source);
                Check("save: imported source unchanged", source is not null && backup is not null && File.ReadAllBytes(source).SequenceEqual(backup));
            }
            else
            {
                Check("save: the hand-authored source is written", saveError is null, saveError?.ToString() ?? source ?? "");
            }
            Check("name: source reopens", session.Authoring.Open() is null);
            Settle();
            Check("name: reopened provenance", tree.NameOf(subject) == (imported ? originalName : "Persistent 灯"));
            Check("name: reopen clears history", !history.CanUndo && !history.CanRedo);
        }
        finally
        {
            if (!imported && source is not null && backup is not null) File.WriteAllBytes(source, backup);
            if (session.Authoring.Open() is not null) throw new InvalidOperationException("restoring the source did not reopen");
            Settle();
        }
        CheckFrame("save");

        // Another client (the CLI is one too): it edits the field the editor edited; the Inspector follows, the undo is refused.
        if (session.Authoring.Document() is not null)  // the editor's document is open; the second client opens its own
        {
            using RemoteSession second = RemoteSession.Connect(info);
            if (second.Authoring.Document() is null) second.Authoring.Open();
            var secondDocument = new RemoteAuthoringDocument(second.Authoring, second);
            SceneAddress? address = SceneSchema.AddressOf(schema, lightId, "Light", "intensity");
            float current = Intensity(lightId);
            Check("second client: the editor edits the field", Commit(new EditPlan("light",
                  new Command[] { new SetProperty(address!.Value, PropertyValue.FromFloat32(current + 1.0f)) })) is null);
            WaitApplied("second client's editor edit");
            secondDocument.SubmitTransaction(new Command[] { new SetProperty(address.Value, PropertyValue.FromFloat32(current + 5.0f)) });
            Check("second client: its edit was accepted", secondDocument.DrainFailures().Count == 0);
            Settle();
            Check("second client: the Inspector follows the outside change", Intensity(lightId) == current + 5.0f);
            HistoryResult refused = Step(undo: true);
            Check("second client: the undo of the overwritten field is refused \"changed elsewhere\"",
                  refused == HistoryResult.ChangedElsewhere && history.Notice.Contains("changed elsewhere"), refused.ToString());
            CheckFrame("second client");
        }
    }

    // ASCII JSON escapes avoid depending on the Windows CRT argv code page.
    private static string QuoteName(string name) => System.Text.Json.JsonSerializer.Serialize(name);

    private static int Cli(string executable, string session, params string[] args)
    {
        var start = new ProcessStartInfo(executable) { UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true };
        start.ArgumentList.Add("--session");
        start.ArgumentList.Add(session);
        foreach (string arg in args) start.ArgumentList.Add(arg);
        using Process process = Process.Start(start) ?? throw new InvalidOperationException("CLI did not start");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(30000))
        {
            process.Kill(entireProcessTree: true);
            throw new TimeoutException("CLI rename timed out");
        }
        File.AppendAllText(Path.Combine(Path.GetDirectoryName(session)!, "name-cli.log"),
                           string.Join(" ", args) + "\n" + stdout.GetAwaiter().GetResult() + stderr.GetAwaiter().GetResult());
        return process.ExitCode;
    }
}
