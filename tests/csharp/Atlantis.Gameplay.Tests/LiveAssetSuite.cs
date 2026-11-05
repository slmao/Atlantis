using System;
using System.Linq;
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
}
