#include <memory>
#include <vector>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Ui/ComponentPropertyEditors.hpp"

using namespace Rellu;

namespace {
  const ComponentInstance* find_role(
      const Document& document, const NodeId& id) {
    if(auto node = document.find(id)) {
      return get_instance_role(*node);
    }
    return nullptr;
  }

  const Component* find_component(
      const Document& document, const ComponentId& id) {
    auto& components = document.get_resources().m_components;
    if(auto entry = components.find(id); entry != components.end()) {
      return &entry->second;
    }
    return nullptr;
  }

  std::unique_ptr<Command> set_component_name(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto name = std::get_if<QString>(&value);
    if(!name || nodes.size() != 1 || name->trimmed().isEmpty()) {
      return nullptr;
    }
    auto role = find_role(document, nodes.front());
    if(!role || !role->m_is_main ||
        is_linked(document, role->m_component)) {
      return nullptr;
    }
    return std::make_unique<RenameComponentCommand>(
      role->m_component, name->toStdString());
  }

  std::unique_ptr<Command> set_state(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto choice = std::get_if<Choice>(&value);
    if(!choice || nodes.empty()) {
      return nullptr;
    }
    auto first = find_role(document, nodes.front());
    if(!first) {
      return nullptr;
    }
    auto component = find_component(document, first->m_component);
    if(!component || choice->m_index < 0 || choice->m_index >
        static_cast<int>(component->m_states.size())) {
      return nullptr;
    }
    auto state = StateId();
    if(choice->m_index != 0) {
      state = component->m_states[choice->m_index - 1].m_id;
    }
    auto instances = std::vector<NodeId>();
    for(auto& id : nodes) {
      if(auto role = find_role(document, id);
          role && role->m_component == first->m_component) {
        instances.push_back(id);
      }
    }
    return std::make_unique<SetInstanceStateCommand>(
      std::move(instances), state);
  }

  std::unique_ptr<Command> set_state_name(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto name = std::get_if<QString>(&value);
    if(!name || nodes.size() != 1 || name->trimmed().isEmpty()) {
      return nullptr;
    }
    auto role = find_role(document, nodes.front());
    if(!role || !role->m_is_main || role->m_state.is_nil() ||
        is_linked(document, role->m_component)) {
      return nullptr;
    }
    return std::make_unique<RenameStateCommand>(
      role->m_component, role->m_state, name->toStdString());
  }

  PropertyEditor make_editor(PropertySetter setter) {
    auto editor = PropertyEditor();
    editor.m_setter = std::move(setter);
    return editor;
  }
}

void Rellu::add_component_property_editors(PropertyEditors& editors) {
  editors.add("component.name", make_editor(set_component_name));
  editors.add("component.state", make_editor(set_state));
  editors.add("component.state_name", make_editor(set_state_name));
}
