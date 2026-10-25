// Spec 0066 / ADR-0132 (Plan 0066 P2): the wire encoding of the Viewport
// overlay (docs/architecture/remote_protocol.md section 6.6), mirroring the C++
// form (connection::overlayToJson / overlayFromJson): encoders build the same
// JSON, member for member, and decoders accept what it accepts and return null
// for anything malformed. Floats are JSON numbers only: an overlay's floats are
// finite (a non-finite one throws ArgumentException when encoded). Pure
// functions.
using System.Collections.Generic;
using System.Numerics;

namespace Atlantis.Gameplay.Remote;

public static partial class Codec
{
    private static JsonValue Floats3(Vector3 v) =>
        JsonValue.NewArray().Push(JsonValue.Number(v.X)).Push(JsonValue.Number(v.Y)).Push(JsonValue.Number(v.Z));

    private static JsonValue Color(OverlayColor c) =>
        JsonValue.NewArray().Push(JsonValue.Number(c.R)).Push(JsonValue.Number(c.G)).Push(JsonValue.Number(c.B)).Push(JsonValue.Number(c.A));

    private static JsonValue Depth(OverlayDepth depth) => JsonValue.String(depth == OverlayDepth.OnTop ? "OnTop" : "Tested");

    public static JsonValue Encode(ViewportOverlay overlay)
    {
        JsonValue selection = JsonValue.NewArray();
        foreach (EntityGuid entity in overlay.Selection) selection.Push(Encode(entity));
        JsonValue groups = JsonValue.NewArray();
        foreach (OverlayGroup group in overlay.Groups)
        {
            JsonValue segments = JsonValue.NewArray();
            foreach (OverlaySegment s in group.Segments)
            {
                segments.Push(JsonValue.NewObject().Set("a", Floats3(s.A)).Set("b", Floats3(s.B)).Set("color", Color(s.Color))
                    .Set("depth", Depth(s.Depth)).Set("width", JsonValue.Number(s.WidthPixels)));
            }
            JsonValue triangles = JsonValue.NewArray();
            foreach (OverlayTriangle t in group.Triangles)
            {
                triangles.Push(JsonValue.NewObject().Set("a", Floats3(t.A)).Set("b", Floats3(t.B)).Set("c", Floats3(t.C))
                    .Set("color", Color(t.Color)).Set("depth", Depth(t.Depth)));
            }
            JsonValue points = JsonValue.NewArray();
            foreach (OverlayPoint p in group.Points)
            {
                points.Push(JsonValue.NewObject().Set("p", Floats3(p.P)).Set("color", Color(p.Color)).Set("depth", Depth(p.Depth))
                    .Set("size", JsonValue.Number(p.SizePixels)));
            }
            JsonValue anchor = group.Anchor is OverlayAnchor a
                ? JsonValue.NewObject().Set("position", Floats3(a.Position)).Set("pixelsPerUnit", JsonValue.Number(a.PixelsPerUnit))
                : JsonValue.Null();
            groups.Push(JsonValue.NewObject().Set("anchor", anchor).Set("segments", segments).Set("triangles", triangles)
                .Set("points", points));
        }
        return JsonValue.NewObject().Set("selection", selection).Set("groups", groups);
    }

    public static ViewportOverlay? DecodeViewportOverlay(JsonValue value)
    {
        if (!value.IsObject || value.Find("selection") is not { IsArray: true } selectionList ||
            value.Find("groups") is not { IsArray: true } groupList)
        {
            return null;
        }
        var selection = new List<EntityGuid>(selectionList.Items.Count);
        foreach (JsonValue item in selectionList.Items)
        {
            if (DecodeEntity(item) is not EntityGuid entity) return null;
            selection.Add(entity);
        }
        var groups = new List<OverlayGroup>(groupList.Items.Count);
        foreach (JsonValue g in groupList.Items)
        {
            if (!g.IsObject || g.Find("anchor") is not JsonValue anchorValue) return null;
            OverlayAnchor? anchor = null;
            if (!anchorValue.IsNull)
            {
                if (NumberVector(anchorValue.Find("position")) is not Vector3 position || Number(anchorValue.Find("pixelsPerUnit")) is not float scale)
                {
                    return null;
                }
                anchor = new OverlayAnchor(position, scale);
            }
            var segments = new List<OverlaySegment>();
            var triangles = new List<OverlayTriangle>();
            var points = new List<OverlayPoint>();
            if (g.Find("segments") is not { IsArray: true } segmentList || g.Find("triangles") is not { IsArray: true } triangleList ||
                g.Find("points") is not { IsArray: true } pointList)
            {
                return null;
            }
            foreach (JsonValue s in segmentList.Items)
            {
                if (NumberVector(s.Find("a")) is not Vector3 a || NumberVector(s.Find("b")) is not Vector3 b ||
                    ColorOf(s.Find("color")) is not OverlayColor color || DepthOf(s.Find("depth")) is not OverlayDepth depth ||
                    Number(s.Find("width")) is not float width)
                {
                    return null;
                }
                segments.Add(new OverlaySegment(a, b, color, depth, width));
            }
            foreach (JsonValue t in triangleList.Items)
            {
                if (NumberVector(t.Find("a")) is not Vector3 a || NumberVector(t.Find("b")) is not Vector3 b ||
                    NumberVector(t.Find("c")) is not Vector3 c || ColorOf(t.Find("color")) is not OverlayColor color ||
                    DepthOf(t.Find("depth")) is not OverlayDepth depth)
                {
                    return null;
                }
                triangles.Add(new OverlayTriangle(a, b, c, color, depth));
            }
            foreach (JsonValue p in pointList.Items)
            {
                if (NumberVector(p.Find("p")) is not Vector3 at || ColorOf(p.Find("color")) is not OverlayColor color ||
                    DepthOf(p.Find("depth")) is not OverlayDepth depth || Number(p.Find("size")) is not float size)
                {
                    return null;
                }
                points.Add(new OverlayPoint(at, color, depth, size));
            }
            groups.Add(new OverlayGroup(anchor, segments, triangles, points));
        }
        return new ViewportOverlay(selection, groups);
    }

    // A JSON number as a float (no "nan"/"inf" text: overlays are finite).
    private static float? Number(JsonValue? value) => value is not null && value.TryGetFloat(out float f) ? f : null;

    private static float[]? Numbers(JsonValue? value, int count)
    {
        if (value is null || !value.IsArray || value.Items.Count != count) return null;
        var output = new float[count];
        for (int i = 0; i < count; ++i)
        {
            if (Number(value.Items[i]) is not float x) return null;
            output[i] = x;
        }
        return output;
    }

    private static Vector3? NumberVector(JsonValue? value) => Numbers(value, 3) is float[] v ? new Vector3(v[0], v[1], v[2]) : null;

    private static OverlayColor? ColorOf(JsonValue? value) => Numbers(value, 4) is float[] c ? new OverlayColor(c[0], c[1], c[2], c[3]) : null;

    private static OverlayDepth? DepthOf(JsonValue? value) =>
        value is { IsString: true } ? value.AsString switch { "Tested" => OverlayDepth.Tested, "OnTop" => OverlayDepth.OnTop, _ => null } : null;
}
