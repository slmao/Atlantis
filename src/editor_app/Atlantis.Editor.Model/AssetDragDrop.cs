using System;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;

namespace Atlantis.Editor.Model;

/// <summary>One window-owned drag. Cancellation and entering the actual send
/// are serialized; native drag completion is not cancellation of a queued drop.</summary>
public sealed class AssetDragIntent(AssetSummary asset, object catalog, long lifetime)
{
    private enum Phase { Hover, Queued, Sent, Cancelled }
    private Phase _phase;
    private readonly object _gate = new();

    public string Token { get; } = Guid.NewGuid().ToString("N");
    public AssetSummary Asset { get; } = asset;
    public object Catalog { get; } = catalog;
    public long Lifetime { get; } = lifetime;

    public bool Matches(string? token)
    {
        lock (_gate) return _phase == Phase.Hover && token == Token;
    }

    public bool Queue()
    {
        lock (_gate)
        {
            if (_phase != Phase.Hover) return false;
            _phase = Phase.Queued;
            return true;
        }
    }

    public void Cancel(bool onlyUndropped = false)
    {
        lock (_gate)
            if (_phase == Phase.Hover || !onlyUndropped && _phase == Phase.Queued)
                _phase = Phase.Cancelled;
    }

    public ulong? Send(Func<ulong> send)
    {
        lock (_gate)
        {
            if (_phase != Phase.Queued) return null;
            _phase = Phase.Sent;
            return send();
        }
    }
}

public sealed record AssetDropField(AssetReferenceTarget Field, BatchPropertyTarget Identity,
    ulong Selection, long Lifetime)
{
    public string? CompatibilityNotice => Field.TargetAssetType is null
        ? "Target type unspecified. Catalog presence does not guarantee compatibility." : null;

    public bool Accepts(AssetType type) => Enum.IsDefined(type) &&
        (Field.TargetAssetType is null || Field.TargetAssetType == type);

    public bool IsCurrent(DocumentTree tree, ulong selection, long lifetime, bool checkStamp = true) =>
        Selection == selection && Lifetime == lifetime && Identity.IsCurrent(tree, checkStamp);
}
