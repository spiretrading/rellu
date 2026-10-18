#include <utility>
#include "Rellu/Model/Subscription.hpp"

using namespace Rellu;

Subscription::Subscription() noexcept = default;

Subscription::Subscription(std::function<void ()> release) noexcept
  : m_release(std::move(release)) {}

Subscription::Subscription(Subscription&& subscription) noexcept
  : m_release(std::exchange(subscription.m_release, nullptr)) {}

Subscription::~Subscription() {
  release();
}

void Subscription::release() {
  if(auto release = std::exchange(m_release, nullptr)) {
    release();
  }
}

Subscription& Subscription::operator =(Subscription&& subscription) noexcept {
  if(this != &subscription) {
    release();
    m_release = std::exchange(subscription.m_release, nullptr);
  }
  return *this;
}
