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
    }
}
