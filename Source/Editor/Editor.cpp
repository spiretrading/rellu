#include <any>
#include <optional>
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Editor/Editor.hpp"

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

struct Editor::Record {
  std::vector<NodeId> m_before;
  NodeId m_before_primary;
  std::vector<NodeId> m_after;
  NodeId m_after_primary;
};

Editor::Editor(Document& document)
    : m_document(&document),
      m_history(document),
      m_selection(document),
      m_context(document),
      m_text_selection(document),
      m_is_updating(false) {
  m_subscription = m_selection.subscribe([this] {
    on_selection();
  });
}

Document& Editor::get_document() const {
  return *m_document;
}

const Command* Editor::capture(auto apply) {
  auto before = m_selection.get_nodes();
  auto before_primary = m_selection.get_primary();
  auto guard = Guard(m_is_updating);
  auto command = apply();
  if(!command) {
    return nullptr;
  }
  if(auto selection = command->get_selection()) {
    m_selection.select(*selection);
  }
  if(auto annotation = m_history.get_undo_annotation()) {
    if(auto record = std::any_cast<Record>(annotation)) {
      record->m_after = m_selection.get_nodes();
      record->m_after_primary = m_selection.get_primary();
    } else {
      *annotation = Record(std::move(before), before_primary,
        m_selection.get_nodes(), m_selection.get_primary());
    }
  }
  return command;
}

const Command* Editor::execute(std::unique_ptr<Command> command) {
  command = make_instance_edit(
    *m_document, m_selection.get_nodes(), std::move(command));
  return get_edited_command(capture([&] {
    return m_history.execute(std::move(command));
  }));
}

const Command* Editor::merge(std::unique_ptr<Command> command) {
  command = make_instance_edit(
    *m_document, m_selection.get_nodes(), std::move(command));
  return get_edited_command(capture([&] {
    return m_history.merge(std::move(command));
  }));
}

bool Editor::execute(const CommandRegistry& registry, std::string_view name) {
  auto command = registry.make(name, *m_document, m_selection.get_nodes());
  if(!command) {
    return false;
  }
  return execute(std::move(command)) != nullptr;
}

bool Editor::undo() {
  auto annotation = m_history.get_undo_annotation();
  if(!annotation) {
    return false;
  }
  auto record = std::optional<Record>();
  if(auto stored = std::any_cast<Record>(annotation)) {
    record = *stored;
  }
  auto guard = Guard(m_is_updating);
  m_history.undo();
  if(record) {
    m_selection.select(record->m_before, record->m_before_primary);
  }
  return true;
}

bool Editor::redo() {
  auto annotation = m_history.get_redo_annotation();
  if(!annotation) {
    return false;
  }
  auto record = std::optional<Record>();
  if(auto stored = std::any_cast<Record>(annotation)) {
    record = *stored;
  }
  auto guard = Guard(m_is_updating);
  m_history.redo();
  if(record) {
    m_selection.select(record->m_after, record->m_after_primary);
  }
  return true;
}

void Editor::select_all() {
  if(auto& container = m_context.get_container(); !container.is_nil()) {
    m_selection.select_all(container);
    return;
  }
  auto nodes = std::vector<NodeId>();
  auto add = [&] (const NodeId& id) {
    if(!m_document->get(id).m_header.m_is_locked) {
      nodes.push_back(id);
    }
  };
  for(auto& id : m_document->get_canvas()) {
    auto& node = m_document->get(id);
    if(!node.get_artboard()) {
      add(id);
    } else if(!node.m_header.m_is_locked) {
      for(auto& child : node.m_children) {
        add(child);
      }
    }
  }
  m_selection.select(nodes);
}

void Editor::on_selection() {
  if(!m_is_updating) {
    m_history.seal();
  }
  auto artboard = find_artboard(*m_document, m_selection.get_primary());
  if(!artboard.is_nil()) {
    m_context.set_artboard(artboard);
  }
}
