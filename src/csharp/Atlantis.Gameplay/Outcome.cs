// Spec 0058 R1, Plan 0058 P6: a value or the refusal that replaced it -- the
// in-band {"ok"}/{"err"} results of atlantis.remote/1 at the session level
// (the C++ atlantis::Result). The Gameplay layer turns a query's refusal into
// RefusedException (J7).
using System;

namespace Atlantis.Gameplay;

public readonly struct Outcome<T, TError> where TError : struct, Enum
{
    private readonly T? _value;
    private readonly TError _error;

    private Outcome(bool ok, T? value, TError error)
    {
        IsOk = ok;
        _value = value;
        _error = error;
    }

    public static Outcome<T, TError> Ok(T value) => new(true, value, default);
    public static Outcome<T, TError> Err(TError error) => new(false, default, error);

    public bool IsOk { get; }

    public T Value => IsOk ? _value! : throw new InvalidOperationException("the outcome is the refusal " + _error);

    public TError Error => !IsOk ? _error : throw new InvalidOperationException("the outcome is a value, not a refusal");

    public override string ToString() => IsOk ? "Ok(" + _value + ")" : "Err(" + _error + ")";
}
