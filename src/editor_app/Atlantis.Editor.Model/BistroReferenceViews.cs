using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;

namespace Atlantis.Editor.Model;

public sealed record BistroReferenceView(string Name, Vector3 Eye, Vector3 LookAt, string ReferenceFile, string Sha256)
{
    public const int Width = 960;
    public const int Height = 540;
    public const int ReferenceWidth = 1920;
    public const int ReferenceHeight = 1061;
    public const string BistroScene = "c446c084-75ac-8ccb-90ef-b4c13ec25ac0";
    public const string Gallery = "https://developer.nvidia.com/orca/amazon-lumberyard-bistro";
    public const string Attribution = "Amazon Lumberyard Bistro · NVIDIA Open Research Content Archive (ORCA)";

    public static readonly BistroReferenceView Street = new("Street Corner", new(-21f, 4.8f, 17.8f), new(-10.2f, 1.9f, 9.2f),
        "Bistro_Exterior_1.png", "35c3c824fe342e92549a525d11e7b80ea4e1b39fe3d4ef1323e4a688253579dc");
    public static readonly BistroReferenceView Terrace = new("Storefront Terrace", new(-5f, 1.5f, 16f), new(-12f, 1.65f, 12.1f),
        "Bistro_Exterior_2.png", "a5f18071df20c4068dcff6d82c22a2c784148a52e2ed2af679f789e2c0cc0cd0");
    public string ReferenceUrl => "https://d29g4g2dyqv443.cloudfront.net/sites/default/files/akamai/" + ReferenceFile;

    public ViewportObservation Observation => new(Name, Eye, Vector3.Normalize(LookAt - Eye),
        new(1.0472f, 0.1f, 200f), new(Width, Height));

    public CameraPose Pose()
    {
        Vector3 forward = Vector3.Normalize(LookAt - Eye);
        Vector3 right = Vector3.Normalize(Vector3.Cross(forward, Vector3.UnitY));
        Vector3 up = Vector3.Cross(right, forward);
        return new CameraPose([
            right.X, right.Y, right.Z, 0, up.X, up.Y, up.Z, 0,
            -forward.X, -forward.Y, -forward.Z, 0, Eye.X, Eye.Y, Eye.Z, 1
        ], new CameraLens(1.0472f, 0.1f, 200f));
    }

    public bool Matches(IReadOnlyList<float> view, IReadOnlyList<float> projection)
    {
        var expected = CameraConvention.Matrices(Pose(), (float)Width / Height)!;
        return Close(view, expected.View) && Close(projection, expected.Projection);
    }

    private static bool Close(IReadOnlyList<float> actual, float[] expected) =>
        actual.Count == expected.Length && actual.Zip(expected).All(pair => MathF.Abs(pair.First - pair.Second) <= 0.0001f);
}
