add_library(atlantis_core STATIC
  src/log.cpp
  src/assert.cpp
  src/schema.cpp
  src/profile.cpp
  src/profile_recorder.cpp
  src/profile_stats.cpp
)
add_library(Atlantis::Core ALIAS atlantis_core)

target_include_directories(atlantis_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_core
  PRIVATE
    atlantis_compiler_warnings
)

# Spec 0059 / ADR-0114 D2-D3: the three options are PUBLIC compile definitions of
# Core, the one source for every module. No public header outside
# atlantis/profile*.h tests them (tests/core/profile_boundary_tests.cpp).
if(ATLANTIS_PROFILING)
  target_compile_definitions(atlantis_core PUBLIC ATLANTIS_PROFILING=1)
  if(ATLANTIS_PROFILING_TRACY_EFFECTIVE)
    target_compile_definitions(atlantis_core PUBLIC ATLANTIS_PROFILING_TRACY=1)
  endif()
endif()
if(ATLANTIS_GPU_MARKERS)
  target_compile_definitions(atlantis_core PUBLIC ATLANTIS_GPU_MARKERS=1)
endif()

# Plan 0059 M2 (ADR-0116 D3): Tracy is linked privately; only profile.cpp includes it.
if(ATLANTIS_PROFILING AND ATLANTIS_PROFILING_TRACY_EFFECTIVE)
  target_link_libraries(atlantis_core PRIVATE atlantis_tracy)
endif()
