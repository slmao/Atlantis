using Atlantis.Gameplay.Remote;

namespace Atlantis.Mcp;

/// <summary>
/// One atlantis.remote/1 request and its whole response envelope. Implementations serialize access;
/// callers must not mutate arguments during a call. Wire refusals stay in the envelope; transport
/// failures throw RemoteException. No retries or policy validation (Plan 0070 P2, ADR-0138 D3).
/// </summary>
internal interface IAtlantisSession
{
    JsonValue Request(string method, JsonValue parameters);
}

/// <summary>
/// Borrows the SDK session for the host's lifetime. Thread-safe: the tools and the liveness watcher
/// share this lock because RemoteSession is not thread-safe. The owner disposes it after use ends.
/// </summary>
internal sealed class AtlantisSession(RemoteSession session) : IAtlantisSession
{
    private readonly object _gate = new();

    public JsonValue Request(string method, JsonValue parameters)
    {
        // Raw SDK requests preserve wire shapes (including schema offsets, literal numbers and
        // refusals) that decoding to SDK values and serializing those values would change.
        lock (_gate) return session.AwaitResponse(session.Send(method, parameters));
    }
}
