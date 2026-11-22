using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;

namespace Atlantis.Gameplay.Remote;

public static partial class Codec
{
    public static bool IsAlignmentRay(Ray ray)
    {
        var direction = DVector.From(ray.Direction);
        return float.IsFinite(ray.Origin.X) && float.IsFinite(ray.Origin.Y) && float.IsFinite(ray.Origin.Z) &&
            Math.Abs(direction.Dot(direction) - 1) <= 1e-4;
    }

    private static float[]? AlignmentFloats(JsonValue? value, int length)
    {
        if (value is not { IsArray: true } || value.Items.Count != length) return null;
        var result = new float[length];
        for (int i = 0; i < length; ++i)
        {
            if (!value.Items[i].IsNumber || DecodeFloat(value.Items[i]) is not float f || !float.IsFinite(f)) return null;
            result[i] = f;
        }
        return result;
    }

    public static Ray? DecodeAlignmentRay(JsonValue value) =>
        value.IsObject && value.Members.Count == 2 &&
        AlignmentFloats(value.Find("origin"), 3) is float[] a && AlignmentFloats(value.Find("direction"), 3) is float[] b &&
        new Ray(new Vector3(a), new Vector3(b)) is { } ray && IsAlignmentRay(ray) ? ray : null;

    public static AlignmentFacesResult? DecodeAlignmentFacesResponse(JsonValue response, IReadOnlyList<Ray> rays)
    {
        if (rays.Count is < 1 or > 2 || !response.IsObject || response.Members.Count != 2 || CountMember(response, "id") != 1)
            return null;
        var result = response.Find("result");
        if (result is null && response.Find("error") is { IsObject: true } error &&
            error.Members.Count == 2 && error.Find("code") is { IsString: true } code &&
            error.Find("message") is { IsString: true } message)
            return new(null, code.AsString == "BadRequest" && message.AsString == "unknown method control.pickAlignmentFaces"
                ? new(AlignmentFaceError.Unsupported) : new(AlignmentFaceError.RpcFailure, Detail: code.AsString + ": " + message.AsString));
        if (result is not { IsObject: true } || result.Members.Count != 1) return null;
        if (result.Find("ok") is { IsObject: true } ok && ok.Members.Count == 2 &&
            ok.Find("sceneGeneration") is { } g && g.TryGetUInt64(out ulong generation) &&
            ok.Find("faces") is { IsArray: true } faces && faces.Items.Count == rays.Count)
        {
            var list = new List<AlignmentFace>(rays.Count);
            for (int i = 0; i < rays.Count; ++i)
            {
                if (ReadFace(faces.Items[i], rays[i]) is not { } face) return null;
                list.Add(face);
            }
            return new(new(generation, list.AsReadOnly()), null);
        }
        if (result.Find("err") is not { IsObject: true } failure ||
            failure.Find("code") is not { IsString: true } token ||
            !Enum.TryParse(token.AsString, out AlignmentFaceError refusal) ||
            !Enum.IsDefined(refusal) || token.AsString != refusal.ToString() || refusal > AlignmentFaceError.Unsupported) return null;
        bool local = refusal is AlignmentFaceError.Miss or AlignmentFaceError.UnsupportedFace or
            AlignmentFaceError.Ambiguous or AlignmentFaceError.TooLarge;
        if (failure.Members.Count != (local ? 2 : 1)) return null;
        if (!local) return new(null, new(refusal));
        if (failure.Find("rayIndex") is not { } index || !index.TryGetUInt64(out ulong ord) || ord >= (ulong)rays.Count)
            return null;
        return new(null, new(refusal, (int)ord));
    }

    private static AlignmentFace? ReadFace(JsonValue v, Ray ray)
    {
        if (!v.IsObject || v.Members.Count != 7 ||
            v.Find("entity") is not { IsString: true } entityToken || DecodeEntity(entityToken) is not EntityGuid entity ||
            entity.IsNil || entity.ToString() != entityToken.AsString ||
            v.Find("mesh") is not { IsString: true } meshToken || DecodeAsset(meshToken) is not AssetGuid mesh ||
            mesh.IsNil || mesh.ToString() != meshToken.AsString ||
            v.Find("triangles") is not { IsArray: true } pair || pair.Items.Count != 2 ||
            !pair.Items[0].TryGetUInt64(out ulong first) || !pair.Items[1].TryGetUInt64(out ulong second) ||
            first >= second || second >= 65536 ||
            v.Find("distance") is not { IsNumber: true } d || DecodeFloat(d) is not float distance ||
            !float.IsFinite(distance) || distance <= 0 ||
            v.Find("corners") is not { IsArray: true } ring || ring.Items.Count != 4 ||
            AlignmentFloats(v.Find("normal"), 3) is not float[] n ||
            AlignmentFloats(v.Find("worldMatrix"), 16) is not float[] m)
            return null;
        var corners = new Vector3[4];
        for (int i = 0; i < corners.Length; ++i)
        {
            if (AlignmentFloats(ring.Items[i], 3) is not float[] c) return null;
            corners[i] = new Vector3(c);
        }
        var normal = new Vector3(n);
        return ValidFaceGeometry(corners, normal, m, ray, distance)
            ? new(entity, mesh, (uint)first, (uint)second, distance, Array.AsReadOnly(corners), normal, Array.AsReadOnly(m))
            : null;
    }

    private readonly record struct DVector(double X, double Y, double Z)
    {
        public static DVector From(Vector3 v) => new(v.X, v.Y, v.Z);
        public static DVector operator -(DVector a, DVector b) => new(a.X-b.X,a.Y-b.Y,a.Z-b.Z);
        public static DVector operator +(DVector a, DVector b) => new(a.X+b.X,a.Y+b.Y,a.Z+b.Z);
        public static DVector operator *(DVector a, double s) => new(a.X*s,a.Y*s,a.Z*s);
        public DVector Cross(DVector b) => new(Y*b.Z-Z*b.Y,Z*b.X-X*b.Z,X*b.Y-Y*b.X);
        public double Dot(DVector b) => X*b.X+Y*b.Y+Z*b.Z;
        public double Length => Math.Sqrt(Dot(this));
    }

    private static bool ValidFaceGeometry(Vector3[] corners, Vector3 normal, float[] m, Ray ray, float distance)
    {
        var x = new DVector(m[0],m[1],m[2]);
        var y = new DVector(m[4],m[5],m[6]);
        var z = new DVector(m[8],m[9],m[10]);
        static bool Orthogonal(DVector a, DVector b) => Math.Abs(a.Dot(b)) <= 1e-4 * a.Length * b.Length;
        var n = DVector.From(normal);
        var direction = DVector.From(ray.Direction);
        if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1 ||
            x.Cross(y).Dot(z) <= 0.9999*x.Length*y.Length*z.Length ||
            !Orthogonal(x,y) || !Orthogonal(y,z) || !Orthogonal(z,x) ||
            Math.Abs(n.Dot(n)-1) > 1e-4 || n.Dot(direction) >= -1e-6) return false;
        var point = DVector.From(ray.Origin) + direction*distance;
        var world = corners.Select(DVector.From).ToArray();
        var edges = Enumerable.Range(0,4).Select(i => world[(i+1)%4]-world[i]).ToArray();
        double epsilon = Math.Max(1e-5,1e-5*edges.Max(edge => edge.Length));
        if (Math.Abs((point-world[0]).Dot(n)) > epsilon) return false;
        for (int i = 0; i < 4; ++i)
        {
            var a = edges[i];
            var b = edges[(i+1)%4];
            if (a.Length < Math.Max(1e-3,100*epsilon) ||
                Math.Abs(a.Dot(b)) > 0.0017453283658983088*a.Length*b.Length ||
                a.Cross(b).Dot(n) < 0.99999*a.Length*b.Length ||
                Math.Abs((world[i]-world[0]).Dot(n)) > epsilon ||
                (a+edges[(i+2)%4]).Length > epsilon ||
                a.Cross(point-world[i]).Dot(n) <= epsilon*a.Length) return false;
        }
        return true;
    }
}
