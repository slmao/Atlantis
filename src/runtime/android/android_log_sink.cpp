#include "android_log_sink.h"

#include <android/log.h>

namespace atlantis::runtime::android_detail {

namespace {

[[nodiscard]] int toAndroidPriority(atlantis::LogLevel level) {
  switch (level) {
    case atlantis::LogLevel::Trace:
      return ANDROID_LOG_VERBOSE;
    case atlantis::LogLevel::Debug:
      return ANDROID_LOG_DEBUG;
    case atlantis::LogLevel::Info:
      return ANDROID_LOG_INFO;
    case atlantis::LogLevel::Warn:
      return ANDROID_LOG_WARN;
    case atlantis::LogLevel::Error:
      return ANDROID_LOG_ERROR;
    case atlantis::LogLevel::Fatal:
      return ANDROID_LOG_FATAL;
  }
  return ANDROID_LOG_INFO;
}

}  // namespace

void AndroidLogSink::write(atlantis::LogLevel level, std::string_view message) {
  // __android_log_print itself requires a null-terminated string; "%.*s"
  // passes message's own bounds explicitly rather than constructing a
  // temporary std::string, matching ConsoleLogSink::write()'s own
  // "%.*s" convention (src/core/src/log.cpp) exactly.
  __android_log_print(toAndroidPriority(level), "Atlantis", "%.*s", static_cast<int>(message.size()), message.data());
}

}  // namespace atlantis::runtime::android_detail
