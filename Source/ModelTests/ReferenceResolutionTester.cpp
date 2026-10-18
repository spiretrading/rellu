#include <doctest/doctest.h>
#include "Rellu/Model/ReferenceResolution.hpp"
#include "SampleDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeHeader make_header(std::string name, const Matrix& transform) {
    auto header = Rellu::Tests::make_header(std::move(name));
    header.m_transform = transform;
    return header;
  }

  Reference make_reference(const NodeId& source,
      std::optional<Matrix> transform = std::nullopt,
      bool has_children = false) {
    return Reference(source, transform, has_children);
  }
}

TEST_SUITE("ReferenceResolution") {
  TEST_CASE("resolution") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto master = transaction.insert_root(make_header("Master"),
      Group());
    auto shape = transaction.append(master,
      make_header("Shape", make_translation(1, 2)),
      make_rectangle_shape(5, 5));
    auto group = transaction.append(master,
      make_header("Group", make_translation(3, 4)), Group());
    auto inner = transaction.append(group, make_header("Inner"),
      make_rectangle_shape(1, 1));
    auto instance = transaction.append(NodeId(),
      make_header("Instance"), Group());
    auto plain = transaction.append(instance,
      make_header("Plain"), make_reference(shape));
    auto moved = transaction.append(instance, make_header("Moved"),
      make_reference(shape, make_translation(9, 9)));
    auto own = transaction.append(instance, make_header("Own"),
      make_reference(group, std::nullopt, true));
    auto own_child = transaction.append(own, make_header("Own Child"),
      make_reference(inner));
    auto inherited = transaction.append(instance,
      make_header("Inherited"), make_reference(group));
    transaction.commit();
    auto& nodes = document.get_nodes();
    REQUIRE(resolve_reference(nodes, plain) ==
      ResolvedReference(shape, make_translation(1, 2), shape));
    REQUIRE(resolve_reference(nodes, moved) ==
      ResolvedReference(shape, make_translation(9, 9), shape));
    REQUIRE(resolve_reference(nodes, own) ==
      ResolvedReference(group, make_translation(3, 4), own));
    REQUIRE(document.get_children(own) == std::vector<NodeId>({own_child}));
    REQUIRE(resolve_reference(nodes, own_child) ==
      ResolvedReference(inner, Matrix(), inner));
    REQUIRE(resolve_reference(nodes, inherited) ==
      ResolvedReference(group, make_translation(3, 4), group));
  }

  TEST_CASE("chain") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto master = transaction.insert_root(make_header("Master"),
      Group());
    auto shape = transaction.append(master,
      make_header("Shape", make_translation(1, 1)),
      make_rectangle_shape(5, 5));
    auto group = transaction.append(master, make_header("Group"),
      Group());
    auto outer_master = transaction.insert_root(
      make_header("Outer Master"), Group());
    auto nested = transaction.append(outer_master,
      make_header("Nested"),
      make_reference(shape, make_translation(2, 2)));
    auto nested_group = transaction.append(outer_master,
      make_header("Nested Group"),
      make_reference(group, std::nullopt, true));
    auto instance = transaction.append(NodeId(),
      make_header("Instance"), Group());
    auto plain = transaction.append(instance, make_header("Plain"),
      make_reference(nested));
    auto moved = transaction.append(instance, make_header("Moved"),
      make_reference(nested, make_translation(3, 3)));
    auto groups = transaction.append(instance, make_header("Groups"),
      make_reference(nested_group));
    transaction.commit();
    auto& nodes = document.get_nodes();
    REQUIRE(resolve_reference(nodes, plain) ==
      ResolvedReference(shape, make_translation(2, 2), shape));
    REQUIRE(resolve_reference(nodes, moved) ==
      ResolvedReference(shape, make_translation(3, 3), shape));
    REQUIRE(resolve_reference(nodes, groups) ==
      ResolvedReference(group, Matrix(), nested_group));
  }

  TEST_CASE("unresolved") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = transaction.append(NodeId(), make_header("Group"),
      Group());
    auto broken = transaction.append(group, make_header("Broken"),
      make_reference(generate_node_id()));
    auto first = generate_node_id();
    auto second = transaction.append(group, make_header("Second"),
      make_reference(first));
    auto header = make_header("First");
    header.m_id = first;
    transaction.append(group, header, make_reference(second));
    transaction.commit();
    auto& nodes = document.get_nodes();
    REQUIRE(!resolve_reference(nodes, group));
    REQUIRE(!resolve_reference(nodes, generate_node_id()));
    REQUIRE(!resolve_reference(nodes, broken));
    REQUIRE(!resolve_reference(nodes, first));
    REQUIRE(!resolve_reference(nodes, second));
  }
}
