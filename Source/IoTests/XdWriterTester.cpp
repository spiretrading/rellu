#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <doctest/doctest.h>
#include <QJsonArray>
#include <QTemporaryDir>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Geometry/SvgPath.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "XdTestPackage.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  std::string join(const std::vector<std::string>& differences) {
    auto text = std::string();
    for(auto& difference : differences) {
      text += difference + '\n';
    }
    return text;
  }

  struct Built {
    NodeId m_artboard;
    NodeId m_rectangle;
    NodeId m_ellipse;
    NodeId m_circle;
    NodeId m_line;
    NodeId m_polygon;
    NodeId m_path;
    NodeId m_compound;
    NodeId m_text;
    NodeId m_stack;
    NodeId m_padding;
    NodeId m_grid;
    NodeId m_cell;
    NodeId m_cell_text;
    NodeId m_scroll;
    NodeId m_mask;
    NodeId m_instance;
    NodeId m_reference;
    NodeId m_loose;
    NodeId m_master;
    NodeId m_state_tree;
    ComponentId m_component;
    ImageId m_image;
    SwatchId m_swatch;
    CharacterStyleId m_character_style;
  };

  NodeHeader make_header(std::string name) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    header.m_constraints.m_is_enabled = true;
    return header;
  }

  Shape make_rectangle_shape(double width, double height) {
    auto shape = Shape();
    shape.m_geometry =
      RectangleShape(Rect(0, 0, width, height), CornerRadii(0, 0, 0, 0));
    shape.m_style.m_fill = SolidFill(make_color(255, 0, 0));
    return shape;
  }

  std::vector<std::byte> make_png() {
    auto bytes = std::vector<std::byte>();
    for(auto c : std::string_view("\x89PNG\r\n\x1A\n0000")) {
      bytes.push_back(static_cast<std::byte>(c));
    }
    return bytes;
  }

  Text make_text_payload(const std::u16string& content) {
    auto text = Text();
    text.m_content = Shared(content);
    text.m_frame = AreaFrame(Size(200, 40));
    text.m_base_style.m_font =
      Font("Roboto", "Regular", "Roboto-Regular", 14);
    text.m_style.m_fill = SolidFill(text.m_base_style.m_color);
    text.m_runs.push_back(TextRun(content.size(), text.m_base_style));
    text.m_paragraph_style.m_alignment = TextAlignment::CENTER;
    text.m_paragraph_style.m_line_height = 18;
    auto layout = StoredLayout();
    layout.m_paragraphs.emplace_back().m_lines.emplace_back().m_runs.
      push_back(StoredRun(0, content.size(), Point(4, 14)));
    text.m_layout = Shared(std::move(layout));
    return text;
  }

  Built build(Document& document) {
    auto built = Built();
    auto transaction = Transaction(document);
    built.m_image = transaction.add_image(
      ImageResource(NodeId(), "image/png", Shared(make_png())));
    built.m_swatch = transaction.add_swatch(
      ColorSwatch(NodeId(), "Primary", make_color(0, 0, 255)));
    auto style = CharacterStyle();
    style.m_name = "Body";
    style.m_style.m_font = Font("Roboto", "Medium", "Roboto-Medium", 12);
    style.m_line_height = 16;
    built.m_character_style = transaction.add_character_style(style);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 50, 375, 812);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    artboard.m_viewport_height = 600;
    artboard.m_layout_grid = LayoutGrid();
    artboard.m_guides.push_back(Guide(Orientation::VERTICAL, 16));
    built.m_artboard =
      transaction.append(NodeId(), make_header("Home"), artboard);
    auto rectangle = make_rectangle_shape(10, 20);
    std::get<RectangleShape>(rectangle.m_geometry).m_radii =
      CornerRadii(1, 2, 3, 4);
    rectangle.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2,
      StrokeAlignment::INSIDE, StrokeCap::ROUND, StrokeJoin::BEVEL, 4,
      std::vector<double>{2, 1}, true);
    rectangle.m_style.m_effects.push_back(
      DropShadow(Point(0, 3), 6, make_color(0, 0, 0, 0.5), true));
    rectangle.m_style.m_effects.push_back(
      Blur(4, 0, 1, true, false));
    auto rectangle_header = make_header("Rectangle");
    rectangle_header.m_transform = make_translation(10, 20);
    rectangle_header.m_opacity = 0.5;
    rectangle_header.m_blend_mode = BlendMode::MULTIPLY;
    rectangle_header.m_is_fixed_position = true;
    rectangle_header.m_aspect_lock = Size(20, 10);
    built.m_rectangle =
      transaction.append(built.m_artboard, rectangle_header, rectangle);
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(5, 5), 5, 3);
    auto gradient = GradientFill();
    gradient.m_geometry = LinearGradient(Point(0, 0), Point(1, 1));
    gradient.m_stops = {GradientStop(0, make_color(255, 0, 0)),
      GradientStop(1, make_color(0, 255, 0))};
    ellipse.m_style.m_fill = gradient;
    built.m_ellipse =
      transaction.append(built.m_artboard, make_header("Ellipse"), ellipse);
    auto circle = Shape();
    circle.m_geometry = EllipseShape(Point(5, 5), 5, 5);
    auto image = ImageFill();
    image.m_image = built.m_image;
    image.m_size = Size(16, 16);
    image.m_scale_behavior = ScaleBehavior::COVER;
    circle.m_style.m_fill = image;
    built.m_circle =
      transaction.append(built.m_artboard, make_header("Circle"), circle);
    auto line = Shape();
    line.m_geometry = LineShape(Point(0, 0), Point(50, 0));
    line.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
    built.m_line =
      transaction.append(built.m_artboard, make_header("Line"), line);
    auto polygon = Shape();
    polygon.m_geometry = PolygonShape(5, Size(40, 40), 2);
    polygon.m_style.m_fill = SolidFill(make_color(0, 0, 255), built.m_swatch);
    built.m_polygon =
      transaction.append(built.m_artboard, make_header("Polygon"), polygon);
    auto path = Shape();
    path.m_geometry = PathShape(Shared(*parse_svg_path(
      "M 0 0 L 10 0 C 12 2 12 8 10 10 Z")), FillRule::EVEN_ODD);
    built.m_path =
      transaction.append(built.m_artboard, make_header("Path"), path);
    auto compound = Shape();
    compound.m_geometry = CompoundShape(CompoundOperation::SUBTRACT,
      Shared(*parse_svg_path("M 10 0 L 20 0 L 20 20 L 0 20 L 0 10 L 10 10 Z")));
    built.m_compound = transaction.append(
      built.m_artboard, make_header("Compound"), compound);
    transaction.append(built.m_compound, make_header("First"),
      make_rectangle_shape(20, 20));
    transaction.append(built.m_compound, make_header("Second"),
      make_rectangle_shape(10, 10));
    auto text = make_text_payload(u"Hello");
    text.m_runs.front().m_character_style = built.m_character_style;
    text.m_runs.front().m_hyperlink = "https://example.com";
    built.m_text =
      transaction.append(built.m_artboard, make_header("Text"), text);
    auto stack = Group();
    stack.m_stack = Stack(Orientation::HORIZONTAL);
    stack.m_stack->m_padding = Margins(1, 2, 3, 4);
    stack.m_stack->m_has_background = true;
    built.m_stack =
      transaction.append(built.m_artboard, make_header("Stack"), stack);
    auto first = transaction.append(
      built.m_stack, make_header("Background"), make_rectangle_shape(5, 5));
    transaction.append(
      built.m_stack, make_header("Item"), make_rectangle_shape(5, 5));
    auto spaced = *document.get(built.m_stack).get_group();
    spaced.m_stack->m_spacing.emplace(first, 8);
    transaction.replace_payload(built.m_stack, spaced);
    auto padding = Group();
    padding.m_padding = Padding(Margins(4, 4, 4, 4), true, true);
    built.m_padding =
      transaction.append(built.m_artboard, make_header("Padding"), padding);
    auto grid = Group();
    grid.m_repeat_grid = RepeatGrid(Size(50, 50), 10, 10, 2, 3, Size(110, 170));
    built.m_grid =
      transaction.append(built.m_artboard, make_header("Grid"), grid);
    built.m_cell = transaction.append(built.m_grid, make_header(""), Group());
    auto series = make_text_payload(u"One");
    series.m_repeat_data = {u"One", u"Two"};
    built.m_cell_text =
      transaction.append(built.m_cell, make_header("Series"), series);
    auto scroll = Group();
    scroll.m_scroll_group =
      ScrollGroup(ScrollDirection::VERTICAL, Size(100, 200), Point(0, 10));
    built.m_scroll =
      transaction.append(built.m_artboard, make_header("Scroll"), scroll);
    auto mask = Group();
    mask.m_mask = Mask();
    mask.m_mask->m_shapes.push_back(
      MaskShape(Matrix(), EllipseShape(Point(10, 10), 10, 10)));
    built.m_mask =
      transaction.append(built.m_artboard, make_header("Mask"), mask);
    transaction.append(
      built.m_mask, make_header("Masked"), make_rectangle_shape(30, 30));
    built.m_master = transaction.insert_root(make_header("Button"), Group());
    auto master_shape = transaction.append(built.m_master,
      make_header("Background"), make_rectangle_shape(80, 30));
    built.m_state_tree =
      transaction.insert_root(make_header("Hover"), Group());
    transaction.append(built.m_state_tree, make_header("Background"),
      make_rectangle_shape(80, 30));
    auto state = generate_node_id();
    auto component = Component();
    component.m_name = "Button";
    component.m_master = built.m_master;
    component.m_default_state = generate_node_id();
    component.m_size = Size(80, 30);
    component.m_states.push_back(
      ComponentState(state, "Hover", built.m_state_tree));
    built.m_component = transaction.add_component(component);
    auto instance = Group();
    instance.m_component_instance =
      ComponentInstance(built.m_component, StateId(), Size(80, 30));
    built.m_instance = transaction.append(
      built.m_artboard, make_header("Button"), instance);
    auto reference = Reference(master_shape);
    auto reference_header = make_header("");
    reference_header.m_constraints = Constraints();
    built.m_reference = transaction.append(
      built.m_instance, reference_header, reference);
    built.m_loose = transaction.append(
      NodeId(), make_header("Loose"), make_rectangle_shape(5, 5));
    auto interactions = std::vector<Interaction>();
    interactions.push_back(Interaction(NodeId(), built.m_rectangle,
      TapTrigger(), ArtboardTransition(built.m_artboard,
        Animation(Transition::DISSOLVE, Easing::EASE_OUT, 0.3))));
    interactions.push_back(Interaction(NodeId(), built.m_instance,
      HoverTrigger(), StateTransition(state)));
    interactions.push_back(Interaction(NodeId(), built.m_ellipse,
      TapTrigger(), ArtboardTransition(NodeId(),
        Animation(Transition::NONE, Easing::EASE_IN, 0.2))));
    interactions.push_back(Interaction(NodeId(), built.m_line,
      TapTrigger(), Hyperlink("https://www.example.com")));
    std::sort(interactions.begin(), interactions.end(),
      [] (const auto& left, const auto& right) {
        return to_string(left.m_owner) < to_string(right.m_owner);
      });
    for(auto& interaction : interactions) {
      transaction.add_interaction(interaction);
    }
    transaction.set_home_artboard(built.m_artboard);
    transaction.set_flows({Flow(built.m_artboard, "Main")});
    transaction.commit();
    return built;
  }

  std::vector<std::string> filter(std::vector<std::string> differences) {
    std::erase(differences, "document opaque data differs");
    return differences;
  }

  QJsonObject find_node(const QJsonValue& value, const NodeId& id) {
    if(value.isObject()) {
      auto object = value.toObject();
      auto key = to_string(id);
      if(object.value("id"_L1).toString().toStdString() == key ||
          object.value("guid"_L1).toString().toStdString() == key) {
        return object;
      }
      for(auto member : object) {
        if(auto found = find_node(member, id); !found.isEmpty()) {
          return found;
        }
      }
    } else if(value.isArray()) {
      for(auto element : value.toArray()) {
        if(auto found = find_node(element, id); !found.isEmpty()) {
          return found;
        }
      }
    }
    return QJsonObject();
  }

  QJsonObject find_written(const XdPackage& package, const NodeId& id) {
    for(auto& entry : package.get_entries()) {
      if(entry.ends_with(".agc")) {
        if(auto node = find_node(package.read_json(entry), id);
            !node.isEmpty()) {
          return node;
        }
      }
    }
    return QJsonObject();
  }

  QJsonObject get_ux(const QJsonObject& node) {
    return node.value("meta"_L1).toObject().value("ux"_L1).toObject();
  }
}

TEST_SUITE("XdWriter") {
  TEST_CASE("round_trip") {
    auto document = Document();
    build(document);
    auto bytes = write_xd(document);
    auto imported = Document();
    auto report = import_package(bytes, imported);
    REQUIRE(report.m_warnings.empty());
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(join(filter(find_differences(document, imported))) == "");
    REQUIRE(report.m_opaque_node_count == 0);
    auto rewritten = Document();
    import_package(write_xd(imported), rewritten);
    REQUIRE(join(find_differences(imported, rewritten)) == "");
  }

  TEST_CASE("package_entries") {
    auto document = Document();
    auto built = build(document);
    auto bytes = write_xd(document);
    auto package = XdPackage(bytes);
    auto& entries = package.get_entries();
    REQUIRE(entries.front() == "mimetype");
    REQUIRE(package.read("mimetype") ==
      "application/vnd.adobe.sparkler.project+dcxucf");
    REQUIRE(package.contains("META-INF/metadata.xml"));
    REQUIRE(package.contains(XdPackage::RESOURCES));
    REQUIRE(package.contains(XdPackage::PASTEBOARD));
    REQUIRE(package.contains(XdPackage::INTERACTIONS));
    REQUIRE(package.contains("resources/" + to_uid(built.m_image)));
    REQUIRE(package.get_artboard_entries().size() == 1);
    REQUIRE(package.get_artboard_entries().front() ==
      "artwork/artboard-" + to_string(built.m_artboard) +
        "/graphics/graphicContent.agc");
    auto manifest = package.get_manifest();
    REQUIRE(manifest.value("manifest-format-version"_L1).toInt() == 6);
    auto children = manifest.value("children"_L1).toArray();
    auto paths = std::vector<std::string>();
    for(auto child : children) {
      paths.push_back(
        child.toObject().value("path"_L1).toString().toStdString());
    }
    REQUIRE(paths == std::vector<std::string>{
      "artwork", "resources", "interactions"});
    auto artwork = children.first().toObject().value("children"_L1).toArray();
    REQUIRE(artwork.size() == 2);
    auto bounds = artwork.at(1).toObject().value("uxdesign#bounds"_L1).
      toObject();
    REQUIRE(bounds.value("x"_L1).toDouble() == 100);
    REQUIRE(bounds.value("height"_L1).toDouble() == 812);
    auto pasteboard = package.read_pasteboard();
    REQUIRE(pasteboard.value("children"_L1).toArray().size() == 1);
  }

  TEST_CASE("shapes") {
    auto document = Document();
    auto built = build(document);
    auto package = XdPackage(write_xd(document));
    auto rectangle = find_written(package, built.m_rectangle);
    REQUIRE(rectangle.value("type"_L1) == "shape"_L1);
    auto transform = rectangle.value("transform"_L1).toObject();
    REQUIRE(transform.value("tx"_L1).toDouble() == 110);
    REQUIRE(transform.value("ty"_L1).toDouble() == 70);
    auto shape = rectangle.value("shape"_L1).toObject();
    REQUIRE(shape.value("type"_L1) == "rect"_L1);
    REQUIRE(shape.value("r"_L1).toArray().size() == 4);
    auto style = rectangle.value("style"_L1).toObject();
    REQUIRE(style.value("opacity"_L1).toDouble() == 0.5);
    REQUIRE(style.value("blendMode"_L1) == "multiply"_L1);
    auto stroke = style.value("stroke"_L1).toObject();
    REQUIRE(stroke.value("align"_L1) == "inside"_L1);
    REQUIRE(stroke.value("cap"_L1) == "round"_L1);
    REQUIRE(stroke.value("join"_L1) == "bevel"_L1);
    REQUIRE(stroke.value("dash"_L1).toArray().size() == 2);
    auto filters = style.value("filters"_L1).toArray();
    REQUIRE(filters.size() == 2);
    REQUIRE(filters.at(0).toObject().value("type"_L1) == "dropShadow"_L1);
    REQUIRE(filters.at(1).toObject().value("type"_L1) == "uxdesign#blur"_L1);
    REQUIRE(get_ux(rectangle).value("interactions"_L1).toArray().size() == 1);
    auto ellipse = find_written(package, built.m_ellipse);
    REQUIRE(ellipse.value("shape"_L1).toObject().value("type"_L1) ==
      "ellipse"_L1);
    REQUIRE(ellipse.value("style"_L1).toObject().value("fill"_L1).toObject().
      value("type"_L1) == "gradient"_L1);
    auto circle = find_written(package, built.m_circle);
    REQUIRE(circle.value("shape"_L1).toObject().value("type"_L1) ==
      "circle"_L1);
    auto pattern = circle.value("style"_L1).toObject().value("fill"_L1).
      toObject().value("pattern"_L1).toObject();
    REQUIRE(get_ux(pattern).value("uid"_L1).toString().toStdString() ==
      to_uid(built.m_image));
    auto line = find_written(package, built.m_line);
    REQUIRE(line.value("shape"_L1).toObject().value("x2"_L1).toDouble() ==
      50);
    auto polygon = find_written(package, built.m_polygon);
    auto polygon_shape = polygon.value("shape"_L1).toObject();
    REQUIRE(polygon_shape.value("uxdesign#cornerCount"_L1).toInt() == 5);
    auto fill = polygon.value("style"_L1).toObject().value("fill"_L1).
      toObject();
    REQUIRE(fill.value("color"_L1).toObject().value("linkId"_L1).toString().
      toStdString() == to_string(built.m_swatch));
    auto path = find_written(package, built.m_path);
    REQUIRE(path.value("shape"_L1).toObject().value("path"_L1) ==
      "M 0 0 L 10 0 C 12 2 12 8 10 10 Z"_L1);
    REQUIRE(path.value("shape"_L1).toObject().value("winding"_L1) ==
      "evenodd"_L1);
    auto compound = find_written(package, built.m_compound);
    auto compound_shape = compound.value("shape"_L1).toObject();
    REQUIRE(compound_shape.value("operation"_L1) == "subtract"_L1);
    REQUIRE(compound_shape.value("children"_L1).toArray().size() == 2);
  }

  TEST_CASE("groups") {
    auto document = Document();
    auto built = build(document);
    auto package = XdPackage(write_xd(document));
    auto stack = get_ux(find_written(package, built.m_stack));
    REQUIRE(stack.value("contentStackType"_L1) == "horizontal"_L1);
    REQUIRE(stack.value("hasContentBackground"_L1).toBool());
    REQUIRE(stack.value("contentPadding"_L1).toObject().value(
      "left"_L1).toDouble() == 4);
    auto stack_children = find_written(package, built.m_stack).value(
      "group"_L1).toObject().value("children"_L1).toArray();
    REQUIRE(get_ux(stack_children.at(0).toObject()).value(
      "contentStackMarginAfter"_L1).toDouble() == 8);
    auto padding = get_ux(find_written(package, built.m_padding));
    REQUIRE(padding.value("isContentAware"_L1).toBool());
    auto grid = get_ux(find_written(package, built.m_grid)).value(
      "repeatGrid"_L1).toObject();
    REQUIRE(grid.value("columns"_L1).toInt() == 2);
    REQUIRE(grid.value("rows"_L1).toInt() == 3);
    auto scroll = get_ux(find_written(package, built.m_scroll));
    REQUIRE(scroll.value("scrollingType"_L1) == "vertical"_L1);
    REQUIRE(scroll.value("viewportHeight"_L1).toDouble() == 200);
    REQUIRE(scroll.value("offsetY"_L1).toDouble() == 10);
    auto mask = get_ux(find_written(package, built.m_mask)).value(
      "clipPathResources"_L1).toObject();
    REQUIRE(mask.value("type"_L1) == "clipPath"_L1);
    REQUIRE(mask.value("children"_L1).toArray().size() == 1);
  }

  TEST_CASE("components") {
    auto document = Document();
    auto built = build(document);
    auto package = XdPackage(write_xd(document));
    auto resources = package.read_resources();
    auto symbols = resources.value("resources"_L1).toObject().value(
      "meta"_L1).toObject().value("ux"_L1).toObject().value(
        "symbols"_L1).toArray();
    REQUIRE(symbols.size() == 1);
    auto master = get_ux(symbols.first().toObject());
    REQUIRE(master.value("symbolId"_L1).toString().toStdString() ==
      to_string(built.m_component));
    auto states = master.value("states"_L1).toArray();
    REQUIRE(states.size() == 1);
    REQUIRE(states.first().toObject().value("name"_L1) == "Hover"_L1);
    auto instance = find_written(package, built.m_instance);
    auto ux = get_ux(instance);
    REQUIRE(ux.value("symbolId"_L1).toString().toStdString() ==
      to_string(built.m_component));
    REQUIRE(ux.value("width"_L1).toDouble() == 80);
    REQUIRE(ux.value("componentType"_L1) == "componentNoBounds"_L1);
    REQUIRE(master.value("width"_L1).toDouble() == 80);
    REQUIRE(master.value("height"_L1).toDouble() == 30);
    REQUIRE(master.value("componentType"_L1) == "componentNoBounds"_L1);
    auto reference = find_written(package, built.m_reference);
    REQUIRE(reference.value("type"_L1) == "syncRef"_L1);
    REQUIRE(!reference.contains("meta"_L1));
    REQUIRE(reference.value("syncSourceGuid"_L1).toString().toStdString() ==
      to_string(document.get(built.m_reference).get_reference()->m_source));
  }

  TEST_CASE("repeat_grid_cells") {
    auto document = Document();
    auto built = build(document);
    auto package = XdPackage(write_xd(document));
    REQUIRE(get_ux(find_written(package, built.m_cell)).value(
      "disableNaming"_L1).toBool());
    REQUIRE(!get_ux(find_written(package, built.m_cell_text)).contains(
      "disableNaming"_L1));
    auto series = get_ux(find_written(package, built.m_cell_text)).value(
      "repeatOverrides"_L1).toArray();
    REQUIRE(series.size() == 2);
    REQUIRE(series[1].toObject() == QJsonObject({{"overrideType", "text"},
      {"data", "Two"}, {"type", "RepeatOverride"}}));
  }

  TEST_CASE("interaction_without_destination") {
    auto document = Document();
    auto built = build(document);
    auto interactions = XdPackage(write_xd(document)).read_interactions();
    auto list = interactions.value("interactions"_L1).toObject().value(
      QString::fromStdString(to_string(built.m_ellipse))).toArray();
    REQUIRE(list.size() == 1);
    auto properties =
      list.first().toObject().value("properties"_L1).toObject();
    REQUIRE(!properties.contains("destination"_L1));
    REQUIRE(properties.value("transition"_L1) == "none"_L1);
    REQUIRE(properties.value("easing"_L1) == "ease-in"_L1);
  }

  TEST_CASE("hyperlink") {
    auto document = Document();
    auto built = build(document);
    auto interactions = XdPackage(write_xd(document)).read_interactions();
    auto list = interactions.value("interactions"_L1).toObject().value(
      QString::fromStdString(to_string(built.m_line))).toArray();
    REQUIRE(list.size() == 1);
    auto entry = list.first().toObject();
    REQUIRE(entry.value("action"_L1) == "hyperlink"_L1);
    REQUIRE(entry.value("triggerEvent"_L1) == "tap"_L1);
    REQUIRE(entry.value("properties"_L1).toObject() ==
      QJsonObject({{"hyperlinkUrl", "https://www.example.com"}}));
  }

  TEST_CASE("main_component") {
    auto document = Document();
    auto built = build(document);
    REQUIRE(!get_ux(find_written(XdPackage(write_xd(document)),
      built.m_instance)).contains("isMaster"_L1));
    {
      auto transaction = Transaction(document);
      auto group = *document.get(built.m_instance).get_group();
      group.m_component_instance->m_is_main = true;
      transaction.replace_payload(built.m_instance, group);
      transaction.commit();
    }
    auto bytes = write_xd(document);
    REQUIRE(get_ux(find_written(XdPackage(bytes), built.m_instance)).value(
      "isMaster"_L1).toBool());
    auto imported = Document();
    import_package(bytes, imported);
    REQUIRE(imported.get(built.m_instance).get_group()->
      m_component_instance->m_is_main);
    REQUIRE(join(filter(find_differences(document, imported))) == "");
  }

  TEST_CASE("text") {
    auto document = Document();
    auto built = build(document);
    auto package = XdPackage(write_xd(document));
    auto text = find_written(package, built.m_text);
    auto content = text.value("text"_L1).toObject();
    REQUIRE(content.value("rawText"_L1) == "Hello"_L1);
    REQUIRE(content.value("frame"_L1).toObject().value("type"_L1) ==
      "area"_L1);
    auto line = content.value("paragraphs"_L1).toArray().first().toObject().
      value("lines"_L1).toArray().first().toArray().first().toObject();
    REQUIRE(line.value("to"_L1).toInt() == 5);
    REQUIRE(line.value("y"_L1).toDouble() == 14);
    auto style = text.value("style"_L1).toObject();
    REQUIRE(style.value("font"_L1).toObject().value("family"_L1) ==
      "Roboto"_L1);
    auto attributes = style.value("textAttributes"_L1).toObject();
    REQUIRE(attributes.value("paragraphAlign"_L1) == "center"_L1);
    REQUIRE(attributes.value("lineHeight"_L1).toDouble() == 18);
    auto ranges = get_ux(text).value("rangedStyles"_L1).toArray();
    REQUIRE(ranges.size() == 1);
    REQUIRE(ranges.first().toObject().value("linkId"_L1).toString().
      toStdString() == to_string(built.m_character_style));
    REQUIRE(ranges.first().toObject().value("hyperlinkUrl"_L1) ==
      "https://example.com"_L1);
  }

  TEST_CASE("text_layout") {
    auto document = Document();
    {
      auto transaction = Transaction(document);
      auto text = make_text_payload(u"One\nTwo");
      text.m_runs.front().m_length = 7;
      text.m_layout = std::nullopt;
      transaction.append(NodeId(), make_header("Text"), text);
      transaction.commit();
    }
    auto bytes = write_xd(document);
    auto pasteboard = XdPackage(bytes).read_pasteboard();
    REQUIRE(!pasteboard.value("children"_L1).toArray().first().toObject().
      value("text"_L1).toObject().contains("paragraphs"_L1));
    auto imported = Document();
    import_package(bytes, imported);
    REQUIRE(!imported.get(imported.get_canvas().front()).get_text()->
      m_layout);
    auto options = XdWriteOptions();
    options.m_layout = [] (const Text&) {
      auto layout = StoredLayout();
      layout.m_paragraphs.emplace_back().m_lines.emplace_back().m_runs.
        push_back(StoredRun(0, 7, Point(1, 2)));
      return std::optional(layout);
    };
    pasteboard = XdPackage(write_xd(document, options)).read_pasteboard();
    auto paragraphs = pasteboard.value("children"_L1).toArray().first().
      toObject().value("text"_L1).toObject().value("paragraphs"_L1).toArray();
    REQUIRE(paragraphs.size() == 1);
  }

  TEST_CASE("resources") {
    auto document = Document();
    auto built = build(document);
    auto package = XdPackage(write_xd(document));
    auto resources = package.read_resources();
    auto ux = resources.value("resources"_L1).toObject().value(
      "meta"_L1).toObject().value("ux"_L1).toObject();
    auto elements = ux.value("documentLibrary"_L1).toObject().value(
      "elements"_L1).toArray();
    REQUIRE(elements.size() == 2);
    REQUIRE(elements.at(0).toObject().value("type"_L1) ==
      "application/vnd.adobe.element.color+dcx"_L1);
    REQUIRE(elements.at(1).toObject().value("name"_L1) == "Body"_L1);
    auto frames = resources.value("artboards"_L1).toObject();
    auto frame = frames.value(QString::fromStdString(
      to_string(built.m_artboard))).toObject();
    REQUIRE(frame.value("name"_L1) == "Home"_L1);
    REQUIRE(frame.value("viewportHeight"_L1).toDouble() == 600);
    auto interactions = package.read_interactions();
    REQUIRE(interactions.value("homeArtboard"_L1).toString().toStdString() ==
      to_string(built.m_artboard));
    REQUIRE(interactions.value("interactions"_L1).toObject().size() == 4);
    REQUIRE(package.read("resources/" + to_uid(built.m_image)).size() == 12);
  }

  TEST_CASE("transform_3d") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 50, 375, 812);
    auto artboard_id =
      transaction.append(NodeId(), make_header("Home"), artboard);
    auto header = make_header("Tilted");
    header.m_transform = Matrix(1, 0, 0, 0.5, 8, 7);
    auto& transform = header.m_transform_3d.emplace();
    transform.m_matrix = std::array<double, 16>(
      {1, 0, 0, 8, 0, 0.5, 0.25, 7, 0, -0.25, 0.5, 3, 0, 0, 0, 1});
    transform.m_rotation_x = -10;
    transform.m_perspective_center = Point(92, 21.5);
    auto group = transaction.append(artboard_id, header, Group());
    transaction.commit();
    auto bytes = write_xd(document);
    auto written = find_written(XdPackage(bytes), group);
    auto matrix = written.value("transform"_L1).toObject();
    REQUIRE(matrix.value("0"_L1).toArray() == QJsonArray({1, 0, 0, 108}));
    REQUIRE(matrix.value("1"_L1).toArray() == QJsonArray({0, 0.5, 0.25, 57}));
    REQUIRE(matrix.value("2"_L1).toArray() == QJsonArray({0, -0.25, 0.5, 3}));
    REQUIRE(matrix.value("3"_L1).toArray() == QJsonArray({0, 0, 0, 1}));
    auto ux = get_ux(written);
    REQUIRE(ux.value("rotationX"_L1).toDouble() == -10);
    REQUIRE(!ux.contains("rotationY"_L1));
    auto center = ux.value("perspectiveCenter"_L1).toObject();
    REQUIRE(center.value("x"_L1).toDouble() == 92);
    REQUIRE(center.value("y"_L1).toDouble() == 21.5);
    auto imported = Document();
    auto report = import_package(bytes, imported);
    REQUIRE(report.m_warnings.empty());
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(join(filter(find_differences(document, imported))) == "");
  }

  TEST_CASE("empty_document") {
    auto document = Document();
    auto package = XdPackage(write_xd(document));
    REQUIRE(package.contains(XdPackage::RESOURCES));
    auto imported = Document();
    auto report = import_package(write_xd(document), imported);
    REQUIRE(report.m_warnings.empty());
    REQUIRE(imported.get_nodes().get_size() == 0);
  }

  TEST_CASE("stale_compound") {
    auto document = Document();
    auto id = NodeId();
    {
      auto transaction = Transaction(document);
      auto compound = Shape();
      compound.m_geometry = CompoundShape(CompoundOperation::SUBTRACT,
        Shared(*parse_svg_path("M 0 0 L 80 0 L 80 80 L 0 80 Z")));
      id = transaction.append(NodeId(), make_header("Compound"), compound);
      transaction.append(id, make_header("Square"),
        make_rectangle_shape(80, 80));
      auto hole = make_rectangle_shape(20, 20);
      auto hole_header = make_header("Hole");
      hole_header.m_transform = make_translation(30, 30);
      transaction.append(id, hole_header, hole);
      transaction.commit();
    }
    auto imported = Document();
    import_package(write_xd(document), imported);
    auto& path = *std::get<CompoundShape>(
      imported.get(id).get_shape()->m_geometry).m_path;
    REQUIRE(contains(path, Point(10, 10)));
    REQUIRE(!contains(path, Point(40, 40)));
    auto rewritten = XdPackage(write_xd(imported)).read_pasteboard();
    REQUIRE(rewritten.value("children"_L1).toArray().first().toObject().value(
      "shape"_L1).toObject().value("path"_L1).toString().toStdString() ==
        to_svg_path(path));
  }

  TEST_CASE("replace_open_file") {
    auto document = Document();
    build(document);
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto path = std::filesystem::path(
      directory.filePath("document.xd").toStdU16String());
    write_xd(document, path);
    auto file = std::ifstream(path, std::ios::binary);
    REQUIRE(file.is_open());
    auto release = std::jthread([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      file.close();
    });
    write_xd(document, path);
    release.join();
    auto imported = Document();
    import_xd(path, imported);
    REQUIRE(join(filter(find_differences(document, imported))) == "");
    REQUIRE(!std::filesystem::exists(path.string() + ".saving"));
  }

  TEST_CASE("default_names") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto rectangle = transaction.append(NodeId(),
      make_header("Rectangle 3"), make_rectangle_shape(5, 5));
    auto custom = transaction.append(
      NodeId(), make_header("Logo"), make_rectangle_shape(5, 5));
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(5, 5), 5, 3);
    auto ellipse_id =
      transaction.append(NodeId(), make_header("Ellipse 12"), ellipse);
    auto group = transaction.append(NodeId(), make_header("Group 2"), Group());
    transaction.append(
      group, make_header("Path 1"), make_rectangle_shape(5, 5));
    auto text = transaction.append(
      NodeId(), make_header("Hello"), make_text_payload(u"Hello"));
    auto renamed = transaction.append(
      NodeId(), make_header("Title"), make_text_payload(u"Hello"));
    auto grid = Group();
    grid.m_repeat_grid = RepeatGrid(Size(50, 50), 10, 10, 2, 3, Size(110, 170));
    auto grid_id =
      transaction.append(NodeId(), make_header("Repeat Grid 1"), grid);
    transaction.commit();
    auto package = XdPackage(write_xd(document));
    auto get_key = [&] (const NodeId& id) {
      return get_ux(find_written(package, id)).value("nameL10N"_L1).
        toString().toStdString();
    };
    REQUIRE(get_key(rectangle) == "SHAPE_RECTANGLE");
    REQUIRE(get_key(custom).empty());
    REQUIRE(get_key(ellipse_id) == "SHAPE_ELLIPSE");
    REQUIRE(get_key(group) == "SHAPE_GROUP");
    REQUIRE(get_key(text) == "SHAPE_GROUP");
    REQUIRE(get_key(renamed).empty());
    REQUIRE(get_key(grid_id) == "SHAPE_REPEAT_GRID");
  }
}
