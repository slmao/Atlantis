#include <atlantis/log.h>

#include <cstdio>
#include <mutex>

namespace atlantis {

std::string_view toString(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Fatal:
      return "FATAL";
  }
  return "UNKNOWN";
}

void ConsoleLogSink::write(LogLevel level, std::string_view message) {
  std::FILE* stream = (static_cast<int>(level) >= static_cast<int>(LogLevel::Warn)) ? stderr : stdout;
  std::fprintf(stream, "%.*s\n", static_cast<int>(message.size()), message.data());
  std::fflush(stream);
}

namespace log {

namespace {

std::mutex& stateMutex() {
  static std::mutex mutex;
  return mutex;
}

std::shared_ptr<LogSink>& activeSink() {
  static std::shared_ptr<LogSink> sink = std::make_shared<ConsoleLogSink>();
  return sink;
}

LogLevel& minLevelState() {
#if defined(NDEBUG)
  static LogLevel level = LogLevel::Info;
#else
  static LogLevel level = LogLevel::Trace;
#endif
  return level;
}

}  // namespace

void initialize(std::shared_ptr<LogSink> sink) {
  std::lock_guard<std::mutex> lock(stateMutex());
  activeSink() = sink ? std::move(sink) : std::make_shared<ConsoleLogSink>();
}

void setMinLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(stateMutex());
  minLevelState() = level;
}

LogLevel minLevel() {
  std::lock_guard<std::mutex> lock(stateMutex());
  return minLevelState();
}

void write(LogLevel level, const std::source_location& location, std::string_view message) {
  std::shared_ptr<LogSink> sinkCopy;
  {
    std::lock_guard<std::mutex> lock(stateMutex());
    sinkCopy = activeSink();
  }

  const std::string formatted =
      std::format("{}:{} [{}] {}", location.file_name(), location.line(), toString(level), message);
  sinkCopy->write(level, formatted);
}

}  // namespace log

}  // namespace atlantis
