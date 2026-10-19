#ifndef RELLU_UI_COMPONENT_ACTIONS_HPP
#define RELLU_UI_COMPONENT_ACTIONS_HPP
#include <functional>
#include <vector>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"

namespace Rellu {

  /**
   * Handles the actions navigating between component instances and their
   * main components, and the actions adding and deleting the states of the
   * selected main component: object.edit_main_component,
   * object.previous_instance, object.reveal_component, component.add_state,
   * component.add_hover_state, component.add_toggle_state and
   * component.delete_state, enabling each while it applies to the selection.
   */
  class ComponentActions {
    public:

      /** Shows a node to the user. */
      using NodeHandler = std::function<void (const NodeId& id)>;

      /** Shows a component to the user. */
      using ComponentHandler = std::function<void (const ComponentId& id)>;

      /**
       * Constructs ComponentActions without an editor.
       * @param actions The registry holding the actions, to which the
       *        component state actions are added.
       */
      explicit ComponentActions(ActionRegistry& actions);

      ~ComponentActions();

      /** Returns the editor acted on, or <code>nullptr</code>. */
      Editor* get_editor() const;

      /**
       * Sets the editor acted on.
       * @param editor The editor, or <code>nullptr</code> for none.
       */
      void set_editor(Editor* editor);

      /**
       * Sets the function showing a node that becomes selected.
       * @param handler The function.
       */
      void set_reveal_handler(NodeHandler handler);

      /**
       * Sets the function showing a component in the assets panel.
       * @param handler The function.
       */
      void set_component_handler(ComponentHandler handler);

      /**
       * Selects a component's main component, placing one on the pasteboard
       * as Edit Main Component does when the component has none.
       * @param component The component.
       * @return <code>true</code> iff a main component is selected.
       */
      bool edit_main_component(const ComponentId& component);

      /**
       * Selects the instance the most recent Edit Main Component started
       * from.
       * @return <code>true</code> iff an instance is selected.
       */
      bool return_to_instance();

      /** Enables the actions that apply to the editor's selection. */
      void update();

    private:
      ActionRegistry* m_actions;
      Editor* m_editor;
      NodeHandler m_reveal_handler;
      ComponentHandler m_component_handler;
      std::vector<NodeId> m_instances;
      Subscription m_selection_subscription;
      Subscription m_history_subscription;

      ComponentActions(const ComponentActions&) = delete;
      ComponentActions& operator =(const ComponentActions&) = delete;
      ComponentId get_selected_component() const;
      NodeId get_selected_main() const;
      void add_state(StateKind kind);
      void delete_state();
  };
}

#endif
