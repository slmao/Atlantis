#include "../../src/runtime/scene_recovery_store.h"

#include <atlantis/asset_system/guid_mint.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <thread>

namespace {
namespace fs = std::filesystem;
namespace as = atlantis::asset_system;
namespace ac = atlantis::connection::authoring;
using atlantis::runtime::SceneRecoveryStore;
using namespace std::chrono_literals;

struct LocalProfile {
  const fs::path path = fs::temp_directory_path() / ("atlantis_0122_" + as::toString(as::mintEntityGuid()));
  std::wstring previous;
  LocalProfile() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size && size < buffer.size()) previous = buffer.substr(0, size);
    fs::create_directories(path);
    REQUIRE(SetEnvironmentVariableW(L"LOCALAPPDATA", path.c_str()));
  }
  ~LocalProfile() {
    SetEnvironmentVariableW(L"LOCALAPPDATA", previous.empty() ? nullptr : previous.c_str());
    std::error_code error;
    fs::remove_all(path, error);
  }
  fs::path recoverySlot() const {
    for (const auto& entry : fs::directory_iterator(path / "Atlantis" / "SceneRecovery" / "v1"))
      if (entry.path().extension() == ".recovery") return entry.path();
    return {};
  }
};

void awaitCopy(SceneRecoveryStore& store, std::uint64_t revision) {
  for (int poll = 0; poll < 500; ++poll) {
    store.settle();
    if (store.status().protectedRevision == revision) return;
    std::this_thread::sleep_for(4ms);
  }
  FAIL("the prepared checkpoint did not publish");
}
}

TEST_CASE("scene recovery store: durable slot, competing lease and guarded cleanup", "[runtime][recovery]") {
  LocalProfile profile;
  const auto source = profile.path / "scene.scene.txt";
  std::ofstream(source, std::ios::binary) << "formal";
  const auto scene = as::mintAssetGuids(1).front();
  ac::RecoveryCopy published;
  {
    SceneRecoveryStore store(source, scene);
    store.opened();
    REQUIRE(store.status().capture == ac::RecoveryCapture::Idle);
    REQUIRE(store.readSource() == "formal");
    const auto base = store.fingerprint("formal").value();
    store.capture(2, base, "edited");
    awaitCopy(store, 2);
    CHECK(fs::exists(profile.recoverySlot()));
    store.capture(3, base, "newer");
    store.capture(4, base, "newest");
    awaitCopy(store, 4);
    REQUIRE(store.status().copy.has_value());
    published = *store.status().copy;
    CHECK_FALSE(published.pending);
    ac::RecoveryCapture other = ac::RecoveryCapture::Idle;
    std::thread competitor([&] {
      SceneRecoveryStore second(source, scene);
      second.opened();
      other = second.status().capture;
    });
    competitor.join();
    CHECK(other == ac::RecoveryCapture::Unavailable);
    store.capture(5, base, "too late");
    store.opened();
    CHECK(store.status().copy->pending);
    CHECK(store.pending(published.id).value().payload == "newest");
    CHECK(store.status().protectedRevision == std::nullopt);
  }
  SceneRecoveryStore reopened(source, scene);
  reopened.opened();
  CHECK(reopened.pending(published.id).value().payload == "newest");
  CHECK(reopened.discard(as::mintEntityGuid()) == ac::RecoveryError::StaleCopy);
  CHECK(reopened.pending(published.id).isOk());
  CHECK_FALSE(reopened.discard(published.id));
  CHECK_FALSE(reopened.status().copy);
  CHECK(profile.recoverySlot().empty());
  CHECK(reopened.readSource() == "formal");
}

TEST_CASE("scene recovery store: oversize and out-of-band slot changes preserve data", "[runtime][recovery]") {
  LocalProfile profile;
  const auto source = profile.path / "scene.scene.txt";
  std::ofstream(source, std::ios::binary) << "formal";
  SceneRecoveryStore store(source, as::mintAssetGuids(1).front());
  store.opened();
  REQUIRE(store.status().capture == ac::RecoveryCapture::Idle);
  const auto base = store.fingerprint("formal").value();
  store.capture(2, base, "retained");
  awaitCopy(store, 2);
  const auto copy = *store.status().copy;
  store.capture(3, base, std::string(ac::kRecoveryMaxSourceBytes + 1, 'x'));
  CHECK(store.status().capture == ac::RecoveryCapture::Failed);
  CHECK(store.status().protectedRevision == 2);
  store.opened();
  const auto slot = profile.recoverySlot();
  REQUIRE_FALSE(slot.empty());
  {
    std::ofstream changed(slot, std::ios::binary | std::ios::app);
    changed << "changed";
    REQUIRE(changed.good());
  }
  CHECK(store.pending(copy.id).error() == ac::RecoveryError::StaleCopy);
  CHECK(store.discard(copy.id) == ac::RecoveryError::StaleCopy);
  CHECK(fs::exists(slot));
  CHECK(store.readSource() == "formal");
}
