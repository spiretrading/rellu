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
  std::optional<InteractionSelection> m_interactions_before;
  std::optional<InteractionSelection> m_interactions_after;
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
  m_history.set_settle_provider([this] {
    return get_settled_containers();
  });
  m_history.set_settle_observer([this] (const auto& nodes) {
    on_settle(nodes);
  });
}

Document& Editor::get_document() const {
  return *m_document;
}

const Command* Editor::capture(auto apply) {
  auto before = m_selection.get_nodes();
  auto before_primary = m_selection.get_primary();
  auto interactions = m_context.get_interaction_selection();
  auto guard = Guard(m_is_updating);
  auto command = apply();
  if(!command) {
    return nullptr;
  }
  if(auto selection = command->get_selection()) {
    m_selection.select(*selection);
  }
  annotate(std::move(before), before_primary, std::move(interactions));
  return command;
}

void Editor::annotate(std::vector<NodeId> before, const NodeId& primary,
    std::optional<InteractionSelection> interactions) {
  auto annotation = m_history.get_undo_annotation();
  if(!annotation) {
    return;
  }
  if(auto record = std::any_cast<Record>(annotation)) {
    record->m_after = m_selection.get_nodes();
    record->m_after_primary = m_selection.get_primary();
    record->m_interactions_after = m_context.get_interaction_selection();
  } else {
    *annotation = Record(std::move(before), primary, m_selection.get_nodes(),
      m_selection.get_primary(), std::move(interactions),
      m_context.get_interaction_selection());
  }
}

void Editor::record(std::string label,
    std::optional<InteractionSelection> interactions) {
  m_history.record(std::move(label), {}, {});
  annotate(m_selection.get_nodes(), m_selection.get_primary(),
    std::move(interactions));
}

void Editor::update_selection() {
  if(auto annotation = m_history.get_undo_annotation()) {
    if(auto record = std::any_cast<Record>(annotation)) {
      record->m_after = m_selection.get_nodes();
      record->m_after_primary = m_selection.get_primary();
      record->m_interactions_after = m_context.get_interaction_selection();
    }
  }
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
    m_context.restore_interactions(
      m_selection.get_primary(), record->m_interactions_before);
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
    m_context.restore_interactions(
      m_selection.get_primary(), record->m_interactions_after);
  }
  return true;
}

void Editor::select_all() {
  if(auto& container = m_context.get_container(); !container.is_nil()) {
    m_selection.select_all(container);
    return;
  }
  auto nodes = std::vector<NodeId>();
  for(auto& id : m_document->get_canvas()) {
    auto& node = m_document->get(id);
    if(node.get_artboard()) {
      nodes.insert(nodes.end(), node.m_children.begin(), node.m_children.end());
    } else {
      nodes.push_back(id);
    }
  }
  m_selection.select(nodes);
}

std::vector<NodeId> Editor::get_settled_containers() const {
  auto nodes = std::vector<NodeId>();
  auto is_top_level = [&] (const Node& node) {
    if(node.m_parent.is_nil()) {
      return m_document->is_on_canvas(node.get_id());
    }
    return static_cast<bool>(m_document->get(node.m_parent).get_artboard());
  };
  for(auto& id : m_selection.get_nodes()) {
    auto node = m_document->find(id);
    if(node && !node->get_artboard() && !node->m_children.empty() &&
        is_top_level(*node)) {
      nodes.push_back(id);
    }
  }
  for(auto node = m_document->find(m_context.get_container());
      node && !node->get_artboard(); node = m_document->find(node->m_parent)) {
    auto group = node->get_group();
    if(group && group->m_component_instance) {
      auto instances = find_instances(
        *m_document, group->m_component_instance->m_component);
      nodes.insert(nodes.end(), instances.begin(), instances.end());
    } else if(is_top_level(*node)) {
      nodes.push_back(node->get_id());
    }
    if(node->m_parent.is_nil()) {
      break;
    }
  }
  return nodes;
}

void Editor::on_settle(const std::vector<NodeId>& nodes) {
  for(auto& id : nodes) {
    if(m_selection.contains(id) || id == m_context.get_container() ||
        is_ancestor(*m_document, id, m_context.get_container())) {
      m_context.set_artboard(m_document->get(id).m_parent);
    }
  }
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
