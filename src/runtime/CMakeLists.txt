# Plan 0013 Step 1 / ADR-0047: atlantis_runtime_host is a PRIVATE
# composition library -- it exists solely so this module's own
# lifecycle/composition logic is unit-testable and so main.cpp stays
# thin. No other top-level module may depend on Atlantis::RuntimeHost,
# and no install()/export rule is added for it (Plan 0013 Section D1a).
add_library(atlantis_runtime_host STATIC
  src/lifecycle_state.cpp
  src/exit_reason.cpp
  src/init_error.cpp
  src/error_classification.cpp
  src/platform_session.cpp
  src/runtime_application.cpp
)
add_library(Atlantis::RuntimeHost ALIAS atlantis_runtime_host)

target_include_directories(atlantis_runtime_host
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Plan 0013 Section D1: the full dependency list this module's real
# composition needs -- Core, Platform, RHI, Vulkan Backend, Renderer,
# Shader System (both targets), Asset System. Deliberately NOT
# Atlantis::RenderGraph: Renderer::drawFrame() already owns RenderGraph
# construction/compilation/execution internally (Spec 0013's own
# Independent Review finding).
target_link_libraries(atlantis_runtime_host
  PUBLIC
    Atlantis::Core
    Atlantis::Platform
    Atlantis::RHI
    Atlantis::VulkanBackend
    Atlantis::Renderer
    Atlantis::ShaderSystem
    Atlantis::ShaderSystemRhiIntegration
    Atlantis::AssetSystem
  PRIVATE
    atlantis_compiler_warnings
)

# Plan 0013 Section D1/D3 (2026-08-21 amendment): real, compile-time
# enum-exhaustiveness protection for classifyPresentationError()/
# classifySubmitError()'s own no-default switches requires MSVC's C4062
# ("enumerator ... not handled"), which is off by default even at /W4.
# Scoped to this target only -- cmake/CompilerWarnings.cmake (shared by
# every target in this repository) is deliberately not touched. The
# repository's existing, already-present /WX promotes C4062 to a hard
# build error once this target opts into seeing it.
target_compile_options(atlantis_runtime_host PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)
