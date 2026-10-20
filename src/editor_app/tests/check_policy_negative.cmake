# Plan 0064 M5 (ADR-0127 D5): cmake/check_editor_app_packages.cmake refuses
# what the policy forbids. Each case copies src/editor_app's policy files
# (Directory.Packages.props, NuGet.config, allowed_packages.txt, every
# .csproj and its packages.lock.json) to OUT/<case>, makes one change, and
# expects the check to fail naming it; the unchanged copy must pass.
#   cmake -DATLANTIS_SOURCE_DIR=<repo> -DOUT=<dir> -P check_policy_negative.cmake
if(NOT ATLANTIS_SOURCE_DIR OR NOT OUT)
  message(FATAL_ERROR "pass -DATLANTIS_SOURCE_DIR=<repo> -DOUT=<dir>")
endif()
set(_source "${ATLANTIS_SOURCE_DIR}/src/editor_app")
set(_editor "Atlantis.Editor/Atlantis.Editor.csproj")
set(_editor_lock "Atlantis.Editor/packages.lock.json")
set(_tests_lock "tests/Atlantis.Editor.Tests/packages.lock.json")

function(copy_policy dir)
  file(REMOVE_RECURSE "${dir}")
  file(GLOB_RECURSE _files RELATIVE "${_source}"
    "${_source}/Directory.Packages.props" "${_source}/NuGet.config" "${_source}/allowed_packages.txt"
    "${_source}/*.csproj" "${_source}/*packages.lock.json")
  list(FILTER _files EXCLUDE REGEX "(^|/)(bin|obj)/")
  foreach(_file IN LISTS _files)
    get_filename_component(_sub "${_file}" DIRECTORY)
    file(COPY "${_source}/${_file}" DESTINATION "${dir}/${_sub}")
  endforeach()
endfunction()

function(edit dir file from to)
  file(READ "${dir}/${file}" _text)
  string(FIND "${_text}" "${from}" _at)
  if(_at EQUAL -1)
    message(FATAL_ERROR "case setup: '${from}' not in ${file}")
  endif()
  string(REPLACE "${from}" "${to}" _text "${_text}")
  file(WRITE "${dir}/${file}" "${_text}")
endfunction()

function(run_check dir result output)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -DATLANTIS_SOURCE_DIR=${ATLANTIS_SOURCE_DIR} -DEDITOR_APP_DIR=${dir}
            -P ${ATLANTIS_SOURCE_DIR}/cmake/check_editor_app_packages.cmake
    RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  set(${result} "${_result}" PARENT_SCOPE)
  set(${output} "${_out}${_err}" PARENT_SCOPE)
endfunction()

set(_failures "")
function(expect_refused name expected)
  run_check("${OUT}/${name}" _result _output)
  string(FIND "${_output}" "${expected}" _at)
  if(_result EQUAL 0 OR _at EQUAL -1)
    set(_failures "${_failures}  ${name}: expected a refusal naming '${expected}', got exit ${_result}:\n${_output}\n" PARENT_SCOPE)
  else()
    message(STATUS "refused as expected: ${name}")
  endif()
endfunction()

# The unchanged copy passes.
copy_policy("${OUT}/unchanged")
run_check("${OUT}/unchanged" _result _output)
if(NOT _result EQUAL 0)
  string(APPEND _failures "  unchanged: the policy files as committed must pass:\n${_output}\n")
endif()

copy_policy("${OUT}/unpinned")
edit("${OUT}/unpinned" "Directory.Packages.props" "Include=\"Avalonia\" Version=\"[12.1.4]\"" "Include=\"Avalonia\" Version=\"12.1.*\"")
expect_refused(unpinned "avalonia is not pinned exactly")

copy_policy("${OUT}/minimum")
edit("${OUT}/minimum" "Directory.Packages.props" "Include=\"Avalonia\" Version=\"[12.1.4]\"" "Include=\"Avalonia\" Version=\"12.1.4\"")
expect_refused(minimum "avalonia is not pinned exactly")

copy_policy("${OUT}/own_version")
edit("${OUT}/own_version" "${_editor}" "<PackageReference Include=\"Avalonia\" />" "<PackageReference Include=\"Avalonia\" Version=\"12.1.4\" />")
expect_refused(own_version "avalonia carries its own version")

copy_policy("${OUT}/unlisted")
edit("${OUT}/unlisted" "${_editor}" "<PackageReference Include=\"Avalonia\" />"
     "<PackageReference Include=\"Avalonia\" />\n    <PackageReference Include=\"Newtonsoft.Json\" />")
edit("${OUT}/unlisted" "Directory.Packages.props" "</ItemGroup>"
     "  <PackageVersion Include=\"Newtonsoft.Json\" Version=\"[13.0.3]\" />\n  </ItemGroup>")
expect_refused(unlisted "newtonsoft.json is not on allowed_packages.txt")

copy_policy("${OUT}/no_lock")
file(REMOVE "${OUT}/no_lock/${_tests_lock}")
expect_refused(no_lock "Atlantis.Editor.Tests.csproj: no committed packages.lock.json")

copy_policy("${OUT}/lock_drift")
edit("${OUT}/lock_drift" "${_editor_lock}" "\"requested\": \"[12.1.4, 12.1.4]\"" "\"requested\": \"[12.1.3, 12.1.3]\"")
expect_refused(lock_drift "the lock file has")

copy_policy("${OUT}/unlocked")
edit("${OUT}/unlocked" "Directory.Packages.props" "<RestoreLockedMode Condition=\"'$(RestoreLockedMode)' == ''\">true</RestoreLockedMode>" "")
expect_refused(unlocked "RestoreLockedMode is not on")

copy_policy("${OUT}/extra_source")
edit("${OUT}/extra_source" "NuGet.config" "</packageSources>"
     "  <add key=\"mirror\" value=\"https://example.invalid/v3/index.json\" />\n  </packageSources>")
expect_refused(extra_source "must add exactly one source")

if(_failures)
  message(FATAL_ERROR "the editor-app package policy check accepted what it must refuse:\n${_failures}")
endif()
message(STATUS "the editor-app package policy check refuses every case")
