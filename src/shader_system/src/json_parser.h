#pragma once

#include <string>

#include <atlantis/result.h>

#include "json_value.h"

namespace atlantis::shader_system::detail {

// Every reason parseJson() below can fail. Deliberately one flat enum,
// not per-limit variants -- every caller of this private parser only
// ever maps the whole thing down to its own public MalformedJson/
// UnexpectedStructure-shaped error anyway (Plan 0008 Section 3); no
// caller branches on which specific JsonParseError occurred.
enum class JsonParseError {
  UnexpectedToken,
  UnterminatedString,
  InvalidEscape,
  InvalidUnicodeEscape,
  InvalidNumber,
  TrailingGarbage,
  InputTooLarge,     // Section 3: > 16 MiB
  NestingTooDeep,    // Section 3: > 64 levels
  StringTooLong,     // Section 3: > 64 KiB
  TooManyElements,   // Section 3: > 4096 array/object elements
  EmptyInput,
};

// Strict, hand-rolled JSON parser -- see json_value.h and Plan 0008
// Section 3 for scope and rationale. Parses the full JSON value grammar
// (object/array/string/number/bool/null); rejects trailing content
// after the top-level value; treats a duplicate object key as "last one
// wins" (matching the JSON specification's own permissive stance, not
// specially rejected). Not thread-safe in the sense of shared state --
// it has none; concurrent calls with distinct `input` arguments are
// independently safe, but this is incidental, not a documented
// guarantee this module relies on anywhere (every real call site is
// single-threaded, ADR-0004).
[[nodiscard]] atlantis::Result<JsonValue, JsonParseError> parseJson(const std::string& input);

}  // namespace atlantis::shader_system::detail
