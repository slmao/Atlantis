#pragma once
#include <atlantis/connection/authoring_connection.h>
namespace atlantis::gameplay {
// Borrows a caller-thread connection, which must outlive this facade. Results own
// values. A Remote caller checks session failure before interpreting a sentinel.
class AuthoringApply {
 public:
  explicit AuthoringApply(connection::AuthoringConnection& connection) : connection_(connection) {}
  [[nodiscard]] Result<connection::authoring::ApplyReceipt, connection::authoring::ApplyError> applyTracked() {
    return connection_.applyTracked();
  }
  [[nodiscard]] Result<connection::authoring::ApplySnapshot, connection::authoring::ApplyLookupError> applyStatus(
      std::optional<connection::authoring::ApplyId> id = std::nullopt) const {
    return connection_.applyStatus(id);
  }
 private:
  connection::AuthoringConnection& connection_;
};
}  // namespace atlantis::gameplay
