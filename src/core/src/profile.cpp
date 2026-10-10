#include <atlantis/profile.h>

// Spec 0059 / ADR-0114, ADR-0116: the macros' out-of-line machinery and the
// Tracy sink. The only Core file that includes Tracy, and only through its C
// API, so profile.h can stay Tracy-free. An empty translation unit when
// ATLANTIS_PROFILING is off.
#if defined(ATLANTIS_PROFILING)

#if defined(ATLANTIS_PROFILING_TRACY)
#include <tracy/TracyC.h>

#include <cstddef>
#include <cstring>

// profile.h's SourceLocation is handed to Tracy as its own location type.
static_assert(sizeof(atlantis::profile::SourceLocation) == sizeof(___tracy_source_location_data));
static_assert(offsetof(atlantis::profile::SourceLocation, name) == offsetof(___tracy_source_location_data, name));
static_assert(offsetof(atlantis::profile::SourceLocation, function) ==
              offsetof(___tracy_source_location_data, function));
static_assert(offsetof(atlantis::profile::SourceLocation, file) == offsetof(___tracy_source_location_data, file));
static_assert(offsetof(atlantis::profile::SourceLocation, line) == offsetof(___tracy_source_location_data, line));
static_assert(offsetof(atlantis::profile::SourceLocation, color) == offsetof(___tracy_source_location_data, color));
#endif

namespace atlantis::profile {

namespace detail {

namespace {

NameId siteId(Site& site) {
  if (site.id == kInvalidName) site.id = internName(site.location.name);
  return site.id;
}

#if defined(ATLANTIS_PROFILING_TRACY)
// ATL_PROFILE_ADD accumulators, plotted once per frame at the frame boundary
// (a gauge is plotted where it is written). Indexed by interned name id.
struct TracyAdds {
  std::array<double, kMaxNames> sum{};
  std::array<const char*, kMaxNames> name{};
  std::array<NameId, kMaxNames> touched{};
  std::size_t touchedCount = 0;
  std::array<bool, kMaxNames> isTouched{};
};

TracyAdds& tracyAdds() {
  static TracyAdds adds;
  return adds;
}

void flushTracyAdds() {
  TracyAdds& adds = tracyAdds();
  for (std::size_t i = 0; i < adds.touchedCount; ++i) {
    const NameId id = adds.touched[i];
    ___tracy_emit_plot(adds.name[id], adds.sum[id]);
    adds.sum[id] = 0.0;
    adds.isTouched[id] = false;
  }
  adds.touchedCount = 0;
}
#endif

}  // namespace

void counterValueSlow(Site& site, double value) noexcept {
  const NameId id = siteId(site);
  if (isRecording()) ::atlantis::profile::counterValue(id, value);
#if defined(ATLANTIS_PROFILING_TRACY)
  ___tracy_emit_plot(site.location.name, value);
#endif
}

void counterAddSlow(Site& site, double delta) noexcept {
  const NameId id = siteId(site);
  if (isRecording()) ::atlantis::profile::counterAdd(id, delta);
#if defined(ATLANTIS_PROFILING_TRACY)
  if (id < kMaxNames) {
    TracyAdds& adds = tracyAdds();
    if (!adds.isTouched[id]) {
      adds.isTouched[id] = true;
      adds.name[id] = site.location.name;
      adds.touched[adds.touchedCount++] = id;
    }
    adds.sum[id] += delta;
  }
#endif
}

}  // namespace detail

void Zone::begin(detail::Site& site) noexcept {
  open_ = true;
  if (isRecording()) {
    id_ = detail::siteId(site);
    startNs_ = zoneBegin(id_);
    recorded_ = true;
  }
#if defined(ATLANTIS_PROFILING_TRACY)
  const TracyCZoneCtx ctx =
      ___tracy_emit_zone_begin(reinterpret_cast<const ___tracy_source_location_data*>(&site.location), 1);
  tracyId_ = ctx.id;
  tracyActive_ = ctx.active;
#endif
}

void Zone::beginDynamic(std::string_view name) noexcept {
  open_ = true;
  if (isRecording()) {
    id_ = internName(name);
    startNs_ = zoneBegin(id_);
    recorded_ = true;
  }
#if defined(ATLANTIS_PROFILING_TRACY)
  const std::uint64_t srcloc = ___tracy_alloc_srcloc_name(0, "", 0, "", 0, name.data(), name.size(), 0);
  const TracyCZoneCtx ctx = ___tracy_emit_zone_begin_alloc(srcloc, 1);
  tracyId_ = ctx.id;
  tracyActive_ = ctx.active;
#endif
}

void Zone::finish() noexcept {
  open_ = false;
  if (recorded_) zoneEnd(id_, startNs_);
#if defined(ATLANTIS_PROFILING_TRACY)
  TracyCZoneCtx ctx;
  ctx.id = tracyId_;
  ctx.active = tracyActive_;
  ___tracy_emit_zone_end(ctx);
#endif
}

bool FrameScope::markFrame() noexcept {
  beginFrame();
#if defined(ATLANTIS_PROFILING_TRACY)
  detail::flushTracyAdds();
  ___tracy_emit_frame_mark(nullptr);
#endif
  return true;
}

}  // namespace atlantis::profile

#endif  // ATLANTIS_PROFILING
