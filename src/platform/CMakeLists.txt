add_library(atlantis_platform STATIC
  src/clock.cpp
  src/platform_event.cpp
  # Plan 0034 Milestone 2: both files are Android-domain (ADR-0077's own
  # APP_CMD_* mapping table / android_app* injection) but deliberately
  # contain no Android NDK header -- unconditional here so
  # tests/platform/'s whitebox unit tests can link and exercise this
  # translation logic on any host, matching this Spec's own Testing &
  # Verification Plan. Neither file defines any atlantis::platform public
  # free function, so there is no symbol collision with
  # src/windows/windows_platform.cpp below on a Windows build.
  src/android/app_command_mapping.cpp
  src/android/android_app_injection.cpp
)
add_library(Atlantis::Platform ALIAS atlantis_platform)

target_include_directories(atlantis_platform
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_platform
  PUBLIC
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)

if(WIN32)
  target_sources(atlantis_platform PRIVATE src/windows/windows_platform.cpp)
elseif(ANDROID)
  # Plan 0034 Milestone 2 (ADR-0077): android_native_app_glue is the NDK's
  # own static helper library, shipped as source (not a prebuilt), built
  # here following the NDK's own recommended CMake pattern -- a
  # project-owned target, not a new third-party dependency (it ships
  # inside the already-required NDK).
  # android_native_app_glue.c is real, unmodified C (e.g. an implicit
  # void* -> struct android_app* conversion in its own android_app_create(),
  # legal C, not legal C++) -- compiling it as C++ (an initial attempt,
  # abandoned) fails on this repository's own real NDK source, confirmed
  # by a real Android build attempt. The top-level CMakeLists.txt's
  # project() call now declares C (see that file's own comment) so this
  # file compiles as what it actually is.
  add_library(atlantis_android_native_app_glue STATIC
    ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
  )
  target_include_directories(atlantis_android_native_app_glue PUBLIC
    ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue
  )

  target_sources(atlantis_platform PRIVATE src/android/android_platform.cpp)
  target_link_libraries(atlantis_platform
    PRIVATE
      atlantis_android_native_app_glue
      log
      android
  )

  # Plan 0034 Milestone 5 (ADR-0077 amendment): resolves
  # android_app_injection.h's own flagged open question -- a future
  # atlantis_runtime_android (a different top-level module, Atlantis
  # Runtime) needs to call setAndroidApp(), but src/platform/src/ is
  # structurally outside every consumer's include path (ADR-0010).
  # atlantis_platform_android_entry is a narrow, single-consumer
  # extension point: an INTERFACE target (no sources of its own) whose
  # only purpose is to grant include-path visibility into
  # src/platform/src/android/ -- it is not linked into anything, does
  # not become part of Atlantis::Platform's own PUBLIC interface, and
  # is named for exactly what it is (an Android-only Runtime entry-point
  # concern), not a general-purpose escape hatch other modules might
  # reach for.
  add_library(atlantis_platform_android_entry INTERFACE)
  add_library(Atlantis::PlatformAndroidEntry ALIAS atlantis_platform_android_entry)
  target_include_directories(atlantis_platform_android_entry INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/android
  )
endif()
