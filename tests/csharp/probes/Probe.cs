// Plan 0058 P9 (Spec 0058 R3; ADR-0112 D3; J5): what the typed layer refuses
// at compile time. Built once per probe with -p:DefineConstants=<PROBE>; each
// refused statement has a compiling twin, and run_probe.cmake checks that
// the refused build fails on the marked line of this file.
using System.Numerics;
using S = Atlantis.Gameplay.Synthetic;
using W = Atlantis.Gameplay.World;

namespace Atlantis.Gameplay.Probes;

public static class Probe
{
    public static void Use(GameplayWorld world, Transaction transaction, EntityGuid entity)
    {
#if PROBE_READONLY_SET
        world.Set(entity, S.Fields.Locked.Fixed, 1.0f);  // probe: PROBE_READONLY_SET
#endif
#if PROBE_READONLY_SET_TWIN
        world.Set(entity, S.Fields.Locked.Free, 1.0f);  // probe: PROBE_READONLY_SET_TWIN
#endif
#if PROBE_READONLY_ADD
        transaction.Add(entity, new S.Locked());  // probe: PROBE_READONLY_ADD
#endif
#if PROBE_READONLY_ADD_TWIN
        transaction.Add(entity, new S.Probe()).Add<S.Locked>(entity);  // probe: PROBE_READONLY_ADD_TWIN
#endif
#if PROBE_WRONG_TYPE
        world.Set(entity, W.Fields.Light.Intensity, new Vector3(1.0f, 2.0f, 3.0f));  // probe: PROBE_WRONG_TYPE
#endif
#if PROBE_WRONG_TYPE_TWIN
        world.Set(entity, W.Fields.Light.Color, new Vector3(1.0f, 2.0f, 3.0f));  // probe: PROBE_WRONG_TYPE_TWIN
#endif
    }
}
