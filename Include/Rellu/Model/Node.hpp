#ifndef RELLU_MODEL_NODE_HPP
#define RELLU_MODEL_NODE_HPP
#include <variant>
#include <vector>
#include "Rellu/Model/Artboard.hpp"
#include "Rellu/Model/Group.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Reference.hpp"
#include "Rellu/Model/Shape.hpp"
#include "Rellu/Model/Text.hpp"

namespace Rellu {

  /** Lists the kinds of node, in the order of the Payload alternatives. */
  enum class NodeKind : unsigned char {

    /** A top-level frame. */
    ARTBOARD,

    /** A container. */
    GROUP,

    /** A vector shape. */
    SHAPE,

    /** A block of text. */
    TEXT,

    /** A reference to a node inside a component. */
    REFERENCE
  };

  /** The kind-specific data of a node. */
  using Payload = std::variant<Artboard, Group, Shape, Text, Reference>;

  /**
   * Returns the kind of node a payload belongs to.
   * @param payload The payload.
   */
  NodeKind get_kind(const Payload& payload);

  /** An immutable record of a node within a NodeStore. */
  struct Node {

    /** The properties shared by every kind of node. */
    NodeHeader m_header;

    /** The kind-specific data. */
    Payload m_payload;

    /** The parent, or nil for a node without one. */
    NodeId m_parent;

    /** The children in z order, the first being the bottom-most. */
    std::vector<NodeId> m_children;

    /** Returns the node's identifier. */
    const NodeId& get_id() const;

    /** Returns the node's kind. */
    NodeKind get_kind() const;

    /** Returns the artboard payload, or <code>nullptr</code> if absent. */
    const Artboard* get_artboard() const;

    /** Returns the group payload, or <code>nullptr</code> if absent. */
    const Group* get_group() const;

    /** Returns the shape payload, or <code>nullptr</code> if absent. */
    const Shape* get_shape() const;

    /** Returns the text payload, or <code>nullptr</code> if absent. */
    const Text* get_text() const;

    /** Returns the reference payload, or <code>nullptr</code> if absent. */
    const Reference* get_reference() const;

    bool operator ==(const Node&) const = default;
  };

  inline NodeKind get_kind(const Payload& payload) {
    return static_cast<NodeKind>(payload.index());
  }

  inline const NodeId& Node::get_id() const {
    return m_header.m_id;
  }

  inline NodeKind Node::get_kind() const {
    return Rellu::get_kind(m_payload);
  }

  inline const Artboard* Node::get_artboard() const {
    return std::get_if<Artboard>(&m_payload);
  }

  inline const Group* Node::get_group() const {
    return std::get_if<Group>(&m_payload);
  }

  inline const Shape* Node::get_shape() const {
    return std::get_if<Shape>(&m_payload);
  }

  inline const Text* Node::get_text() const {
    return std::get_if<Text>(&m_payload);
  }

  inline const Reference* Node::get_reference() const {
    return std::get_if<Reference>(&m_payload);
  }
}

#endif
