#ifndef RELLU_COMMANDS_CREATION_COMMANDS_HPP
#define RELLU_COMMANDS_CREATION_COMMANDS_HPP
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /**
   * Returns the style Adobe XD gives a rectangle, ellipse or polygon it
   * draws: a white fill and a 1 pixel #707070 inner border.
   */
  Style make_default_style();

  /**
   * Returns the style Adobe XD gives a line it draws: no fill and a 1 pixel
   * #707070 centered border.
   */
  Style make_default_line_style();

  /**
   * Names a new node as Adobe XD does: a prefix followed by one more than
   * the largest number following that prefix in the name of any node.
   * @param document The document the node is added to.
   * @param prefix The prefix, such as "Rectangle ".
   */
  std::string make_node_name(const Document& document, std::string_view prefix);

  /**
   * Inserts a node as the topmost child of a parent and selects it. Applied
   * to a document already holding the node, it replaces the node's header
   * and payload instead, so that a node is reshaped while it is drawn by
   * merging commands inserting it.
   */
  class InsertNodeCommand : public Command {
    public:

      /**
       * Constructs an InsertNodeCommand.
       * @param label The label naming the operation, such as Rectangle.
       * @param parent The parent, or nil for the canvas.
       * @param header The node's header, whose id is generated if nil.
       * @param payload The node's payload.
       */
      InsertNodeCommand(std::string label, const NodeId& parent,
        NodeHeader header, Payload payload);

      /** Returns the node inserted. */
      const NodeId& get_node() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the header and payload of an InsertNodeCommand inserting the
       * same node into the same parent.
       */
      bool merge(const Command& command) override;

      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::string m_label;
      NodeId m_parent;
      NodeHeader m_header;
      Payload m_payload;
  };
}

#endif
