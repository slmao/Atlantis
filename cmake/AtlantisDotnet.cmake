# Plan 0058 P7 (Spec 0058 R7, ruling Q3; ADR-0113 D2, D4): .NET discovery and
# the build of the Atlantis C# SDK's projects. Included from the top-level
# CMakeLists.txt only if(NOT ANDROID): Android never declares a C# target.
#
# A .NET SDK of global.json's 10.0.1xx band (at least its version) enables
# C#: ATLANTIS_CSHARP_ENABLED is ON and the C# targets and tests are
# declared. Without one, C# is skipped with one status line -- or configure
# fails when ATLANTIS_REQUIRE_CSHARP is ON. Nothing native depends on C#.

option(ATLANTIS_REQUIRE_CSHARP "Fail configure without a .NET 10.0.1xx SDK (Plan 0058 P7)" OFF)

set(ATLANTIS_CSHARP_ENABLED OFF)
set(_atlantis_dotnet_minimum "")
file(READ "${CMAKE_SOURCE_DIR}/global.json" _atlantis_global_json)
string(JSON _atlantis_dotnet_minimum GET "${_atlantis_global_json}" sdk version)

find_program(ATLANTIS_DOTNET dotnet)
set(_atlantis_dotnet_sdk "")
if(ATLANTIS_DOTNET)
  execute_process(
    COMMAND "${ATLANTIS_DOTNET}" --list-sdks
    OUTPUT_VARIABLE _atlantis_dotnet_sdks
    RESULT_VARIABLE _atlantis_dotnet_result
    ERROR_QUIET)
  if(_atlantis_dotnet_result EQUAL 0)
    # Lines look like "10.0.111 [C:\Program Files\dotnet\sdk]"; keep the
    # newest 10.0.1xx at or above global.json's version (rollForward
    # latestPatch stays inside the band).
    string(REPLACE "\n" ";" _atlantis_dotnet_lines "${_atlantis_dotnet_sdks}")
    foreach(_line IN LISTS _atlantis_dotnet_lines)
      if(_line MATCHES "^(10\\.0\\.1[0-9][0-9]) ")
        set(_version "${CMAKE_MATCH_1}")
        if(NOT _version VERSION_LESS _atlantis_dotnet_minimum
           AND (_atlantis_dotnet_sdk STREQUAL "" OR _version VERSION_GREATER _atlantis_dotnet_sdk))
          set(_atlantis_dotnet_sdk "${_version}")
        endif()
      endif()
    endforeach()
  endif()
endif()

if(_atlantis_dotnet_sdk)
  set(ATLANTIS_CSHARP_ENABLED ON)
  message(STATUS "Atlantis C# SDK: .NET SDK ${_atlantis_dotnet_sdk} (${ATLANTIS_DOTNET})")
elseif(ATLANTIS_REQUIRE_CSHARP)
  message(FATAL_ERROR
    "ATLANTIS_REQUIRE_CSHARP is ON but no .NET 10.0.1xx SDK at or above ${_atlantis_dotnet_minimum} "
    "(global.json) was found. Install it, or configure with -DATLANTIS_REQUIRE_CSHARP=OFF.")
else()
  message(STATUS
    "C# targets and tests are not declared: no .NET 10.0.1xx SDK; "
    "set ATLANTIS_REQUIRE_CSHARP=ON to make this an error")
endif()

# atlantis_add_dotnet_project(NAME <target> PROJECT <path/to/x.csproj>
#                             SOURCE_DIRS <dir>... [DEPENDS <target>...])
# Builds the project with `dotnet build` into
# ${CMAKE_BINARY_DIR}/csharp/<config>/<project name>/ (Directory.Build.props
# routes it there), re-running when any .cs/.csproj/.props under SOURCE_DIRS
# or the shared root files change. Sets <NAME>_DLL to the built assembly
# (a path with $<CONFIG>).
function(atlantis_add_dotnet_project)
  cmake_parse_arguments(ARG "" "NAME;PROJECT" "SOURCE_DIRS;DEPENDS" ${ARGN})
  get_filename_component(_project_name "${ARG_PROJECT}" NAME_WLE)
  set(_root "${CMAKE_BINARY_DIR}/csharp/$<CONFIG>")
  set(_dll "${_root}/${_project_name}/${_project_name}.dll")
  set(_sources)
  foreach(_dir IN LISTS ARG_SOURCE_DIRS)
    file(GLOB_RECURSE _found CONFIGURE_DEPENDS
      "${_dir}/*.cs" "${_dir}/*.csproj" "${_dir}/*.props")
    list(FILTER _found EXCLUDE REGEX "/(bin|obj)/")
    list(APPEND _sources ${_found})
  endforeach()
  list(APPEND _sources
    "${CMAKE_SOURCE_DIR}/global.json"
    "${CMAKE_SOURCE_DIR}/NuGet.config"
    "${CMAKE_SOURCE_DIR}/Directory.Build.props")
  add_custom_command(
    OUTPUT "${_dll}"
    COMMAND ${CMAKE_COMMAND} -E env --unset=LIB DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
            DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
            "${ATLANTIS_DOTNET}" build "${ARG_PROJECT}" -c $<CONFIG>
            "-p:AtlantisCSharpBuildRoot=${_root}" --nologo -nodeReuse:false -v:minimal
    COMMAND ${CMAKE_COMMAND} -E touch "${_dll}"
    DEPENDS ${_sources} ${ARG_DEPENDS}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "dotnet build ${_project_name} ($<CONFIG>)"
    VERBATIM)
  add_custom_target(${ARG_NAME} ALL DEPENDS "${_dll}")
  if(ARG_DEPENDS)
    add_dependencies(${ARG_NAME} ${ARG_DEPENDS})
  endif()
  set(${ARG_NAME}_DLL "${_dll}" PARENT_SCOPE)
endfunction()
