#include <algorithm>
#include <utility>
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
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

  void append(ChangeSet& changes, const ChangeSet& addition) {
    changes.m_inserted.insert(changes.m_inserted.end(),
      addition.m_inserted.begin(), addition.m_inserted.end());
    changes.m_removed.insert(changes.m_removed.end(),
      addition.m_removed.begin(), addition.m_removed.end());
    changes.m_moved.insert(changes.m_moved.end(), addition.m_moved.begin(),
      addition.m_moved.end());
    changes.m_modified.insert(changes.m_modified.end(),
      addition.m_modified.begin(), addition.m_modified.end());
  }
}

History::History(Document& document)
    : m_document(&document),
      m_clean(0),
      m_base(document.get_undo_count()),
      m_external(0),
      m_is_open(false),
      m_is_applying(false),
      m_is_recording(false),
      m_hold(0) {
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

void History::set_text_measurer(TextMeasurer measurer) {
  m_layout.m_measurer = std::move(measurer);
}

void History::set_pointer(std::optional<Point> pointer) {
  m_pointer = pointer;
}

void History::set_settle_provider(SettleProvider provider) {
  m_settle_provider = std::move(provider);
}

void History::set_settle_observer(SettleObserver observer) {
  m_settle_observer = std::move(observer);
}

void History::hold_settling() {
  if(m_settle_hold) {
    return;
  }
  synchronize();
  m_settle_hold = get_depth();
  m_pending.clear();
}

void History::release_settling() {
  if(!m_settle_hold) {
    return;
  }
  auto hold = *std::exchange(m_settle_hold, std::nullopt);
  auto nodes = std::exchange(m_pending, std::vector<NodeId>());
  synchronize();
  if(get_depth() <= hold || m_document->get_undo_count() != get_depth()) {
    return;
  }
  std::erase_if(nodes, [&] (const auto& id) {
    return !m_document->find(id);
  });
  {
    auto guard = Guard(m_is_applying);
    auto recording = Guard(m_is_recording);
    settle(std::move(nodes));
    m_changes = ChangeSet();
  }
  m_signal();
}

void History::hold_layout(std::vector<NodeId> nodes) {
  synchronize();
  m_layout.m_floating = std::move(nodes);
  m_hold = get_depth();
}

void History::release_layout() {
  auto nodes = std::exchange(m_layout.m_floating, std::vector<NodeId>());
  if(nodes.empty()) {
    return;
  }
  synchronize();
  if(get_depth() <= m_hold ||
      m_document->get_undo_count() != get_depth()) {
    return;
  }
  auto changes = ChangeSet();
  for(auto& id : nodes) {
    if(m_document->find(id)) {
      changes.m_modified.push_back(NodeChange(id, ChangeMask::TRANSFORM));
    }
  }
  {
    auto guard = Guard(m_is_applying);
    auto recording = Guard(m_is_recording);
    if(refresh_layouts(*m_document, changes, m_layout)) {
      refresh_compounds(
        *m_document, std::exchange(m_changes, ChangeSet()), m_filter);
    }
    m_changes = ChangeSet();
  }
  if(m_document->get_undo_count() < get_depth()) {
    m_undo.pop_back();
    m_is_open = false;
  }
  m_signal();
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
  if(m_document->get_undo_count() <= get_depth()) {
    synchronize();
    return nullptr;
  }
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
    is_recorded = m_document->get_undo_count() == get_depth();
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

void History::record(std::string label, std::function<void ()> revert,
    std::function<void ()> reapply) {
  synchronize();
  auto step = Step(std::move(label), nullptr, std::any());
  step.m_is_external = true;
  step.m_revert = std::move(revert);
  step.m_reapply = std::move(reapply);
  push(std::move(step));
  ++m_external;
  m_is_open = false;
  m_signal();
}

bool History::attach(
    std::function<void ()> revert, std::function<void ()> reapply) {
  synchronize();
  if(m_undo.empty()) {
    return false;
  }
  auto& step = m_undo.back();
  step.m_revert = [first = std::move(revert),
      second = std::move(step.m_revert)] {
    if(first) {
      first();
    }
    if(second) {
      second();
    }
  };
  step.m_reapply = [first = std::move(step.m_reapply),
      second = std::move(reapply)] {
    if(first) {
      first();
    }
    if(second) {
      second();
    }
  };
  return true;
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
  if(m_undo.back().m_is_external) {
    --m_external;
    if(m_undo.back().m_revert) {
      m_undo.back().m_revert();
    }
  } else {
    {
      auto guard = Guard(m_is_applying);
      m_document->undo();
    }
    if(m_undo.back().m_revert) {
      m_undo.back().m_revert();
    }
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
  if(m_redo.back().m_is_external) {
    ++m_external;
    if(m_redo.back().m_reapply) {
      m_redo.back().m_reapply();
    }
  } else {
    {
      auto guard = Guard(m_is_applying);
      m_document->redo();
    }
    if(m_redo.back().m_reapply) {
      m_redo.back().m_reapply();
    }
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
  m_external = 0;
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

std::size_t History::get_depth() const {
  return m_base + m_undo.size() - m_external;
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
  if(get_depth() >= count) {
    return;
  }
  while(get_depth() < count) {
    push(Step(std::string(), nullptr, std::any()));
  }
  m_is_open = false;
  m_signal();
}

void History::refresh() {
  auto changes = std::exchange(m_changes, ChangeSet());
  auto guard = Guard(m_is_applying);
  refresh_components(*m_document, changes);
  auto recording = Guard(m_is_recording);
  if(refresh_compounds(*m_document, changes, m_filter)) {
    append(changes, std::exchange(m_changes, ChangeSet()));
  }
  if(refresh_layouts(*m_document, changes, m_layout)) {
    auto laid_out = std::exchange(m_changes, ChangeSet());
    refresh_compounds(*m_document, laid_out, m_filter);
    append(changes, laid_out);
  }
  m_changes = ChangeSet();
  refresh_component_sizes(*m_document, changes, m_layout.m_measurer);
  m_changes = ChangeSet();
  auto nodes = get_settled_nodes(*m_document, changes);
  if(m_settle_hold) {
    for(auto& id : nodes) {
      if(std::find(m_pending.begin(), m_pending.end(), id) ==
          m_pending.end()) {
        m_pending.push_back(id);
      }
    }
  } else {
    settle(std::move(nodes));
    m_changes = ChangeSet();
  }
}

void History::settle(std::vector<NodeId> nodes) {
  if(m_settle_provider) {
    for(auto& id : m_settle_provider()) {
      if(m_document->find(id) &&
          std::find(nodes.begin(), nodes.end(), id) == nodes.end()) {
        nodes.push_back(id);
      }
    }
  }
  if(nodes.empty()) {
    return;
  }
  auto transaction = Transaction(*m_document);
  auto moved = settle_nodes(
    *m_document, transaction, nodes, m_pointer, m_layout.m_measurer);
  if(moved.empty()) {
    return;
  }
  transaction.amend();
  if(m_settle_observer) {
    m_settle_observer(moved);
  }
}

void History::on_change(const ChangeSet& changes) {
  if(m_is_recording) {
    m_changes = changes;
  }
  if(!m_is_applying) {
    synchronize();
  }
}
