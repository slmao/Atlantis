// Atlantis Tools: atlantis_shader_compiler CLI entry point. Invoked
// once per shader pair by atlantis_add_slang_shader_pair()
// (src/shader_system/CMakeLists.txt, Plan 0008 Section 7) via a plain
// --flag=value argv convention -- a Plan-stage mechanical detail, not
// an architectural surface.

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "compile_and_validate.h"

namespace {

[[nodiscard]] std::optional<std::string> valueAfterEquals(std::string_view arg, std::string_view flag) {
  if (arg.substr(0, flag.size()) != flag) return std::nullopt;
  return std::string(arg.substr(flag.size()));
}

}  // namespace

int main(int argc, char** argv) {
  using atlantis::tools::shader_compiler::CompileAndValidateRequest;

  CompileAndValidateRequest request;
  bool sawAllRequiredFlags = true;
  bool sawSlangcPath = false, sawSpirvValPath = false, sawSource = false, sawVertexEntry = false,
       sawFragmentEntry = false, sawOutputDir = false, sawExpectedContract = false, sawStamp = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (auto slangcPathValue = valueAfterEquals(arg, "--slangc-path=")) {
      request.slangcPath = *slangcPathValue;
      sawSlangcPath = true;
    } else if (auto spirvValPathValue = valueAfterEquals(arg, "--spirv-val-path=")) {
      request.spirvValPath = *spirvValPathValue;
      sawSpirvValPath = true;
    } else if (auto sourceValue = valueAfterEquals(arg, "--source=")) {
      request.sourcePath = *sourceValue;
      sawSource = true;
    } else if (auto vertexEntryValue = valueAfterEquals(arg, "--vertex-entry=")) {
      request.vertexEntry = *vertexEntryValue;
      sawVertexEntry = true;
    } else if (auto fragmentEntryValue = valueAfterEquals(arg, "--fragment-entry=")) {
      request.fragmentEntry = *fragmentEntryValue;
      sawFragmentEntry = true;
    } else if (auto outputDirValue = valueAfterEquals(arg, "--output-dir=")) {
      request.outputDir = *outputDirValue;
      sawOutputDir = true;
    } else if (auto expectedContractValue = valueAfterEquals(arg, "--expected-contract=")) {
      request.expectedContract = *expectedContractValue;
      sawExpectedContract = true;
    } else if (auto stampValue = valueAfterEquals(arg, "--stamp=")) {
      request.stampPath = *stampValue;
      sawStamp = true;
    } else {
      std::cerr << "atlantis_shader_compiler: unrecognized argument: " << arg << "\n";
      sawAllRequiredFlags = false;
    }
  }

  sawAllRequiredFlags = sawAllRequiredFlags && sawSlangcPath && sawSpirvValPath && sawSource && sawVertexEntry &&
                        sawFragmentEntry && sawOutputDir && sawExpectedContract && sawStamp;
  if (!sawAllRequiredFlags) {
    std::cerr << "usage: atlantis_shader_compiler --slangc-path=<path> --spirv-val-path=<path> --source=<path> "
                 "--vertex-entry=<name> --fragment-entry=<name> --output-dir=<dir> "
                 "--expected-contract=<minimal-renderer|textured-material|lit-textured|pbr-direct-lit> "
                 "--stamp=<path>\n";
    return 1;
  }

  return atlantis::tools::shader_compiler::compileAndValidate(request);
}
