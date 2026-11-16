using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using A = Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public enum DocumentAvailability { Starting, Known, Legacy, Unavailable }

public sealed record DocumentObservation(DocumentAvailability Availability, DocumentState? State = null,
    string? Diagnostic = null)
{
    public bool Clean => Availability == DocumentAvailability.Known && State?.Dirty != true;
    public bool Writable => State is { Provenance: Provenance.HandAuthored };

    public bool Matches(DocumentObservation reviewed) =>
        Availability == reviewed.Availability && State?.Token == reviewed.State?.Token;

    public bool ProvesSaved(DocumentToken token) =>
        State is { Dirty: false } current && current.Token == token &&
        current.LastPersistedRevision == token.DocumentRevision;

    public string Title => Availability switch
    {
        DocumentAvailability.Known when State is null => "No document",
        DocumentAvailability.Known when State!.Dirty => "Document has unsaved changes",
        DocumentAvailability.Known => "Document matches source",
        DocumentAvailability.Starting => "Document is opening",
        _ => "Document persistence unknown"
    };
}

public sealed record DocumentMutation(DocumentState? State = null, DocumentError? Refusal = null,
    string? Diagnostic = null, bool Unknown = false);

public sealed record DocumentDraft(string Key, string Text, IReadOnlyList<EntityGuid> Targets, TypeId? Component, FieldId? Field);

public sealed class DocumentDraftProtection
{
    // Copy-only text outlives the Inspector control rebuilt by the external edit.
    public DocumentDraft? Stale { get; private set; }

    public bool Observe(DocumentDraft draft, IReadOnlyList<A.Event> events)
    {
        if (!events.Any(e => e switch
        {
            A.DocumentReopened => true,
            A.NodeDestroyed gone => draft.Targets.Contains(gone.Node),
            A.NodeNameChanged name => draft.Key == "Name" && draft.Targets.Contains(name.Node),
            A.ComponentRemoved removed => draft.Component == removed.Component && draft.Targets.Contains(removed.Node),
            A.PropertyChanged property => draft.Component == property.Address.Component &&
                draft.Field == property.Address.Field && draft.Targets.Contains(property.Address.Node),
            _ => false
        })) return false;

        Stale = draft;
        return true;
    }

    public bool RequiresConfirmation(DocumentObservation document, bool hasEditableDraft) =>
        !document.Clean || hasEditableDraft || Stale is not null;

    public void ConfirmLeave() => Stale = null;
}

public enum LeaveChoice { Cancel, Save, Discard, CloseAnyway, Check }
