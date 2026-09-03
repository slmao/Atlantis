#pragma once

#include <atlantis/result.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace atlantis::image_regression {

struct Provenance {
  std::string captureDate;
  std::string sourceRevision;
  std::string gpuVendor;
  std::string gpuModel;
  std::string driverVersion;
  std::string osBuild;
  std::string vulkanLoaderApiVersion;            // dotted string, or "unavailable"
  std::string vulkanRequestedInstanceApiVersion;  // dotted string
  std::string vulkanPhysicalDeviceApiVersion;     // dotted string
  std::uint32_t extentWidth = 0;
  std::uint32_t extentHeight = 0;
  std::string format;  // matches an atlantis::rhi::Format enumerator name
  // Schema v2, used only by environment-backed captures. All four are empty
  // for schema-v1 legacy goldens and all four are populated together for v2.
  std::string environmentSourceSha256;
  std::string environmentArtifactSha256;
  std::string environmentCookerSettings;
  std::string goldenUpdateReason;
};

// The narrower schema tests/image_regression/current_environment.sidecar.txt
// uses (Section 3.2) -- same 7 hardware/environment-identity fields, no
// capture_date/source_revision/extent/format (those describe a specific
// *capture*, not the machine itself).
struct EnvironmentProvenance {
  std::string gpuVendor;
  std::string gpuModel;
  std::string driverVersion;
  std::string osBuild;
  std::string vulkanLoaderApiVersion;
  std::string vulkanRequestedInstanceApiVersion;
  std::string vulkanPhysicalDeviceApiVersion;
};

enum class ProvenanceParseError { WrongLineCount, UnknownSchemaVersion, FieldNameMismatch, MalformedValue };

// Strict, anchored-prefix parsing -- see the implementation for the
// exact delimiter/CRLF/version-format rules (Plan 0011 Section 2.3).
[[nodiscard]] atlantis::Result<Provenance, ProvenanceParseError> parseGoldenProvenance(const std::string& sidecarText);
[[nodiscard]] std::string serializeGoldenProvenance(const Provenance& provenance);

[[nodiscard]] atlantis::Result<EnvironmentProvenance, ProvenanceParseError>
parseEnvironmentProvenance(const std::string& sidecarText);
[[nodiscard]] std::string serializeEnvironmentProvenance(const EnvironmentProvenance& provenance);

struct ProvenanceFieldDiff {
  std::string fieldName;
  std::string goldenValue;
  std::string currentValue;
};

// Compares golden's 7 hardware/environment fields (gpuVendor through
// vulkanPhysicalDeviceApiVersion) against current -- never
// captureDate/sourceRevision/extent/format, which describe the capture
// event, not the environment. Empty return == full match.
[[nodiscard]] std::vector<ProvenanceFieldDiff> compareProvenanceEnvironment(const Provenance& golden,
                                                                             const EnvironmentProvenance& current);

}  // namespace atlantis::image_regression
