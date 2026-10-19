#include <utility>
#include "Rellu/Commands/History.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  class Guard {
    public:
      explicit Guard(bool& flag)
          : m_flag(&flag),
            m_previous(flag) {
        flag = true;
      }

      ~Guard() {
        *m_flag = m_previous;
      }

    private:
      bool* m_flag;
      bool m_previous;

      Guard(const Guard&) = delete;
      Guard& operator =(const Guard&) = delete;
  };
}

History::History(Document& document)
    : m_document(&document),
      m_clean(0),
      m_base(document.get_undo_count()),
      m_is_open(false),
      m_is_applying(false),
      m_is_recording(false) {
  m_subscription = document.subscribe([this] (const auto& changes) {
    on_change(changes);
  });
}

Document& History::get_document() const {
  return *m_document;
}

void History::set_compound_filter(CompoundFilter filter) {
  m_filter = std::move(filter);
}

const Command* History::execute(std::unique_ptr<Command> command) {
  synchronize();
  auto is_changed = [&] {
    auto guard = Guard(m_is_applying);
    auto transaction = Transaction(*m_document);
    command->apply(*m_document, transaction);
    auto recording = Guard(m_is_recording);
    return transaction.commit();
  }();
  if(!is_changed) {
    m_changes = ChangeSet();
    synchronize();
    return nullptr;
  }
  refresh();
  auto label = command->get_label();
  auto result = command.get();
  push(Step(std::move(label), std::move(command), std::any()));
  m_is_open = true;
  m_signal();
  synchronize();
  return result;
}

const Command* History::merge(std::unique_ptr<Command> command) {
  synchronize();
  if(!m_is_open || m_undo.empty() || !m_undo.back().m_command ||
      !m_undo.back().m_command->merge(*command)) {
    return execute(std::move(command));
  }
  auto is_recorded = [&] {
    auto guard = Guard(m_is_applying);
    try {
      auto transaction = Transaction(*m_document);
      command->apply(*m_document, transaction);
      auto recording = Guard(m_is_recording);
      return transaction.amend();
    } catch(...) {
      m_is_open = false;
      throw;
    }
  }();
  if(is_recorded) {
    refresh();
  } else {
    m_changes = ChangeSet();
  }
  if(m_clean == m_undo.size()) {
    m_clean = std::nullopt;
  }
  auto result = static_cast<const Command*>(nullptr);
  if(is_recorded) {
    auto& step = m_undo.back();
    step.m_label = step.m_command->get_label();
    result = step.m_command.get();
  } else {
    m_undo.pop_back();
    m_is_open = false;
  }
  m_signal();
  synchronize();
  return result;
}

void History::seal() {
  m_is_open = false;
}

bool History::can_undo() const {
  return !m_undo.empty();
}

bool History::can_redo() const {
  return !m_redo.empty();
}

std::size_t History::get_undo_count() const {
  return m_undo.size();
}

std::size_t History::get_redo_count() const {
  return m_redo.size();
}

std::string History::get_undo_label() const {
  if(m_undo.empty()) {
    return std::string();
  }
  return m_undo.back().m_label;
}

std::string History::get_redo_label() const {
  if(m_redo.empty()) {
    return std::string();
  }
  return m_redo.back().m_label;
}

std::any* History::get_undo_annotation() {
  if(m_undo.empty()) {
    return nullptr;
  }
  return &m_undo.back().m_annotation;
}

std::any* History::get_redo_annotation() {
  if(m_redo.empty()) {
    return nullptr;
  }
  return &m_redo.back().m_annotation;
}

bool History::undo() {
  synchronize();
  if(m_undo.empty()) {
    return false;
  }
  {
    auto guard = Guard(m_is_applying);
    m_document->undo();
  }
  m_redo.push_back(std::move(m_undo.back()));
  m_undo.pop_back();
  m_is_open = false;
  m_signal();
  return true;
}

bool History::redo() {
  synchronize();
  if(m_redo.empty()) {
    return false;
  }
  {
    auto guard = Guard(m_is_applying);
    m_document->redo();
  }
  m_undo.push_back(std::move(m_redo.back()));
  m_redo.pop_back();
  m_is_open = false;
  m_signal();
  return true;
}

bool History::is_dirty() const {
  return m_clean != m_undo.size();
}

void History::mark_clean() {
  if(m_clean == m_undo.size()) {
    return;
  }
  m_clean = m_undo.size();
  m_signal();
}

void History::clear() {
  auto is_clean = !is_dirty();
  m_document->clear_history();
  m_base = 0;
  m_undo.clear();
  m_redo.clear();
  if(is_clean) {
    m_clean = 0;
  } else {
    m_clean = std::nullopt;
  }
  m_is_open = false;
  m_signal();
}

Subscription History::subscribe(std::function<void ()> observer) {
  return m_signal.subscribe(std::move(observer));
}

void History::push(Step step) {
  if(m_clean && *m_clean > m_undo.size()) {
    m_clean = std::nullopt;
  }
  m_redo.clear();
  m_undo.push_back(std::move(step));
}

void History::synchronize() {
  auto count = m_document->get_undo_count();
  if(m_base + m_undo.size() >= count) {
    return;
  }
  while(m_base + m_undo.size() < count) {
    push(Step(std::string(), nullptr, std::any()));
  }
  m_is_open = false;
  m_signal();
}

void History::refresh() {
  auto changes = std::exchange(m_changes, ChangeSet());
  auto guard = Guard(m_is_applying);
  refresh_compounds(*m_document, changes, m_filter);
}

void History::on_change(const ChangeSet& changes) {
  if(m_is_recording) {
    m_changes = changes;
  }
  if(!m_is_applying) {
    synchronize();
  }
}
