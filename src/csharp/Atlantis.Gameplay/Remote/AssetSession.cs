using System;
using System.Collections.Generic;
using A = Atlantis.Gameplay.Assets;

namespace Atlantis.Gameplay.Remote;

/// <summary>
/// Spec 0081's two read-only catalog queries. Not thread-safe; the session's
/// thread. Owned copies; in-band refusals preserve the session, transport or
/// malformed typed results throw RemoteException. No connect-time request.
/// </summary>
public sealed class AssetSession
{
    private readonly RemoteSession _session;
    internal AssetSession(RemoteSession session) => _session = session;
    public Outcome<IReadOnlyList<A.AssetSummary>, A.AssetError> ListAssets()
    {
        JsonValue value = _session.Call("assets.list");
        CheckEnvelope(value, "assets.list");
        return _session.InBand<IReadOnlyList<A.AssetSummary>, A.AssetError>(value, "assets.list", v =>
        {
            if (!v.IsArray) return null;
            var list = new List<A.AssetSummary>();
            foreach (JsonValue item in v.Items)
            {
                A.AssetSummary? s = Codec.DecodeAssetSummary(item);
                if (s is null || list.Count > 0 && string.CompareOrdinal(list[^1].Guid.ToString(), s.Guid.ToString()) >= 0) return null;
                list.Add(s);
            }
            return list;
        }, Codec.DecodeAssetError);
    }
    public Outcome<A.AssetDetails, A.AssetError> GetAsset(AssetGuid guid)
    {
        if (guid == default) throw new ArgumentException("a non-nil asset GUID is required", nameof(guid));
        JsonValue value = _session.Call("assets.get", JsonValue.NewObject().Set("guid", Codec.Encode(guid)));
        CheckEnvelope(value, "assets.get");
        return _session.InBand<A.AssetDetails, A.AssetError>(value, "assets.get", v =>
            Codec.DecodeAssetDetails(v) is A.AssetDetails d && d.Guid == guid ? d : null, Codec.DecodeAssetError);
    }
    private void CheckEnvelope(JsonValue value, string method)
    {
        if ((value.Find("ok") is null) == (value.Find("err") is null)) throw _session.Malformed(method);
    }
}
