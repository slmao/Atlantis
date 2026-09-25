#include "import_command.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] bool parseArgumentValue(int argc, char** argv, const std::string& name, std::string& valueOut) {
  const std::string prefix = "--" + name + "=";
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument.rfind(prefix, 0) == 0) {
      valueOut = argument.substr(prefix.size());
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  std::string input;
  std::string outputDir;
  std::string name;
  std::string contentRoot;
  if (!parseArgumentValue(argc, argv, "input", input) || !parseArgumentValue(argc, argv, "output-dir", outputDir) ||
      !parseArgumentValue(argc, argv, "name", name) || !parseArgumentValue(argc, argv, "content-root", contentRoot)) {
    std::cerr << "usage: atlantis_gltf_importer --input=<file.gltf> --output-dir=<dir> --name=<slug>"
                 " --content-root=<dir> [--overlay=<file.scene.txt>]\n";
    return 2;
  }
  // Plan 0046 Milestone 2 (ADR-0094 Decision 3): optional.
  std::string overlay;
  std::optional<fs::path> overlayPath;
  if (parseArgumentValue(argc, argv, "overlay", overlay)) overlayPath = fs::path(overlay);

  const auto result = atlantis::gltf_importer::importGltf(fs::path(input), fs::path(contentRoot),
                                                                fs::path(outputDir), name, overlayPath);
  if (result.isErr()) {
    std::cerr << "atlantis_gltf_importer: " << atlantis::gltf_importer::gltfImportErrorMessage(result.error())
              << "\n";
    return 1;
  }
  const auto& summary = result.value();
  std::cout << "atlantis_gltf_importer: " << summary.meshCount << " meshes, " << summary.totalVertices
            << " vertices, " << summary.totalIndices << " indices (" << summary.meshesOverU16Range
            << " over u16 range); handedness split: " << summary.splitVertices << " vertices duplicated in "
            << summary.meshesSplit << " meshes; degenerate-basis fallback: " << summary.degenerateFallbackVertices
            << " vertices in " << summary.meshesWithDegenerateFallback << " meshes; " << summary.materialCount
            << " materials (" << summary.materialsSpecGlossFormula << " spec-gloss formula, "
            << summary.materialsSpecGlossTextureFallback << " spec-gloss texture fallback, "
            << summary.materialsMetallicRoughness << " metallic-roughness; " << summary.materialsTransmission
            << " transmission, " << summary.materialsWhiteFallback << " white fallback), "
            << summary.texturesReferenced << " textures, " << summary.colorSpaceWarnings
            << " colour-space overrides; scene: " << summary.sceneNodeLines << " node lines (" << summary.sceneMeshLines
            << " mesh, " << summary.sceneLightLines << " light, " << summary.overlayNodeLines << " overlay), depth " << summary.sceneMaxDepth << " -> " << outputDir
            << "\n";
  return 0;
}
