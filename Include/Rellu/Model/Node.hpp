#ifndef RELLU_MODEL_NODE_HPP
#define RELLU_MODEL_NODE_HPP
#include <variant>
#include <vector>
#include "Rellu/Model/Artboard.hpp"
#include "Rellu/Model/Box.hpp"
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

  /**
   * The kind-specific data of a node, keeping the larger kinds in boxes so
   * that every node record stays small.
   */
  using Payload =
    std::variant<Box<Artboard>, Group, Box<Shape>, Box<Text>, Reference>;

  /**
   * Returns the kind of node a payload belongs to.
   * @param payload The payload.
   */
  NodeKind get_kind(const Payload& payload);

  /**
   * Tests whether a node may hold a child of a given kind.
   * @param parent The payload of the parent.
   * @param kind The kind of the child.
   */
  bool can_contain(const Payload& parent, NodeKind kind);

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

  inline bool can_contain(const Payload& parent, NodeKind kind) {
    if(kind == NodeKind::ARTBOARD) {
      return false;
    }
    if(auto shape = std::get_if<Box<Shape>>(&parent)) {
      return (kind == NodeKind::SHAPE || kind == NodeKind::GROUP ||
        kind == NodeKind::REFERENCE) &&
        std::holds_alternative<CompoundShape>((*shape)->m_geometry);
    } else if(auto reference = std::get_if<Reference>(&parent)) {
      return reference->m_has_children;
    }
    return !std::holds_alternative<Box<Text>>(parent);
  }

  inline const NodeId& Node::get_id() const {
    return m_header.m_id;
  }

  inline NodeKind Node::get_kind() const {
    return Rellu::get_kind(m_payload);
  }

  inline const Artboard* Node::get_artboard() const {
    if(auto artboard = std::get_if<Box<Artboard>>(&m_payload)) {
      return &**artboard;
    }
    return nullptr;
  }

  inline const Group* Node::get_group() const {
    return std::get_if<Group>(&m_payload);
  }

  inline const Shape* Node::get_shape() const {
    if(auto shape = std::get_if<Box<Shape>>(&m_payload)) {
      return &**shape;
    }
    return nullptr;
  }

  inline const Text* Node::get_text() const {
    if(auto text = std::get_if<Box<Text>>(&m_payload)) {
      return &**text;
    }
    return nullptr;
  }

  inline const Reference* Node::get_reference() const {
    return std::get_if<Reference>(&m_payload);
  }
}

#endif
