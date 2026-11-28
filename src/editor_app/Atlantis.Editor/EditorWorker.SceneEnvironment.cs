using System;
using System.IO;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor;

public sealed partial class EditorWorker
{
    internal sealed record SceneEnvironmentReview(DocumentState Document, EffectiveSceneEnvironment Effective,
        AssetSummary[] Choices, long Lifetime);

    internal void RequestSceneEnvironment(Action<SceneEnvironmentReview?, string> done) => Enqueue(session =>
    {
        SceneEnvironmentReview? review = null;
        string notice;
        try
        {
            var status = session.Control.Status();
            if (status.SceneEnvironment is not { } effective)
                notice = "This Runtime does not support scene environment editing.";
            else
            {
                var document = session.Authoring.DocumentStateWithEnvironment();
                var catalog = session.Assets.ListAssets();
                var latest = session.Control.Status();
                if (!document.IsOk || document.Value is not { Environment: not null } state)
                    notice = "Open an authoring document to view its environment.";
                else if (!catalog.IsOk)
                    notice = "The Runtime asset catalog is unavailable.";
                else if (latest.SceneGeneration != status.SceneGeneration || latest.SceneEnvironment != effective)
                    notice = "The Runtime projection changed. Refresh to view the settled environment.";
                else
                {
                    var choices = catalog.Value!.Where(asset => asset.Type == AssetType.Environment)
                        .OrderBy(asset => asset.DisplayName ?? asset.Source, StringComparer.OrdinalIgnoreCase)
                        .ThenBy(asset => asset.Guid.ToString(), StringComparer.Ordinal).ToArray();
                    review = new(state, effective, choices, MeshDocumentLifetime);
                    notice = state.Provenance == Provenance.HandAuthored ? "" : "This imported scene is read-only.";
                }
            }
        }
        catch (Exception error) when (error is GameplayException or IOException)
        {
            notice = error.Message;
        }
        _post(() =>
        {
            if (review is not null && review.Lifetime != MeshDocumentLifetime)
                done(null, "The document was replaced. Refresh to continue.");
            else done(review, notice);
        });
    });

    internal void ChooseSceneEnvironment(SceneEnvironmentReview review, SceneEnvironmentChoice choice,
        Func<bool> intent, Action<string> done) => Enqueue(session =>
    {
        string notice = "The scene environment choice is stale. Refresh before retrying.";
        if (review.Document.Environment is { } before && before != choice &&
            (_history, _historyTarget) is ({ } history, { } target))
        {
            var edit = new SceneEnvironmentEdit(review.Document.Token.DocumentId, before, choice);
            var result = SettleSceneEnvironment(session, edit, true, review.Lifetime, review.Document.Token, intent);
            notice = result.Settlement.Notice;
            if (result.Revision is ulong revision)
            {
                history.Record(new EditTransaction("Scene environment", [], [edit]), target.Committed([]));
                history.Update();
                PostHistory(review.Lifetime);
                notice = ApplyVerifiedSceneObject(session, review.Lifetime, revision, notice);
                PostApplyState(review.Lifetime);
                PollDocument(session);
            }
        }
        _post(() =>
        {
            if (review.Lifetime != MeshDocumentLifetime) return;
            EditNotice?.Invoke(notice);
            done(notice);
        });
    });

    private (BatchReplayResult Settlement, ulong? Revision) SettleSceneEnvironment(RemoteSession session,
        SceneEnvironmentEdit edit, bool forward, long lifetime, DocumentToken? reviewed = null, Func<bool>? intent = null)
    {
        bool Current() => !_stopping && lifetime == MeshDocumentLifetime && !_dragActive && !_gizmoActive &&
            !_batchSettling && !_postProcessingWriting && (intent?.Invoke() ?? true);
        (BatchReplayResult, ulong?) Refuse(string message, BatchReplayState state = BatchReplayState.Retry) =>
            (new BatchReplayResult(state, message), null);
        bool sent = false;
        var wanted = forward ? edit.After : edit.Before;
        try
        {
            if (!Current() || session.Control.Status().SceneEnvironment is null)
                return Refuse("The scene environment command is unavailable.");
            var state = session.Authoring.DocumentStateWithEnvironment();
            if (!state.IsOk || state.Value is not { Provenance: Provenance.HandAuthored } document ||
                document.Token.DocumentId != edit.Document || (reviewed is { } token && token != document.Token))
                return Refuse("The document changed. Refresh the environment choice.");
            if (document.Environment != (forward ? edit.Before : edit.After))
                return Refuse("The scene environment changed elsewhere.", BatchReplayState.Drop);
            if (wanted.Asset is { } asset)
            {
                var candidate = session.Assets.GetAsset(asset);
                if (!candidate.IsOk || candidate.Value is not { Type: AssetType.Environment })
                    return Refuse("The chosen environment is no longer in this Runtime catalog.");
            }
            var command = new SetSceneEnvironment(document.Token, wanted);
            var parameters = JsonValue.NewObject().Set("commands", JsonValue.NewArray().Push(Codec.Encode(command)));
            ulong request;
            lock (_lock)
            {
                if (!Current()) return Refuse("The environment change was canceled or became stale.");
                sent = true;
                request = session.Send("authoring.submitTransaction", parameters);
            }
            var ticket = Codec.DecodeTransactionTicket(session.AwaitResult(request));
            if (ticket is not { First.Value: > 0, Count: 1 })
                throw new RemoteException(RemoteError.ProtocolError, "Malformed scene environment transaction receipt.");
            var failures = session.Authoring.DrainFailures();
            var refusal = failures.FirstOrDefault(item => item.Ticket == ticket.Value.First);
            if (refusal is { Ticket.Value: > 0 }) return Refuse("Environment refused. " + refusal.Error);
            var settled = session.Authoring.DocumentStateWithEnvironment();
            if (!settled.IsOk || settled.Value is not { } after || after.Token.DocumentId != edit.Document ||
                document.Token.DocumentRevision == ulong.MaxValue || after.Token.DocumentRevision != document.Token.DocumentRevision + 1 ||
                after.Environment != wanted || lifetime != MeshDocumentLifetime)
                throw new RemoteException(RemoteError.ProtocolError, "The environment revision could not be verified.");
            return (new BatchReplayResult(BatchReplayState.Committed, "Scene environment committed."), after.Token.DocumentRevision);
        }
        catch (Exception error) when (error is GameplayException or IOException)
        {
            if (!sent) return Refuse(error.Message);
            if (lifetime == MeshDocumentLifetime)
            {
                _apply?.RequireExplicitApply("the scene environment transaction could not be verified");
                if (reviewed.HasValue) _history?.DiscardRedo();
            }
            return Refuse($"Environment outcome unknown for {wanted.Asset?.ToString() ?? "Inherit"}. " +
                "Check the document and Apply status before retrying. " + error.Message, BatchReplayState.Drop);
        }
    }
}
