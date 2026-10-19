#ifndef RELLU_COMMANDS_COMPONENTS_HPP
#define RELLU_COMMANDS_COMPONENTS_HPP
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Group.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu {

  /**
   * Returns the component instance role of a node.
   * @param node The node.
   * @return The role, or <code>nullptr</code> if the node is not a component
   *         instance.
   */
  const ComponentInstance* get_instance_role(const Node& node);

  /**
   * Finds the component instance a node belongs to.
   * @param document The document holding the node.
   * @param id The node's id.
   * @return The nearest of the node and its ancestors that is a component
   *         instance, or nil if there is none.
   */
  NodeId find_instance(const Document& document, const NodeId& id);

  /**
   * Finds the instances of a component, its main component among them.
   * @param document The document holding the component.
   * @param component The component's id.
   * @return The instances on the canvas, in component trees and nested in
   *         other instances, in no particular order.
   */
  std::vector<NodeId> find_instances(
    const Document& document, const ComponentId& component);

  /**
   * Finds a component's main component.
   * @param document The document holding the component.
   * @param component The component's id.
   * @return The instance marked as the main component, or nil if there is
   *         none.
   */
  NodeId find_main_component(
    const Document& document, const ComponentId& component);

  /**
   * Returns the root of the tree a component instance mirrors: the master,
   * or the tree of its active state.
   * @param document The document holding the instance.
   * @param instance The instance role.
   * @return The tree's root, or nil if the component is unknown.
   */
  NodeId get_source_tree(
    const Document& document, const ComponentInstance& instance);

  /**
   * Tests whether a component is linked from another document or library.
   * @param document The document holding the component.
   * @param component The component's id.
   */
  bool is_linked(const Document& document, const ComponentId& component);

  /**
   * Lists the properties in which a node differs from the node it mirrors,
   * under the names Adobe XD records for overridden properties.
   * @param document The document holding both nodes.
   * @param node The node, such as a node of a component instance.
   * @param source The node it mirrors, such as a node of the master.
   * @return The names of the properties that differ.
   */
  std::vector<std::string> find_differences(
    const Document& document, const NodeId& node, const NodeId& source);

  /**
   * Copies the named properties of one node onto another.
   * @param node The node to change.
   * @param source The node whose properties are copied.
   * @param names The properties, as Adobe XD names overridden properties.
   * @return <code>true</code> iff the <i>node</i> changed.
   */
  bool copy_properties(
    Node& node, const Node& source, const std::vector<std::string>& names);

  /**
   * Copies the properties of one node onto another except the named ones.
   * @param node The node to change.
   * @param source The node whose properties are copied.
   * @param is_root Whether the node is the root of a component instance,
   *        whose name and transform are its own.
   * @param names The properties kept, as Adobe XD names overridden
   *        properties.
   * @return <code>true</code> iff the <i>node</i> changed.
   */
  bool update_properties(Node& node, const Node& source, bool is_root,
    const std::vector<std::string>& names);

  /**
   * Copies a node and its descendants with new ids into a parent.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The node's id.
   * @param parent The parent of the copy, or nil for the canvas.
   * @param index The copy's position among the parent's children.
   * @param mapping The map to which each copied id is added with the id of
   *        its copy.
   * @return The id of the copy.
   */
  NodeId clone_subtree(const Document& document, Transaction& transaction,
    const NodeId& id, const NodeId& parent, std::size_t index,
    std::unordered_map<NodeId, NodeId>& mapping);

  /**
   * Copies a node and its descendants with new ids as the root of an
   * unused tree, such as a component state's tree.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The node's id.
   * @param mapping The map to which each copied id is added with the id of
   *        its copy.
   * @return The id of the copy.
   */
  NodeId clone_tree(const Document& document, Transaction& transaction,
    const NodeId& id, std::unordered_map<NodeId, NodeId>& mapping);

  /**
   * Replaces a reference inside a component instance with an editable copy
   * of what it renders, keeping its id.
   * @param document The document holding the reference.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The reference's id.
   * @return The ids of the children added to the copy.
   */
  std::vector<NodeId> materialize_reference(
    const Document& document, Transaction& transaction, const NodeId& id);

  /**
   * Replaces the references inside component instances among nodes and their
   * descendants with editable copies.
   * @param document The document holding the nodes.
   * @param transaction The open transaction on the <i>document</i>.
   * @param nodes The roots of the subtrees.
   */
  void materialize_references(const Document& document,
    Transaction& transaction, const std::vector<NodeId>& nodes);

  /**
   * Returns the header a node renders with, which for a reference is its
   * source's.
   * @param document The document holding the node.
   * @param id The node's id.
   * @return The header, or <code>nullptr</code> if the node is unknown or
   *         is a reference that does not resolve.
   */
  const NodeHeader* find_rendered_header(
    const Document& document, const NodeId& id);

  /**
   * Edits a node's header as it renders. A reference inside a component
   * instance that the edit changes is first replaced with an editable copy,
   * which Adobe XD writes in full, while its children remain references.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The node's id.
   * @param edit The function editing the header, given the node as it
   *        renders and the header to edit.
   * @return <code>true</code> iff the header changed.
   */
  bool edit_rendered_header(const Document& document, Transaction& transaction,
    const NodeId& id,
    const std::function<void (const Node&, NodeHeader&)>& edit);

  /**
   * Tests whether a node is a reference inside a component instance that
   * renders text.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  bool is_text_reference(const Document& document, const NodeId& id);

  /**
   * Prepares a command to edit the references inside component instances
   * among its targets, by making them editable copies in the same step.
   * @param document The document the command applies to.
   * @param targets The nodes the command edits.
   * @param command The command.
   * @return The command to apply in its place.
   */
  std::unique_ptr<Command> make_instance_edit(const Document& document,
    const std::vector<NodeId>& targets, std::unique_ptr<Command> command);

  /**
   * Returns the command a command prepared by make_instance_edit applies.
   * @param command The command, or <code>nullptr</code>.
   * @return The command applied after materialization, the <i>command</i>
   *         itself if it was not prepared.
   */
  const Command* get_edited_command(const Command* command);

  /**
   * Inserts an instance of a component referring to the master's children.
   * @param document The document holding the component.
   * @param transaction The open transaction on the <i>document</i>.
   * @param component The component's id.
   * @param parent The parent, or nil for the canvas.
   * @param index The position among the parent's children.
   * @param transform The instance's transform.
   * @param measurer The function measuring text, which sizes the instance.
   * @return The instance's id.
   */
  NodeId insert_instance(const Document& document, Transaction& transaction,
    const ComponentId& component, const NodeId& parent, std::size_t index,
    const Matrix& transform,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Returns the name Adobe XD gives a new instance of a component: the
   * component's name followed by an en dash and the next number when the
   * component's instances are numbered so, otherwise the component's name.
   * @param document The document holding the component.
   * @param component The component's id.
   */
  std::string make_instance_name(
    const Document& document, const ComponentId& component);

  /**
   * Returns the id Adobe XD's files give a node's copy of an interaction it
   * inherits, such as a component instance's copy of an interaction of its
   * master's root.
   * @param source The inherited interaction's id.
   * @param owner The inheriting node's id.
   */
  InteractionId derive_inherited_id(
    const InteractionId& source, const NodeId& owner);

  /**
   * Propagates the changes of the most recent revision through components
   * as Adobe XD does, as part of that revision: edits of a main component
   * reach the component and its instances' properties that are not
   * overridden, edits of other instances become overrides, and every
   * instance inherits the interactions of the root of the tree it mirrors,
   * those of a main component's root belonging to the component.
   * @param document The document changed.
   * @param changes The changes the most recent revision made, to which the
   *        changes made here are appended.
   * @return <code>true</code> iff anything changed.
   */
  bool refresh_components(Document& document, ChangeSet& changes);

  /**
   * Updates the sizes Adobe XD records for the masters and component
   * instances containing the nodes the most recent revision changed, as part
   * of that revision.
   * @param document The document changed.
   * @param changes The changes the most recent revision made.
   * @param measurer The function measuring text.
   * @return <code>true</code> iff a size changed.
   */
  bool refresh_component_sizes(Document& document, const ChangeSet& changes,
    const TextMeasurer& measurer);
}

#endif
