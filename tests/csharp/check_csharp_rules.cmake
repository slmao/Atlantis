# Plan 0058 P9 (Spec 0058 R1, R7; ADR-0113 D3; J9): the C# rules, checked by
# ctest with or without .NET:
#   cmake -DATLANTIS_SOURCE_DIR=<repo> -P check_csharp_rules.cmake
# - no PackageReference in any .csproj or .props (zero NuGet);
# - NuGet.config clears its package sources and adds none;
# - no native interop (DllImport, LibraryImport, NativeLibrary, extern
#   methods) under src/csharp/ or examples/csharp/;
# - the generated bindings and the conformance vectors are eol=lf.
if(NOT ATLANTIS_SOURCE_DIR)
  message(FATAL_ERROR "pass -DATLANTIS_SOURCE_DIR=<repository root>")
endif()

set(_failures "")

file(GLOB_RECURSE _projects
  "${ATLANTIS_SOURCE_DIR}/src/csharp/*.csproj" "${ATLANTIS_SOURCE_DIR}/src/csharp/*.props"
  "${ATLANTIS_SOURCE_DIR}/examples/csharp/*.csproj" "${ATLANTIS_SOURCE_DIR}/examples/csharp/*.props"
  "${ATLANTIS_SOURCE_DIR}/tests/csharp/*.csproj" "${ATLANTIS_SOURCE_DIR}/tests/csharp/*.props")
list(APPEND _projects "${ATLANTIS_SOURCE_DIR}/Directory.Build.props")
list(LENGTH _projects _project_count)
if(_project_count LESS 3)
  string(APPEND _failures "  expected the C# projects and Directory.Build.props, found ${_project_count} file(s)\n")
endif()
foreach(_file IN LISTS _projects)
  file(READ "${_file}" _text)
  if(_text MATCHES "<(PackageReference|PackageVersion|PackageDownload)[ \t\r\n/>]")
    string(APPEND _failures "  ${_file}: a NuGet package reference\n")
  endif()
endforeach()

file(READ "${ATLANTIS_SOURCE_DIR}/NuGet.config" _nuget)
if(NOT _nuget MATCHES "<packageSources>[ \t\r\n]*<clear[ \t]*/>[ \t\r\n]*</packageSources>")
  string(APPEND _failures "  NuGet.config: packageSources must hold <clear /> and nothing else\n")
endif()
if(_nuget MATCHES "<add ")
  string(APPEND _failures "  NuGet.config: adds a source or setting\n")
endif()

file(GLOB_RECURSE _sources
  "${ATLANTIS_SOURCE_DIR}/src/csharp/*.cs" "${ATLANTIS_SOURCE_DIR}/examples/csharp/*.cs")
list(FILTER _sources EXCLUDE REGEX "/(bin|obj)/")
list(LENGTH _sources _source_count)
if(_source_count LESS 5)
  string(APPEND _failures "  expected the C# SDK's sources, found ${_source_count} file(s)\n")
endif()
foreach(_file IN LISTS _sources)
  file(READ "${_file}" _text)
  if(_text MATCHES "DllImport|LibraryImport|NativeLibrary|[ \t]extern[ \t]|unsafe[ \t\r\n{]|fixed[ \t]*\\(")
    string(APPEND _failures "  ${_file}: native interop or unsafe code\n")
  endif()
endforeach()

file(READ "${ATLANTIS_SOURCE_DIR}/.gitattributes" _attributes)
foreach(_rule IN ITEMS "*.g.cs text eol=lf" "tests/csharp/conformance/*.jsonl text eol=lf")
  string(FIND "${_attributes}" "${_rule}" _at)
  if(_at EQUAL -1)
    string(APPEND _failures "  .gitattributes: missing \"${_rule}\"\n")
  endif()
endforeach()

if(_failures)
  message(FATAL_ERROR "C# rules (Plan 0058 J9) violated:\n${_failures}")
endif()
message(STATUS "C# rules (Plan 0058 J9): ${_project_count} project file(s), ${_source_count} source file(s) -- no NuGet, no native interop, eol=lf")
