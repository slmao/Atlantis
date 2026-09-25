#include "material_import.h"

#include "material_conversion.h"

#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/material_source.h>

#include <cgltf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <string_view>
#include <system_error>

// Plan 0037 Milestone 4: glTF materials -> .material.txt v6 (ADR-0083 D3)
// and their textures -> cook_manifest.txt lines for the unmodified
// atlantis_asset_cooker (D4; the DDS file's DXGI format stays the colour-space
// authority). Every material imports as pbr_direct_lit.

namespace atlantis::gltf_importer::detail {

namespace {

namespace fs = std::filesystem;
using atlantis::asset_system::MaterialAlphaMode;
using atlantis::asset_system::MaterialKind;
using atlantis::asset_system::MaterialSamplerAddressMode;
using atlantis::asset_system::MaterialSamplerFilter;
using atlantis::asset_system::ParsedMaterialSource;

using CheckResult = atlantis::Result<std::monostate, GltfImportError>;

constexpr std::string_view kWhiteFallbackRelative = "_importer/white_4x4_bc7.dds";

enum class TextureUsage { Color, Data };

// Where a referenced texture's file is and how the cooker must be told
// about it. Texture logical paths are "<content-root dir name>/<uri>" and
// are cooked with --asset-root=<content root's parent>, so the path the
// cooker hashes is exactly the string the material names.
struct ResolvedTexture {
  std::string uri;  // decoded, relative to the content root
  bool isDds = false;
  const cgltf_sampler* sampler = nullptr;
};

[[nodiscard]] bool jsonIntMember(const char* json, const char* key, long long& out) {
  if (json == nullptr) return false;
  const std::string needle = std::string("\"") + key + "\"";
  const char* p = std::strstr(json, needle.c_str());
  if (p == nullptr) return false;
  p = std::strchr(p + needle.size(), ':');
  if (p == nullptr) return false;
  ++p;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
  char* end = nullptr;
  out = std::strtoll(p, &end, 10);
  return end != p;
}

[[nodiscard]] std::optional<std::string> decodedUri(const cgltf_image* image) {
  if (image == nullptr || image->uri == nullptr) return std::nullopt;
  std::string uri = image->uri;
  if (uri.rfind("data:", 0) == 0) return std::nullopt;
  std::vector<char> buffer(uri.begin(), uri.end());
  buffer.push_back('\0');
  cgltf_decode_uri(buffer.data());
  return std::string(buffer.data());
}

// MSFT_texture_dds (read through cgltf's generic extension pass-through,
// ADR-0082) wins over the core source image, which in Bistro names PNG/JPG
// files that do not exist upstream.
[[nodiscard]] std::optional<ResolvedTexture> resolveTexture(const cgltf_data& data, const cgltf_texture& texture) {
  ResolvedTexture resolved;
  resolved.sampler = texture.sampler;
  for (cgltf_size e = 0; e < texture.extensions_count; ++e) {
    if (std::strcmp(texture.extensions[e].name, "MSFT_texture_dds") != 0) continue;
    long long source = -1;
    if (!jsonIntMember(texture.extensions[e].data, "source", source) || source < 0 ||
        static_cast<cgltf_size>(source) >= data.images_count) {
      return std::nullopt;
    }
    const auto uri = decodedUri(&data.images[source]);
    if (!uri) return std::nullopt;
    resolved.uri = *uri;
    resolved.isDds = true;
    return resolved;
  }
  const auto uri = decodedUri(texture.image);
  if (!uri) return std::nullopt;
  resolved.uri = *uri;
  std::string lower = resolved.uri;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  resolved.isDds = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".dds") == 0;
  return resolved;
}

// The only material extensions in scope are the three Spec 0037 found in
// Bistro; cgltf parses the other Khronos ones into typed flags, and anything
// it does not know lands in extensions[].
[[nodiscard]] bool hasOutOfScopeExtension(const cgltf_material& m) {
  return m.has_clearcoat || m.has_volume || m.has_ior || m.has_specular || m.has_sheen || m.has_emissive_strength ||
         m.has_iridescence || m.has_diffuse_transmission || m.has_anisotropy || m.has_dispersion || m.unlit ||
         m.extensions_count > 0;
}

struct MaterialTextures {
  const cgltf_texture_view* baseColor = nullptr;
  const cgltf_texture_view* normal = nullptr;
  // Plan 0046 Milestone 1 (ADR-0096): set only when the texture is mapped --
  // a non-zero, in-range emissiveFactor beside it (mapsEmissiveTexture()).
  const cgltf_texture_view* emissive = nullptr;
};

// Plan 0046 Milestone 1 (ADR-0096, Plan 0046 P5): glTF's emissive is
// factor x texture, so a texture maps exactly when its factor does -- a
// non-zero factor inside [0, 65504]. A zero factor leaves it inert.
[[nodiscard]] bool emissiveFactorInRange(const cgltf_material& m) {
  for (int c = 0; c < 3; ++c) {
    const float component = m.emissive_factor[c];
    if (!std::isfinite(component) || component < 0.0f || component > 65504.0f) return false;
  }
  return true;
}

[[nodiscard]] bool hasNonZeroEmissiveFactor(const cgltf_material& m) {
  return m.emissive_factor[0] != 0.0f || m.emissive_factor[1] != 0.0f || m.emissive_factor[2] != 0.0f;
}

[[nodiscard]] bool mapsEmissiveTexture(const cgltf_material& m) {
  return m.emissive_texture.texture != nullptr && hasNonZeroEmissiveFactor(m) && emissiveFactorInRange(m);
}

[[nodiscard]] MaterialTextures usedTextures(const cgltf_material& m) {
  MaterialTextures used;
  const cgltf_texture_view& base =
      m.has_pbr_specular_glossiness ? m.pbr_specular_glossiness.diffuse_texture : m.pbr_metallic_roughness.base_color_texture;
  if (base.texture != nullptr) used.baseColor = &base;
  if (m.normal_texture.texture != nullptr) used.normal = &m.normal_texture;
  if (mapsEmissiveTexture(m)) used.emissive = &m.emissive_texture;
  return used;
}

[[nodiscard]] fs::path contentRootDirectory(const fs::path& contentRoot) {
  fs::path root = contentRoot.lexically_normal();
  if (root.filename().empty()) root = root.parent_path();
  return root;
}

[[nodiscard]] std::string textureLogicalPath(const fs::path& contentRoot, const std::string& uri) {
  return contentRootDirectory(contentRoot).filename().string() + "/" + uri;
}

[[nodiscard]] CheckResult checkTextureView(const cgltf_data& data, const cgltf_texture_view& view,
                                           const fs::path& contentRoot) {
  if (view.texcoord != 0 || view.has_transform) return CheckResult::Err(GltfImportError::UnsupportedTextureFeature);
  const auto resolved = resolveTexture(data, *view.texture);
  if (!resolved) return CheckResult::Err(GltfImportError::TextureWithoutSource);
  if (atlantis::asset_system::normalizeLogicalPath(textureLogicalPath(contentRoot, resolved->uri)).isErr()) {
    return CheckResult::Err(GltfImportError::TextureWithoutSource);
  }
  std::error_code ec;
  if (!fs::is_regular_file(contentRoot / resolved->uri, ec)) return CheckResult::Err(GltfImportError::MissingTextureFile);
  if (const cgltf_sampler* s = resolved->sampler) {
    if (s->wrap_s != s->wrap_t || s->wrap_s == cgltf_wrap_mode_mirrored_repeat) {
      return CheckResult::Err(GltfImportError::UnsupportedSamplerWrap);
    }
  }
  return CheckResult::Ok(std::monostate{});
}

[[nodiscard]] bool isUnitFactor(float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }

// Maps a glTF sampler onto v6's single per-material filter/address pair.
// No sampler at all (every Bistro texture) means glTF's default: filtering
// is implementation-defined -- Atlantis picks linear -- and wrap is REPEAT.
void applySampler(const cgltf_sampler* sampler, ParsedMaterialSource& source) {
  source.filter = MaterialSamplerFilter::Linear;
  source.addressMode = MaterialSamplerAddressMode::Repeat;
  if (sampler == nullptr) return;
  if (sampler->mag_filter == cgltf_filter_type_nearest) source.filter = MaterialSamplerFilter::Nearest;
  if (sampler->wrap_s == cgltf_wrap_mode_clamp_to_edge) source.addressMode = MaterialSamplerAddressMode::ClampToEdge;
}

// Reads a DDS file's DXGI format from its DX10 header; nullopt for a legacy
// (non-DX10) header or an unreadable file. Heuristic use only (Ruling 4) --
// the cooker's own parser remains the authority.
[[nodiscard]] std::optional<std::uint32_t> ddsDxgiFormat(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  unsigned char header[148] = {};
  if (!in.read(reinterpret_cast<char*>(header), sizeof header)) return std::nullopt;
  if (std::memcmp(header + 84, "DX10", 4) != 0) return std::nullopt;
  return static_cast<std::uint32_t>(header[128]) | (static_cast<std::uint32_t>(header[129]) << 8) |
         (static_cast<std::uint32_t>(header[130]) << 16) | (static_cast<std::uint32_t>(header[131]) << 24);
}

[[nodiscard]] std::string toLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

[[nodiscard]] std::string formatFloat(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.4g", value);
  return buffer;
}

// The texture cook names its artifact after the --stamp stem. Short and
// unique by the texture's manifest position: flattening the whole logical
// path overflows Windows' 260-character path limit for Bistro's deepest
// textures once the cooker appends its metadata/temp suffixes.
[[nodiscard]] std::string stampStem(std::size_t index, const std::string& logicalPath) {
  std::string basename = logicalPath.substr(logicalPath.rfind('/') + 1);
  const auto dot = basename.rfind('.');
  if (dot != std::string::npos) basename.resize(dot);
  char prefix[16];
  std::snprintf(prefix, sizeof prefix, "t%03zu_", index);
  return prefix + basename;
}

[[nodiscard]] bool writeFile(const fs::path& path, const void* data, std::size_t size) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  out.flush();
  return out.good();
}

}  // namespace

std::string materialLogicalPath(const std::string& name, std::size_t index) {
  return name + "/materials/" + std::to_string(index) + ".material.txt";
}

std::vector<unsigned char> whiteFallbackDds() {
  std::vector<unsigned char> bytes(148 + 16, 0);
  auto put32 = [&](std::size_t offset, std::uint32_t value) {
    for (int b = 0; b < 4; ++b) bytes[offset + b] = static_cast<unsigned char>(value >> (8 * b));
  };
  std::memcpy(bytes.data(), "DDS ", 4);
  put32(4, 124);          // dwSize
  put32(8, 0x00081007);   // CAPS | HEIGHT | WIDTH | PIXELFORMAT | LINEARSIZE
  put32(12, 4);           // height
  put32(16, 4);           // width
  put32(20, 16);          // linear size: one 16-byte block
  put32(76, 32);          // DDPIXELFORMAT.dwSize
  put32(80, 0x4);         // DDPF_FOURCC
  std::memcpy(bytes.data() + 84, "DX10", 4);
  put32(108, 0x1000);     // DDSCAPS_TEXTURE
  put32(128, 99);         // DXGI_FORMAT_BC7_UNORM
  put32(132, 3);          // D3D10_RESOURCE_DIMENSION_TEXTURE2D
  put32(140, 1);          // arraySize
  // One BC7 mode-6 block: all eight 7-bit endpoints 127 with both p-bits 1
  // (so every endpoint channel is 255) and all indices 0 -> opaque white.
  const unsigned char block[16] = {0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0};
  std::memcpy(bytes.data() + 148, block, sizeof block);
  return bytes;
}

atlantis::Result<std::monostate, GltfImportError> checkMaterials(const cgltf_data& data, const fs::path& contentRoot) {
  std::map<std::string, TextureUsage> usageByUri;
  for (cgltf_size i = 0; i < data.materials_count; ++i) {
    const cgltf_material& m = data.materials[i];
    if (hasOutOfScopeExtension(m)) return CheckResult::Err(GltfImportError::UnsupportedMaterialExtension);

    const float* factors[] = {m.pbr_metallic_roughness.base_color_factor, m.pbr_specular_glossiness.diffuse_factor};
    for (const float* f : factors) {
      for (int c = 0; c < 4; ++c) {
        if (!isUnitFactor(f[c])) return CheckResult::Err(GltfImportError::InvalidMaterialFactor);
      }
    }
    for (int c = 0; c < 3; ++c) {
      if (!isUnitFactor(m.pbr_specular_glossiness.specular_factor[c])) {
        return CheckResult::Err(GltfImportError::InvalidMaterialFactor);
      }
    }
    if (!isUnitFactor(m.pbr_metallic_roughness.metallic_factor) ||
        !isUnitFactor(m.pbr_metallic_roughness.roughness_factor) ||
        !isUnitFactor(m.pbr_specular_glossiness.glossiness_factor)) {
      return CheckResult::Err(GltfImportError::InvalidMaterialFactor);
    }

    const MaterialTextures used = usedTextures(m);
    for (const auto& [view, usage] : {std::pair{used.baseColor, TextureUsage::Color}, std::pair{used.normal, TextureUsage::Data},
                                      std::pair{used.emissive, TextureUsage::Color}}) {
      if (view == nullptr) continue;
      const auto check = checkTextureView(data, *view, contentRoot);
      if (check.isErr()) return check;
      // A PNG/JPG is cooked with an explicit colour space, and one logical
      // path can only carry one; a DDS decides its own (DXGI) and is exempt.
      const auto resolved = resolveTexture(data, *view->texture);
      if (!resolved->isDds) {
        const auto [it, inserted] = usageByUri.emplace(resolved->uri, usage);
        if (!inserted && it->second != usage) return CheckResult::Err(GltfImportError::UnsupportedTextureFeature);
      }
    }
  }
  return CheckResult::Ok(std::monostate{});
}

atlantis::Result<std::monostate, GltfImportError> writeMaterials(const cgltf_data& data, const fs::path& contentRoot,
                                                                 const fs::path& stagingDir, const std::string& name,
                                                                 GltfImportSummary& summary,
                                                                 std::vector<std::string>& reportLines,
                                                                 std::vector<std::string>& manifestLines,
                                                                 std::vector<std::string>& declaredAssets) {
  // Textures in first-reference order, deduplicated by logical path.
  struct TextureEntry {
    std::string logicalPath;
    std::string source;     // manifest --source (placeholder-prefixed)
    std::string assetRoot;  // manifest --asset-root placeholder
    bool isDds = false;
    TextureUsage usage = TextureUsage::Color;
  };
  std::vector<TextureEntry> textures;
  std::map<std::string, std::size_t> textureIndex;
  auto addTexture = [&](TextureEntry entry) {
    if (textureIndex.emplace(entry.logicalPath, textures.size()).second) textures.push_back(std::move(entry));
  };

  std::vector<std::string> materialManifest;
  const std::string whiteLogical = name + "/" + std::string(kWhiteFallbackRelative);
  std::size_t samplerDefaulted = 0;

  for (cgltf_size i = 0; i < data.materials_count; ++i) {
    const cgltf_material& m = data.materials[i];
    const std::string label = "material_" + std::to_string(i) + (m.name != nullptr ? std::string(" (") + m.name + ")" : "");
    const MaterialTextures used = usedTextures(m);

    ParsedMaterialSource source;
    source.kind = MaterialKind::PbrDirectLit;
    std::vector<std::string> dropped;

    if (m.has_pbr_specular_glossiness) {
      const auto& sg = m.pbr_specular_glossiness;
      if (sg.specular_glossiness_texture.texture != nullptr) {
        // Ruling 2: the formula works on factors only, and with a texture
        // present those are multipliers (default 1) that would make the
        // material a pure metal. Dielectric fallback, texture dropped.
        for (int c = 0; c < 4; ++c) source.baseColorFactor[c] = sg.diffuse_factor[c];
        source.metallicFactor = 0.0f;
        source.roughnessFactor = std::clamp(1.0f - sg.glossiness_factor, 0.0f, 1.0f);
        summary.materialsSpecGlossTextureFallback += 1;
        reportLines.push_back(label + ": specularGlossinessTexture dropped; dielectric fallback metallic=0 roughness=" +
                              formatFloat(source.roughnessFactor) + " (Ruling 2)");
      } else {
        SpecularGlossinessFactors in;
        for (int c = 0; c < 4; ++c) in.diffuse[c] = sg.diffuse_factor[c];
        for (int c = 0; c < 3; ++c) in.specular[c] = sg.specular_factor[c];
        in.glossiness = sg.glossiness_factor;
        const MetallicRoughnessFactors out = convertSpecularGlossiness(in);
        for (int c = 0; c < 4; ++c) source.baseColorFactor[c] = out.baseColor[c];
        source.metallicFactor = out.metallic;
        source.roughnessFactor = out.roughness;
        summary.materialsSpecGlossFormula += 1;
        reportLines.push_back(label + ": spec-gloss factors converted (D3, Khronos formula): metallic=" +
                              formatFloat(out.metallic) + " roughness=" + formatFloat(out.roughness));
      }
    } else {
      const auto& mr = m.pbr_metallic_roughness;
      for (int c = 0; c < 4; ++c) source.baseColorFactor[c] = mr.base_color_factor[c];
      source.metallicFactor = mr.metallic_factor;
      source.roughnessFactor = mr.roughness_factor;
      summary.materialsMetallicRoughness += 1;
      if (mr.metallic_roughness_texture.texture != nullptr) dropped.push_back("metallicRoughnessTexture");
    }

    if (m.has_transmission) {
      summary.materialsTransmission += 1;
      reportLines.push_back(label + ": KHR_materials_transmission factor " +
                            formatFloat(m.transmission.transmission_factor) +
                            " recorded only; imported as pbr_direct_lit (ADR-0083 D3)");
      if (m.transmission.transmission_texture.texture != nullptr) dropped.push_back("transmissionTexture");
    }

    // Base colour texture, or the white fallback (Ruling 7).
    const cgltf_sampler* sampler = nullptr;
    if (used.baseColor != nullptr) {
      const ResolvedTexture resolved = *resolveTexture(data, *used.baseColor->texture);
      source.textureLogicalPath = textureLogicalPath(contentRoot, resolved.uri);
      sampler = resolved.sampler;
      addTexture({source.textureLogicalPath, "{content_parent}/" + source.textureLogicalPath, "{content_parent}",
                  resolved.isDds, TextureUsage::Color});
    } else {
      source.textureLogicalPath = whiteLogical;
      summary.materialsWhiteFallback += 1;
      reportLines.push_back(label + ": no base-color texture; white 4x4 BC7 fallback (Ruling 7)");
      addTexture({whiteLogical, "{import_dir}/" + whiteLogical, "{import_dir}", true, TextureUsage::Color});
    }
    if (used.normal != nullptr) {
      const ResolvedTexture resolved = *resolveTexture(data, *used.normal->texture);
      source.normalMapLogicalPath = textureLogicalPath(contentRoot, resolved.uri);
      if (sampler == nullptr) sampler = resolved.sampler;
      addTexture({source.normalMapLogicalPath, "{content_parent}/" + source.normalMapLogicalPath, "{content_parent}",
                  resolved.isDds, TextureUsage::Data});
      if (m.normal_texture.scale != 1.0f) dropped.push_back("normalTexture.scale=" + formatFloat(m.normal_texture.scale));
    }
    if (sampler == nullptr) samplerDefaulted += 1;
    applySampler(sampler, source);

    // Spec 0041 Requirement 8 (rulings O3, Q3), widened by Plan 0046
    // Milestone 1 (ADR-0096, Plan 0046 P5): material v9 has both
    // destinations, emissiveFactor and emissive_texture. An in-range factor
    // is mapped, with its texture when it has one; a factor outside
    // [0, 65504] (or non-finite) is dropped with its texture rather than
    // failing the whole cook later. A texture with a zero factor is inert
    // under glTF's factor x texture rule and is not mapped. Each case gets
    // its own report line.
    const bool hasEmissiveFactor = hasNonZeroEmissiveFactor(m);
    const bool hasEmissiveTexture = m.emissive_texture.texture != nullptr;
    if (hasEmissiveFactor) {
      const std::string factorText = "emissiveFactor=(" + formatFloat(m.emissive_factor[0]) + "," +
                                     formatFloat(m.emissive_factor[1]) + "," + formatFloat(m.emissive_factor[2]) + ")";
      if (!emissiveFactorInRange(m)) {
        reportLines.push_back(label + ": " + factorText + " dropped" +
                              (hasEmissiveTexture ? std::string(" with its emissiveTexture") : std::string()) +
                              ", outside the emissive range [0, 65504] (Spec 0041 R8)");
      } else {
        for (int c = 0; c < 3; ++c) source.emissiveFactor[c] = m.emissive_factor[c];
        if (used.emissive != nullptr) {
          const ResolvedTexture resolved = *resolveTexture(data, *used.emissive->texture);
          source.emissiveTextureLogicalPath = textureLogicalPath(contentRoot, resolved.uri);
          addTexture({source.emissiveTextureLogicalPath, "{content_parent}/" + source.emissiveTextureLogicalPath,
                      "{content_parent}", resolved.isDds, TextureUsage::Color});
          reportLines.push_back(label + ": " + factorText + " mapped with emissiveTexture " +
                                source.emissiveTextureLogicalPath + " (ADR-0096)");
        } else {
          reportLines.push_back(label + ": " + factorText + " mapped (Spec 0041 R8)");
        }
      }
    } else if (hasEmissiveTexture) {
      reportLines.push_back(label + ": emissiveTexture inert (emissiveFactor 0), not mapped (ADR-0096)");
    }

    // Spec 0042 Requirement 9 (ruling O1): MASK -> Mask + alphaCutoff
    // (cgltf fills glTF's 0.5 default when the file omits it), BLEND ->
    // Blend. Transmission materials stay Opaque whatever their alphaMode
    // (ruling O3: whether glass renders as Blend is Spec 0036 (7)'s
    // decision), and a cutoff outside [0, 1] keeps the 0.5 default rather
    // than failing the whole cook later. Each case gets its own report line.
    if (m.alpha_mode == cgltf_alpha_mode_mask || m.alpha_mode == cgltf_alpha_mode_blend) {
      const bool isMask = m.alpha_mode == cgltf_alpha_mode_mask;
      const std::string modeText = isMask ? "alphaMode=MASK alphaCutoff=" + formatFloat(m.alpha_cutoff)
                                          : std::string("alphaMode=BLEND");
      if (m.has_transmission) {
        reportLines.push_back(label + ": " + modeText +
                              " not mapped, transmission material stays opaque (Spec 0042 O3)");
      } else if (!isMask) {
        source.alphaMode = MaterialAlphaMode::Blend;
        reportLines.push_back(label + ": " + modeText + " mapped (Spec 0042 R9)");
      } else {
        source.alphaMode = MaterialAlphaMode::Mask;
        const float cutoff = m.alpha_cutoff;
        if (std::isfinite(cutoff) && cutoff >= 0.0f && cutoff <= 1.0f) {
          source.alphaCutoff = cutoff;
          reportLines.push_back(label + ": " + modeText + " mapped (Spec 0042 R9)");
        } else {
          reportLines.push_back(label + ": " + modeText +
                                " mapped as MASK, cutoff outside [0, 1] replaced by 0.5 (Spec 0042 R9)");
        }
      }
    }

    // Ruling 3: properties v9 has no destination for.
    if (m.double_sided) dropped.push_back("doubleSided");
    if (m.occlusion_texture.texture != nullptr) dropped.push_back("occlusionTexture");
    if (!dropped.empty()) {
      std::string line = label + ": no v9 destination, dropped (Ruling 3):";
      for (const std::string& d : dropped) line += " " + d;
      reportLines.push_back(line);
    }

    const std::string text = atlantis::asset_system::serializeMaterialSource(source);
    const std::string logical = materialLogicalPath(name, i);
    if (!writeFile(stagingDir / logical, text.data(), text.size())) {
      return CheckResult::Err(GltfImportError::OutputWriteFailed);
    }
    declaredAssets.push_back(logical);
    materialManifest.push_back("--kind=material --source={import_dir}/" + logical +
                               " --asset-root={import_dir} --output-dir={cooked_dir}");
    summary.materialCount += 1;
  }

  if (summary.materialsWhiteFallback > 0) {
    const std::vector<unsigned char> dds = whiteFallbackDds();
    if (!writeFile(stagingDir / whiteLogical, dds.data(), dds.size())) {
      return CheckResult::Err(GltfImportError::OutputWriteFailed);
    }
  }

  if (samplerDefaulted > 0) {
    reportLines.push_back("sampler: " + std::to_string(samplerDefaulted) +
                          " materials' textures reference no glTF sampler; glTF default applied -> filter linear "
                          "(implementation-defined in glTF), address repeat");
  }

  // Ruling 4: the DDS file's own DXGI format decides the colour space; a
  // base-colour-named (*_diff*) file stored linear is reported, not changed.
  const fs::path contentParent = contentRootDirectory(contentRoot).parent_path();
  for (const TextureEntry& t : textures) {
    if (!t.isDds || t.assetRoot != "{content_parent}") continue;
    if (toLower(t.logicalPath).find("_diff") == std::string::npos) continue;
    const auto dxgi = ddsDxgiFormat(contentParent / t.logicalPath);
    if (dxgi && *dxgi == 100) continue;
    summary.colorSpaceWarnings += 1;
    reportLines.push_back("colour-space warning (Ruling 4): " + t.logicalPath + " is named *_diff* but DXGI " +
                          (dxgi ? std::to_string(*dxgi) : std::string("unknown")) +
                          " is not BC7_UNORM_SRGB (100); it will be sampled as linear (file wins)");
  }

  for (std::size_t i = 0; i < textures.size(); ++i) {
    const TextureEntry& t = textures[i];
    declaredAssets.push_back(t.logicalPath);
    std::string line = "--kind=texture --source=" + t.source + " --asset-root=" + t.assetRoot +
                       " --output-dir={cooked_dir} --stamp={cooked_dir}/" + stampStem(i, t.logicalPath) + ".stamp";
    if (!t.isDds) line += t.usage == TextureUsage::Color ? " --color-space=srgb" : " --color-space=unorm";
    manifestLines.push_back(line);
  }
  summary.texturesReferenced += static_cast<std::uint32_t>(textures.size());
  manifestLines.insert(manifestLines.end(), materialManifest.begin(), materialManifest.end());

  reportLines.push_back("materials: " + std::to_string(summary.materialCount) + " (spec-gloss formula " +
                        std::to_string(summary.materialsSpecGlossFormula) + ", spec-gloss texture fallback " +
                        std::to_string(summary.materialsSpecGlossTextureFallback) + ", metallic-roughness " +
                        std::to_string(summary.materialsMetallicRoughness) + "; transmission " +
                        std::to_string(summary.materialsTransmission) + ", white base-color fallback " +
                        std::to_string(summary.materialsWhiteFallback) + "); textures referenced " +
                        std::to_string(summary.texturesReferenced) + "; colour-space warnings " +
                        std::to_string(summary.colorSpaceWarnings));
  return CheckResult::Ok(std::monostate{});
}

}  // namespace atlantis::gltf_importer::detail
