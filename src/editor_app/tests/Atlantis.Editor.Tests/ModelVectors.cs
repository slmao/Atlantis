// Plan 0068 M1 (Spec 0068 R2; ADR-0137 D1): Atlantis.Editor.Model against the
// vectors the C++ models generate (tests/csharp/conformance/, written by
// atlantis_csharp_conformance_tests): the whole camera, picking and markers,
// the Gizmo's math and screen handles, world matrices over a hierarchy, and
// E2's edit history replayed step by step against a fake target that behaves
// as the C++ transcript's (its transactions settle at the next Update()).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static IEnumerable<(string Name, string Kind, JsonValue Data)> Lines(string path)
    {
        string text = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(path));
        Check(!text.Contains('\r'), Path.GetFileName(path) + " has LF line endings");
        foreach (string line in text.TrimEnd('\n').Split('\n'))
        {
            JsonValue parsed = Json.Parse(line);
            yield return (parsed.Find("case")!.AsString, parsed.Find("kind")!.AsString, parsed.Find("data")!);
        }
    }

    private static CameraMatrices MatricesOf(JsonValue data) => new(Floats(data.Find("view")!), Floats(data.Find("projection")!));

    private static PixelRect RectOf(JsonValue data)
    {
        float[] r = Floats(data.Find("rect")!);
        return new PixelRect(r[0], r[1], r[2], r[3]);
    }

    private static Vector3 Vec3(JsonValue value)
    {
        float[] f = Floats(value);
        return new Vector3(f[0], f[1], f[2]);
    }

    private static Vector2 Vec2(JsonValue value)
    {
        float[] f = Floats(value);
        return new Vector2(f[0], f[1]);
    }

    private static long Integer(JsonValue value) => long.Parse(value.NumberText, CultureInfo.InvariantCulture);

    private static float[] Columns(JsonValue value) => value.Items.SelectMany(Floats).ToArray();

    private static bool NearOrNull(Vector2? actual, JsonValue expected) =>
        expected.IsNull ? actual is null : actual is Vector2 a && Near(new[] { a.X, a.Y }, Floats(expected));

    private static bool NearScalar(float actual, JsonValue expected) => Near(new[] { actual }, new[] { Float(expected) });

    // camera_vectors.jsonl's Plan 0068 kinds: the whole navigation, and points projected with their depth.
    private static bool CameraKindPlan0068(string kind, string name, JsonValue data)
    {
        switch (kind)
        {
            case "navigation_full":
            {
                var camera = new EditorCamera();
                camera.Reset(Pose(data.Find("reset")!));
                foreach (JsonValue step in data.Find("steps")!.Items)
                {
                    string op = step.Find("op")!.AsString;
                    float[] a = Floats(step.Find("args")!);
                    switch (op)
                    {
                        case "look": camera.Look(a[0], a[1]); break;
                        case "pan": camera.Pan(a[0], a[1], a[2]); break;
                        case "dollyDrag": camera.DollyDrag(a[0]); break;
                        case "fly": camera.Fly(new Vector3(a[0], a[1], a[2]), a[3], false); break;
                        case "flyFast": camera.Fly(new Vector3(a[0], a[1], a[2]), a[3], true); break;
                        case "scaleFlySpeed": camera.ScaleFlySpeed(a[0]); break;
                        case "frame": camera.Frame(new Vector3(a[0], a[1], a[2]), a[3]); break;
                        case "orbit": camera.Orbit(a[0], a[1]); break;
                        default: Fail(name + ": unknown op " + op); continue;
                    }
                    CameraPose expected = Pose(step.Find("pose")!);
                    float[] yawPitch = Floats(step.Find("yawPitch")!);
                    Check(Near(camera.Pose().WorldMatrix, expected.WorldMatrix) && Near(camera.Pivot(), Floats(step.Find("pivot")!)) &&
                          NearScalar(camera.PivotDistance, step.Find("pivotDistance")!) && NearScalar(camera.FlySpeed, step.Find("flySpeed")!) &&
                          Near(new[] { camera.Yaw, camera.Pitch }, yawPitch),
                          name + " after " + op + ": pose, pivot, distance, fly speed, yaw and pitch");
                }
                return true;
            }
            case "project":
            {
                CameraMatrices camera = MatricesOf(data);
                Vector3 point = Vec3(data.Find("point")!);
                Check(NearOrNull(CameraConvention.ProjectToNdc(camera, point), data.Find("ndc")!) &&
                      NearScalar(CameraConvention.ViewDepth(camera, point), data.Find("depth")!),
                      name + ": NDC and view depth");
                return true;
            }
            default:
                return false;
        }
    }

    private static void PickingChecks(string path)
    {
        int markers = 0;
        foreach (var (name, kind, data) in Lines(path))
        {
            CameraMatrices camera = MatricesOf(data);
            PixelRect rect = RectOf(data);
            switch (kind)
            {
                case "pixel":
                    Check(NearOrNull(Picking.PixelOf(camera, rect, Vec3(data.Find("point")!)), data.Find("pixel")!), name + ": pixel");
                    break;
                case "ray":
                {
                    Vector2 pixel = Vec2(data.Find("pixel")!);
                    PickRay ray = Picking.RayThroughPixel(camera, rect, pixel.X, pixel.Y);
                    Check(Near(ray.Origin, Floats(data.Find("origin")!)) && Near(ray.Direction, Floats(data.Find("direction")!)),
                          name + ": origin and direction");
                    break;
                }
                case "marker":
                {
                    ++markers;
                    var list = data.Find("markers")!.Items
                        .Select(m => new Marker(EntityGuid.Parse(m.Find("entity")!.AsString), MarkerKind.Light, Vec3(m.Find("position")!)))
                        .ToList();
                    Vector2 click = Vec2(data.Find("click")!);
                    EntityGuid? picked = Picking.PickMarker(list, camera, rect, click.X, click.Y);
                    JsonValue expected = data.Find("picked")!;
                    Check(expected.IsNull ? picked is null : picked == EntityGuid.Parse(expected.AsString), name + ": the picked marker");
                    break;
                }
                default:
                    Fail(name + ": unknown kind " + kind);
                    break;
            }
        }
        Check(markers >= 9, $"picking vectors have their marker cases ({markers})");
    }

    private static GizmoMode ModeOf(string text) => text switch
    {
        "translate" => GizmoMode.Translate,
        "rotate" => GizmoMode.Rotate,
        _ => GizmoMode.Scale,
    };

    private static void GizmoVectorChecks(string path)
    {
        int drags = 0, grabs = 0;
        foreach (var (name, kind, data) in Lines(path))
        {
            switch (kind)
            {
                case "decompose":
                {
                    Trs? trs = Gizmo.Decompose(Columns(data.Find("matrix")!));
                    JsonValue expected = data.Find("trs")!;
                    if (expected.IsNull)
                    {
                        Check(trs is null, name + ": no TRS form");
                        break;
                    }
                    Check(trs is not null && Near(trs.Translation, Floats(expected.Find("translation")!)) &&
                          Near(trs.Rotation, Floats(expected.Find("rotation")!)) && Near(trs.Scale, Floats(expected.Find("scale")!)) &&
                          Near(Gizmo.Compose(trs), Columns(data.Find("recomposed")!)),
                          name + ": translation, rotation, scale and recomposed");
                    break;
                }
                case "drag":
                {
                    float[] start = Columns(data.Find("start")!);
                    GizmoMode mode = ModeOf(data.Find("mode")!.AsString);
                    GizmoSpace space = data.Find("space")!.AsString == "world" ? GizmoSpace.World : GizmoSpace.Local;
                    var axis = (GizmoAxis)Integer(data.Find("axis")!);
                    Trs trs = Gizmo.Decompose(start)!;
                    bool ok = Near(Gizmo.AxisDirection(trs, mode, space, axis), Floats(data.Find("direction")!));
                    foreach (JsonValue d in data.Find("drags")!.Items)
                    {
                        ++drags;
                        float[] actual = Gizmo.DragMatrix(start, mode, space, axis, Float(d.Find("amount")!), d.Find("snap")!.AsBool);
                        ok &= Near(actual, Columns(d.Find("matrix")!));
                    }
                    Check(ok, name + ": axis direction and drag matrices");
                    break;
                }
                case "snap":
                {
                    GizmoMode mode = ModeOf(data.Find("mode")!.AsString);
                    bool ok = data.Find("pairs")!.Items.Select(Floats).All(p => Near(new[] { Gizmo.SnapAmount(mode, p[0]) }, new[] { p[1] }));
                    Check(ok, name + ": snapped amounts");
                    break;
                }
                case "handles":
                {
                    CameraMatrices camera = MatricesOf(data);
                    PixelRect rect = RectOf(data);
                    GizmoMode mode = ModeOf(data.Find("mode")!.AsString);
                    Vector3[] directions = data.Find("directions")!.Items.Select(Vec3).ToArray();
                    GizmoHandle?[]? handles = Gizmo.Handles(camera, rect, Vec3(data.Find("position")!), directions);
                    JsonValue expected = data.Find("handles")!;
                    if (expected.IsNull)
                    {
                        Check(handles is null, name + ": no handles (behind the camera)");
                        break;
                    }
                    bool ok = handles is not null;
                    for (int axis = 0; ok && axis < 3; ++axis)
                    {
                        JsonValue h = expected.Items[axis];
                        if (h.IsNull)
                        {
                            ok &= handles![axis] is null;
                            continue;
                        }
                        ok &= handles![axis] is GizmoHandle a && Near(new[] { a.Origin.X, a.Origin.Y }, Floats(h.Find("origin")!)) &&
                              Near(new[] { a.Direction.X, a.Direction.Y }, Floats(h.Find("direction")!)) &&
                              Near(new[] { a.End.X, a.End.Y }, Floats(h.Find("end")!)) && NearScalar(a.PixelsPerUnit, h.Find("pixelsPerUnit")!);
                    }
                    foreach (JsonValue g in data.Find("grabs")!.Items)
                    {
                        if (!ok) break;
                        ++grabs;
                        GizmoGrab? grab = Gizmo.Grab(handles!, directions, camera, mode, Vec2(g.Find("press")!));
                        JsonValue axisValue = g.Find("axis")!;
                        if (axisValue.IsNull)
                        {
                            ok &= grab is null;
                            continue;
                        }
                        ok &= grab is GizmoGrab gr && (int)gr.Axis == Integer(axisValue) && NearScalar(gr.StartAngle, g.Find("startAngle")!) &&
                              gr.TowardCamera == g.Find("towardCamera")!.AsBool &&
                              g.Find("amounts")!.Items.Select(Floats).All(t => NearScalar(Gizmo.DragAmount(mode, gr, new Vector2(t[0], t[1])), JsonValue.NumberLiteral(t[2].ToString("R", CultureInfo.InvariantCulture))));
                    }
                    Check(ok, name + ": handles, grabs and drag amounts");
                    break;
                }
                default:
                    Fail(name + ": unknown kind " + kind);
                    break;
            }
        }
        Check(drags >= 200 && grabs >= 30, $"gizmo vectors have their cases ({drags} drags, {grabs} grabs)");
    }

    private static void WorldMatrixChecks(string path)
    {
        int scenes = 0;
        foreach (var (name, _, data) in Lines(path))
        {
            ++scenes;
            var nodes = data.Find("nodes")!.Items.Select(n =>
            {
                JsonValue parent = n.Find("parent")!;
                return ((int?)(parent.IsNull ? null : (int)Integer(parent)),
                        new LocalTransform(Vec3(n.Find("position")!), Vec3(n.Find("rotation")!), Vec3(n.Find("scale")!)));
            }).ToList();
            float[][] world = WorldMatrices.Solve(nodes);
            bool ok = true;
            for (int i = 0; i < nodes.Count; ++i) ok &= Near(world[i], Floats(data.Find("nodes")!.Items[i].Find("world")!));
            Check(ok, name + ": every node's world matrix");
        }
        Check(scenes >= 3, $"world matrix vectors have their scenes ({scenes})");
    }

    // The C++ transcript's fake target, step for step (conformance_tests.cpp, TranscriptTarget).
    private sealed class TranscriptTarget : IEditTarget
    {
        public readonly Dictionary<SceneAddress, PropertyValue> Values = new();
        public bool RefuseNext;
        public readonly List<Invalidation> Invalidations = new();
        public readonly List<IReadOnlyList<(SceneAddress Address, PropertyValue Value)>> Submitted = new();
        private ulong _next = 1;
        private readonly List<(EditHandle Handle, IReadOnlyList<(SceneAddress Address, PropertyValue Value)> Writes, bool Refused)> _pending = new();
        private readonly List<(EditHandle Handle, Settlement Settlement)> _reported = new();

        public PropertyValue? Read(SceneAddress address) => Values.TryGetValue(address, out PropertyValue v) ? v : null;

        public EditHandle Submit(IReadOnlyList<(SceneAddress Address, PropertyValue Value)> writes)
        {
            var handle = new EditHandle(_next++, 1);
            Submitted.Add(writes);
            _pending.Add((handle, writes, RefuseNext));
            RefuseNext = false;
            return handle;
        }

        public void Track(EditHandle handle, IReadOnlyList<(SceneAddress Address, PropertyValue Value)> writes) => _pending.Add((handle, writes, false));

        public Settlement SettlementOf(EditHandle handle)
        {
            foreach (var (h, s) in _reported)
            {
                if (h == handle) return s;
            }
            return Settlement.Pending;
        }

        public string RefusalText(EditHandle handle) => SettlementOf(handle) == Settlement.Refused ? "refused by the fake target" : "";

        public void Update()
        {
            _reported.Clear();
            foreach (var (handle, writes, refused) in _pending)
            {
                if (!refused)
                {
                    foreach (var (address, value) in writes) Values[address] = value;
                }
                _reported.Add((handle, refused ? Settlement.Refused : Settlement.Applied));
            }
            _pending.Clear();
        }

        public IReadOnlyList<Invalidation> DrainInvalidations()
        {
            var drained = Invalidations.ToList();
            Invalidations.Clear();
            return drained;
        }

        public EditHandle Allocate() => new(_next++, 1);
    }

    private static SceneAddress AddressOf(JsonValue w) =>
        new(EntityGuid.Parse(w.Find("node")!.AsString), new TypeId((ulong)Integer(w.Find("component")!)), new FieldId((ulong)Integer(w.Find("field")!)));

    private static void HistoryTranscriptChecks(string path)
    {
        string text = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(path));
        Check(!text.Contains('\r'), "history_transcripts.jsonl has LF line endings");
        int steps = 0;
        foreach (string line in text.TrimEnd('\n').Split('\n'))
        {
            JsonValue parsed = Json.Parse(line);
            string scenario = parsed.Find("scenario")!.AsString;
            var target = new TranscriptTarget();
            var history = new EditHistory(target);
            JsonValue[] script = parsed.Find("steps")!.Items.ToArray();
            // Every address the scenario names starts at 0, as the C++ transcript's three do.
            foreach (JsonValue step in script)
            {
                foreach (JsonValue w in step.Find("writes")?.Items ?? Array.Empty<JsonValue>()) target.Values[AddressOf(w)] = PropertyValue.FromFloat32(0.0f);
            }
            int index = 0;
            foreach (JsonValue step in script)
            {
                ++steps;
                string op = step.Find("op")!.AsString;
                string? result = null;
                switch (op)
                {
                    case "edit":
                    {
                        var writes = step.Find("writes")!.Items
                            .Select(w => new PropertyWrite(AddressOf(w), PropertyValue.FromFloat32(Float(w.Find("before")!)), PropertyValue.FromFloat32(Float(w.Find("value")!))))
                            .ToList();
                        history.Record(new EditTransaction(step.Find("label")!.AsString, writes), target.Allocate());
                        break;
                    }
                    case "update": history.Update(); break;
                    case "undo": result = history.Undo().ToString(); break;
                    case "redo": result = history.Redo().ToString(); break;
                    case "external":
                    {
                        JsonValue w = step.Find("write")!;
                        target.Values[AddressOf(w)] = PropertyValue.FromFloat32(Float(w.Find("value")!));
                        break;
                    }
                    case "invalidate":
                    {
                        JsonValue component = step.Find("component")!;
                        target.Invalidations.Add(new Invalidation(EntityGuid.Parse(step.Find("node")!.AsString),
                                                                  component.IsNull ? null : new TypeId((ulong)Integer(component))));
                        break;
                    }
                    case "refuse_next": target.RefuseNext = true; break;
                    case "clear": history.Clear(); break;
                    default: Fail(scenario + ": unknown op " + op); break;
                }
                JsonValue expect = step.Find("expect")!;
                JsonValue expectedResult = expect.Find("result")!;
                var submitted = target.Submitted.ToList();
                target.Submitted.Clear();
                JsonValue[] expectedSubmitted = expect.Find("submitted")!.Items.ToArray();
                bool same = (expectedResult.IsNull ? result is null : result == expectedResult.AsString) &&
                            submitted.Count == expectedSubmitted.Length &&
                            submitted.Zip(expectedSubmitted).All(p => p.First.Count == p.Second.Items.Count &&
                                p.First.Zip(p.Second.Items).All(w => w.First.Address == AddressOf(w.Second) &&
                                                                    w.First.Value.Equals(PropertyValue.FromFloat32(Float(w.Second.Find("value")!))))) &&
                            history.Waiting == expect.Find("waiting")!.AsBool && history.CanUndo == expect.Find("canUndo")!.AsBool &&
                            history.CanRedo == expect.Find("canRedo")!.AsBool && history.UndoDepth == Integer(expect.Find("undoDepth")!) &&
                            history.RedoDepth == Integer(expect.Find("redoDepth")!) && history.UndoLabel == expect.Find("undoLabel")!.AsString &&
                            history.RedoLabel == expect.Find("redoLabel")!.AsString && history.Notice == expect.Find("notice")!.AsString;
                Check(same, $"history {scenario} step {index++} ({op}): result, submissions and state");
            }
        }
        Check(steps >= 40, $"history transcripts have their steps ({steps})");
    }
}
