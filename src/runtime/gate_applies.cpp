#include "gate_applies.h"

#include <fstream>
#include <iterator>

namespace atlantis::runtime::gate {

using ApplicabilityResult = atlantis::Result<Applicability, std::string>;

namespace {

[[nodiscard]] std::string trim(std::string_view text) {
  const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  return std::string(text);
}

[[nodiscard]] bool anyMatch(const std::vector<std::string>& patterns, std::string_view path) {
  for (const std::string& pattern : patterns) {
    if (matchesGlob(pattern, path)) return true;
  }
  return false;
}

}  // namespace

bool matchesGlob(std::string_view pattern, std::string_view path) {
  if (pattern.empty()) return path.empty();
  if (pattern.starts_with("**")) {
    const std::string_view rest = pattern.substr(2);
    if (rest.starts_with('/')) {
      // `**/`: zero directories, or consume one directory and try again.
      if (matchesGlob(rest.substr(1), path)) return true;
      for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/' && matchesGlob(pattern, path.substr(i + 1))) return true;
      }
      return false;
    }
    for (std::size_t i = 0; i <= path.size(); ++i) {
      if (matchesGlob(rest, path.substr(i))) return true;
    }
    return false;
  }
  if (pattern.front() == '*') {
    const std::string_view rest = pattern.substr(1);
    for (std::size_t i = 0;; ++i) {
      if (matchesGlob(rest, path.substr(i))) return true;
      if (i >= path.size() || path[i] == '/') return false;
    }
  }
  if (pattern.front() == '?') {
    if (path.empty() || path.front() == '/') return false;
    return matchesGlob(pattern.substr(1), path.substr(1));
  }
  if (path.empty() || path.front() != pattern.front()) return false;
  return matchesGlob(pattern.substr(1), path.substr(1));
}

std::string normalizePath(std::string_view path) {
  std::string out(path);
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
  while (out.rfind("./", 0) == 0) out.erase(0, 2);
  return out;
}

ApplicabilityResult parseApplicability(std::string_view text) {
  Applicability out;
  enum class Section { None, Include, Exclude } section = Section::None;
  std::size_t lineNumber = 0;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string line = trim(text.substr(start, end - start));
    start = end + 1;
    ++lineNumber;
    if (const auto hash = line.find('#'); hash != std::string::npos) line = trim(std::string_view(line).substr(0, hash));
    if (line.empty()) continue;
    if (line == "include:") {
      section = Section::Include;
    } else if (line == "exclude:") {
      section = Section::Exclude;
    } else if (line.back() == ':') {
      return ApplicabilityResult::Err("line " + std::to_string(lineNumber) + ": unknown section '" + line + "'");
    } else if (section == Section::None) {
      return ApplicabilityResult::Err("line " + std::to_string(lineNumber) +
                                      ": a pattern before the first 'include:' or 'exclude:'");
    } else {
      (section == Section::Include ? out.include : out.exclude).push_back(normalizePath(line));
    }
  }
  if (out.include.empty()) return ApplicabilityResult::Err("the file has no include patterns");
  return ApplicabilityResult::Ok(std::move(out));
}

ApplicabilityResult loadApplicability(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return ApplicabilityResult::Err("cannot read the applicability file " + path);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto parsed = parseApplicability(text);
  if (parsed.isErr()) return ApplicabilityResult::Err(path + ": " + parsed.error());
  return parsed;
}

AppliesResult evaluateApplicability(const Applicability& applicability, std::span<const std::string> changedPaths) {
  AppliesResult out;
  for (const std::string& raw : changedPaths) {
    const std::string path = normalizePath(raw);
    if (path.empty()) continue;
    // An include wins over an exclude: the exclude list says what is exempt on purpose, and a path
    // that matches both (tests/performance/** against tests/**) is the narrower rule's.
    if (anyMatch(applicability.include, path)) out.matched.push_back(path);
  }
  out.applies = !out.matched.empty();
  return out;
}

Value toValue(const AppliesResult& result) {
  Value out = Value::object();
  out.set("applies", Value::boolean(result.applies));
  Value matched = Value::array();
  for (const std::string& path : result.matched) matched.push(Value::string(path));
  out.set("matched", std::move(matched));
  return out;
}

}  // namespace atlantis::runtime::gate
