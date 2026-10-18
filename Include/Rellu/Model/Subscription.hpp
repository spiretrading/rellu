#ifndef RELLU_MODEL_SUBSCRIPTION_HPP
#define RELLU_MODEL_SUBSCRIPTION_HPP
#include <functional>

namespace Rellu {

  /** A handle that ends an observer's subscription when released. */
  class Subscription {
    public:

      /** Constructs a Subscription that observes nothing. */
      Subscription() noexcept;

      /**
       * Constructs a Subscription.
       * @param release The function ending the subscription.
       */
      explicit Subscription(std::function<void ()> release) noexcept;

      Subscription(Subscription&& subscription) noexcept;

      /** Releases the subscription. */
      ~Subscription();

      /** Ends the subscription, doing nothing if it already ended. */
      void release();

      Subscription& operator =(Subscription&& subscription) noexcept;

    private:
      std::function<void ()> m_release;

      Subscription(const Subscription&) = delete;
      Subscription& operator =(const Subscription&) = delete;
  };
}

#endif
