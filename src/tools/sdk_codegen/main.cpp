// atlantis_sdk_codegen (Plan 0057 P3, ADR-0111 D1; Plan 0058 P5, ADR-0112
// D3): writes World's typed bindings from world::worldSchema() -- the
// committed src/gameplay_sdk/include/atlantis/gameplay/generated/world.h
// (C++, the default) or src/csharp/Atlantis.Gameplay/Generated/World.g.cs
// (--lang csharp). Host-only. The output is written in binary mode, so its
// bytes are the generator's (LF line endings, Plan 0057 J2).
//
//   atlantis_sdk_codegen [--lang cpp|csharp] --out <path>

#include "generate_bindings.h"
#include "generate_csharp_bindings.h"

#include <atlantis/world/world_schema.h>

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
  std::string_view lang = "cpp";
  const char* outPath = nullptr;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--lang" && i + 1 < argc) {
      lang = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      outPath = argv[++i];
    } else {
      outPath = nullptr;
      break;
    }
  }
  if (outPath == nullptr || (lang != "cpp" && lang != "csharp")) {
    std::cerr << "usage: atlantis_sdk_codegen [--lang cpp|csharp] --out <path>\n";
    return 2;
  }
  namespace codegen = atlantis::tools::sdk_codegen;
  const auto generated = lang == "cpp" ? codegen::generateBindings(atlantis::world::worldSchema(),
                                                                   codegen::worldBindingOptions())
                                       : codegen::generateCSharpBindings(atlantis::world::worldSchema(),
                                                                         codegen::worldCSharpOptions());
  if (generated.isErr()) {
    std::cerr << "error: " << codegen::toString(generated.error().error) << " " << generated.error().name << "\n";
    return 1;
  }
  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  out << generated.value();
  if (!out) {
    std::cerr << "error: cannot write " << outPath << "\n";
    return 1;
  }
  return 0;
}
