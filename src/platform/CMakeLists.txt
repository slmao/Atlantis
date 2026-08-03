add_library(atlantis_platform STATIC
  src/clock.cpp
  src/platform_event.cpp
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
  # Not implemented — Android build/toolchain support does not exist yet.
  # See specs/0001-project-foundation.md and
  # specs/0002-platform-foundation.md Non-Goals.
endif()
