#pragma once

#include <atlantis/connection/asset_connection.h>

#include <memory>
#include <string>

namespace atlantis::runtime {
// Spec 0081 R3: executable-private, frame-thread-only host. Construction and
// connect do no I/O. The first query fixes Ready/Unavailable until destruction.
// Connections borrow this host; destroy them first. No RuntimeApplication seam.
class AssetHost {
 public:
  explicit AssetHost(std::string catalogPath);
  ~AssetHost();
  AssetHost(const AssetHost&) = delete;
  AssetHost& operator=(const AssetHost&) = delete;
  [[nodiscard]] std::unique_ptr<connection::AssetConnection> connect();
 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace atlantis::runtime
