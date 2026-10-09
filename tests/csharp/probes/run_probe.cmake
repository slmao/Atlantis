# Plan 0058 P9 (J5): one compile probe, run by ctest:
#   cmake -DDOTNET=<dotnet> -DPROBE=<PROBE_NAME> -DEXPECT=fail|compile
#         -DGAMEPLAY_DLL=<Atlantis.Gameplay.dll> -DOUT=<dir> -P run_probe.cmake
# Builds Probe.csproj with only <PROBE_NAME> defined. A "fail" probe passes
# only if the build fails with a C# error on Probe.cs's line marked
# "// probe: <PROBE_NAME>"; a "compile" probe (its twin) must build.
foreach(_var DOTNET PROBE EXPECT GAMEPLAY_DLL OUT)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_probe.cmake: ${_var} is required")
  endif()
endforeach()

set(_dir "${CMAKE_CURRENT_LIST_DIR}")
file(STRINGS "${_dir}/Probe.cs" _lines)
set(_line 0)
set(_marked 0)
foreach(_text IN LISTS _lines)
  math(EXPR _line "${_line} + 1")
  if(_text MATCHES "// probe: ${PROBE}$")
    set(_marked ${_line})
  endif()
endforeach()
if(_marked EQUAL 0)
  message(FATAL_ERROR "Probe.cs has no line marked '// probe: ${PROBE}'")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E env --unset=LIB DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
          "${DOTNET}" build "${_dir}/Probe.csproj" -c Debug --nologo -nodeReuse:false -v:quiet
          "-p:DefineConstants=${PROBE}" "-p:AtlantisCSharpBuildRoot=${OUT}/${PROBE}"
          "-p:AtlantisGameplayDll=${GAMEPLAY_DLL}"
  WORKING_DIRECTORY "${_dir}"
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _output
  ERROR_VARIABLE _output)

if(EXPECT STREQUAL "compile")
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "${PROBE} should compile but did not:\n${_output}")
  endif()
  message(STATUS "${PROBE}: compiles")
else()
  if(_result EQUAL 0)
    message(FATAL_ERROR "${PROBE} compiled, but the typed layer should refuse it")
  endif()
  if(NOT _output MATCHES "Probe\\.cs\\(${_marked},[0-9]+\\): error CS[0-9]+")
    message(FATAL_ERROR "${PROBE} failed, but not with a C# error on Probe.cs line ${_marked}:\n${_output}")
  endif()
  string(REGEX MATCH "Probe\\.cs\\(${_marked},[0-9]+\\): error CS[0-9]+" _error "${_output}")
  message(STATUS "${PROBE}: refused -- ${_error}")
endif()
