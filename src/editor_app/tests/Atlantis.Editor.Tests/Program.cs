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
// Plan 0066 M5: the pick overlay's Gizmo builder -- its primitives for a known
// matrix, and where the Runtime's screen-constant scaling (the
// viewport_overlay shader's) puts them for a known camera, against E1's
// ImGui handles (editor_view.cpp: 80-pixel axes along each axis's projected
// direction).
// Plan 0068 M1 (ModelVectors.cs): Atlantis.Editor.Model against every vector
// file the C++ models generate (ADR-0137 D1).
//
//   Atlantis.Editor.Tests --repo <source dir>
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using Atlantis.Editor.Model;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private const float Tolerance = 2e-5f;
    private static int s_passed;
    private static int s_failed;

    public static int Main(string[] args)
    {
        string? repo = null, runtime = null, cli = null, scene = "integrated_showcase_demo";
        bool integration = false, acceptance = false;
        for (int i = 0; i < args.Length; ++i)
        {
            if (args[i] == "--repo" && i + 1 < args.Length) repo = args[++i];
            else if (args[i] == "--integration") integration = true;
            else if (args[i] == "--j7") acceptance = true;
            else if (args[i] == "--runtime" && i + 1 < args.Length) runtime = args[++i];
            else if (args[i] == "--cli" && i + 1 < args.Length) cli = args[++i];
            else if (args[i] == "--scene" && i + 1 < args.Length) scene = args[++i];
        }
        if (integration)
        {
            // Plan 0068 M7 (P5): the live scripted session against atlantis_runtime (GPU).
            return runtime is null ? Usage() : IntegrationTest.Run(runtime, scene);
        }
        if (acceptance)
        {
            // Plan 0068 M7 (J7): the owner's acceptance run, scripted (GPU).
            return runtime is null || cli is null ? Usage() : J7Acceptance.Run(runtime, cli, scene);
        }
        if (repo is null)
        {
            return Usage();
        }
        try
        {
            Run(Path.Combine(repo, "tests", "csharp", "conformance", "camera_vectors.jsonl"));
            DragReportChecks();
            GizmoChecks(Path.Combine(repo, "tests", "csharp", "conformance", "camera_vectors.jsonl"));
            // Plan 0068 M1 (ADR-0137 D1): every ported model against its vectors.
            string conformance = Path.Combine(repo, "tests", "csharp", "conformance");
            PickingChecks(Path.Combine(conformance, "picking_vectors.jsonl"));
            GizmoVectorChecks(Path.Combine(conformance, "gizmo_vectors.jsonl"));
            WorldMatrixChecks(Path.Combine(conformance, "world_matrix_vectors.jsonl"));
            HistoryTranscriptChecks(Path.Combine(conformance, "history_transcripts.jsonl"));
            // Plan 0068 M2: the Hierarchy's model, its commands (J4) and the apply controller, against a fake document.
            HierarchyChecks();
            ApplyControllerChecks();
            // Plan 0068 M3: the Inspector's model.
            InspectorChecks();
            // Plan 0068 M4: the Gizmo drag controller (previews, J3's cap, the release, J4's refusal).
            GizmoDragChecks();
            // Plan 0068 M5: the history over the authoring document, structural records included.
            HistoryChecks();
        }
        catch (Exception e)
        {
            Fail("an unexpected " + e.GetType().Name + ": " + e);
        }
        Console.WriteLine($"camera convention: {s_passed} checks passed, {s_failed} failed");
        return s_failed == 0 && s_passed > 0 ? 0 : 1;
    }

    private static int Usage()
    {
        Console.Error.WriteLine("usage: Atlantis.Editor.Tests --repo <source dir>");
        Console.Error.WriteLine("       Atlantis.Editor.Tests --integration --runtime <exe> [--scene <name>]");
        return 2;
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
                    var camera = new EditorCamera();
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
                    if (!CameraKindPlan0068(kind, name, data)) Fail(name + ": unknown kind " + kind);
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

    // Plan 0066 M5: one entity at a known WorldMatrix, under the C++ model's first camera of the vectors (an oblique
    // view) and under a head-on one (the X and Y axes parallel to the image plane).
    private static void GizmoChecks(string vectorsPath)
    {
        var entity = EntityGuid.Parse("01234567-89ab-4def-8123-456789abcdef");
        float[] matrix = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -18.5f, 1.25f, 17.0f, 1 };  // in front of the vectors' camera
        var position = new Vector3(matrix[12], matrix[13], matrix[14]);
        ViewportOverlay overlay = GizmoOverlay.ForPick(entity, position);
        Check(overlay.Selection.SequenceEqual(new[] { entity }) && overlay.Groups.Count == 1, "gizmo: the pick is selected, one group");
        OverlayGroup group = overlay.Groups[0];
        Check(group.Anchor == new OverlayAnchor(position, 80.0f), "gizmo: anchored at the matrix's translation, 80 pixels per unit");
        Vector3[] axes = { Vector3.UnitX, Vector3.UnitY, Vector3.UnitZ };
        OverlayColor[] colors = { new(230 / 255.0f, 60 / 255.0f, 60 / 255.0f), new(70 / 255.0f, 200 / 255.0f, 70 / 255.0f),
                                  new(70 / 255.0f, 120 / 255.0f, 240 / 255.0f) };
        Check(group.Segments.Count == 3 && group.Points.Count == 0 &&
              group.Segments.Select((s, i) => s == new OverlaySegment(Vector3.Zero, axes[i], colors[i], OverlayDepth.OnTop, 3.0f)).All(x => x),
              "gizmo: three on-top 3-pixel axis segments in E1's axis colours");
        bool tips = group.Triangles.Count == 3 * 16;
        for (int t = 0; tips && t < group.Triangles.Count; ++t)
        {
            OverlayTriangle triangle = group.Triangles[t];
            int axis = t / 16;
            Vector3 along = axes[axis];
            bool mantle = t % 2 == 0;
            float alongA = Vector3.Dot(triangle.A, along);
            float radiusB = (triangle.B - along * Vector3.Dot(triangle.B, along)).Length();
            float radiusC = (triangle.C - along * Vector3.Dot(triangle.C, along)).Length();
            tips = triangle.Color == colors[axis] && triangle.Depth == OverlayDepth.OnTop &&
                   MathF.Abs(alongA - (mantle ? 1.125f : 1.0f)) < 1e-6f &&
                   MathF.Abs(Vector3.Dot(triangle.B, along) - 1.0f) < 1e-6f && MathF.Abs(Vector3.Dot(triangle.C, along) - 1.0f) < 1e-6f &&
                   MathF.Abs(radiusB - 0.0625f) < 1e-6f && MathF.Abs(radiusC - 0.0625f) < 1e-6f;
        }
        Check(tips, "gizmo: each axis's cone tip, 10 pixels long and 5 wide each side, 16 on-top triangles");

        // The oblique camera: each axis drawn along E1's handle direction.
        string first = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(vectorsPath)).Split('\n')[0];
        JsonValue data = Json.Parse(first).Find("data")!;
        var oblique = new CameraMatrices(Floats(data.Find("view")!), Floats(data.Find("projection")!));
        float aspect = Float(data.Find("aspect")!);
        const float height = 720.0f;
        float width = height * aspect;
        int directions = 0;
        bool aligned = true;
        Vector2 center = Screen(oblique, position, width, height);
        for (int axis = 0; axis < 3; ++axis)
        {
            Vector2 handle = Screen(oblique, position + axes[axis], width, height) - center;  // E1: the projected unit axis
            if (handle.Length() < 1e-3f) continue;
            Vector2 drawn = Screen(oblique, Drawn(oblique, position, axes[axis], height), width, height) - center;
            aligned &= Vector2.Distance(Vector2.Normalize(drawn), Vector2.Normalize(handle)) < 1e-3f;
            ++directions;
        }
        Check(directions == 3 && aligned, "gizmo: under the C++ model's camera, each axis is drawn along E1's handle direction");

        // Head-on: the X and Y axes are E1's 80 pixels long, their tips 10 pixels beyond.
        CameraMatrices headOn = CameraConvention.Matrices(new CameraPose(new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 },
                                                                         CameraLens.Default), 16.0f / 9.0f)!;
        var ahead = new Vector3(1.5f, -0.75f, -12.0f);
        Vector2 origin = Screen(headOn, ahead, 1280.0f, 720.0f);
        bool lengths = true;
        for (int axis = 0; axis < 2; ++axis)
        {
            float end = Vector2.Distance(Screen(headOn, Drawn(headOn, ahead, axes[axis], 720.0f), 1280.0f, 720.0f), origin);
            float tip = Vector2.Distance(Screen(headOn, Drawn(headOn, ahead, axes[axis] * 1.125f, 720.0f), 1280.0f, 720.0f), origin);
            lengths &= MathF.Abs(end - 80.0f) < 0.01f && MathF.Abs(tip - 90.0f) < 0.01f;
        }
        Check(lengths, "gizmo: head-on, the X and Y axes are 80 pixels long with their tips at 90");
    }

    // The viewport_overlay shader's toWorld(): a screen-constant offset at the anchor's view depth, 80 pixels per unit.
    private static Vector3 Drawn(CameraMatrices camera, Vector3 anchor, Vector3 offset, float heightPixels)
    {
        float w = Clip(camera, anchor).W;
        float worldPerPixel = 2.0f * w / (MathF.Abs(camera.Projection[5]) * heightPixels);
        return anchor + offset * (GizmoOverlay.HandlePixels * worldPerPixel);
    }

    private static Vector4 Clip(CameraMatrices camera, Vector3 p)
    {
        float[] v = camera.View, m = camera.Projection;
        var e = new Vector4(v[0] * p.X + v[4] * p.Y + v[8] * p.Z + v[12], v[1] * p.X + v[5] * p.Y + v[9] * p.Z + v[13],
                            v[2] * p.X + v[6] * p.Y + v[10] * p.Z + v[14], 1.0f);
        return new Vector4(m[0] * e.X + m[4] * e.Y + m[8] * e.Z + m[12], m[1] * e.X + m[5] * e.Y + m[9] * e.Z + m[13],
                           m[2] * e.X + m[6] * e.Y + m[10] * e.Z + m[14], m[3] * e.X + m[7] * e.Y + m[11] * e.Z + m[15]);
    }

    private static Vector2 Screen(CameraMatrices camera, Vector3 p, float width, float height)
    {
        Vector4 c = Clip(camera, p);
        return new Vector2((c.X / c.W + 1.0f) * 0.5f * width, (c.Y / c.W + 1.0f) * 0.5f * height);
    }

    private static void Check(bool ok, string name)
    {
        if (ok) ++s_passed;
        else ++s_failed;
        Console.WriteLine((ok ? "  ok    " : "  FAIL  ") + name);
    }

    private static void Fail(string name) => Check(false, name);
}
