using System;
using Atlantis.Gameplay.Remote;
using A = Atlantis.Gameplay.Assets;

namespace Atlantis.Gameplay.Tests;

public static class AssetCodecSuite
{
    public static void Run(TestContext t)
    {
        var guid = AssetGuid.Parse("00810081-0081-4081-8081-008100810081");
        foreach (A.AssetType type in System.Enum.GetValues<A.AssetType>())
        {
            var summary = new A.AssetSummary(guid, type, "content:scene.gltf#mesh/0");
            t.Equal<A.AssetSummary?>(summary, Codec.DecodeAssetSummary(Codec.Encode(summary)), "asset summary " + type);
            var d = new A.AssetDetails(guid, type, summary.Source, uint.MaxValue, null, "test/1", System.Array.Empty<AssetGuid>());
            t.Check(Json.Write(Codec.Encode(Codec.DecodeAssetDetails(Codec.Encode(d))!)) == Json.Write(Codec.Encode(d)), "asset detail round trip");
            JsonValue extra = Codec.Encode(d).Set("future", JsonValue.Bool(true));
            t.Check(Codec.DecodeAssetDetails(extra)?.ArtifactSchema == uint.MaxValue, "extra member and unknown schema accepted");
            foreach (string key in new[] { "guid", "type", "source", "artifactSchema", "tool", "dependencies" })
                t.Check(Codec.DecodeAssetDetails(Codec.Encode(d).Set(key, JsonValue.Null())) is null, "malformed detail " + key);
            t.Check(Codec.DecodeAssetDetails(Codec.Encode(d).Set("artifactSchema", JsonValue.Number((ulong)uint.MaxValue + 1))) is null, "schema overflow");
            t.Check(Codec.DecodeAssetDetails(Codec.Encode(d).Set("dependencies", JsonValue.NewArray().Push(Codec.Encode(guid)).Push(Codec.Encode(guid)))) is null, "duplicate dependency");
        }
        t.Check(Codec.DecodeAssetError(JsonValue.String("wrong")) is null, "closed asset errors");
        var renamed = new A.AssetSummary(guid, A.AssetType.Texture, "content:texture_probe/source.png") { DisplayName = "blue_probe" };
        t.Equal<A.AssetSummary?>(renamed, Codec.DecodeAssetSummary(Codec.Encode(renamed)), "texture label leaves source and GUID alone");
        t.Check(Codec.DecodeAssetSummary(Codec.Encode(renamed with { DisplayName = null }))?.DisplayName is null,
            "legacy asset summaries omit the name");
        t.Check(Codec.DecodeAssetSummary(Codec.Encode(renamed).Set("displayName", JsonValue.Null())) is null &&
            Codec.DecodeAssetSummary(Json.Parse(Json.Write(Codec.Encode(renamed))[..^1] + ",\"displayName\":\"other\"}")) is null &&
            Codec.DecodeAssetSummary(Codec.Encode(renamed with { DisplayName = "bad path" })) is null,
            "asset summary rejects null, duplicate or malformed present names");
        foreach (A.RenameTextureError error in Enum.GetValues<A.RenameTextureError>())
            t.Equal<A.RenameTextureError?>(error, Codec.DecodeRenameTextureError(JsonValue.String(error.ToString())), "rename refusal " + error);
        RenameReceipts(t, guid);
        JsonValue missingSchema = Codec.Encode(new A.AssetSummary(guid, A.AssetType.Mesh, "assets:mesh.txt"))
            .Set("artifactSchema", JsonValue.Number(0UL)).Set("tool", JsonValue.String("test/1")).Set("dependencies", JsonValue.NewArray());
        t.Check(Codec.DecodeAssetDetails(missingSchema) is null, "sourceSchema is required even when absent natively");
        t.Check(Codec.DecodeAssetDetails(missingSchema.Set("sourceSchema", JsonValue.Number((ulong)uint.MaxValue))) is { ArtifactSchema: 0, SourceSchema: uint.MaxValue }, "both uint32 boundaries");
        t.Check(Codec.DecodeAssetDetails(missingSchema.Set("sourceSchema", JsonValue.Number(-1L))) is null, "negative schema refused");
        var running = new A.RebuildJob(new(guid), guid, A.RebuildState.Running);
        t.Equal<A.RebuildJob?>(running, Codec.DecodeRebuildJob(Codec.Encode(running)), "rebuild running receipt");
        t.Check(Codec.DecodeRebuildJob(Codec.Encode(running).Set("failure", JsonValue.Null())) is null,
                "running does not carry failure, including explicit null");
        var failed = running with { State = A.RebuildState.Failed,
            Failure = new(A.RebuildStage.Pipeline, A.RebuildFailureCode.BuildFailed, "bad input", uint.MaxValue) };
        var status = new A.RebuildStatus(null, failed);
        t.Equal<A.RebuildStatus?>(status, Codec.DecodeRebuildStatus(Codec.Encode(status)), "rebuild status with full exit code");
        t.Check(Codec.DecodeRebuildJob(Codec.Encode(failed with { Failure = failed.Failure! with { Stage = A.RebuildStage.Validation } })) is null,
                "closed failure stage/code pair");
        t.Check(Codec.DecodeRebuildError(JsonValue.String("UnknownJob"), true) is null &&
                Codec.DecodeRebuildError(JsonValue.String("Busy"), false) is null, "closed operation refusal sets");
        Preview(t, guid);
        PairPreview(t, guid);
    }

    private static void RenameReceipts(TestContext t, AssetGuid asset)
    {
        foreach (string scenario in new[] { "lost", "malformed", "legacy" })
        {
            using var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
            listener.Start();
            var peer = System.Threading.Tasks.Task.Run(() =>
            {
                using var socket = listener.AcceptTcpClient();
                socket.ReceiveTimeout = 5000;
                using var stream = socket.GetStream();
                using var reader = new System.IO.StreamReader(stream);
                using var writer = new System.IO.StreamWriter(stream) { AutoFlush = true, NewLine = "\n" };
                while (reader.ReadLine() is string line)
                {
                    var request = Json.Parse(line);
                    var method = request.Find("method")!.AsString;
                    var envelope = JsonValue.NewObject().Set("id", request.Find("id")!);
                    if (method == "hello")
                        envelope.Set("result", JsonValue.NewObject().Set("scene", Codec.Encode(asset)));
                    else if (method == "connection.schema")
                        envelope.Set("result", JsonValue.NewArray());
                    else
                    {
                        if (method != "assets.renameTexture" || request.Find("params")?.Find("displayName")?.AsString != "blue_probe" ||
                            request.Find("params")?.Find("asset")?.AsString != asset.ToString() ||
                            request.Find("params")?.Find("expectedGeneration")?.AsString != asset.ToString())
                            throw new InvalidOperationException("unexpected rename method or identity");
                        if (scenario == "malformed") writer.WriteLine(Json.Write(envelope.Set("result", Codec.Ok(JsonValue.NewObject()))));
                        if (scenario == "legacy") writer.WriteLine(Json.Write(envelope.Set("error", JsonValue.NewObject()
                            .Set("code", JsonValue.String("BadRequest")).Set("message", JsonValue.String("unknown or malformed asset query")))));
                        return 1;
                    }
                    writer.WriteLine(Json.Write(envelope));
                }
                return 0;
            });
            using var client = RemoteSession.Connect(new SessionInfo((ushort)((System.Net.IPEndPoint)listener.LocalEndpoint).Port,
                "00112233445566778899aabbccddeeff", 1, asset), new RemoteOptions { ResponseTimeoutMilliseconds = 5000 });
            if (scenario == "legacy")
                t.Equal(A.RenameTextureError.Unsupported, client.Assets.RenameTexture(asset, new(asset), "blue_probe").Error,
                    "only legacy refusal maps to Unsupported");
            else
            {
                var failure = t.Throws<RemoteException>(() => client.Assets.RenameTexture(asset, new(asset), "blue_probe"), "rename " + scenario);
                t.Check(failure?.Error == (scenario == "lost" ? RemoteError.Disconnected : RemoteError.ProtocolError) &&
                    failure.Message.Contains("outcome unknown") && failure.Message.Contains(asset.ToString()),
                    "rename retains category, target and unknown outcome");
                var notSent = t.Throws<RemoteException>(() => client.Assets.RenameTexture(asset, new(asset), "not_sent"), "failed session");
                t.Check(notSent is not null && !notSent.Message.Contains("not_sent"), "known unsent has no unknown-write attribution");
            }
            t.Equal(1, peer.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult(), "rename sent at most once");
        }
    }

    // Minimal PNG header for wire checks. The SDK deliberately does not decode image data.
    internal static readonly byte[] PreviewHeader = [137, 80, 78, 71, 13, 10, 26, 10,
        0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 1, 0, 0, 0, 1, 0, 8, 6, 0, 0, 0, 0, 0, 0, 0];

    internal static JsonValue PreviewValue(AssetGuid guid, byte[] png) => JsonValue.NewObject()
        .Set("guid", Codec.Encode(guid)).Set("width", JsonValue.Number(256L)).Set("height", JsonValue.Number(256L))
        .Set("mimeType", JsonValue.String("image/png")).Set("pngBase64", JsonValue.String(Convert.ToBase64String(png)));

    internal static readonly AssetGuid PreviewMaterial = AssetGuid.Parse("11111111-2222-4333-8444-555555555555");

    internal static JsonValue PairValue(AssetGuid mesh, AssetGuid material, byte[] png) => JsonValue.NewObject()
        .Set("meshGuid", Codec.Encode(mesh)).Set("materialGuid", Codec.Encode(material))
        .Set("width", JsonValue.Number(256L)).Set("height", JsonValue.Number(256L))
        .Set("mimeType", JsonValue.String("image/png")).Set("pngBase64", JsonValue.String(Convert.ToBase64String(png)));

    private static void PairPreview(TestContext t, AssetGuid mesh)
    {
        AssetGuid material = PreviewMaterial;
        var value = PairValue(mesh, material, PreviewHeader);
        var decoded = Codec.DecodeMeshMaterialPreview(value, mesh, material);
        t.Check(decoded is { Width: 256, Height: 256 } && decoded.MeshGuid == mesh && decoded.MaterialGuid == material &&
            decoded.PngBytes.AsSpan().SequenceEqual(PreviewHeader), "pair PNG returns both correlated roles and owned image bytes");
        decoded!.PngBytes[0] = 0;
        t.Check(Codec.DecodeMeshMaterialPreview(value, mesh, material)?.PngBytes[0] == 137, "pair image bytes are owned by each result");
        t.Check(Codec.DecodeMeshMaterialPreview(value, material, mesh) is null, "pair roles cannot be swapped");
        t.Check(Codec.DecodeMeshMaterialPreview(value, material, material) is null, "pair mesh mismatch fails independently");
        t.Check(Codec.DecodeMeshMaterialPreview(value, mesh, mesh) is null, "pair material mismatch fails independently");
        foreach (var member in value.Members)
        {
            var duplicated = Json.Parse(Json.Write(value)[..^1] + ",\"" + member.Key + "\":" + Json.Write(member.Value) + "}");
            t.Check(Codec.DecodeMeshMaterialPreview(duplicated, mesh, material) is null,
                "pair rejects a repeated required image field " + member.Key);
        }
        t.Check(Codec.DecodeMeshMaterialPreview(PairValue(mesh, material, PreviewHeader).Set("height", JsonValue.Number(255L)),
            mesh, material) is null, "pair image requires fixed dimensions");
        t.Check(Codec.DecodeMeshMaterialPreview(PairValue(mesh, material, PreviewHeader).Set("mimeType", JsonValue.String("image/jpeg")),
            mesh, material) is null, "pair image requires PNG MIME");
        byte[] badHeader = (byte[])PreviewHeader.Clone();
        ++badHeader[23];
        t.Check(Codec.DecodeMeshMaterialPreview(PairValue(mesh, material, badHeader), mesh, material) is null,
            "pair image requires dimensions to match the IHDR");
        byte[] padded = [.. PreviewHeader, 0];
        string canonical = Convert.ToBase64String(padded);
        t.Check(Codec.DecodeMeshMaterialPreview(PairValue(mesh, material, padded).Set("pngBase64",
            JsonValue.String(canonical[..^3] + "B==")), mesh, material) is null, "pair rejects noncanonical unused base64 bits");
        const int cap = 512 * 1024;
        byte[] boundary = new byte[cap];
        PreviewHeader.CopyTo(boundary, 0);
        t.Check(Codec.DecodeMeshMaterialPreview(PairValue(mesh, material, boundary), mesh, material)?.PngBytes.Length == cap,
            "pair accepts the exact decoded byte cap");
        Array.Resize(ref boundary, cap + 1);
        var oversized = PairValue(mesh, material, boundary);
        t.Check(oversized.Find("pngBase64")?.AsString.Length == 699_052 &&
            Codec.DecodeMeshMaterialPreview(oversized, mesh, material) is null,
            "pair enforces the decoded cap even if the encoded length fits");
        foreach (var error in Enum.GetValues<A.MeshMaterialPreviewError>())
            t.Equal<A.MeshMaterialPreviewError?>(error, Codec.DecodeMeshMaterialPreviewError(JsonValue.String(error.ToString())),
                "typed pair refusal " + error);
        t.Check(Codec.DecodeMeshMaterialPreviewError(JsonValue.String("0")) is null &&
            Codec.DecodeMeshMaterialPreviewError(JsonValue.String("UnknownAsset")) is null, "pair errors are closed wire tokens");
    }

    private static void Preview(TestContext t, AssetGuid guid)
    {
        var value = PreviewValue(guid, PreviewHeader);
        var decoded = Codec.DecodeMeshPreview(value, guid);
        t.Check(decoded is { Width: 256, Height: 256 } && decoded.Guid == guid &&
                decoded.PngBytes.AsSpan().SequenceEqual(PreviewHeader), "preview header and bytes decode");
        decoded!.PngBytes[0] = 0;
        t.Check(Codec.DecodeMeshPreview(value, guid)?.PngBytes[0] == 137, "each preview result owns its bytes");
        t.Check(Codec.DecodeMeshPreview(value, AssetGuid.Parse("11111111-2222-4333-8444-555555555555")) is null,
            "mismatched preview GUID is refused");
        t.Check(Codec.DecodeMeshPreview(PreviewValue(guid, PreviewHeader).Set("mimeType", JsonValue.String("image/jpeg")), guid) is null,
            "wrong preview MIME is refused");
        t.Check(Codec.DecodeMeshPreview(PreviewValue(guid, PreviewHeader).Set("width", JsonValue.Number(255L)), guid) is null,
            "wrong preview dimensions are refused");
        foreach (int byteIndex in new[] { 0, 11, 12, 19, 23 })
        {
            byte[] malformed = (byte[])PreviewHeader.Clone();
            ++malformed[byteIndex];
            t.Check(Codec.DecodeMeshPreview(PreviewValue(guid, malformed), guid) is null, "invalid preview PNG header byte " + byteIndex);
        }
        t.Check(Codec.DecodeMeshPreview(PreviewValue(guid, PreviewHeader[..^1]), guid) is null, "truncated first PNG chunk is refused");
        byte[] padded = [.. PreviewHeader, 0];
        string canonical = Convert.ToBase64String(padded);
        t.Check(Codec.DecodeMeshPreview(PreviewValue(guid, padded), guid) is not null, "canonical padded base64 is accepted");
        foreach (string invalid in new[] { canonical[..^3] + "B==", canonical.TrimEnd('='), canonical[..8] + " " + canonical[9..] })
            t.Check(Codec.DecodeMeshPreview(PreviewValue(guid, padded).Set("pngBase64", JsonValue.String(invalid)), guid) is null,
                "noncanonical preview base64 is refused");
        const int limit = 512 * 1024;
        byte[] boundary = new byte[limit];
        PreviewHeader.CopyTo(boundary, 0);
        t.Check(Codec.DecodeMeshPreview(PreviewValue(guid, boundary), guid)?.PngBytes.Length == limit,
            "512 KiB decoded limit is inclusive");
        Array.Resize(ref boundary, limit + 1);
        JsonValue oversized = PreviewValue(guid, boundary);
        t.Check(oversized.Find("pngBase64")?.AsString.Length == 699_052 && Codec.DecodeMeshPreview(oversized, guid) is null,
            "decoded limit applies even when encoded character length fits");
        foreach (var error in Enum.GetValues<A.PreviewError>())
            t.Equal<A.PreviewError?>(error, Codec.DecodePreviewError(JsonValue.String(error.ToString())), "typed preview refusal " + error);
        t.Check(Codec.DecodePreviewError(JsonValue.String("0")) is null && Codec.DecodePreviewError(JsonValue.String("Future")) is null,
            "preview errors are closed wire tokens");
    }
}
