#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Ui/ComponentActions.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAIN_OFFSET = 100.0;

  const auto EDIT_MAIN = "object.edit_main_component";
  const auto PREVIOUS_INSTANCE = "object.previous_instance";
  const auto REVEAL_COMPONENT = "object.reveal_component";
  const auto ADD_STATE = "component.add_state";
  const auto ADD_HOVER_STATE = "component.add_hover_state";
  const auto ADD_TOGGLE_STATE = "component.add_toggle_state";
  const auto DELETE_STATE = "component.delete_state";

  void ensure(ActionRegistry& actions, const char* name, const QString& text) {
    if(!actions.find(name)) {
      actions.add(ActionDefinition(
        name, translate("menu", text), QKeySequence(), "", "component"));
    }
  }

  std::string make_state_name(const Document& document, const ComponentId& id,
      StateKind kind) {
    auto& component = document.get_resources().m_components.at(id);
    auto is_taken = [&] (const std::string& candidate) {
      return std::any_of(component.m_states.begin(), component.m_states.end(),
        [&] (const auto& state) { return state.m_name == candidate; });
    };
    if(kind == StateKind::NEW) {
      for(auto i = component.m_states.size() + 2;; ++i) {
        auto candidate = translate("name", "State %1").arg(i).toStdString();
        if(!is_taken(candidate)) {
          return candidate;
        }
      }
    }
    auto name = std::string(localize("name", "Hover State"));
    if(kind == StateKind::TOGGLE) {
      name = localize("name", "Toggle State");
    }
    if(!is_taken(name)) {
      return name;
    }
    for(auto i = 2;; ++i) {
      auto candidate = name + " " + std::to_string(i);
      if(!is_taken(candidate)) {
        return candidate;
      }
    }
  }
}

ComponentActions::ComponentActions(ActionRegistry& actions)
    : m_actions(&actions),
      m_editor(nullptr) {
  ensure(actions, ADD_STATE, "New State");
  ensure(actions, ADD_HOVER_STATE, "Hover State");
  ensure(actions, ADD_TOGGLE_STATE, "Toggle State");
  ensure(actions, DELETE_STATE, "Delete State");
  actions.set_handler(EDIT_MAIN, [this] {
    auto component = get_selected_component();
    if(!component.is_nil()) {
      edit_main_component(component);
    }
  });
  actions.set_handler(PREVIOUS_INSTANCE, [this] {
    return_to_instance();
  });
  actions.set_handler(REVEAL_COMPONENT, [this] {
    auto component = get_selected_component();
    if(!component.is_nil() && m_component_handler) {
      m_component_handler(component);
    }
  });
  actions.set_handler(ADD_STATE, [this] { add_state(StateKind::NEW); });
  actions.set_handler(ADD_HOVER_STATE, [this] {
    add_state(StateKind::HOVER);
  });
  actions.set_handler(ADD_TOGGLE_STATE, [this] {
    add_state(StateKind::TOGGLE);
  });
  actions.set_handler(DELETE_STATE, [this] { delete_state(); });
  update();
}

ComponentActions::~ComponentActions() = default;

Editor* ComponentActions::get_editor() const {
  return m_editor;
}

void ComponentActions::set_editor(Editor* editor) {
  m_selection_subscription.release();
  m_history_subscription.release();
  m_editor = editor;
  m_instances.clear();
  if(m_editor) {
    m_selection_subscription = m_editor->get_selection().subscribe([this] {
      update();
    });
    m_history_subscription = m_editor->get_history().subscribe([this] {
      update();
    });
  }
  update();
}

void ComponentActions::set_reveal_handler(NodeHandler handler) {
  m_reveal_handler = std::move(handler);
}

void ComponentActions::set_component_handler(ComponentHandler handler) {
  m_component_handler = std::move(handler);
}

bool ComponentActions::edit_main_component(const ComponentId& component) {
  if(!m_editor || is_linked(m_editor->get_document(), component)) {
    return false;
  }
  auto& document = m_editor->get_document();
  if(!document.get_resources().m_components.contains(component)) {
    return false;
  }
  auto origin = m_editor->get_selection().get_primary();
  auto instance = origin.is_nil() ? NodeId() : find_instance(document, origin);
  auto main = find_main_component(document, component);
  if(main.is_nil()) {
    auto position = Point(0, 0);
    auto anchor = instance;
    if(!anchor.is_nil()) {
      auto artboard = find_artboard(document, anchor);
      if(!artboard.is_nil()) {
        anchor = artboard;
      }
      if(auto bounds = get_world_bounds(document, anchor)) {
        position = Point(bounds->m_x + bounds->m_width + MAIN_OFFSET,
          bounds->m_y);
      }
    } else if(auto bounds = get_world_bounds(document, document.get_canvas())) {
      position = Point(bounds->m_x + bounds->m_width + MAIN_OFFSET,
        bounds->m_y);
    }
    try {
      auto command = m_editor->execute(
        std::make_unique<RestoreMainComponentCommand>(
          component, make_translation(position.m_x, position.m_y)));
      if(!command) {
        return false;
      }
      main = static_cast<const RestoreMainComponentCommand&>(*command).
        get_main();
    } catch(const std::exception&) {
      return false;
    }
  } else {
    m_editor->get_selection().select(main);
  }
  if(!instance.is_nil() && instance != main) {
    m_instances.push_back(instance);
  }
  if(m_reveal_handler) {
    m_reveal_handler(main);
  }
  update();
  return true;
}

bool ComponentActions::return_to_instance() {
  if(!m_editor) {
    return false;
  }
  while(!m_instances.empty()) {
    auto instance = m_instances.back();
    m_instances.pop_back();
    if(m_editor->get_document().find(instance)) {
      m_editor->get_selection().select(instance);
      if(m_reveal_handler) {
        m_reveal_handler(instance);
      }
      update();
      return true;
    }
  }
  update();
  return false;
}

void ComponentActions::update() {
  auto component = get_selected_component();
  auto main = get_selected_main();
  auto is_editable = !component.is_nil() && m_editor &&
    !is_linked(m_editor->get_document(), component);
  m_actions->set_enabled(EDIT_MAIN, is_editable && main.is_nil());
  m_actions->set_enabled(REVEAL_COMPONENT, !component.is_nil());
  m_actions->set_enabled(PREVIOUS_INSTANCE, m_editor && std::any_of(
    m_instances.begin(), m_instances.end(), [&] (const auto& instance) {
      return m_editor->get_document().find(instance) != nullptr;
    }));
  auto is_main_editable = is_editable && !main.is_nil();
  m_actions->set_enabled(ADD_STATE, is_main_editable);
  m_actions->set_enabled(ADD_HOVER_STATE, is_main_editable);
  m_actions->set_enabled(ADD_TOGGLE_STATE, is_main_editable);
  auto has_state = is_main_editable && !get_instance_role(
    m_editor->get_document().get(main))->m_state.is_nil();
  m_actions->set_enabled(DELETE_STATE, has_state);
}

ComponentId ComponentActions::get_selected_component() const {
  if(!m_editor) {
    return ComponentId();
  }
  auto& primary = m_editor->get_selection().get_primary();
  if(primary.is_nil()) {
    return ComponentId();
  }
  auto& document = m_editor->get_document();
  auto instance = find_instance(document, primary);
  if(instance.is_nil()) {
    return ComponentId();
  }
  return get_instance_role(document.get(instance))->m_component;
}

NodeId ComponentActions::get_selected_main() const {
  if(!m_editor) {
    return NodeId();
  }
  auto& primary = m_editor->get_selection().get_primary();
  if(primary.is_nil()) {
    return NodeId();
  }
  auto& document = m_editor->get_document();
  auto instance = find_instance(document, primary);
  if(instance.is_nil() || !get_instance_role(document.get(instance))->
      m_is_main) {
    return NodeId();
  }
  return instance;
}

void ComponentActions::add_state(StateKind kind) {
  auto main = get_selected_main();
  if(main.is_nil()) {
    return;
  }
  auto& document = m_editor->get_document();
  auto component = get_instance_role(document.get(main))->m_component;
  try {
    m_editor->execute(std::make_unique<AddStateCommand>(
      component, make_state_name(document, component, kind), kind));
  } catch(const std::exception&) {}
  update();
}

void ComponentActions::delete_state() {
  auto main = get_selected_main();
  if(main.is_nil()) {
    return;
  }
  auto& document = m_editor->get_document();
  auto& role = *get_instance_role(document.get(main));
  if(role.m_state.is_nil()) {
    return;
  }
  auto component = role.m_component;
  auto state = role.m_state;
  try {
    m_editor->execute(std::make_unique<RemoveStateCommand>(component, state));
  } catch(const std::exception&) {}
  update();
}
