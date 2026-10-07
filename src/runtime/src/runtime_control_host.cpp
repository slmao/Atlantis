#include <atlantis/runtime/runtime_control_host.h>

#include <atlantis/runtime/runtime_application.h>

#include <utility>

namespace atlantis::runtime {

namespace connection = atlantis::connection;

DiagnosticsRing::DiagnosticsRing(std::size_t capacity, std::shared_ptr<atlantis::LogSink> forwardTo)
    : forwardTo_(std::move(forwardTo)), capacity_(capacity == 0 ? 1 : capacity) {}

void DiagnosticsRing::write(atlantis::LogLevel level, std::string_view message) {
  if (forwardTo_) forwardTo_->write(level, message);
  if (static_cast<int>(level) < static_cast<int>(atlantis::LogLevel::Warn)) return;
  const connection::DiagnosticSeverity severity = level == atlantis::LogLevel::Warn ? connection::DiagnosticSeverity::Warning
                                                  : level == atlantis::LogLevel::Error
                                                      ? connection::DiagnosticSeverity::Error
                                                      : connection::DiagnosticSeverity::Fatal;
  const std::lock_guard<std::mutex> lock(mutex_);
  records_.push_back(connection::Diagnostic{++latest_, severity, std::string(message)});
  if (records_.size() > capacity_) {
    records_.pop_front();
    ++dropped_;
  }
}

connection::DiagnosticBatch DiagnosticsRing::read(std::uint64_t afterSequence, std::size_t max) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  connection::DiagnosticBatch batch;
  batch.latest = latest_;
  batch.dropped = dropped_;
  for (const connection::Diagnostic& record : records_) {
    if (batch.entries.size() >= max) break;
    if (record.sequence > afterSequence) batch.entries.push_back(record);
  }
  return batch;
}

RuntimeControlHost::Target RuntimeControlHost::forApplication(RuntimeApplication& application) {
  Target target;
  target.setCommandsHeld = [&application](bool held) { application.setCommandsHeld(held); };
  target.frameData = [&application] { return application.captureFrameData(); };
  target.captureImage = [](const std::string&) {
    return atlantis::Result<connection::CapturedImage, connection::ControlError>::Err(
        connection::ControlError::CaptureFailed);
  };
  target.scene = application.sceneGuid();
  return target;
}

RuntimeControlHost::RuntimeControlHost(Target target, Options options) : target_(std::move(target)) {
  if (options.recordDiagnostics) {
    ring_ = std::make_shared<DiagnosticsRing>(options.diagnosticCapacity);
    atlantis::log::initialize(ring_);
  }
}

RuntimeControlHost::~RuntimeControlHost() {
  stop();
  if (ring_) atlantis::log::initialize(nullptr);  // the default console sink again
}

void RuntimeControlHost::beforeFrame() {
  if (!steps_.empty() && !stepStarted_) {
    stepStarted_ = true;
    steps_.front().remaining = steps_.front().request.frames;
  }
  const bool stepping = stepStarted_ && steps_.front().remaining > 0;
  target_.setCommandsHeld(paused_ && !stepping);
}

void RuntimeControlHost::afterFrame() {
  ++frame_;
  if (!stepStarted_) return;
  PendingStep& current = steps_.front();
  if (--current.remaining > 0) return;
  PendingStep finished = std::move(current);
  steps_.pop_front();
  stepStarted_ = false;

  using ResultT = atlantis::Result<connection::FrameReport, connection::ControlError>;
  auto data = target_.frameData();
  if (data.isErr()) {
    finished.done(ResultT::Err(data.error()));
    return;
  }
  connection::FrameReport report;
  report.frame = frame_;
  report.applied = true;
  report.data = std::move(data.value());
  if (finished.request.imagePath.has_value()) {
    auto image = target_.captureImage(*finished.request.imagePath);
    if (image.isErr()) {
      finished.done(ResultT::Err(image.error()));
      return;
    }
    report.image = std::move(image.value());
  }
  finished.done(ResultT::Ok(std::move(report)));
}

void RuntimeControlHost::stop() {
  using ResultT = atlantis::Result<connection::FrameReport, connection::ControlError>;
  std::deque<PendingStep> waiting = std::move(steps_);
  steps_.clear();
  stepStarted_ = false;
  for (PendingStep& pending : waiting) pending.done(ResultT::Err(connection::ControlError::Stopped));
}

connection::RuntimeStatus RuntimeControlHost::status() {
  return connection::RuntimeStatus{paused_, frame_, target_.scene};
}

void RuntimeControlHost::pause() { paused_ = true; }

void RuntimeControlHost::resume() { paused_ = false; }

void RuntimeControlHost::step(
    connection::StepRequest request,
    std::function<void(atlantis::Result<connection::FrameReport, connection::ControlError>)> done) {
  if (request.frames == 0) {
    done(atlantis::Result<connection::FrameReport, connection::ControlError>::Err(
        connection::ControlError::InvalidRequest));
    return;
  }
  steps_.push_back(PendingStep{std::move(request), std::move(done), 0});
}

connection::DiagnosticBatch RuntimeControlHost::diagnostics(std::uint64_t afterSequence, std::size_t max) {
  if (!ring_) return {};
  return ring_->read(afterSequence, max);
}

}  // namespace atlantis::runtime
