#ifndef RELLU_UI_LAYERS_MODEL_HPP
#define RELLU_UI_LAYERS_MODEL_HPP
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <QAbstractItemModel>
#include <QString>
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * Presents a document's canvas as a tree of layers, one row per node with
   * the topmost first, kept up to date as the document changes.
   */
  class LayersModel : public QAbstractItemModel {
    public:

      /** Lists the columns. */
      enum Column {

        /** The node's name and kind. */
        NAME,

        /** Whether the node is visible. */
        VISIBILITY,

        /** Whether the node is locked. */
        LOCK
      };

      /** The number of columns. */
      static constexpr auto COLUMN_COUNT = 3;

      /** Lists the item data roles beyond Qt's own, valid in any column. */
      enum Role {

        /** The NodeKind, as an int, of the node displayed. */
        KIND_ROLE = Qt::UserRole,

        /** Whether the node is a reference. */
        REFERENCE_ROLE,

        /** Whether the node is visible. */
        VISIBLE_ROLE,

        /** Whether the node is locked. */
        LOCKED_ROLE,

        /** The node's id in canonical text form. */
        ID_ROLE
      };

      /**
       * The type of function called when the user renames a node.
       * @param id The node's id.
       * @param name The name entered.
       */
      using RenameHandler =
        std::function<void (const NodeId& id, const QString& name)>;

      /**
       * Constructs a LayersModel without a document.
       * @param parent The parent object.
       */
      explicit LayersModel(QObject* parent = nullptr);

      ~LayersModel() override;

      /** Returns the document presented, or <code>nullptr</code> if none. */
      Document* get_document() const;

      /**
       * Presents a document.
       * @param document The document to present, or <code>nullptr</code>
       *        for none. It must outlive this model or be replaced first.
       */
      void set_document(Document* document);

      /**
       * Sets the function called when the user renames a node, the names
       * being editable only while there is one.
       * @param handler The function to call, or empty to make names
       *        read-only.
       */
      void set_rename_handler(RenameHandler handler);

      /**
       * Returns the id of the node at an index.
       * @param index The index.
       * @return The node's id, or nil for an invalid index.
       */
      NodeId get_id(const QModelIndex& index) const;

      /**
       * Returns the index of a node on the canvas.
       * @param id The node's id.
       * @param column The column.
       * @return The node's index, or an invalid index if the node is not on
       *         the canvas.
       */
      QModelIndex find(const NodeId& id, int column = NAME);

      QModelIndex index(int row, int column,
        const QModelIndex& parent = QModelIndex()) const override;
      QModelIndex parent(const QModelIndex& index) const override;
      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      int columnCount(
        const QModelIndex& parent = QModelIndex()) const override;
      bool hasChildren(
        const QModelIndex& parent = QModelIndex()) const override;
      QVariant data(const QModelIndex& index, int role) const override;
      QVariant headerData(
        int section, Qt::Orientation orientation, int role) const override;
      Qt::ItemFlags flags(const QModelIndex& index) const override;
      bool setData(
        const QModelIndex& index, const QVariant& value, int role) override;

    private:
      struct Item {
        NodeId m_id;
        Item* m_parent;
        int m_row;
        bool m_is_populated;
        std::vector<NodeId> m_child_ids;
        std::vector<Item*> m_children;
      };
      Document* m_document;
      RenameHandler m_rename_handler;
      Subscription m_subscription;
      std::unique_ptr<Item> m_root;
      mutable std::unordered_map<NodeId, std::unique_ptr<Item>> m_items;

      LayersModel(const LayersModel&) = delete;
      LayersModel& operator =(const LayersModel&) = delete;
      Item* get_item(const QModelIndex& index) const;
      Item* find_item(const NodeId& id) const;
      QModelIndex make_index(Item& item, int column) const;
      void populate(Item& item) const;
      std::vector<NodeId> get_rows(const Item& item) const;
      Item& get_child(Item& item, int row) const;
      void destroy(Item& item);
      void renumber(Item& item, int first);
      void remove_children(Item& item);
      void reorder_children(Item& item);
      void insert_children(Item& item);
      void on_change(const ChangeSet& changes);
  };
}

#endif
