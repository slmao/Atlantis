using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

public sealed record SurfaceAlignmentSolution(Vector3 Position, IReadOnlyList<Vector3> PreviewCorners, double Tolerance,
    IReadOnlyList<double> CornerErrors, double Gap);

public static class SurfaceAlignment
{
    private readonly record struct Point(double X, double Y, double Z)
    {
        public static Point From(Vector3 v) => new(v.X, v.Y, v.Z);
        public Vector3 Narrow() => new((float)X, (float)Y, (float)Z);
        public static Point operator +(Point a, Point b) => new(a.X+b.X,a.Y+b.Y,a.Z+b.Z);
        public static Point operator -(Point a, Point b) => new(a.X-b.X,a.Y-b.Y,a.Z-b.Z);
        public static Point operator *(Point a, double s) => new(a.X*s,a.Y*s,a.Z*s);
        public Point Cross(Point b) => new(Y*b.Z-Z*b.Y,Z*b.X-X*b.Z,X*b.Y-Y*b.X);
        public double Dot(Point b) => X*b.X+Y*b.Y+Z*b.Z;
        public double Length => Math.Sqrt(Dot(this));
    }

    private static Point Translation(IReadOnlyList<float> m) => new(m[12],m[13],m[14]);
    private static Point Column(IReadOnlyList<float> m, int i) => new(m[4*i],m[4*i+1],m[4*i+2]);
    private static Point Transform(IReadOnlyList<float> m, Point p) =>
        Translation(m)+Column(m,0)*p.X+Column(m,1)*p.Y+Column(m,2)*p.Z;
    private static Point? InverseDirection(IReadOnlyList<float> m, Point p)
    {
        var a = Column(m,0); var b = Column(m,1); var c = Column(m,2);
        var yz = b.Cross(c);
        double d = a.Dot(yz);
        if (!double.IsFinite(d) || Math.Abs(d) <= 1e-8*a.Length*b.Length*c.Length) return null;
        return new Point(p.Dot(yz)/d, p.Dot(c.Cross(a))/d, p.Dot(a.Cross(b))/d);
    }

    private static double Longest(IReadOnlyList<Vector3> corners) =>
        Enumerable.Range(0,4).Max(i => (Point.From(corners[(i+1)%4])-Point.From(corners[i])).Length);
    private static Point Center(IEnumerable<Point> corners) => corners.Aggregate(new Point(), (a,b) => a+b)*0.25;

    private static Point[]? LocalCorners(AlignmentFace face)
    {
        var points = new Point[4];
        var origin = Translation(face.WorldMatrix);
        for (int i = 0; i < 4; ++i)
        {
            if (InverseDirection(face.WorldMatrix, Point.From(face.Corners[i])-origin) is not Point point)
                return null;
            points[i] = point;
        }
        return points;
    }

    public static bool MatchesAuthored(AlignmentFace face, IReadOnlyList<float> authoredWorld)
    {
        if (authoredWorld.Count != 16 || !authoredWorld.All(float.IsFinite) || LocalCorners(face) is not { } local)
            return false;
        for (int i = 0; i < 3; ++i)
        {
            var live = Column(face.WorldMatrix,i);
            if (!((Column(authoredWorld,i)-live).Length <= 1e-4*live.Length)) return false;
        }
        double epsilon = Math.Max(1e-5,1e-5*Longest(face.Corners));
        for (int i = 0; i < 4; ++i)
            if (!((Transform(authoredWorld,local[i])-Point.From(face.Corners[i])).Length <= epsilon)) return false;
        return true;
    }

    public static SurfaceAlignmentSolution? Solve(AlignmentFace source, AlignmentFace target,
        LocalTransform local, float[] parentWorld, double gap)
    {
        var normal = Point.From(target.Normal);
        var sourceNormal = Point.From(source.Normal);
        if (!double.IsFinite(gap) || gap < 0 || source.Entity == target.Entity ||
            !(normal.Dot(sourceNormal)/(normal.Length*sourceNormal.Length) <= -0.9999984769132877) ||
            LocalCorners(source) is not { } localCorners) return null;
        normal *= 1/normal.Length;

        double epsilon = Math.Max(1e-5,1e-5*Math.Max(Longest(source.Corners),Longest(target.Corners)));
        Point[] fixedCorners = target.Corners.Select(Point.From).ToArray();
        Point[] current = source.Corners.Select(Point.From).ToArray();
        Point offset = normal*gap;
        Point delta = Center(fixedCorners)+offset-Center(current);
        if (InverseDirection(parentWorld,delta) is not Point localDelta) return null;
        var position = (Point.From(local.Position)+localDelta).Narrow();
        if (!ViewportMeshPlacement.IsFinite(position)) return null;

        var matrix = WorldMatrices.Multiply(parentWorld, WorldMatrices.ComposeLocal(local with { Position = position }));
        Point[] moved = localCorners.Select(point => Transform(matrix,point)).ToArray();
        for (int start = 0; start < 4; ++start)
        {
            var residuals = new double[4];
            for (int i = 0; i < 4; ++i)
                residuals[i] = (moved[i]-fixedCorners[(start-i+4)%4]-offset).Length;
            if (!residuals.All(residual => residual <= epsilon)) continue;
            var preview = moved.Select(p => p.Narrow()).ToArray();
            if (!preview.All(ViewportMeshPlacement.IsFinite)) return null;
            double measured = (Center(moved)-Center(fixedCorners)).Dot(normal);
            if (!(Math.Abs(measured-gap) <= epsilon)) return null;
            return new(position,preview,epsilon,residuals,measured);
        }
        return null;
    }

    public static OverlayGroup Preview(AlignmentFace target, SurfaceAlignmentSolution solution)
    {
        var edges = new List<OverlaySegment>(9);
        AddRing(target.Corners,new OverlayColor(0.40f,0.78f,1.0f));
        AddRing(solution.PreviewCorners,new OverlayColor(1.0f,0.78f,0.45f));
        if (solution.Gap > solution.Tolerance)
        {
            var start = Center(target.Corners.Select(Point.From)).Narrow();
            var end = Center(solution.PreviewCorners.Select(Point.From)).Narrow();
            if (start != end) edges.Add(new(start,end,new OverlayColor(1,1,1),OverlayDepth.OnTop,2));
        }
        return new(null,edges,[],[]);

        void AddRing(IReadOnlyList<Vector3> corners, OverlayColor color)
        {
            for (int i = 0; i < 4; ++i)
                edges.Add(new(corners[i],corners[(i+1)%4],color,OverlayDepth.OnTop,2));
        }
    }
}
