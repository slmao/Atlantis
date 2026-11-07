// Plan 0065 P6 (Spec 0065 R11, Q5 S-a, Q8 G-b; ADR-0130 D1, D3): the
// authoring document's value types in C# -- the counterparts of the C++
// connection::authoring values (atlantis/connection/authoring_connection.h),
// as atlantis.remote/1 carries them (remote_protocol.md section 6.8). The
// session is reflective only: components and fields are addressed by the ids
// the authoring schema gives, values are the SDK's PropertyValue, and no
// typed bindings of the scene components exist. Plain immutable values.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Atlantis.Gameplay.Authoring;

/// <summary>Where the open document came from.</summary>
public enum Provenance
{
    HandAuthored,  // its committed source file; Save writes it back
    Imported,      // the build's generated source; editable and applyable, never saved
}

/// <summary>The open document's identity: its scene, provenance, source path and node count.</summary>
public sealed record DocumentInfo(AssetGuid Scene, Provenance Provenance, string SourcePath, ulong NodeCount);

/// <summary>One node, in the document's order, with its parent (null at the root).</summary>
public readonly record struct NodeSummary(EntityGuid Node, EntityGuid? Parent, string Name = "");

/// <summary>Spec 0069's authoring label domain; names never identify a node.</summary>
public static class NodeNames
{
    public const int MaxUtf8Bytes = 255;
    private static readonly UTF8Encoding Utf8 = new(false, true);

    public static bool IsValid(string? name)
    {
        if (!IsWellFormed(name)) return false;
        if (Utf8.GetByteCount(name!) > MaxUtf8Bytes) return false;
        foreach (char c in name!)
            if (c <= '\u001f' || c is >= '\u007f' and <= '\u009f' || c is '\u2028' or '\u2029') return false;
        return true;
    }

    internal static bool IsWellFormed(string? name)
    {
        if (name is null) return false;
        try { _ = Utf8.GetByteCount(name); return true; }
        catch (EncoderFallbackException) { return false; }
    }
}

/// <summary>One property of one node: the scene component and the leaf field (ids from the authoring schema).</summary>
public readonly record struct SceneAddress(EntityGuid Node, TypeId Component, FieldId Field);

/// <summary>An authoring command: applied when submitted, or refused and reported by ticket.</summary>
public abstract record Command
{
    private protected Command() { }
}

public sealed record SetProperty(SceneAddress Address, PropertyValue Value) : Command;

/// <summary>
/// A node at <paramref name="Position"/> in the node order, with
/// <paramref name="Components"/> besides its Transform; <paramref name="Node"/>
/// null has the session mint its GUID.
/// </summary>
public sealed record CreateNode(EntityGuid? Node, ulong Position, IReadOnlyList<TypeId> Components) : Command
{
    public bool Equals(CreateNode? other) =>
        other is not null && Node == other.Node && Position == other.Position && Components.SequenceEqual(other.Components);

    public override int GetHashCode() => HashCode.Combine(Node, Position, Components.Count);
}

public sealed record DestroyNode(EntityGuid Node) : Command;
public sealed record AddComponent(EntityGuid Node, TypeId Component) : Command;
public sealed record RemoveComponent(EntityGuid Node, TypeId Component) : Command;

/// <summary>Reparents <paramref name="Node"/>; a null parent makes it a root.</summary>
public sealed record SetParent(EntityGuid Node, EntityGuid? Parent) : Command;

/// <summary>Sets the active camera; null clears it.</summary>
public sealed record SetActiveCamera(EntityGuid? Camera) : Command;

/// <summary>Sets an authoring label; empty clears it. The Runtime validates its domain.</summary>
public sealed record SetNodeName(EntityGuid Node, string Name) : Command;

/// <summary>Event kinds; a set of them is a filter's <see cref="EventFilter.Kinds"/>.</summary>
[Flags]
public enum EventKind
{
    None = 0,
    PropertyChanged = 1,
    NodeCreated = 2,
    NodeDestroyed = 4,
    ComponentAdded = 8,
    ComponentRemoved = 16,
    ParentChanged = 32,
    ActiveCameraChanged = 64,
    DocumentReopened = 128,
    Applied = 256,
    NodeNameChanged = 512,
    All = PropertyChanged | NodeCreated | NodeDestroyed | ComponentAdded | ComponentRemoved | ParentChanged |
          ActiveCameraChanged | DocumentReopened | Applied | NodeNameChanged,
}

/// <summary>A change to the document, in the order applied (or the Runtime's reload of it: Applied).</summary>
public abstract record Event
{
    private protected Event() { }

    public abstract EventKind Kind { get; }
}

public sealed record PropertyChanged(SceneAddress Address, PropertyValue Value) : Event
{
    public override EventKind Kind => EventKind.PropertyChanged;
}

/// <summary>A node created at <paramref name="Position"/>; one ComponentAdded per initial component follows.</summary>
public sealed record NodeCreated(EntityGuid Node, ulong Position) : Event
{
    public override EventKind Kind => EventKind.NodeCreated;
}

public sealed record NodeDestroyed(EntityGuid Node) : Event
{
    public override EventKind Kind => EventKind.NodeDestroyed;
}

public sealed record ComponentAdded(EntityGuid Node, TypeId Component) : Event
{
    public override EventKind Kind => EventKind.ComponentAdded;
}

public sealed record ComponentRemoved(EntityGuid Node, TypeId Component) : Event
{
    public override EventKind Kind => EventKind.ComponentRemoved;
}

public sealed record ParentChanged(EntityGuid Node, EntityGuid? Parent) : Event
{
    public override EventKind Kind => EventKind.ParentChanged;
}

public sealed record ActiveCameraChanged(EntityGuid? Camera) : Event
{
    public override EventKind Kind => EventKind.ActiveCameraChanged;
}

/// <summary>The document was (re)read from its source by an Open.</summary>
public sealed record DocumentReopened(AssetGuid Scene) : Event
{
    public override EventKind Kind => EventKind.DocumentReopened;
}

/// <summary>The Runtime swapped in an applied document; its new RuntimeStatus.SceneGeneration.</summary>
public sealed record Applied(ulong Generation) : Event
{
    public override EventKind Kind => EventKind.Applied;
}

public sealed record NodeNameChanged(EntityGuid Node, string Name) : Event
{
    public override EventKind Kind => EventKind.NodeNameChanged;
}

/// <summary>
/// Which events a subscription delivers: those of a listed kind, and -- when
/// set -- of that node and that component. ActiveCameraChanged,
/// DocumentReopened and Applied carry no node; only PropertyChanged,
/// ComponentAdded and ComponentRemoved carry a component.
/// </summary>
public sealed record EventFilter(EventKind Kinds = EventKind.All, EntityGuid? Node = null, TypeId? Component = null);

/// <summary>Why a query or a submission was refused (the wire names): addressing, then the scene schema's constraints.</summary>
public enum AuthoringError
{
    NoDocument,
    UnknownNode,
    UnknownComponentType,
    ComponentMissing,
    ComponentAlreadyPresent,
    ComponentRequired,
    UnknownField,
    KindMismatch,
    EnumValueOutOfRange,
    NodePositionOutOfRange,
    FieldOutOfDomain,
    NonEmptyDocument,
    NodeGuidNonNil,
    NodeGuidUnique,
    ParentExists,
    ParentAcyclic,
    ActiveCameraExists,
    ActiveCameraHasCamera,
    ComponentExclusivity,
    LightRangeMatchesKind,
    MaxDirectionalLights,
    MaxPointLights,
    CookRefused,
    NotRepresentableInSource,
    InvalidNodeName,
}

/// <summary>
/// A refused submission, reported to the session that submitted it. An
/// addressing refusal names the refused command; a validity refusal names the
/// transaction's first command. A refused transaction changes nothing.
/// </summary>
public readonly record struct CommandFailure(CommandTicket Ticket, AuthoringError Error);

/// <summary>Why Open failed.</summary>
public enum OpenError
{
    SourceUnreadable,
    SourceParseFailed,
    SourceInvalid,
}

/// <summary>Why Save failed.</summary>
public enum SaveError
{
    NoDocument,
    ImportedSceneNotSavable,
    WriteFailed,
}

/// <summary>Why Apply was not accepted.</summary>
public enum ApplyError
{
    NoDocument,
    NoSceneSink,
    CookFailed,
    DecodeFailed,
    ReloadRefused,
}

/// <summary>Opaque apply identity; distinct from an asset/entity GUID. Default is invalid nil.</summary>
public readonly record struct ApplyId(Guid Value)
{
    public bool IsNil => Value == Guid.Empty;
    public static ApplyId Parse(string text) => TryParse(text, out ApplyId id) ? id : throw new FormatException("invalid apply id");
    public static bool TryParse(string? text, out ApplyId id)
    {
        bool ok = AssetGuid.TryParse(text, out AssetGuid guid) && guid.Value != Guid.Empty;
        id = ok ? new ApplyId(guid.Value) : default;
        return ok;
    }
    public override string ToString() => new AssetGuid(Value).ToString();
}
public sealed record ApplyReceipt(ApplyId Id, ulong DocumentRevision);
public enum ApplyState { Pending, Applied, Failed, Superseded }
public enum ReloadError { ScenePreparationFailed, RenderPreparationFailed, RuntimeStopped }
public enum ApplyLookupError { UnknownApply }
public sealed record ApplyRecord(ApplyReceipt Receipt, ApplyState State, ulong? Generation = null,
    ReloadError? Error = null, ApplyId? SupersededBy = null);
public sealed record ApplyProjection(ApplyReceipt Receipt, ulong Generation);
/// <summary>Owned bounded observation. Projection is origin, not equality with disposable World writes.</summary>
public sealed record ApplySnapshot(ulong? CurrentDocumentRevision, ApplyRecord? Request, ApplyProjection? Projection);
