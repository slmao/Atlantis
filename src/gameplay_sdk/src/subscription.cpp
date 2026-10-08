#include <atlantis/gameplay/subscription.h>

#include <string>
#include <utility>

namespace atlantis::gameplay {

Subscription::~Subscription() { release(); }

Subscription::Subscription(Subscription&& other) noexcept
    : connection_(std::exchange(other.connection_, nullptr)), id_(other.id_) {}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
  if (this != &other) {
    release();
    connection_ = std::exchange(other.connection_, nullptr);
    id_ = other.id_;
  }
  return *this;
}

void Subscription::release() noexcept {
  if (connection_ != nullptr) (void)connection_->unsubscribe(id_);
  connection_ = nullptr;
}

atlantis::Result<std::vector<::atlantis::world::access::Event>, Error> Subscription::drain() {
  using ResultT = atlantis::Result<std::vector<::atlantis::world::access::Event>, Error>;
  if (connection_ == nullptr) {
    Error error;
    error.kind = ErrorKind::Connection;
    error.subject = "a moved-from subscription";
    error.connection = ::atlantis::connection::ConnectionError::UnknownSubscription;
    return ResultT::Err(std::move(error));
  }
  auto drained = connection_->drainEvents(id_);
  if (drained.isErr()) {
    Error error;
    error.kind = ErrorKind::Connection;
    error.subject = "subscription " + std::to_string(id_.value);
    error.connection = drained.error();
    return ResultT::Err(std::move(error));
  }
  return ResultT::Ok(std::move(drained.value()));
}

}  // namespace atlantis::gameplay
