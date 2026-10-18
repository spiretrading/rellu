#ifndef RELLU_COMMANDS_SIGNAL_HPP
#define RELLU_COMMANDS_SIGNAL_HPP
#include <functional>
#include <memory>
#include <vector>
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /** Notifies observers of a change, in the order they subscribed. */
  class Signal {
    public:

      /** Constructs a Signal without observers. */
      Signal();

      /**
       * Subscribes an observer to every notification from now on.
       * @param observer The function called with each notification.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

      /** Calls every observer subscribed when the notification starts. */
      void operator ()() const;

    private:
      struct Entry {
        std::function<void ()> m_observer;
        bool m_is_released;
      };
      std::shared_ptr<std::vector<std::shared_ptr<Entry>>> m_entries;

      Signal(const Signal&) = delete;
      Signal& operator =(const Signal&) = delete;
  };
}

#endif
