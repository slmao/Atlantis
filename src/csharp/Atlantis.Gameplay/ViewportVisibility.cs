using System;
using System.Collections.Generic;
using System.Linq;

namespace Atlantis.Gameplay;

public readonly record struct ViewportStamp(ulong SceneGeneration, ulong Revision);
public enum ViewportOwner { None, Caller, Other }
public enum ViewportPickKind { Selection, Alignment }
public enum ViewportVisibilityError
{
    Unsupported, InvalidRequest, Unavailable, TooLarge, NotOwner, StaleGeneration, StaleRevision,
    UnknownMember, NotPresented, NoGeometry, FaceRefused, RpcFailure
}

public sealed record ViewportPolicy(IReadOnlyList<EntityGuid> Hidden, IReadOnlyList<EntityGuid> Locked,
    IReadOnlyList<EntityGuid>? Isolated)
{
    public const int MaxEntries = 8192;

    public ViewportVisibilityError? Validate()
    {
        long count = (long)Hidden.Count + Locked.Count + (Isolated?.Count ?? 0);
        if (count > MaxEntries) return ViewportVisibilityError.TooLarge;
        return count == 0 || !Ordered(Hidden) || !Ordered(Locked) ||
            (Isolated is not null && (Isolated.Count == 0 || !Ordered(Isolated)))
            ? ViewportVisibilityError.InvalidRequest : null;
    }

    private static bool Ordered(IReadOnlyList<EntityGuid> members)
    {
        string? previous = null;
        foreach (var member in members)
        {
            string current = member.ToString();
            if (member.IsNil || (previous is not null && string.CompareOrdinal(previous, current) >= 0)) return false;
            previous = current;
        }
        return true;
    }

    public bool SameMembership(ViewportPolicy? other) => other is not null &&
        Hidden.SequenceEqual(other.Hidden) && Locked.SequenceEqual(other.Locked) &&
        (Isolated is null ? other.Isolated is null : other.Isolated is not null && Isolated.SequenceEqual(other.Isolated));
}

public sealed record ViewportVisibility(ViewportStamp Stamp, bool Presented, ViewportOwner Owner, ViewportPolicy? Policy)
{
    public bool Acknowledges(ViewportStamp previous, ViewportPolicy? request) =>
        previous.SceneGeneration == Stamp.SceneGeneration && previous.Revision <= Stamp.Revision &&
        (request is null
            ? Policy is null && Owner == ViewportOwner.None
            : Stamp.Revision > previous.Revision && Owner == ViewportOwner.Caller && request.SameMembership(Policy));
}

public sealed record ViewportPick(ViewportStamp Stamp, RayHit? Hit, AlignmentFaces? Alignment);
public sealed record ViewportVisibilityFailure(ViewportVisibilityError Code, AlignmentFaceFailure? Face = null,
    string? Detail = null);
public sealed record ViewportReply<T>(T? Value, ViewportVisibilityFailure? Error) where T : class;
