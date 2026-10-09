# Plan 0058 M1 (Spec 0058 R6, ruling Q8; J3): the atlantis.remote/1
# conformance vectors -- C++ only, no .NET.
add_subdirectory(conformance)

# Plan 0058 P9 (J9): the C# rules -- no NuGet, no native interop, eol=lf --
# checked with or without .NET.
add_test(NAME "csharp: rules"
  COMMAND ${CMAKE_COMMAND} -DATLANTIS_SOURCE_DIR=${CMAKE_SOURCE_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/check_csharp_rules.cmake)
set_tests_properties("csharp: rules" PROPERTIES LABELS csharp)

# Plan 0058 P9 (ruling Q7): the C# SDK's tests, one ctest per suite of the
# one test program, against the GPU-free fixture server -- declared only when
# a .NET 10.0.1xx SDK was found (cmake/AtlantisDotnet.cmake).
if(ATLANTIS_CSHARP_ENABLED)
  add_subdirectory(fixture_server)

  # After the library's own target: the test build reuses its outputs.
  atlantis_add_dotnet_project(
    NAME atlantis_csharp_tests
    PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Gameplay.Tests/Atlantis.Gameplay.Tests.csproj"
    SOURCE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Gameplay.Tests"
                "${CMAKE_CURRENT_SOURCE_DIR}/Generated"
                "${CMAKE_SOURCE_DIR}/src/csharp/Atlantis.Gameplay"
    DEPENDS atlantis_csharp_gameplay)
  add_dependencies(atlantis_csharp_tests atlantis_csharp_fixture_server)

  foreach(suite IN ITEMS codec session live.connection)
    add_test(NAME "csharp: ${suite}"
      COMMAND "${ATLANTIS_DOTNET}" "${atlantis_csharp_tests_DLL}" ${suite}
              --repo "${CMAKE_SOURCE_DIR}"
              --fixture-server "$<TARGET_FILE:atlantis_csharp_fixture_server>")
    set_tests_properties("csharp: ${suite}" PROPERTIES LABELS csharp TIMEOUT 600)
  endforeach()
endif()
