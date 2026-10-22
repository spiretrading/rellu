#ifndef RELLU_UI_LAYERS_MODEL_HPP
#define RELLU_UI_LAYERS_MODEL_HPP
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <QAbstractItemModel>
#include <QMimeData>
#include <QString>
#include <QStringList>
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * Presents a document's canvas as a tree of layers, one row per node with
   * the topmost first, kept up to date as the document changes. As in Adobe
   * XD, while the canvas holds both artboards and other layers, the canvas
   * lists the artboards followed by a Pasteboard row listing the other
   * layers.
   */
  class LayersModel : public QAbstractItemModel {
    public:

      /** The id of the Pasteboard row. */
      static inline const auto PASTEBOARD = NodeId(std::to_array<std::uint8_t>(
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x4F, 0xFF, 0xBF, 0xFF, 0xFF,
          0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));

      /** Lists the columns. */
      enum Column {

        /** The node's name and kind. */
        NAME,

        /** Whether the node is visible. */
        VISIBILITY,

        /** Whether the node is locked. */
        LOCK,

        /** Whether the node is marked for export. */
        EXPORT
      };

      /** The number of columns. */
      static constexpr auto COLUMN_COUNT = 4;

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

        /** Whether the node is marked for export. */
        MARKED_ROLE,

        /** The node's id in canonical text form. */
        ID_ROLE,

        /**
         * The icon shown in place of the decoration while the row is
         * expanded, null if the decoration stays.
         */
        EXPANDED_ICON_ROLE
      };

      /**
       * The type of function called when the user renames a node.
       * @param id The node's id.
       * @param name The name entered.
       */
      using RenameHandler =
        std::function<void (const NodeId& id, const QString& name)>;

      /**
       * The type of function called when the user drags layers to a new
       * place.
       * @param nodes The nodes moved, in z order.
       * @param parent The new parent's id, or nil for the canvas.
       * @param index The position among the parent's other children, in z
       *        order, at which the nodes are placed.
       * @return <code>true</code> iff the nodes moved.
       */
      using MoveHandler = std::function<bool (const std::vector<NodeId>& nodes,
        const NodeId& parent, std::size_t index)>;

      /** The MIME type of layers dragged within the panel. */
      static inline const auto MIME_TYPE =
        QString("application/x-rellu-layers");

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
       * Sets the function called when the user drags layers to a new place,
       * the layers being draggable only while there is one.
       * @param handler The function to call, or empty to disable dragging.
       */
      void set_move_handler(MoveHandler handler);

      /**
       * Tests whether layers may be placed in a parent, as Adobe XD allows:
       * artboards only on the canvas, other layers on the canvas, the
       * Pasteboard row, in artboards or in plain groups, and never in
       * themselves.
       * @param nodes The nodes moved.
       * @param parent The parent's id, nil for the canvas or PASTEBOARD.
       */
      bool is_movable(
        const std::vector<NodeId>& nodes, const NodeId& parent) const;

      /**
       * Returns the id of the node at an index.
       * @param index The index.
       * @return The node's id, PASTEBOARD for the Pasteboard row, or nil for
       *         an invalid index.
       */
      NodeId get_id(const QModelIndex& index) const;

      /**
       * Returns the index of a node on the canvas.
       * @param id The node's id, or PASTEBOARD for the Pasteboard row.
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
      Qt::DropActions supportedDropActions() const override;
      QStringList mimeTypes() const override;
      QMimeData* mimeData(const QModelIndexList& indexes) const override;
      bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
        int row, int column, const QModelIndex& parent) const override;
      bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row,
        int column, const QModelIndex& parent) override;

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
      MoveHandler m_move_handler;
      Subscription m_subscription;
      std::unique_ptr<Item> m_root;
      mutable std::unordered_map<NodeId, std::unique_ptr<Item>> m_items;

      LayersModel(const LayersModel&) = delete;
      LayersModel& operator =(const LayersModel&) = delete;
      Item* get_item(const QModelIndex& index) const;
      Item* find_item(const NodeId& id) const;
      bool has_pasteboard() const;
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
