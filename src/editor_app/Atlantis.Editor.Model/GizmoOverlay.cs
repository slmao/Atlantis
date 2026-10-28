// Plan 0066 M5 (Spec 0066 R7, P5): the overlay the editor sends for a pick --
// the picked entity's outline (its selection) and a translate Gizmo at its
// WorldMatrix's translation, drawn by the Runtime after the scene. The Gizmo
// is one screen-constant group (Spec 0066 Q2, S-a) anchored at the
// translation: three axis segments along the world's axes, each with a cone
// tip of triangles, all on top. One unit of the group is HandlePixels pixels
// at the anchor's depth, so an axis parallel to the image plane is as long
// on screen as E1's ImGui handle (gizmo.h's kGizmoHandlePixels since Plan
// 0068 M1; editor_view.cpp: the 3-pixel line, the 10-pixel tip 5 pixels wide each side,
// kAxisColors). Draws only: no hit-testing, no editing (Spec 0066 Q4, Q5).
// UI-free.
using System;
using System.Collections.Generic;
using System.Numerics;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

public static class GizmoOverlay
{
    // E1's convention (editor_view.cpp), in pixels.
    public const float HandlePixels = 80.0f;
    public const float LineWidthPixels = 3.0f;
    public const float TipLengthPixels = 10.0f;
    public const float TipRadiusPixels = 5.0f;
    // The cone's sides; each side is a mantle triangle and a base triangle.
    public const int TipSides = 8;

    // kAxisColors: IM_COL32(230, 60, 60), (70, 200, 70), (70, 120, 240), opaque.
    public static readonly OverlayColor[] AxisColors =
    {
        new(230 / 255.0f, 60 / 255.0f, 60 / 255.0f),
        new(70 / 255.0f, 200 / 255.0f, 70 / 255.0f),
        new(70 / 255.0f, 120 / 255.0f, 240 / 255.0f),
    };

    private static readonly Vector3[] Axes = { Vector3.UnitX, Vector3.UnitY, Vector3.UnitZ };

    /// <summary>The picked entity outlined, with a translate Gizmo at <paramref name="position"/>.</summary>
    public static ViewportOverlay ForPick(EntityGuid entity, Vector3 position) =>
        new(new[] { entity }, new[] { TranslateGizmo(position) });

    /// <summary>E1's active-axis colour (kActiveAxisColor: IM_COL32(250, 220, 60)).</summary>
    public static readonly OverlayColor ActiveColor = new(250 / 255.0f, 220 / 255.0f, 60 / 255.0f);

    /// <summary>
    /// Plan 0068 M4 (Spec 0068 R5): a Gizmo for <paramref name="mode"/> along <paramref name="directions"/> (the axes' world
    /// directions), screen-constant at <paramref name="position"/>: Translate, a segment and a cone tip per axis; Scale, a
    /// segment and a square marker at its end (E1's 5-pixel half square); Rotate, a thin segment and a marker where E1's
    /// 9-pixel ring is hit-tested. The active axis, while dragged, is E1's yellow.
    /// </summary>
    public static OverlayGroup ModeGizmo(Vector3 position, GizmoMode mode, Vector3[] directions, int? activeAxis)
    {
        var segments = new List<OverlaySegment>(3);
        var triangles = new List<OverlayTriangle>();
        var points = new List<OverlayPoint>();
        for (int axis = 0; axis < 3; ++axis)
        {
            Vector3 along = directions[axis];
            OverlayColor color = activeAxis == axis ? ActiveColor : AxisColors[axis];
            segments.Add(new OverlaySegment(Vector3.Zero, along, color, OverlayDepth.OnTop, mode == GizmoMode.Rotate ? 1.0f : LineWidthPixels));
            switch (mode)
            {
                case GizmoMode.Translate:
                {
                    Vector3 u = Vector3.Cross(along, MathF.Abs(along.Y) < 0.9f ? Vector3.UnitY : Vector3.UnitX);
                    u = Vector3.Normalize(u);
                    Vector3 v = Vector3.Cross(along, u);
                    Vector3 tip = along * (1.0f + TipLengthPixels / HandlePixels);
                    float radius = TipRadiusPixels / HandlePixels;
                    for (int side = 0; side < TipSides; ++side)
                    {
                        Vector3 rim0 = Rim(along, u, v, radius, side);
                        Vector3 rim1 = Rim(along, u, v, radius, side + 1);
                        triangles.Add(new OverlayTriangle(tip, rim0, rim1, color, OverlayDepth.OnTop));
                        triangles.Add(new OverlayTriangle(along, rim1, rim0, color, OverlayDepth.OnTop));
                    }
                    break;
                }
                case GizmoMode.Scale:
                    points.Add(new OverlayPoint(along, color, OverlayDepth.OnTop, 10.0f));
                    break;
                default:
                    points.Add(new OverlayPoint(along, color, OverlayDepth.OnTop, 14.0f));
                    break;
            }
        }
        return new OverlayGroup(new OverlayAnchor(position, HandlePixels), segments, triangles, points);
    }

    /// <summary>The editor's overlay with a Gizmo group for the mode (see <see cref="ModeGizmo"/>) instead of the world-axis translate.</summary>
    public static ViewportOverlay ForSelection(EntityGuid? node, OverlayGroup? gizmo, IReadOnlyList<Marker> markers)
    {
        var groups = new List<OverlayGroup>(2);
        if (gizmo is not null) groups.Add(gizmo);
        if (markers.Count > 0)
        {
            var points = new List<OverlayPoint>(markers.Count);
            foreach (Marker marker in markers) points.Add(new OverlayPoint(marker.Position, MarkerColor, OverlayDepth.OnTop, MarkerPixels));
            groups.Add(OverlayGroup.World(points: points));
        }
        return new ViewportOverlay(node is EntityGuid n ? new[] { n } : Array.Empty<EntityGuid>(), groups);
    }

    /// <summary>E1's marker colour (kMarkerColor: IM_COL32(235, 235, 235, 230)) and a size the pick radius covers.</summary>
    public static readonly OverlayColor MarkerColor = new(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 230 / 255.0f);
    public const float MarkerPixels = 9.0f;

    /// <summary>
    /// Plan 0068 M2 (Spec 0068 R6, Q6): the editor's overlay -- the selected node outlined, a translate Gizmo at its world
    /// position when it has one, and a point at every light and camera marker (drawn on top, as E1 draws them).
    /// </summary>
    public static ViewportOverlay ForSelection(EntityGuid? node, Vector3? gizmoAt, IReadOnlyList<Marker> markers)
    {
        var groups = new List<OverlayGroup>(2);
        if (gizmoAt is Vector3 position) groups.Add(TranslateGizmo(position));
        if (markers.Count > 0)
        {
            var points = new List<OverlayPoint>(markers.Count);
            foreach (Marker marker in markers) points.Add(new OverlayPoint(marker.Position, MarkerColor, OverlayDepth.OnTop, MarkerPixels));
            groups.Add(OverlayGroup.World(points: points));
        }
        return new ViewportOverlay(node is EntityGuid n ? new[] { n } : Array.Empty<EntityGuid>(), groups);
    }

    /// <summary>
    /// The translate Gizmo anchored at <paramref name="position"/>: per axis (X, Y, Z) one segment from the anchor to
    /// one unit along it, then its cone's 2 * TipSides triangles; one unit is HandlePixels pixels.
    /// </summary>
    public static OverlayGroup TranslateGizmo(Vector3 position)
    {
        const float tipLength = TipLengthPixels / HandlePixels;
        const float tipRadius = TipRadiusPixels / HandlePixels;
        var segments = new List<OverlaySegment>(3);
        var triangles = new List<OverlayTriangle>(3 * 2 * TipSides);
        for (int axis = 0; axis < 3; ++axis)
        {
            Vector3 along = Axes[axis];
            Vector3 u = Axes[(axis + 1) % 3];
            Vector3 v = Axes[(axis + 2) % 3];
            OverlayColor color = AxisColors[axis];
            segments.Add(new OverlaySegment(Vector3.Zero, along, color, OverlayDepth.OnTop, LineWidthPixels));
            Vector3 tip = along * (1.0f + tipLength);
            for (int side = 0; side < TipSides; ++side)
            {
                Vector3 rim0 = Rim(along, u, v, tipRadius, side);
                Vector3 rim1 = Rim(along, u, v, tipRadius, side + 1);
                triangles.Add(new OverlayTriangle(tip, rim0, rim1, color, OverlayDepth.OnTop));
                triangles.Add(new OverlayTriangle(along, rim1, rim0, color, OverlayDepth.OnTop));
            }
        }
        return new OverlayGroup(new OverlayAnchor(position, HandlePixels), segments, triangles, Array.Empty<OverlayPoint>());
    }

    private static Vector3 Rim(Vector3 along, Vector3 u, Vector3 v, float radius, int side)
    {
        float angle = 2.0f * MathF.PI * (side % TipSides) / TipSides;
        return along + (u * MathF.Cos(angle) + v * MathF.Sin(angle)) * radius;
    }
}
