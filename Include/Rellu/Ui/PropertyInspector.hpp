#ifndef RELLU_UI_PROPERTY_INSPECTOR_HPP
#define RELLU_UI_PROPERTY_INSPECTOR_HPP
#include <string>
#include <unordered_set>
#include <vector>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QWidget>
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"

namespace Rellu {

  /**
   * Shows the properties of a document's current node, kept up to date as
   * the document changes.
   */
  class PropertyInspector : public QWidget {
    public:

      /**
       * Constructs a PropertyInspector without a document.
       * @param parent The parent widget.
       */
      explicit PropertyInspector(QWidget* parent = nullptr);

      /**
       * Sets the document whose nodes are inspected, clearing the current
       * node.
       * @param document The document, or <code>nullptr</code> for none. It
       *        must outlive this inspector or be replaced first.
       */
      void set_document(Document* document);

      /** Returns the id of the node inspected, or nil if there is none. */
      const NodeId& get_current() const;

      /**
       * Inspects a node.
       * @param id The node's id, or nil to inspect nothing.
       */
      void set_current(const NodeId& id);

      /** Returns the properties shown. */
      const std::vector<PropertyDescriptor>& get_properties() const;

      /** Returns the tree listing the properties. */
      QTreeWidget& get_view();

    private:
      Document* m_document;
      Subscription m_subscription;
      NodeId m_current;
      NodeId m_source;
      std::vector<PropertyDescriptor> m_properties;
      std::unordered_set<std::string> m_collapsed;
      QStackedWidget* m_stack;
      QTreeWidget* m_view;

      void refresh();
      void add(QTreeWidgetItem& parent, const PropertyDescriptor& property);
      void on_change(const ChangeSet& changes);
  };
}

#endif
