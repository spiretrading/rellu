#include <algorithm>
#include "Rellu/Commands/Signal.hpp"

using namespace Rellu;

Signal::Signal()
  : m_entries(std::make_shared<std::vector<std::shared_ptr<Entry>>>()) {}

Subscription Signal::subscribe(std::function<void ()> observer) {
  auto entry = std::make_shared<Entry>(std::move(observer), false);
  m_entries->push_back(entry);
  return Subscription([entries = std::weak_ptr(m_entries),
      entry = std::weak_ptr(entry)] {
    auto locked_entry = entry.lock();
    if(!locked_entry) {
      return;
    }
    locked_entry->m_is_released = true;
    if(auto locked_entries = entries.lock()) {
      std::erase(*locked_entries, locked_entry);
    }
  });
}

void Signal::operator ()() const {
  auto entries = *m_entries;
  for(auto& entry : entries) {
    if(!entry->m_is_released) {
      entry->m_observer();
    }
  }
}
