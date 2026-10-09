// Spec 0058 R1/R6, Plan 0058 P6: the JSON of atlantis.remote/1 -- a small
// value tree, a reader built on System.Text.Json, and a writer that produces
// exactly the bytes the C++ writer (src/connection/src/json.cpp) produces:
// compact, members in insertion order, the same escapes, numbers as their
// literal text, floats in std::to_chars's shortest form (FloatText). The
// conformance vectors check it byte for byte.
//
// System.Text.Json's own writer is not used: it escapes non-ASCII and HTML
// characters by default and formats floats differently.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Numerics;
using System.Text;
using System.Text.Json;

namespace Atlantis.Gameplay.Remote;

public enum JsonKind
{
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
}

/// <summary>
/// A JSON value. A number keeps its literal text, so a value read and written
/// again is unchanged. Objects keep their members in order (duplicates too:
/// <see cref="Find"/> returns the first, as the C++ reader does). Not
/// thread-safe while being built.
/// </summary>
public sealed class JsonValue
{
    private readonly bool _bool;
    private readonly string _text = "";  // a number's literal, or a string
    private readonly List<JsonValue>? _items;
    private readonly List<KeyValuePair<string, JsonValue>>? _members;

    private JsonValue(JsonKind kind, bool boolean = false, string text = "")
    {
        Kind = kind;
        _bool = boolean;
        _text = text;
        if (kind == JsonKind.Array) _items = new List<JsonValue>();
        if (kind == JsonKind.Object) _members = new List<KeyValuePair<string, JsonValue>>();
    }

    public JsonKind Kind { get; }

    public static JsonValue Null() => new(JsonKind.Null);
    public static JsonValue Bool(bool value) => new(JsonKind.Bool, boolean: value);
    public static JsonValue String(string value) => new(JsonKind.String, text: value);
    public static JsonValue Number(ulong value) => new(JsonKind.Number, text: value.ToString(CultureInfo.InvariantCulture));
    public static JsonValue Number(long value) => new(JsonKind.Number, text: value.ToString(CultureInfo.InvariantCulture));

    /// <summary>A finite float, in std::to_chars's shortest form.</summary>
    public static JsonValue Number(float value)
    {
        if (!float.IsFinite(value)) throw new ArgumentException("a non-finite float has no JSON number form", nameof(value));
        return new(JsonKind.Number, text: FloatText.Format(value));
    }

    public static JsonValue NumberLiteral(string literal) => new(JsonKind.Number, text: literal);
    public static JsonValue NewArray() => new(JsonKind.Array);
    public static JsonValue NewObject() => new(JsonKind.Object);

    public bool IsNull => Kind == JsonKind.Null;
    public bool IsBool => Kind == JsonKind.Bool;
    public bool IsNumber => Kind == JsonKind.Number;
    public bool IsString => Kind == JsonKind.String;
    public bool IsArray => Kind == JsonKind.Array;
    public bool IsObject => Kind == JsonKind.Object;

    public bool AsBool => Kind == JsonKind.Bool ? _bool : throw WrongKind();
    public string AsString => Kind == JsonKind.String ? _text : throw WrongKind();
    public string NumberText => Kind == JsonKind.Number ? _text : throw WrongKind();
    public IReadOnlyList<JsonValue> Items => _items ?? throw WrongKind();
    public IReadOnlyList<KeyValuePair<string, JsonValue>> Members => _members ?? throw WrongKind();

    private InvalidOperationException WrongKind() => new($"the JSON value is {Kind}");

    /// <summary>The first member named <paramref name="key"/>; null if none, or if this is not an object.</summary>
    public JsonValue? Find(string key)
    {
        if (_members is null) return null;
        foreach (KeyValuePair<string, JsonValue> member in _members)
        {
            if (member.Key == key) return member.Value;
        }
        return null;
    }

    /// <summary>Replaces the member named <paramref name="key"/>, or appends it.</summary>
    public JsonValue Set(string key, JsonValue value)
    {
        if (_members is null) throw WrongKind();
        for (int i = 0; i < _members.Count; ++i)
        {
            if (_members[i].Key == key)
            {
                _members[i] = new KeyValuePair<string, JsonValue>(key, value);
                return this;
            }
        }
        _members.Add(new KeyValuePair<string, JsonValue>(key, value));
        return this;
    }

    public JsonValue Push(JsonValue item)
    {
        if (_items is null) throw WrongKind();
        _items.Add(item);
        return this;
    }

    /// <summary>A number of decimal digits only, in range (json.cpp toUInt64).</summary>
    public bool TryGetUInt64(out ulong value)
    {
        value = 0;
        if (Kind != JsonKind.Number || _text.Length == 0) return false;
        foreach (char c in _text)
        {
            if (c < '0' || c > '9') return false;
        }
        return ulong.TryParse(_text, NumberStyles.None, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>A number of decimal digits with an optional leading '-', in range (json.cpp toInt64).</summary>
    public bool TryGetInt64(out long value)
    {
        value = 0;
        if (Kind != JsonKind.Number || _text.Length == 0) return false;
        for (int i = 0; i < _text.Length; ++i)
        {
            char c = _text[i];
            if (!((c >= '0' && c <= '9') || (i == 0 && c == '-'))) return false;
        }
        return long.TryParse(_text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>A number that converts to a finite float (json.cpp toFloat).</summary>
    public bool TryGetFloat(out float value)
    {
        value = 0;
        return Kind == JsonKind.Number && FloatText.TryParse(_text, out value);
    }
}

public static class Json
{
    private const int MaxDepth = 64;

    /// <summary>Parses one JSON value (RFC 8259; no comments or trailing commas). Throws <see cref="FormatException"/>.</summary>
    public static JsonValue Parse(string text) => Parse(Encoding.UTF8.GetBytes(text));

    public static JsonValue Parse(ReadOnlySpan<byte> utf8)
    {
        try
        {
            var reader = new Utf8JsonReader(utf8, new JsonReaderOptions
            {
                CommentHandling = JsonCommentHandling.Disallow,
                AllowTrailingCommas = false,
                MaxDepth = MaxDepth,
            });
            if (!reader.Read()) throw new FormatException("empty JSON text");
            JsonValue value = ReadValue(ref reader);
            if (reader.Read()) throw new FormatException("text after the JSON value");
            return value;
        }
        catch (JsonException e)
        {
            throw new FormatException("malformed JSON: " + e.Message, e);
        }
        catch (InvalidOperationException e)
        {
            throw new FormatException("malformed JSON: " + e.Message, e);
        }
    }

    private static JsonValue ReadValue(ref Utf8JsonReader reader)
    {
        switch (reader.TokenType)
        {
            case JsonTokenType.Null: return JsonValue.Null();
            case JsonTokenType.True: return JsonValue.Bool(true);
            case JsonTokenType.False: return JsonValue.Bool(false);
            case JsonTokenType.Number: return JsonValue.NumberLiteral(Encoding.UTF8.GetString(reader.ValueSpan));
            case JsonTokenType.String: return JsonValue.String(reader.GetString() ?? "");
            case JsonTokenType.StartArray:
            {
                JsonValue array = JsonValue.NewArray();
                while (reader.Read() && reader.TokenType != JsonTokenType.EndArray) array.Push(ReadValue(ref reader));
                return array;
            }
            case JsonTokenType.StartObject:
            {
                JsonValue obj = JsonValue.NewObject();
                var members = (List<KeyValuePair<string, JsonValue>>)obj.Members;
                while (reader.Read() && reader.TokenType != JsonTokenType.EndObject)
                {
                    string key = reader.GetString() ?? "";
                    reader.Read();
                    members.Add(new KeyValuePair<string, JsonValue>(key, ReadValue(ref reader)));
                }
                return obj;
            }
            default: throw new FormatException("unexpected JSON token " + reader.TokenType);
        }
    }

    /// <summary>Compact JSON text, byte for byte as the C++ writer (once encoded as UTF-8).</summary>
    public static string Write(JsonValue value)
    {
        var output = new StringBuilder();
        WriteValue(output, value);
        return output.ToString();
    }

    private static void WriteValue(StringBuilder output, JsonValue value)
    {
        switch (value.Kind)
        {
            case JsonKind.Null: output.Append("null"); return;
            case JsonKind.Bool: output.Append(value.AsBool ? "true" : "false"); return;
            case JsonKind.Number: output.Append(value.NumberText); return;
            case JsonKind.String: WriteString(output, value.AsString); return;
            case JsonKind.Array:
            {
                output.Append('[');
                bool first = true;
                foreach (JsonValue item in value.Items)
                {
                    if (!first) output.Append(',');
                    first = false;
                    WriteValue(output, item);
                }
                output.Append(']');
                return;
            }
            case JsonKind.Object:
            {
                output.Append('{');
                bool first = true;
                foreach (KeyValuePair<string, JsonValue> member in value.Members)
                {
                    if (!first) output.Append(',');
                    first = false;
                    WriteString(output, member.Key);
                    output.Append(':');
                    WriteValue(output, member.Value);
                }
                output.Append('}');
                return;
            }
        }
    }

    private static void WriteString(StringBuilder output, string text)
    {
        const string hex = "0123456789abcdef";
        output.Append('"');
        foreach (char c in text)
        {
            switch (c)
            {
                case '"': output.Append("\\\""); break;
                case '\\': output.Append("\\\\"); break;
                case '\b': output.Append("\\b"); break;
                case '\f': output.Append("\\f"); break;
                case '\n': output.Append("\\n"); break;
                case '\r': output.Append("\\r"); break;
                case '\t': output.Append("\\t"); break;
                default:
                    if (c < 0x20)
                    {
                        output.Append("\\u00").Append(hex[(c >> 4) & 0xF]).Append(hex[c & 0xF]);
                    }
                    else
                    {
                        output.Append(c);
                    }
                    break;
            }
        }
        output.Append('"');
    }
}

/// <summary>
/// Float text as C++ <c>std::to_chars(float)</c> writes it, and as
/// <c>strtof</c> reads it: the shortest round trip; fixed unless scientific
/// is strictly shorter; an integral value in fixed notation written exactly;
/// a lowercase 'e', a sign and at least two exponent digits. The shortest
/// digits come from .NET's round-trip formatting; only the layout is chosen
/// here.
/// </summary>
public static class FloatText
{
    public static string Format(float value)
    {
        if (!float.IsFinite(value)) throw new ArgumentException("not finite", nameof(value));
        string r = value.ToString("R", CultureInfo.InvariantCulture);  // e.g. "-1.2345E-05", "123456.7", "-0"
        bool negative = r.StartsWith('-');
        if (negative) r = r.Substring(1);
        int e = r.IndexOfAny(new[] { 'E', 'e' });
        int exponent = e < 0 ? 0 : int.Parse(r.AsSpan(e + 1), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture);
        string mantissa = e < 0 ? r : r.Substring(0, e);
        int point = mantissa.IndexOf('.');
        int integerDigits = point < 0 ? mantissa.Length : point;
        string digits = point < 0 ? mantissa : mantissa.Remove(point, 1);
        // value = 0.digits x 10^(integerDigits + exponent); normalize to d.ddd x 10^x.
        int x = integerDigits - 1 + exponent;
        int lead = 0;
        while (lead < digits.Length && digits[lead] == '0')
        {
            ++lead;
            --x;
        }
        digits = digits.Substring(lead).TrimEnd('0');
        string sign = negative ? "-" : "";
        if (digits.Length == 0) return sign + "0";

        int n = digits.Length;
        int absX = Math.Abs(x);
        string scientific = digits[0] + (n > 1 ? "." + digits.Substring(1) : "") + "e" + (x < 0 ? "-" : "+") +
                            (absX < 10 ? "0" : "") + absX.ToString(CultureInfo.InvariantCulture);
        string fixedText;
        // An integral value in fixed notation is written exactly: its exact
        // digits are as short as the shortest digits padded with zeros, and
        // closer (std::to_chars's tie rule), e.g. 5409459712, not 5409459700.
        if (x >= n - 1) fixedText = new BigInteger(Math.Abs((double)value)).ToString(CultureInfo.InvariantCulture);
        else if (x >= 0) fixedText = digits.Substring(0, x + 1) + "." + digits.Substring(x + 1);
        else fixedText = "0." + new string('0', -x - 1) + digits;
        return sign + (fixedText.Length <= scientific.Length ? fixedText : scientific);
    }

    /// <summary>A JSON number's text to a finite float, correctly rounded.</summary>
    public static bool TryParse(string text, out float value)
    {
        if (!float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value)) return false;
        return float.IsFinite(value);
    }
}
