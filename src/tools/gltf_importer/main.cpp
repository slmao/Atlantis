#include "import_command.h"

#include <filesystem>
#include <iostream>
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
                 " --content-root=<dir>\n";
    return 2;
  }

  const auto result = atlantis::gltf_importer::importGltfMeshes(fs::path(input), fs::path(contentRoot),
                                                                fs::path(outputDir), name);
  if (result.isErr()) {
    std::cerr << "atlantis_gltf_importer: " << atlantis::gltf_importer::gltfImportErrorMessage(result.error())
              << "\n";
    return 1;
  }
  const auto& summary = result.value();
  std::cout << "atlantis_gltf_importer: " << summary.meshCount << " meshes, " << summary.totalVertices
            << " vertices, " << summary.totalIndices << " indices (" << summary.meshesOverU16Range
            << " over u16 range) -> " << outputDir << "\n";
  return 0;
}
