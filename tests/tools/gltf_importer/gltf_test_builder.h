#pragma once

// Builds tiny single-primitive glTF files in memory for the importer's unit
// tests -- the buffer is embedded as a base64 data: URI, so no fixture files
// exist on disk beyond the .gltf each test writes to its own temp directory.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gltf_test {

struct PrimitiveSpec {
  std::vector<float> positions;  // xyz per vertex
  std::vector<float> normals;    // xyz per vertex; empty = attribute omitted
  std::vector<float> uvs;        // uv per vertex; empty = attribute omitted
  std::vector<std::uint8_t> colorsRgbaU8;  // COLOR_0 as normalized VEC4 u8; empty = omitted
  std::vector<float> tangents;             // xyzw per vertex; empty = omitted
  std::vector<std::uint32_t> indices;
  int indexComponentType = 5125;  // 5121 u8, 5123 u16, 5125 u32
  bool includeIndices = true;
  bool includePosition = true;
  int mode = 4;
  // > 0: shrinks the POSITION bufferView by this many bytes so its accessor
  // reaches past the view's end.
  std::uint32_t truncatePositionViewBytes = 0;
  // Material-slice fixtures: raw JSON spliced into the document. The
  // primitive references material 0 when materialsJson is non-empty.
  std::string materialsJson;   // e.g. [{...}]
  std::string texturesJson;
  std::string imagesJson;
  std::string samplersJson;
  std::string extensionsUsedJson;  // e.g. ["MSFT_texture_dds"]
};

inline std::string base64(const std::vector<std::uint8_t>& bytes) {
  static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const std::uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += kAlphabet[(n >> 6) & 63];
    out += kAlphabet[n & 63];
  }
  if (i + 1 == bytes.size()) {
    const std::uint32_t n = bytes[i] << 16;
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += "==";
  } else if (i + 2 == bytes.size()) {
    const std::uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += kAlphabet[(n >> 6) & 63];
    out += '=';
  }
  return out;
}

inline std::string buildGltf(const PrimitiveSpec& spec) {
  std::vector<std::uint8_t> buffer;
  std::string views;
  std::string accessors;
  std::string attributes;
  int accessorCount = 0;
  int viewCount = 0;

  auto appendView = [&](const void* data, std::size_t size, std::uint32_t truncate) {
    while (buffer.size() % 4 != 0) buffer.push_back(0);
    const std::size_t offset = buffer.size();
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    buffer.insert(buffer.end(), bytes, bytes + size);
    const int viewIndex = viewCount++;
    if (!views.empty()) views += ",";
    views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) +
             ",\"byteLength\":" + std::to_string(size - truncate) + "}";
    return viewIndex;
  };
  auto appendAccessor = [&](int view, int componentType, std::size_t count, const char* type, bool normalized) {
    if (!accessors.empty()) accessors += ",";
    accessors += "{\"bufferView\":" + std::to_string(view) + ",\"componentType\":" + std::to_string(componentType) +
                 ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" +
                 (normalized ? ",\"normalized\":true" : "") + "}";
    return accessorCount++;
  };
  auto addAttribute = [&](const char* name, int accessor) {
    if (!attributes.empty()) attributes += ",";
    attributes += "\"" + std::string(name) + "\":" + std::to_string(accessor);
  };

  const std::size_t vertexCount = spec.positions.size() / 3;
  if (spec.includePosition) {
    const int v = appendView(spec.positions.data(), spec.positions.size() * 4, spec.truncatePositionViewBytes);
    addAttribute("POSITION", appendAccessor(v, 5126, vertexCount, "VEC3", false));
  }
  if (!spec.normals.empty()) {
    const int v = appendView(spec.normals.data(), spec.normals.size() * 4, 0);
    addAttribute("NORMAL", appendAccessor(v, 5126, spec.normals.size() / 3, "VEC3", false));
  }
  if (!spec.uvs.empty()) {
    const int v = appendView(spec.uvs.data(), spec.uvs.size() * 4, 0);
    addAttribute("TEXCOORD_0", appendAccessor(v, 5126, spec.uvs.size() / 2, "VEC2", false));
  }
  if (!spec.colorsRgbaU8.empty()) {
    const int v = appendView(spec.colorsRgbaU8.data(), spec.colorsRgbaU8.size(), 0);
    addAttribute("COLOR_0", appendAccessor(v, 5121, spec.colorsRgbaU8.size() / 4, "VEC4", true));
  }
  if (!spec.tangents.empty()) {
    const int v = appendView(spec.tangents.data(), spec.tangents.size() * 4, 0);
    addAttribute("TANGENT", appendAccessor(v, 5126, spec.tangents.size() / 4, "VEC4", false));
  }
  std::string indicesMember;
  if (spec.includeIndices) {
    std::vector<std::uint8_t> packed;
    const std::size_t width = spec.indexComponentType == 5121 ? 1 : spec.indexComponentType == 5123 ? 2 : 4;
    for (const std::uint32_t index : spec.indices) {
      for (std::size_t b = 0; b < width; ++b) packed.push_back(static_cast<std::uint8_t>(index >> (8 * b)));
    }
    const int v = appendView(packed.data(), packed.size(), 0);
    indicesMember = ",\"indices\":" +
                    std::to_string(appendAccessor(v, spec.indexComponentType, spec.indices.size(), "SCALAR", false));
  }

  std::string extra;
  if (!spec.materialsJson.empty()) extra += ",\"materials\":" + spec.materialsJson;
  if (!spec.texturesJson.empty()) extra += ",\"textures\":" + spec.texturesJson;
  if (!spec.imagesJson.empty()) extra += ",\"images\":" + spec.imagesJson;
  if (!spec.samplersJson.empty()) extra += ",\"samplers\":" + spec.samplersJson;
  if (!spec.extensionsUsedJson.empty()) extra += ",\"extensionsUsed\":" + spec.extensionsUsedJson;
  const std::string materialMember = spec.materialsJson.empty() ? "" : ",\"material\":0";

  return "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
         "\"meshes\":[{\"primitives\":[{\"attributes\":{" +
         attributes + "}" + indicesMember + materialMember + ",\"mode\":" + std::to_string(spec.mode) +
         "}]}]" + extra + ",\"accessors\":[" + accessors + "],\"bufferViews\":[" + views +
         "],\"buffers\":[{\"byteLength\":" + std::to_string(buffer.size()) +
         ",\"uri\":\"data:application/octet-stream;base64," + base64(buffer) + "\"}]}";
}

// A unit quad in z = 0 facing +Z, UVs following positions (tangent +X,
// handedness +1 everywhere), two triangles.
inline PrimitiveSpec unitQuad() {
  PrimitiveSpec spec;
  spec.positions = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  spec.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
  spec.uvs = {0, 0, 1, 0, 1, 1, 0, 1};
  spec.indices = {0, 1, 2, 0, 2, 3};
  return spec;
}

// A fresh, empty per-test directory under the system temp directory.
inline std::filesystem::path freshDirectory(const std::string& name) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atlantis_gltf_importer_tests" / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

inline std::filesystem::path writeGltf(const std::filesystem::path& dir, const PrimitiveSpec& spec) {
  const std::filesystem::path path = dir / "input.gltf";
  std::ofstream(path, std::ios::binary) << buildGltf(spec);
  return path;
}

inline std::vector<std::byte> readBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  if (!chars.empty()) std::memcpy(bytes.data(), chars.data(), chars.size());
  return bytes;
}

inline float vertexFloat(const std::vector<std::byte>& vertexBytes, std::size_t vertex, std::size_t byteOffset) {
  float value = 0.0f;
  std::memcpy(&value, vertexBytes.data() + vertex * 60 + byteOffset, sizeof value);
  return value;
}

}  // namespace gltf_test
