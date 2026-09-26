#include "scene_import.h"

#include "material_import.h"
#include "scene_transform.h"

#include <atlantis/asset_system/scene_source.h>
#include <atlantis/asset_system/scene_types.h>
#include <cgltf.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <system_error>

// Plan 0037 Milestone 5: the glTF default scene -> .scene.txt v4 (ADR-0083
// D5). The hierarchy is kept, not baked: every node becomes one node: line
// with its LOCAL transform and parent=, so Atlantis World composes world
// matrices exactly as glTF does (both are column-vector T*R*S chains, D6).

namespace atlantis::gltf_importer::detail {

namespace {

namespace fs = std::filesystem;
using CheckResult = atlantis::Result<std::monostate, GltfImportError>;

[[nodiscard]] const cgltf_scene* defaultScene(const cgltf_data& data) {
  if (data.scene != nullptr) return data.scene;
  return data.scenes_count > 0 ? &data.scenes[0] : nullptr;
}

[[nodiscard]] std::size_t indexOf(const cgltf_data& data, const cgltf_node* node) {
  return static_cast<std::size_t>(node - data.nodes);
}

[[nodiscard]] atlantis::Result<DecomposedTransform, GltfImportError> localTransform(const cgltf_node& node) {
  using ResultT = atlantis::Result<DecomposedTransform, GltfImportError>;
  if (node.has_matrix) {
    std::array<double, 16> m{};
    for (int i = 0; i < 16; ++i) m[i] = node.matrix[i];
    return decomposeMatrix(m);
  }
  DecomposedTransform t;
  std::array<double, 4> q{0.0, 0.0, 0.0, 1.0};
  for (int i = 0; i < 3; ++i) {
    if (node.has_translation) t.translation[i] = node.translation[i];
    if (node.has_scale) t.scale[i] = node.scale[i];
  }
  if (node.has_rotation) {
    for (int i = 0; i < 4; ++i) q[i] = node.rotation[i];
  }
  for (const double v : {t.translation[0], t.translation[1], t.translation[2], t.scale[0], t.scale[1], t.scale[2], q[0],
                         q[1], q[2], q[3]}) {
    if (!std::isfinite(v)) return ResultT::Err(GltfImportError::InvalidNodeTransform);
  }
  if (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] < 1e-12) {
    return ResultT::Err(GltfImportError::InvalidNodeTransform);
  }
  // An odd number of negative scale components mirrors: rejected rather
  // than folded, so no handedness flip reaches the mesh winding.
  if (t.scale[0] * t.scale[1] * t.scale[2] < 0.0) return ResultT::Err(GltfImportError::NegativeDeterminant);
  t.eulerRadians = eulerFromQuaternion(q);
  return ResultT::Ok(t);
}

[[nodiscard]] bool isUnit(float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; }

struct LightLine {
  bool directional = false;
  std::array<float, 3> color{1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 0.0f;
};

struct SceneLine {
  std::uint32_t id = 0;
  std::optional<std::uint32_t> parent;
  DecomposedTransform transform;
  std::optional<std::string> mesh;
  std::optional<std::string> material;
  std::optional<LightLine> light;
};

[[nodiscard]] std::string formatFloat(float value) {
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
  return std::string(buffer, result.ptr);
}

[[nodiscard]] std::string formatTriple(const std::array<double, 3>& v) {
  return formatFloat(static_cast<float>(v[0])) + ' ' + formatFloat(static_cast<float>(v[1])) + ' ' +
         formatFloat(static_cast<float>(v[2]));
}

// The .scene.txt v4 grammar (scene_source.cpp), written with shortest
// round-trip floats: serializeSceneSource() prints std::to_string's fixed
// six decimals, which would lose precision on imported transforms. Plan
// 0046 Milestone 2 (ADR-0094 Decision 3): the overlay's node lines are
// appended verbatim after the imported ones (serializeSceneSource()'s own
// lines -- six decimals is exact enough for hand-authored values) and its
// camera becomes the active one.
[[nodiscard]] std::string serialize(const std::vector<SceneLine>& lines, const std::vector<std::string>& overlayLines,
                                    std::optional<std::uint32_t> activeCamera) {
  std::string out = "atlantis_scene_source_version: 6\nnode_count: " +
                    std::to_string(lines.size() + overlayLines.size()) + "\nactive_camera: " +
                    (activeCamera ? std::to_string(*activeCamera) : std::string("none")) + "\n";
  for (const SceneLine& l : lines) {
    out += "node: node_id=" + std::to_string(l.id) + " parent=" + (l.parent ? std::to_string(*l.parent) : "none") +
           " position=" + formatTriple(l.transform.translation) + " rotation=" + formatTriple(l.transform.eulerRadians) +
           " scale=" + formatTriple(l.transform.scale);
    if (l.mesh) {
      out += " mesh=" + *l.mesh;
      if (l.material) out += " material=" + *l.material;
    } else if (l.light) {
      out += std::string(" light=") + (l.light->directional ? "directional" : "point") + " color=" +
             formatFloat(l.light->color[0]) + ' ' + formatFloat(l.light->color[1]) + ' ' +
             formatFloat(l.light->color[2]) + " intensity=" + formatFloat(l.light->intensity);
      if (!l.light->directional) out += " range=" + formatFloat(l.light->range);
    }
    out += '\n';
  }
  for (const std::string& line : overlayLines) out += line + '\n';
  return out;
}

// Plan 0046 Milestone 2 (ADR-0094 Decision 3): the overlay's node lines,
// renumbered from firstId in declaration order (parents inside the overlay
// with them), and its active camera under the new numbering.
struct RenumberedOverlay {
  std::vector<std::string> nodeLines;
  std::optional<std::uint32_t> activeCamera;
};

[[nodiscard]] RenumberedOverlay renumberOverlay(const atlantis::asset_system::ParsedSceneSource& overlay,
                                                std::uint32_t firstId) {
  std::map<std::uint32_t, std::uint32_t> newId;
  for (std::size_t i = 0; i < overlay.nodes.size(); ++i) {
    newId.emplace(overlay.nodes[i].nodeId, firstId + static_cast<std::uint32_t>(i));
  }
  atlantis::asset_system::ParsedSceneSource renumbered;
  for (const atlantis::asset_system::ParsedSceneNode& node : overlay.nodes) {
    atlantis::asset_system::ParsedSceneNode copy = node;
    copy.nodeId = newId.at(node.nodeId);
    if (node.parentNodeId) copy.parentNodeId = newId.at(*node.parentNodeId);  // checked by checkScene()
    renumbered.nodes.push_back(std::move(copy));
  }
  RenumberedOverlay out;
  for (const atlantis::asset_system::ParsedSceneNode& node : renumbered.nodes) {
    if (node.camera) out.activeCamera = node.nodeId;
  }
  const std::string text = atlantis::asset_system::serializeSceneSource(renumbered);
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start);
    if (line.rfind("node: ", 0) == 0) out.nodeLines.push_back(line);
    start = end + 1;
  }
  return out;
}

}  // namespace

std::string meshLogicalPath(const std::string& name, std::size_t meshIndex, std::size_t primitiveIndex) {
  return "meshes/" + name + "/mesh_" + std::to_string(meshIndex) + "_" + std::to_string(primitiveIndex);
}

atlantis::Result<std::monostate, GltfImportError> checkScene(const cgltf_data& data,
                                                             const atlantis::asset_system::ParsedSceneSource* overlay) {
  const cgltf_scene* scene = defaultScene(data);
  if (scene == nullptr) return CheckResult::Ok(std::monostate{});

  // Plan 0046 Milestone 2 (ADR-0094 Decision 3, Plan 0046 P7): the overlay
  // holds only non-renderable nodes (the camera and lights), at most one
  // camera, and parents only inside itself.
  std::size_t directional = 0;
  std::size_t point = 0;
  if (overlay != nullptr) {
    std::set<std::uint32_t> overlayIds;
    for (const auto& node : overlay->nodes) overlayIds.insert(node.nodeId);
    std::size_t cameras = 0;
    for (const auto& node : overlay->nodes) {
      if (node.meshLogicalPath) return CheckResult::Err(GltfImportError::OverlayRenderableNode);
      if (node.parentNodeId && !overlayIds.contains(*node.parentNodeId)) {
        return CheckResult::Err(GltfImportError::OverlayParentOutsideOverlay);
      }
      if (node.camera) cameras += 1;
      if (node.light) {
        (node.light->kind == atlantis::asset_system::DecodedLightKind::Directional ? directional : point) += 1;
      }
    }
    if (cameras > 1) return CheckResult::Err(GltfImportError::OverlaySecondCamera);
  }

  std::set<const cgltf_node*> visited;
  std::vector<const cgltf_node*> stack(scene->nodes, scene->nodes + scene->nodes_count);
  while (!stack.empty()) {
    const cgltf_node* node = stack.back();
    stack.pop_back();
    // A node reachable twice would become two node: lines with one id.
    if (!visited.insert(node).second) return CheckResult::Err(GltfImportError::MalformedGltf);
    const auto transform = localTransform(*node);
    if (transform.isErr()) return CheckResult::Err(transform.error());
    if (const cgltf_light* light = node->light) {
      if (light->type == cgltf_light_type_spot) return CheckResult::Err(GltfImportError::UnsupportedLightType);
      if (!isUnit(light->color[0]) || !isUnit(light->color[1]) || !isUnit(light->color[2]) ||
          !std::isfinite(light->intensity) || light->intensity < 0.0f) {
        return CheckResult::Err(GltfImportError::InvalidLightValue);
      }
      (light->type == cgltf_light_type_directional ? directional : point) += 1;
    }
    for (cgltf_size c = 0; c < node->children_count; ++c) stack.push_back(node->children[c]);
  }
  // The grammar's cap -- 1 directional, kMaxPointLightsPerScene (64) point
  // since Spec 0040, not Spec 0019's stale 1 + 4 (Plan 0046 P7) -- over the
  // imported and overlay lights together, checked here so it surfaces as a
  // named import error, not a cook failure.
  if (directional > 1 || point > atlantis::asset_system::kMaxPointLightsPerScene) {
    return CheckResult::Err(GltfImportError::TooManyLights);
  }
  return CheckResult::Ok(std::monostate{});
}

atlantis::Result<std::monostate, GltfImportError> writeScene(const cgltf_data& data, const fs::path& stagingDir,
                                                             const std::string& name, GltfImportSummary& summary,
                                                             std::vector<std::string>& reportLines,
                                                             std::vector<std::string>& manifestLines,
                                                             const atlantis::asset_system::ParsedSceneSource* overlay) {
  const cgltf_scene* scene = defaultScene(data);
  if (scene == nullptr) {
    reportLines.push_back("scene: the glTF defines no scene; no .scene.txt written" +
                          std::string(overlay != nullptr ? " (the overlay is not applied)" : ""));
    return CheckResult::Ok(std::monostate{});
  }

  std::vector<SceneLine> lines;
  // node_id = glTF node index + 1; synthetic children (a second primitive,
  // or a light on a node that already carries a mesh -- the grammar allows
  // one of mesh/camera/light per line) are numbered after every glTF id, in
  // emission order.
  std::uint32_t nextSyntheticId = static_cast<std::uint32_t>(data.nodes_count) + 1;
  std::map<const cgltf_mesh*, std::size_t> meshInstances;

  struct Frame {
    const cgltf_node* node;
    std::optional<std::uint32_t> parent;
    std::size_t depth;
  };
  std::vector<Frame> stack;
  for (cgltf_size r = scene->nodes_count; r > 0; --r) stack.push_back({scene->nodes[r - 1], std::nullopt, 1});

  while (!stack.empty()) {
    const Frame frame = stack.back();
    stack.pop_back();
    const cgltf_node& node = *frame.node;
    const std::size_t nodeIndex = indexOf(data, frame.node);
    const std::uint32_t id = static_cast<std::uint32_t>(nodeIndex) + 1;
    summary.sceneMaxDepth = std::max<std::uint32_t>(summary.sceneMaxDepth, static_cast<std::uint32_t>(frame.depth));

    SceneLine line;
    line.id = id;
    line.parent = frame.parent;
    line.transform = localTransform(node).value();  // checked by checkScene()
    const auto& s = line.transform.scale;
    if (s[0] != s[1] || s[1] != s[2]) summary.nonUniformScaleNodes += 1;

    std::vector<SceneLine> synthetic;
    auto syntheticChild = [&]() {
      SceneLine child;
      child.id = nextSyntheticId++;
      child.parent = id;
      summary.syntheticNodes += 1;
      return child;
    };

    if (const cgltf_mesh* mesh = node.mesh) {
      meshInstances[mesh] += 1;
      const std::size_t meshIndex = static_cast<std::size_t>(mesh - data.meshes);
      for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
        std::optional<std::string> material;
        if (const cgltf_material* m = mesh->primitives[p].material) {
          material = materialLogicalPath(name, static_cast<std::size_t>(m - data.materials));
        } else {
          summary.primitivesWithoutMaterial += 1;
        }
        const std::string path = meshLogicalPath(name, meshIndex, p);
        if (mesh->primitives_count == 1) {
          line.mesh = path;
          line.material = material;
        } else {
          SceneLine child = syntheticChild();
          child.mesh = path;
          child.material = material;
          synthetic.push_back(std::move(child));
        }
        summary.sceneMeshLines += 1;
      }
    }

    if (const cgltf_light* light = node.light) {
      LightLine l;
      l.directional = light->type == cgltf_light_type_directional;
      l.color = {light->color[0], light->color[1], light->color[2]};
      l.intensity = light->intensity;
      l.range = kImportedPointLightRangePlaceholder;
      const std::string label = "node_" + std::to_string(nodeIndex) + (node.name ? std::string(" (") + node.name + ")" : "");
      if (l.directional) {
        reportLines.push_back("light: " + label + " directional, intensity " + formatFloat(l.intensity) +
                              " recorded as the raw glTF value (lux; meaning belongs to workflow 2's ADR, Ruling 8)");
      } else {
        const std::string glTFRange = light->range > 0.0f ? formatFloat(light->range) : std::string("undefined");
        reportLines.push_back("light: " + label + " point, intensity " + formatFloat(l.intensity) +
                              " recorded as the raw glTF value (candela; Ruling 8); glTF range " + glTFRange +
                              " discarded, range= set to kImportedPointLightRangePlaceholder = " +
                              formatFloat(kImportedPointLightRangePlaceholder) +
                              " (approximately infinite, glTF's default; real attenuation model belongs to Spec 0036 "
                              "workflow 2)");
      }
      if (line.mesh) {
        SceneLine child = syntheticChild();
        child.light = l;
        synthetic.push_back(std::move(child));
      } else {
        line.light = l;
      }
      summary.sceneLightLines += 1;
    }

    if (node.camera != nullptr) {
      summary.camerasDropped += 1;
      reportLines.push_back("camera: node_" + std::to_string(nodeIndex) +
                            " carries a glTF camera; imported as a plain transform node, camera dropped (Spec 0037 "
                            "Non-Goal)");
    }

    lines.push_back(std::move(line));
    for (SceneLine& child : synthetic) lines.push_back(std::move(child));
    for (cgltf_size c = node.children_count; c > 0; --c) {
      stack.push_back({node.children[c - 1], id, frame.depth + 1});
    }
  }

  for (const auto& [mesh, count] : meshInstances) {
    if (count > 1) summary.meshesInstancedMoreThanOnce += 1;
  }
  // Plan 0046 Milestone 2 (ADR-0094 Decision 3): the overlay's nodes follow
  // every imported and synthetic id.
  RenumberedOverlay merged;
  if (overlay != nullptr) {
    merged = renumberOverlay(*overlay, nextSyntheticId);
    std::size_t overlayLights = 0;
    for (const auto& node : overlay->nodes) overlayLights += node.light ? 1 : 0;
    summary.overlayNodeLines = static_cast<std::uint32_t>(merged.nodeLines.size());
    summary.sceneLightLines += static_cast<std::uint32_t>(overlayLights);
    reportLines.push_back("overlay: " + std::to_string(merged.nodeLines.size()) + " node lines appended from id " +
                          std::to_string(nextSyntheticId) + " (" + std::to_string(overlayLights) + " light)" +
                          (merged.activeCamera ? ", active_camera " + std::to_string(*merged.activeCamera)
                                               : std::string(", no camera")) +
                          " (ADR-0094)");
  }
  summary.sceneNodeLines = static_cast<std::uint32_t>(lines.size() + merged.nodeLines.size());

  const std::string logical = name + "/" + name + ".scene.txt";
  const std::string text = serialize(lines, merged.nodeLines, merged.activeCamera);
  // The merged scene against the grammar itself (ADR-0094 Decision 3) --
  // checkScene() already enforced every rule it can fail on.
  if (overlay != nullptr && atlantis::asset_system::parseSceneSource(text).isErr()) {
    return CheckResult::Err(GltfImportError::OverlayMalformed);
  }
  std::error_code ec;
  fs::create_directories((stagingDir / logical).parent_path(), ec);
  std::ofstream out(stagingDir / logical, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return CheckResult::Err(GltfImportError::OutputWriteFailed);
  out << text;
  out.flush();
  if (!out.good()) return CheckResult::Err(GltfImportError::OutputWriteFailed);

  manifestLines.push_back("--kind=scene --source={import_dir}/" + logical +
                          " --asset-root={import_dir} --output-dir={cooked_dir}");
  reportLines.push_back("scene: " + std::to_string(summary.sceneNodeLines) + " node lines (" +
                        std::to_string(summary.sceneMeshLines) + " mesh, " + std::to_string(summary.sceneLightLines) +
                        " light, " + std::to_string(summary.syntheticNodes) + " synthetic), max depth " +
                        std::to_string(summary.sceneMaxDepth) + ", " + std::to_string(summary.meshesInstancedMoreThanOnce) +
                        " meshes instanced more than once, " + std::to_string(summary.nonUniformScaleNodes) +
                        " non-uniform-scale nodes, " + std::to_string(summary.primitivesWithoutMaterial) +
                        " mesh lines without material, " + std::to_string(summary.camerasDropped) + " cameras dropped");
  return CheckResult::Ok(std::monostate{});
}

}  // namespace atlantis::gltf_importer::detail
