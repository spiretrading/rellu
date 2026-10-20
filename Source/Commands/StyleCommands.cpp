#include <unordered_set>
#include "Rellu/Commands/StyleCommands.hpp"

using namespace Rellu;

namespace {
  void collect_styled(const Document& document, const NodeId& id,
      std::unordered_set<NodeId>& visited, std::vector<NodeId>& targets) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      return;
    }
    if(node->get_group()) {
      for(auto& child : node->m_children) {
        collect_styled(document, child, visited, targets);
      }
    } else if(!node->get_reference()) {
      targets.push_back(id);
    }
  }

  std::vector<NodeId> get_styled(
      const Document& document, const std::vector<NodeId>& nodes) {
    auto visited = std::unordered_set<NodeId>();
    auto targets = std::vector<NodeId>();
    for(auto& id : nodes) {
      collect_styled(document, id, visited, targets);
    }
    return targets;
  }

  template<typename T>
  void assign(std::optional<T>& target, const std::optional<T>& source) {
    if(source) {
      target = source;
    }
  }

  template<typename T>
  void assign(T& target, const std::optional<T>& source) {
    if(source) {
      target = *source;
    }
  }

  void edit_stroke(Style& style, const StrokeEdit& edit) {
    auto stroke = style.m_stroke.value_or(Stroke());
    assign(stroke.m_color, edit.m_color);
    assign(stroke.m_width, edit.m_width);
    assign(stroke.m_alignment, edit.m_alignment);
    assign(stroke.m_cap, edit.m_cap);
    assign(stroke.m_join, edit.m_join);
    assign(stroke.m_miter_limit, edit.m_miter_limit);
    assign(stroke.m_dashes, edit.m_dashes);
    assign(stroke.m_is_enabled, edit.m_is_enabled);
    style.m_stroke = std::move(stroke);
  }
}

SetFillCommand::SetFillCommand(std::vector<NodeId> nodes, Fill fill) noexcept
  : SetFillCommand(std::move(nodes), std::move(fill), "Change Fill") {}

SetFillCommand::SetFillCommand(
  std::vector<NodeId> nodes, Fill fill, std::string label) noexcept
  : m_nodes(std::move(nodes)),
    m_fill(std::move(fill)),
    m_label(std::move(label)) {}

std::string SetFillCommand::get_label() const {
  return m_label;
}

void SetFillCommand::apply(const Document& document, Transaction& transaction) {
  for(auto& id : get_styled(document, m_nodes)) {
    auto& node = document.get(id);
    if(auto shape = node.get_shape()) {
      if(shape->m_style.m_fill != m_fill) {
        auto payload = *shape;
        payload.m_style.m_fill = m_fill;
        transaction.replace_payload(id, std::move(payload));
      }
    } else if(auto text = node.get_text()) {
      auto payload = *text;
      payload.m_style.m_fill = m_fill;
      if(auto solid = std::get_if<SolidFill>(&m_fill)) {
        payload.m_base_style.m_color = solid->m_color;
        for(auto& run : payload.m_runs) {
          run.m_style.m_color = solid->m_color;
        }
      }
      if(payload != *text) {
        transaction.replace_payload(id, std::move(payload));
      }
    } else if(auto artboard = node.get_artboard()) {
      if(artboard->m_background != m_fill) {
        auto payload = *artboard;
        payload.m_background = m_fill;
        transaction.replace_payload(id, std::move(payload));
      }
    }
  }
}

bool SetFillCommand::merge(const Command& command) {
  auto fill = dynamic_cast<const SetFillCommand*>(&command);
  if(!fill || fill->m_nodes != m_nodes || fill->m_label != m_label) {
    return false;
  }
  m_fill = fill->m_fill;
  return true;
}

SetStrokeCommand::SetStrokeCommand(
  std::vector<NodeId> nodes, StrokeEdit edit) noexcept
  : m_nodes(std::move(nodes)),
    m_edit(std::move(edit)) {}

std::string SetStrokeCommand::get_label() const {
  return "Change Stroke";
}

void SetStrokeCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_styled(document, m_nodes)) {
    auto& node = document.get(id);
    if(auto shape = node.get_shape()) {
      auto payload = *shape;
      edit_stroke(payload.m_style, m_edit);
      if(payload != *shape) {
        transaction.replace_payload(id, std::move(payload));
      }
    } else if(auto text = node.get_text()) {
      auto payload = *text;
      edit_stroke(payload.m_style, m_edit);
      if(payload != *text) {
        transaction.replace_payload(id, std::move(payload));
      }
    }
  }
}

bool SetStrokeCommand::merge(const Command& command) {
  auto stroke = dynamic_cast<const SetStrokeCommand*>(&command);
  if(!stroke || stroke->m_nodes != m_nodes) {
    return false;
  }
  auto& edit = stroke->m_edit;
  assign(m_edit.m_color, edit.m_color);
  assign(m_edit.m_width, edit.m_width);
  assign(m_edit.m_alignment, edit.m_alignment);
  assign(m_edit.m_cap, edit.m_cap);
  assign(m_edit.m_join, edit.m_join);
  assign(m_edit.m_miter_limit, edit.m_miter_limit);
  assign(m_edit.m_dashes, edit.m_dashes);
  assign(m_edit.m_is_enabled, edit.m_is_enabled);
  return true;
}
