// Spec 0059 R2 / ADR-0114 D2: the compiled-out expansion, proven in EVERY build
// configuration by undefining the options before the header is first seen in
// this translation unit (the rest of the test executable keeps the build's own).
#undef ATLANTIS_PROFILING
#undef ATLANTIS_PROFILING_TRACY
#undef ATLANTIS_GPU_MARKERS

#include <atlantis/profile.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#define ATL_TEST_STR_IMPL(x) #x
#define ATL_TEST_STR(x) ATL_TEST_STR_IMPL(x)

namespace {

struct CountingCommandList {
  int begins = 0;
  void beginRegion(std::string_view) { ++begins; }
  void endRegion() {}
};

}  // namespace

TEST_CASE("every ATL_PROFILE_* macro expands to ((void)0) when profiling is off", "[profile][compiled_out]") {
  CHECK(std::string_view(ATL_TEST_STR(ATL_PROFILE_FRAME())) == "((void)0)");
  CHECK(std::string_view(ATL_TEST_STR(ATL_PROFILE_SCOPE("x"))) == "((void)0)");
  CHECK(std::string_view(ATL_TEST_STR(ATL_PROFILE_SCOPE_DYNAMIC(name))) == "((void)0)");
  CHECK(std::string_view(ATL_TEST_STR(ATL_PROFILE_SCOPE_NAMED(z, "x"))) == "((void)0)");
  CHECK(std::string_view(ATL_TEST_STR(ATL_PROFILE_SCOPE_END(z))) == "((void)0)");
  CHECK(std::string_view(ATL_TEST_STR(ATL_PROFILE_GPU_SCOPE(cmd, "x"))) == "((void)0)");
  CHECK(std::string_view(ATL_TEST_STR(ATL_PROFILE_VALUE("x", 1))) == "((void)0)");
  CHECK(std::string_view(ATL_TEST_STR(ATL_PROFILE_ADD("x", 1))) == "((void)0)");
}

TEST_CASE("disabled macros do not evaluate their arguments", "[profile][compiled_out]") {
  int evaluations = 0;
  CountingCommandList cmd;
  ATL_PROFILE_FRAME();
  ATL_PROFILE_SCOPE_DYNAMIC((++evaluations, std::string_view("x")));
  ATL_PROFILE_VALUE("x", ++evaluations);
  ATL_PROFILE_ADD("x", ++evaluations);
  ATL_PROFILE_GPU_SCOPE((++evaluations, cmd), "x");
  ATL_PROFILE_SCOPE_NAMED(zone, (++evaluations, "x"));
  ATL_PROFILE_SCOPE_END(zone);
  CHECK(evaluations == 0);
  CHECK(cmd.begins == 0);
}

TEST_CASE("ATL_DEBUG_NAME is an empty view and does not evaluate its argument without GPU markers",
          "[profile][compiled_out]") {
  int evaluations = 0;
  const std::string_view name = ATL_DEBUG_NAME((++evaluations, "built name"));
  CHECK(name.empty());
  CHECK(evaluations == 0);
}
