#pragma once

#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace atlantis {

// Severity levels, low to high. See ADR-0008.
enum class LogLevel {
  Trace,
  Debug,
  Info,
  Warn,
  Error,
  Fatal,
};

std::string_view toString(LogLevel level);

// Replaceable sink — the seam a future platform-specific sink (e.g. an
// Android logcat sink) plugs into without changing the public logging
// API. See ADR-0008.
class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void write(LogLevel level, std::string_view message) = 0;
};

// Default sink: Warn/Error/Fatal to stderr, everything else to stdout.
class ConsoleLogSink final : public LogSink {
 public:
  void write(LogLevel level, std::string_view message) override;
};

namespace log {

// Installs the active sink. Passing nullptr restores the default
// ConsoleLogSink. Thread-safe.
void initialize(std::shared_ptr<LogSink> sink);

// Runtime-configurable minimum level; messages below it are dropped
// before formatting. Thread-safe.
void setMinLevel(LogLevel level);
LogLevel minLevel();

// Formats "<file>:<line> [<LEVEL>] <message>" and dispatches to the
// active sink, unconditionally (no level filtering here — callers that
// want filtering go through detail::logFormatted below). Thread-safe;
// synchronous — see ADR-0008 on why there is no background thread/queue.
void write(LogLevel level, const std::source_location& location, std::string_view message);

namespace detail {

template <typename... Args>
void logFormatted(LogLevel level, const std::source_location& location,
                   std::format_string<Args...> fmt, Args&&... args) {
  if (static_cast<int>(level) < static_cast<int>(minLevel())) {
    return;
  }
  log::write(level, location, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace detail

}  // namespace log

}  // namespace atlantis

#define ATLANTIS_LOG_TRACE(...)                                      \
  ::atlantis::log::detail::logFormatted(::atlantis::LogLevel::Trace, \
                                         std::source_location::current(), __VA_ARGS__)
#define ATLANTIS_LOG_DEBUG(...)                                      \
  ::atlantis::log::detail::logFormatted(::atlantis::LogLevel::Debug, \
                                         std::source_location::current(), __VA_ARGS__)
#define ATLANTIS_LOG_INFO(...)                                      \
  ::atlantis::log::detail::logFormatted(::atlantis::LogLevel::Info, \
                                         std::source_location::current(), __VA_ARGS__)
#define ATLANTIS_LOG_WARN(...)                                      \
  ::atlantis::log::detail::logFormatted(::atlantis::LogLevel::Warn, \
                                         std::source_location::current(), __VA_ARGS__)
#define ATLANTIS_LOG_ERROR(...)                                      \
  ::atlantis::log::detail::logFormatted(::atlantis::LogLevel::Error, \
                                         std::source_location::current(), __VA_ARGS__)
#define ATLANTIS_LOG_FATAL(...)                                      \
  ::atlantis::log::detail::logFormatted(::atlantis::LogLevel::Fatal, \
                                         std::source_location::current(), __VA_ARGS__)
