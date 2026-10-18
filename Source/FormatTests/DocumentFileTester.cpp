#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QtZlib/zlib.h>
#include "Rellu/Format/DocumentComparison.hpp"
#include "Rellu/Format/DocumentFile.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  constexpr auto SIGNATURE_SIZE = std::size_t(8);
  constexpr auto FILE_HEADER_SIZE = std::size_t(16);
  constexpr auto CHUNK_HEADER_SIZE = std::size_t(28);

  struct ChunkSpan {
    std::string m_type;
    std::size_t m_offset;
    std::size_t m_size;
  };

  NodeId make_id(std::uint32_t value) {
    auto bytes = std::array<std::uint8_t, NodeId::SIZE>();
    bytes[0] = 0xA5;
    for(auto i = std::size_t(0); i != 4; ++i) {
      bytes[NodeId::SIZE - 1 - i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    return NodeId(bytes);
  }

  NodeHeader make_header(std::uint32_t id, std::string name) {
    auto header = NodeHeader();
    header.m_id = make_id(id);
    header.m_name = std::move(name);
    return header;
  }

  Path make_path() {
    auto path = Path(FillRule::EVEN_ODD);
    path.move_to(Point(0.5, 1.25));
    path.line_to(Point(10, 0));
    path.quadratic_to(Point(12, 3), Point(14, 7.125));
    path.cubic_to(Point(1.0 / 3, 2), Point(4, 1e-7), Point(0, 10));
    path.close();
    path.line_to(Point(-3, -4));
    path.close();
    path.move_to(Point(100, 100));
    return path;
  }

  TextStyle make_text_style(std::string family, double size) {
    auto style = TextStyle();
    style.m_font = Font(family, "Medium", family + "-Medium", size);
    style.m_letter_spacing = 25;
    style.m_color = make_color(12, 34, 56, 0.75);
    return style;
  }

  Style make_style() {
    auto style = Style();
    style.m_fill = SolidFill(make_color(255, 0, 128), make_id(9101));
    auto stroke = Stroke();
    stroke.m_color = make_color(1, 2, 3, 0.5);
    stroke.m_width = 2.5;
    stroke.m_alignment = StrokeAlignment::OUTSIDE;
    stroke.m_cap = StrokeCap::SQUARE;
    stroke.m_join = StrokeJoin::BEVEL;
    stroke.m_miter_limit = 10;
    stroke.m_dashes = {4, 2.5, 0.1};
    stroke.m_is_enabled = false;
    style.m_stroke = stroke;
    style.m_effects.push_back(
      DropShadow(Point(1, 2), 3, make_color(0, 0, 0, 0.16), true));
    style.m_effects.push_back(
      InnerShadow(Point(-1, -2), 4.5, make_color(255, 255, 255), false));
    style.m_effects.push_back(Blur(8, -0.25, 0.4, true, false));
    style.m_effects.push_back(Blur(2, 0, 1, false, true));
    return style;
  }

  Text make_text() {
    auto text = Text();
    auto content = std::u16string(u"Hello ");
    content += char16_t(0xD83D);
    content += char16_t(0xDE00);
    content += u" world\nSecond";
    content += char16_t(0xD800);
    text.m_content = Shared(content);
    text.m_frame = AreaFrame(Size(200, 48.5));
    text.m_style = Style(SolidFill(make_color(0, 0, 0), NodeId()),
      std::nullopt, std::vector<Effect>());
    text.m_base_style = make_text_style("Roboto", 14);
    auto first = TextRun();
    first.m_length = 6;
    first.m_style = make_text_style("Roboto", 14);
    first.m_style.m_is_underlined = true;
    first.m_style.m_transform = TextTransform::TITLE_CASE;
    first.m_character_style = make_id(9201);
    auto second = TextRun();
    second.m_length = content.size() - 6;
    second.m_style = make_text_style("Open Sans", 18.5);
    second.m_style.m_is_struck_through = true;
    second.m_style.m_script = TextScript::SUBSCRIPT;
    second.m_style.m_transform = TextTransform::UPPERCASE;
    text.m_runs = {first, second};
    text.m_paragraph_style =
      ParagraphStyle(TextAlignment::CENTER, std::optional<double>(21), 4);
    auto layout = StoredLayout();
    auto& paragraph = layout.m_paragraphs.emplace_back();
    auto& line = paragraph.m_lines.emplace_back();
    line.m_runs.push_back(StoredRun(0, 6, Point(0, 14.5)));
    line.m_runs.push_back(StoredRun(6, 14, Point(40.25, 14.5),
      Font("Roboto", "Bold", "Roboto-Bold", 14), make_color(9, 9, 9)));
    auto& second_line = paragraph.m_lines.emplace_back();
    second_line.m_runs.push_back(StoredRun(15, 21, Point(0, 35.5),
      std::nullopt, make_color(1, 1, 1, 0.5)));
    layout.m_paragraphs.emplace_back();
    text.m_layout = Shared(layout);
    return text;
  }

  Overrides make_overrides() {
    auto overrides = Overrides();
    overrides.m_added = {make_id(53), make_id(77777)};
    overrides.m_removed = {make_id(102)};
    overrides.m_reordered.emplace(make_id(51), make_id(52));
    overrides.m_properties.emplace(make_id(51),
      std::vector<std::string>({"transform", "fill"}));
    overrides.m_properties.emplace(
      make_id(52), std::vector<std::string>({"fill"}));
    return overrides;
  }

  void build_sample(Document& document) {
    auto transaction = Transaction(document);
    auto bytes = std::vector<std::byte>();
    for(auto i = 0; i != 300; ++i) {
      bytes.push_back(static_cast<std::byte>(i * 7));
    }
    transaction.add_image(ImageResource(make_id(9001), "image/png",
      Shared(bytes)));
    transaction.add_image(ImageResource(make_id(9002), "image/jpeg",
      Shared(std::vector<std::byte>())));
    transaction.add_swatch(
      ColorSwatch(make_id(9101), "Brand", make_color(255, 0, 128)));
    transaction.add_swatch(ColorSwatch(make_id(9102), "", make_color(0, 0, 0),
      std::vector<GradientStop>({GradientStop(0, make_color(1, 2, 3)),
        GradientStop(0.35, make_color(4, 5, 6, 0.5))})));
    transaction.add_character_style(CharacterStyle(make_id(9201), "Body",
      make_text_style("Roboto", 14), std::optional<double>(20), 6));
    transaction.add_character_style(CharacterStyle(make_id(9202), "Title",
      make_text_style("Raleway", 32)));
    auto artboard = Artboard();
    artboard.m_frame = Rect(-100, 50.5, 375, 812);
    artboard.m_background = SolidFill(make_color(250, 250, 250), make_id(9101));
    artboard.m_viewport_height = 667;
    auto grid = LayoutGrid();
    grid.m_type = GridType::LAYOUT;
    grid.m_is_visible = true;
    grid.m_column_count = 12;
    grid.m_gutter = 16;
    grid.m_default_width = 375;
    grid.m_margins = Margins(1, 2, 3, 4);
    grid.m_column_color = make_color(255, 0, 0, 0.1);
    grid.m_row_color = make_color(0, 255, 0, 0.1);
    grid.m_square_size = Size(8, 8);
    grid.m_square_column_color = make_color(0, 0, 255, 0.2);
    grid.m_square_row_color = make_color(9, 9, 9, 0.2);
    artboard.m_layout_grid = grid;
    artboard.m_guides = {Guide(Orientation::HORIZONTAL, 100),
      Guide(Orientation::VERTICAL, 187.5)};
    auto header = make_header(1, "Home");
    header.m_is_marked_for_export = true;
    auto home = transaction.append(NodeId(), header, artboard);
    auto rectangle_header = make_header(2, "Rectangle 1");
    rectangle_header.m_transform = Matrix(1, 0, 0, 1, 20, 30.5);
    rectangle_header.m_opacity = 0.5;
    rectangle_header.m_blend_mode = BlendMode::MULTIPLY;
    rectangle_header.m_is_locked = true;
    rectangle_header.m_is_isolated = true;
    rectangle_header.m_constraints =
      Constraints(true, true, true, false, true, false, true, false);
    transaction.append(home, rectangle_header, Shape(RectangleShape(
      Rect(0, 0, 100, 50), CornerRadii(1, 2, 3, 4)), make_style()));
    auto ellipse_header = make_header(3, "Ellipse");
    ellipse_header.m_transform =
      Matrix(0.7071067811865476, 0.7071067811865476, -0.7071067811865476,
        0.7071067811865476, 5, 6);
    ellipse_header.m_is_visible = false;
    transaction.append(home, ellipse_header, Shape(EllipseShape(
      Point(10, 20), 30, 15), Style(GradientFill(LinearGradient(Point(0, 0),
        Point(1, 1)), std::vector<GradientStop>({GradientStop(0,
          make_color(0, 0, 0)), GradientStop(1, make_color(255, 255, 255))}),
        GradientUnits::OBJECT_BOUNDING_BOX, Matrix(2, 0, 0, 2, 0, 0)),
      std::nullopt, std::vector<Effect>())));
    transaction.append(home, make_header(4, "Line"), Shape(LineShape(
      Point(0, 0), Point(100, 0.001)), Style()));
    transaction.append(home, make_header(5, "Polygon"), Shape(PolygonShape(
      5, Size(40, 40), 2, std::vector<Point>({Point(0, 0), Point(40, 0),
        Point(20, 40)})), Style(ImageFill(make_id(9001), Size(640, 480),
          ScaleBehavior::COVER, Point(-3, 4), 1.5, 0.75, 1.25, true, false),
        std::nullopt, std::vector<Effect>())));
    transaction.append(home, make_header(6, "Path"), Shape(PathShape(
      Shared(make_path()), FillRule::EVEN_ODD), Style(GradientFill(
        RadialGradient(Point(0.5, 0.5), 0.75, Point(0.25, 0.4)),
        std::vector<GradientStop>({GradientStop(0.2, make_color(9, 8, 7))}),
        GradientUnits::USER_SPACE, Matrix()), std::nullopt,
      std::vector<Effect>())));
    auto compound = transaction.append(home, make_header(7, "Compound"),
      Shape(CompoundShape(CompoundOperation::SUBTRACT, Shared(make_path())),
        make_style()));
    transaction.append(compound, make_header(8, "Operand A"), Shape(
      RectangleShape(Rect(0, 0, 10, 10), CornerRadii(0, 0, 0, 0)), Style()));
    transaction.append(compound, make_header(9, "Operand B"), Shape(
      EllipseShape(Point(5, 5), 5, 5), Style()));
    transaction.append(home, make_header(10, "Area Text"), make_text());
    auto positioned = make_text();
    positioned.m_frame = PositionedFrame();
    positioned.m_layout = std::nullopt;
    positioned.m_paragraph_style.m_line_height = std::nullopt;
    transaction.append(home, make_header(11, ""), positioned);
    auto auto_height = Text();
    auto_height.m_content = Shared(std::u16string(u"Plain"));
    auto_height.m_frame = AutoHeightFrame(120.5);
    auto_height.m_paragraph_style.m_alignment = TextAlignment::RIGHT;
    transaction.append(home, make_header(12, "Auto"), auto_height);
    auto roles = Group();
    roles.m_mask = Mask(std::vector<MaskShape>({
      MaskShape(Matrix(1, 0, 0, 1, 3, 4), RectangleShape(
        Rect(0, 0, 50, 50), CornerRadii(5, 5, 5, 5))),
      MaskShape(Matrix(), PathShape(
        Shared(make_path()), FillRule::NON_ZERO))}));
    roles.m_repeat_grid =
      RepeatGrid(Size(100, 40), 10, 12.5, 3, 4, Size(320, 190));
    roles.m_scroll_group =
      ScrollGroup(ScrollDirection::PANNING, Size(300, 200), Point(0, -40));
    auto stack = Stack();
    stack.m_orientation = Orientation::HORIZONTAL;
    stack.m_spacing.emplace(make_id(14), 8);
    stack.m_spacing.emplace(make_id(15), 12.5);
    stack.m_padding = Margins(4, 8, 4, 8);
    stack.m_has_background = true;
    roles.m_stack = stack;
    roles.m_padding = Padding(Margins(10, 20, 30, 40), true, true);
    auto group = transaction.append(home, make_header(13, "Roles"), roles);
    transaction.append(group, make_header(14, "Cell"), Shape(RectangleShape(
      Rect(0, 0, 100, 40), CornerRadii(0, 0, 0, 0)), Style()));
    transaction.append(group, make_header(15, "Cell"), Shape(RectangleShape(
      Rect(0, 0, 100, 40), CornerRadii(0, 0, 0, 0)), Style()));
    auto second = Artboard();
    second.m_frame = Rect(500, 0, 1920, 1080);
    second.m_background = NoFill();
    auto second_artboard =
      transaction.append(NodeId(), make_header(30, "Desktop"), second);
    auto master = transaction.insert_root(make_header(100, "Button"), Group());
    transaction.append(master, make_header(101, "Background"), Shape(
      RectangleShape(Rect(0, 0, 120, 40), CornerRadii(8, 8, 8, 8)),
      make_style()));
    transaction.append(master, make_header(102, "Label"), make_text());
    auto state_tree =
      transaction.insert_root(make_header(110, "Button"), Group());
    transaction.append(state_tree, make_header(111, "Background"),
      Reference(make_id(101), std::nullopt, false));
    auto component = Component();
    component.m_id = make_id(9301);
    component.m_name = "Button";
    component.m_master = master;
    component.m_states.push_back(ComponentState(make_id(9311), "Hover",
      state_tree, std::map<NodeId, NodeId>({{make_id(111), make_id(101)}}),
      make_overrides()));
    component.m_default_state = make_id(9310);
    component.m_link =
      ComponentLink("library://button", "Design System", 1700000000123.0);
    auto instance_tree =
      transaction.insert_root(make_header(120, "Button"), Group());
    transaction.append(instance_tree, make_header(121, "Background"),
      Reference(make_id(101), Matrix(1, 0, 0, 1, 2, 2), false));
    auto instance = ComponentInstance();
    instance.m_component = make_id(9301);
    instance.m_state = make_id(9311);
    instance.m_size = Size(120, 40);
    instance.m_sync_map.emplace(make_id(51), make_id(101));
    instance.m_sync_map.emplace(make_id(52), make_id(102));
    instance.m_overrides = make_overrides();
    instance.m_states.push_back(ComponentState(make_id(9311), "Hover",
      instance_tree, std::map<NodeId, NodeId>({{make_id(121), make_id(111)}}),
      Overrides()));
    auto instance_group = Group();
    instance_group.m_component_instance = instance;
    instance_group.m_stack = Stack();
    auto instance_header = make_header(50, "Button");
    instance_header.m_transform = Matrix(1, 0, 0, 1, 40, 700);
    auto instance_node =
      transaction.append(home, instance_header, instance_group);
    auto reference_header = make_header(51, "Background");
    reference_header.m_opacity = 0;
    transaction.append(instance_node, reference_header,
      Reference(make_id(101), Matrix(1.5, 0, 0, 1.5, -1, -1), false));
    auto with_children = transaction.append(instance_node,
      make_header(52, "Label"), Reference(make_id(102), std::nullopt, true));
    transaction.append(with_children, make_header(53, "Extra"), Shape(
      EllipseShape(Point(0, 0), 1, 1), Style()));
    auto loose = Group();
    loose.m_mask = Mask();
    auto loose_group = transaction.append(NodeId(), make_header(40, "Loose"),
      loose);
    transaction.append(loose_group, make_header(41, "Inner"), Group());
    transaction.add_component(component);
    transaction.add_interaction(Interaction(make_id(9401), make_id(2),
      TapTrigger(), ArtboardTransition(second_artboard, Animation(
        Transition::SLIDE_LEFT, Easing::EASE_IN_OUT, 0.3), true)));
    transaction.add_interaction(Interaction(make_id(9402), make_id(3),
      HoverTrigger(), Overlay(second_artboard, Point(10, 20), Animation(
        Transition::DISSOLVE, Easing::BOUNCE, 0.6)), false));
    transaction.add_interaction(Interaction(make_id(9403), instance_node,
      DragTrigger(), StateTransition(make_id(9311), Animation(
        Transition::AUTO_ANIMATE, Easing::SNAP, 1)), true));
    transaction.add_interaction(Interaction(make_id(9404), make_id(4),
      TimeTrigger(2.5), PreviousArtboard(), true));
    transaction.add_interaction(Interaction(make_id(9405), make_id(5),
      KeyTrigger("Ctrl+A", KeyDevice::GAMEPAD), VideoPlayback(make_id(6),
        VideoCommand::STOP), true));
    transaction.add_interaction(Interaction(make_id(9406), state_tree,
      TapTrigger(), ArtboardTransition(home, Animation(), false), true));
    transaction.set_home_artboard(home);
    transaction.set_flows({Flow(home, "Main"), Flow(second_artboard, "")});
    transaction.commit();
    document.clear_history();
  }

  std::string to_bytes(const Document& document,
      const SaveOptions& options = SaveOptions()) {
    auto stream = std::ostringstream(std::ios::binary);
    save(document, stream, options);
    return stream.str();
  }

  void load_bytes(const std::string& bytes, Document& document) {
    auto stream = std::istringstream(bytes, std::ios::binary);
    load(stream, document);
  }

  bool is_empty(const Document& document) {
    return document.get_nodes().get_size() == 0 &&
      document.get_canvas().empty() &&
      document.get_resources() == Resources() &&
      document.get_prototype() == Prototype() && !document.can_undo();
  }

  void require_round_trip(const Document& document) {
    auto loaded = Document();
    load_bytes(to_bytes(document), loaded);
    auto differences = find_differences(document, loaded);
    for(auto& difference : differences) {
      MESSAGE(difference);
    }
    REQUIRE(differences.empty());
    REQUIRE(!loaded.can_undo());
  }

  void require_failure(const std::string& bytes) {
    auto document = Document();
    REQUIRE_THROWS_AS(load_bytes(bytes, document), FormatError);
    REQUIRE(is_empty(document));
  }

  void require_failure_or_load(const std::string& bytes) {
    auto document = Document();
    try {
      load_bytes(bytes, document);
    } catch(const FormatError&) {
      REQUIRE(is_empty(document));
    }
  }

  std::uint64_t read_integer(
      const std::string& bytes, std::size_t offset, std::size_t size) {
    auto value = std::uint64_t(0);
    for(auto i = std::size_t(0); i != size; ++i) {
      value |= static_cast<std::uint64_t>(
        static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    }
    return value;
  }

  void write_integer(std::string& bytes, std::size_t offset, std::size_t size,
      std::uint64_t value) {
    for(auto i = std::size_t(0); i != size; ++i) {
      bytes[offset + i] = static_cast<char>(value >> (8 * i));
    }
  }

  std::vector<ChunkSpan> list_chunks(const std::string& bytes) {
    auto chunks = std::vector<ChunkSpan>();
    auto offset = FILE_HEADER_SIZE;
    while(offset < bytes.size()) {
      auto size = static_cast<std::size_t>(read_integer(bytes, offset + 8, 8));
      chunks.push_back(ChunkSpan(bytes.substr(offset, 4), offset, size));
      offset += CHUNK_HEADER_SIZE + size;
    }
    return chunks;
  }

  void update_checksum(std::string& bytes, const ChunkSpan& chunk) {
    auto data = reinterpret_cast<const Bytef*>(bytes.data());
    auto checksum = crc32_z(0, data + chunk.m_offset, CHUNK_HEADER_SIZE - 4);
    checksum = crc32_z(
      checksum, data + chunk.m_offset + CHUNK_HEADER_SIZE, chunk.m_size);
    write_integer(bytes, chunk.m_offset + CHUNK_HEADER_SIZE - 4, 4, checksum);
  }

  std::string make_chunk(const std::string& type, const std::string& data) {
    auto bytes = type + std::string(CHUNK_HEADER_SIZE - 4, '\0') + data;
    write_integer(bytes, 8, 8, data.size());
    write_integer(bytes, 16, 8, data.size());
    update_checksum(bytes, ChunkSpan(type, 0, data.size()));
    return bytes;
  }

  bool is_same_bits(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
      std::bit_cast<std::uint64_t>(right);
  }
}

TEST_SUITE("DocumentFile") {
  TEST_CASE("empty_document") {
    auto document = Document();
    auto bytes = to_bytes(document);
    REQUIRE(bytes.size() < 256);
    require_round_trip(document);
  }

  TEST_CASE("sample_document") {
    auto document = Document();
    build_sample(document);
    require_round_trip(document);
  }

  TEST_CASE("node_ids") {
    auto document = Document();
    build_sample(document);
    auto loaded = Document();
    load_bytes(to_bytes(document), loaded);
    REQUIRE(loaded.get_canvas() == document.get_canvas());
    document.get_nodes().for_each([&] (const auto& node) {
      auto other = loaded.find(node.get_id());
      REQUIRE(other);
      REQUIRE(*other == node);
    });
    REQUIRE(loaded.get_nodes().get_size() == document.get_nodes().get_size());
    REQUIRE(loaded.get_resources() == document.get_resources());
    REQUIRE(loaded.get_prototype() == document.get_prototype());
  }

  TEST_CASE("image_bytes") {
    auto document = Document();
    build_sample(document);
    auto loaded = Document();
    load_bytes(to_bytes(document), loaded);
    auto& image = loaded.get_resources().m_images.at(make_id(9001));
    REQUIRE(image.m_media_type == "image/png");
    REQUIRE(image.m_data->size() == 300);
    REQUIRE((*image.m_data)[299] == static_cast<std::byte>(299 * 7));
    REQUIRE(loaded.get_resources().m_images.at(make_id(9002)).m_data->empty());
  }

  TEST_CASE("special_numbers") {
    auto document = Document();
    auto values = std::vector<double>({-0.0, 0.0,
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(), 0.1, 1.0 / 3, 128 / 255.0,
      0.30000001192092896, 9007199254740992.0, 9007199254740994.0,
      -1152921504606846976.0, 1e-300, 123.4567, -0.5,
      std::numeric_limits<double>::quiet_NaN()});
    {
      auto transaction = Transaction(document);
      for(auto i = std::size_t(0); i != values.size(); ++i) {
        auto header = make_header(static_cast<std::uint32_t>(i + 1), "");
        header.m_transform = Matrix(values[i], values[i], values[i],
          values[i], values[i], values[i]);
        header.m_opacity = values[i];
        transaction.append(NodeId(), header, Shape(LineShape(
          Point(values[i], values[i]), Point(0, 0)), Style()));
      }
      transaction.commit();
    }
    auto loaded = Document();
    load_bytes(to_bytes(document), loaded);
    for(auto i = std::size_t(0); i != values.size(); ++i) {
      auto& node = loaded.get(make_id(static_cast<std::uint32_t>(i + 1)));
      auto& transform = node.m_header.m_transform;
      REQUIRE(is_same_bits(transform.m_a, values[i]));
      REQUIRE(is_same_bits(transform.m_d, values[i]));
      REQUIRE(is_same_bits(transform.m_ty, values[i]));
      REQUIRE(is_same_bits(node.m_header.m_opacity, values[i]));
      auto& line = std::get<LineShape>(node.get_shape()->m_geometry);
      REQUIRE(is_same_bits(line.m_start.m_x, values[i]));
    }
  }

  TEST_CASE("multiple_blocks") {
    auto document = Document();
    {
      auto transaction = Transaction(document);
      auto id = std::uint32_t(1);
      for(auto i = 0; i != 4; ++i) {
        auto artboard = transaction.append(NodeId(), make_header(id++,
          "Artboard"), Artboard(Rect(i * 400, 0, 375, 812)));
        for(auto j = 0; j != 3000; ++j) {
          auto group = transaction.append(
            artboard, make_header(id++, "Group"), Group());
          transaction.append(group, make_header(id++, "Item"), Shape(
            RectangleShape(Rect(j, j, 10, 10), CornerRadii(0, 0, 0, 0)),
            Style()));
        }
      }
      auto parent = transaction.insert_root(make_header(id++, "Deep"), Group());
      for(auto i = 0; i != 2000; ++i) {
        parent = transaction.append(parent, make_header(id++, ""), Group());
      }
      transaction.commit();
    }
    REQUIRE(document.get_nodes().get_size() > 3 * 8192);
    require_round_trip(document);
  }

  TEST_CASE("file") {
    auto document = Document();
    build_sample(document);
    auto directory = std::filesystem::path(
      std::filesystem::temp_directory_path() / ("rellu_format_test_" +
        to_string(generate_node_id())));
    std::filesystem::create_directories(directory);
    auto path = directory / "sample.rellu";
    {
      auto file = std::ofstream(path, std::ios::binary);
      file << "previous contents";
    }
    save(document, path);
    REQUIRE(!std::filesystem::exists(directory / "sample.rellu.saving"));
    auto loaded = Document();
    load(path, loaded);
    REQUIRE(is_equal(document, loaded));
    auto missing = Document();
    REQUIRE_THROWS_AS(load(directory / "missing.rellu", missing), FormatError);
    std::filesystem::remove_all(directory);
  }

  TEST_CASE("compression_levels") {
    auto document = Document();
    build_sample(document);
    auto stored = to_bytes(document, SaveOptions(0, 1));
    auto compressed = to_bytes(document, SaveOptions(9, 0));
    REQUIRE(compressed.size() < stored.size());
    for(auto& bytes : {stored, compressed}) {
      auto loaded = Document();
      load_bytes(bytes, loaded);
      REQUIRE(is_equal(document, loaded));
    }
  }

  TEST_CASE("deterministic_output") {
    auto document = Document();
    build_sample(document);
    auto other = Document();
    build_sample(other);
    REQUIRE(to_bytes(document) == to_bytes(other));
    REQUIRE(to_bytes(document,
      SaveOptions(SaveOptions().m_compression_level, 1)) ==
      to_bytes(document));
  }

  TEST_CASE("single_transaction") {
    auto document = Document();
    build_sample(document);
    auto loaded = Document();
    auto count = 0;
    auto subscription = loaded.subscribe([&] (const auto& changes) {
      ++count;
      REQUIRE(changes.m_inserted.size() == document.get_nodes().get_size());
    });
    load_bytes(to_bytes(document), loaded);
    REQUIRE(count == 1);
    REQUIRE(!loaded.can_undo());
  }

  TEST_CASE("non_empty_document") {
    auto document = Document();
    build_sample(document);
    auto bytes = to_bytes(document);
    REQUIRE_THROWS_AS(load_bytes(bytes, document), std::invalid_argument);
    auto other = Document();
    build_sample(other);
    REQUIRE(is_equal(document, other));
  }

  TEST_CASE("version") {
    auto document = Document();
    build_sample(document);
    auto bytes = to_bytes(document);
    REQUIRE(read_integer(bytes, SIGNATURE_SIZE, 4) == FORMAT_VERSION);
    for(auto version : {std::uint64_t(0), std::uint64_t(FORMAT_VERSION + 1),
        std::uint64_t(0xFFFFFFFF)}) {
      auto future = bytes;
      write_integer(future, SIGNATURE_SIZE, 4, version);
      auto loaded = Document();
      try {
        load_bytes(future, loaded);
        FAIL("Version accepted.");
      } catch(const FormatError& error) {
        REQUIRE(std::string(error.what()).find("version") !=
          std::string::npos);
      }
      REQUIRE(is_empty(loaded));
    }
  }

  TEST_CASE("signature") {
    require_failure("");
    require_failure("PK\x03\x04 not a rellu document at all");
    auto document = Document();
    auto bytes = to_bytes(document);
    bytes[1] = 'X';
    require_failure(bytes);
  }

  TEST_CASE("ancillary_chunks") {
    auto document = Document();
    build_sample(document);
    auto bytes = to_bytes(document);
    auto chunks = list_chunks(bytes);
    REQUIRE(chunks.back().m_type == "END ");
    auto end = chunks.back().m_offset;
    auto extended = bytes.substr(0, end) +
      make_chunk("note", "written by a later version") + bytes.substr(end);
    auto loaded = Document();
    load_bytes(extended, loaded);
    REQUIRE(is_equal(document, loaded));
    auto critical = bytes.substr(0, end) + make_chunk("ZZZZ", "required") +
      bytes.substr(end);
    require_failure(critical);
  }

  TEST_CASE("truncation") {
    auto document = Document();
    build_sample(document);
    auto bytes = to_bytes(document);
    for(auto size = std::size_t(0); size != bytes.size(); ++size) {
      require_failure(bytes.substr(0, size));
    }
  }

  TEST_CASE("corruption") {
    auto document = Document();
    build_sample(document);
    auto bytes = to_bytes(document);
    for(auto i = std::size_t(0); i != bytes.size(); ++i) {
      auto corrupt = bytes;
      corrupt[i] = static_cast<char>(corrupt[i] ^ 0x5A);
      if(i >= SIGNATURE_SIZE + 4 && i < FILE_HEADER_SIZE) {
        auto loaded = Document();
        load_bytes(corrupt, loaded);
        REQUIRE(is_equal(document, loaded));
      } else {
        require_failure(corrupt);
      }
    }
  }

  TEST_CASE("corrupt_contents") {
    auto document = Document();
    build_sample(document);
    auto bytes = to_bytes(document, SaveOptions(0, 1));
    for(auto& chunk : list_chunks(bytes)) {
      if(chunk.m_type == "IMAG" || chunk.m_type == "IDS ") {
        continue;
      }
      auto start = chunk.m_offset + CHUNK_HEADER_SIZE;
      for(auto i = start; i != start + chunk.m_size; ++i) {
        for(auto value : {0x00, 0x80, 0xFF}) {
          auto corrupt = bytes;
          corrupt[i] = static_cast<char>(value);
          update_checksum(corrupt, chunk);
          require_failure_or_load(corrupt);
        }
      }
    }
  }

  TEST_CASE("enumerators") {
    auto document = Document();
    {
      auto transaction = Transaction(document);
      auto artboard = transaction.append(NodeId(), make_header(1, "Artboard"),
        Artboard(Rect(0, 0, 100, 100)));
      auto id = std::uint32_t(100);
      for(auto i = 0; i <= static_cast<int>(BlendMode::LUMINOSITY); ++i) {
        auto header = make_header(id++, "");
        header.m_blend_mode = static_cast<BlendMode>(i);
        auto style = make_style();
        style.m_stroke->m_alignment = static_cast<StrokeAlignment>(i % 3);
        style.m_stroke->m_cap = static_cast<StrokeCap>(i % 3);
        style.m_stroke->m_join = static_cast<StrokeJoin>((i + 1) % 3);
        transaction.append(artboard, header, Shape(PathShape(
          Shared(make_path()), static_cast<FillRule>(i % 2)), style));
      }
      for(auto i = 0; i != 4; ++i) {
        auto text = make_text();
        text.m_runs[0].m_style.m_transform = static_cast<TextTransform>(i);
        text.m_runs[1].m_style.m_script = static_cast<TextScript>(i % 3);
        text.m_paragraph_style.m_alignment =
          static_cast<TextAlignment>(i % 3);
        transaction.append(artboard, make_header(id++, ""), text);
      }
      for(auto i = 0; i != 4; ++i) {
        auto group = Group();
        group.m_scroll_group = ScrollGroup(
          static_cast<ScrollDirection>(i % 3), Size(1, 1), Point(0, 0));
        group.m_stack = Stack(static_cast<Orientation>(i % 2));
        transaction.append(artboard, make_header(id++, ""), group);
        transaction.append(artboard, make_header(id++, ""),
          Shape(CompoundShape(static_cast<CompoundOperation>(i),
            Shared(make_path())), Style()));
      }
      for(auto i = 0; i <= static_cast<int>(Transition::AUTO_ANIMATE); ++i) {
        transaction.add_interaction(Interaction(make_id(id++), artboard,
          KeyTrigger("K", static_cast<KeyDevice>(i % 2)),
          ArtboardTransition(artboard, Animation(static_cast<Transition>(i),
            static_cast<Easing>(i % 7), i), false)));
        transaction.add_interaction(Interaction(make_id(id++), artboard,
          TapTrigger(), VideoPlayback(artboard,
            static_cast<VideoCommand>(i % 3))));
      }
      transaction.commit();
    }
    require_round_trip(document);
  }

  TEST_CASE("detached_roots") {
    auto document = Document();
    {
      auto transaction = Transaction(document);
      transaction.insert_root(make_header(1, "Orphan"), Group());
      transaction.append(NodeId(), make_header(2, "Visible"), Group());
      transaction.commit();
    }
    require_round_trip(document);
  }
}
