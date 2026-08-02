#include <atlantis/assert.h>
#include <atlantis/log.h>

#include <cstdlib>
#include <format>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace atlantis::assertions {

namespace {

void defaultFailureHandler(const AssertFailureInfo& info) {
  const std::string formatted = info.message.empty()
                                     ? std::format("Assertion failed: {}", info.expression)
                                     : std::format("Assertion failed: {} ({})", info.expression, info.message);

  log::write(LogLevel::Fatal, info.location, formatted);

#if defined(_WIN32)
  if (IsDebuggerPresent()) {
    __debugbreak();
  }
#endif

  std::abort();
}

std::mutex& handlerMutex() {
  static std::mutex mutex;
  return mutex;
}

AssertFailureHandler& currentHandler() {
  static AssertFailureHandler handler = &defaultFailureHandler;
  return handler;
}

}  // namespace

AssertFailureHandler setFailureHandler(AssertFailureHandler handler) {
  std::lock_guard<std::mutex> lock(handlerMutex());
  AssertFailureHandler previous = std::move(currentHandler());
  currentHandler() = handler ? std::move(handler) : AssertFailureHandler(&defaultFailureHandler);
  return previous;
}

void reportFailure(const AssertFailureInfo& info) {
  AssertFailureHandler handlerCopy;
  {
    std::lock_guard<std::mutex> lock(handlerMutex());
    handlerCopy = currentHandler();
  }
  handlerCopy(info);
}

}  // namespace atlantis::assertions
