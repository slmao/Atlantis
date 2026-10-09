#include <atlantis/gameplay/error.h>

#include <utility>

namespace atlantis::gameplay {

std::string_view toString(ErrorKind kind) noexcept {
  switch (kind) {
    case ErrorKind::UnknownType: return "UnknownType";
    case ErrorKind::UnknownField: return "UnknownField";
    case ErrorKind::NotALeaf: return "NotALeaf";
    case ErrorKind::SchemaMismatch: return "SchemaMismatch";
    case ErrorKind::Refused: return "Refused";
    case ErrorKind::Connection: return "Connection";
  }
  return "(unrecognized ErrorKind)";
}

Error Error::fromText(::atlantis::connection::text::TextError error, std::string subject) {
  using ::atlantis::connection::text::TextError;
  Error out;
  out.subject = std::move(subject);
  switch (error) {
    case TextError::UnknownType: out.kind = ErrorKind::UnknownType; break;
    case TextError::NotALeaf: out.kind = ErrorKind::NotALeaf; break;
    default: out.kind = ErrorKind::UnknownField; break;  // parsePath reports only the three
  }
  return out;
}

Error Error::refused(::atlantis::world::access::AccessError error, std::string subject) {
  Error out;
  out.kind = ErrorKind::Refused;
  out.subject = std::move(subject);
  out.access = error;
  return out;
}

Error Error::mismatch(std::string subject) {
  Error out;
  out.kind = ErrorKind::SchemaMismatch;
  out.subject = std::move(subject);
  return out;
}

std::string Error::describe() const {
  std::string text = std::string(toString(kind)) + ": " + subject;
  if (access.has_value()) text += " (" + std::string(::atlantis::world::access::toString(*access)) + ")";
  if (connection.has_value()) text += " (" + std::string(::atlantis::connection::toString(*connection)) + ")";
  return text;
}

}  // namespace atlantis::gameplay
