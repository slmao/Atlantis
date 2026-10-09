// Plan 0058 P9 (Spec 0058 R1): the "session" suite -- the session file's
// parsing (as the C++ parseSession), its writer, and the path resolution
// order (explicit, then ATLANTIS_SESSION, then the default).
using System;
using System.IO;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Gameplay.Tests;

public static class SessionSuite
{
    private const string Scene = "52052052-00aa-4052-8052-0000000000aa";
    private const string Token = "00112233445566778899aabbccddeeff";

    private static string File(string port = "51234", string token = "\"" + Token + "\"", string pid = "4242",
                               string scene = "\"" + Scene + "\"", string protocol = "\"atlantis.remote/1\"") =>
        "{\"protocol\":" + protocol + ",\"port\":" + port + ",\"token\":" + token + ",\"pid\":" + pid + ",\"scene\":" + scene + "}\n";

    public static void Run(TestContext t)
    {
        t.Require(SessionFile.TryParse(File(), out SessionInfo? session, out _), "a valid session file parses");
        t.Equal(new SessionInfo(51234, Token, 4242, AssetGuid.Parse(Scene)), session, "its port, token, pid and scene");
        t.Equal(File(), SessionFile.Format(session!), "it is written back byte for byte");
        t.Check(SessionFile.TryParse(File(port: "1"), out _, out _) && SessionFile.TryParse(File(port: "65535"), out _, out _),
                "ports 1 and 65535");
        t.Check(SessionFile.TryParse(File(pid: "4294967295"), out _, out _), "pid UINT32_MAX");
        t.Check(SessionFile.TryParse("  " + File().TrimEnd('\n') + "\r\n", out _, out _), "surrounding whitespace");

        Malformed(t, "{\"protocol\":\"atlantis.remote/1\"}", "missing fields");
        Malformed(t, File(port: "0"), "port 0");
        Malformed(t, File(port: "65536"), "port 65536");
        Malformed(t, File(port: "-1"), "a negative port");
        Malformed(t, File(port: "80.0"), "a fractional port");
        Malformed(t, File(port: "\"80\""), "a string port");
        Malformed(t, File(token: "42"), "a numeric token");
        Malformed(t, File(pid: "4294967296"), "pid over UINT32_MAX");
        Malformed(t, File(scene: "\"00000000-0000-0000-0000-000000000000\""), "the nil scene");
        Malformed(t, File(scene: "\"52052052-00AA-4052-8052-0000000000AA\""), "an uppercase scene");
        Malformed(t, File(scene: "7"), "a numeric scene");
        Malformed(t, "[1,2]", "an array");
        Malformed(t, "{\"protocol\":", "truncated JSON");
        Malformed(t, File(protocol: "7"), "a numeric protocol");
        t.Check(!SessionFile.TryParse(File(protocol: "\"atlantis.remote/2\""), out _, out SessionFileError error) &&
                error == SessionFileError.WrongProtocol, "another protocol: WrongProtocol");

        // Resolution order.
        t.Equal("explicit.json", SessionFile.Resolve("explicit.json", "env.json"), "an explicit path wins");
        t.Equal("env.json", SessionFile.Resolve(null, "env.json"), "else ATLANTIS_SESSION");
        t.Equal(Path.Combine(".atlantis", "runtime.session.json"), SessionFile.Resolve(null, ""), "an empty variable is unset");
        t.Equal(Path.Combine(".atlantis", "runtime.session.json"), SessionFile.Resolve(null, null), "else the default path");

        // Reading files.
        string dir = Path.Combine(Path.GetTempPath(), "atlantis_csharp_session_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            string path = Path.Combine(dir, "runtime.session.json");
            t.Equal(SessionFileError.NotFound, t.Throws<SessionFileException>(() => SessionFile.Read(path), "a missing file throws")?.Error,
                    "NotFound");
            System.IO.File.WriteAllText(path, File());
            t.Equal(session, SessionFile.Read(path), "a file on disk reads");
            System.IO.File.WriteAllBytes(path, new byte[] { 0x7b, 0xff, 0x7d });
            t.Equal(SessionFileError.Malformed, t.Throws<SessionFileException>(() => SessionFile.Read(path), "invalid UTF-8 throws")?.Error,
                    "Malformed");
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    private static void Malformed(TestContext t, string text, string what) =>
        t.Check(!SessionFile.TryParse(text, out _, out SessionFileError error) && error == SessionFileError.Malformed,
                "Malformed: " + what);
}
