using System;
using System.Linq;
using System.Numerics;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void ViewportPlacementChecks()
    {
        var asset = new AssetSummary(AssetGuid.Parse("01280128-0128-4128-8128-012801280001"), AssetType.Mesh, "fixture");
        var surface = new EntityGuid(Guid.NewGuid());
        var pose = new CameraPose(
            [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], new CameraLens(1.2f, 0.1f, 20.0f));
        var camera = NavigationFraming.Projection(pose, 960, 540)!;
        var center = new PlacementRay(480, 270, 960, 540, 5, pose, CameraConvention.RayThroughNdc(camera, 0, 0));
        Check(ViewportMeshPlacement.HitPosition(center, new(surface, 3)) == new Vector3(0, 0, -3),
            "placement: root pivot is the actual world hit");
        Check(ViewportMeshPlacement.HitPosition(center, null) is null &&
            ViewportMeshPlacement.HitPosition(center, new(surface, float.NaN)) is null &&
            ViewportMeshPlacement.HitPosition(center, new(surface, 0)) is null &&
            ViewportMeshPlacement.HitPosition(center, new(surface, 0.05f)) is null &&
            ViewportMeshPlacement.HitPosition(center, new(surface, 21)) is null,
            "placement: miss, invalid distances and near/far clipping are refused");
        var edge = center with { X = 950, Ray = CameraConvention.RayThroughNdc(camera, 950f / 960 * 2 - 1, 0) };
        Check(ViewportMeshPlacement.HitPosition(edge, new(surface, 21)) is not null &&
            ViewportMeshPlacement.HitPosition(edge with { X = 960 }, new(surface, 3)) is null,
            "placement: lens clip is based on view depth and the pixel extent excludes its outer edge");
        var marker = ViewportMeshPlacement.Marker(new(0, 0, -3));
        Check(marker.Anchor is { Position: { Z: -3 } } && marker.Segments.Count == 2 && marker.Points.Count == 1,
            "placement: one screen-constant pivot cross and point");

        var drag = new AssetDragIntent(asset, new object(), 5);
        var gesture = new MeshPlacementGesture(drag);
        gesture.Move(center);
        gesture.Move(edge);
        Check(!gesture.IsHover(center) && !gesture.Queue(center) && gesture.IsHover(edge),
            "placement: late answer and release at an earlier pixel are refused");
        gesture.Leave();
        Check(!gesture.IsHover(edge) && !gesture.Queue(edge), "placement: leave retires the current hover");
        gesture.Move(center);
        Check(gesture.Queue(center) && !gesture.Queue(center) && !gesture.IsHover(center),
            "placement: a valid release consumes the shared asset token once");
        gesture.Leave();
        drag.Cancel(onlyUndropped: true);
        Check(gesture.IsReleased(center) && drag.Send(() => 7) == 7,
            "placement: native completion clears hover without canceling admitted release");
        gesture.InvalidateRelease();
        Check(!gesture.IsReleased(center), "placement: a later view invalidation retires the release");
        var expiredDrag = new AssetDragIntent(asset, new object(), 5);
        var expired = new MeshPlacementGesture(expiredDrag);
        expired.Move(edge);
        expired.Queue(edge);
        expired.InvalidateRelease();
        Check(expiredDrag.Send(() => throw new InvalidOperationException("expired write")) is null,
            "placement: invalidation before transport send cancels the shared queue");

        var document = new FakeDocument();
        var node = FakeDocument.Guid(87);
        TypeId renderable = new(870);
        FieldId mesh = new(871);
        var schema = document.Schema.Append(new SchemaType(renderable, "asset_system::scene::Renderable", TypeKind.Struct, 1,
            [new SchemaField(mesh, "meshAsset", TypeKind.Primitive, PrimitiveKind.AssetGuid, default, FieldFlags.Serializable)],
            [])).ToArray();
        var hitPoint = new Vector3(1, 2, 3);
        var plan = HierarchyCommands.AddMesh(schema, 5, node, asset.Guid, hitPoint);
        Check(plan.Commands is [CreateNode create, SetProperty reference, SetProperty translation] &&
            create.Node == node && create.Position == 5 && reference.Value.AsAsset() == asset.Guid &&
            translation.Address == new SceneAddress(node, FakeDocument.Transform, FakeDocument.Position) &&
            translation.Value.Equals(PropertyValue.FromVec3(hitPoint)),
            "placement: one structural transaction creates root, assigns captured mesh and writes exact hit translation");
        Check(HierarchyCommands.AddMesh(schema, 5, node, asset.Guid).Commands.Count == 2 &&
            HierarchyCommands.AddMesh(schema, 5, node, asset.Guid, new Vector3(float.PositiveInfinity)).Refusal is not null,
            "placement: original mesh-add shape is preserved and invalid translations are refused");
    }
}
