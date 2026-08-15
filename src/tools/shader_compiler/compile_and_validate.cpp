#include "compile_and_validate.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include <atlantis/shader_system/command_line.h>
#include <atlantis/shader_system/descriptor_contract.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/reflection_metadata.h>
#include <atlantis/shader_system/slang_json_transform.h>
#include <atlantis/shader_system/version_provenance.h>

#include "process_launch.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace atlantis::tools::shader_compiler {

namespace {

using atlantis::shader_system::buildSlangcArgv;
using atlantis::shader_system::buildSpirvValArgv;
using atlantis::shader_system::DescriptorBinding;
using atlantis::shader_system::minimalRendererExpectedDescriptorContract;
using atlantis::shader_system::PushConstantRange;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::saveReflectionMetadata;
using atlantis::shader_system::ShaderStage;
using atlantis::shader_system::SlangCompileRequest;
using atlantis::shader_system::SlangShaderStageArg;
using atlantis::shader_system::transformSlangReflectionJson;
using atlantis::shader_system::validateDescriptorContract;

// One compiled stage's own temp-directory artifacts, tracked together
// so the cross-stage check (step 12) and the publish transaction (step
// 14) can reference both stages uniformly.
struct StageResult {
  std::filesystem::path tempSpirvPath;
  ReflectionMetadata metadata;
};

void logDiagnostics(const std::string& toolLabel, const std::string& diagnostics) {
  std::cerr << "atlantis_shader_compiler: " << toolLabel << " diagnostics:\n" << diagnostics << "\n";
}

[[nodiscard]] std::filesystem::path createUniqueTempDir(const std::filesystem::path& outputDir,
                                                         const std::string& name) {
  const auto pid = GetCurrentProcessId();
  const auto counter = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  std::filesystem::path tempDir =
      outputDir / (".tmp-" + name + "-" + std::to_string(pid) + "-" + std::to_string(counter));
  std::error_code createError;
  std::filesystem::create_directories(tempDir, createError);
  return tempDir;
}

// Steps 1-6, 13 for ONE stage: compile via slangc, transform Slang's raw
// reflection JSON into Atlantis's own schema, spirv-val the result.
// Returns std::nullopt (having already logged diagnostics) on any
// failure -- the caller does not need its own separate error logging
// for this stage.
[[nodiscard]] std::optional<StageResult> compileStage(const CompileAndValidateRequest& request,
                                                        const std::filesystem::path& tempDir,
                                                        const std::string& entryPointName, ShaderStage stage,
                                                        const std::string& sdkProvenance) {
  const std::string stageFileTag = stage == ShaderStage::Vertex ? "vert" : "frag";
  const SlangShaderStageArg stageArg =
      stage == ShaderStage::Vertex ? SlangShaderStageArg::Vertex : SlangShaderStageArg::Fragment;

  SlangCompileRequest compileRequest;
  compileRequest.sourcePath = request.sourcePath;
  compileRequest.entryPointName = entryPointName;
  compileRequest.stage = stageArg;
  compileRequest.spirvOutputPath = tempDir / (stageFileTag + ".spv");
  compileRequest.reflectionJsonOutputPath = tempDir / (stageFileTag + ".slang_raw.json");

  const auto argv = buildSlangcArgv(request.slangcPath, compileRequest);
  auto launchResult = launchProcess(request.slangcPath, argv);
  if (launchResult.isErr()) {
    std::cerr << "atlantis_shader_compiler: failed to launch slangc for entry point '" << entryPointName << "'\n";
    return std::nullopt;
  }
  if (launchResult.value().exitCode != 0) {
    logDiagnostics("slangc (" + entryPointName + ")", launchResult.value().diagnostics);
    return std::nullopt;
  }

  auto transformResult = transformSlangReflectionJson(compileRequest.reflectionJsonOutputPath, entryPointName, stage,
                                                        sdkProvenance);
  if (transformResult.isErr()) {
    std::cerr << "atlantis_shader_compiler: failed to transform Slang reflection JSON for entry point '"
              << entryPointName << "'\n";
    return std::nullopt;
  }

  const auto spirvValArgv = buildSpirvValArgv(request.spirvValPath, compileRequest.spirvOutputPath);
  auto spirvValResult = launchProcess(request.spirvValPath, spirvValArgv);
  if (spirvValResult.isErr()) {
    std::cerr << "atlantis_shader_compiler: failed to launch spirv-val for entry point '" << entryPointName << "'\n";
    return std::nullopt;
  }
  if (spirvValResult.value().exitCode != 0) {
    logDiagnostics("spirv-val (" + entryPointName + ")", spirvValResult.value().diagnostics);
    return std::nullopt;
  }

  return StageResult{compileRequest.spirvOutputPath, std::move(transformResult.value())};
}

// Step 7: descriptor-contract validation, scoped per stage. The fixed
// contract (minimalRendererExpectedDescriptorContract()) is a
// PIPELINE-level expectation, not an independent per-stage one -- a
// real captured fragment-stage reflection (Plan 0008 Section 3's own
// module-level-parameters-vs-entry-point-bindings-used filtering rule)
// correctly has ZERO descriptor bindings, since fragmentMain never
// references the camera uniform. Validating the FULL fixed contract
// against each stage independently would therefore always fail for
// the fragment stage. The fix is narrow: filter `expected` down to just
// the entries whose own `.stage` matches the stage being validated
// before comparing -- for this round's contract, that is the whole
// vector for Vertex and an empty vector for Fragment, which correctly
// matches fragment's own real, empty reflection.
[[nodiscard]] bool validateDescriptorContractForStage(const ReflectionMetadata& metadata, ShaderStage stage) {
  const auto fullContract = minimalRendererExpectedDescriptorContract();
  std::vector<DescriptorBinding> scoped;
  std::copy_if(fullContract.begin(), fullContract.end(), std::back_inserter(scoped),
               [stage](const DescriptorBinding& binding) { return binding.stage == stage; });

  const auto result = validateDescriptorContract(metadata, scoped);
  if (result.isErr()) {
    std::cerr << "atlantis_shader_compiler: descriptor contract validation failed for entry point '"
              << metadata.entryPointName << "'\n";
    return false;
  }
  return true;
}

// Step 8: push-constant validation. This material's own fixed
// expectation is vertex-only (mirroring the original GLSL pair, where
// only the vertex shader ever declared a push_constant block). A real
// captured sample shows Slang's own raw JSON lists a pushConstantBuffer
// resource in EVERY entry point's bindings[] that can see it in module
// scope, regardless of whether that entry point's own compiled SPIR-V
// actually references it (confirmed by disassembly -- see
// slang_json_transform.cpp's own top comment) -- so this check
// deliberately only runs against the vertex stage's own metadata; a
// stray PushConstantRange on the fragment stage's own metadata is
// expected, harmless, and not validated here.
[[nodiscard]] bool validatePushConstantsForVertexStage(const ReflectionMetadata& vertexMetadata) {
  const std::vector<PushConstantRange> expected = {
      PushConstantRange{.offsetBytes = 0, .sizeBytes = sizeof(float) * 16, .stage = ShaderStage::Vertex}};
  if (vertexMetadata.pushConstantRanges != expected) {
    std::cerr << "atlantis_shader_compiler: vertex stage push-constant layout does not match the fixed "
                 "expectation (offset 0, size "
              << sizeof(float) * 16 << ", vertex stage)\n";
    return false;
  }
  return true;
}

// Step 9: every vertex-input location must be explicit and unique --
// a narrow, redundant sanity check on the SHAPE transform already
// produced (Slang itself already rejects two [[vk::location]] collisions
// as a compile error, caught at step 3).
[[nodiscard]] bool validateUniqueVertexInputLocations(const ReflectionMetadata& vertexMetadata) {
  std::vector<std::uint32_t> locations;
  for (const auto& attribute : vertexMetadata.vertexInputAttributes) locations.push_back(attribute.location);
  std::sort(locations.begin(), locations.end());
  if (std::adjacent_find(locations.begin(), locations.end()) != locations.end()) {
    std::cerr << "atlantis_shader_compiler: duplicate vertex-input location detected\n";
    return false;
  }
  return true;
}

// Step 12: supplementary cross-stage interface check -- vertex's own
// varying outputs must be a superset of fragment's own varying inputs,
// by location index.
[[nodiscard]] bool validateCrossStageInterface(const ReflectionMetadata& vertexMetadata,
                                                const ReflectionMetadata& fragmentMetadata) {
  for (const auto location : fragmentMetadata.varyingInputLocations) {
    const bool found = std::find(vertexMetadata.varyingOutputLocations.begin(),
                                  vertexMetadata.varyingOutputLocations.end(),
                                  location) != vertexMetadata.varyingOutputLocations.end();
    if (!found) {
      std::cerr << "atlantis_shader_compiler: fragment stage reads varying location " << location
                << " that the vertex stage does not write\n";
      return false;
    }
  }
  return true;
}

// Section 7's publish transaction (14b-14f already has 14a -- stale
// stamp deletion -- performed by the caller before this function is
// even reached). Renames the four temp-directory artifacts to their
// final OUTPUT_DIR paths, then writes the stamp last -- only once all
// four renames have succeeded. On any failure, best-effort removes
// whichever final-path files THIS invocation already renamed before
// failing, and always removes the whole temp directory, regardless of
// outcome.
[[nodiscard]] bool publishArtifacts(const CompileAndValidateRequest& request, const std::filesystem::path& tempDir,
                                     const StageResult& vertexResult, const StageResult& fragmentResult) {
  const std::string name = request.stampPath.filename().stem().string();
  const std::filesystem::path vertSpirvFinal = request.outputDir / (name + ".vert.spv");
  const std::filesystem::path vertReflFinal = request.outputDir / (name + ".vert.refl.json");
  const std::filesystem::path fragSpirvFinal = request.outputDir / (name + ".frag.spv");
  const std::filesystem::path fragReflFinal = request.outputDir / (name + ".frag.refl.json");

  const std::filesystem::path tempVertRefl = tempDir / "vert.refl.json";
  const std::filesystem::path tempFragRefl = tempDir / "frag.refl.json";
  if (saveReflectionMetadata(vertexResult.metadata, tempVertRefl).isErr() ||
      saveReflectionMetadata(fragmentResult.metadata, tempFragRefl).isErr()) {
    std::cerr << "atlantis_shader_compiler: failed to write reflection metadata to the temp publish directory\n";
    std::filesystem::remove_all(tempDir);
    return false;
  }

  const std::vector<std::pair<std::filesystem::path, std::filesystem::path>> renames = {
      {vertexResult.tempSpirvPath, vertSpirvFinal},
      {tempVertRefl, vertReflFinal},
      {fragmentResult.tempSpirvPath, fragSpirvFinal},
      {tempFragRefl, fragReflFinal},
  };

  std::vector<std::filesystem::path> published;
  for (const auto& [from, to] : renames) {
    std::error_code renameError;
    std::filesystem::rename(from, to, renameError);
    if (renameError) {
      std::cerr << "atlantis_shader_compiler: failed to publish artifact " << to.string() << "\n";
      // 14f: best-effort remove whatever THIS invocation already
      // published before this failure -- the stamp is never written,
      // so a subsequent build always retries regardless of whether
      // this cleanup itself fully succeeds.
      for (const auto& alreadyPublished : published) {
        std::error_code removeError;
        std::filesystem::remove(alreadyPublished, removeError);
      }
      std::filesystem::remove_all(tempDir);
      return false;
    }
    published.push_back(to);
  }

  // 14e: the stamp itself, via the same temp-then-rename pattern,
  // written strictly last -- only reached once all four real files are
  // already safely in place. Content is a plain text record, useful for
  // human debugging, never parsed back by any Atlantis code.
  const std::filesystem::path tempStamp = tempDir / "stamp.tmp";
  {
    std::ofstream stampFile(tempStamp, std::ios::binary | std::ios::trunc);
    stampFile << "sdkProvenance: " << vertexResult.metadata.sdkProvenance << "\n";
    if (!stampFile.good()) {
      std::filesystem::remove_all(tempDir);
      return false;
    }
  }
  std::error_code stampRenameError;
  std::filesystem::rename(tempStamp, request.stampPath, stampRenameError);
  std::filesystem::remove_all(tempDir);
  return !stampRenameError;
}

}  // namespace

int compileAndValidate(const CompileAndValidateRequest& request) {
  // 14a: delete any pre-existing stamp first, defensively -- CMake
  // itself would not have invoked this command unless the stamp was
  // already missing/stale, but this is a cheap, explicit belt-and-
  // suspenders guarantee against a prior run's stamp being mistaken
  // for "still current" if anything below fails partway.
  std::error_code removeStampError;
  std::filesystem::remove(request.stampPath, removeStampError);

  const std::string name = request.stampPath.filename().stem().string();
  const std::filesystem::path tempDir = createUniqueTempDir(request.outputDir, name);

  const auto sdkProvenance = atlantis::shader_system::describeSdkProvenance(request.slangcPath);
  const std::string sdkProvenanceText = sdkProvenance.value_or(std::string("unknown"));

  auto vertexResult =
      compileStage(request, tempDir, request.vertexEntry, ShaderStage::Vertex, sdkProvenanceText);
  auto fragmentResult =
      compileStage(request, tempDir, request.fragmentEntry, ShaderStage::Fragment, sdkProvenanceText);
  if (!vertexResult.has_value() || !fragmentResult.has_value()) {
    std::filesystem::remove_all(tempDir);
    return 1;
  }

  const bool validationOk =
      validateDescriptorContractForStage(vertexResult->metadata, ShaderStage::Vertex) &&
      validateDescriptorContractForStage(fragmentResult->metadata, ShaderStage::Fragment) &&
      validatePushConstantsForVertexStage(vertexResult->metadata) &&
      validateUniqueVertexInputLocations(vertexResult->metadata) &&
      validateCrossStageInterface(vertexResult->metadata, fragmentResult->metadata);
  if (!validationOk) {
    std::filesystem::remove_all(tempDir);
    return 1;
  }

  if (!publishArtifacts(request, tempDir, *vertexResult, *fragmentResult)) return 1;
  return 0;
}

}  // namespace atlantis::tools::shader_compiler
