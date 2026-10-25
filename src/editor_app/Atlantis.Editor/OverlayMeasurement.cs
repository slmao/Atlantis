// Plan 0066 M6 (Spec 0066 Q6): the Viewport overlay's measurement --
// measurement-only, not an editing feature. With --measure-overlay the editor,
// once the scene has settled, picks the entity under a point of the Viewport
// (its centre, or --pick-ndc; or takes --entity <guid>, to compare a large
// mesh's outline with a small one's) and its session worker
// (EditorWorker.MeasureOverlay):
//   1. samples the Runtime's frame period (back-to-back status answers);
//   2. holds a phase in which no overlay was ever sent ("pristine");
//   3. sends the pick's overlay (GizmoOverlay.ForPick) for the first time and
//      dates it -- the Runtime builds the overlay's pipelines in that frame
//      (Plan 0066 J11), so its excess over the later sends is their one-time
//      cost;
//   4. while orbiting (a camera step before each send), clears and re-sends
//      it, dating each send. The Runtime answers between frames: the set is
//      answered after frame N, the status that follows after frame N+1, the
//      first frame drawn with the overlay (submitted for presentation, as
//      Phase 1's camera latency and M1's drag latency are dated);
//   5. holds the scene camera still and alternates phases without and with
//      the overlay, recording each phase's UTC start and end, so an external
//      sampler of the Runtime process's GPU 3D-engine utilisation (typeperf;
//      the editor takes no package for performance counters) is averaged per
//      phase.
// The report is overlay-report.json beside editor.log.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using Atlantis.Gameplay;

namespace Atlantis.Editor;

/// <summary>
/// What the worker measures: the camera and pick ray (or an entity given by GUID, which skips the pick), and the
/// run's sizes.
/// </summary>
public sealed record OverlayMeasureRequest(CameraPose Pose, PickRay Ray, EntityGuid? Entity, int Sends, int PhasePairs, double PhaseSeconds);

/// <summary>One phase of step 5 (or the pristine one): whether the overlay was set, and when, in UTC.</summary>
public sealed record OverlayPhase(string Name, bool Overlay, DateTime StartUtc, DateTime EndUtc, double FramePeriodMs);

public sealed record OverlayReport(string Label, EntityGuid? Entity, double FramePeriodMs, double FirstSendMs, Stats SendMs,
                                   IReadOnlyList<OverlayPhase> Phases, string? Error = null)
{
    public double Frames(double ms) => FramePeriodMs > 0 ? ms / FramePeriodMs : 0;

    public string ToJson()
    {
        var phases = string.Join(",", Phases.Select(p => string.Format(CultureInfo.InvariantCulture,
            "{{\"name\":\"{0}\",\"overlay\":{1},\"startUtc\":\"{2:O}\",\"endUtc\":\"{3:O}\",\"framePeriodMs\":{4:F3}}}", p.Name,
            p.Overlay ? "true" : "false", p.StartUtc, p.EndUtc, p.FramePeriodMs)));
        return string.Format(CultureInfo.InvariantCulture,
            "{{\"label\":\"{0}\",\"entity\":{1},\"framePeriodMs\":{2:F3},\"firstSendMs\":{3:F1},\"firstSendFrames\":{4:F2}," +
            "\"sendMs\":{5},\"sendFramesMedian\":{6:F2},\"sendFramesP95\":{7:F2},\"phases\":[{8}],\"error\":{9}}}",
            Label, Entity is EntityGuid e ? "\"" + e + "\"" : "null", FramePeriodMs, FirstSendMs, Frames(FirstSendMs), SendMs.ToJson(),
            Frames(SendMs.Median), Frames(SendMs.P95), phases, Error is null ? "null" : "\"" + Error.Replace("\"", "'") + "\"");
    }

    public override string ToString() => Error is not null
        ? Label + ": " + Error
        : string.Format(CultureInfo.InvariantCulture,
                        "{0}: overlay send -> first frame median {1:F1} ms ({2:F2} frames), p95 {3:F1} ms; first send {4:F1} ms; frame {5:F2} ms",
                        Label, SendMs.Median, Frames(SendMs.Median), SendMs.P95, FirstSendMs, FramePeriodMs);
}

public static class OverlayReportFile
{
    public static string Write(string directory, OverlayReport report)
    {
        string path = System.IO.Path.Combine(directory, "overlay-report.json");
        System.IO.File.WriteAllText(path, report.ToJson() + "\n", new UTF8Encoding(false));
        return path;
    }
}
