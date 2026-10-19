#ifndef RELLU_UI_LAYERS_PANEL_HPP
#define RELLU_UI_LAYERS_PANEL_HPP
#include <functional>
#include <QLabel>
#include <QToolButton>
#include <QTreeView>
#include <QWidget>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/LayersModel.hpp"

namespace Rellu {

  /**
   * The Layers panel, listing a document's layers as Adobe XD does: the
   * artboards while the current layer is not inside one, otherwise the
   * layers of the artboard holding the current layer under a heading
   * naming it. With an editor, the layers selected are the editor's
   * selection, the current layer being its primary node, and layers are
   * renamed in place.
   */
  class LayersPanel : public QWidget {
    public:

      /**
       * The type of function called when the current layer changes.
       * @param current The current layer's id, or nil if there is none.
       */
      using CurrentHandler = std::function<void (const NodeId& current)>;

      /**
       * Constructs a LayersPanel without a document.
       * @param parent The parent widget.
       */
      explicit LayersPanel(QWidget* parent = nullptr);

      /** Returns the model of the layers listed. */
      LayersModel& get_model();

      /** Returns the tree view listing the layers. */
      QTreeView& get_view();

      /**
       * Lists a document's layers without an editor, clearing the current
       * layer.
       * @param document The document, or <code>nullptr</code> for none. It
       *        must outlive this panel or be replaced first.
       */
      void set_document(Document* document);

      /** Returns the editor whose selection is shown, if any. */
      Editor* get_editor() const;

      /**
       * Lists the layers of an editor's document, showing and setting its
       * selection.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this panel or be replaced first.
       */
      void set_editor(Editor* editor);

      /** Returns the current layer's id, or nil if there is none. */
      const NodeId& get_current() const;

      /**
       * Makes a layer current and the only one selected, expanding its
       * ancestors and scrolling to it.
       * @param id The layer's id, or nil to clear the current layer.
       */
      void set_current(const NodeId& id);

      /**
       * Returns the artboard whose layers are listed, or nil while the
       * artboards are listed.
       */
      const NodeId& get_scope() const;

      /** Returns the heading above the layers listed. */
      QString get_heading() const;

      /**
       * Starts renaming a layer in place.
       * @param id The layer's id.
       * @return <code>true</code> iff the layer can be renamed.
       */
      bool rename(const NodeId& id);

      /**
       * Sets the function called whenever the current layer changes.
       * @param handler The function to call.
       */
      void set_current_handler(CurrentHandler handler);

      /**
       * Sets the function called when the user makes a layer current in the
       * tree.
       * @param handler The function to call.
       */
      void set_activation_handler(CurrentHandler handler);

    private:
      LayersModel* m_model;
      QTreeView* m_view;
      QToolButton* m_back;
      QLabel* m_heading;
      Editor* m_editor;
      Subscription m_subscription;
      Subscription m_document_subscription;
      NodeId m_current;
      NodeId m_scope;
      bool m_is_synchronizing;
      CurrentHandler m_current_handler;
      CurrentHandler m_activation_handler;

      void synchronize();
      void on_selection();
      void on_current(const QModelIndex& index);
      void update_scope();
      void show_artboards();
  };
}

#endif
