#ifndef RELLU_UI_PROPERTY_INSPECTOR_HPP
#define RELLU_UI_PROPERTY_INSPECTOR_HPP
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QWidget>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"

namespace Rellu {

  /**
   * Shows the properties a document's inspected nodes share, kept up to date
   * as the document changes. With an editor, it inspects the editor's
   * selection and edits the properties that have a property editor through
   * commands, each field's successive entries merging into one undoable
   * step until the field is left.
   */
  class PropertyInspector : public QWidget {
    public:

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

      /** Returns the properties shown. */
      const std::vector<PropertyDescriptor>& get_properties() const;

      /**
       * Returns the widget a property's value is entered in.
       * @param path The property's path.
       * @return The widget, or <code>nullptr</code> if the property is not
       *         shown as editable.
       */
      QWidget* find_field(std::string_view path) const;

      /** Returns the tree listing the properties. */
      QTreeWidget& get_view();

    private:
      struct Field;
      Document* m_document;
      Editor* m_editor;
      Subscription m_subscription;
      Subscription m_selection_subscription;
      PropertyEditors m_property_editors;
      TextMeasurer m_measurer;
      std::vector<NodeId> m_nodes;
      NodeId m_current;
      std::vector<NodeId> m_sources;
      std::vector<PropertyDescriptor> m_properties;
      std::unordered_set<std::string> m_collapsed;
      std::unordered_map<std::string, QTreeWidgetItem*> m_items;
      std::unordered_map<std::string, std::unique_ptr<Field>> m_fields;
      std::string m_edited_path;
      int m_generation;
      bool m_is_committing;
      bool m_is_rebuild_pending;
      QStackedWidget* m_stack;
      QTreeWidget* m_view;

      void attach(Document* document);
      void refresh();
      void rebuild();
      void update_rows();
      void update_row(const PropertyDescriptor& property);
      void add(QTreeWidgetItem& parent, const PropertyDescriptor& property);
      std::unique_ptr<Field> make_field(const PropertyDescriptor& property,
        const PropertyEditor& editor);
      void commit(
        int generation, const std::string& path, const PropertyValue& value);
      void finish(int generation, const std::string& path);
      bool is_affected(const ChangeSet& changes) const;
      void on_change(const ChangeSet& changes);
  };
}

#endif
