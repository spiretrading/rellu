#ifndef RELLU_UI_TEXT_ACTIONS_HPP
#define RELLU_UI_TEXT_ACTIONS_HPP
#include <optional>
#include <vector>
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"

namespace Rellu {

  /**
   * Handles the actions of Adobe XD's Object > Text menu:
   * object.text.bold, object.text.italic, object.text.underline,
   * object.text.strikethrough, object.text.uppercase, object.text.lowercase,
   * object.text.title_case, object.text.superscript, object.text.subscript,
   * object.text.increase_size and object.text.decrease_size. Each applies as
   * the TEXT section does: to the range selected in the text being edited,
   * to the style of the text typed at its caret, or to the whole of the
   * selected text nodes. Bold and Italic switch to the face of the font's
   * family with or without a bold weight or italic slant. The actions are
   * enabled while text is selected, the toggles checked while the style
   * the TEXT section shows has their property.
   */
  class TextActions {
    public:

      /** The change in font size of Increase and Decrease Font Size. */
      static constexpr auto SIZE_STEP = 1.0;

      /**
       * Sets the handlers of the actions, which are disabled until an
       * editor is set. The registry must already have Adobe XD's menus.
       * @param actions The registry holding the actions.
       * @param library The fonts that font faces are chosen among, which
       *        must outlive this object.
       */
      TextActions(ActionRegistry& actions, const FontLibrary& library);

      ~TextActions();

      /** Returns the editor acted on, if any. */
      Editor* get_editor() const;

      /**
       * Sets the editor acted on.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this object or be replaced first.
       */
      void set_editor(Editor* editor);

      /** Updates the enabled and checked state of every action. */
      void update();

    private:
      ActionRegistry* m_actions;
      const FontLibrary* m_library;
      TextLayouter m_layouter;
      Editor* m_editor;
      Subscription m_selection_subscription;
      Subscription m_history_subscription;
      Subscription m_text_selection_subscription;

      TextActions(const TextActions&) = delete;
      TextActions& operator =(const TextActions&) = delete;
      std::vector<NodeId> get_texts() const;
      std::optional<TextStyle> get_style() const;
      void apply(TextStyleEdit edit);
      void toggle_weight();
      void toggle_slant();
      void change_size(double delta);
  };
}

#endif
