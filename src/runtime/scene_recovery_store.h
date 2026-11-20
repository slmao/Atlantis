#pragma once

#include <atlantis/authoring/authoring_session.h>

namespace atlantis::runtime {

class SceneRecoveryStore final : public authoring::RecoveryStore {
 public:
  SceneRecoveryStore(std::filesystem::path source, asset_system::AssetGuid scene);
  ~SceneRecoveryStore() override;

  void opened() override;
  [[nodiscard]] std::optional<std::string> fingerprint(std::string_view bytes) const override;
  [[nodiscard]] std::optional<std::string> readSource() const override;
  [[nodiscard]] connection::authoring::RecoveryStatus status() const override;
  void capture(std::uint64_t revision, std::string baseDigest, std::string payload) override;
  void retire() override;
  [[nodiscard]] Result<authoring::RecoverySnapshot, connection::authoring::RecoveryError>
  pending(const asset_system::EntityGuid& id) const override;
  void adopted(std::uint64_t revision) override;
  [[nodiscard]] std::optional<connection::authoring::RecoveryError>
  discard(const asset_system::EntityGuid& id) override;
  void settle();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace atlantis::runtime
