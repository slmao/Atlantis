#include <atlantis/platform/native_window_handle.h>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using atlantis::platform::NativeWindowHandle;
using atlantis::platform::PlatformKind;

TEST_CASE("NativeWindowHandle preserves its PlatformKind tag", "[platform][native_window_handle]") {
  NativeWindowHandle handle{PlatformKind::Windows, nullptr, nullptr};
  REQUIRE(handle.kind == PlatformKind::Windows);
}

TEST_CASE("NativeWindowHandle payload fields are exactly what was set", "[platform][native_window_handle]") {
  int fakeTarget = 0;
  NativeWindowHandle handle{PlatformKind::Windows, &fakeTarget, nullptr};
  REQUIRE(handle.value0 == &fakeTarget);
  REQUIRE(handle.value1 == nullptr);
}

TEST_CASE("NativeWindowHandle is trivially copyable (borrowed, non-owning)", "[platform][native_window_handle]") {
  STATIC_REQUIRE(std::is_trivially_copyable_v<NativeWindowHandle>);

  NativeWindowHandle original{PlatformKind::Android, nullptr, nullptr};
  NativeWindowHandle copy = original;
  REQUIRE(copy.kind == original.kind);
}
