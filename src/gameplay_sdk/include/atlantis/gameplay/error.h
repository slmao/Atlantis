#pragma once

#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/text.h>
#include <atlantis/world/access/access_error.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Spec 0057 R2/R4/R5, Plan 0057 P2: why an SDK call was refused. Name errors
// are the client's own (resolved through connection::text, J5); a World
// refusal of a query is reported as Refused with its AccessError; a command's
// refusal is not an Error at all -- it comes back later as a CommandFailure
// by ticket, exactly as RuntimeConnection reports it (R4: the SDK adds no
// rule the boundary owns). Plain values; no state.
namespace atlantis::gameplay {

enum class ErrorKind : std::uint8_t {
  UnknownType,     // no schema type has that short or qualified name
  UnknownField,    // a path segment names no field of the type it is in
  NotALeaf,        // the path stops at a struct, or names only a type
  SchemaMismatch,  // a generated binding does not match the connection's schema (R5)
  Refused,         // the World refused a query (access holds why)
  Connection,      // a connection-level refusal (connection holds why)
};

[[nodiscard]] std::string_view toString(ErrorKind kind) noexcept;

struct Error {
  ErrorKind kind = ErrorKind::UnknownType;
  std::string subject;  // the name, path or type the error is about
  std::optional<::atlantis::world::access::AccessError> access;
  std::optional<::atlantis::connection::ConnectionError> connection;

  [[nodiscard]] static Error fromText(::atlantis::connection::text::TextError error, std::string subject);
  [[nodiscard]] static Error refused(::atlantis::world::access::AccessError error, std::string subject);
  [[nodiscard]] static Error mismatch(std::string subject);

  // "<kind>: <subject>[ (<AccessError|ConnectionError>)]".
  [[nodiscard]] std::string describe() const;
  friend bool operator==(const Error&, const Error&) = default;
};

}  // namespace atlantis::gameplay
