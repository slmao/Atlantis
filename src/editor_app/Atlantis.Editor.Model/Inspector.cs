// Plan 0068 M3 (Spec 0068 R4, Q5; ADR-0135 D2): the Inspector's model -- the
// selected node's components and every field of each, reflected from the
// authoring schema (authoring.schema; nested structs expanded in place, as
// E2's InspectorModel expands the World's), one typed editor kind per field
// (E2's FieldControl set: integer, float, float3, float4, GUID, enum), an
// Optional field shown absent or present. A commit is one authoring
// transaction (one SetProperty) and then an apply; a refusal shows its reason
// and the field reverts to the document's value; values other clients change
// arrive through events and refresh the panel. Add and remove component are
// commands on the same panel. A new C# model with no C++ reference (ADR-0137
// D2): E2's inspector reads the World's schema. UI-free.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public enum FieldControl
{
    Integer,
    Float,
    Float3,
    Float4,
    Guid,
    Enum,
}

/// <summary>One field of one component of the subject: its address, how to edit it and its current value.</summary>
public sealed record InspectorField(TypeId Component, string ComponentName, string Path, string Label, SchemaField Field,
                                    FieldControl Control, bool Editable, bool Optional, IReadOnlyList<SchemaConstant> Choices,
                                    PropertyValue? Value)
{
    /// <summary>The value as the field's editor shows it ("" for an absent optional).</summary>
    public string Text => Value is PropertyValue v ? InspectorModel.Format(this, v) : "";

    public bool Present => Value is PropertyValue v && !v.IsAbsent;
}

public sealed class InspectorModel
{
    private readonly IAuthoringDocument _document;
    private EntityGuid? _subject;
    private List<InspectorField> _fields = new();
    private List<TypeId> _components = new();

    public InspectorModel(IAuthoringDocument document) => _document = document;

    public EntityGuid? Subject => _subject;

    public IReadOnlyList<InspectorField> Fields => _fields;

    public IReadOnlyList<TypeId> Components => _components;

    /// <summary>The scene component types the subject does not have yet: the schema's structs no other struct embeds.</summary>
    public IReadOnlyList<SchemaType> Addable()
    {
        IReadOnlyList<SchemaType> schema = _document.Schema;
        var nested = new HashSet<TypeId>(schema.SelectMany(t => t.Fields).Where(f => f.Kind == TypeKind.Struct).Select(f => f.Type));
        return schema.Where(t => t.Kind == TypeKind.Struct && !nested.Contains(t.Id) && !_components.Contains(t.Id))
                     .OrderBy(t => t.ShortName, StringComparer.Ordinal).ToList();
    }

    /// <summary>Re-reads the subject's components and every field (on selection, and when events say it changed).</summary>
    public void Refresh(EntityGuid? subject)
    {
        _subject = subject;
        _fields = new List<InspectorField>();
        _components = new List<TypeId>();
        if (subject is not EntityGuid node) return;
        IReadOnlyList<SchemaType> schema = _document.Schema;
        _components = _document.ListComponents(node).ToList();
        foreach (TypeId component in _components.OrderBy(c => SceneSchema.NameOf(schema, c) == "Transform" ? 0 : 1)
                                                .ThenBy(c => SceneSchema.NameOf(schema, c), StringComparer.Ordinal))
        {
            string componentName = SceneSchema.NameOf(schema, component);
            foreach (LeafPath leaf in SchemaText.LeavesOf(schema, component))
            {
                SchemaField field = leaf.Leaf;
                var address = new SceneAddress(node, component, field.Id);
                IReadOnlyList<SchemaConstant> choices = field.Kind == TypeKind.Enum
                    ? SchemaText.FindType(schema, field.Type)?.Constants ?? Array.Empty<SchemaConstant>()
                    : Array.Empty<SchemaConstant>();
                _fields.Add(new InspectorField(component, componentName, leaf.Path, leaf.Path[(componentName.Length + 1)..], field,
                                               ControlOf(field), IsEditable(field), field.IsOptional, choices, _document.GetProperty(address)));
            }
        }
    }

    /// <summary>The field at <paramref name="path"/> ("Light.intensity"), or null.</summary>
    public InspectorField? Find(string path) => _fields.FirstOrDefault(f => f.Path == path);

    /// <summary>
    /// A commit as one authoring transaction -- one SetProperty -- or its refusal (a read-only field, text that is not a
    /// value of the field's kind, a value equal to the current one).
    /// </summary>
    public EditPlan Commit(string path, string text)
    {
        string label = "edit " + path;
        if (_subject is not EntityGuid node || Find(path) is not InspectorField field) return EditPlan.Refused(label, "no such field");
        if (!field.Editable) return EditPlan.Refused(label, "the field is read-only");
        if (!TryParse(field, text, out PropertyValue value, out string error)) return EditPlan.Refused(label, error);
        if (field.Value is PropertyValue current && current.Equals(value)) return EditPlan.Refused(label, "unchanged");
        return new EditPlan(label, new Command[] { new SetProperty(new SceneAddress(node, field.Component, field.Field.Id), value) });
    }

    /// <summary>An optional field's absent state: one SetProperty of Absent, or its refusal.</summary>
    public EditPlan Clear(string path)
    {
        string label = "clear " + path;
        if (_subject is not EntityGuid node || Find(path) is not InspectorField field) return EditPlan.Refused(label, "no such field");
        if (!field.Optional) return EditPlan.Refused(label, "the field is not optional");
        if (!field.Present) return EditPlan.Refused(label, "unchanged");
        return new EditPlan(label, new Command[] { new SetProperty(new SceneAddress(node, field.Component, field.Field.Id), PropertyValue.Absent) });
    }

    public EditPlan AddComponent(TypeId component)
    {
        string name = SceneSchema.NameOf(_document.Schema, component);
        if (_subject is not EntityGuid node) return EditPlan.Refused("add " + name, "nothing is selected");
        return _components.Contains(component)
            ? EditPlan.Refused("add " + name, "the node has it")
            : new EditPlan("add " + name, new Command[] { new AddComponent(node, component) });
    }

    public EditPlan RemoveComponent(TypeId component)
    {
        string name = SceneSchema.NameOf(_document.Schema, component);
        if (_subject is not EntityGuid node) return EditPlan.Refused("remove " + name, "nothing is selected");
        if (name == "Transform") return EditPlan.Refused("remove " + name, "the Transform is required");
        return !_components.Contains(component)
            ? EditPlan.Refused("remove " + name, "the node does not have it")
            : new EditPlan("remove " + name, new Command[] { new RemoveComponent(node, component) });
    }

    /// <summary>
    /// The document's edit surface is the schema's serializable fields (what the scene grammar round-trips): the
    /// authoring schema never sets Editable, which means the live World's edit surface (schema.h). A field with
    /// neither flag is shown read-only.
    /// </summary>
    public static bool IsEditable(SchemaField field) => field.IsEditable || (field.Flags & FieldFlags.Serializable) != 0;

    public static FieldControl ControlOf(SchemaField field) => field.Kind == TypeKind.Enum ? FieldControl.Enum : field.Primitive switch
    {
        PrimitiveKind.UInt64 => FieldControl.Integer,
        PrimitiveKind.Float32 => FieldControl.Float,
        PrimitiveKind.Vec3Float32 => FieldControl.Float3,
        PrimitiveKind.Vec4Float32 => FieldControl.Float4,
        _ => FieldControl.Guid,
    };

    /// <summary>A value as its editor shows it: floats in the shortest round-trip form, vectors space separated, enums by name.</summary>
    public static string Format(InspectorField field, PropertyValue value)
    {
        if (value.IsAbsent) return "";
        return field.Control switch
        {
            FieldControl.Integer => value.TryGetUInt64(out ulong u) ? u.ToString(CultureInfo.InvariantCulture) : "",
            FieldControl.Float => value.TryGetFloat32(out float f) ? Float(f) : "",
            FieldControl.Float3 => value.TryGetVec3(out Vector3 v) ? string.Join(" ", Float(v.X), Float(v.Y), Float(v.Z)) : "",
            FieldControl.Float4 => value.TryGetVec4(out Vector4 v) ? string.Join(" ", Float(v.X), Float(v.Y), Float(v.Z), Float(v.W)) : "",
            FieldControl.Enum => value.TryGetEnum(out long e) ? field.Choices.FirstOrDefault(c => c.Value == e)?.Name ?? e.ToString(CultureInfo.InvariantCulture) : "",
            _ => value.TryGetAsset(out AssetGuid a) ? a.ToString() : value.TryGetEntity(out EntityGuid g) ? g.ToString() : "",
        };
    }

    /// <summary>Text to a value of the field's kind; an error text when it is not one.</summary>
    public static bool TryParse(InspectorField field, string text, out PropertyValue value, out string error)
    {
        value = default;
        error = "";
        text = text.Trim();
        if (text.Length == 0 && field.Optional)
        {
            value = PropertyValue.Absent;
            return true;
        }
        float[]? Floats(int count)
        {
            string[] parts = text.Split(new[] { ' ', ',', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length != count) return null;
            var numbers = new float[count];
            for (int i = 0; i < count; ++i)
            {
                if (!float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out numbers[i]) || !float.IsFinite(numbers[i])) return null;
            }
            return numbers;
        }
        switch (field.Control)
        {
            case FieldControl.Integer:
                if (ulong.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out ulong u)) { value = PropertyValue.FromUInt64(u); return true; }
                error = "not a whole number";
                return false;
            case FieldControl.Float:
                if (Floats(1) is float[] one) { value = PropertyValue.FromFloat32(one[0]); return true; }
                error = "not a number";
                return false;
            case FieldControl.Float3:
                if (Floats(3) is float[] three) { value = PropertyValue.FromVec3(new Vector3(three[0], three[1], three[2])); return true; }
                error = "expected three numbers";
                return false;
            case FieldControl.Float4:
                if (Floats(4) is float[] four) { value = PropertyValue.FromVec4(new Vector4(four[0], four[1], four[2], four[3])); return true; }
                error = "expected four numbers";
                return false;
            case FieldControl.Enum:
            {
                SchemaConstant? choice = field.Choices.FirstOrDefault(c => c.Name == text);
                if (choice is null && long.TryParse(text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out long n)) choice = field.Choices.FirstOrDefault(c => c.Value == n);
                if (choice is null) { error = "one of " + string.Join(", ", field.Choices.Select(c => c.Name)); return false; }
                value = PropertyValue.FromEnum(choice.Value);
                return true;
            }
            default:
                if (field.Field.Primitive == PrimitiveKind.AssetGuid && AssetGuid.TryParse(text, out AssetGuid asset)) { value = PropertyValue.FromAsset(asset); return true; }
                if (field.Field.Primitive == PrimitiveKind.EntityGuid && EntityGuid.TryParse(text, out EntityGuid entity)) { value = PropertyValue.FromEntity(entity); return true; }
                error = "not a GUID";
                return false;
        }
    }

    private static string Float(float f) => f.ToString("R", CultureInfo.InvariantCulture);
}
