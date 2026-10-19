#ifndef RELLU_COMPONENT_DROP_HPP
#define RELLU_COMPONENT_DROP_HPP
#include <QObject>
#include "Rellu/Editor/Editor.hpp"
#include "DocumentView.hpp"

namespace Rellu {

  /**
   * Places an instance of a component dragged out of the Document Assets
   * panel where it is dropped on a document view, inside the artboard under
   * the pointer if there is one. Dropped on an instance of another component,
   * the outermost one under the pointer within the edit context, which is
   * outlined while dragged over, it replaces that instance instead.
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
