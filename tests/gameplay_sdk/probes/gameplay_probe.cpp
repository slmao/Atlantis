// Plan 0057 M3 (Spec 0057 R3, R10; J3): three compile-failure probes, each
// with a twin that must compile. The build defines one ATLANTIS_PROBE_* and
// ATLANTIS_PROBE_TWIN (0: the offending line; 1: its compiling twin); the
// two differ only in that line, so the failing build is attributable to it
// (tests/gameplay_sdk/CMakeLists.txt matches the error to this file).

#include <atlantis/gameplay/generated/world.h>
#include <atlantis/gameplay/transaction.h>
#include <atlantis/gameplay/world.h>

#include "generated/synthetic.h"

#include <array>

namespace w = atlantis::gameplay::world;
namespace syn = atlantis::gameplay::synthetic;

void atlantisGameplayProbe(atlantis::gameplay::World& world, atlantis::gameplay::Transaction& tx,
                           const atlantis::gameplay::EntityGuid& entity) {
  (void)world;
  (void)tx;
  (void)entity;
#if defined(ATLANTIS_PROBE_WRONG_KIND_SET)
#if ATLANTIS_PROBE_TWIN
  (void)world.set(entity, w::fields::Light.intensity, 1.0f);
#else
  (void)world.set(entity, w::fields::Light.intensity, std::array<float, 3>{});  // a Vec3 into a Float32 leaf
#endif
#elif defined(ATLANTIS_PROBE_READ_ONLY_SET)
#if ATLANTIS_PROBE_TWIN
  (void)world.set(entity, syn::fields::Locked.free, 1.0f);
#else
  (void)world.set(entity, syn::fields::Locked.fixed, 1.0f);  // a ReadOnlyField
#endif
#elif defined(ATLANTIS_PROBE_READ_ONLY_ADD)
#if ATLANTIS_PROBE_TWIN
  tx.add(entity, syn::Probe{});
#else
  tx.add(entity, syn::Locked{});  // a type with a read-only leaf
#endif
#else
#error "no ATLANTIS_PROBE_* defined"
#endif
}
