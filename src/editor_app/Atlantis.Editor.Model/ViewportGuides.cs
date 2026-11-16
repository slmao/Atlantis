using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

/// <summary>Verified authoring inputs only. The guide uses the existing viewport overlay primitives.</summary>
public static class ViewportGuides
{
    public const int MaxChainNodes = 64;
    private static readonly OverlayColor Accent = new(0.36f, 0.65f, 1.0f, 0.92f);
    private static readonly string[] TransformFields = ["localPosition", "localEulerAnglesRadians", "localScale"];

    public sealed record Result(IReadOnlyList<OverlayGroup> Groups, string? Unavailable = null)
    {
        public static readonly Result Empty = new([]);
    }

    public static ViewportOverlay Compose(ViewportOverlay ordinary, Result guides) => guides.Groups.Count == 0
        ? ordinary : ordinary with { Groups = ordinary.Groups.Concat(guides.Groups).ToArray() };

    // Retain the observed chain even for an incomplete read, so fixing a missing field can refresh it.
    public static IReadOnlyList<EntityGuid> Chain(DocumentTree tree, EntityGuid node, out bool complete)
    {
        var chain = new List<EntityGuid>();
        var seen = new HashSet<EntityGuid>();
        for (EntityGuid? id = node; id is EntityGuid current; id = tree.ParentOf(current))
        {
            if (chain.Count == MaxChainNodes || !tree.Contains(current) || !seen.Add(current))
            {
                complete = false;
                return chain;
            }
            chain.Add(current);
        }
        complete = true;
        return chain;
    }

    public static Result Read(IAuthoringDocument document, EntityGuid subject, IReadOnlyList<EntityGuid> chain,
        bool complete, int width, int height)
    {
        if (width <= 0 || height <= 0) return Result.Empty;
        var schema = document.Schema;
        var camera = SceneSchema.TypeOf(schema, "Camera");
        var light = SceneSchema.TypeOf(schema, "Light");
        var membership = document.ObserveComponents([subject]);
        if (membership is not [var types]) return new([], "Camera/light components could not be verified.");
        bool hasCamera = camera is TypeId c && types.Contains(c);
        bool hasLight = light is TypeId l && types.Contains(l);
        if (!hasCamera && !hasLight) return Result.Empty;
        if (!complete || chain.Count is < 1 or > MaxChainNodes || chain[0] != subject)
            return new([], "Transform chain unavailable (up to 64 nodes).");

        var addresses = new List<SceneAddress>();
        int Field(EntityGuid node, string component, string field)
        {
            if (SceneSchema.AddressOf(schema, node, component, field) is not SceneAddress address) return -1;
            addresses.Add(address);
            return addresses.Count - 1;
        }
        var transforms = chain.Select(node => TransformFields.Select(f => Field(node, "Transform", f)).ToArray()).ToArray();
        int fov = -1, near = -1, far = -1, kind = -1, range = -1;
        if (hasCamera)
        {
            fov = Field(subject, "Camera", "fovYRadians");
            near = Field(subject, "Camera", "nearZ");
            far = Field(subject, "Camera", "farZ");
        }
        if (hasLight)
        {
            kind = Field(subject, "Light", "kind");
            range = Field(subject, "Light", "range");
        }
        var values = document.GetProperties(addresses);
        if (values.Count != addresses.Count) return new([], "Guide inputs could not be verified.");
        PropertyValue? Value(int index) => index < 0 ? null : values[index];
        Vector3? Vector(int index) => Value(index) is PropertyValue v && v.TryGetVec3(out var vector) && Finite(vector) ? vector : null;
        float? Scalar(int index) => Value(index) is PropertyValue v && v.Kind == PropertyKind.Float32 && float.IsFinite(v.AsFloat32())
            ? v.AsFloat32() : null;

        float[] world = WorldMatrices.Identity();
        for (int i = transforms.Length - 1; i >= 0; --i)
        {
            var fields = transforms[i];
            if (Vector(fields[0]) is not Vector3 position || Vector(fields[1]) is not Vector3 euler ||
                Vector(fields[2]) is not Vector3 scale)
                return new([], "Transform fields could not be verified.");
            world = WorldMatrices.Multiply(world, WorldMatrices.ComposeLocal(new LocalTransform(position, euler, scale)));
            if (!world.All(float.IsFinite)) return new([], "Transform exceeds guide precision.");
        }

        var groups = new List<OverlayGroup>(2);
        var unavailable = new List<string>(2);
        if (hasCamera)
        {
            OverlayGroup? guide = Scalar(fov) is float f && Scalar(near) is float n && Scalar(far) is float z
                ? Frustum(world, new CameraLens(f, n, z), (float)width / height) : null;
            if (guide is null) unavailable.Add("camera frustum");
            else groups.Add(guide);
        }
        if (hasLight)
        {
            var descriptor = schema.FirstOrDefault(t => t.Id == light)?.Fields.FirstOrDefault(f => f.Name == "kind");
            var constant = descriptor is { Kind: TypeKind.Enum } && Value(kind) is PropertyValue v && v.Kind == PropertyKind.Enum
                ? schema.FirstOrDefault(t => t.Id == descriptor.Type && t.Kind == TypeKind.Enum)?
                    .Constants.FirstOrDefault(e => e.Value == v.AsEnum())?.Name : null;
            OverlayGroup? guide = constant switch
            {
                "Directional" => Direction(world),
                "Point" when Scalar(range) is float r => Point(new(world[12], world[13], world[14]), r),
                _ => null
            };
            if (guide is null) unavailable.Add("light guide");
            else groups.Add(guide);
        }
        return new(groups, unavailable.Count > 0 ? "Unavailable: " + string.Join(", ", unavailable) + "." : null);
    }

    public static OverlayGroup? Frustum(float[] world, CameraLens lens, float aspect)
    {
        if (world.Length != 16 || !world.All(float.IsFinite) ||
            !(lens.FovYRadians > 0 && lens.FovYRadians < MathF.PI && lens.NearZ > 0 && lens.FarZ > lens.NearZ) ||
            !float.IsFinite(lens.FarZ) || !(aspect > 0) || !float.IsFinite(aspect) ||
            CameraConvention.Matrices(new CameraPose(world, lens), aspect) is not { } matrices ||
            !matrices.View.All(float.IsFinite) || !matrices.Projection.All(float.IsFinite))
            return null;
        var eye = new Vector3(world[12], world[13], world[14]);
        var right = new Vector3(matrices.View[0], matrices.View[4], matrices.View[8]);
        var up = new Vector3(matrices.View[1], matrices.View[5], matrices.View[9]);
        var forward = new Vector3(-matrices.View[2], -matrices.View[6], -matrices.View[10]);
        if (eye + lens.NearZ * forward == eye + lens.FarZ * forward) return null;
        var corners = new Vector3[8];
        for (int plane = 0; plane < 2; ++plane)
        {
            float d = plane == 0 ? lens.NearZ : lens.FarZ;
            float h = d * MathF.Tan(lens.FovYRadians / 2);
            float w = h * aspect;
            var center = eye + d * forward;
            corners[plane * 4] = center - w * right - h * up;
            corners[plane * 4 + 1] = center + w * right - h * up;
            corners[plane * 4 + 2] = center + w * right + h * up;
            corners[plane * 4 + 3] = center - w * right + h * up;
        }
        var edges = new List<OverlaySegment>(12);
        for (int i = 0; i < 4; ++i)
        {
            int next = (i + 1) % 4;
            edges.Add(Line(corners[i], corners[next]));
            edges.Add(Line(corners[4 + i], corners[4 + next]));
            edges.Add(Line(corners[i], corners[4 + i]));
        }
        return Valid(edges) ? OverlayGroup.World(segments: edges) : null;
    }

    public static OverlayGroup? Point(Vector3 center, float range)
    {
        if (!Finite(center) || !(range > 0) || !float.IsFinite(range)) return null;
        // A circle collapsed along one axis can keep distinct consecutive points but lose its radius.
        if (center.X + range == center.X || center.X - range == center.X ||
            center.Y + range == center.Y || center.Y - range == center.Y ||
            center.Z + range == center.Z || center.Z - range == center.Z) return null;
        const int sides = 24;
        var edges = new List<OverlaySegment>(3 * sides);
        foreach (var (u, v) in new[] { (Vector3.UnitX, Vector3.UnitY), (Vector3.UnitX, Vector3.UnitZ), (Vector3.UnitY, Vector3.UnitZ) })
        {
            var ring = new Vector3[sides];
            for (int i = 0; i < sides; ++i)
            {
                float angle = i * MathF.Tau / sides;
                ring[i] = center + range * (u * MathF.Cos(angle) + v * MathF.Sin(angle));
            }
            for (int i = 0; i < sides; ++i) edges.Add(Line(ring[i], ring[(i + 1) % sides]));
        }
        return Valid(edges) ? OverlayGroup.World(segments: edges) : null;
    }

    public static OverlayGroup? Direction(float[] world)
    {
        if (world.Length != 16 || !world.All(float.IsFinite)) return null;
        var forward = new Vector3(-world[8], -world[9], -world[10]);
        float length = CameraConvention.Length(forward);
        if (!(length >= 1e-6f) || !float.IsFinite(length)) return null;
        forward /= length;
        Vector3 reference = MathF.Abs(forward.Y) < .9f ? Vector3.UnitY : Vector3.UnitX;
        Vector3 side = Vector3.Normalize(Vector3.Cross(forward, reference));
        Vector3 up = Vector3.Cross(side, forward);
        Vector3 neck = forward * .72f;
        OverlaySegment[] lines = [Line(Vector3.Zero, forward),
            Line(forward, neck + .15f * side), Line(forward, neck - .15f * side),
            Line(forward, neck + .15f * up), Line(forward, neck - .15f * up)];
        return Valid(lines)
            ? new(new OverlayAnchor(new(world[12], world[13], world[14]), 40), lines, [], [])
            : null;
    }

    private static OverlaySegment Line(Vector3 a, Vector3 b) => new(a, b, Accent, WidthPixels: 2);
    private static bool Valid(IEnumerable<OverlaySegment> edges) =>
        edges.All(edge => Finite(edge.A) && Finite(edge.B) && edge.A != edge.B);
    private static bool Finite(Vector3 v) => float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);
}
