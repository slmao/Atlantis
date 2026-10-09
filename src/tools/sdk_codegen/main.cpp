// atlantis_sdk_codegen (Plan 0057 P3, ADR-0111 D1): writes World's typed
// bindings -- the committed
// src/gameplay_sdk/include/atlantis/gameplay/generated/world.h -- from
// world::worldSchema(). Host-only. The output is written in binary mode, so
// its bytes are the generator's (LF line endings, Plan 0057 J2).
//
//   atlantis_sdk_codegen --out <path>

#include "generate_bindings.h"

#include <atlantis/world/world_schema.h>

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
  if (argc != 3 || std::string_view(argv[1]) != "--out") {
    std::cerr << "usage: atlantis_sdk_codegen --out <path>\n";
    return 2;
  }
  namespace codegen = atlantis::tools::sdk_codegen;
  const auto generated = codegen::generateBindings(atlantis::world::worldSchema(), codegen::worldBindingOptions());
  if (generated.isErr()) {
    std::cerr << "error: " << codegen::toString(generated.error().error) << " " << generated.error().name << "\n";
    return 1;
  }
  std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
  out << generated.value();
  if (!out) {
    std::cerr << "error: cannot write " << argv[2] << "\n";
    return 1;
  }
  return 0;
}
