#pragma once

// Spec 0059 / ADR-0114 D1-D2: the profiling abstraction. The only header
// business code includes. It names no Tracy and no Vulkan type.
//
//   ATL_PROFILE_FRAME()                    frame boundary + the "Frame" zone
//   ATL_PROFILE_SCOPE(name)                a CPU zone to the end of the scope
//   ATL_PROFILE_SCOPE_DYNAMIC(string_view) the same, for a runtime-built name
//   ATL_PROFILE_SCOPE_NAMED(var, name)     a zone ended early by
//   ATL_PROFILE_SCOPE_END(var)             this (idempotent; the destructor
//                                          ends it otherwise)
//   ATL_PROFILE_GPU_SCOPE(cmd, name)       a GPU region on an rhi::CommandList
//   ATL_PROFILE_VALUE(name, v)             a per-frame gauge, last write wins
//   ATL_PROFILE_ADD(name, delta)           a per-frame accumulator
//   ATL_DEBUG_NAME(expr)                   expr as a string_view when GPU markers
//                                          are on, an empty one otherwise
//
// `name` is a string literal unless stated. With ATLANTIS_PROFILING undefined
// every ATL_PROFILE_* macro expands to ((void)0): its arguments are NOT
// evaluated, so an argument with a side effect is a bug the reviewer must
// catch. ATL_PROFILE_GPU_SCOPE is live when ATLANTIS_PROFILING or
// ATLANTIS_GPU_MARKERS is defined; ATL_DEBUG_NAME evaluates `expr` only with
// ATLANTIS_GPU_MARKERS, and the temporary it may create lives to the end of the
// full expression it appears in (use it directly as a call argument).
//
// No public struct layout or virtual-function set in any module depends on
// these options: they are tested only in this header and in private sources.
//
// Thread-safety: frame thread only (ADR-0004); see profile_recorder.h.

#include <string_view>

#define ATL_PROFILE_CONCAT_IMPL(a, b) a##b
#define ATL_PROFILE_CONCAT(a, b) ATL_PROFILE_CONCAT_IMPL(a, b)

#if defined(ATLANTIS_PROFILING)

#include <atlantis/profile_recorder.h>

#include <cstdint>

namespace atlantis::profile {

// Laid out like Tracy's ___tracy_source_location_data (static_assert in
// profile.cpp), so the Tracy sink can pass it straight through its C API.
struct SourceLocation {
  const char* name;
  const char* function;
  const char* file;
  std::uint32_t line;
  std::uint32_t color;
};

namespace detail {

// One per macro call site, function-local static: the name is interned once.
struct Site {
  SourceLocation location;
  NameId id = kInvalidName;
};

// True if a consumer (the recorder, or Tracy when compiled in) wants events.
[[nodiscard]] inline bool consumersActive() noexcept {
#if defined(ATLANTIS_PROFILING_TRACY)
  return true;
#else
  return gRecording;
#endif
}

void counterValueSlow(Site& site, double value) noexcept;
void counterAddSlow(Site& site, double delta) noexcept;

inline void counterValue(Site& site, double value) noexcept {
  if (consumersActive()) counterValueSlow(site, value);
}
inline void counterAdd(Site& site, double delta) noexcept {
  if (consumersActive()) counterAddSlow(site, delta);
}

}  // namespace detail

// A CPU zone. Ends at destruction or at end(), whichever comes first.
class Zone {
 public:
  explicit Zone(detail::Site& site) noexcept {
    if (detail::consumersActive()) begin(site);
  }
  explicit Zone(std::string_view dynamicName) noexcept {
    if (detail::consumersActive()) beginDynamic(dynamicName);
  }
  ~Zone() { end(); }
  Zone(const Zone&) = delete;
  Zone& operator=(const Zone&) = delete;

  void end() noexcept {
    if (open_) finish();
  }

 private:
  void begin(detail::Site& site) noexcept;
  void beginDynamic(std::string_view name) noexcept;
  void finish() noexcept;

  NameId id_ = kInvalidName;
  std::uint64_t startNs_ = 0;
  std::uint32_t tracyId_ = 0;
  int tracyActive_ = 0;
  bool open_ = false;
  bool recorded_ = false;
};

// ATL_PROFILE_FRAME(): closes the previous frame, marks the next one, and
// opens the "Frame" zone for the enclosing scope.
class FrameScope {
 public:
  explicit FrameScope(detail::Site& site) noexcept : started_(markFrame()), zone_(site) {}

 private:
  static bool markFrame() noexcept;
  [[maybe_unused]] bool started_;
  Zone zone_;
};

}  // namespace atlantis::profile

// Macros that need two names built from one counter value pass __COUNTER__
// through a helper, so both uses see the same number (and nothing depends on
// __LINE__, which MSVC's /ZI makes a non-constant).
#define ATL_PROFILE_SITE_(var, label)                                                \
  static ::atlantis::profile::detail::Site var {                                     \
    { label, __func__, __FILE__, static_cast<std::uint32_t>(__LINE__), 0u },         \
        ::atlantis::profile::kInvalidName                                            \
  }

#define ATL_PROFILE_FRAME()                          \
  ATL_PROFILE_SITE_(atl_frame_site_, "Frame");       \
  ::atlantis::profile::FrameScope atl_frame_scope_ { atl_frame_site_ }

#define ATL_PROFILE_SCOPE_IMPL_(name, id)                            \
  ATL_PROFILE_SITE_(ATL_PROFILE_CONCAT(atl_zone_site_, id), name);   \
  ::atlantis::profile::Zone ATL_PROFILE_CONCAT(atl_zone_, id) {      \
    ATL_PROFILE_CONCAT(atl_zone_site_, id)                           \
  }
#define ATL_PROFILE_SCOPE(name) ATL_PROFILE_SCOPE_IMPL_(name, __COUNTER__)

#define ATL_PROFILE_SCOPE_DYNAMIC_IMPL_(name, id) \
  ::atlantis::profile::Zone ATL_PROFILE_CONCAT(atl_zone_, id) { std::string_view(name) }
#define ATL_PROFILE_SCOPE_DYNAMIC(name) ATL_PROFILE_SCOPE_DYNAMIC_IMPL_(name, __COUNTER__)

#define ATL_PROFILE_SCOPE_NAMED(var, name)                     \
  ATL_PROFILE_SITE_(ATL_PROFILE_CONCAT(var, _site_), name);    \
  ::atlantis::profile::Zone var { ATL_PROFILE_CONCAT(var, _site_) }

#define ATL_PROFILE_SCOPE_END(var) (var).end()

#define ATL_PROFILE_VALUE(name, value)                                                       \
  do {                                                                                       \
    ATL_PROFILE_SITE_(atl_value_site_, name);                                                \
    ::atlantis::profile::detail::counterValue(atl_value_site_, static_cast<double>(value));  \
  } while (false)

#define ATL_PROFILE_ADD(name, delta)                                                         \
  do {                                                                                       \
    ATL_PROFILE_SITE_(atl_add_site_, name);                                                  \
    ::atlantis::profile::detail::counterAdd(atl_add_site_, static_cast<double>(delta));      \
  } while (false)

#else  // !ATLANTIS_PROFILING

#define ATL_PROFILE_FRAME() ((void)0)
#define ATL_PROFILE_SCOPE(name) ((void)0)
#define ATL_PROFILE_SCOPE_DYNAMIC(name) ((void)0)
#define ATL_PROFILE_SCOPE_NAMED(var, name) ((void)0)
#define ATL_PROFILE_SCOPE_END(var) ((void)0)
#define ATL_PROFILE_VALUE(name, value) ((void)0)
#define ATL_PROFILE_ADD(name, delta) ((void)0)

#endif  // ATLANTIS_PROFILING

#if defined(ATLANTIS_PROFILING) || defined(ATLANTIS_GPU_MARKERS)

namespace atlantis::profile::detail {

// Brackets a region on a command list. Cmd is rhi::CommandList; it is a
// template parameter only so that Core need not include RHI (Core depends on
// nothing in Atlantis).
template <class Cmd>
class GpuScope {
 public:
  GpuScope(Cmd& cmd, std::string_view name) : cmd_(cmd) { cmd_.beginRegion(name); }
  ~GpuScope() { cmd_.endRegion(); }
  GpuScope(const GpuScope&) = delete;
  GpuScope& operator=(const GpuScope&) = delete;

 private:
  Cmd& cmd_;
};

}  // namespace atlantis::profile::detail

#define ATL_PROFILE_GPU_SCOPE_IMPL_(cmd, name, id)                                \
  ::atlantis::profile::detail::GpuScope ATL_PROFILE_CONCAT(atl_gpu_scope_, id) {  \
    (cmd), std::string_view(name)                                                 \
  }
#define ATL_PROFILE_GPU_SCOPE(cmd, name) ATL_PROFILE_GPU_SCOPE_IMPL_(cmd, name, __COUNTER__)

#else

#define ATL_PROFILE_GPU_SCOPE(cmd, name) ((void)0)

#endif

#if defined(ATLANTIS_GPU_MARKERS)
#define ATL_DEBUG_NAME(expr) (std::string_view(expr))
#else
#define ATL_DEBUG_NAME(expr) (std::string_view{})
#endif
