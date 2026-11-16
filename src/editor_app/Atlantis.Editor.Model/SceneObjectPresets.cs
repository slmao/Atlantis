using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public enum SceneObjectPreset { Empty, Camera, DirectionalLight, PointLight, Cube, Sphere, Plane, UnnamedRoot, EmptyChild }

public sealed record SceneObjectPlan(EditPlan Edit, NodeState Expected);

/// <summary>Spec 0110: Editor compositions of the ordinary authoring commands, with no wire preset type.</summary>
public static class SceneObjectPresets
{
    public static string Label(SceneObjectPreset preset) => preset switch
    {
        SceneObjectPreset.DirectionalLight => "Directional Light",
        SceneObjectPreset.PointLight => "Point Light",
        SceneObjectPreset.UnnamedRoot => "Empty root (unnamed)",
        SceneObjectPreset.EmptyChild => "Empty child",
        _ => preset.ToString()
    };

    public static bool Named(SceneObjectPreset preset) =>
        preset is not (SceneObjectPreset.UnnamedRoot or SceneObjectPreset.EmptyChild);

    public static bool Available(SceneObjectPreset preset, bool handAuthored, int selectionCount) => preset switch
    {
        SceneObjectPreset.UnnamedRoot => selectionCount == 0,
        SceneObjectPreset.EmptyChild => selectionCount == 1,
        _ => handAuthored
    };

    public static AssetGuid? Mesh(SceneObjectPreset preset) => preset switch
    {
        SceneObjectPreset.Cube => new AssetGuid(Guid.Parse("3b9b8177-eac1-4139-b203-a49d5577eb3b")),
        SceneObjectPreset.Sphere => new AssetGuid(Guid.Parse("da09198f-d590-44f3-9049-ba5b43fe76cd")),
        SceneObjectPreset.Plane => new AssetGuid(Guid.Parse("caa7dc9c-6c16-4f93-bdec-53ac93791e16")),
        _ => null
    };

    public static bool MatchesMesh(AssetGuid guid, AssetDetails? detail) => detail is { Type: AssetType.Mesh } &&
        detail.Guid == guid;

    public static SceneObjectPlan? Build(IReadOnlyList<SchemaType> schema, SceneObjectPreset preset,
        EntityGuid node, ulong position, EntityGuid? parent)
    {
        if (node.Value == Guid.Empty || (preset == SceneObjectPreset.EmptyChild) != parent.HasValue) return null;
        var wanted = new Dictionary<string, (PropertyValue Value, bool Write)>();
        void Value(string path, PropertyValue value, bool write = false) => wanted.Add(path, (value, write));
        void Float(string path, float value, bool write = false) => Value(path, PropertyValue.FromFloat32(value), write);
        void Vec(string path, Vector3 value, bool write = false) => Value(path, PropertyValue.FromVec3(value), write);
        bool camera = preset == SceneObjectPreset.Camera, point = preset == SceneObjectPreset.PointLight;
        bool directional = preset == SceneObjectPreset.DirectionalLight;
        Vec("Transform.localPosition", camera ? new(0, 0, 5) : point ? new(0, 2, 2) : Vector3.Zero, camera || point);
        Vec("Transform.localEulerAnglesRadians", directional ? new(-MathF.PI / 4, 0, 0) : Vector3.Zero, directional);
        Vec("Transform.localScale", Vector3.One);
        string? extra = null;
        if (camera)
        {
            extra = "Camera";
            Float("Camera.fovYRadians", MathF.PI / 3, true);
            Float("Camera.nearZ", .1f, true);
            Float("Camera.farZ", 100, true);
            Float("Camera.exposureCompensationEv", 0);
            Vec("Camera.fog.color", Vector3.One);
            Float("Camera.fog.density", 0);
            Float("Camera.fog.height", 0);
            Float("Camera.fog.heightFalloff", 0);
            Float("Camera.fog.maxOpacity", 1);
            Float("Camera.bloom.strength", 0);
            Float("Camera.bloom.threshold", 1);
        }
        else if (point || directional)
        {
            extra = "Light";
            TypeId? light = SceneSchema.TypeOf(schema, extra);
            SchemaField? kind = schema.FirstOrDefault(t => t.Id == light)?.Fields.FirstOrDefault(f => f.Name == "kind");
            if (kind is not { Kind: TypeKind.Enum }) return null;
            SchemaConstant? constant = schema.FirstOrDefault(t => t.Id == kind.Type && t.Kind == TypeKind.Enum)?
                .Constants.FirstOrDefault(c => c.Name == (point ? "Point" : "Directional"));
            if (constant is null) return null;
            Value("Light.kind", PropertyValue.FromEnum(constant.Value), true);
            Vec("Light.color", Vector3.One);
            Float("Light.intensity", 1);
            Float("Light.range", point ? 10 : 0, point);
        }
        else if (Mesh(preset) is AssetGuid mesh)
        {
            extra = "Renderable";
            Value("Renderable.meshAsset", PropertyValue.FromAsset(mesh), true);
            Value("Renderable.materialAsset", PropertyValue.Absent);
        }

        string name = Named(preset) ? Label(preset) : "";
        var states = new List<ComponentState>();
        var writes = new List<Command>();
        var extras = new List<TypeId>();
        foreach (string component in extra is null ? new[] { "Transform" } : new[] { "Transform", extra })
        {
            if (SceneSchema.TypeOf(schema, component) is not TypeId id) return null;
            if (component != "Transform") extras.Add(id);
            var fields = new List<(SceneAddress, PropertyValue)>();
            foreach (var leaf in SchemaText.LeavesOf(schema, id).Where(l => (l.Leaf.Flags & FieldFlags.Serializable) != 0))
            {
                if (!wanted.Remove(leaf.Path, out var entry) || !MatchesField(leaf.Leaf, entry.Value)) return null;
                var address = new SceneAddress(node, id, leaf.Leaf.Id);
                fields.Add((address, entry.Value));
                if (entry.Write) writes.Add(new SetProperty(address, entry.Value));
            }
            states.Add(new ComponentState(id, fields));
        }
        if (wanted.Count != 0) return null;
        var commands = new List<Command> { new CreateNode(node, position, extras) };
        if (name.Length != 0) commands.Add(new SetNodeName(node, name));
        commands.AddRange(writes);
        if (parent is EntityGuid target) commands.Add(new SetParent(node, target));
        return new(new EditPlan("Create " + Label(preset), commands),
            new NodeState(node, position, parent, [], states.OrderBy(c => c.Type.Value).ToArray(), name));
    }

    private static bool MatchesField(SchemaField field, PropertyValue value) => value.Kind switch
    {
        PropertyKind.Enum => field.Kind == TypeKind.Enum,
        PropertyKind.Float32 => field.Kind == TypeKind.Primitive && field.Primitive == PrimitiveKind.Float32,
        PropertyKind.Vec3 => field.Kind == TypeKind.Primitive && field.Primitive == PrimitiveKind.Vec3Float32,
        PropertyKind.AssetGuid => field.Kind == TypeKind.Primitive && field.Primitive == PrimitiveKind.AssetGuid &&
            field.TargetAssetType is null or AssetType.Mesh && !field.IsOptional,
        PropertyKind.Absent => field.Kind == TypeKind.Primitive && field.Primitive == PrimitiveKind.AssetGuid && field.IsOptional,
        _ => false
    };
}
