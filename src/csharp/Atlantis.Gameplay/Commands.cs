// Spec 0058 R1/R3, Plan 0058 P6: the World's commands and events, tickets,
// failures and the event filter -- RuntimeConnection's value types (Spec
// 0054), as atlantis.remote/1 carries them. Plain immutable values.
using System;

namespace Atlantis.Gameplay;

/// <summary>A World command: applied at the Runtime's next frame, or refused and reported by ticket.</summary>
public abstract record Command
{
    private protected Command() { }
}

public sealed record CreateEntity(EntityGuid Entity) : Command;
public sealed record DestroyEntity(EntityGuid Entity) : Command;
public sealed record AddComponent(EntityGuid Entity, TypeId Component) : Command;
public sealed record RemoveComponent(EntityGuid Entity, TypeId Component) : Command;
public sealed record SetProperty(PropertyAddress Address, PropertyValue Value) : Command;

/// <summary>Event kinds; a set of them is a filter's <see cref="EventFilter.Kinds"/>.</summary>
[Flags]
public enum EventKind
{
    None = 0,
    EntityCreated = 1,
    EntityDestroyed = 2,
    ComponentAdded = 4,
    ComponentRemoved = 8,
    PropertyChanged = 16,
    All = EntityCreated | EntityDestroyed | ComponentAdded | ComponentRemoved | PropertyChanged,
}

/// <summary>Something an applied command changed, in application order.</summary>
public abstract record Event
{
    private protected Event() { }

    public abstract EventKind Kind { get; }
}

public sealed record EntityCreated(EntityGuid Entity) : Event
{
    public override EventKind Kind => EventKind.EntityCreated;
}

public sealed record EntityDestroyed(EntityGuid Entity) : Event
{
    public override EventKind Kind => EventKind.EntityDestroyed;
}

public sealed record ComponentAdded(EntityGuid Entity, TypeId Component) : Event
{
    public override EventKind Kind => EventKind.ComponentAdded;
}

public sealed record ComponentRemoved(EntityGuid Entity, TypeId Component) : Event
{
    public override EventKind Kind => EventKind.ComponentRemoved;
}

/// <summary>A property's new value, as applied.</summary>
public sealed record PropertyChanged(PropertyAddress Address, PropertyValue Value) : Event
{
    public override EventKind Kind => EventKind.PropertyChanged;
}

/// <summary>
/// Which events a subscription delivers: those of a listed kind, and -- when
/// set -- of that entity and that component.
/// </summary>
public sealed record EventFilter(EventKind Kinds = EventKind.All, EntityGuid? Entity = null, TypeId? Component = null);

/// <summary>A submitted command's ticket; its refusal, if any, is reported under it.</summary>
public readonly record struct CommandTicket(ulong Value);

/// <summary>
/// A transaction's tickets: <see cref="Count"/> consecutive tickets from
/// <see cref="First"/> (zero for an empty transaction).
/// </summary>
public readonly record struct TransactionTicket(CommandTicket First, ulong Count)
{
    public bool Contains(CommandTicket ticket) => ticket.Value >= First.Value && ticket.Value - First.Value < Count;
}

/// <summary>Why the World refused a command or a query (the wire names).</summary>
public enum AccessError
{
    UnknownEntity,
    NilGuid,
    DuplicateGuid,
    UnknownComponentType,
    ComponentMissing,
    ComponentAlreadyPresent,
    UnknownField,
    FieldNotEditable,
    KindMismatch,
    EnumValueOutOfRange,
    NonFiniteValue,
    LightLimitExceeded,
    ActiveCameraProtected,
}

/// <summary>A connection-level refusal.</summary>
public enum ConnectionError
{
    UnknownSubscription,
}

/// <summary>
/// A refused command (or an aborted transaction, under the ticket of the
/// command that was refused): none of a transaction applied.
/// </summary>
public readonly record struct CommandFailure(CommandTicket Ticket, AccessError Error);

/// <summary>A subscription's id on its connection.</summary>
public readonly record struct SubscriptionId(ulong Value);
