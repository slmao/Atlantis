// Plan 0064 M5 (P4): the C# camera-convention test. The editor's port of the
// E1 camera (src/editor_app/Atlantis.Editor/EditorCamera.cs) against
// tests/csharp/conformance/camera_vectors.jsonl, which the C++ model writes
// (and tests/runtime/editor_camera_convention_tests.cpp holds to the host):
// view and projection from a pose, the pick ray through a point, and the
// orbit/dolly navigation step by step. The first pose after a reset is the
// reset pose bit for bit; the rest agree to float rounding (C# and C++ math
// libraries may differ in the last bit).
// Plan 0066 M1: also the drag measurement's report -- the UI gaps classified
// as during a drag, at a transition or idle, and the latency statistics.
//
//   Atlantis.Editor.Tests --repo <source dir>
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor.Tests;

public static class Program
{
    private const float Tolerance = 2e-5f;
    private static int s_passed;
    private static int s_failed;

    public static int Main(string[] args)
    {
        string? repo = null;
        for (int i = 0; i + 1 < args.Length; ++i)
        {
            if (args[i] == "--repo") repo = args[i + 1];
        }
        if (repo is null)
        {
            Console.Error.WriteLine("usage: Atlantis.Editor.Tests --repo <source dir>");
            return 2;
        }
        try
        {
            Run(Path.Combine(repo, "tests", "csharp", "conformance", "camera_vectors.jsonl"));
            DragReportChecks();
        }
        catch (Exception e)
        {
            Fail("an unexpected " + e.GetType().Name + ": " + e);
        }
        Console.WriteLine($"camera convention: {s_passed} checks passed, {s_failed} failed");
        return s_failed == 0 && s_passed > 0 ? 0 : 1;
    }

    private static void Run(string path)
    {
        string text = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(path));
        Check(!text.Contains('\r'), "the vectors file has LF line endings");
        int matrices = 0, rays = 0, navigations = 0;
        foreach (string line in text.TrimEnd('\n').Split('\n'))
        {
            JsonValue parsed = Json.Parse(line);
            string name = parsed.Find("case")!.AsString;
            string kind = parsed.Find("kind")!.AsString;
            JsonValue data = parsed.Find("data")!;
            switch (kind)
            {
                case "matrices":
                {
                    ++matrices;
                    CameraPose pose = Pose(data.Find("pose")!);
                    float aspect = Float(data.Find("aspect")!);
                    CameraMatrices? actual = CameraConvention.Matrices(pose, aspect);
                    JsonValue view = data.Find("view")!;
                    if (view.IsNull)
                    {
                        Check(actual is null, name + ": no matrices (degenerate)");
                    }
                    else
                    {
                        Check(actual is not null && Near(actual.View, Floats(view)) && Near(actual.Projection, Floats(data.Find("projection")!)),
                              name + ": view and projection");
                    }
                    break;
                }
                case "ray":
                {
                    ++rays;
                    var camera = new CameraMatrices(Floats(data.Find("view")!), Floats(data.Find("projection")!));
                    float[] ndc = Floats(data.Find("ndc")!);
                    PickRay ray = CameraConvention.RayThroughNdc(camera, ndc[0], ndc[1]);
                    Check(Near(ray.Origin, Floats(data.Find("origin")!)) && Near(ray.Direction, Floats(data.Find("direction")!)),
                          name + ": origin and direction");
                    break;
                }
                case "navigation":
                {
                    ++navigations;
                    var camera = new OrbitCamera();
                    camera.Reset(Pose(data.Find("reset")!));
                    foreach (JsonValue step in data.Find("steps")!.Items)
                    {
                        string op = step.Find("op")!.AsString;
                        if (op != "reset")
                        {
                            float[] a = Floats(step.Find("args")!);
                            if (op == "orbit") camera.Orbit(a[0], a[1]);
                            else camera.Dolly(a[0]);
                        }
                        CameraPose expected = Pose(step.Find("pose")!);
                        CameraPose actual = camera.Pose();
                        bool same = op == "reset"
                            ? actual.WorldMatrix.SequenceEqual(expected.WorldMatrix)  // bit for bit
                            : Near(actual.WorldMatrix, expected.WorldMatrix);
                        Check(same && actual.Lens == expected.Lens && Near(camera.Pivot(), Floats(step.Find("pivot")!)),
                              name + " after " + op + ": pose and pivot");
                    }
                    break;
                }
                default:
                    Fail(name + ": unknown kind " + kind);
                    break;
            }
        }
        Check(matrices >= 9 && rays >= 8 && navigations >= 3,
              $"the vectors have their cases ({matrices} matrices, {rays} rays, {navigations} navigations)");
    }

    private static CameraPose Pose(JsonValue value)
    {
        float[] lens = Floats(value.Find("lens")!);
        return new CameraPose(Floats(value.Find("worldMatrix")!), new CameraLens(lens[0], lens[1], lens[2]));
    }

    private static float[] Floats(JsonValue value) => value.Items.Select(Float).ToArray();

    private static float Float(JsonValue value) =>
        Codec.DecodeFloat(value) ?? throw new FormatException("not a float: " + Json.Write(value));

    private static bool Near(IReadOnlyList<float> actual, IReadOnlyList<float> expected) =>
        actual.Count == expected.Count &&
        actual.Zip(expected).All(p => MathF.Abs(p.First - p.Second) <= Tolerance * MathF.Max(1.0f, MathF.Abs(p.Second)));

    private static bool Near(Vector3 actual, float[] expected) => Near(new[] { actual.X, actual.Y, actual.Z }, expected);

    // Plan 0066 M1: one drag from 1000 ms to 2000 ms; gaps ending inside it, near its press or release, and outside.
    private static void DragReportChecks()
    {
        long Ticks(double ms) => (long)(ms * System.Diagnostics.Stopwatch.Frequency / 1000.0);
        var recorder = new DragRecorder();
        recorder.Press(Ticks(1000));
        recorder.Release(Ticks(2000));
        recorder.UiGap(Ticks(500), 16);   // idle
        recorder.UiGap(Ticks(1100), 120); // overlaps the press's 250 ms: a transition
        recorder.UiGap(Ticks(1600), 40);  // inside the drag
        recorder.UiGap(Ticks(1700), 16);  // inside the drag
        recorder.UiGap(Ticks(2300), 30);  // after the release's 250 ms: idle
        recorder.UiGap(Ticks(2100), 300); // spans the release: a transition
        recorder.Frame(Ticks(1200), 10);
        recorder.Frame(Ticks(1400), 14);  // 50 ms per frame inside the drag
        recorder.Write(new DragWrite(Ticks(1100), 60, 110, 14, 1));
        recorder.Write(new DragWrite(Ticks(1300), 70, 150, 16, 2));
        DragReport report = recorder.Report("test");
        Check(report.Drags == 1 && report.Writes == 2, "drag report: one drag, two writes");
        Check(report.UiGapsAtTransitions.Count == 2 && report.UiGapsAtTransitions.Max == 300, "drag report: gaps at the press and release are transitions");
        Check(report.UiGapsInDrag.Count == 2 && report.UiGapsInDrag.Max == 40, "drag report: gaps inside the drag");
        Check(report.UiGapsIdle.Count == 2 && report.UiGapsIdle.Max == 30, "drag report: gaps outside are idle");
        Check(report.MaxUiGap == 300 && report.UiGapsAtTransitions.Over100 == 2, "drag report: the longest gap and the count over 100 ms");
        Check(report.RuntimeFrame.Median == 50 && report.InputToFrame.Max == 150 && report.P95Frames == 3,
              "drag report: Runtime frame time and latency in frames");
    }

    private static void Check(bool ok, string name)
    {
        if (ok) ++s_passed;
        else ++s_failed;
        Console.WriteLine((ok ? "  ok    " : "  FAIL  ") + name);
    }

    private static void Fail(string name) => Check(false, name);
}
