# Spec 0070 (Plan 0070 M1; ADR-0138 D1/D2): Atlantis MCP, the agent tool
# layer's host, built by `dotnet build` like the C# SDK (ADR-0113 D3).
# Declared only when ATLANTIS_BUILD_MCP is ON (default OFF) and a .NET
# 10.0.1xx SDK was found; never on Android. Its one package (the official
# MCP C# SDK, J1) is restored in locked mode from nuget.org, or from
# ATLANTIS_NUGET_OFFLINE_FEED when set, under this area's own allow-list
# (ADR-0127's mechanism, Spec 0070 Plan J5).

set(ATLANTIS_NUGET_OFFLINE_FEED "$ENV{ATLANTIS_NUGET_OFFLINE_FEED}" CACHE PATH
    "Restore Atlantis MCP's packages from this directory instead of nuget.org (ADR-0138 D2)")

set(_atlantis_mcp_restore "")
if(ATLANTIS_NUGET_OFFLINE_FEED)
  set(_atlantis_mcp_restore "-p:RestoreSources=${ATLANTIS_NUGET_OFFLINE_FEED}")
  message(STATUS "Atlantis MCP: packages from the offline feed ${ATLANTIS_NUGET_OFFLINE_FEED}")
endif()

# Like atlantis_add_dotnet_project (cmake/AtlantisDotnet.cmake), plus this
# area's policy files as inputs; project references are built by their own
# targets (BuildProjectReferences=false), so the shared C# SDK is built once.
set(_mcp_root "${CMAKE_BINARY_DIR}/csharp/$<CONFIG>")
set(atlantis_mcp_DLL "${_mcp_root}/Atlantis.Mcp/Atlantis.Mcp.dll")
file(GLOB_RECURSE _mcp_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Mcp/*.cs"
  "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Mcp/*.csproj"
  "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Mcp/packages.lock.json")
list(FILTER _mcp_sources EXCLUDE REGEX "/(bin|obj)/")
list(APPEND _mcp_sources
  "${CMAKE_CURRENT_SOURCE_DIR}/Directory.Packages.props"
  "${CMAKE_CURRENT_SOURCE_DIR}/NuGet.config"
  "${CMAKE_SOURCE_DIR}/global.json"
  "${CMAKE_SOURCE_DIR}/Directory.Build.props")
add_custom_command(
  OUTPUT "${atlantis_mcp_DLL}"
  COMMAND ${CMAKE_COMMAND} -E env --unset=LIB DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
          DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
          "${ATLANTIS_DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Mcp/Atlantis.Mcp.csproj" -c $<CONFIG>
          "-p:AtlantisCSharpBuildRoot=${_mcp_root}" -p:BuildProjectReferences=false ${_atlantis_mcp_restore}
          --nologo -nodeReuse:false -v:minimal
  COMMAND ${CMAKE_COMMAND} -E touch "${atlantis_mcp_DLL}"
  DEPENDS ${_mcp_sources} atlantis_csharp_gameplay
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "dotnet build Atlantis.Mcp ($<CONFIG>)"
  VERBATIM)
add_custom_target(atlantis_mcp ALL DEPENDS "${atlantis_mcp_DLL}")
set(atlantis_mcp_DLL "${atlantis_mcp_DLL}" CACHE INTERNAL "Atlantis MCP's built dll")

# After every other `dotnet build` that touches the C# SDK's project.
foreach(_other IN ITEMS atlantis_csharp_tests atlantis_csharp_gameplay_demo atlantis_editor_model
                    atlantis_editor_app atlantis_editor_app_tests)
  if(TARGET ${_other})
    add_dependencies(atlantis_mcp ${_other})
  endif()
endforeach()

if(ATLANTIS_BUILD_TESTS)
  # ADR-0138 D2 (Plan 0070 J5): the package policy, one script over both managed
  # areas, and that it refuses what it must for this one too.
  add_test(NAME "mcp: package policy"
    COMMAND ${CMAKE_COMMAND} -DATLANTIS_SOURCE_DIR=${CMAKE_SOURCE_DIR} -DAREA_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/check_editor_app_packages.cmake)
  add_test(NAME "mcp: package policy refuses unpinned, unlisted and unlocked packages"
    COMMAND ${CMAKE_COMMAND} -DATLANTIS_SOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/policy_cases
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_policy_negative.cmake)
  set_tests_properties("mcp: package policy"
    "mcp: package policy refuses unpinned, unlisted and unlocked packages"
    PROPERTIES LABELS mcp TIMEOUT 600)
endif()
