#include "line_channel.h"

#include <array>
#include <utility>

namespace atlantis::remote {

namespace {

constexpr std::size_t kChunkBytes = 64 * 1024;
// Per read() call: enough for any one line plus a full pipeline of small
// requests, small enough that a frame is never held for long.
constexpr std::size_t kMaxBytesPerRead = 4 * 1024 * 1024;

}  // namespace

LineChannel::LineChannel(os::Socket socket, std::size_t maxLineBytes, std::size_t maxPendingWriteBytes)
    : socket_(std::move(socket)), maxLineBytes_(maxLineBytes), maxPendingWriteBytes_(maxPendingWriteBytes) {}

LineChannel::ReadStatus LineChannel::read(std::vector<std::string>& lines) {
  std::array<char, kChunkBytes> chunk{};
  std::size_t total = 0;
  while (total < kMaxBytesPerRead) {
    const os::IoResult result = os::receive(socket_, chunk);
    if (result.status == os::IoResult::Status::WouldBlock) break;
    if (result.status == os::IoResult::Status::Closed) return ReadStatus::Closed;
    total += result.bytes;
    std::size_t start = 0;
    for (std::size_t i = 0; i < result.bytes; ++i) {
      if (chunk[i] != '\n') continue;
      partial_.append(chunk.data() + start, i - start);
      if (partial_.size() > maxLineBytes_) return ReadStatus::Overflow;
      if (!partial_.empty() && partial_.back() == '\r') partial_.pop_back();
      lines.push_back(std::exchange(partial_, {}));
      start = i + 1;
    }
    partial_.append(chunk.data() + start, result.bytes - start);
    if (partial_.size() > maxLineBytes_) return ReadStatus::Overflow;
  }
  return ReadStatus::Ok;
}

bool LineChannel::queue(std::string_view line) {
  if (written_ > 0 && written_ == pending_.size()) {
    pending_.clear();
    written_ = 0;
  }
  if (pending_.size() - written_ + line.size() + 1 > maxPendingWriteBytes_) return false;
  if (written_ > pending_.size() / 2) {  // reclaim the written prefix
    pending_.erase(0, written_);
    written_ = 0;
  }
  pending_.append(line);
  pending_ += '\n';
  return true;
}

bool LineChannel::flush() {
  while (written_ < pending_.size()) {
    const os::IoResult result =
        os::send(socket_, std::span<const char>(pending_.data() + written_, pending_.size() - written_));
    if (result.status == os::IoResult::Status::WouldBlock) return true;
    if (result.status == os::IoResult::Status::Closed) return false;
    written_ += result.bytes;
  }
  pending_.clear();
  written_ = 0;
  return true;
}

}  // namespace atlantis::remote
