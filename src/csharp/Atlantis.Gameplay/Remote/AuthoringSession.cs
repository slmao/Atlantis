// Plan 0065 P6 (Spec 0065 R11, Q8 G-b; ADR-0130 D1, D3): authoring.* over a
// session -- the C# counterpart of the C++ RemoteAuthoring
// (src/remote/src/remote_client.cpp), one request per call. Reflective only:
// a scene component and its fields are named, as the C# SDK's World paths are
// (SchemaText), against the authoring schema -- fetched once, at the first
// call that needs it -- and values are PropertyValue. No typed bindings of the
// scene components (atlantis_sdk_codegen is not involved).
//
// Refusals come back in band: a query's as the Outcome's error, a command's
// later as a CommandFailure under its ticket, Open/Apply/Save's as their error
// (null when they succeeded). A Runtime that serves no authoring session
// answers NoAuthoring, which fails the session (RemoteError.ProtocolError,
// "NoAuthoring" in its detail), as NoControl does for control.*.
//
// Apply returns once the Runtime has accepted the document; the reload itself
// happens at the Runtime's next frame that applies commands -- while it is
// paused, at the next step or on resume (Spec 0065 third Correction) -- and is
// reported to every subscription as Applied{generation}.
//
// Not thread-safe: the session's thread.
using System;
using System.Collections.Generic;
using System.Linq;
using A = Atlantis.Gameplay.Authoring;

namespace Atlantis.Gameplay.Remote;

public sealed class AuthoringSession
{
    private readonly RemoteSession _session;
    private IReadOnlyList<SchemaType>? _schema;
    private int? _siblingOrderTier;

    internal AuthoringSession(RemoteSession session) => _session = session;

    // --- The document.

    /// <summary>(Re)reads the Runtime's scene from its source; null on success.</summary>
    public A.OpenError? Open() => Unit("authoring.open", Codec.DecodeOpenError);

    /// <summary>The open document's identity; null before a successful Open.</summary>
    public A.DocumentInfo? Document()
    {
        JsonValue result = _session.Call("authoring.document");
        if (result.IsNull) return null;
        return Codec.DecodeDocumentInfo(result) ?? throw _session.Malformed("authoring.document");
    }

    /// <summary>Hands the document to the Runtime for reload; null when accepted (Applied follows).</summary>
    public A.ApplyError? Apply() => Unit("authoring.apply", Codec.DecodeApplyError);

    /// <summary>Acceptance receipt only. Query its id for completion; never blindly replay a lost reply.</summary>
    public Outcome<A.ApplyReceipt, A.ApplyError> ApplyTracked()
    {
        const string method = "authoring.applyTracked";
        JsonValue result = _session.Call(method);
        if (result.Find("ok") is JsonValue ok)
            return Outcome<A.ApplyReceipt, A.ApplyError>.Ok(Codec.DecodeApplyReceipt(ok) ?? throw _session.Malformed(method));
        if (result.Find("err") is JsonValue err && Codec.DecodeApplyError(err) is A.ApplyError error)
            return Outcome<A.ApplyReceipt, A.ApplyError>.Err(error);
        throw _session.Malformed(method);
    }

    /// <summary>Non-destructive shared status. Null id means global newest, not this caller's receipt.
    /// Transport/protocol failures throw as usual; UnknownApply is no terminal verdict.</summary>
    public Outcome<A.ApplySnapshot, A.ApplyLookupError> ApplyStatus(A.ApplyId? id = null)
    {
        const string method = "authoring.applyStatus";
        JsonValue args = JsonValue.NewObject(); if (id is A.ApplyId known) args.Set("id", JsonValue.String(known.ToString()));
        JsonValue result = _session.Call(method, args);
        if (result.Find("ok") is JsonValue ok)
        {
            A.ApplySnapshot snapshot = Codec.DecodeApplySnapshot(ok) ?? throw _session.Malformed(method);
            if (id is A.ApplyId wanted && (snapshot.Request is null || snapshot.Request.Receipt.Id != wanted)) throw _session.Malformed(method);
            return Outcome<A.ApplySnapshot, A.ApplyLookupError>.Ok(snapshot);
        }
        if (result.Find("err") is JsonValue err && Codec.DecodeApplyLookupError(err) is A.ApplyLookupError error)
            return Outcome<A.ApplySnapshot, A.ApplyLookupError>.Err(error);
        throw _session.Malformed(method);
    }

    /// <summary>Writes a hand-authored document to its source; null on success.</summary>
    public A.SaveError? Save() => Unit("authoring.save", Codec.DecodeSaveError);

    /// <summary>Reads persistence independently of the Runtime's Apply projection.</summary>
    public Outcome<A.DocumentState?, A.DocumentError> DocumentState()
    {
        const string method = "authoring.documentState";
        var reply = _session.AwaitResponse(_session.Send(method, JsonValue.NewObject()));
        if (Codec.DocumentRpcRefusal(reply, method) is { } refusal)
            return Outcome<A.DocumentState?, A.DocumentError>.Err(refusal);
        if (Codec.DocumentResult(reply)?.Find("ok") is not { } ok) throw _session.Malformed(method);
        return Outcome<A.DocumentState?, A.DocumentError>.Ok(ok.IsNull ? null :
            Codec.DecodeDocumentState(ok) ?? throw _session.Malformed(method));
    }

    /// <summary>Atomically saves the reviewed revision. A lost reply has an unknown outcome.</summary>
    public Outcome<A.DocumentState, A.DocumentError> SaveIfCurrent(A.DocumentToken expected) => ChangeDocument(expected, true);

    /// <summary>Reopens the reviewed document. Null opens the source only while no document exists.</summary>
    public Outcome<A.DocumentState, A.DocumentError> ReopenIfCurrent(A.DocumentToken? expected) => ChangeDocument(expected, false);

    private Outcome<A.DocumentState, A.DocumentError> ChangeDocument(A.DocumentToken? expected, bool saving)
    {
        if (expected is { } token && (token.DocumentId.IsNil || token.DocumentRevision == 0))
            return Outcome<A.DocumentState, A.DocumentError>.Err(A.DocumentError.InvalidRequest);
        string method = saving ? "authoring.saveIfCurrent" : "authoring.reopenIfCurrent";
        var parameters = JsonValue.NewObject().Set("expected", expected is { } key ? Codec.Encode(key) : JsonValue.Null());
        _session.ThrowIfFailed();
        try
        {
            var reply = _session.AwaitResponse(_session.Send(method, parameters));
            if (Codec.DocumentRpcRefusal(reply, method) is { } refusal)
                return Outcome<A.DocumentState, A.DocumentError>.Err(refusal);
            var result = Codec.DocumentResult(reply);
            if (result?.Find("err") is { } err && Codec.DecodeDocumentError(err, saving) is { } error)
                return Outcome<A.DocumentState, A.DocumentError>.Err(error);
            if (result?.Find("ok") is { } ok && Codec.DecodeDocumentState(ok) is { } state &&
                Codec.ValidDocumentReceipt(state, expected, saving)) return Outcome<A.DocumentState, A.DocumentError>.Ok(state);
            throw _session.Malformed(method);
        }
        catch (RemoteException error)
        {
            string target = expected is { } known ? $"{known.DocumentId}, revision {known.DocumentRevision}" : "no open document";
            throw new RemoteException(error, $"{method} outcome unknown for {target}. Check document state on the original Runtime before retrying.");
        }
    }

    /// <summary>Reads capture status, or the reviewed copy's bounded source comparison.</summary>
    public Outcome<A.RecoveryStatus, A.RecoveryError> RecoveryStatus(EntityGuid? copyId = null)
    {
        const string method = "authoring.recoveryStatus";
        if (copyId is { IsNil: true }) return Outcome<A.RecoveryStatus, A.RecoveryError>.Err(A.RecoveryError.InvalidRequest);
        var parameters = JsonValue.NewObject();
        if (copyId is { } id) parameters.Set("copyId", Codec.Encode(id));
        var reply = _session.AwaitResponse(_session.Send(method, parameters));
        if (Codec.RecoveryRpcRefusal(reply, method) is { } refusal)
            return Outcome<A.RecoveryStatus, A.RecoveryError>.Err(refusal);
        var result = Codec.DocumentResult(reply);
        if (result?.Find("err") is { } err && Codec.DecodeRecoveryError(err) is { } failure)
            return Outcome<A.RecoveryStatus, A.RecoveryError>.Err(failure);
        if (result?.Find("ok") is { } ok && Codec.DecodeRecoveryStatus(ok, copyId) is { } value)
            return Outcome<A.RecoveryStatus, A.RecoveryError>.Ok(value);
        throw _session.Malformed(method);
    }

    /// <summary>Restores only the reviewed copy into a fresh, dirty authoring document.</summary>
    public Outcome<A.RecoveryReceipt, A.RecoveryError> RestoreRecovery(A.DocumentToken expected, EntityGuid copyId) =>
        ChangeRecovery(expected, copyId, restoring: true);

    /// <summary>Removes only the reviewed pending recovery copy.</summary>
    public Outcome<A.RecoveryReceipt, A.RecoveryError> DiscardRecovery(A.DocumentToken expected, EntityGuid copyId) =>
        ChangeRecovery(expected, copyId, restoring: false);

    private Outcome<A.RecoveryReceipt, A.RecoveryError> ChangeRecovery(A.DocumentToken expected, EntityGuid copyId, bool restoring)
    {
        if (expected.DocumentId.IsNil || expected.DocumentRevision == 0 || copyId.IsNil)
            return Outcome<A.RecoveryReceipt, A.RecoveryError>.Err(A.RecoveryError.InvalidRequest);
        string method = restoring ? "authoring.restoreRecovery" : "authoring.discardRecovery";
        var parameters = JsonValue.NewObject().Set("expected", Codec.Encode(expected)).Set("copyId", Codec.Encode(copyId));
        _session.ThrowIfFailed();
        try
        {
            var reply = _session.AwaitResponse(_session.Send(method, parameters));
            if (Codec.RecoveryRpcRefusal(reply, method) is { } refusal)
                return Outcome<A.RecoveryReceipt, A.RecoveryError>.Err(refusal);
            var result = Codec.DocumentResult(reply);
            if (result?.Find("err") is { } err && Codec.DecodeRecoveryError(err) is { } failure)
                return Outcome<A.RecoveryReceipt, A.RecoveryError>.Err(failure);
            if (result?.Find("ok") is { } ok && Codec.DecodeRecoveryReceipt(ok, expected, copyId, restoring) is { } receipt)
                return Outcome<A.RecoveryReceipt, A.RecoveryError>.Ok(receipt);
            throw _session.Malformed(method);
        }
        catch (RemoteException error)
        {
            throw new RemoteException(error, $"{method} outcome unknown for copy {copyId}, " +
                $"document {expected.DocumentId}, revision {expected.DocumentRevision}. Check recovery and document state " +
                "on the original Runtime before retrying.");
        }
    }

    /// <summary>Creates a source copy without changing the bound document. A lost reply throws and must not be replayed.</summary>
    public Outcome<A.SavedScene, A.SaveAsError> SaveAs(string name)
    {
        const string method = "authoring.saveAs";
        if (!Codec.ValidSaveAsName(name)) return Outcome<A.SavedScene, A.SaveAsError>.Err(A.SaveAsError.InvalidName);
        var reply = _session.AwaitResponse(_session.Send(method, JsonValue.NewObject().Set("name", JsonValue.String(name))));
        var error = reply.Find("error");
        var result = reply.Find("result");
        if (Codec.IsLegacySaveAsRefusal(reply))
            return Outcome<A.SavedScene, A.SaveAsError>.Err(A.SaveAsError.Unsupported);
        if (error is not null || result is null || ((result.Find("ok") is null) == (result.Find("err") is null)))
            throw _session.Malformed(method);
        return _session.InBand<A.SavedScene, A.SaveAsError>(result, method,
            value => Codec.DecodeSavedScene(value) is { } copy && copy.Source == Codec.SaveAsSource(name) ? copy : null,
            Codec.DecodeSaveAsError);
    }

    /// <summary>Creates a camera-only source without changing the bound document.</summary>
    /// <exception cref="RemoteException">A missing or invalid reply reports an unknown creation outcome
    /// and its destination while retaining the transport or protocol error.</exception>
    public Outcome<A.CreatedScene, A.CreateSceneError> CreateScene(string name)
    {
        const string method = "authoring.createScene";
        if (!Codec.ValidSaveAsName(name)) return Outcome<A.CreatedScene, A.CreateSceneError>.Err(A.CreateSceneError.InvalidName);
        string source = Codec.SaveAsSource(name);
        _session.ThrowIfFailed(); // A previously failed session cannot send this request.
        try
        {
            var reply = _session.AwaitResponse(_session.Send(method, JsonValue.NewObject().Set("name", JsonValue.String(name))));
            var error = reply.Find("error");
            var result = reply.Find("result");
            if (Codec.IsLegacyCreateSceneRefusal(reply))
                return Outcome<A.CreatedScene, A.CreateSceneError>.Err(A.CreateSceneError.Unsupported);
            if (result is null && error?.Find("code") is { IsString: true, AsString: "NoAuthoring" })
                return Outcome<A.CreatedScene, A.CreateSceneError>.Err(A.CreateSceneError.NoAuthoring);
            if (error is not null || result is null || ((result.Find("ok") is null) == (result.Find("err") is null)))
                throw _session.Malformed(method);
            return _session.InBand<A.CreatedScene, A.CreateSceneError>(result, method,
                value => Codec.DecodeCreatedScene(value) is { } created && created.Source == source ? created : null,
                Codec.DecodeCreateSceneError);
        }
        catch (RemoteException e)
        {
            throw new RemoteException(e, $"Create Scene outcome unknown for {name} at {source}. " +
                "Inspect scene.scene.txt and catalog-source.txt and verify any existing unit. " +
                "Confirm the previous request can no longer commit before retrying.");
        }
    }

    public Outcome<A.PrefabRegistration, A.SaveAsError> CreatePrefab(string name, EntityGuid node, A.DocumentToken token)
    {
        const string method = "authoring.createPrefab";
        if (!Codec.ValidSaveAsName(name))
            return Outcome<A.PrefabRegistration, A.SaveAsError>.Err(A.SaveAsError.InvalidName);
        string source = Codec.PrefabSource(name);
        _session.ThrowIfFailed();
        try
        {
            var parameters = JsonValue.NewObject().Set("name", JsonValue.String(name))
                .Set("node", Codec.Encode(node)).Set("expected", Codec.Encode(token));
            var reply = _session.AwaitResponse(_session.Send(method, parameters));
            var error = reply.Find("error");
            var result = reply.Find("result");
            if (result is null && error?.Find("code") is { IsString: true, AsString: "NoAuthoring" })
                return Outcome<A.PrefabRegistration, A.SaveAsError>.Err(A.SaveAsError.NoDocument);
            if (result is null && error?.Find("code") is { IsString: true, AsString: "BadRequest" } &&
                error.Find("message") is { IsString: true, AsString: "unknown method authoring.createPrefab" })
                return Outcome<A.PrefabRegistration, A.SaveAsError>.Err(A.SaveAsError.Unsupported);
            if (error is not null || result is not { IsObject: true } || result.Members.Count != 1)
                throw _session.Malformed(method);
            return _session.InBand<A.PrefabRegistration, A.SaveAsError>(result, method,
                value => Codec.DecodePrefabRegistration(value) is { } receipt && receipt.Source == source &&
                    receipt.DocumentRevision == token.DocumentRevision ? receipt : null, Codec.DecodeSaveAsError);
        }
        catch (RemoteException e)
        {
            throw new RemoteException(e, $"Prefab registration outcome unknown for {name} at {source}. " +
                "Inspect source.scene.txt and catalog-source.txt. Confirm the previous request has settled before retrying.");
        }
    }

    public Outcome<A.PrefabUpdate, A.SaveAsError> UpdatePrefab(AssetGuid prefab, string revision,
        EntityGuid donor, A.DocumentToken token)
    {
        if (!Codec.ValidPrefabDigest(revision))
            return Outcome<A.PrefabUpdate, A.SaveAsError>.Err(A.SaveAsError.StaleSource);
        const string method = "authoring.updatePrefab";
        var parameters = PrefabParameters(donor, token).Set("prefab", Codec.Encode(prefab))
            .Set("revision", JsonValue.String(revision));
        string context = $"source update for Prefab {prefab} at revision {revision}. Check the registration's " +
            "source.scene.txt and catalog-source.txt after the request ends before retrying.";
        return SendPrefab(method, parameters, context, A.SaveAsError.Unsupported, A.SaveAsError.NoDocument,
            Codec.DecodeSaveAsError, value =>
            {
                var receipt = Codec.DecodePrefabUpdate(value);
                return receipt is not null && receipt.Registration.Prefab == prefab &&
                    receipt.Registration.DocumentRevision == token.DocumentRevision &&
                    receipt.PreviousRevision == revision ? receipt : null;
            });
    }

    public Outcome<A.PrefabEdit, A.PrefabEditError> SyncPrefab(EntityGuid root, string revision,
        A.DocumentToken token, AssetGuid? instantiate = null)
    {
        if (!Codec.ValidPrefabDigest(revision))
            return Outcome<A.PrefabEdit, A.PrefabEditError>.Err(A.PrefabEditError.StaleSource);
        var parameters = PrefabParameters(root, token).Set("revision", JsonValue.String(revision));
        if (instantiate is AssetGuid source) parameters.Set("prefab", Codec.Encode(source));
        return SendPrefabEdit("authoring.syncPrefab", parameters, root, token, $"at source revision {revision}");
    }

    public Outcome<A.PrefabEdit, A.PrefabEditError> ReplayPrefab(EntityGuid root, A.PrefabSubtree before,
        A.PrefabSubtree after, A.DocumentToken token)
    {
        var parameters = PrefabParameters(root, token).Set("before", Codec.Encode(before)).Set("after", Codec.Encode(after));
        return SendPrefabEdit("authoring.replayPrefab", parameters, root, token, "during history replay",
            receipt => receipt.Before.Matches(before) && receipt.After.Matches(after));
    }

    private static JsonValue PrefabParameters(EntityGuid node, A.DocumentToken token) => JsonValue.NewObject()
        .Set("node", Codec.Encode(node)).Set("expected", Codec.Encode(token));

    private Outcome<A.PrefabEdit, A.PrefabEditError> SendPrefabEdit(string method, JsonValue parameters, EntityGuid root,
        A.DocumentToken token, string operation, Func<A.PrefabEdit, bool>? verify = null) =>
        SendPrefab(method, parameters, $"Prefab instance {root} {operation}. Inspect the document before retrying.",
            A.PrefabEditError.Unsupported, A.PrefabEditError.NoDocument, Codec.DecodePrefabEditError, value =>
            {
                var edit = Codec.DecodePrefabEdit(value);
                return edit is not null && edit.Document.DocumentId == token.DocumentId &&
                    edit.Document.DocumentRevision >= token.DocumentRevision &&
                    edit.Document.DocumentRevision - token.DocumentRevision == (edit.Changed ? 1UL : 0UL) &&
                    (verify is null || verify(edit)) ? edit : null;
            });

    private Outcome<T, E> SendPrefab<T, E>(string method, JsonValue parameters, string context, E unsupported, E noDocument,
        Func<JsonValue, E?> decodeError, Func<JsonValue, T?> decode) where T : class where E : struct, Enum
    {
        _session.ThrowIfFailed();
        try
        {
            var reply = _session.AwaitResponse(_session.Send(method, parameters));
            var error = reply.Find("error");
            var result = reply.Find("result");
            if (result is null && error?.Find("code") is { IsString: true, AsString: "NoAuthoring" })
                return Outcome<T, E>.Err(noDocument);
            if (result is null && error?.Find("code") is { IsString: true, AsString: "BadRequest" } &&
                error.Find("message") is { IsString: true } message && message.AsString == $"unknown method {method}")
                return Outcome<T, E>.Err(unsupported);
            if (error is not null || result is not { IsObject: true } || result.Members.Count != 1)
                throw _session.Malformed(method);
            return _session.InBand<T, E>(result, method, decode, decodeError);
        }
        catch (RemoteException e)
        {
            throw new RemoteException(e, "Result unknown for " + context);
        }
    }

    // --- Queries.

    /// <summary>Every node, in the document's order.</summary>
    public Outcome<IReadOnlyList<A.NodeSummary>, A.AuthoringError> ListNodes() =>
        _session.InBand<IReadOnlyList<A.NodeSummary>, A.AuthoringError>(_session.Call("authoring.listNodes"), "authoring.listNodes",
            ok => Values(ok, Codec.DecodeNodeSummary), Codec.DecodeAuthoringError);

    /// <summary>A node's component types, sorted.</summary>
    public Outcome<IReadOnlyList<TypeId>, A.AuthoringError> ListComponents(EntityGuid node) =>
        _session.InBand<IReadOnlyList<TypeId>, A.AuthoringError>(
            _session.Call("authoring.listComponents", JsonValue.NewObject().Set("node", Codec.Encode(node))),
            "authoring.listComponents", ok => Values(ok, Codec.DecodeTypeId), Codec.DecodeAuthoringError);

    public Outcome<PropertyValue, A.AuthoringError> GetProperty(A.SceneAddress address)
    {
        Outcome<PropertyValue?, A.AuthoringError> decoded = _session.InBand<PropertyValue?, A.AuthoringError>(
            _session.Call("authoring.getProperty", JsonValue.NewObject().Set("address", Codec.Encode(address))),
            "authoring.getProperty", Codec.DecodePropertyValue, Codec.DecodeAuthoringError);
        return decoded.IsOk ? Outcome<PropertyValue, A.AuthoringError>.Ok(decoded.Value!.Value)
                            : Outcome<PropertyValue, A.AuthoringError>.Err(decoded.Error);
    }

    /// <summary>A property by path ("Light.intensity"); throws <see cref="UnknownNameException"/> if it does not resolve.</summary>
    public Outcome<PropertyValue, A.AuthoringError> GetProperty(EntityGuid node, string path) => GetProperty(Address(node, path));

    /// <summary>The active camera's node; null when the document has none.</summary>
    public Outcome<EntityGuid?, A.AuthoringError> ActiveCamera()
    {
        JsonValue result = _session.Call("authoring.activeCamera");
        if (result.Find("ok") is JsonValue ok)
        {
            if (ok.IsNull) return Outcome<EntityGuid?, A.AuthoringError>.Ok(null);
            return Codec.DecodeEntity(ok) is EntityGuid camera ? Outcome<EntityGuid?, A.AuthoringError>.Ok(camera)
                                                              : throw _session.Malformed("authoring.activeCamera");
        }
        if (result.Find("err") is JsonValue err && Codec.DecodeAuthoringError(err) is A.AuthoringError error)
        {
            return Outcome<EntityGuid?, A.AuthoringError>.Err(error);
        }
        throw _session.Malformed("authoring.activeCamera");
    }

    /// <summary>The scene's component types (section 6.7), fetched once.</summary>
    public IReadOnlyList<SchemaType> Schema =>
        _schema ??= Codec.DecodeSchema(_session.Call("authoring.schema")) ?? throw _session.Malformed("authoring.schema");

    // --- Names, through the authoring schema (Q8: reflective).

    /// <summary>A scene component type by short or qualified name ("Light"); throws <see cref="UnknownNameException"/>.</summary>
    public TypeId Type(string name)
    {
        SchemaType? type = SchemaText.FindType(Schema, name);
        return type is { Kind: TypeKind.Struct } ? type.Id : throw new UnknownNameException(NameError.UnknownType, name);
    }

    /// <summary>A node's property by path ("Light.intensity", "Camera.fog.density"); throws <see cref="UnknownNameException"/>.</summary>
    public A.SceneAddress Address(EntityGuid node, string path) =>
        SchemaText.TryParsePath(Schema, path, out TypeId component, out SchemaField? leaf, out NameError error)
            ? new A.SceneAddress(node, component, leaf!.Id)
            : throw new UnknownNameException(error, path);

    // --- Commands: applied when submitted, refusals reported by ticket.

    public CommandTicket Submit(A.Command command)
    {
        if (command is A.SwapAdjacentSiblings or A.MoveSiblingRelative or A.RequireDocumentRevision or A.RequireTransformBatchRevision
            or A.RequireDuplicateBatchRevision or A.RequireNameBatchDocument or A.RequireRenderLookDocument or A.SetPrefabOrigin)
            return SubmitTransaction(new[] { command }).First;
        JsonValue result = _session.Call("authoring.submit", JsonValue.NewObject().Set("command", Codec.Encode(command)));
        return Codec.DecodeUInt64(result) is ulong ticket ? new CommandTicket(ticket) : throw _session.Malformed("authoring.submit");
    }

    /// <summary>All or nothing: every command applies, or none does and one failure names the transaction.</summary>
    public TransactionTicket SubmitTransaction(IReadOnlyList<A.Command> commands)
    {
        bool property = commands.Any(command => command is A.RequireDocumentRevision);
        bool transform = commands.Any(command => command is A.RequireTransformBatchRevision);
        bool duplicate = commands.Any(command => command is A.RequireDuplicateBatchRevision);
        bool prefab = commands.Any(command => command is A.SetPrefabOrigin);
        bool names = commands.Any(command => command is A.RequireNameBatchDocument);
        bool looks = commands.Any(command => command is A.RequireRenderLookDocument);
        if (property || transform || duplicate || prefab || names || looks)
        {
            A.DocumentInfo? info = Document();
            if (looks && info?.SupportsGuardedRenderLookTransactions != true)
                throw new NotSupportedException("This open document does not support guarded render look transactions.");
            if (names && info?.SupportsGuardedNameTransactions != true)
                throw new NotSupportedException("This open document does not support guarded name transactions.");
            if (duplicate && info?.SupportsGuardedDuplicateTransactions != true)
                throw new NotSupportedException("This open document does not support guarded duplicate transactions.");
            if (prefab && info?.SupportsPrefabOrigin != true)
                throw new NotSupportedException("This open document does not support Prefab provenance.");
            if (transform && info?.SupportsGuardedTransformTransactions != true)
                throw new NotSupportedException("This open document does not support guarded Transform transactions.");
            if (property && info?.SupportsGuardedPropertyTransactions != true)
                throw new NotSupportedException("This open document does not support guarded property transactions.");
        }
        JsonValue list = JsonValue.NewArray();
        foreach (A.Command command in commands)
        {
            RequireSiblingOrderIfNeeded(command);
            list.Push(Codec.Encode(command));
        }
        JsonValue result = _session.Call("authoring.submitTransaction", JsonValue.NewObject().Set("commands", list));
        return Codec.DecodeTransactionTicket(result) ?? throw _session.Malformed("authoring.submitTransaction");
    }

    /// <summary>Refusals of this session's submissions since the last drain.</summary>
    public IReadOnlyList<A.CommandFailure> DrainFailures()
    {
        JsonValue result = _session.Call("authoring.drainFailures");
        return Values(result, Codec.DecodeAuthoringFailure) ?? throw _session.Malformed("authoring.drainFailures");
    }

    // --- Events: every client's changes, and every Applied.

    private void RequireSiblingOrderIfNeeded(A.Command command)
    {
        if (command is A.SwapAdjacentSiblings && !SupportsSiblingOrder())
            throw new NotSupportedException("This Runtime does not support adjacent sibling swaps.");
        if (command is A.MoveSiblingRelative && !SupportsSiblingMove())
            throw new NotSupportedException("This Runtime does not support relative sibling moves.");
    }

    /// <summary>Checks once per connection using the full unfiltered subscription.</summary>
    public bool SupportsSiblingOrder() => ProbeSiblingOrder() >= 1;

    public bool SupportsSiblingMove() => ProbeSiblingOrder() == 2;

    private int ProbeSiblingOrder()
    {
        if (_siblingOrderTier is null)
        {
            SubscriptionId probe = Subscribe(new A.EventFilter(A.EventKind.AllWithSiblingMove));
            if (Unsubscribe(probe) is not null) throw _session.Malformed("authoring.unsubscribe");
        }
        return _siblingOrderTier ?? 0;
    }

    public SubscriptionId Subscribe(A.EventFilter filter)
    {
        const string method = "authoring.subscribe";
        static JsonValue Parameters(A.EventFilter value) => JsonValue.NewObject().Set("filter", Codec.Encode(value));
        static A.EventFilter TierFilter(int tier) => new(tier switch
        {
            2 => A.EventKind.AllWithSiblingMove,
            1 => A.EventKind.AllWithSiblingOrder,
            _ => A.EventKind.All
        });
        int requested = filter == TierFilter(2) ? 2 : filter == TierFilter(1) ? 1 : 0;
        if (requested != 0 && _siblingOrderTier is null)
        {
            for (int tier = requested; tier >= 0; --tier)
            {
                JsonValue response = _session.AwaitResponse(_session.Send(method, Parameters(TierFilter(tier))));
                if (response.Find("error") is null && response.Find("result") is JsonValue positive &&
                    Codec.DecodeUInt64(positive) is ulong supportedId && supportedId != 0)
                {
                    if (requested == 2 || tier == 0) _siblingOrderTier = tier;
                    return new SubscriptionId(supportedId);
                }
                if (tier == 0 || !Codec.IsLegacySiblingOrderRefusal(response)) break;
            }
            throw _session.Malformed(method);
        }
        if (requested != 0 && _siblingOrderTier is int supported) filter = TierFilter(Math.Min(requested, supported));
        JsonValue result = _session.Call(method, Parameters(filter));
        return Codec.DecodeUInt64(result) is ulong id && id != 0 ? new SubscriptionId(id) : throw _session.Malformed(method);
    }

    /// <summary>Null when released; UnknownSubscription otherwise.</summary>
    public ConnectionError? Unsubscribe(SubscriptionId subscription)
    {
        JsonValue result = _session.Call("authoring.unsubscribe", SubscriptionParams(subscription));
        if (result.Find("ok") is JsonValue ok) return ok.IsNull ? null : throw _session.Malformed("authoring.unsubscribe");
        if (result.Find("err") is JsonValue err)
        {
            return Codec.DecodeConnectionError(err) ?? throw _session.Malformed("authoring.unsubscribe");
        }
        throw _session.Malformed("authoring.unsubscribe");
    }

    /// <summary>The matching events since the last drain, in the order applied.</summary>
    public Outcome<IReadOnlyList<A.Event>, ConnectionError> DrainEvents(SubscriptionId subscription) =>
        _session.InBand<IReadOnlyList<A.Event>, ConnectionError>(
            _session.Call("authoring.drainEvents", SubscriptionParams(subscription)), "authoring.drainEvents",
            ok => Records(ok, Codec.DecodeAuthoringEvent), Codec.DecodeConnectionError);

    private static JsonValue SubscriptionParams(SubscriptionId subscription) =>
        JsonValue.NewObject().Set("subscription", JsonValue.Number(subscription.Value));

    // {"ok":null} -> null; {"err":"<name>"} -> the error.
    private TError? Unit<TError>(string method, Func<JsonValue, TError?> decodeErr) where TError : struct, Enum
    {
        JsonValue result = _session.Call(method);
        if (result.Find("ok") is JsonValue ok) return ok.IsNull ? null : throw _session.Malformed(method);
        if (result.Find("err") is JsonValue err) return decodeErr(err) ?? throw _session.Malformed(method);
        throw _session.Malformed(method);
    }

    private static List<T>? Values<T>(JsonValue value, Func<JsonValue, T?> decode) where T : struct
    {
        if (!value.IsArray) return null;
        var items = new List<T>(value.Items.Count);
        foreach (JsonValue item in value.Items)
        {
            if (decode(item) is not T decoded) return null;
            items.Add(decoded);
        }
        return items;
    }

    private static List<T>? Records<T>(JsonValue value, Func<JsonValue, T?> decode) where T : class
    {
        if (!value.IsArray) return null;
        var items = new List<T>(value.Items.Count);
        foreach (JsonValue item in value.Items)
        {
            if (decode(item) is not T decoded) return null;
            items.Add(decoded);
        }
        return items;
    }
}
