using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public sealed record PostProcessingField(string Group, InspectorField Field, BatchPropertyTarget Target);

public sealed record PostProcessingView(long Epoch, long Lifetime, DocumentToken? Document, EntityGuid Camera,
    string Name, bool Writable, IReadOnlyList<PostProcessingField> Fields)
{
    public string Label => string.IsNullOrEmpty(Name) ? "Unnamed camera" : Name;
}

public sealed record PostProcessingDraft(PostProcessingView View, PostProcessingField Input, string Text)
{
    public DocumentDraft LeaveDraft => new(Input.Field.Path, Text, [View.Camera], Input.Field.Component, Input.Field.Field.Id);

    public bool CanSubmit(PostProcessingView? current) => ReferenceEquals(View, current) && View.Writable &&
        View.Document is not null && Input.Field.Editable && Input.Field.Present && View.Fields.Contains(Input);
}

public static class PostProcessingPanel
{
    public static string? GroupOf(string path) => path switch
    {
        "Camera.exposureCompensationEv" => "Exposure",
        "Camera.fog.color" or "Camera.fog.density" or "Camera.fog.height" or "Camera.fog.heightFalloff" or
            "Camera.fog.maxOpacity" => "Fog",
        "Camera.bloom.strength" or "Camera.bloom.threshold" => "Bloom",
        _ => null
    };

    public static IReadOnlyList<PostProcessingField> Read(IAuthoringDocument document, DocumentTree tree, EntityGuid camera)
    {
        if (SceneSchema.TypeOf(document.Schema, "Camera") is not TypeId component || !tree.ComponentsOf(camera).Contains(component))
            return Array.Empty<PostProcessingField>();
        return InspectorModel.ReadFields(document, camera, component, path => GroupOf(path) is not null).Select(field =>
        {
            var address = new SceneAddress(camera, component, field.Field.Id);
            return new PostProcessingField(GroupOf(field.Path)!, field,
                new BatchPropertyTarget(address, field.Value, tree.IncarnationOf(camera),
                    tree.ComponentIncarnationOf(address), tree.PropertyStamp(address)));
        }).ToArray();
    }
}
