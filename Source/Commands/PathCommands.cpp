#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Geometry/Stroke.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Model/CompoundResolution.hpp"

using namespace Rellu;

namespace {
  constexpr auto TOLERANCE = 0.01;
  constexpr auto OUTLINE_TOLERANCE = 0.01;
  constexpr auto FIT_TOLERANCE = 0.03;

  void clear_pins(
      const Document& document, Transaction& transaction, const NodeId& id) {
    auto children = document.get(id).m_children;
    for(auto& child : children) {
      auto header = document.get(child).m_header;
      auto& constraints = header.m_constraints;
      if(constraints.m_left || constraints.m_right || constraints.m_top ||
          constraints.m_bottom || constraints.m_width ||
          constraints.m_height) {
        constraints.m_left = false;
        constraints.m_right = false;
        constraints.m_top = false;
        constraints.m_bottom = false;
        constraints.m_width = false;
        constraints.m_height = false;
        transaction.replace_header(std::move(header));
      }
      clear_pins(document, transaction, child);
    }
  }

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
      if(auto outline = get_operand_outline(
          document.get_nodes(), document.get(child_id), TOLERANCE)) {
        operands.push_back(std::move(*outline));
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

  bool is_operand(const Document& document, const Node& node) {
    auto parent = document.find(node.m_parent);
    return parent && get_compound(*parent);
  }

  bool is_outline_group(const Group& group) {
    return !group.m_component_instance && !group.m_repeat_grid &&
      !group.m_scroll_group;
  }

  std::string get_outline_name(const Node& node) {
    if(!node.m_header.m_name.empty()) {
      return node.m_header.m_name;
    } else if(auto text = node.get_text()) {
      return make_text_name(*text->m_content);
    }
    auto& geometry = node.get_shape()->m_geometry;
    if(std::holds_alternative<RectangleShape>(geometry)) {
      return std::string(localize("name", "Rectangle"));
    } else if(std::holds_alternative<EllipseShape>(geometry)) {
      return std::string(localize("name", "Ellipse"));
    } else if(std::holds_alternative<PolygonShape>(geometry)) {
      return std::string(localize("name", "Polygon"));
    } else if(std::holds_alternative<LineShape>(geometry)) {
      return std::string(localize("name", "Line"));
    }
    return std::string(localize("name", "Path"));
  }

  void hide_effects(Style& style) {
    for(auto& effect : style.m_effects) {
      std::visit([] (auto& effect) {
        effect.m_is_visible = false;
      }, effect);
    }
  }

  CommandFactory make_outline_stroke(TextOutliner outliner) {
    return [=] (const Document& document,
        const std::vector<NodeId>& targets) -> std::unique_ptr<Command> {
      auto nodes = get_operands(document, targets);
      if(std::none_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
          return is_outline_strokable(document, id);
        })) {
        return nullptr;
      }
      return std::make_unique<OutlineStrokeCommand>(
        std::move(nodes), outliner);
    };
  }

  void replace_with_shape(const Document& document, Transaction& transaction,
      const NodeId& id, NodeHeader header, Shape shape) {
    auto& node = document.get(id);
    auto parent = node.m_parent;
    auto index = get_index(document, id);
    auto interactions = std::vector<Interaction>();
    for(auto& interaction : document.get_prototype().m_interactions) {
      if(interaction.m_owner == id) {
        interactions.push_back(interaction);
      }
    }
    transaction.remove(id);
    transaction.insert(parent, index, std::move(header), std::move(shape));
    for(auto& interaction : interactions) {
      transaction.add_interaction(std::move(interaction));
    }
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

Path Rellu::to_editable_path(const ShapeGeometry& geometry) {
  return remove_arcs(to_path(geometry));
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
    shape.m_geometry =
      PathShape(Shared(to_editable_path(shape.m_geometry)), FillRule::NON_ZERO);
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
  auto kind = get_name_prefix(m_operation);
  kind.pop_back();
  header.m_name =
    make_node_name(document, std::string(localize("name", kind)) + ' ');
  auto shape = Shape(CompoundShape(m_operation, Shared(Path())),
    document.get(nodes.front()).get_shape()->m_style);
  m_compound = transaction.insert(parent, index, std::move(header), shape);
  ReparentCommand(nodes, m_compound, 0).apply(document, transaction);
  clear_pins(document, transaction, m_compound);
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
    auto is_renamed = !node->m_header.m_has_custom_name;
    shape.m_geometry = CompoundShape(m_operation,
      Shared(combine_children(document, id, m_operation)));
    transaction.replace_payload(id, std::move(shape));
    if(is_renamed) {
      auto header = document.get(id).m_header;
      auto kind = get_name_prefix(m_operation);
      kind.pop_back();
      header.m_name =
        make_node_name(document, std::string(localize("name", kind)) + ' ');
      transaction.replace_header(std::move(header));
    }
  }
}

Shape Rellu::make_stroke_outline(const Path& path, const Style& style) {
  auto& stroke = *style.m_stroke;
  auto geometry = StrokeGeometry(stroke.m_width, stroke.m_alignment,
    stroke.m_cap, stroke.m_join, stroke.m_miter_limit, stroke.m_dashes,
    stroke.m_dash_offset);
  auto polygons = outline_stroke(path, geometry, OUTLINE_TOLERANCE);
  auto outline = Shape();
  outline.m_geometry = PathShape(Shared(fit_curves(
    flatten(polygons, OUTLINE_TOLERANCE), FIT_TOLERANCE)),
    FillRule::NON_ZERO);
  outline.m_style = style;
  outline.m_style.m_fill = SolidFill(stroke.m_color);
  outline.m_style.m_stroke->m_is_enabled = false;
  return outline;
}

bool Rellu::is_outline_strokable(const Document& document, const NodeId& id) {
  auto node = document.find(id);
  if(!node || is_operand(document, *node)) {
    return false;
  }
  if(auto shape = node->get_shape()) {
    return shape->m_style.m_stroke && shape->m_style.m_stroke->m_is_enabled;
  } else if(auto text = node->get_text()) {
    return text->m_style.m_stroke && text->m_style.m_stroke->m_is_enabled;
  } else if(auto group = node->get_group(); group && is_outline_group(*group)) {
    return std::any_of(node->m_children.begin(), node->m_children.end(),
      [&] (const auto& child) {
        return is_outline_strokable(document, child);
      });
  }
  return false;
}

OutlineStrokeCommand::OutlineStrokeCommand(
  std::vector<NodeId> nodes, TextOutliner outliner) noexcept
  : m_nodes(std::move(nodes)),
    m_outliner(std::move(outliner)) {}

std::string OutlineStrokeCommand::get_label() const {
  return "Outline Stroke";
}

void OutlineStrokeCommand::apply(
    const Document& document, Transaction& transaction) {
  m_selection.clear();
  for(auto& id : get_top_level(document, m_nodes)) {
    outline(document, transaction, id, m_selection);
  }
}

std::optional<std::vector<NodeId>> OutlineStrokeCommand::get_selection() const {
  return m_selection;
}

void OutlineStrokeCommand::outline(const Document& document,
    Transaction& transaction, const NodeId& id, std::vector<NodeId>& results) {
  if(!is_outline_strokable(document, id)) {
    results.push_back(id);
    return;
  }
  auto& node = document.get(id);
  if(node.get_group()) {
    auto children = node.m_children;
    auto outlined = std::vector<NodeId>();
    for(auto& child : children) {
      outline(document, transaction, child, outlined);
    }
    results.push_back(id);
    return;
  }
  auto text = std::optional<Text>();
  auto shape = std::optional<Shape>();
  if(auto payload = node.get_text()) {
    if(!m_outliner) {
      results.push_back(id);
      return;
    }
    text = *payload;
  } else {
    shape = *node.get_shape();
  }
  auto& style = text ? text->m_style : shape->m_style;
  auto outlined = make_stroke_outline(
    text ? m_outliner(*text) : to_path(shape->m_geometry), style);
  hide_effects(outlined.m_style);
  auto header = node.m_header;
  auto parent = node.m_parent;
  auto is_filled = !std::holds_alternative<NoFill>(style.m_fill) &&
    (text || !std::holds_alternative<LineShape>(shape->m_geometry));
  if(!is_filled) {
    header.m_name = get_outline_name(node);
    header.m_has_custom_name = true;
    if(text) {
      replace_with_shape(
        document, transaction, id, std::move(header), std::move(outlined));
    } else {
      auto children = node.m_children;
      for(auto& child : children) {
        transaction.remove(child);
      }
      transaction.replace_header(header);
      transaction.replace_payload(id, std::move(outlined));
    }
    results.push_back(id);
    return;
  }
  auto container = document.find(parent);
  auto is_grouped = header.m_opacity != 1 ||
    header.m_blend_mode != BlendMode::NORMAL ||
    (container && container->get_group() && container->get_group()->m_stack);
  auto path = NodeHeader();
  path.m_name =
    get_outline_name(node) + " - " + std::string(localize("name", "Outline"));
  path.m_has_custom_name = true;
  path.m_is_visible = header.m_is_visible;
  path.m_transform = header.m_transform;
  path.m_transform_3d = header.m_transform_3d;
  path.m_constraints = header.m_constraints;
  path.m_is_fixed_position = header.m_is_fixed_position;
  path.m_aspect_lock = header.m_aspect_lock;
  if(!is_grouped) {
    path.m_opacity = header.m_opacity;
    path.m_blend_mode = header.m_blend_mode;
  }
  auto index = get_index(document, id);
  auto copy = transaction.insert(
    parent, index + 1, std::move(path), std::move(outlined));
  for(auto& interaction : document.get_prototype().m_interactions) {
    if(interaction.m_owner == id) {
      auto duplicate = interaction;
      duplicate.m_id = InteractionId();
      duplicate.m_owner = copy;
      transaction.add_interaction(std::move(duplicate));
    }
  }
  if(text) {
    text->m_style.m_stroke->m_is_enabled = false;
    hide_effects(text->m_style);
    transaction.replace_payload(id, std::move(*text));
  } else {
    shape->m_style.m_stroke->m_is_enabled = false;
    hide_effects(shape->m_style);
    transaction.replace_payload(id, std::move(*shape));
  }
  if(!is_grouped) {
    results.push_back(id);
    results.push_back(copy);
    return;
  }
  auto group = NodeHeader();
  group.m_name = header.m_name;
  group.m_opacity = header.m_opacity;
  group.m_blend_mode = header.m_blend_mode;
  auto group_id = transaction.insert(parent, index, std::move(group), Group());
  ReparentCommand(std::vector{id, copy}, group_id, 0).apply(
    document, transaction);
  header = document.get(id).m_header;
  header.m_opacity = 1;
  header.m_blend_mode = BlendMode::NORMAL;
  transaction.replace_header(std::move(header));
  results.push_back(group_id);
}

void Rellu::add_path_commands(
    CommandRegistry& registry, TextOutliner outliner) {
  registry.add(
    "object.path.add", make_boolean_factory(CompoundOperation::UNION));
  registry.add("object.path.subtract",
    make_boolean_factory(CompoundOperation::SUBTRACT));
  registry.add("object.path.intersect",
    make_boolean_factory(CompoundOperation::INTERSECT));
  registry.add("object.path.exclude",
    make_boolean_factory(CompoundOperation::EXCLUDE));
  registry.add("object.path.convert", make_convert);
  registry.add("object.path.outline_stroke",
    make_outline_stroke(std::move(outliner)));
}
