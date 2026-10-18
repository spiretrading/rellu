#ifndef RELLU_MODEL_TRANSACTION_HPP
#define RELLU_MODEL_TRANSACTION_HPP
#include <cstddef>
#include <unordered_set>
#include <vector>
#include "Rellu/Model/Component.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Prototype.hpp"
#include "Rellu/Model/Resources.hpp"

namespace Rellu {

  /**
   * A group of changes to a Document that is committed as a whole. Changes
   * apply to the document as they are made, so the transaction's owner reads
   * them back through the document, but they are published to observers and
   * recorded for undo only on commit. A transaction destroyed without being
   * committed reverts every change it made. Only one transaction may be open
   * on a document at a time.
   *
   * An operation that fails validation throws std::invalid_argument and
   * leaves the document unchanged; an operation on a transaction that is no
   * longer open throws std::logic_error.
   */
  class Transaction {
    public:

      /**
       * Opens a transaction, throwing std::logic_error if the document
       * already has one open.
       * @param document The document to change.
       */
      explicit Transaction(Document& document);

      /** Reverts every change if the transaction was not committed. */
      ~Transaction();

      /**
       * Inserts a node.
       * @param parent The parent's id, or nil to insert onto the canvas.
       * @param index The position among the parent's children.
       * @param header The node's header, whose id is generated if nil.
       * @param payload The node's payload.
       * @return The node's id.
       */
      NodeId insert(const NodeId& parent, std::size_t index, NodeHeader header,
        Payload payload);

      /**
       * Inserts a node as the topmost child of a parent.
       * @param parent The parent's id, or nil to insert onto the canvas.
       * @param header The node's header, whose id is generated if nil.
       * @param payload The node's payload.
       * @return The node's id.
       */
      NodeId append(const NodeId& parent, NodeHeader header, Payload payload);

      /**
       * Inserts a node without a parent that is not on the canvas, to serve
       * as the root of a component's master or state tree.
       * @param header The node's header, whose id is generated if nil.
       * @param payload The node's payload.
       * @return The node's id.
       */
      NodeId insert_root(NodeHeader header, Payload payload);

      /**
       * Removes a node and its descendants, along with the interactions
       * they own, any flow starting on them and the state trees of the
       * component instances among them.
       * @param id The node's id.
       */
      void remove(const NodeId& id);

      /**
       * Reparents or reorders a node.
       * @param id The node's id.
       * @param parent The new parent's id, or nil for the canvas.
       * @param index The position among the new parent's children once the
       *        node has left its current position.
       */
      void move(const NodeId& id, const NodeId& parent, std::size_t index);

      /**
       * Replaces the header of the node with the header's id.
       * @param header The new header.
       */
      void replace_header(NodeHeader header);

      /**
       * Replaces a node's payload with one of the same kind. A text node's
       * stored layout is discarded when its content, frame, character or
       * paragraph styles change and the new payload keeps the old layout.
       * @param id The node's id.
       * @param payload The new payload.
       */
      void replace_payload(const NodeId& id, Payload payload);

      /**
       * Adds a component whose master and state trees are unused roots.
       * @param component The component, whose id is generated if nil.
       * @return The component's id.
       */
      ComponentId add_component(Component component);

      /**
       * Replaces the component with the same id.
       * @param component The new component.
       */
      void replace_component(Component component);

      /**
       * Removes a component along with its master and state trees.
       * @param id The component's id.
       */
      void remove_component(const ComponentId& id);

      /**
       * Adds an image.
       * @param image The image, whose id is generated if nil.
       * @return The image's id.
       */
      ImageId add_image(ImageResource image);

      /**
       * Removes an image.
       * @param id The image's id.
       */
      void remove_image(const ImageId& id);

      /**
       * Appends a color swatch.
       * @param swatch The swatch, whose id is generated if nil.
       * @return The swatch's id.
       */
      SwatchId add_swatch(ColorSwatch swatch);

      /**
       * Replaces the color swatch with the same id.
       * @param swatch The new swatch.
       */
      void replace_swatch(ColorSwatch swatch);

      /**
       * Removes a color swatch.
       * @param id The swatch's id.
       */
      void remove_swatch(const SwatchId& id);

      /**
       * Appends a character style.
       * @param style The style, whose id is generated if nil.
       * @return The style's id.
       */
      CharacterStyleId add_character_style(CharacterStyle style);

      /**
       * Replaces the character style with the same id.
       * @param style The new style.
       */
      void replace_character_style(CharacterStyle style);

      /**
       * Removes a character style.
       * @param id The style's id.
       */
      void remove_character_style(const CharacterStyleId& id);

      /**
       * Appends an interaction owned by an existing node.
       * @param interaction The interaction, whose id is generated if nil.
       * @return The interaction's id.
       */
      InteractionId add_interaction(Interaction interaction);

      /**
       * Replaces the interaction with the same id.
       * @param interaction The new interaction.
       */
      void replace_interaction(Interaction interaction);

      /**
       * Removes an interaction.
       * @param id The interaction's id.
       */
      void remove_interaction(const InteractionId& id);

      /**
       * Sets the artboard the prototype starts on.
       * @param artboard The artboard's id, or nil to unset it.
       */
      void set_home_artboard(const NodeId& artboard);

      /**
       * Replaces the prototype's flows.
       * @param flows The flows, each starting on an artboard.
       */
      void set_flows(std::vector<Flow> flows);

      /**
       * Commits the changes, recording them for undo and publishing them to
       * the document's observers if there are any.
       */
      void commit();

    private:
      Document* m_document;
      Document::Revision m_revision;
      std::unordered_set<NodeId> m_touched;
      std::vector<NodeId> m_moved;
      std::unordered_set<NodeId> m_touched_components;
      std::unordered_set<NodeId> m_touched_images;
      bool m_is_open;

      Transaction(const Transaction&) = delete;
      Transaction& operator =(const Transaction&) = delete;
      void check_open() const;
      void touch(const NodeId& id);
      void touch_canvas();
      void touch_component(const ComponentId& id);
      void touch_image(const ImageId& id);
      void touch_swatches();
      void touch_character_styles();
      void touch_interactions();
      void touch_flows();
      std::vector<NodeId>& edit_children(const NodeId& parent);
      bool is_used_tree(const NodeId& root, const ComponentId& owner) const;
      void validate_node(const NodeHeader& header, const Payload& payload,
        const NodeId& parent) const;
      void validate_component(
        const Component& component, const ComponentId& owner) const;
      void remove_subtree(const NodeId& id);
      ChangeSet make_change_set() const;
  };
}

#endif
