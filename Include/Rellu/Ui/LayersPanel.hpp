#ifndef RELLU_UI_LAYERS_PANEL_HPP
#define RELLU_UI_LAYERS_PANEL_HPP
#include <cstddef>
#include <functional>
#include <unordered_set>
#include <vector>
#include <QAbstractProxyModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QToolButton>
#include <QTreeView>
#include <QWidget>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/LayersModel.hpp"

namespace Rellu {

  /** Lists the kinds of layer the Layers panel's filter lists. */
  enum class LayerFilter : unsigned char {

    /** Every layer. */
    ALL,

    /** Layers filled with an image. */
    IMAGES,

    /**
     * Rectangles, ellipses, polygons, lines and paths, other than images:
     * rectangles filled with an image at a locked aspect ratio.
     */
    SHAPES,

    /** Text. */
    TEXT,

    /** Videos. */
    VIDEO
  };

  /**
   * The Layers panel, listing a document's layers as Adobe XD does: the
   * artboards and the Pasteboard row while the current layer is not inside
   * an artboard or on the pasteboard, otherwise the layers of the artboard
   * holding the current layer under a heading naming it, the layers on the
   * pasteboard while the current layer is one of them or the Pasteboard row
   * was double-clicked, or, while searching, every layer matching the
   * search and filter, flat under a heading naming its artboard, the
   * pasteboard or the canvas, a selection coming to include a layer outside
   * the results ending the search. Clicking a container's icon in the tree
   * selects it and expands or collapses it. With an editor, the layers
   * selected are the editor's selection, the current layer being its
   * primary node, layers are renamed in place, dragged to reorder and
   * reparent them, shown, hidden, locked and unlocked with the buttons
   * appearing on hover, and act through a context menu.
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
       * Returns the list shown in place of the tree view while searching,
       * listing the matching layers of each artboard, the pasteboard or the
       * canvas under a heading naming it.
       */
      QTreeView& get_results();

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
       * Returns the artboard whose layers are listed, LayersModel::PASTEBOARD
       * while the layers on the pasteboard are listed, or nil while the
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

      /**
       * Sets the registry whose actions the context menu lists.
       * @param actions The registry, or <code>nullptr</code> for none. It
       *        must outlive this panel or be replaced first.
       */
      void set_actions(ActionRegistry* actions);

      /** Returns the text layers are searched for. */
      QString get_search() const;

      /**
       * Lists only the layers whose names contain a text, ignoring case,
       * along with their ancestors, searching the whole document.
       * @param text The text, or empty to list every layer.
       */
      void set_search(const QString& text);

      /** Returns the kind of layer listed. */
      LayerFilter get_filter() const;

      /**
       * Lists only the layers of a kind, along with their ancestors.
       * @param filter The kind of layer listed.
       */
      void set_filter(LayerFilter filter);

      /**
       * Returns <code>true</code> iff a layer's row is hidden by the search
       * or filter.
       * @param id The layer's id.
       */
      bool is_filtered_out(const NodeId& id);

      /**
       * Shows or hides a layer, as its eye button does.
       * @param id The layer's id.
       * @return <code>true</code> iff the layer changed.
       */
      bool toggle_visibility(const NodeId& id);

      /**
       * Locks or unlocks a layer, as its lock button does.
       * @param id The layer's id.
       * @return <code>true</code> iff the layer changed.
       */
      bool toggle_lock(const NodeId& id);

      /**
       * Marks or unmarks a layer for export, as its Mark for Export button
       * does.
       * @param id The layer's id.
       * @return <code>true</code> iff the layer changed.
       */
      bool toggle_export_mark(const NodeId& id);

      /**
       * Moves layers as dragging them in the panel does.
       * @param nodes The layers to move.
       * @param parent The new parent's id, or nil for the canvas.
       * @param index The position among the parent's other children, in z
       *        order.
       * @return <code>true</code> iff the layers moved.
       */
      bool move(const std::vector<NodeId>& nodes, const NodeId& parent,
        std::size_t index);

      /**
       * Makes the context menu of the selected layers.
       * @param parent The menu's parent.
       * @return The menu.
       */
      QMenu* make_context_menu(QWidget* parent);

    protected:
      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      LayersModel* m_model;
      QTreeView* m_view;
      QAbstractProxyModel* m_results_model;
      QTreeView* m_results;
      QLineEdit* m_search;
      QToolButton* m_filter_button;
      QWidget* m_search_glyphs;
      QToolButton* m_back;
      QLabel* m_heading;
      Editor* m_editor;
      ActionRegistry* m_actions;
      LayerFilter m_filter;
      Subscription m_subscription;
      Subscription m_document_subscription;
      Subscription m_context_subscription;
      NodeId m_current;
      NodeId m_scope;
      bool m_is_synchronizing;
      bool m_is_filtered;
      bool m_is_pasteboard_open;
      bool m_is_results_stale;
      std::unordered_set<NodeId> m_matches;
      CurrentHandler m_current_handler;
      CurrentHandler m_activation_handler;

      void synchronize();
      void on_selection();
      void on_current(const QModelIndex& index);
      void update_scope();
      void show_artboards();
      bool is_searching() const;
      bool is_match(const Node& node) const;
      void collect_matches(
        const NodeId& id, std::vector<NodeId>& matches) const;
      void update_results();
      void select_results();
      void on_result_selection();
      void exit_search();
      void show_context_menu(QTreeView& view, const QPoint& position);
  };
}

#endif
