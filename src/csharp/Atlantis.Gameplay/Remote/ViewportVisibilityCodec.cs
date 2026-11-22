using System;
using System.Collections.Generic;

namespace Atlantis.Gameplay.Remote;

public static partial class Codec
{
    public static JsonValue Encode(ViewportStamp stamp) => JsonValue.NewObject()
        .Set("sceneGeneration", JsonValue.Number(stamp.SceneGeneration)).Set("revision", JsonValue.Number(stamp.Revision));

    public static ViewportStamp? DecodeViewportStamp(JsonValue value) =>
        value.Find("sceneGeneration") is { } generation && generation.TryGetUInt64(out ulong g) &&
        value.Find("revision") is { } revision && revision.TryGetUInt64(out ulong r) ? new(g, r) : null;

    public static bool IsMaterialBindingRequest(MaterialBindingPickRequest request) =>
        IsAlignmentRay(request.Ray) && float.IsFinite(request.MinDistance) && float.IsFinite(request.MaxDistance) &&
        request.MinDistance >= 0 && request.MaxDistance > request.MinDistance &&
        (request.Viewport is null || request.Viewport.Value.SceneGeneration == request.SceneGeneration);

    public static JsonValue Encode(MaterialBindingPickRequest request)
    {
        var value = JsonValue.NewObject().Set("ray", Encode(request.Ray))
            .Set("minDistance", JsonValue.Number(request.MinDistance)).Set("maxDistance", JsonValue.Number(request.MaxDistance))
            .Set("sceneGeneration", JsonValue.Number(request.SceneGeneration));
        if (request.Viewport is { } stamp) value.Set("viewport", Encode(stamp));
        return value;
    }

    public static MaterialBindingPickRequest? DecodeMaterialBindingRequest(JsonValue value)
    {
        var viewport = value.Find("viewport");
        if (!Exact(value, viewport is null ? 4 : 5) ||
            value.Find("ray") is not { } rayValue || DecodeAlignmentRay(rayValue) is not Ray ray ||
            value.Find("minDistance") is not { IsNumber: true } min || DecodeFloat(min) is not float near ||
            value.Find("maxDistance") is not { IsNumber: true } max || DecodeFloat(max) is not float far ||
            value.Find("sceneGeneration") is not { IsNumber: true } gen || !gen.TryGetUInt64(out ulong generation))
            return null;
        var stamp = MaterialBindingStamp(viewport);
        var request = new MaterialBindingPickRequest(ray, near, far, generation, stamp);
        return (viewport is null || stamp is not null) && IsMaterialBindingRequest(request) ? request : null;
    }

    private static ViewportStamp? MaterialBindingStamp(JsonValue? value) =>
        value is not null && Exact(value, 2) && value.Find("sceneGeneration") is { IsNumber: true } &&
        value.Find("revision") is { IsNumber: true } ? DecodeViewportStamp(value) : null;

    public static MaterialBindingPickResult? DecodeMaterialBindingResponse(JsonValue response, MaterialBindingPickRequest request)
    {
        if (!Exact(response, 2) || CountMember(response, "id") != 1) return null;
        var result = response.Find("result");
        if (result is null && response.Find("error") is { IsObject: true } error && Exact(error, 2) &&
            error.Find("code") is { IsString: true } code && error.Find("message") is { IsString: true } message)
            return new(null, code.AsString == "BadRequest" && message.AsString == "unknown method control.pickMaterialBinding"
                ? MaterialBindingPickError.Unsupported : MaterialBindingPickError.RpcFailure);
        if (result is null || !Exact(result, 1)) return null;
        if (result.Find("err") is { IsString: true } refusal &&
            Enum.TryParse(refusal.AsString, out MaterialBindingPickError kind) &&
            kind <= MaterialBindingPickError.TooLarge && kind.ToString() == refusal.AsString)
            return new(null, kind);
        if (result.Find("ok") is not { IsObject: true } ok ||
            !Exact(ok, request.Viewport is null ? 6 : 7) ||
            ok.Find("sceneGeneration") is not { IsNumber: true } generation || !generation.TryGetUInt64(out ulong gen) ||
            gen != request.SceneGeneration || MaterialBindingStamp(ok.Find("viewport")) != request.Viewport ||
            (ok.Find("viewport") is null) != (request.Viewport is null) ||
            ok.Find("slot") is not { IsString: true } slot || slot.AsString != MaterialBindingPick.Slot ||
            ok.Find("entity") is not { IsString: true } entityToken || DecodeEntity(entityToken) is not EntityGuid entity ||
            entity.IsNil || entity.ToString() != entityToken.AsString ||
            ok.Find("mesh") is not { IsString: true } meshToken || DecodeAsset(meshToken) is not AssetGuid mesh ||
            mesh.IsNil || mesh.ToString() != meshToken.AsString ||
            ok.Find("material") is not { IsString: true } materialToken || DecodeAsset(materialToken) is not AssetGuid material ||
            material.IsNil || material.ToString() != materialToken.AsString ||
            ok.Find("distance") is not { IsNumber: true } distance || DecodeFloat(distance) is not float d ||
            !float.IsFinite(d) || d < request.MinDistance || d > request.MaxDistance)
            return null;
        return new(new(gen, request.Viewport, entity, mesh, material, d), null);
    }

    public static JsonValue Encode(ViewportPolicy policy)
    {
        static JsonValue Members(IReadOnlyList<EntityGuid> ids)
        {
            var list = JsonValue.NewArray();
            foreach (var id in ids) list.Push(Encode(id));
            return list;
        }
        return JsonValue.NewObject().Set("hidden", Members(policy.Hidden)).Set("locked", Members(policy.Locked))
            .Set("isolated", policy.Isolated is null ? JsonValue.Null() : Members(policy.Isolated));
    }

    public static ViewportPolicy? DecodeViewportPolicy(JsonValue value, out ViewportVisibilityError error)
    {
        error = ViewportVisibilityError.InvalidRequest;
        if (!Exact(value, 3) || value.Find("hidden") is not { IsArray: true } hidden ||
            value.Find("locked") is not { IsArray: true } locked ||
            value.Find("isolated") is not { } isolated || !(isolated.IsArray || isolated.IsNull)) return null;

        long count = (long)hidden.Items.Count + locked.Items.Count + (isolated.IsNull ? 0 : isolated.Items.Count);
        if (count > ViewportPolicy.MaxEntries)
        {
            error = ViewportVisibilityError.TooLarge;
            return null;
        }
        static IReadOnlyList<EntityGuid>? Members(JsonValue list)
        {
            var ids = new EntityGuid[list.Items.Count];
            for (int i = 0; i < ids.Length; ++i)
            {
                var item = list.Items[i];
                if (!item.IsString || DecodeEntity(item) is not EntityGuid id || id.IsNil ||
                    item.AsString != id.ToString()) return null;
                ids[i] = id;
            }
            return Array.AsReadOnly(ids);
        }
        var keep = isolated.IsNull ? null : Members(isolated);
        if (Members(hidden) is not { } hide || Members(locked) is not { } lockedIds ||
            (!isolated.IsNull && keep is null)) return null;
        var policy = new ViewportPolicy(hide, lockedIds, keep);
        if (policy.Validate() is not { } refusal) return policy;
        error = refusal;
        return null;
    }

    public static ViewportVisibility? DecodeViewportVisibility(JsonValue value)
    {
        if (!Exact(value, 5) || DecodeViewportStamp(value) is not ViewportStamp stamp ||
            value.Find("presented") is not { IsBool: true } presented ||
            value.Find("owner") is not { IsString: true } owner || !Enum.TryParse(owner.AsString, out ViewportOwner holder) ||
            !Enum.IsDefined(holder) || holder.ToString() != owner.AsString || value.Find("policy") is not { } policy)
            return null;
        if (holder != ViewportOwner.Caller) return policy.IsNull ? new(stamp, presented.AsBool, holder, null) : null;
        return DecodeViewportPolicy(policy, out _) is { } mask ? new(stamp, presented.AsBool, holder, mask) : null;
    }

    public static IReadOnlyList<Ray>? DecodeViewportRays(JsonValue value, ViewportPickKind kind)
    {
        if (!value.IsArray || value.Items.Count == 0 ||
            value.Items.Count > (kind == ViewportPickKind.Alignment ? 2 : 1)) return null;
        var rays = new Ray[value.Items.Count];
        for (int i = 0; i < rays.Length; ++i)
        {
            if (DecodeAlignmentRay(value.Items[i]) is not Ray ray) return null;
            rays[i] = ray;
        }
        return rays;
    }

    public static ViewportPick? DecodeViewportPick(JsonValue value, ViewportStamp expected,
        ViewportPickKind kind, IReadOnlyList<Ray> rays)
    {
        if (!Exact(value, 3) || DecodeViewportStamp(value) != expected) return null;
        if (kind == ViewportPickKind.Alignment)
        {
            if (value.Find("alignment") is not { } evidence) return null;
            var wrapper = JsonValue.NewObject().Set("id", JsonValue.Number(0ul)).Set("result", Ok(evidence));
            var alignment = DecodeAlignmentFacesResponse(wrapper, rays)?.Value;
            return alignment?.SceneGeneration == expected.SceneGeneration ? new(expected, null, alignment) : null;
        }
        if (value.Find("hit") is not { } hit) return null;
        if (hit.IsNull) return new(expected, null, null);
        if (!Exact(hit, 2) || hit.Find("entity") is not { IsString: true } id ||
            hit.Find("distance") is not { IsNumber: true } || DecodeRayHit(hit) is not RayHit found ||
            found.Entity.IsNil || id.AsString != found.Entity.ToString() || !float.IsFinite(found.Distance) ||
            found.Distance <= 0) return null;
        return new(expected, found, null);
    }

    public static ViewportReply<T>? DecodeViewportReply<T>(JsonValue envelope, string method,
        Func<JsonValue,T?> read, IReadOnlyList<Ray>? alignmentRays = null) where T : class
    {
        if (!Exact(envelope, 2) || CountMember(envelope, "id") != 1) return null;
        if (envelope.Find("error") is { } outer && Exact(outer, 2) &&
            outer.Find("code") is { IsString: true } code && outer.Find("message") is { IsString: true } message)
            return new(null, code.AsString == "BadRequest" && message.AsString == "unknown method " + method
                ? new(ViewportVisibilityError.Unsupported)
                : new(ViewportVisibilityError.RpcFailure, Detail: code.AsString + ": " + message.AsString));
        if (envelope.Find("result") is not { } result || !Exact(result, 1)) return null;
        if (result.Find("ok") is { } ok) return read(ok) is { } entry ? new(entry, null) : null;
        if (result.Find("err") is not { IsObject: true } err || err.Find("code") is not { IsString: true } kind ||
            !Enum.TryParse(kind.AsString, out ViewportVisibilityError reason) || !Enum.IsDefined(reason) ||
            reason.ToString() != kind.AsString || reason == ViewportVisibilityError.RpcFailure) return null;
        if (reason != ViewportVisibilityError.FaceRefused) return Exact(err, 1) ? new(null, new(reason)) : null;
        if (!Exact(err, 2) || err.Find("face") is not { } face || alignmentRays is null) return null;
        var wrapper = JsonValue.NewObject().Set("id", JsonValue.Number(0ul)).Set("result", Err(face));
        var detail = DecodeAlignmentFacesResponse(wrapper, alignmentRays)?.Error;
        return detail?.RayIndex is not null ? new(null, new(reason, detail)) : null;
    }
}
