// Spec 0058 R3/R4, ADR-0112 D5, Plan 0058 P6: a transaction builder,
// independent of any session -- the C# counterpart of the C++ SDK's
// atlantis::gameplay::Transaction. It records operations in call order and
// resolves nothing; GameplayWorld.Submit resolves it whole -- names through
// SchemaText, typed operations through their bindings -- and submits it as
// one connection.submitTransaction, or refuses all of it (throws, nothing
// sent, no ticket) if any name or binding fails. It never reorders, merges or
// splits operations. A plain value; not thread-safe.
using System;
using System.Collections.Generic;

namespace Atlantis.Gameplay;

public sealed class Transaction
{
    public enum Kind
    {
        Create,
        Destroy,
        AddComponent,
        RemoveComponent,
        SetProperty,
    }

    /// <summary>
    /// What an operation names: by name (a type name, or a property path), by id
    /// (Component, Field), or typed (Binding set: the generated module and the
    /// type's binding, plus the ids).
    /// </summary>
    public sealed record Target(string? Name, TypeId Component, FieldId Field, TypeBinding[]? Module, TypeBinding? Binding);

    public sealed record Operation(Kind Kind, EntityGuid Entity, Target Target, PropertyValue Value);

    private static readonly Target None = new(null, default, default, null, null);

    private readonly List<Operation> _operations = new();

    public IReadOnlyList<Operation> Operations => _operations;
    public bool IsEmpty => _operations.Count == 0;

    public Transaction Create(EntityGuid entity) => Push(Kind.Create, entity, None, PropertyValue.Absent);
    public Transaction Destroy(EntityGuid entity) => Push(Kind.Destroy, entity, None, PropertyValue.Absent);

    /// <summary>A bare AddComponent: the component with the World's default values.</summary>
    public Transaction Add(EntityGuid entity, string type) =>
        Push(Kind.AddComponent, entity, None with { Name = type }, PropertyValue.Absent);

    public Transaction Add(EntityGuid entity, TypeId type) =>
        Push(Kind.AddComponent, entity, None with { Component = type }, PropertyValue.Absent);

    /// <summary>
    /// AddComponent, then one SetProperty per given leaf, in the given order;
    /// leaf paths are relative to the type ("kind", "fog.density").
    /// </summary>
    public Transaction Add(EntityGuid entity, string type, IEnumerable<(string Leaf, PropertyValue Value)> leaves)
    {
        Add(entity, type);
        foreach ((string leaf, PropertyValue value) in leaves) Set(entity, type + "." + leaf, value);
        return this;
    }

    public Transaction Remove(EntityGuid entity, string type) =>
        Push(Kind.RemoveComponent, entity, None with { Name = type }, PropertyValue.Absent);

    public Transaction Remove(EntityGuid entity, TypeId type) =>
        Push(Kind.RemoveComponent, entity, None with { Component = type }, PropertyValue.Absent);

    public Transaction Set(EntityGuid entity, string path, PropertyValue value) =>
        Push(Kind.SetProperty, entity, None with { Name = path }, value);

    public Transaction Set(PropertyAddress address, PropertyValue value) =>
        Push(Kind.SetProperty, address.Entity, None with { Component = address.Component, Field = address.Field }, value);

    // --- Typed (checked against the Runtime's schema at Submit).

    /// <summary>
    /// AddComponent, then one SetProperty per leaf in the schema's leaf order.
    /// Only for a type whose every leaf is Editable: for another, add the bare
    /// component and set its editable leaves. Every leaf is written, so a
    /// default T writes zeros and enum value 0 -- not the World's defaults;
    /// <see cref="Add{T}(EntityGuid)"/> gives the World's defaults.
    /// </summary>
    public Transaction Add<T>(EntityGuid entity, T value) where T : struct, IEditableComponent<T>
    {
        Add<T>(entity);
        FieldId[] leaves = T.Leaves;
        var values = new PropertyValue[leaves.Length];
        T.Write(value, values);
        for (int i = 0; i < leaves.Length; ++i) Push(Kind.SetProperty, entity, Typed<T>(leaves[i]), values[i]);
        return this;
    }

    /// <summary>A bare AddComponent: the component with the World's default values.</summary>
    public Transaction Add<T>(EntityGuid entity) where T : struct, IComponent<T> =>
        Push(Kind.AddComponent, entity, Typed<T>(default), PropertyValue.Absent);

    public Transaction Remove<T>(EntityGuid entity) where T : struct, IComponent<T> =>
        Push(Kind.RemoveComponent, entity, Typed<T>(default), PropertyValue.Absent);

    /// <summary>A typed set. A <see cref="ReadOnlyField{TC,TV}"/>, or a value of another type, does not compile.</summary>
    public Transaction Set<TC, TV>(EntityGuid entity, Field<TC, TV> field, TV value) where TC : struct, IComponent<TC> =>
        Push(Kind.SetProperty, entity, Typed<TC>(field.Id), field.Encode(value));

    private static Target Typed<T>(FieldId field) where T : struct, IComponent<T> =>
        new(null, T.Binding.Id, field, T.Module, T.Binding);

    private Transaction Push(Kind kind, EntityGuid entity, Target target, PropertyValue value)
    {
        _operations.Add(new Operation(kind, entity, target, value));
        return this;
    }
}

/// <summary>
/// Failures drained from a session, kept so a client can ask about any of its
/// tickets later (one failure per refused command or aborted transaction,
/// routed by ticket). A plain value; not thread-safe.
/// </summary>
public sealed class FailureLog
{
    private readonly List<CommandFailure> _failures = new();

    public IReadOnlyList<CommandFailure> Failures => _failures;

    public void Absorb(IEnumerable<CommandFailure> failures) => _failures.AddRange(failures);

    public CommandFailure? Refusal(CommandTicket ticket)
    {
        foreach (CommandFailure failure in _failures)
        {
            if (failure.Ticket == ticket) return failure;
        }
        return null;
    }

    /// <summary>The failure whose ticket lies in the transaction's range: it was aborted there, and none of it applied.</summary>
    public CommandFailure? Refusal(TransactionTicket ticket)
    {
        foreach (CommandFailure failure in _failures)
        {
            if (ticket.Contains(failure.Ticket)) return failure;
        }
        return null;
    }
}
