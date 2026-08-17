#include "provenance.h"

#include <array>
#include <regex>
#include <sstream>

namespace atlantis::image_regression {

namespace {

constexpr std::array<const char*, 12> kGoldenFieldNamesAfterSchema = {
    "capture_date",
    "source_revision",
    "gpu_vendor",
    "gpu_model",
    "driver_version",
    "os_build",
    "vulkan_loader_api_version",
    "vulkan_requested_instance_api_version",
    "vulkan_physical_device_api_version",
    "extent_width",
    "extent_height",
    "format",
};

constexpr std::array<const char*, 7> kEnvironmentFieldNames = {
    "gpu_vendor",
    "gpu_model",
    "driver_version",
    "os_build",
    "vulkan_loader_api_version",
    "vulkan_requested_instance_api_version",
    "vulkan_physical_device_api_version",
};

// atlantis::rhi::Format's enumerator names, by string -- validated by
// name, not parsed as the real enum, since this file has no RHI
// dependency (Critical Architectural Boundaries).
constexpr std::array<const char*, 5> kKnownFormatNames = {
    "Unknown", "Bgra8Unorm", "Bgra8Srgb", "Rgba8Unorm", "Rgba8Srgb",
};

// Splits sidecar text into physical lines. Accepts \n or \r\n line
// endings (strips one trailing \r per line if present) -- deliberately
// more lenient than this format's own canonical \n-only *write* form,
// since current_environment.sidecar.txt is git-ignored and may be saved
// by a plain Windows text editor. Drops exactly one trailing empty
// element produced by a well-formed file's own final newline.
[[nodiscard]] std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (true) {
    const std::size_t newlinePos = text.find('\n', start);
    std::string line =
        (newlinePos == std::string::npos) ? text.substr(start) : text.substr(start, newlinePos - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    if (newlinePos == std::string::npos) break;
    start = newlinePos + 1;
  }
  if (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

// Anchored-prefix match (std::string::rfind at position 0, i.e. "starts
// with"), never a delimiter scan -- this is why capture_date's own ISO
// 8601 value (which itself contains colons) is never ambiguous: the
// parser already knows, from the line's fixed position, which field
// name to expect there.
[[nodiscard]] bool matchesField(const std::string& line, const std::string& fieldName, std::string& valueOut) {
  const std::string prefix = fieldName + ": ";
  if (line.rfind(prefix, 0) != 0) return false;
  valueOut = line.substr(prefix.size());
  return true;
}

[[nodiscard]] bool isValidVersionField(const std::string& value, bool allowUnavailable) {
  if (allowUnavailable && value == "unavailable") return true;
  static const std::regex kVersionPattern(R"(^[0-9]+\.[0-9]+\.[0-9]+$)");
  return std::regex_match(value, kVersionPattern);
}

[[nodiscard]] bool isKnownFormatName(const std::string& value) {
  for (const char* known : kKnownFormatNames) {
    if (value == known) return true;
  }
  return false;
}

[[nodiscard]] bool parseDecimalUint32(const std::string& value, std::uint32_t& out) {
  if (value.empty()) return false;
  for (const char c : value) {
    if (c < '0' || c > '9') return false;
  }
  try {
    out = static_cast<std::uint32_t>(std::stoul(value));
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

Result<Provenance, ProvenanceParseError> parseGoldenProvenance(const std::string& sidecarText) {
  const std::vector<std::string> lines = splitLines(sidecarText);
  if (lines.size() != 1 + kGoldenFieldNamesAfterSchema.size()) {
    return Result<Provenance, ProvenanceParseError>::Err(ProvenanceParseError::WrongLineCount);
  }

  std::string schemaValue;
  if (!matchesField(lines[0], "schema_version", schemaValue) || schemaValue != "1") {
    return Result<Provenance, ProvenanceParseError>::Err(ProvenanceParseError::UnknownSchemaVersion);
  }

  std::array<std::string, kGoldenFieldNamesAfterSchema.size()> values;
  for (std::size_t i = 0; i < kGoldenFieldNamesAfterSchema.size(); ++i) {
    if (!matchesField(lines[i + 1], kGoldenFieldNamesAfterSchema[i], values[i])) {
      return Result<Provenance, ProvenanceParseError>::Err(ProvenanceParseError::FieldNameMismatch);
    }
  }
  for (const std::string& value : values) {
    if (value.empty()) return Result<Provenance, ProvenanceParseError>::Err(ProvenanceParseError::MalformedValue);
  }

  Provenance provenance;
  provenance.captureDate = values[0];
  provenance.sourceRevision = values[1];
  provenance.gpuVendor = values[2];
  provenance.gpuModel = values[3];
  provenance.driverVersion = values[4];
  provenance.osBuild = values[5];
  provenance.vulkanLoaderApiVersion = values[6];
  provenance.vulkanRequestedInstanceApiVersion = values[7];
  provenance.vulkanPhysicalDeviceApiVersion = values[8];
  provenance.format = values[11];

  if (!isValidVersionField(provenance.vulkanLoaderApiVersion, /*allowUnavailable=*/true) ||
      !isValidVersionField(provenance.vulkanRequestedInstanceApiVersion, /*allowUnavailable=*/false) ||
      !isValidVersionField(provenance.vulkanPhysicalDeviceApiVersion, /*allowUnavailable=*/false) ||
      !parseDecimalUint32(values[9], provenance.extentWidth) ||
      !parseDecimalUint32(values[10], provenance.extentHeight) || !isKnownFormatName(provenance.format)) {
    return Result<Provenance, ProvenanceParseError>::Err(ProvenanceParseError::MalformedValue);
  }

  return Result<Provenance, ProvenanceParseError>::Ok(std::move(provenance));
}

std::string serializeGoldenProvenance(const Provenance& provenance) {
  std::ostringstream out;
  out << "schema_version: 1\n";
  out << "capture_date: " << provenance.captureDate << "\n";
  out << "source_revision: " << provenance.sourceRevision << "\n";
  out << "gpu_vendor: " << provenance.gpuVendor << "\n";
  out << "gpu_model: " << provenance.gpuModel << "\n";
  out << "driver_version: " << provenance.driverVersion << "\n";
  out << "os_build: " << provenance.osBuild << "\n";
  out << "vulkan_loader_api_version: " << provenance.vulkanLoaderApiVersion << "\n";
  out << "vulkan_requested_instance_api_version: " << provenance.vulkanRequestedInstanceApiVersion << "\n";
  out << "vulkan_physical_device_api_version: " << provenance.vulkanPhysicalDeviceApiVersion << "\n";
  out << "extent_width: " << provenance.extentWidth << "\n";
  out << "extent_height: " << provenance.extentHeight << "\n";
  out << "format: " << provenance.format << "\n";
  return out.str();
}

Result<EnvironmentProvenance, ProvenanceParseError> parseEnvironmentProvenance(const std::string& sidecarText) {
  const std::vector<std::string> lines = splitLines(sidecarText);
  if (lines.size() != 1 + kEnvironmentFieldNames.size()) {
    return Result<EnvironmentProvenance, ProvenanceParseError>::Err(ProvenanceParseError::WrongLineCount);
  }

  std::string schemaValue;
  if (!matchesField(lines[0], "schema_version", schemaValue) || schemaValue != "1") {
    return Result<EnvironmentProvenance, ProvenanceParseError>::Err(ProvenanceParseError::UnknownSchemaVersion);
  }

  std::array<std::string, kEnvironmentFieldNames.size()> values;
  for (std::size_t i = 0; i < kEnvironmentFieldNames.size(); ++i) {
    if (!matchesField(lines[i + 1], kEnvironmentFieldNames[i], values[i])) {
      return Result<EnvironmentProvenance, ProvenanceParseError>::Err(ProvenanceParseError::FieldNameMismatch);
    }
  }
  for (const std::string& value : values) {
    if (value.empty()) {
      return Result<EnvironmentProvenance, ProvenanceParseError>::Err(ProvenanceParseError::MalformedValue);
    }
  }

  EnvironmentProvenance provenance;
  provenance.gpuVendor = values[0];
  provenance.gpuModel = values[1];
  provenance.driverVersion = values[2];
  provenance.osBuild = values[3];
  provenance.vulkanLoaderApiVersion = values[4];
  provenance.vulkanRequestedInstanceApiVersion = values[5];
  provenance.vulkanPhysicalDeviceApiVersion = values[6];

  if (!isValidVersionField(provenance.vulkanLoaderApiVersion, /*allowUnavailable=*/true) ||
      !isValidVersionField(provenance.vulkanRequestedInstanceApiVersion, /*allowUnavailable=*/false) ||
      !isValidVersionField(provenance.vulkanPhysicalDeviceApiVersion, /*allowUnavailable=*/false)) {
    return Result<EnvironmentProvenance, ProvenanceParseError>::Err(ProvenanceParseError::MalformedValue);
  }

  return Result<EnvironmentProvenance, ProvenanceParseError>::Ok(std::move(provenance));
}

std::string serializeEnvironmentProvenance(const EnvironmentProvenance& provenance) {
  std::ostringstream out;
  out << "schema_version: 1\n";
  out << "gpu_vendor: " << provenance.gpuVendor << "\n";
  out << "gpu_model: " << provenance.gpuModel << "\n";
  out << "driver_version: " << provenance.driverVersion << "\n";
  out << "os_build: " << provenance.osBuild << "\n";
  out << "vulkan_loader_api_version: " << provenance.vulkanLoaderApiVersion << "\n";
  out << "vulkan_requested_instance_api_version: " << provenance.vulkanRequestedInstanceApiVersion << "\n";
  out << "vulkan_physical_device_api_version: " << provenance.vulkanPhysicalDeviceApiVersion << "\n";
  return out.str();
}

std::vector<ProvenanceFieldDiff> compareProvenanceEnvironment(const Provenance& golden,
                                                                const EnvironmentProvenance& current) {
  std::vector<ProvenanceFieldDiff> diffs;
  const auto check = [&diffs](const char* fieldName, const std::string& goldenValue, const std::string& currentValue) {
    if (goldenValue != currentValue) {
      diffs.push_back(ProvenanceFieldDiff{fieldName, goldenValue, currentValue});
    }
  };
  check("gpu_vendor", golden.gpuVendor, current.gpuVendor);
  check("gpu_model", golden.gpuModel, current.gpuModel);
  check("driver_version", golden.driverVersion, current.driverVersion);
  check("os_build", golden.osBuild, current.osBuild);
  check("vulkan_loader_api_version", golden.vulkanLoaderApiVersion, current.vulkanLoaderApiVersion);
  check("vulkan_requested_instance_api_version", golden.vulkanRequestedInstanceApiVersion,
        current.vulkanRequestedInstanceApiVersion);
  check("vulkan_physical_device_api_version", golden.vulkanPhysicalDeviceApiVersion,
        current.vulkanPhysicalDeviceApiVersion);
  return diffs;
}

}  // namespace atlantis::image_regression
