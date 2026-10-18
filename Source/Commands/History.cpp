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
      m_is_open(false),
      m_is_applying(false),
      m_publications(0) {
  m_subscription = document.subscribe([this] (const ChangeSet&) {
    on_change();
  });
}

Document& History::get_document() const {
  return *m_document;
}

const Command* History::execute(std::unique_ptr<Command> command) {
  auto is_changed = [&] {
    auto guard = Guard(m_is_applying);
    return apply(*command);
  }();
  if(!is_changed) {
    return nullptr;
  }
  auto label = command->get_label();
  auto result = command.get();
  push(Step(std::move(label), std::move(command), std::any()));
  m_is_open = true;
  m_signal();
  return result;
}

const Command* History::merge(std::unique_ptr<Command> command) {
  if(!m_is_open || m_undo.empty() || !m_undo.back().m_command ||
      !m_undo.back().m_command->merge(*command)) {
    return execute(std::move(command));
  }
  auto& step = m_undo.back();
  auto is_changed = false;
  {
    auto guard = Guard(m_is_applying);
    m_document->undo();
    try {
      is_changed = apply(*step.m_command);
    } catch(...) {
      m_document->redo();
      m_is_open = false;
      throw;
    }
  }
  if(m_clean == m_undo.size()) {
    m_clean = std::nullopt;
  }
  if(!is_changed) {
    m_undo.pop_back();
    m_is_open = false;
    m_signal();
    return nullptr;
  }
  step.m_label = step.m_command->get_label();
  auto result = step.m_command.get();
  m_signal();
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

bool History::apply(Command& command) {
  auto publications = m_publications;
  {
    auto transaction = Transaction(*m_document);
    command.apply(*m_document, transaction);
    transaction.commit();
  }
  return m_publications != publications;
}

void History::push(Step step) {
  if(m_clean && *m_clean > m_undo.size()) {
    m_clean = std::nullopt;
  }
  m_redo.clear();
  m_undo.push_back(std::move(step));
}

void History::on_change() {
  if(m_is_applying) {
    ++m_publications;
    return;
  }
  push(Step(std::string(), nullptr, std::any()));
  m_is_open = false;
  m_signal();
}
