#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/log.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace atlantis::runtime {

class RuntimeApplication;

// Plan 0055 P6 (Spec 0055 R3, R6): a LogSink that forwards every record
// unchanged (to the console sink unless told otherwise) and keeps the newest
// Warning-and-above records in a bounded ring, each with a sequence number
// from 1, counting what it drops.
// write() may be called from any thread (Core's logging is thread-safe), so
// the ring is guarded; read() is the control's, on the frame thread.
class DiagnosticsRing final : public atlantis::LogSink {
 public:
  explicit DiagnosticsRing(std::size_t capacity,
                           std::shared_ptr<atlantis::LogSink> forwardTo = std::make_shared<atlantis::ConsoleLogSink>());

  void write(atlantis::LogLevel level, std::string_view message) override;

  [[nodiscard]] atlantis::connection::DiagnosticBatch read(std::uint64_t afterSequence, std::size_t max) const;

 private:
  std::shared_ptr<atlantis::LogSink> forwardTo_;  // null: forwards nothing
  std::size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<atlantis::connection::Diagnostic> records_;
  std::uint64_t latest_ = 0;
  std::uint64_t dropped_ = 0;
};

// Plan 0055 P5 (Spec 0055 R3; ADR-0106 D3, D4): Runtime's implementation of
// RuntimeControl. The host loop calls beforeFrame() and afterFrame() around
// each runFrame() -- only when attachable clients are served (`--listen`,
// P11) -- and the control decides from its pause and step state whether the
// frame applies commands, counts frames, completes steps, and captures.
//
// Steps are served one at a time in request order: a step's frames start at
// the next beforeFrame() after the previous step completed.
//
// Ownership: borrows its Target's application for its whole life. While it
// lives with recordDiagnostics set, its DiagnosticsRing is Core's active log
// sink; the destructor restores the default sink. Not thread-safe: the frame
// thread only (ADR-0004).
class RuntimeControlHost final : public atlantis::connection::RuntimeControl {
 public:
  // What the control drives. forApplication() wires a RuntimeApplication;
  // GPU-independent tests supply their own.
  struct Target {
    std::function<void(bool held)> setCommandsHeld;
    std::function<atlantis::Result<atlantis::connection::FrameData, atlantis::connection::ControlError>()> frameData;
    std::function<atlantis::Result<atlantis::connection::CapturedImage, atlantis::connection::ControlError>(
        const std::string& path)>
        captureImage;
    atlantis::asset_system::AssetGuid scene;
  };
  [[nodiscard]] static Target forApplication(RuntimeApplication& application);

  struct Options {
    bool recordDiagnostics = true;  // install the DiagnosticsRing as the log sink
    std::size_t diagnosticCapacity = 4096;
  };

  RuntimeControlHost(Target target, Options options);
  ~RuntimeControlHost() override;
  RuntimeControlHost(const RuntimeControlHost&) = delete;
  RuntimeControlHost& operator=(const RuntimeControlHost&) = delete;

  // Before runFrame(): whether this frame applies pending commands.
  void beforeFrame();
  // After runFrame(): counts the frame and, if it was a step's last,
  // completes the step (frame data, and the image if one was asked for).
  void afterFrame();
  // When the Runtime stops: every step still waiting completes with Stopped.
  void stop();

  atlantis::connection::RuntimeStatus status() override;
  void pause() override;
  void resume() override;
  void step(atlantis::connection::StepRequest request,
            std::function<void(atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>)>
                done) override;
  atlantis::connection::DiagnosticBatch diagnostics(std::uint64_t afterSequence, std::size_t max) override;

 private:
  struct PendingStep {
    atlantis::connection::StepRequest request;
    std::function<void(atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>)> done;
    std::uint32_t remaining = 0;
  };

  Target target_;
  std::shared_ptr<DiagnosticsRing> ring_;  // null unless recording
  bool paused_ = false;
  std::uint64_t frame_ = 0;
  std::deque<PendingStep> steps_;  // front: the step in progress (once started)
  bool stepStarted_ = false;
};

}  // namespace atlantis::runtime
