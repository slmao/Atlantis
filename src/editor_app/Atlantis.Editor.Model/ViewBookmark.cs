using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

public sealed record ViewportObservation(string Name, Vector3 Eye, Vector3 Forward, CameraLens Lens, RenderLookExtent Extent)
{
    public (int Width, int Height) Size => ((int)Extent.Width, (int)Extent.Height);

    public static ViewportObservation? Read(string name, CameraPose pose, RenderLookExtent extent)
    {
        if (pose.WorldMatrix.Length != 16 || !pose.WorldMatrix.All(float.IsFinite)) return null;
        var m = pose.WorldMatrix;
        var view = new ViewportObservation(name, new(m[12], m[13], m[14]),
            Vector3.Normalize(new(-m[8], -m[9], -m[10])), pose.Lens, extent);
        return view.IsValid ? view : null;
    }

    public bool IsValid => Extent.IsValid && Finite(Eye) && Finite(Forward) &&
        float.IsFinite(Lens.FovYRadians) && Lens.FovYRadians > 0 && Lens.FovYRadians < MathF.PI &&
        float.IsFinite(Lens.NearZ) && Lens.NearZ > 0 && float.IsFinite(Lens.FarZ) && Lens.FarZ > Lens.NearZ &&
        MathF.Abs(Forward.LengthSquared() - 1) <= 0.001f &&
        Vector3.Cross(Forward, Vector3.UnitY).LengthSquared() > 1e-12f &&
        CameraConvention.Matrices(Pose(), (float)Extent.Width / Extent.Height) is { } matrices &&
        matrices.View.All(float.IsFinite) && matrices.Projection.All(float.IsFinite);

    public CameraPose Pose()
    {
        Vector3 f = Vector3.Normalize(Forward);
        Vector3 r = Vector3.Normalize(Vector3.Cross(f, Vector3.UnitY));
        Vector3 u = Vector3.Cross(r, f);
        return new([r.X, r.Y, r.Z, 0, u.X, u.Y, u.Z, 0, -f.X, -f.Y, -f.Z, 0, Eye.X, Eye.Y, Eye.Z, 1], Lens);
    }

    public bool Matches(IReadOnlyList<float> view, IReadOnlyList<float> projection) =>
        CameraConvention.Matrices(Pose(), (float)Extent.Width / Extent.Height) is { } expected &&
        Close(view, expected.View) && Close(projection, expected.Projection);

    private static bool Finite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private static bool Close(IReadOnlyList<float> actual, float[] expected) =>
        actual.Count == 16 && actual.Zip(expected).All(pair => MathF.Abs(pair.First - pair.Second) <= 0.0001f);
}

public sealed record ViewBookmark(Guid Id, ViewportObservation View);
