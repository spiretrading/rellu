#ifndef RELLU_COMPONENT_DROP_HPP
#define RELLU_COMPONENT_DROP_HPP
#include <QObject>
#include "Rellu/Editor/Editor.hpp"
#include "DocumentView.hpp"

namespace Rellu {

  /**
   * Drops a component dragged out of the Document Assets panel on a
   * document view as make_component_drop does. The instance it would
   * replace is outlined, filled with translucent blue and shown with the
   * swap pointer while dragged over; elsewhere the drag shows the plain
   * arrow.
   */
  class ComponentDrop : public QObject {
    public:

      /**
       * Constructs a ComponentDrop accepting drops on a view.
       * @param view The view, which must outlive the ComponentDrop.
       */
      explicit ComponentDrop(DocumentView& view);

      /**
       * Sets the editor placing the instances.
       * @param editor The editor, or <code>nullptr</code> to refuse drops.
       */
      void set_editor(Editor* editor);

    protected:
      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      DocumentView* m_view;
      Editor* m_editor;
  };
}

#endif
