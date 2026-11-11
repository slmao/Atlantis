using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading.Tasks;
using Atlantis.Gameplay.Remote;
using A = Atlantis.Gameplay.Assets;

namespace Atlantis.Gameplay.Tests;
public static class LiveAssetSuite
{
    public static void RunOldPeer(TestContext t)
    {
        using var server = new FixtureServer(t);
        using RemoteSession client = server.Attach();
        _ = client.Assets;
        t.Check(client.Failure is null && client.ListEntities().Count > 0 && client.Schema.Count > 0, "old binary connects and existing queries work after lazy Assets access");
        t.Throws<RemoteException>(() => client.Assets.ListAssets(), "new unsupported capability follows typed failure contract");
        t.Check(client.Failure is not null, "unsupported asset query fails typed session");
    }

    public static void Run(TestContext t)
    {
        PreviewContract(t);
        PairPreviewContract(t);
        using var server = new FixtureServer(t);
        using RemoteSession a = server.Attach(), b = server.Attach();
        var list = a.Assets.ListAssets();
        t.Require(list.IsOk && list.Value.Count == 5, "five catalog types");
        t.Check(list.Value.Select(v => v.Guid).SequenceEqual(b.Assets.ListAssets().Value.Select(v => v.Guid)), "two clients same GUID order");
        foreach (var summary in list.Value)
        {
            var d = b.Assets.GetAsset(summary.Guid);
            t.Require(d.IsOk, "get known asset");
            t.Check(d.Value.Type == summary.Type && d.Value.Source == summary.Source && d.Value.SourceSchema is null, "exact summary/details");
        }
        var unknown = a.Assets.GetAsset(AssetGuid.Parse("00810081-0081-4081-8081-008100810081"));
        t.Check(!unknown.IsOk && unknown.Error == A.AssetError.UnknownAsset && a.Failure is null, "in-band unknown preserves session");
        JsonValue bad = a.AwaitResponse(a.Send("assets.get", JsonValue.NewObject().Set("guid", JsonValue.String("00000000-0000-0000-0000-000000000000"))));
        t.Check(bad.Find("error")?.Find("code")?.AsString == "BadRequest", "nil wire GUID refused without host query/assert");
        t.Check(a.Assets.ListAssets().IsOk, "raw refusal did not poison SDK session");
        t.Throws<ArgumentException>(() => a.Assets.GetAsset(default), "typed nil input rejected locally");
        using (var unavailable = new FixtureServer(t, mutate: "assets-unavailable"))
        using (var client = unavailable.Attach())
        {
            t.Check(client.Assets.ListAssets().Error == A.AssetError.CatalogUnavailable && client.Failure is null, "catalog refusal preserves typed session");
            t.Check(client.ListEntities().Count > 0, "World remains usable after catalog refusal");
        }
        foreach (string mode in new[] { "assets-duplicate", "assets-wrong-guid" })
        {
            using var broken = new FixtureServer(t, mutate: mode);
            using var client = broken.Attach();
            if (mode == "assets-duplicate") t.Throws<RemoteException>(() => client.Assets.ListAssets(), "duplicate list fails typed session");
            else
            {
                AssetGuid known = client.Assets.ListAssets().Value[0].Guid;
                t.Throws<RemoteException>(() => client.Assets.GetAsset(known), "mismatched get fails typed session");
            }
            t.Check(client.Failure is not null, "malformed result is sticky");
        }
    }

    private static void PreviewContract(TestContext t)
    {
        AssetGuid guid = AssetGuid.Parse("00810081-0081-4081-8081-008100810081");
        JsonValue Metadata() => Codec.Encode(new A.AssetDetails(guid, A.AssetType.Mesh, "assets:mesh.txt", 4,
            null, "test/1", Array.Empty<AssetGuid>()));
        JsonValue LegacyError() => Json.Parse("""{"code":"BadRequest","message":"unknown or malformed asset query"}""");
        JsonValue Result(JsonValue value) => JsonValue.NewObject().Set("result", value);
        JsonValue Ok(JsonValue value) => Result(JsonValue.NewObject().Set("ok", value));
        JsonValue Refusal(string token) => Result(JsonValue.NewObject().Set("err", JsonValue.String(token)));
        JsonValue? Common(string method) => method switch
        {
            "assets.list" => Ok(JsonValue.NewArray().Push(Codec.Encode(new A.AssetSummary(guid, A.AssetType.Mesh, "assets:mesh.txt")))),
            "assets.get" => Ok(Metadata()),
            _ => null,
        };
        WithPeer(t, guid, method => method == "assets.previewMesh" ? JsonValue.NewObject().Set("error", LegacyError()) : Common(method), client =>
        {
            t.Throws<ArgumentException>(() => client.Assets.PreviewMesh(default), "nil preview GUID is rejected before sending");
            var legacy = client.Assets.PreviewMesh(guid);
            t.Check(!legacy.IsOk && legacy.Error == A.PreviewError.Unsupported, "exact legacy preview refusal becomes Unsupported");
            t.Check(client.Failure is null && client.Assets.ListAssets().Value.Single().Guid == guid &&
                client.Assets.GetAsset(guid).Value.Guid == guid, "older asset-capable peer remains usable for list/get");
        });
        int attempt = 0;
        WithPeer(t, guid, _ => ++attempt == 1 ? Refusal("Busy") : Ok(AssetCodecSuite.PreviewValue(guid, AssetCodecSuite.PreviewHeader)), client =>
        {
            var busy = client.Assets.PreviewMesh(guid);
            t.Check(!busy.IsOk && busy.Error == A.PreviewError.Busy && client.Failure is null, "Busy is in-band and never retried internally");
            var image = client.Assets.PreviewMesh(guid);
            t.Check(image.IsOk && image.Value.Guid == guid && image.Value.PngBytes.AsSpan().SequenceEqual(AssetCodecSuite.PreviewHeader),
                "explicit SDK retry returns a typed preview from the correlated response");
        }, expectedPreviews: 2);
        foreach (var (name, reply) in new (string, JsonValue)[]
        {
            ("result/error", Refusal("Busy").Set("error", LegacyError())),
            ("ok/err", Result(JsonValue.NewObject().Set("ok", AssetCodecSuite.PreviewValue(guid, AssetCodecSuite.PreviewHeader))
                .Set("err", JsonValue.String("Busy")))),
            ("other top-level refusal", JsonValue.NewObject().Set("error", LegacyError().Set("message", JsonValue.String("invalid guid")))),
            ("wrong reply GUID", Ok(AssetCodecSuite.PreviewValue(AssetGuid.Parse("11111111-2222-4333-8444-555555555555"), AssetCodecSuite.PreviewHeader))),
        })
        {
            WithPeer(t, guid, _ => reply, client =>
            {
                var error = t.Throws<RemoteException>(() => client.Assets.PreviewMesh(guid), "malformed preview " + name + " throws");
                t.Check(error?.Error == RemoteError.ProtocolError && client.Failure == RemoteError.ProtocolError,
                    "malformed preview " + name + " is a sticky protocol failure");
            });
        }
        WithPeer(t, guid, _ => null, client =>
        {
            var error = t.Throws<RemoteException>(() => client.Assets.PreviewMesh(guid), "lost preview reply throws");
            t.Check(error?.Error == RemoteError.Disconnected && client.Failure == RemoteError.Disconnected,
                "lost preview reply stays a transport failure");
        });
    }

    private static void PairPreviewContract(TestContext t)
    {
        const string method = "assets.previewMeshMaterial";
        AssetGuid mesh = AssetGuid.Parse("00810081-0081-4081-8081-008100810081");
        AssetGuid material = AssetCodecSuite.PreviewMaterial;
        JsonValue Result(JsonValue value) => JsonValue.NewObject().Set("result", value);
        JsonValue Ok(JsonValue value) => Result(JsonValue.NewObject().Set("ok", value));
        JsonValue Refusal(string token) => Result(JsonValue.NewObject().Set("err", JsonValue.String(token)));
        JsonValue Pair() => AssetCodecSuite.PairValue(mesh, material, AssetCodecSuite.PreviewHeader);
        JsonValue Legacy() => Json.Parse("""{"error":{"code":"BadRequest","message":"unknown or malformed asset query"}}""");
        JsonValue Details() => Codec.Encode(new A.AssetDetails(mesh, A.AssetType.Mesh, "assets:mesh.txt", 4,
            null, "test/1", Array.Empty<AssetGuid>()));
        WithPeer(t, mesh, m => m switch
        {
            method => Legacy(),
            "assets.list" => Ok(JsonValue.NewArray().Push(Codec.Encode(new A.AssetSummary(mesh, A.AssetType.Mesh, "assets:mesh.txt")))),
            "assets.get" => Ok(Details()),
            "assets.previewMesh" => Ok(AssetCodecSuite.PreviewValue(mesh, AssetCodecSuite.PreviewHeader)),
            _ => null,
        }, client =>
        {
            t.Throws<ArgumentException>(() => client.Assets.PreviewMeshMaterial(default, material), "nil mesh is rejected before sending");
            t.Throws<ArgumentException>(() => client.Assets.PreviewMeshMaterial(mesh, default), "nil material is rejected before sending");
            var legacy = client.Assets.PreviewMeshMaterial(mesh, material);
            t.Check(!legacy.IsOk && legacy.Error == A.MeshMaterialPreviewError.Unsupported, "0091 fallback accepts only the legacy refusal");
            t.Check(client.Failure is null && client.Assets.ListAssets().Value.Single().Guid == mesh &&
                client.Assets.GetAsset(mesh).Value.Guid == mesh && client.Assets.PreviewMesh(mesh).Value.Guid == mesh,
                "pair fallback keeps the same session usable for list, get and geometry preview");
        }, material: material);

        int attempt = 0;
        WithPeer(t, mesh, _ => ++attempt == 1 ? Refusal("Busy") : Ok(Pair()), client =>
        {
            var busy = client.Assets.PreviewMeshMaterial(mesh, material);
            t.Check(!busy.IsOk && busy.Error == A.MeshMaterialPreviewError.Busy && client.Failure is null,
                "pair Busy is an in-band refusal without automatic retry");
            var image = client.Assets.PreviewMeshMaterial(mesh, material);
            t.Check(image.IsOk && image.Value.MeshGuid == mesh && image.Value.MaterialGuid == material &&
                image.Value.PngBytes.AsSpan().SequenceEqual(AssetCodecSuite.PreviewHeader),
                "explicit pair retry succeeds with the correlated roles and PNG");
        }, expectedPreviews: 2, material: material);

        foreach (var (name, reply) in new (string, JsonValue)[]
        {
            ("duplicate id", Json.Parse("""{"id":0,"id":1,"result":{"err":"Busy"}}""")),
            ("duplicate result", Json.Parse("""{"result":{"err":"Busy"},"result":{"err":"TooLarge"}}""")),
            ("duplicate error", Json.Parse("""{"error":{"code":"BadRequest","message":"unknown or malformed asset query"},"error":null}""")),
            ("result/error", Legacy().Set("result", JsonValue.NewObject().Set("err", JsonValue.String("Busy")))),
            ("ok/err", Result(JsonValue.NewObject().Set("ok", Pair()).Set("err", JsonValue.String("Busy")))),
            ("duplicate err", Json.Parse("""{"result":{"err":"Busy","err":"TooLarge"}}""")),
            ("duplicate ok", Json.Parse("{\"result\":{\"ok\":" + Json.Write(Pair()) + ",\"ok\":null}}")),
            ("wrong legacy message", JsonValue.NewObject().Set("error",
                Legacy().Find("error")!.Set("message", JsonValue.String("invalid material GUID")))),
            ("duplicate legacy code", Json.Parse("""{"error":{"code":"BadRequest","code":"Other","message":"unknown or malformed asset query"}}""")),
            ("duplicate legacy message", Json.Parse("""{"error":{"code":"BadRequest","message":"unknown or malformed asset query","message":"Other"}}""")),
            ("wrong mesh GUID", Ok(Pair().Set("meshGuid", Codec.Encode(material)))),
            ("wrong material GUID", Ok(Pair().Set("materialGuid", Codec.Encode(mesh)))),
            ("swapped roles", Ok(AssetCodecSuite.PairValue(material, mesh, AssetCodecSuite.PreviewHeader))),
            ("unknown refusal", Refusal("UnknownAsset")),
        })
        {
            WithPeer(t, mesh, _ => reply, client =>
            {
                var error = t.Throws<RemoteException>(() => client.Assets.PreviewMeshMaterial(mesh, material),
                    "malformed pair preview " + name + " throws");
                t.Check(error?.Error == RemoteError.ProtocolError && client.Failure == RemoteError.ProtocolError,
                    "malformed pair preview " + name + " is a sticky protocol failure");
            }, material: material);
        }
        WithPeer(t, mesh, _ => null, client =>
        {
            var error = t.Throws<RemoteException>(() => client.Assets.PreviewMeshMaterial(mesh, material), "lost pair reply throws");
            t.Check(error?.Error == RemoteError.Disconnected && client.Failure == RemoteError.Disconnected,
                "lost pair reply stays a transport failure");
        }, material: material);
    }

    private static void WithPeer(TestContext t, AssetGuid guid, Func<string, JsonValue?> reply, Action<RemoteSession> run,
        int expectedPreviews = 1, AssetGuid? material = null)
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        Task<int> peer = Task.Run(() =>
        {
            using TcpClient socket = listener.AcceptTcpClient();
            socket.ReceiveTimeout = 5000;
            using NetworkStream stream = socket.GetStream();
            using StreamReader reader = new(stream);
            using StreamWriter writer = new(stream) { AutoFlush = true, NewLine = "\n" };
            int requests = 0;
            while (reader.ReadLine() is string line)
            {
                JsonValue request = Json.Parse(line);
                string method = request.Find("method")!.AsString;
                JsonValue? envelope = method switch
                {
                    "hello" => JsonValue.NewObject().Set("result", JsonValue.NewObject().Set("scene", Codec.Encode(guid))),
                    "connection.schema" => JsonValue.NewObject().Set("result", JsonValue.NewArray()),
                    _ => reply(method),
                };
                if (method == (material is null ? "assets.previewMesh" : "assets.previewMeshMaterial"))
                {
                    ++requests;
                    var parameters = material is AssetGuid asset
                        ? JsonValue.NewObject().Set("meshGuid", Codec.Encode(guid)).Set("materialGuid", Codec.Encode(asset))
                        : JsonValue.NewObject().Set("guid", Codec.Encode(guid));
                    if (Json.Write(request.Find("params")!) != Json.Write(parameters))
                        throw new InvalidOperationException("preview did not send the exact expected GUID parameters");
                }
                if (envelope is null) break;
                writer.WriteLine(Json.Write(envelope.Set("id", request.Find("id")!)));
            }
            return requests;
        });

        var info = new SessionInfo((ushort)((IPEndPoint)listener.LocalEndpoint).Port,
            "00112233445566778899aabbccddeeff", 1, guid);
        using (RemoteSession client = RemoteSession.Connect(info, new RemoteOptions { ResponseTimeoutMilliseconds = 5000 })) run(client);
        t.Equal(expectedPreviews, peer.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult(),
            "preview peer received exactly the requested number of GUID-only calls");
    }
}
