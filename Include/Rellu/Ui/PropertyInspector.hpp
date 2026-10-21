#ifndef RELLU_UI_PROPERTY_INSPECTOR_HPP
#define RELLU_UI_PROPERTY_INSPECTOR_HPP
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/ColorPicker.hpp"
#include "Rellu/Ui/GridActions.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"

namespace Rellu {

  /**
   * The property inspector at the right of the window, showing the
   * properties a document's inspected nodes share in Adobe XD's sections,
   * kept up to date as the document changes. With an editor, it inspects
   * the editor's selection and edits the properties that have a property
   * editor through commands, each field's successive entries merging into
   * one undoable step until the field is left; the other properties are
   * shown read-only, and every field is disabled while an inspected node is
   * locked. The text section of text being edited in place follows its caret
   * and selection. Its swatches open Adobe XD's color picker, whose edits
   * merge into one undoable step until it closes. Its buttons trigger the
   * actions of an ActionRegistry.
   */
  class PropertyInspector : public QWidget {
    public:

      /** The width of the inspector in device independent pixels. */
      static constexpr auto WIDTH = 290;

      /**
       * Constructs a PropertyInspector without a document.
       * @param parent The parent widget.
       */
      explicit PropertyInspector(QWidget* parent = nullptr);

      ~PropertyInspector() override;

      /**
       * Sets the document whose nodes are inspected, without an editor,
       * clearing the inspected nodes.
       * @param document The document, or <code>nullptr</code> for none. It
       *        must outlive this inspector or be replaced first.
       */
      void set_document(Document* document);

      /** Returns the editor whose selection is edited, if any. */
      Editor* get_editor() const;

      /**
       * Edits the selection of an editor, following it as it changes.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this inspector or be replaced first.
       */
      void set_editor(Editor* editor);

      /**
       * Sets the editors of the properties that can be edited.
       * @param editors The property editors.
       */
      void set_property_editors(PropertyEditors editors);

      /**
       * Sets the function measuring text.
       * @param measurer The function measuring text.
       */
      void set_measurer(TextMeasurer measurer);

      /**
       * Sets the registry whose actions the inspector's buttons trigger,
       * such as object.align.left, each button enabled while its action is.
       * @param actions The registry, or <code>nullptr</code> to disable the
       *        buttons. It must outlive this inspector or be replaced first.
       */
      void set_actions(ActionRegistry* actions);

      /**
       * Sets the grid settings the grid section shows and changes, its
       * checkbox toggling the kind of grid offered and its type showing only
       * the kind chosen.
       * @param actions The grid settings, or <code>nullptr</code> to show no
       *        grid as editable. They must outlive this inspector or be
       *        replaced first.
       */
      void set_grid_actions(GridActions* actions);

      /**
       * Shows the inspector of a workspace.
       * @param workspace The workspace.
       */
      void set_workspace(Workspace workspace);

      /**
       * Shows a widget below the sections of the design workspace, taking
       * ownership of it.
       * @param footer The widget, or <code>nullptr</code> for none.
       * @param is_exclusive Whether the sections are hidden while it shows.
       */
      void set_footer(QWidget* footer, bool is_exclusive);

      /**
       * Returns the id of the primary node inspected, or nil if there is
       * none.
       */
      const NodeId& get_current() const;

      /**
       * Inspects a single node.
       * @param id The node's id, or nil to inspect nothing.
       */
      void set_current(const NodeId& id);

      /** Returns the nodes inspected. */
      const std::vector<NodeId>& get_nodes() const;

      /**
       * Inspects nodes.
       * @param nodes The nodes, the last being the primary node.
       */
      void set_nodes(std::vector<NodeId> nodes);

      /** Returns the properties of the inspected nodes. */
      const std::vector<PropertyDescriptor>& get_properties() const;

      /** Returns the paths of the sections shown, from top to bottom. */
      std::vector<std::string> get_sections() const;

      /**
       * Returns the widget showing a section.
       * @param path The section's path, such as transform.
       * @return The widget, or <code>nullptr</code> if the section is not
       *         shown.
       */
      QWidget* find_section(std::string_view path) const;

      /**
       * Returns the widget a property's value is entered in.
       * @param path The property's path.
       * @return The widget, or <code>nullptr</code> if the property is not
       *         shown as editable.
       */
      QWidget* find_field(std::string_view path) const;

      /**
       * Returns the widget showing a property's value, whether or not it is
       * editable.
       * @param path The property's path.
       * @return The widget, or <code>nullptr</code> if the property is not
       *         shown.
       */
      QWidget* find_display(std::string_view path) const;

      /** Returns the color picker the swatches open. */
      ColorPicker& get_color_picker();

      /**
       * Returns the path of the property the color picker edits, or empty
       * if it is closed.
       */
      const std::string& get_picked_path() const;

      /**
       * Opens the color picker editing a property, as clicking its swatch
       * does.
       * @param path The property's path, such as fill.paint or stroke.color.
       * @return <code>true</code> iff the property is shown as editable.
       */
      bool open_color_picker(std::string_view path);

      /**
       * Returns <code>true</code> iff the fields are disabled because an
       * inspected node is locked.
       */
      bool is_locked() const;

    private:
      struct Field;
      Document* m_document;
      Editor* m_editor;
      ActionRegistry* m_actions;
      GridActions* m_grid_actions;
      Workspace m_workspace;
      Subscription m_subscription;
      Subscription m_selection_subscription;
      Subscription m_text_selection_subscription;
      Subscription m_history_subscription;
      Subscription m_grid_subscription;
      PropertyEditors m_property_editors;
      TextMeasurer m_measurer;
      std::vector<NodeId> m_nodes;
      NodeId m_current;
      std::vector<NodeId> m_sources;
      std::vector<PropertyDescriptor> m_properties;
      std::vector<std::pair<std::string, QWidget*>> m_sections;
      std::vector<std::unique_ptr<Field>> m_fields;
      std::string m_edited_path;
      std::string m_picked_path;
      int m_generation;
      bool m_is_committing;
      bool m_is_rebuild_pending;
      bool m_is_locked;
      bool m_is_corner_split;
      QScrollArea* m_scroll;
      QWidget* m_content;
      QVBoxLayout* m_layout;
      ColorPicker* m_picker;
      QWidget* m_footer;
      bool m_is_footer_exclusive;

      void attach(Document* document);
      void give_initial_grids();
      void show_grid_settings(
        std::vector<PropertyDescriptor>& properties) const;
      void refresh();
      void rebuild();
      void update_fields();
      void commit(
        int generation, const std::string& path, const PropertyValue& value);
      void finish(int generation, const std::string& path);
      void apply(const std::string& path, const PropertyValue& value);
      void seal(const std::string& path);
      void open_picker(const QRect& anchor, const std::string& path);
      void add_swatch(const Fill& fill);
      bool find_lock() const;
      bool is_affected(const ChangeSet& changes) const;
      void on_change(const ChangeSet& changes);
  };
}

#endif
