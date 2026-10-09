// Spec 0058 R1, Plan 0058 P6: how a client finds a listening Runtime
// (remote_protocol.md section 1) -- the same resolution order, default path
// and parsing rules as the C++ client half (src/remote/src/session_file.cpp,
// remote_client.cpp).
using System;
using System.IO;

namespace Atlantis.Gameplay.Remote;

/// <summary>What a Runtime's session file records.</summary>
public sealed record SessionInfo(ushort Port, string Token, uint Pid, AssetGuid Scene);

public enum SessionFileError
{
    NotFound,       // no file at the resolved path
    Unreadable,     // the file exists but could not be read
    Malformed,      // not a session object
    WrongProtocol,  // written by a Runtime speaking another protocol
}

public sealed class SessionFileException : GameplayException
{
    public SessionFileException(SessionFileError error, string path) : base(error + ": " + path)
    {
        Error = error;
        Path = path;
    }

    public SessionFileError Error { get; }
    public string Path { get; }
}

public static class SessionFile
{
    /// <summary>The one protocol this client speaks.</summary>
    public const string Protocol = "atlantis.remote/1";

    /// <summary>The environment variable naming a session file.</summary>
    public const string EnvironmentVariable = "ATLANTIS_SESSION";

    /// <summary><c>.atlantis/runtime.session.json</c>, relative to the working directory.</summary>
    public static string DefaultPath => System.IO.Path.Combine(".atlantis", "runtime.session.json");

    /// <summary>
    /// <paramref name="explicitPath"/> if given, else <paramref name="environmentValue"/>
    /// (ATLANTIS_SESSION) if set and non-empty, else <see cref="DefaultPath"/>.
    /// </summary>
    public static string Resolve(string? explicitPath, string? environmentValue)
    {
        if (explicitPath is not null) return explicitPath;
        if (!string.IsNullOrEmpty(environmentValue)) return environmentValue;
        return DefaultPath;
    }

    /// <summary>Resolves with this process's ATLANTIS_SESSION.</summary>
    public static string Resolve(string? explicitPath) =>
        Resolve(explicitPath, Environment.GetEnvironmentVariable(EnvironmentVariable));

    /// <summary>Reads and parses a session file; throws <see cref="SessionFileException"/>.</summary>
    public static SessionInfo Read(string path)
    {
        if (!File.Exists(path)) throw new SessionFileException(SessionFileError.NotFound, path);
        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            throw new SessionFileException(SessionFileError.Unreadable, path);
        }
        string text;
        try
        {
            text = new System.Text.UTF8Encoding(false, true).GetString(bytes);
        }
        catch (ArgumentException)
        {
            throw new SessionFileException(SessionFileError.Malformed, path);
        }
        return TryParse(text, out SessionInfo? session, out SessionFileError error)
            ? session!
            : throw new SessionFileException(error, path);
    }

    /// <summary>Parses a session file's text, as the C++ parseSession does.</summary>
    public static bool TryParse(string text, out SessionInfo? session, out SessionFileError error)
    {
        session = null;
        error = SessionFileError.Malformed;
        JsonValue parsed;
        try
        {
            parsed = Json.Parse(text);
        }
        catch (FormatException)
        {
            return false;
        }
        if (!parsed.IsObject) return false;
        JsonValue? protocol = parsed.Find("protocol");
        JsonValue? port = parsed.Find("port");
        JsonValue? token = parsed.Find("token");
        JsonValue? pid = parsed.Find("pid");
        JsonValue? scene = parsed.Find("scene");
        if (protocol is null || !protocol.IsString) return false;
        if (protocol.AsString != Protocol)
        {
            error = SessionFileError.WrongProtocol;
            return false;
        }
        if (port is null || !port.TryGetUInt64(out ulong portNumber) || portNumber == 0 || portNumber > 65535 ||
            token is null || !token.IsString || pid is null || !pid.TryGetUInt64(out ulong pidNumber) ||
            pidNumber > uint.MaxValue || scene is null || !scene.IsString)
        {
            return false;
        }
        if (!AssetGuid.TryParse(scene.AsString, out AssetGuid sceneGuid) || sceneGuid.IsNil) return false;
        session = new SessionInfo((ushort)portNumber, token.AsString, (uint)pidNumber, sceneGuid);
        return true;
    }

    /// <summary>The file's text, as a Runtime writes it (one object and a newline).</summary>
    public static string Format(SessionInfo session) =>
        Json.Write(JsonValue.NewObject()
            .Set("protocol", JsonValue.String(Protocol))
            .Set("port", JsonValue.Number((ulong)session.Port))
            .Set("token", JsonValue.String(session.Token))
            .Set("pid", JsonValue.Number((ulong)session.Pid))
            .Set("scene", JsonValue.String(session.Scene.ToString()))) + "\n";
}
