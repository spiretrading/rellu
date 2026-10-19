#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Geometry/BooleanOperation.hpp"

using namespace Rellu;

namespace {
  constexpr auto TOLERANCE = 0.01;

  bool is_tree_root(const Document& document, const NodeId& id) {
    return document.get(id).m_parent.is_nil() && !document.is_on_canvas(id);
  }

  const CompoundShape* get_compound(const Node& node) {
    if(auto shape = node.get_shape()) {
      return std::get_if<CompoundShape>(&shape->m_geometry);
    }
    return nullptr;
  }

  BooleanOperation to_boolean(CompoundOperation operation) {
    switch(operation) {
      case CompoundOperation::SUBTRACT:
        return BooleanOperation::SUBTRACT;
      case CompoundOperation::INTERSECT:
        return BooleanOperation::INTERSECT;
      case CompoundOperation::EXCLUDE:
        return BooleanOperation::EXCLUDE;
      default:
        return BooleanOperation::ADD;
    }
  }

  Path remove_arcs(const Path& path) {
    auto result = Path(path.get_fill_rule());
    for(auto& segment : path.get_segments()) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        result.move_to(points[0]);
      } else if(segment.m_type == SegmentType::LINE) {
        result.line_to(points[0]);
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        result.quadratic_to(points[0], points[1]);
      } else if(segment.m_type == SegmentType::CUBIC) {
        result.cubic_to(points[0], points[1], points[2]);
      } else {
        result.close();
      }
    }
    return result;
  }

  Path combine_children(const Document& document, const NodeId& id,
      CompoundOperation operation) {
    auto operands = std::vector<Path>();
    for(auto& child_id : document.get(id).m_children) {
      auto& child = document.get(child_id);
      if(auto shape = child.get_shape()) {
        operands.push_back(transform(
          to_path(shape->m_geometry), child.m_header.m_transform));
      }
    }
    return combine(operands, to_boolean(operation), TOLERANCE);
  }

  std::vector<NodeId> get_operands(
      const Document& document, const std::vector<NodeId>& targets) {
    auto nodes = get_top_level(document, targets);
    std::erase_if(nodes, [&] (const auto& id) {
      return is_tree_root(document, id);
    });
    return nodes;
  }

  CommandFactory make_boolean_factory(CompoundOperation operation) {
    return [=] (const auto& document, const auto& targets) ->
        std::unique_ptr<Command> {
      auto nodes = get_operands(document, targets);
      if(nodes.size() == 1 && get_compound(document.get(nodes.front()))) {
        return std::make_unique<SetCompoundOperationCommand>(
          std::move(nodes), operation);
      }
      if(nodes.size() < 2 || std::any_of(nodes.begin(), nodes.end(),
          [&] (const auto& id) { return !document.get(id).get_shape(); })) {
        return nullptr;
      }
      return std::make_unique<BooleanCommand>(std::move(nodes), operation);
    };
  }

  std::unique_ptr<Command> make_convert(
      const Document& document, const std::vector<NodeId>& targets) {
    auto nodes = get_operands(document, targets);
    std::erase_if(nodes, [&] (const auto& id) {
      return !is_convertible_to_path(document, id);
    });
    if(nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<ConvertToPathCommand>(std::move(nodes));
  }
}

Style Rellu::make_default_path_style() {
  return make_default_line_style();
}

std::string Rellu::get_operation_label(CompoundOperation operation) {
  switch(operation) {
    case CompoundOperation::SUBTRACT:
      return "Subtract";
    case CompoundOperation::INTERSECT:
      return "Intersect";
    case CompoundOperation::EXCLUDE:
      return "Exclude Overlap";
    default:
      return "Add";
  }
}

std::string Rellu::get_name_prefix(CompoundOperation operation) {
  switch(operation) {
    case CompoundOperation::SUBTRACT:
      return "Subtraction ";
    case CompoundOperation::INTERSECT:
      return "Intersection ";
    case CompoundOperation::EXCLUDE:
      return "Exclusion ";
    default:
      return "Union ";
  }
}

SetPathCommand::SetPathCommand(
  std::string label, const NodeId& id, PathShape geometry) noexcept
  : m_label(std::move(label)),
    m_node(id),
    m_geometry(std::move(geometry)) {}

const NodeId& SetPathCommand::get_node() const {
  return m_node;
}

const PathShape& SetPathCommand::get_geometry() const {
  return m_geometry;
}

std::string SetPathCommand::get_label() const {
  return m_label;
}

void SetPathCommand::apply(
    const Document& document, Transaction& transaction) {
  auto node = document.find(m_node);
  if(!node || !node->get_shape()) {
    throw std::invalid_argument("The node is not a shape.");
  }
  if(get_compound(*node)) {
    throw std::invalid_argument("A compound shape has no points.");
  }
  auto shape = *node->get_shape();
  auto geometry = ShapeGeometry(m_geometry);
  if(shape.m_geometry == geometry) {
    return;
  }
  shape.m_geometry = std::move(geometry);
  transaction.replace_payload(m_node, std::move(shape));
}

bool SetPathCommand::merge(const Command& command) {
  auto edit = dynamic_cast<const SetPathCommand*>(&command);
  if(!edit || edit->m_node != m_node) {
    return false;
  }
  m_geometry = edit->m_geometry;
  return true;
}

bool Rellu::is_convertible_to_path(const Document& document, const NodeId& id) {
  auto node = document.find(id);
  if(!node) {
    return false;
  }
  auto shape = node->get_shape();
  return shape && !std::holds_alternative<PathShape>(shape->m_geometry);
}

ConvertToPathCommand::ConvertToPathCommand(std::vector<NodeId> nodes) noexcept
  : m_nodes(std::move(nodes)) {}

std::string ConvertToPathCommand::get_label() const {
  return "Convert to Path";
}

void ConvertToPathCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_top_level(document, m_nodes)) {
    if(!is_convertible_to_path(document, id)) {
      continue;
    }
    auto& node = document.get(id);
    auto shape = *node.get_shape();
    auto children = node.m_children;
    shape.m_geometry = PathShape(
      Shared(remove_arcs(to_path(shape.m_geometry))), FillRule::NON_ZERO);
    for(auto& child : children) {
      transaction.remove(child);
    }
    transaction.replace_payload(id, std::move(shape));
  }
}

BooleanCommand::BooleanCommand(
  std::vector<NodeId> nodes, CompoundOperation operation) noexcept
  : m_nodes(std::move(nodes)),
    m_operation(operation) {}

const NodeId& BooleanCommand::get_compound() const {
  return m_compound;
}

std::string BooleanCommand::get_label() const {
  return get_operation_label(m_operation);
}

void BooleanCommand::apply(
    const Document& document, Transaction& transaction) {
  auto nodes = get_operands(document, m_nodes);
  if(nodes.size() < 2) {
    throw std::invalid_argument("A boolean operation needs two shapes.");
  }
  for(auto& id : nodes) {
    if(!document.get(id).get_shape()) {
      throw std::invalid_argument("Only shapes can be combined.");
    }
  }
  sort_by_z_order(document, nodes);
  auto& topmost = nodes.back();
  auto parent = document.get(topmost).m_parent;
  auto index = get_index(document, topmost) + 1;
  if(!invert(get_world_transform(document, parent))) {
    throw std::invalid_argument("The container's transform is singular.");
  }
  auto header = NodeHeader();
  header.m_name = make_node_name(document, get_name_prefix(m_operation));
  auto shape = Shape(CompoundShape(m_operation, Shared(Path())),
    document.get(nodes.front()).get_shape()->m_style);
  m_compound = transaction.insert(parent, index, std::move(header), shape);
  ReparentCommand(nodes, m_compound, 0).apply(document, transaction);
  std::get<CompoundShape>(shape.m_geometry).m_path =
    Shared(combine_children(document, m_compound, m_operation));
  transaction.replace_payload(m_compound, std::move(shape));
}

std::optional<std::vector<NodeId>> BooleanCommand::get_selection() const {
  return std::vector{m_compound};
}

SetCompoundOperationCommand::SetCompoundOperationCommand(
  std::vector<NodeId> nodes, CompoundOperation operation) noexcept
  : m_nodes(std::move(nodes)),
    m_operation(operation) {}

std::string SetCompoundOperationCommand::get_label() const {
  return get_operation_label(m_operation);
}

void SetCompoundOperationCommand::apply(
    const Document& document, Transaction& transaction) {
  auto visited = std::unordered_set<NodeId>();
  for(auto& id : m_nodes) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      continue;
    }
    auto compound = get_compound(*node);
    if(!compound || compound->m_operation == m_operation) {
      continue;
    }
    auto shape = *node->get_shape();
    shape.m_geometry = CompoundShape(m_operation,
      Shared(combine_children(document, id, m_operation)));
    transaction.replace_payload(id, std::move(shape));
  }
}

void Rellu::add_path_commands(CommandRegistry& registry) {
  registry.add(
    "object.path.add", make_boolean_factory(CompoundOperation::UNION));
  registry.add("object.path.subtract",
    make_boolean_factory(CompoundOperation::SUBTRACT));
  registry.add("object.path.intersect",
    make_boolean_factory(CompoundOperation::INTERSECT));
  registry.add("object.path.exclude",
    make_boolean_factory(CompoundOperation::EXCLUDE));
  registry.add("object.path.convert", make_convert);
}
