// Spec 0058 ruling Q5, Plan 0058 P6 (J7): what the C# SDK throws. A
// transport failure, a name that does not resolve, a binding that does not
// match the Runtime's schema, and a World refusal of a *query* are thrown. A
// World refusal of a *command* is not an exception: it comes back later as a
// CommandFailure under the command's ticket, as RuntimeConnection reports it.
using System;

namespace Atlantis.Gameplay;

/// <summary>The base of every exception the C# SDK throws.</summary>
public abstract class GameplayException : Exception
{
    private protected GameplayException(string message) : base(message) { }
}

/// <summary>Why a remote session failed (the C++ RemoteError's names).</summary>
public enum RemoteError
{
    ConnectFailed,  // nothing listening on the session's port
    BadToken,       // the Runtime refused the session token
    WrongProtocol,  // the Runtime speaks another protocol
    Disconnected,   // the connection closed
    Timeout,        // no response in time
    ProtocolError,  // a response that does not decode, or a refused request
}

/// <summary>
/// The session failed. Once a session has failed, every later call on it
/// throws this again with the first failure.
/// </summary>
public sealed class RemoteException : GameplayException
{
    public RemoteException(RemoteError error, string detail)
        : base(error + (detail.Length > 0 ? ": " + detail : ""))
    {
        Error = error;
    }

    public RemoteError Error { get; }
}

/// <summary>A type name or property path that does not resolve in the Runtime's schema. Nothing was sent.</summary>
public sealed class UnknownNameException : GameplayException
{
    public UnknownNameException(NameError error, string subject) : base(error + ": " + subject)
    {
        Error = error;
        Subject = subject;
    }

    public NameError Error { get; }
    public string Subject { get; }
}

/// <summary>
/// A generated binding does not match the Runtime's schema (the type, or a
/// type it references). Nothing was sent.
/// </summary>
public sealed class SchemaMismatchException : GameplayException
{
    public SchemaMismatchException(string subject) : base("SchemaMismatch: " + subject)
    {
        Subject = subject;
    }

    public string Subject { get; }
}

/// <summary>The World refused a query (for example ComponentMissing on a read).</summary>
public sealed class RefusedException : GameplayException
{
    public RefusedException(AccessError error, string subject) : base("Refused: " + subject + " (" + error + ")")
    {
        Error = error;
        Subject = subject;
    }

    public AccessError Error { get; }
    public string Subject { get; }
}

/// <summary>A connection-level refusal (an unknown or released subscription).</summary>
public sealed class ConnectionRefusedException : GameplayException
{
    public ConnectionRefusedException(ConnectionError error, string subject) : base("Connection: " + subject + " (" + error + ")")
    {
        Error = error;
        Subject = subject;
    }

    public ConnectionError Error { get; }
    public string Subject { get; }
}
