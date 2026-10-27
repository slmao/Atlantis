# Spec 0064 Phase 1 (Plan 0064 P4; ADR-0125 D1, ADR-0127 D6): Atlantis Editor
# App, the out-of-process Avalonia editor, built by `dotnet build` like the C#
# SDK (ADR-0113 D3). Declared only when ATLANTIS_BUILD_EDITOR_APP is ON (default
# OFF) and a .NET 10.0.1xx SDK was found; never on Android. Its packages are
# restored in locked mode from nuget.org, or from ATLANTIS_NUGET_OFFLINE_FEED
# when set (a directory of the locked .nupkg files; ADR-0127 D3, D7).
# Avalonia's build-time telemetry is opted out (AVALONIA_TELEMETRY_OPTOUT).

set(ATLANTIS_NUGET_OFFLINE_FEED "$ENV{ATLANTIS_NUGET_OFFLINE_FEED}" CACHE PATH
    "Restore Atlantis Editor App's packages from this directory instead of nuget.org (ADR-0127 D3)")
set(_atlantis_editor_restore "")
if(ATLANTIS_NUGET_OFFLINE_FEED)
  set(_atlantis_editor_restore "-p:RestoreSources=${ATLANTIS_NUGET_OFFLINE_FEED}")
  message(STATUS "Atlantis Editor App: packages from the offline feed ${ATLANTIS_NUGET_OFFLINE_FEED}")
endif()

# atlantis_add_editor_app_project(NAME <target> PROJECT <csproj> SOURCE_DIR <dir> [DEPENDS <target>...]
#                                 [INPUTS <file>...])
# Like atlantis_add_dotnet_project (cmake/AtlantisDotnet.cmake), plus the
# policy files as inputs; project references are built by their own targets
# (BuildProjectReferences=false), so the shared C# SDK is built once. INPUTS
# are files whose change rebuilds the project -- a referenced project's DLL,
# which a target-level DEPENDS orders but does not track (Plan 0068 M1).
function(atlantis_add_editor_app_project)
  cmake_parse_arguments(ARG "" "NAME;PROJECT;SOURCE_DIR" "DEPENDS;INPUTS" ${ARGN})
  get_filename_component(_project_name "${ARG_PROJECT}" NAME_WLE)
  set(_root "${CMAKE_BINARY_DIR}/csharp/$<CONFIG>")
  set(_dll "${_root}/${_project_name}/${_project_name}.dll")
  file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
    "${ARG_SOURCE_DIR}/*.cs" "${ARG_SOURCE_DIR}/*.csproj" "${ARG_SOURCE_DIR}/packages.lock.json"
    "${ARG_SOURCE_DIR}/*.manifest")
  list(FILTER _sources EXCLUDE REGEX "/(bin|obj)/")
  list(APPEND _sources
    "${CMAKE_CURRENT_SOURCE_DIR}/Directory.Packages.props"
    "${CMAKE_CURRENT_SOURCE_DIR}/NuGet.config"
    "${CMAKE_SOURCE_DIR}/global.json"
    "${CMAKE_SOURCE_DIR}/Directory.Build.props")
  add_custom_command(
    OUTPUT "${_dll}"
    COMMAND ${CMAKE_COMMAND} -E env --unset=LIB DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
            DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 AVALONIA_TELEMETRY_OPTOUT=1
            "${ATLANTIS_DOTNET}" build "${ARG_PROJECT}" -c $<CONFIG>
            "-p:AtlantisCSharpBuildRoot=${_root}" -p:BuildProjectReferences=false ${_atlantis_editor_restore}
            --nologo -nodeReuse:false -v:minimal
    COMMAND ${CMAKE_COMMAND} -E touch "${_dll}"
    DEPENDS ${_sources} ${ARG_DEPENDS} ${ARG_INPUTS}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "dotnet build ${_project_name} ($<CONFIG>)"
    VERBATIM)
  add_custom_target(${ARG_NAME} ALL DEPENDS "${_dll}")
  if(ARG_DEPENDS)
    add_dependencies(${ARG_NAME} ${ARG_DEPENDS})
  endif()
  set(${ARG_NAME}_DLL "${_dll}" PARENT_SCOPE)
endfunction()

# Plan 0068 M1 (J5): the editor's models, a library with no Avalonia reference.
atlantis_add_editor_app_project(
  NAME atlantis_editor_model
  PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Editor.Model/Atlantis.Editor.Model.csproj"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Editor.Model"
  DEPENDS atlantis_csharp_gameplay)
atlantis_add_editor_app_project(
  NAME atlantis_editor_app
  PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Editor/Atlantis.Editor.csproj"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Editor"
  DEPENDS atlantis_csharp_gameplay atlantis_editor_model
  INPUTS "${atlantis_editor_model_DLL}")
# After every other `dotnet build` that touches the C# SDK's project.
foreach(_other IN ITEMS atlantis_csharp_tests atlantis_csharp_gameplay_demo)
  if(TARGET ${_other})
    add_dependencies(atlantis_editor_model ${_other})
    add_dependencies(atlantis_editor_app ${_other})
  endif()
endforeach()

if(ATLANTIS_BUILD_TESTS)
  atlantis_add_editor_app_project(
    NAME atlantis_editor_app_tests
    PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/tests/Atlantis.Editor.Tests/Atlantis.Editor.Tests.csproj"
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/Atlantis.Editor.Tests"
    DEPENDS atlantis_editor_app
    INPUTS "${atlantis_editor_model_DLL}" "${atlantis_editor_app_DLL}")

  # ADR-0127 D5: the package policy, and that it refuses what it must.
  add_test(NAME "editor_app: package policy"
    COMMAND ${CMAKE_COMMAND} -DATLANTIS_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/check_editor_app_packages.cmake)
  add_test(NAME "editor_app: package policy refuses unpinned, unlisted and unlocked packages"
    COMMAND ${CMAKE_COMMAND} -DATLANTIS_SOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/policy_cases
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_policy_negative.cmake)
  # Plan 0064 P4: the C# camera port against the C++ model's vectors; since
  # Plan 0068 M1 every ported model against its vectors (ADR-0137 D1).
  add_test(NAME "editor_app: camera convention"
    COMMAND "${ATLANTIS_DOTNET}" "${atlantis_editor_app_tests_DLL}" --repo "${CMAKE_SOURCE_DIR}")
  # Plan 0064 M5: the shell's code paths against a real atlantis_runtime
  # --listen, without embedding (GPU: the Runtime renders).
  add_test(NAME "editor_app: headless smoke against atlantis_runtime --listen"
    COMMAND "${ATLANTIS_DOTNET}" "${atlantis_editor_app_DLL}" --smoke --runtime "$<TARGET_FILE:atlantis_runtime>")
  set_tests_properties(
    "editor_app: package policy"
    "editor_app: package policy refuses unpinned, unlisted and unlocked packages"
    "editor_app: camera convention"
    PROPERTIES LABELS editor_app TIMEOUT 600)
  set_tests_properties("editor_app: headless smoke against atlantis_runtime --listen"
    PROPERTIES LABELS "editor_app;gpu" TIMEOUT 600 RUN_SERIAL TRUE)
endif()
