using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;
using SkiaSharp;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void ViewportGuideChecks()
    {
        float[] identity = WorldMatrices.Identity();
        var lens = new CameraLens(MathF.PI / 2, 1, 4);
        var wide = ViewportGuides.Frustum(identity, lens, 2);
        var narrow = ViewportGuides.Frustum(identity, lens, .5f);
        Check(wide is { Segments.Count: 12, Anchor: null } &&
            wide.Segments.All(e => e.Depth == OverlayDepth.Tested) &&
            wide.Segments[0].A == new Vector3(-2, -1, -1) &&
            wide.Segments[1].B == new Vector3(8, -4, -4) &&
            narrow?.Segments[0].A == new Vector3(-.5f, -1, -1),
            "camera guide uses the selected lens, actual aspect and twelve depth-tested edges");
        var rolled = WorldMatrices.ComposeLocal(new LocalTransform(Vector3.Zero, new(0, 0, MathF.PI / 3), new(2, 3, .5f)));
        Check(ViewportGuides.Frustum(rolled, lens, 2)?.Segments.SequenceEqual(wide!.Segments) == true,
            "scale and roll that preserve forward do not change the Runtime camera basis");
        var yaw = WorldMatrices.ComposeLocal(new LocalTransform(new(5, 1, 5), new(0, MathF.PI / 2, 0), Vector3.One));
        var yawGuide = ViewportGuides.Frustum(yaw, lens, 2);
        Check(yawGuide is not null && MathF.Abs(yawGuide.Segments[0].A.X - 4) < 1e-5f,
            "translated rotated subject moves the frustum with its authored camera");
        var vertical = WorldMatrices.ComposeLocal(new LocalTransform(Vector3.Zero, new(MathF.PI / 2, 0, 0), Vector3.One));
        var direction = ViewportGuides.Direction(vertical);
        Check(ViewportGuides.Frustum(vertical, lens, 2) is null &&
            direction is { Anchor.PixelsPerUnit: 40, Segments.Count: 5 } &&
            Vector3.Dot(direction.Segments[0].B, Vector3.UnitY) > .999f && direction.Segments.All(s => s.A != s.B),
            "camera rejects its vertical basis while a vertical directional light still forms an arrow");
        Check(ViewportGuides.Frustum(identity, lens with { NearZ = 0 }, 2) is null &&
            ViewportGuides.Frustum(identity, lens with { FarZ = float.PositiveInfinity }, 2) is null &&
            ViewportGuides.Frustum(identity, lens with { FovYRadians = MathF.PI }, 2) is null &&
            ViewportGuides.Frustum(identity, new CameraLens(1, 1e20f, 1e25f), 2) is null,
            "unusable camera lens and overflow never produce partial geometry");
        var ring = ViewportGuides.Point(new(4, 5, 6), 3);
        Check(ring is { Segments.Count: 72, Anchor: null } &&
            ring.Segments.All(s => MathF.Abs(Vector3.Distance(s.A, new(4, 5, 6)) - 3) < 1e-5f),
            "point guide matches the authored radius around the world position");
        Check(ViewportGuides.Point(new(float.MaxValue, 0, 0), 1) is null &&
            ViewportGuides.Point(Vector3.Zero, float.NaN) is null && ViewportGuides.Point(Vector3.Zero, -1) is null,
            "point guide rejects collapsed and invalid circles");

        var doc = new FakeDocument();
        var parent = doc.Add(1, null, new(new(1, 2, 3), Vector3.Zero, new(2, 3, 4)));
        var selected = doc.Add(2, parent, new(new(1, 1, 1), Vector3.Zero, Vector3.One), FakeDocument.CameraType, FakeDocument.Light);
        doc.SubmitTransaction([new SetProperty(new(selected, FakeDocument.Light, FakeDocument.Kind), PropertyValue.FromEnum(1)),
            new SetProperty(new(selected, FakeDocument.Light, FakeDocument.Range), PropertyValue.FromFloat32(3))]);
        var tree = new DocumentTree();
        tree.Rebuild(doc);
        var chain = ViewportGuides.Chain(tree, selected, out bool complete);
        var partial = ViewportGuides.Read(doc, selected, chain, complete, 800, 600);
        Check(partial is { Groups.Count: 1, Unavailable: not null } && partial.Unavailable.Contains("camera") &&
            partial.Groups[0].Segments[0].A == new Vector3(6, 5, 7),
            "missing camera fields omit only that guide; ancestor scale moves the point center without scaling its radius");
        // The real authoring host enforces ComponentExclusivity. This model fixture can cover both shapes defensively.
        var fixture = new GuideReadFixture(doc);
        fixture.Values[new(selected, FakeDocument.CameraType, GuideReadFixture.Fov)] = PropertyValue.FromFloat32(MathF.PI / 3);
        fixture.Values[new(selected, FakeDocument.CameraType, GuideReadFixture.Near)] = PropertyValue.FromFloat32(.5f);
        fixture.Values[new(selected, FakeDocument.CameraType, GuideReadFixture.Far)] = PropertyValue.FromFloat32(12);
        var both = ViewportGuides.Read(fixture, selected, chain, complete, 800, 600);
        Check(both.Groups.Count == 2 && both.Groups.Sum(group => group.Segments.Count) == 84 && both.Unavailable is null,
            "the model can compose complete camera and point inputs in a synthetic dual-component fixture");
        fixture.Values[new(parent, FakeDocument.Transform, FakeDocument.Position)] = null;
        var incomplete = ViewportGuides.Read(fixture, selected, chain, complete, 800, 600);
        Check(incomplete.Groups.Count == 0 && incomplete.Unavailable?.Contains("Transform") == true,
            "missing ancestor property omits guides without substituting an identity transform");
        Check(ViewportGuides.Read(doc, selected, chain, complete, 0, 600).Groups.Count == 0,
            "collapsed drawable suppresses guide input");
        EntityGuid? head = selected;
        for (int n = 3; n <= 66; ++n) head = doc.Add(n, head, new(Vector3.Zero, Vector3.Zero, Vector3.One));
        tree.Rebuild(doc);
        Check(ViewportGuides.Chain(tree, FakeDocument.Guid(64), out bool atLimit).Count == 64 && atLimit &&
            ViewportGuides.Chain(tree, FakeDocument.Guid(65), out bool overLimit).Count == 64 && !overLimit,
            "guide ancestor traversal stops at the 64 node budget");
        var ordinary = ViewportOverlay.Select(selected);
        Check(ReferenceEquals(ViewportGuides.Compose(ordinary, ViewportGuides.Result.Empty), ordinary) &&
            ViewportGuides.Compose(ordinary, new([wide!, ring!])) is { Groups.Count: 2 } combined &&
            combined.Selection.SequenceEqual(ordinary.Selection) && combined.Groups.Sum(g => g.Segments.Count) == 84,
            "guide composition preserves ordinary selection and uses at most 84 segments");
    }

    private sealed class GuideReadFixture(FakeDocument source) : IAuthoringDocument
    {
        public static readonly FieldId Fov = new(101), Near = new(102), Far = new(103);
        public Dictionary<SceneAddress, PropertyValue?> Values { get; } = new();
        public IReadOnlyList<SchemaType> Schema { get; } = source.Schema.Select(t => t.Id != FakeDocument.CameraType ? t :
            t with { Fields = t.Fields.Concat(new[] { Lens(Fov, "fovYRadians"), Lens(Near, "nearZ"), Lens(Far, "farZ") }).ToArray() }).ToArray();
        private static SchemaField Lens(FieldId id, string name) =>
            new(id, name, TypeKind.Primitive, PrimitiveKind.Float32, default, FieldFlags.Serializable);
        public DocumentInfo? Info() => source.Info();
        public IReadOnlyList<NodeSummary> ListNodes() => source.ListNodes();
        public IReadOnlyList<TypeId> ListComponents(EntityGuid node) => source.ListComponents(node);
        public PropertyValue? GetProperty(SceneAddress address) =>
            Values.TryGetValue(address, out var value) ? value : source.GetProperty(address);
        public EntityGuid? ActiveCamera() => source.ActiveCamera();
        public TransactionTicket SubmitTransaction(IReadOnlyList<Atlantis.Gameplay.Authoring.Command> commands) => source.SubmitTransaction(commands);
        public IReadOnlyList<Atlantis.Gameplay.Authoring.CommandFailure> DrainFailures() => source.DrainFailures();
        public IReadOnlyList<Atlantis.Gameplay.Authoring.Event> DrainEvents() => source.DrainEvents();
        public ApplyError? Apply() => source.Apply();
        public Outcome<ApplyReceipt, ApplyError> ApplyTracked() => source.ApplyTracked();
        public Outcome<ApplySnapshot, ApplyLookupError> ApplyStatus(ApplyId? id = null) => source.ApplyStatus(id);
        public SaveError? Save() => source.Save();
    }

    private static int RunGuideCaptures(string runtimePath, string scene)
    {
        string work = RuntimeProcess.NewWorkDirectory();
        Console.WriteLine("Guide captures and Runtime log: " + work);
        using var runtime = RuntimeProcess.Start(new RuntimeLaunch(Path.GetFullPath(runtimePath), scene, null, work));
        try
        {
            SessionInfo host = runtime.WaitForSession(TimeSpan.FromSeconds(25));
            using var a = RemoteSession.Connect(host);
            using var b = RemoteSession.Connect(host);
            a.Control.Pause();
            var camera = WorldMatrices.Identity();
            camera[13] = 1.5f;
            camera[14] = 8;
            a.Control.SetViewportCamera(new(camera, MathF.PI / 3, .1f, 100f));
            SKColor[] Capture(RemoteSession connection, string name, bool overlay = false)
            {
                string path = Path.Combine(work, name + ".png");
                if (!connection.Control.Step(new StepRequest(1, path, overlay)).IsOk)
                    throw new InvalidOperationException("Capture failed: " + name);
                using var bitmap = SKBitmap.Decode(path) ?? throw new InvalidOperationException("Could not decode " + path);
                return bitmap.Pixels;
            }
            static void Require(bool ok, string behavior)
            {
                if (!ok) throw new InvalidOperationException(behavior);
                Console.WriteLine("PASS " + behavior);
            }
            var ordinary = ViewportOverlay.Select();
            var point = ViewportGuides.Point(new(-1.8f, 1.5f, 2.5f), .75f)!;
            float[] lightWorld = WorldMatrices.Identity();
            lightWorld[8] = -1; lightWorld[10] = 0;
            lightWorld[12] = 1.5f; lightWorld[13] = 1.5f; lightWorld[14] = 3;
            var arrow = ViewportGuides.Direction(lightWorld)!;
            var baseline = Capture(a, "default-off");
            Require(a.Control.SetViewportOverlay(ViewportGuides.Compose(ordinary, new([point]))) is null, "A submits the candidate point guide");
            Require(b.Control.SetViewportOverlay(ViewportGuides.Compose(ordinary, new([arrow]))) is null, "B submits the candidate direction guide");
            var onlyA = Capture(a, "own-A", true);
            var onlyB = Capture(b, "own-B", true);
            Require(!onlyA.SequenceEqual(baseline) && !onlyB.SequenceEqual(baseline) && !onlyA.SequenceEqual(onlyB),
                "both owners capture visibly distinct guide geometry");
            Require(Capture(a, "default-on").SequenceEqual(baseline) && Capture(b, "default-B").SequenceEqual(baseline),
                "default captures have identical decoded pixels with both guide overlays active");
            Require(a.Control.SetViewportOverlay(ViewportGuides.Compose(ordinary, new([arrow]))) is null &&
                Capture(a, "A-replaced", true).SequenceEqual(onlyB) && Capture(b, "B-after-A-replace", true).SequenceEqual(onlyB),
                "A replaces its overlay and B retains its own");
            Require(a.Control.SetViewportOverlay(null) is null && Capture(a, "A-cleared", true).SequenceEqual(baseline) &&
                Capture(b, "B-after-A-clear", true).SequenceEqual(onlyB), "clearing A neither shows nor clears B");
            a.Control.SetViewportOverlay(ViewportGuides.Compose(ordinary, new([point])));
            b.Dispose();
            Require(Capture(a, "A-after-B-disconnect", true).SequenceEqual(onlyA), "disconnecting B preserves A's overlay");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
        finally
        {
            runtime.CloseMainWindow();
            runtime.WaitForExit(TimeSpan.FromSeconds(8));
        }
    }
}
