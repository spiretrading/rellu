#ifndef RELLU_UI_PROTOTYPE_INSPECTOR_HPP
#define RELLU_UI_PROTOTYPE_INSPECTOR_HPP
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Prototype.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * The property inspector of Adobe XD's Prototype mode, editing the
   * interactions of the selected node: the INTERACTION section with its +
   * adding an interaction like the one last edited, a row per interaction
   * when there are several, and the Trigger and, under ACTION, the Type,
   * Destination, Animation, Easing and Duration of the one being edited,
   * with the fields particular to its trigger and action (the delay of a
   * time trigger, the keys of a key trigger, the address of a hyperlink,
   * the position of an overlay, Preserve Scroll Position) and the ACTION
   * row adding a second action; the TEXT HYPERLINKS section of text with
   * links, editing the address of each; the FLOW section of an artboard;
   * and the SCROLLING section. The triggers, action types and destinations
   * offered depend on the node as in Adobe XD: Time only for an artboard,
   * Hover only within a component with several states, Tap, Drag, Hover
   * and Time once per node, the other states of a component with states
   * before the artboards other than the node's own. Voice triggers, second
   * actions and the Scroll To, Audio Playback and Speech Playback actions
   * are listed but unavailable. Each change is one undoable step. Without
   * an interaction to edit, the fields show Adobe XD's placeholder values,
   * disabled.
   */
  class PrototypeInspector : public QWidget {
    public:

      /**
       * Makes the command setting whether a node keeps its place when its
       * artboard scrolls, as Fix Position When Scrolling does.
       */
      using FixedPositionFactory = std::function<
        std::unique_ptr<Command> (const NodeId& node, bool is_fixed)>;

      /** The labels of the triggers, in Adobe XD's order. */
      static const std::vector<std::string>& get_trigger_labels();

      /** The labels of the action types, in Adobe XD's order. */
      static const std::vector<std::string>& get_type_labels();

      /**
       * Constructs a PrototypeInspector without an editor.
       * @param parent The parent widget.
       */
      explicit PrototypeInspector(QWidget* parent = nullptr);

      ~PrototypeInspector() override;

      /** Returns the editor whose selection is edited, if any. */
      Editor* get_editor() const;

      /**
       * Edits the interactions of an editor's selection, following it and
       * its document as they change.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this inspector or be replaced first.
       */
      void set_editor(Editor* editor);

      /**
       * Sets the function making the command of the Fix Position When
       * Scrolling checkbox, which is disabled without one.
       * @param factory The function.
       */
      void set_fixed_position_factory(FixedPositionFactory factory);

      /** Returns the node inspected, or nil if there is none. */
      const NodeId& get_node() const;

      /** Returns the interaction being edited, or nil if there is none. */
      const InteractionId& get_current() const;

      /**
       * Edits one of the inspected node's interactions.
       * @param id The interaction's id.
       */
      void set_current(const InteractionId& id);

      /** Returns the names of the fields shown, from top to bottom. */
      std::vector<std::string> get_fields() const;

      /**
       * Returns the widget showing a field, such as trigger or destination.
       * @param name The field's name.
       * @return The widget, or <code>nullptr</code> if the field is not
       *         shown.
       */
      QWidget* find_field(std::string_view name) const;

      /** Shows the current state of the selection and document at once. */
      void refresh();

    private:
      Editor* m_editor;
      FixedPositionFactory m_fixed_position_factory;
      Subscription m_subscription;
      Subscription m_selection_subscription;
      NodeId m_node;
      InteractionId m_current;
      std::optional<Interaction> m_last;
      bool m_is_pending;
      QScrollArea* m_scroll;
      QWidget* m_content;
      QVBoxLayout* m_layout;
      std::vector<std::pair<std::string, QWidget*>> m_fields;

      void schedule();
      void rebuild();
      void apply(Interaction interaction);
  };
}

#endif
