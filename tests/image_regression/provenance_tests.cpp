#include "support/provenance.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

using atlantis::image_regression::compareProvenanceEnvironment;
using atlantis::image_regression::EnvironmentProvenance;
using atlantis::image_regression::parseEnvironmentProvenance;
using atlantis::image_regression::parseGoldenProvenance;
using atlantis::image_regression::Provenance;
using atlantis::image_regression::ProvenanceParseError;
using atlantis::image_regression::serializeEnvironmentProvenance;
using atlantis::image_regression::serializeGoldenProvenance;

namespace {

[[nodiscard]] std::string wellFormedGoldenSidecar() {
  return "schema_version: 1\n"
         "capture_date: 2026-08-17T00:00:00Z\n"
         "source_revision: 217db1a30c0934c66afa1dfbba8fdbfbe60fea67\n"
         "gpu_vendor: Intel\n"
         "gpu_model: Intel(R) Arc(TM) B370 GPU\n"
         "driver_version: 101.8509\n"
         "os_build: Windows 11 Home, Build 26200\n"
         "vulkan_loader_api_version: 1.4.357\n"
         "vulkan_requested_instance_api_version: 1.3.0\n"
         "vulkan_physical_device_api_version: 1.4.335\n"
         "extent_width: 512\n"
         "extent_height: 512\n"
         "format: Rgba8Unorm\n";
}

[[nodiscard]] std::string wellFormedEnvironmentSidecar() {
  return "schema_version: 1\n"
         "gpu_vendor: Intel\n"
         "gpu_model: Intel(R) Arc(TM) B370 GPU\n"
         "driver_version: 101.8509\n"
         "os_build: Windows 11 Home, Build 26200\n"
         "vulkan_loader_api_version: 1.4.357\n"
         "vulkan_requested_instance_api_version: 1.3.0\n"
         "vulkan_physical_device_api_version: 1.4.335\n";
}

}  // namespace

TEST_CASE("parseGoldenProvenance: a well-formed sidecar parses correctly", "[image_regression][provenance]") {
  const auto result = parseGoldenProvenance(wellFormedGoldenSidecar());
  REQUIRE(result.isOk());
  const Provenance& provenance = result.value();
  REQUIRE(provenance.captureDate == "2026-08-17T00:00:00Z");
  REQUIRE(provenance.sourceRevision == "217db1a30c0934c66afa1dfbba8fdbfbe60fea67");
  REQUIRE(provenance.gpuVendor == "Intel");
  REQUIRE(provenance.gpuModel == "Intel(R) Arc(TM) B370 GPU");
  REQUIRE(provenance.driverVersion == "101.8509");
  REQUIRE(provenance.osBuild == "Windows 11 Home, Build 26200");
  REQUIRE(provenance.vulkanLoaderApiVersion == "1.4.357");
  REQUIRE(provenance.vulkanRequestedInstanceApiVersion == "1.3.0");
  REQUIRE(provenance.vulkanPhysicalDeviceApiVersion == "1.4.335");
  REQUIRE(provenance.extentWidth == 512);
  REQUIRE(provenance.extentHeight == 512);
  REQUIRE(provenance.format == "Rgba8Unorm");
}

TEST_CASE("parseGoldenProvenance/serializeGoldenProvenance: round-trips byte-for-byte",
          "[image_regression][provenance]") {
  const std::string original = wellFormedGoldenSidecar();
  const auto parsed = parseGoldenProvenance(original);
  REQUIRE(parsed.isOk());
  REQUIRE(serializeGoldenProvenance(parsed.value()) == original);
}

TEST_CASE("parseGoldenProvenance: wrong line count is rejected", "[image_regression][provenance]") {
  const auto result = parseGoldenProvenance("schema_version: 1\ncapture_date: 2026-08-17T00:00:00Z\n");
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ProvenanceParseError::WrongLineCount);
}

TEST_CASE("parseGoldenProvenance: a wrong field name at a position is rejected", "[image_regression][provenance]") {
  std::string text = wellFormedGoldenSidecar();
  const std::string wrongFieldName = "capture_date: 2026-08-17T00:00:00Z";
  const std::size_t pos = text.find(wrongFieldName);
  REQUIRE(pos != std::string::npos);
  text.replace(pos, std::string("capture_date").size(), "captured_at");

  const auto result = parseGoldenProvenance(text);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ProvenanceParseError::FieldNameMismatch);
}

TEST_CASE("parseGoldenProvenance: an unknown schema_version is rejected", "[image_regression][provenance]") {
  std::string text = wellFormedGoldenSidecar();
  const std::size_t pos = text.find("schema_version: 1\n");
  REQUIRE(pos != std::string::npos);
  text.replace(pos, std::string("schema_version: 1\n").size(), "schema_version: 2\n");

  const auto result = parseGoldenProvenance(text);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ProvenanceParseError::UnknownSchemaVersion);
}

TEST_CASE("parseGoldenProvenance: a malformed numeric extent field is rejected", "[image_regression][provenance]") {
  std::string text = wellFormedGoldenSidecar();
  const std::size_t pos = text.find("extent_width: 512\n");
  REQUIRE(pos != std::string::npos);
  text.replace(pos, std::string("extent_width: 512\n").size(), "extent_width: not-a-number\n");

  const auto result = parseGoldenProvenance(text);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ProvenanceParseError::MalformedValue);
}

TEST_CASE("parseGoldenProvenance: an unrecognized format value is rejected", "[image_regression][provenance]") {
  std::string text = wellFormedGoldenSidecar();
  const std::size_t pos = text.find("format: Rgba8Unorm\n");
  REQUIRE(pos != std::string::npos);
  text.replace(pos, std::string("format: Rgba8Unorm\n").size(), "format: NotARealFormat\n");

  const auto result = parseGoldenProvenance(text);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ProvenanceParseError::MalformedValue);
}

TEST_CASE("parseGoldenProvenance/parseEnvironmentProvenance: vulkan_loader_api_version accepts \"unavailable\"",
          "[image_regression][provenance]") {
  std::string goldenText = wellFormedGoldenSidecar();
  const std::size_t goldenPos = goldenText.find("vulkan_loader_api_version: 1.4.357\n");
  REQUIRE(goldenPos != std::string::npos);
  goldenText.replace(goldenPos, std::string("vulkan_loader_api_version: 1.4.357\n").size(),
                      "vulkan_loader_api_version: unavailable\n");
  const auto goldenResult = parseGoldenProvenance(goldenText);
  REQUIRE(goldenResult.isOk());
  REQUIRE(goldenResult.value().vulkanLoaderApiVersion == "unavailable");

  std::string envText = wellFormedEnvironmentSidecar();
  const std::size_t envPos = envText.find("vulkan_loader_api_version: 1.4.357\n");
  REQUIRE(envPos != std::string::npos);
  envText.replace(envPos, std::string("vulkan_loader_api_version: 1.4.357\n").size(),
                   "vulkan_loader_api_version: unavailable\n");
  const auto envResult = parseEnvironmentProvenance(envText);
  REQUIRE(envResult.isOk());
  REQUIRE(envResult.value().vulkanLoaderApiVersion == "unavailable");
}

TEST_CASE("parseEnvironmentProvenance/serializeEnvironmentProvenance: round-trips byte-for-byte",
          "[image_regression][provenance]") {
  const std::string original = wellFormedEnvironmentSidecar();
  const auto parsed = parseEnvironmentProvenance(original);
  REQUIRE(parsed.isOk());
  REQUIRE(serializeEnvironmentProvenance(parsed.value()) == original);
}

TEST_CASE("compareProvenanceEnvironment: an empty result means a full match", "[image_regression][provenance]") {
  const auto golden = parseGoldenProvenance(wellFormedGoldenSidecar()).value();
  const auto current = parseEnvironmentProvenance(wellFormedEnvironmentSidecar()).value();

  const auto diffs = compareProvenanceEnvironment(golden, current);
  REQUIRE(diffs.empty());
}

TEST_CASE("compareProvenanceEnvironment: names every differing field, not just the first",
          "[image_regression][provenance]") {
  const auto golden = parseGoldenProvenance(wellFormedGoldenSidecar()).value();
  EnvironmentProvenance current = parseEnvironmentProvenance(wellFormedEnvironmentSidecar()).value();
  current.gpuModel = "Different GPU";
  current.driverVersion = "999.9999";

  const auto diffs = compareProvenanceEnvironment(golden, current);
  REQUIRE(diffs.size() == 2);

  bool foundGpuModel = false;
  bool foundDriverVersion = false;
  for (const auto& diff : diffs) {
    if (diff.fieldName == "gpu_model") {
      foundGpuModel = true;
      REQUIRE(diff.goldenValue == "Intel(R) Arc(TM) B370 GPU");
      REQUIRE(diff.currentValue == "Different GPU");
    }
    if (diff.fieldName == "driver_version") {
      foundDriverVersion = true;
      REQUIRE(diff.goldenValue == "101.8509");
      REQUIRE(diff.currentValue == "999.9999");
    }
  }
  REQUIRE(foundGpuModel);
  REQUIRE(foundDriverVersion);
}
