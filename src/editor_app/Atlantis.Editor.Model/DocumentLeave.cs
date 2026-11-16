using Atlantis.Gameplay.Authoring;

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

public enum LeaveChoice { Cancel, Save, Discard, CloseAnyway, Check }
