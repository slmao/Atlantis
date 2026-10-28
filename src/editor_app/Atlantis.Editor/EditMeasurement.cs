// Plan 0068 M4 (P6; Spec 0068 Q8): the E3 editor's edit measurement, headless and
// measurement-only (reported against the Spec's targets, never a verdict). It
// starts atlantis_runtime --listen on a scene, opens the authoring document
// and drives real Gizmo drags through the same controller and preview path the
// editor's worker uses (GizmoDragController, RuntimeWorldPreview), with the
// Runtime running free:
//   - Hierarchy open: the time to read the document's tree;
//   - per drag batch, the write -> visible latency (the preview transaction
//     answered, then a status answered after the next frame: the first frame
//     drawn with the write, as Plan 0066 M1 dated it) and the batch cycle (the
//     preview and the overlay resend, the worker's per-batch work);
//   - the release -> Applied time (the authoring transaction submitted and the
//     apply requested, until the Runtime reports Applied), and the same for an
//     Inspector-style commit;
// for a leaf node, and for the node with the largest subtree of at most 256
// descendants (previewed with it), and for one with more than 256 where there
// is one (J3: only the node previews). Prints one JSON object. UI-free.
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
using Event = Atlantis.Gameplay.Authoring.Event;
using PropertyChanged = Atlantis.Gameplay.Authoring.PropertyChanged;

namespace Atlantis.Editor;

public static class EditMeasurement
{
    private const int Drags = 12;
    private const int BatchesPerDrag = 20;

    private sealed record Stats(int N, double Median, double P95, double Max)
    {
        public static Stats Of(List<double> samples)
        {
            if (samples.Count == 0) return new Stats(0, 0, 0, 0);
            var sorted = samples.OrderBy(x => x).ToList();
            return new Stats(sorted.Count, sorted[sorted.Count / 2], sorted[Math.Min(sorted.Count - 1, (int)(sorted.Count * 0.95))], sorted[^1]);
        }

        public string Json() => string.Format(CultureInfo.InvariantCulture, "{{\"n\":{0},\"median\":{1:F2},\"p95\":{2:F2},\"max\":{3:F2}}}", N, Median, P95, Max);
    }

    public static int Run(string runtimePath, string scene, string? outPath)
    {
        string work = RuntimeProcess.NewWorkDirectory();
        var report = new StringBuilder();
        try
        {
            using RuntimeProcess runtime = RuntimeProcess.Start(new RuntimeLaunch(runtimePath, scene, null, work));
            SessionInfo info = runtime.WaitForSession(TimeSpan.FromSeconds(180));
            using (RemoteSession session = RemoteSession.Connect(info))
            {
                session.Control.Resume();
                report.Append("{\"measure\":\"edit\",\"scene\":\"").Append(scene).Append('"');
                Measure(session, report);
                report.Append('}');
            }
            runtime.CloseMainWindow();
            runtime.WaitForExit(TimeSpan.FromSeconds(30));
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("measure-edit failed: " + e);
            return 1;
        }
        string text = report.ToString();
        Console.WriteLine(text);
        if (outPath is not null) File.WriteAllText(outPath, text + "\n");
        return 0;
    }

    private static void Measure(RemoteSession session, StringBuilder report)
    {
        if (session.Authoring.Document() is null && session.Authoring.Open() is OpenError error) throw new InvalidOperationException("the document does not open: " + error);
        var document = new RemoteAuthoringDocument(session.Authoring, session);
        var tree = new DocumentTree();
        var transforms = new LocalTransforms();
        long start = Stopwatch.GetTimestamp();
        tree.Rebuild(document);
        double openMs = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        start = Stopwatch.GetTimestamp();
        IReadOnlyList<HierarchyRow> rows = SceneMarkers.Rows(tree, document.Schema);
        IReadOnlyList<Marker> markers = SceneMarkers.Of(document, tree, transforms);
        double rowsMs = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        report.Append(string.Format(CultureInfo.InvariantCulture,
                                    ",\"nodes\":{0},\"hierarchy_open_ms\":{1:F1},\"rows_and_markers_ms\":{2:F1},\"rows\":{3},\"markers\":{4}",
                                    tree.Count, openMs, rowsMs, rows.Count, markers.Count));

        var apply = new ApplyController(document);
        RemoteSession? previewSession = session;
        var drag = new GizmoDragController(document, tree, transforms, new RuntimeWorldPreview(() => previewSession));
        TypeId? renderable = SceneSchema.TypeOf(document.Schema, "Renderable");
        var counts = tree.Order.ToDictionary(n => n, n => tree.DescendantsOf(n).Count);
        EntityGuid? leaf = tree.Order.Cast<EntityGuid?>().FirstOrDefault(n => counts[n!.Value] == 0 && renderable is TypeId r && tree.ComponentsOf(n.Value).Contains(r));
        EntityGuid? subtree = tree.Order.Where(n => counts[n] is >= 2 and <= GizmoDragController.MaxPreviewDescendants)
                                  .OrderByDescending(n => counts[n]).Cast<EntityGuid?>().FirstOrDefault();
        EntityGuid? big = tree.Order.Where(n => counts[n] > GizmoDragController.MaxPreviewDescendants).OrderBy(n => counts[n]).Cast<EntityGuid?>().FirstOrDefault();

        void Settle()
        {
            foreach (Event e in document.DrainEvents())
            {
                tree.Apply(e, document);
                transforms.Apply(e);
                apply.Apply(e);
            }
        }
        // Submits a plan and waits, the Runtime running free, for its Applied; milliseconds from the submit.
        double CommitAndWait(EditPlan plan)
        {
            long t0 = Stopwatch.GetTimestamp();
            document.SubmitTransaction(plan.Commands);
            if (document.DrainFailures().Count > 0) return double.NaN;
            apply.Request();
            while (apply.Status == ApplyStatus.Applying && Stopwatch.GetElapsedTime(t0).TotalSeconds < 20)
            {
                Thread.Sleep(1);
                Settle();
            }
            return apply.Status == ApplyStatus.Applied ? Stopwatch.GetElapsedTime(t0).TotalMilliseconds : double.NaN;
        }

        // The same commit through ApplyController.Commit (J8), the submit, the failure drain and the apply sent together: each wire call is answered at a
        // frame boundary, so one after another they cost a frame apiece; sent together they share one.
        double CommitPipelinedAndWait(IReadOnlyList<Command> commands)
        {
            long t0 = Stopwatch.GetTimestamp();
            bool refused = apply.Commit(commands).Count > 0;  // the product's path: submit, drain and apply sent together
            while (apply.Status != ApplyStatus.Applied && Stopwatch.GetElapsedTime(t0).TotalSeconds < 20)
            {
                Settle();
                if (apply.Status == ApplyStatus.Applied) break;
                Thread.Sleep(1);
            }
            return refused || apply.Status != ApplyStatus.Applied ? double.NaN : Stopwatch.GetElapsedTime(t0).TotalMilliseconds;
        }

        var subjects = new List<(string Name, EntityGuid? Node)> { ("leaf", leaf), ("subtree", subtree), ("subtree_over_cap", big) };
        report.Append(",\"drags\":[");
        bool firstSubject = true;
        foreach (var (name, node) in subjects)
        {
            if (!firstSubject) report.Append(',');
            firstSubject = false;
            if (node is not EntityGuid subject)
            {
                report.Append("{\"subject\":\"").Append(name).Append("\",\"available\":false}");
                continue;
            }
            var visible = new List<double>();
            var cycle = new List<double>();
            var released = new List<double>();
            var releasedPipelined = new List<double>();
            int previewed = 0;
            for (int d = 0; d < Drags; ++d)
            {
                transforms.Apply(new PropertyChanged(new SceneAddress(subject, default, default), default));  // re-read the document's local transform
                if (!drag.Begin(subject, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.X, snap: false)) break;
                previewed = drag.PreviewedDescendants;
                for (int b = 1; b <= BatchesPerDrag; ++b)
                {
                    long t0 = Stopwatch.GetTimestamp();
                    float[]? world = drag.Preview(0.02f * b);  // the preview transaction answered at a frame boundary
                    _ = session.Control.Status();               // answered after the next frame: the first drawn with the write
                    visible.Add(Stopwatch.GetElapsedTime(t0).TotalMilliseconds);
                    if (world is not null)
                    {
                        var overlay = GizmoOverlay.ForSelection(subject, GizmoOverlay.ModeGizmo(new Vector3(world[12], world[13], world[14]), GizmoMode.Translate,
                                                                 new[] { Vector3.UnitX, Vector3.UnitY, Vector3.UnitZ }, 0), markers);
                        session.Control.SetViewportOverlay(overlay);
                    }
                    cycle.Add(Stopwatch.GetElapsedTime(t0).TotalMilliseconds);
                }
                EditPlan plan = drag.Release();
                if (plan.Refusal is not null) continue;
                if (d % 2 == 0)
                {
                    released.Add(CommitAndWait(plan));
                }
                else
                {
                    releasedPipelined.Add(CommitPipelinedAndWait(plan.Commands));
                }
                // Put it back (not measured) so each drag starts from the same document.
                LocalTransform now = TransformMath.LocalOf(document, subject);
                CommitAndWait(new EditPlan("restore", TransformMath.WriteLocal(document.Schema, subject, now with { Position = now.Position - new Vector3(0.4f, 0, 0) }).Take(1).ToList()));
                Settle();
            }
            report.Append("{\"subject\":\"").Append(name).Append("\",\"descendants\":").Append(counts[subject].ToString(CultureInfo.InvariantCulture))
                  .Append(",\"previewed_descendants\":").Append(previewed.ToString(CultureInfo.InvariantCulture))
                  .Append(",\"write_to_visible_ms\":").Append(Stats.Of(visible).Json())
                  .Append(",\"batch_cycle_ms\":").Append(Stats.Of(cycle).Json())
                  .Append(",\"release_to_applied_ms\":").Append(Stats.Of(released.Where(x => !double.IsNaN(x)).ToList()).Json())
                  .Append(",\"release_to_applied_pipelined_ms\":").Append(Stats.Of(releasedPipelined.Where(x => !double.IsNaN(x)).ToList()).Json()).Append('}');
        }
        report.Append(']');

        // An Inspector-style commit: one SetProperty on a light's intensity, to Applied.
        TypeId? lightType = SceneSchema.TypeOf(document.Schema, "Light");
        EntityGuid? light = tree.Order.Cast<EntityGuid?>().FirstOrDefault(n => lightType is TypeId l && tree.ComponentsOf(n!.Value).Contains(l));
        report.Append(",\"inspector_commit_to_applied_ms\":");
        if (light is EntityGuid lightId)
        {
            var inspector = new InspectorModel(document);
            var times = new List<double>();
            for (int i = 0; i < Drags; ++i)
            {
                inspector.Refresh(lightId);
                float current = inspector.Find("Light.intensity")!.Value!.Value.AsFloat32();
                double ms = CommitAndWait(inspector.Commit("Light.intensity", (current + 0.5f).ToString("R", CultureInfo.InvariantCulture)));
                if (!double.IsNaN(ms)) times.Add(ms);
            }
            inspector.Refresh(lightId);
            CommitAndWait(inspector.Commit("Light.intensity", (inspector.Find("Light.intensity")!.Value!.Value.AsFloat32() - 0.5f * Drags).ToString("R", CultureInfo.InvariantCulture)));
            report.Append(Stats.Of(times).Json());
        }
        else
        {
            report.Append("null");
        }
        FrameTimes frames = FrameSampler.Measure(session.Control, 120);
        report.Append(",\"frame_ms\":").Append(string.Format(CultureInfo.InvariantCulture, "{{\"median\":{0:F2},\"p95\":{1:F2}}}", frames.MedianMs, frames.P95Ms));
    }
}
