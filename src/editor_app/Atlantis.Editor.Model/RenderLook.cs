using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Numerics;
using System.Text;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public sealed record RenderLookPreset(string Name, RenderLookBasis Basis, IReadOnlyList<SetProperty> Values)
{
    public bool Matches(RenderLookBasis current) => Basis.ActiveCamera == current.ActiveCamera &&
        Basis.LightRoster.SequenceEqual(current.LightRoster) && Basis.PlacementSignature == current.PlacementSignature;
}

public sealed record RenderLookSnapshot(RenderLookBasis Basis, IReadOnlyList<BatchPropertyTarget> Targets)
{
    public static bool Finite(PropertyValue value) => value.TryGetFloat32(out float f) ? float.IsFinite(f) :
        value.TryGetVec3(out Vector3 v) && float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);

    public RenderLookPreset Preset(string name) =>
        new(name, Basis, Targets.Select(t => new SetProperty(t.Address, t.Value!.Value)).ToArray());

    public static RenderLookSnapshot? Read(RemoteAuthoringDocument document, DocumentTree tree)
    {
        if (!tree.OrderSynchronized || !document.ObserveActiveCamera(out var current) ||
            current is not EntityGuid camera || !tree.Contains(camera) ||
            SceneMarkers.ReadLightKinds(document, tree) is not { } kinds ||
            kinds.Count > RequireRenderLookDocument.MaxLights || kinds.Values.Any(v => v is null)) return null;
        var lights = kinds.Keys.OrderBy(id => id.ToString(), StringComparer.Ordinal).ToArray();
        var directionalNodes = lights.Where(id => kinds[id] == HierarchyLightKind.Directional).ToArray();
        if (directionalNodes.Length > 1 || directionalNodes.Any(id => tree.ChildrenOf(id).Count != 0)) return null;
        var ancestors = new HashSet<EntityGuid>();
        foreach (var start in lights.Prepend(camera))
        {
            for (EntityGuid? n = start; n is EntityGuid id && ancestors.Add(id); n = tree.ParentOf(id))
                if (!tree.Contains(id)) return null;
        }
        var placementNodes = ancestors.OrderBy(id => id.ToString(), StringComparer.Ordinal).ToArray();
        var addresses = new List<SceneAddress>();
        bool Add(EntityGuid node, string component, string owner, string field)
        {
            var type = SceneSchema.TypeOf(document.Schema, component);
            var leaf = document.Schema.FirstOrDefault(t => t.ShortName == owner)?.Fields.FirstOrDefault(f => f.Name == field);
            if (type is not TypeId found || leaf is null) return false;
            addresses.Add(new SceneAddress(node, found, leaf.Id));
            return true;
        }
        foreach (var node in placementNodes)
        {
            if (!Add(node, "Transform", "Transform", "localPosition") || !Add(node, "Transform", "Transform", "localScale"))
                return null;
            if (!directionalNodes.Contains(node) && !Add(node, "Transform", "Transform", "localEulerAnglesRadians")) return null;
            if (node == camera && (!Add(node, "Camera", "Camera", "fovYRadians") ||
                !Add(node, "Camera", "Camera", "nearZ") || !Add(node, "Camera", "Camera", "farZ"))) return null;
        }
        int firstLook = addresses.Count;
        foreach (var pair in new (string Owner, string Field)[]
        {
            ("Camera", "exposureCompensationEv"), ("CameraFog", "color"), ("CameraFog", "density"), ("CameraFog", "height"),
            ("CameraFog", "heightFalloff"), ("CameraFog", "maxOpacity"), ("CameraBloom", "strength"), ("CameraBloom", "threshold")
        })
            if (!Add(camera, "Camera", pair.Owner, pair.Field)) return null;
        foreach (var light in lights)
        {
            if (!Add(light, "Light", "Light", "color") || !Add(light, "Light", "Light", "intensity")) return null;
            if (directionalNodes.Contains(light))
            {
                if (!Add(light, "Transform", "Transform", "localEulerAnglesRadians")) return null;
            }
            else if (!Add(light, "Light", "Light", "range")) return null;
        }
        var values = document.GetProperties(addresses);
        if (values.Count != addresses.Count || values.Any(v => v is not PropertyValue p || !Finite(p))) return null;
        var signature = new StringBuilder();
        int next = 0;
        foreach (var node in placementNodes)
        {
            bool direction = directionalNodes.Contains(node);
            char role = node == camera ? 'C' : direction ? 'D' : kinds.ContainsKey(node) ? 'P' : 'T';
            signature.Append(node).Append('/').Append(tree.ParentOf(node)).Append('/').Append(role);
            int count = (direction ? 2 : 3) + (node == camera ? 3 : 0);
            for (int i = 0; i < count; ++i)
            {
                var value = values[next++]!.Value;
                if (value.TryGetVec3(out var v))
                {
                    Bits(v.X);
                    Bits(v.Y);
                    Bits(v.Z);
                }
                else Bits(value.AsFloat32());
            }
            signature.Append('|');
            if (signature.Length > RequireRenderLookDocument.MaxPlacementBytes) return null;
        }
        var targets = addresses.Skip(firstLook).Select((address, offset) => new BatchPropertyTarget(address,
            values[firstLook + offset], tree.IncarnationOf(address.Node),
            tree.ComponentIncarnationOf(address), tree.PropertyStamp(address))).ToArray();
        return new(new RenderLookBasis(camera, lights, signature.ToString()), targets);

        void Bits(float value) => signature.Append('/').Append(
            BitConverter.SingleToUInt32Bits(value).ToString(CultureInfo.InvariantCulture));
    }
}
