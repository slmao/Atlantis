// Spec 0058 R3/R4, ADR-0112 D5, Plan 0058 P6 (J6, J7): the C# Gameplay SDK's
// World -- the reflective layer (types and fields by name or id, component
// reads, the component filter, transactions, subscriptions) and the typed
// layer (generated value types and field handles) over one RemoteSession.
// The same model as the C++ SDK's atlantis::gameplay::World (ADR-0110): every
// operation is a sequence of atlantis.remote/1 requests a client could make
// by hand, in the order the client wrote it; the SDK adds no rule the World
// owns (finiteness, limits, editability and enum ranges stay its refusals).
//
// Errors (ruling Q5, J7): a name that does not resolve throws
// UnknownNameException and a binding that does not match throws
// SchemaMismatchException -- nothing is sent. A query the World refuses
// throws RefusedException. A command the World refuses is not an exception:
// it is applied (or refused) at the Runtime's next frame and reported by
// ticket (DrainFailures, FailureLog).
//
// Reads are not snapshots (Spec 0057 R10): each leaf is as it was when it was
// answered. A multi-leaf read is consistent only if, for its whole duration,
// the Runtime stays paused, no client steps or resumes it, and no step is
// pending; nothing here enforces that.
//
// Not thread-safe: one thread uses a GameplayWorld and its session.
using System;
using System.Collections.Generic;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Gameplay;

/// <summary>One leaf of a component read: its canonical path, id and value.</summary>
public sealed record DynamicLeaf(string Path, FieldId Field, PropertyValue Value);

/// <summary>A component's leaves, in leaf order, with values (not a snapshot).</summary>
public sealed record DynamicComponent(TypeId Type, string Name, IReadOnlyList<DynamicLeaf> Leaves);

public sealed class GameplayWorld
{
    private readonly Dictionary<TypeId, bool> _compatible = new();  // per type: the binding matched (R4)

    public GameplayWorld(RemoteSession session) => Session = session;

    public RemoteSession Session { get; }
    public IReadOnlyList<SchemaType> Schema => Session.Schema;

    // --- Queries.

    public bool Exists(EntityGuid entity) => Session.FindEntity(entity);

    public IReadOnlyList<EntityGuid> Entities() => Session.ListEntities();

    public IReadOnlyList<TypeId> Components(EntityGuid entity)
    {
        Outcome<IReadOnlyList<TypeId>, AccessError> listed = Session.ListComponents(entity);
        return listed.IsOk ? listed.Value : throw new RefusedException(listed.Error, entity.ToString());
    }

    /// <summary>
    /// Every listed entity holding all of <paramref name="types"/> (short or
    /// qualified names), in listing order: one listEntities and one pipelined
    /// listComponents for all of them, filtered here.
    /// </summary>
    public IReadOnlyList<EntityGuid> EntitiesWith(params string[] types)
    {
        var ids = new TypeId[types.Length];
        for (int i = 0; i < types.Length; ++i) ids[i] = TypeNamed(types[i]);
        return EntitiesWith(ids);
    }

    public IReadOnlyList<EntityGuid> EntitiesWith(params TypeId[] types)
    {
        IReadOnlyList<EntityGuid> listed = Session.ListEntities();
        IReadOnlyList<Outcome<IReadOnlyList<TypeId>, AccessError>> components = Session.ListComponents(listed);
        var output = new List<EntityGuid>();
        for (int i = 0; i < listed.Count && i < components.Count; ++i)
        {
            if (!components[i].IsOk) continue;  // destroyed between the listing and its query
            IReadOnlyList<TypeId> held = components[i].Value;
            bool all = true;
            foreach (TypeId type in types) all &= Contains(held, type);
            if (all) output.Add(listed[i]);
        }
        return output;
    }

    private static bool Contains(IReadOnlyList<TypeId> held, TypeId type)
    {
        foreach (TypeId candidate in held)
        {
            if (candidate == type) return true;
        }
        return false;
    }

    // --- Properties, by path ("Light.intensity", "Camera.fog.density") or address.

    public PropertyAddress Resolve(EntityGuid entity, string path)
    {
        if (!SchemaText.TryParsePath(Schema, path, out TypeId component, out SchemaField? leaf, out NameError error))
        {
            throw new UnknownNameException(error, path);
        }
        return new PropertyAddress(entity, component, leaf!.Id);
    }

    public PropertyValue Get(EntityGuid entity, string path)
    {
        PropertyAddress address = Resolve(entity, path);
        Outcome<PropertyValue, AccessError> value = Session.GetProperty(address);
        return value.IsOk ? value.Value : throw new RefusedException(value.Error, path);
    }

    public PropertyValue Get(PropertyAddress address)
    {
        Outcome<PropertyValue, AccessError> value = Session.GetProperty(address);
        return value.IsOk ? value.Value : throw new RefusedException(value.Error, address.Entity.ToString());
    }

    /// <summary>One SetProperty, submitted alone.</summary>
    public CommandTicket Set(EntityGuid entity, string path, PropertyValue value) =>
        Session.Submit(new SetProperty(Resolve(entity, path), value));

    /// <summary>Every leaf of a component (by short or qualified name), in one pipelined batch.</summary>
    public DynamicComponent ReadComponent(EntityGuid entity, string type)
    {
        TypeId id = TypeNamed(type);
        List<LeafPath> leaves = SchemaText.LeavesOf(Schema, id);
        var addresses = new PropertyAddress[leaves.Count];
        for (int i = 0; i < leaves.Count; ++i) addresses[i] = new PropertyAddress(entity, id, leaves[i].Leaf.Id);
        IReadOnlyList<Outcome<PropertyValue, AccessError>> values = Session.GetProperties(addresses);
        var read = new List<DynamicLeaf>(leaves.Count);
        for (int i = 0; i < leaves.Count; ++i)
        {
            if (!values[i].IsOk) throw new RefusedException(values[i].Error, leaves[i].Path);
            read.Add(new DynamicLeaf(leaves[i].Path, leaves[i].Leaf.Id, values[i].Value));
        }
        return new DynamicComponent(id, SchemaText.FindType(Schema, id)!.Name, read);
    }

    // --- Submission.

    /// <summary>
    /// Resolves the transaction whole and submits it as one transaction. Any
    /// name or binding error throws before anything is sent: no ticket.
    /// </summary>
    public TransactionTicket Submit(Transaction transaction)
    {
        var commands = new List<Command>(transaction.Operations.Count);
        foreach (Transaction.Operation operation in transaction.Operations) commands.Add(ResolveOperation(operation));
        return Session.SubmitTransaction(commands);
    }

    /// <summary>One command, passed through unchanged.</summary>
    public CommandTicket Submit(Command command) => Session.Submit(command);

    // --- Events and outcomes.

    /// <summary>A subscription, released when disposed.</summary>
    public Subscription Subscribe(EventFilter? filter = null) => new(Session, Session.Subscribe(filter ?? new EventFilter()));

    public IReadOnlyList<CommandFailure> DrainFailures() => Session.DrainFailures();

    // --- Typed. Every typed call first checks its type's generated binding --
    // and, recursively, every struct or enum it references -- against the
    // Runtime's schema (once per type, cached for this GameplayWorld); on a
    // mismatch it throws SchemaMismatchException and sends nothing. The
    // reflective calls above are never blocked by a binding.

    public void CheckCompatible<T>() where T : struct, IComponent<T> => CheckBinding(T.Module, T.Binding);

    public void CheckBinding(TypeBinding[] module, TypeBinding binding)
    {
        if (!_compatible.TryGetValue(binding.Id, out bool compatible))
        {
            compatible = Compatibility.Matches(module, binding, Schema, new HashSet<TypeId>());
            _compatible[binding.Id] = compatible;
        }
        if (!compatible) throw new SchemaMismatchException(binding.Name);
    }

    /// <summary>One leaf's value as the handle's C# type.</summary>
    public TV Get<TC, TV>(EntityGuid entity, FieldHandle<TC, TV> field) where TC : struct, IComponent<TC>
    {
        CheckCompatible<TC>();
        Outcome<PropertyValue, AccessError> value = Session.GetProperty(new PropertyAddress(entity, field.Component, field.Id));
        if (!value.IsOk) throw new RefusedException(value.Error, field.Path);
        return field.TryDecode(value.Value, out TV result) ? result : throw new SchemaMismatchException(field.Path);
    }

    /// <summary>One SetProperty, submitted alone. A ReadOnlyField, or a value of another type, does not compile.</summary>
    public CommandTicket Set<TC, TV>(EntityGuid entity, Field<TC, TV> field, TV value) where TC : struct, IComponent<TC>
    {
        CheckCompatible<TC>();
        return Session.Submit(new SetProperty(new PropertyAddress(entity, field.Component, field.Id), field.Encode(value)));
    }

    /// <summary>Every leaf of <typeparamref name="T"/> in one pipelined batch (not a snapshot).</summary>
    public T Get<T>(EntityGuid entity) where T : struct, IComponent<T>
    {
        CheckCompatible<T>();
        TypeBinding binding = T.Binding;
        FieldId[] leaves = T.Leaves;
        var addresses = new PropertyAddress[leaves.Length];
        for (int i = 0; i < leaves.Length; ++i) addresses[i] = new PropertyAddress(entity, binding.Id, leaves[i]);
        IReadOnlyList<Outcome<PropertyValue, AccessError>> answers = Session.GetProperties(addresses);
        var values = new PropertyValue[leaves.Length];
        for (int i = 0; i < values.Length; ++i)
        {
            if (!answers[i].IsOk) throw new RefusedException(answers[i].Error, binding.Name);
            values[i] = answers[i].Value;
        }
        return T.TryRead(values, out T value) ? value : throw new SchemaMismatchException(binding.Name);
    }

    public IReadOnlyList<EntityGuid> EntitiesWith<T1>() where T1 : struct, IComponent<T1>
    {
        CheckCompatible<T1>();
        return EntitiesWith(T1.Binding.Id);
    }

    public IReadOnlyList<EntityGuid> EntitiesWith<T1, T2>()
        where T1 : struct, IComponent<T1>
        where T2 : struct, IComponent<T2>
    {
        CheckCompatible<T1>();
        CheckCompatible<T2>();
        return EntitiesWith(T1.Binding.Id, T2.Binding.Id);
    }

    public IReadOnlyList<EntityGuid> EntitiesWith<T1, T2, T3>()
        where T1 : struct, IComponent<T1>
        where T2 : struct, IComponent<T2>
        where T3 : struct, IComponent<T3>
    {
        CheckCompatible<T1>();
        CheckCompatible<T2>();
        CheckCompatible<T3>();
        return EntitiesWith(T1.Binding.Id, T2.Binding.Id, T3.Binding.Id);
    }

    /// <summary>
    /// The handle's value if <paramref name="evt"/> is a PropertyChanged of that
    /// property: true with the value; false for any other event.
    /// </summary>
    public bool TryDecode<TC, TV>(Event evt, FieldHandle<TC, TV> field, out TV value) where TC : struct, IComponent<TC>
    {
        CheckCompatible<TC>();
        value = default!;
        if (evt is not PropertyChanged changed || changed.Address.Component != field.Component || changed.Address.Field != field.Id)
        {
            return false;
        }
        return field.TryDecode(changed.Value, out value) ? true : throw new SchemaMismatchException(field.Path);
    }

    // --- Resolution.

    private TypeId TypeNamed(string type)
    {
        SchemaType? found = SchemaText.FindType(Schema, type);
        if (found is null || found.Kind != TypeKind.Struct) throw new UnknownNameException(NameError.UnknownType, type);
        return found.Id;
    }

    private Command ResolveOperation(Transaction.Operation operation)
    {
        Transaction.Target target = operation.Target;
        if (target.Binding is not null) CheckBinding(target.Module!, target.Binding);  // typed: refuses the whole transaction
        switch (operation.Kind)
        {
            case Transaction.Kind.Create: return new CreateEntity(operation.Entity);
            case Transaction.Kind.Destroy: return new DestroyEntity(operation.Entity);
            case Transaction.Kind.AddComponent:
            case Transaction.Kind.RemoveComponent:
            {
                TypeId type = target.Name is not null ? TypeNamed(target.Name) : target.Component;
                return operation.Kind == Transaction.Kind.AddComponent
                    ? new AddComponent(operation.Entity, type)
                    : new RemoveComponent(operation.Entity, type);
            }
            default:
            {
                PropertyAddress address = target.Name is not null
                    ? Resolve(operation.Entity, target.Name)
                    : new PropertyAddress(operation.Entity, target.Component, target.Field);
                return new SetProperty(address, operation.Value);
            }
        }
    }
}

/// <summary>
/// Spec 0058 R4, ADR-0112 D3 (ADR-0111 D4's rule): a generated binding against
/// the Runtime's schema -- the type's name, id, kind and version, its exact
/// field set (name, id, kind, primitive, referenced type, Optional, Editable),
/// its constants by name and value, and recursively every struct or enum its
/// fields reference.
/// </summary>
internal static class Compatibility
{
    public static bool Matches(TypeBinding[] module, TypeBinding binding, IReadOnlyList<SchemaType> schema, HashSet<TypeId> seen)
    {
        if (!seen.Add(binding.Id)) return true;
        SchemaType? descriptor = SchemaText.FindType(schema, binding.Id);
        if (descriptor is null) return false;
        if (descriptor.Name != binding.Name || descriptor.Kind != binding.Kind || descriptor.Version != binding.Version) return false;
        if (descriptor.Fields.Count != binding.Fields.Length) return false;
        foreach (FieldBinding field in binding.Fields)
        {
            SchemaField? found = null;
            foreach (SchemaField candidate in descriptor.Fields)
            {
                if (candidate.Id == field.Id) found = candidate;
            }
            if (found is null || !SameField(field, found)) return false;
        }
        if (descriptor.Constants.Count != binding.Constants.Length) return false;
        foreach (EnumConstantBinding constant in binding.Constants)
        {
            SchemaConstant? found = null;
            foreach (SchemaConstant candidate in descriptor.Constants)
            {
                if (candidate.Name == constant.Name) found = candidate;
            }
            if (found is null || found.Value != constant.Value) return false;
        }
        foreach (FieldBinding field in binding.Fields)
        {
            if (field.Kind == TypeKind.Primitive) continue;
            TypeBinding? referenced = null;
            foreach (TypeBinding candidate in module)
            {
                if (candidate.Id == field.Type) referenced = candidate;
            }
            if (referenced is null || !Matches(module, referenced, schema, seen)) return false;
        }
        return true;
    }

    private static bool SameField(FieldBinding b, SchemaField d)
    {
        if (b.Name != d.Name || b.Id != d.Id || b.Kind != d.Kind || b.Type != d.Type) return false;
        if (b.Kind == TypeKind.Primitive && b.Primitive != d.Primitive) return false;
        return b.Optional == d.IsOptional && b.Editable == d.IsEditable;
    }
}

/// <summary>
/// One pull subscription, owned: unsubscribed when disposed. Not thread-safe
/// (the session's thread).
/// </summary>
public sealed class Subscription : IDisposable
{
    private RemoteSession? _session;

    internal Subscription(RemoteSession session, SubscriptionId id)
    {
        _session = session;
        Id = id;
    }

    public SubscriptionId Id { get; }

    /// <summary>The matching events since the last drain, in application order.</summary>
    public IReadOnlyList<Event> Drain()
    {
        if (_session is null) throw new ConnectionRefusedException(ConnectionError.UnknownSubscription, "a disposed subscription");
        Outcome<IReadOnlyList<Event>, ConnectionError> drained = _session.DrainEvents(Id);
        return drained.IsOk ? drained.Value : throw new ConnectionRefusedException(drained.Error, "subscription " + Id.Value);
    }

    public void Dispose()
    {
        RemoteSession? session = _session;
        _session = null;
        if (session is not null && session.Failure is null) session.Unsubscribe(Id);
    }
}
