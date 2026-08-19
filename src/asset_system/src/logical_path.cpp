#include <atlantis/asset_system/logical_path.h>

#include <vector>

namespace atlantis::asset_system {

namespace {

[[nodiscard]] bool isAllowedByte(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
         c == '.' || c == '/' || c == '\\';
}

[[nodiscard]] bool isAsciiLetter(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

[[nodiscard]] bool startsWithDriveLetterPrefix(std::string_view s) {
  return s.size() >= 2 && isAsciiLetter(s[0]) && s[1] == ':';
}

}  // namespace

atlantis::Result<std::string, LogicalPathError> normalizeLogicalPath(std::string_view input) {
  using ResultT = atlantis::Result<std::string, LogicalPathError>;

  if (input.empty()) return ResultT::Err(LogicalPathError::EmptyPath);

  // Checked before the general allowed-character sweep below: ':' is not
  // itself an allowed byte, so a drive-letter prefix must be recognized
  // here first, or DriveLetterRejected could never be reached (it would
  // always be preempted by DisallowedCharacter) -- a mechanical ordering
  // fix over Plan 0012's own D9 prose, disclosed in the Implementation
  // PR, not a change to the decision that drive-letter prefixes are
  // rejected with their own distinct error.
  if (startsWithDriveLetterPrefix(input)) return ResultT::Err(LogicalPathError::DriveLetterRejected);

  for (char c : input) {
    if (!isAllowedByte(c)) return ResultT::Err(LogicalPathError::DisallowedCharacter);
  }

  std::string normalized(input);
  for (char& c : normalized) {
    if (c == '\\') c = '/';
  }

  if (normalized.front() == '/') return ResultT::Err(LogicalPathError::AbsolutePathRejected);

  std::vector<std::string> stack;
  std::size_t segmentStart = 0;
  for (std::size_t i = 0; i <= normalized.size(); ++i) {
    if (i == normalized.size() || normalized[i] == '/') {
      const std::string_view segment(normalized.data() + segmentStart, i - segmentStart);
      segmentStart = i + 1;
      if (segment.empty() || segment == ".") {
        // Skip: empty segments (from "//" or a trailing "/") and "."
        // contribute nothing to the normalized path.
      } else if (segment == "..") {
        if (stack.empty()) return ResultT::Err(LogicalPathError::EscapesAssetRoot);
        stack.pop_back();
      } else {
        stack.emplace_back(segment);
      }
    }
  }

  if (stack.empty()) return ResultT::Err(LogicalPathError::EmptyPath);

  std::string joined;
  for (std::size_t i = 0; i < stack.size(); ++i) {
    if (i != 0) joined += '/';
    joined += stack[i];
  }
  return ResultT::Ok(std::move(joined));
}

}  // namespace atlantis::asset_system
