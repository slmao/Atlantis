#pragma once

#include <atlantis/log.h>

namespace atlantis::runtime::android_detail {

// Plan 0034 Milestone 6 (real on-device finding): Atlantis Core's
// default ConsoleLogSink (src/core/src/log.cpp) writes to stdout/stderr
// via std::fprintf -- on Android, native stdout/stderr are not
// connected to logcat, so every ATLANTIS_LOG_* call was silently
// invisible, confirmed during this Milestone's first real on-device
// run (a genuine createRuntimeApplication() failure produced no visible
// diagnostic at all). Resolved via log.h's own already-designed
// extension seam ("the seam a future platform-specific sink (e.g. an
// Android logcat sink) plugs into without changing the public logging
// API," ADR-0008) -- no change to Atlantis Core itself. Installed by
// android_main.cpp as the very first statement, before any other log
// call, via atlantis::log::initialize().
class AndroidLogSink final : public atlantis::LogSink {
 public:
  void write(atlantis::LogLevel level, std::string_view message) override;
};

}  // namespace atlantis::runtime::android_detail
