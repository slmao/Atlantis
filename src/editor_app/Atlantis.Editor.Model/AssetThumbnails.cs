using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

public sealed record AssetThumbnailIntent(long CatalogEpoch, long DemandEpoch, AssetGuid Guid);

/// <summary>Thumbnail scheduling under the shared preview admission lock.</summary>
internal sealed class AssetThumbnailDemand
{
    private AssetGuid[] _visible = [];
    private AssetGuid[] _missing = [];
    private long _catalogEpoch, _demandEpoch, _stableAt;
    private long? _lastSend;
    private bool _blocked = true, _sent;
    public AssetThumbnailIntent? Active { get; private set; }

    public void Invalidate(long now, bool catalog)
    {
        if (catalog) ++_catalogEpoch;
        ++_demandEpoch;
        _stableAt = now + 200;
        _visible = _missing = [];
    }

    public void Update(IReadOnlyList<AssetGuid> visible, IReadOnlyList<AssetGuid> missing, bool blocked, long now)
    {
        if (_blocked != blocked || !_visible.SequenceEqual(visible))
        {
            ++_demandEpoch;
            _stableAt = now + 200;
            _visible = visible.ToArray();
            _blocked = blocked;
        }
        _missing = missing.ToArray();
    }

    public long? Delay(long now)
    {
        if (Active is not null || _blocked || _missing.Length == 0) return null;
        long notBefore = Math.Max(_stableAt, _lastSend is long last ? last + 500 : _stableAt);
        return Math.Max(0, notBefore - now);
    }

    public AssetThumbnailIntent? Take(long now)
    {
        if (Delay(now) != 0) return null;
        _sent = false;
        return Active = new(_catalogEpoch, _demandEpoch, _missing[0]);
    }

    public bool IsCurrent(AssetThumbnailIntent intent) =>
        ReferenceEquals(Active, intent) && intent.CatalogEpoch == _catalogEpoch &&
        intent.DemandEpoch == _demandEpoch && !_blocked && _missing.Contains(intent.Guid);

    public bool Admit(AssetThumbnailIntent intent, long now)
    {
        if (!IsCurrent(intent) || _sent) return false;
        _lastSend = now;
        _sent = true;
        return true;
    }

    public bool Settle(AssetThumbnailIntent intent)
    {
        if (!ReferenceEquals(Active, intent)) return false;
        bool current = IsCurrent(intent);
        Active = null;
        return current;
    }
}

/// <summary>UI-owned records. Callers detach current images before clearing and pin each displayed record.</summary>
public sealed class AssetThumbnailCache<T> where T : class, IDisposable
{
    public const int Limit = 128;
    public const int Side = 64;
    public const int PixelBytes = Side * Side * 4;
    public const int ByteLimit = 2 * 1024 * 1024;

    private sealed record Entry(T? Image, string? Failure, long? RetryAfter, long LastUse);
    private readonly Dictionary<AssetGuid, Entry> _entries = new();
    private HashSet<AssetGuid> _visible = new();
    private long _use;
    private int _images;
    public int Count => _entries.Count;
    public int RetainedPixelBytes => _images * PixelBytes;
    public string? Suspended { get; private set; }

    public void SetVisible(IReadOnlyList<AssetGuid> visible, long now)
    {
        var current = visible.ToHashSet();
        foreach (var guid in current)
            if (!_visible.Contains(guid) && _entries.TryGetValue(guid, out var entry) &&
                entry.RetryAfter is long due && now >= due)
                _entries.Remove(guid);
        _visible = current;
    }

    public bool TryGet(AssetGuid guid, out T? image, out string? failure)
    {
        image = null;
        failure = Suspended;
        if (!_entries.TryGetValue(guid, out var entry)) return false;
        _entries[guid] = entry with { LastUse = ++_use };
        image = entry.Image;
        failure = entry.Failure;
        return true;
    }

    public bool CanInsert => _entries.Count < Limit || _entries.Keys.Any(id => !_visible.Contains(id));

    public bool Store(AssetGuid guid, T? image, string? failure, long? retryAfter)
    {
        if (_entries.ContainsKey(guid)) return false;
        if (_entries.Count == Limit || (image is not null && RetainedPixelBytes + PixelBytes > ByteLimit))
        {
            var oldest = _entries.Where(pair => !_visible.Contains(pair.Key))
                .OrderBy(pair => pair.Value.LastUse).FirstOrDefault();
            if (oldest.Value is null) return false;
            _entries.Remove(oldest.Key);
            if (oldest.Value.Image is { } evicted)
            {
                --_images;
                evicted.Dispose();
            }
        }
        _entries.Add(guid, new(image, failure, retryAfter, ++_use));
        if (image is not null) ++_images;
        return true;
    }

    public void Suspend(string reason) => Suspended = reason;

    public void Clear()
    {
        foreach (var entry in _entries.Values) entry.Image?.Dispose();
        _entries.Clear();
        _visible.Clear();
        _images = 0;
        Suspended = null;
    }
}
