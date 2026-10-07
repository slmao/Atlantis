#pragma once

#include "os/socket.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::remote {

// Plan 0055 P2/P3: one JSON-lines connection over a non-blocking socket, with
// bounded buffers (ADR-0106 D1). Private to Atlantis Remote; used by both the
// server (polled once per frame, never waiting) and the client.
// Not thread-safe.
class LineChannel {
 public:
  // P3's limits: a line may be at most `maxLineBytes` (excluding '\n'); at
  // most `maxPendingWriteBytes` may wait to be written.
  LineChannel(os::Socket socket, std::size_t maxLineBytes, std::size_t maxPendingWriteBytes);

  enum class ReadStatus {
    Ok,        // read what was available; complete lines appended
    Closed,    // the peer closed or the socket failed
    Overflow,  // a line exceeded the limit
  };
  // Reads without waiting and appends every complete line (without its '\n';
  // a trailing '\r' is dropped) to `lines`. Reads at most a bounded amount
  // per call, so one flooding peer cannot hold the caller indefinitely.
  [[nodiscard]] ReadStatus read(std::vector<std::string>& lines);

  // Queues `line` plus '\n'. False if that would exceed the write limit
  // (nothing is queued then).
  [[nodiscard]] bool queue(std::string_view line);
  // Writes queued bytes until done or the socket would block. False if the
  // peer closed or the socket failed.
  [[nodiscard]] bool flush();
  [[nodiscard]] bool hasPendingWrite() const noexcept { return written_ < pending_.size(); }

  [[nodiscard]] const os::Socket& socket() const noexcept { return socket_; }

 private:
  os::Socket socket_;
  std::size_t maxLineBytes_;
  std::size_t maxPendingWriteBytes_;
  std::string partial_;  // bytes after the last complete line
  std::string pending_;  // queued output
  std::size_t written_ = 0;
};

}  // namespace atlantis::remote
