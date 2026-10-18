#include <bit>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include "ByteReader.hpp"
#include "ByteWriter.hpp"
#include "Codec.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
  enum class MatrixForm : std::uint8_t {
    IDENTITY,
    TRANSLATION,
    GENERAL
  };

  constexpr auto IS_VISIBLE = std::uint16_t(1) << 0;
  constexpr auto IS_LOCKED = std::uint16_t(1) << 1;
  constexpr auto IS_ISOLATED = std::uint16_t(1) << 2;
  constexpr auto IS_MARKED_FOR_EXPORT = std::uint16_t(1) << 3;
  constexpr auto HAS_NAME = std::uint16_t(1) << 4;
  constexpr auto HAS_OPACITY = std::uint16_t(1) << 5;
  constexpr auto HAS_BLEND_MODE = std::uint16_t(1) << 6;
  constexpr auto HAS_CONSTRAINTS = std::uint16_t(1) << 7;
  constexpr auto FORM_SHIFT = 8;
  constexpr auto KIND_SHIFT = 10;
  constexpr auto RESERVED_FLAGS = std::uint16_t(0xE000);

  constexpr auto HAS_MASK = std::uint8_t(1) << 0;
  constexpr auto HAS_REPEAT_GRID = std::uint8_t(1) << 1;
  constexpr auto HAS_SCROLL_GROUP = std::uint8_t(1) << 2;
  constexpr auto HAS_STACK = std::uint8_t(1) << 3;
  constexpr auto HAS_PADDING = std::uint8_t(1) << 4;
  constexpr auto HAS_COMPONENT_INSTANCE = std::uint8_t(1) << 5;

  bool is_same(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
      std::bit_cast<std::uint64_t>(right);
  }

  MatrixForm get_form(const Matrix& matrix) {
    if(is_same(matrix.m_a, 1) && is_same(matrix.m_b, 0) &&
        is_same(matrix.m_c, 0) && is_same(matrix.m_d, 1)) {
      if(is_same(matrix.m_tx, 0) && is_same(matrix.m_ty, 0)) {
        return MatrixForm::IDENTITY;
      }
      return MatrixForm::TRANSLATION;
    }
    return MatrixForm::GENERAL;
  }

  std::uint8_t to_bits(const Constraints& constraints) {
    auto bits = std::uint8_t(0);
    auto set = [&] (bool value, int bit) {
      if(value) {
        bits |= static_cast<std::uint8_t>(1 << bit);
      }
    };
    set(constraints.m_is_enabled, 0);
    set(constraints.m_is_custom, 1);
    set(constraints.m_left, 2);
    set(constraints.m_right, 3);
    set(constraints.m_top, 4);
    set(constraints.m_bottom, 5);
    set(constraints.m_width, 6);
    set(constraints.m_height, 7);
    return bits;
  }

  Constraints to_constraints(std::uint8_t bits) {
    auto test = [&] (int bit) {
      return (bits & (1 << bit)) != 0;
    };
    return Constraints(test(0), test(1), test(2), test(3), test(4), test(5),
      test(6), test(7));
  }

  std::size_t get_point_count(SegmentType type) {
    if(type == SegmentType::QUADRATIC) {
      return 2;
    } else if(type == SegmentType::CUBIC) {
      return 3;
    } else if(type == SegmentType::CLOSE) {
      return 0;
    }
    return 1;
  }

  class Encoder {
    public:
      explicit Encoder(const IdIndex& ids);

      ByteWriter& get_writer();
      std::vector<std::byte> release();
      void write_reference(const NodeId& id);
      void write_text(std::string_view value);
      void write_matrix_values(const Matrix& matrix, MatrixForm form);
      void write_node(const Node& node);
      void write(const Point& point);
      void write(const Size& size);
      void write(const Rect& rect);
      void write(const Matrix& matrix);
      void write(const Color& color);
      void write(const CornerRadii& radii);
      void write(const Margins& margins);
      void write(const GradientStop& stop);
      void write(const LinearGradient& gradient);
      void write(const RadialGradient& gradient);
      void write(const Path& path);
      void write(const SolidFill& fill);
      void write(const GradientFill& fill);
      void write(const ImageFill& fill);
      void write(const Stroke& stroke);
      void write(const DropShadow& shadow);
      void write(const InnerShadow& shadow);
      void write(const Blur& blur);
      void write(const Style& style);
      void write(const RectangleShape& shape);
      void write(const EllipseShape& shape);
      void write(const LineShape& shape);
      void write(const PolygonShape& shape);
      void write(const PathShape& shape);
      void write(const CompoundShape& shape);
      void write(const Font& font);
      void write(const TextStyle& style);
      void write(const TextRun& run);
      void write(const AreaFrame& frame);
      void write(const AutoHeightFrame& frame);
      void write(const ParagraphStyle& style);
      void write(const StoredRun& run);
      void write(const StoredLayout& layout);
      void write(const Text& text);
      void write(const LayoutGrid& grid);
      void write(const Guide& guide);
      void write(const Artboard& artboard);
      void write(const MaskShape& shape);
      void write(const Mask& mask);
      void write(const RepeatGrid& grid);
      void write(const ScrollGroup& group);
      void write(const Stack& stack);
      void write(const Padding& padding);
      void write(const Overrides& overrides);
      void write(const ComponentState& state);
      void write(const ComponentInstance& instance);
      void write(const Group& group);
      void write(const Shape& shape);
      void write(const Reference& reference);
      void write(const Component& component);
      void write(const ColorSwatch& swatch);
      void write(const CharacterStyle& style);
      void write(const ImageResource& image);
      void write(const TimeTrigger& trigger);
      void write(const KeyTrigger& trigger);
      void write(const Animation& animation);
      void write(const ArtboardTransition& action);
      void write(const Overlay& action);
      void write(const StateTransition& action);
      void write(const VideoPlayback& action);
      void write(const Interaction& interaction);
      void write(const Flow& flow);
      void write(const Prototype& prototype);
      template<typename T> requires std::is_empty_v<T>
      void write(const T&);
      template<typename... T>
      void write(const std::variant<T...>& value);
      void write_map(const std::map<NodeId, NodeId>& map);
      void write_count(std::size_t count);
      void write_enum(auto value);

    private:
      ByteWriter m_writer;
      const IdIndex* m_ids;
      std::unordered_map<std::string_view, std::uint32_t> m_strings;
  };

  class Decoder {
    public:
      Decoder(std::span<const std::byte> data, std::span<const NodeId> ids,
        std::uint32_t version, std::string_view context) noexcept;

      ByteReader& get_reader();
      NodeId read_reference();
      std::string read_text();
      void read_matrix_values(Matrix& matrix, MatrixForm form);
      NodeRecord read_node(const NodeId& id, std::size_t node_count);
      void read(Point& point);
      void read(Size& size);
      void read(Rect& rect);
      void read(Matrix& matrix);
      void read(Color& color);
      void read(CornerRadii& radii);
      void read(Margins& margins);
      void read(GradientStop& stop);
      void read(LinearGradient& gradient);
      void read(RadialGradient& gradient);
      void read(Path& path);
      void read(SolidFill& fill);
      void read(GradientFill& fill);
      void read(ImageFill& fill);
      void read(Stroke& stroke);
      void read(DropShadow& shadow);
      void read(InnerShadow& shadow);
      void read(Blur& blur);
      void read(Style& style);
      void read(RectangleShape& shape);
      void read(EllipseShape& shape);
      void read(LineShape& shape);
      void read(PolygonShape& shape);
      void read(PathShape& shape);
      void read(CompoundShape& shape);
      void read(Font& font);
      void read(TextStyle& style);
      void read(TextRun& run);
      void read(AreaFrame& frame);
      void read(AutoHeightFrame& frame);
      void read(ParagraphStyle& style);
      void read(StoredRun& run);
      void read(StoredLayout& layout);
      void read(Text& text);
      void read(LayoutGrid& grid);
      void read(Guide& guide);
      void read(Artboard& artboard);
      void read(MaskShape& shape);
      void read(Mask& mask);
      void read(RepeatGrid& grid);
      void read(ScrollGroup& group);
      void read(Stack& stack);
      void read(Padding& padding);
      void read(Overrides& overrides);
      void read(ComponentState& state);
      void read(ComponentInstance& instance);
      void read(Group& group);
      void read(Shape& shape);
      void read(Reference& reference);
      void read(Component& component);
      void read(ColorSwatch& swatch);
      void read(CharacterStyle& style);
      void read(ImageEntry& image);
      void read(TimeTrigger& trigger);
      void read(KeyTrigger& trigger);
      void read(Animation& animation);
      void read(ArtboardTransition& action);
      void read(Overlay& action);
      void read(StateTransition& action);
      void read(VideoPlayback& action);
      void read(Interaction& interaction);
      void read(Flow& flow);
      void read(Prototype& prototype);
      template<typename T> requires std::is_empty_v<T>
      void read(T&);
      template<typename... T>
      void read(std::variant<T...>& value);
      void read_map(std::map<NodeId, NodeId>& map);
      std::size_t read_count();
      template<typename E>
      void read_enum(E& value, E last);

    private:
      ByteReader m_reader;
      std::span<const NodeId> m_ids;
      [[maybe_unused]] std::uint32_t m_version;
      std::vector<std::string> m_strings;

      template<typename V, std::size_t... I>
      void read_alternative(
        V& value, std::size_t index, std::index_sequence<I...>);
  };

  Encoder::Encoder(const IdIndex& ids)
    : m_ids(&ids) {}

  ByteWriter& Encoder::get_writer() {
    return m_writer;
  }

  std::vector<std::byte> Encoder::release() {
    return m_writer.release();
  }

  void Encoder::write_reference(const NodeId& id) {
    if(id.is_nil()) {
      m_writer.write_byte(0);
      return;
    }
    if(auto index = m_ids->find(id)) {
      m_writer.write_varint(std::uint64_t(*index) + 2);
      return;
    }
    m_writer.write_byte(1);
    m_writer.write_bytes(id.get_bytes().data(), NodeId::SIZE);
  }

  void Encoder::write_text(std::string_view value) {
    auto entry = m_strings.try_emplace(
      value, static_cast<std::uint32_t>(m_strings.size()));
    if(entry.second) {
      m_writer.write_byte(0);
      m_writer.write_string(value);
    } else {
      m_writer.write_varint(std::uint64_t(entry.first->second) + 1);
    }
  }

  void Encoder::write_matrix_values(const Matrix& matrix, MatrixForm form) {
    if(form == MatrixForm::GENERAL) {
      m_writer.write_number(matrix.m_a);
      m_writer.write_number(matrix.m_b);
      m_writer.write_number(matrix.m_c);
      m_writer.write_number(matrix.m_d);
    }
    if(form != MatrixForm::IDENTITY) {
      m_writer.write_number(matrix.m_tx);
      m_writer.write_number(matrix.m_ty);
    }
  }

  void Encoder::write_node(const Node& node) {
    auto& header = node.m_header;
    auto form = get_form(header.m_transform);
    auto constraints = to_bits(header.m_constraints);
    auto flags = std::uint16_t(0);
    auto set = [&] (bool value, std::uint16_t flag) {
      if(value) {
        flags |= flag;
      }
    };
    set(header.m_is_visible, IS_VISIBLE);
    set(header.m_is_locked, IS_LOCKED);
    set(header.m_is_isolated, IS_ISOLATED);
    set(header.m_is_marked_for_export, IS_MARKED_FOR_EXPORT);
    set(!header.m_name.empty(), HAS_NAME);
    set(!is_same(header.m_opacity, 1), HAS_OPACITY);
    set(header.m_blend_mode != BlendMode::NORMAL, HAS_BLEND_MODE);
    set(constraints != 0, HAS_CONSTRAINTS);
    flags |= static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(form) << FORM_SHIFT);
    flags |= static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(node.get_kind()) << KIND_SHIFT);
    m_writer.write_u16(flags);
    m_writer.write_varint(node.m_children.size());
    if(flags & HAS_NAME) {
      write_text(header.m_name);
    }
    write_matrix_values(header.m_transform, form);
    if(flags & HAS_OPACITY) {
      m_writer.write_number(header.m_opacity);
    }
    if(flags & HAS_BLEND_MODE) {
      write_enum(header.m_blend_mode);
    }
    if(flags & HAS_CONSTRAINTS) {
      m_writer.write_byte(constraints);
    }
    std::visit([&] (const auto& payload) { write(payload); }, node.m_payload);
  }

  void Encoder::write(const Point& point) {
    m_writer.write_number(point.m_x);
    m_writer.write_number(point.m_y);
  }

  void Encoder::write(const Size& size) {
    m_writer.write_number(size.m_width);
    m_writer.write_number(size.m_height);
  }

  void Encoder::write(const Rect& rect) {
    m_writer.write_number(rect.m_x);
    m_writer.write_number(rect.m_y);
    m_writer.write_number(rect.m_width);
    m_writer.write_number(rect.m_height);
  }

  void Encoder::write(const Matrix& matrix) {
    auto form = get_form(matrix);
    write_enum(form);
    write_matrix_values(matrix, form);
  }

  void Encoder::write(const Color& color) {
    m_writer.write_number(color.m_red);
    m_writer.write_number(color.m_green);
    m_writer.write_number(color.m_blue);
    m_writer.write_number(color.m_alpha);
  }

  void Encoder::write(const CornerRadii& radii) {
    m_writer.write_number(radii.m_top_left);
    m_writer.write_number(radii.m_top_right);
    m_writer.write_number(radii.m_bottom_right);
    m_writer.write_number(radii.m_bottom_left);
  }

  void Encoder::write(const Margins& margins) {
    m_writer.write_number(margins.m_top);
    m_writer.write_number(margins.m_right);
    m_writer.write_number(margins.m_bottom);
    m_writer.write_number(margins.m_left);
  }

  void Encoder::write(const GradientStop& stop) {
    m_writer.write_number(stop.m_offset);
    write(stop.m_color);
  }

  void Encoder::write(const LinearGradient& gradient) {
    write(gradient.m_start);
    write(gradient.m_end);
  }

  void Encoder::write(const RadialGradient& gradient) {
    write(gradient.m_center);
    m_writer.write_number(gradient.m_radius);
    write(gradient.m_focus);
  }

  void Encoder::write(const Path& path) {
    write_enum(path.get_fill_rule());
    auto& segments = path.get_segments();
    write_count(segments.size());
    for(auto& segment : segments) {
      write_enum(segment.m_type);
      auto count = get_point_count(segment.m_type);
      for(auto i = std::size_t(0); i != count; ++i) {
        write(segment.m_points[i]);
      }
    }
  }

  void Encoder::write(const SolidFill& fill) {
    write(fill.m_color);
    write_reference(fill.m_swatch);
  }

  void Encoder::write(const GradientFill& fill) {
    write(fill.m_geometry);
    write_count(fill.m_stops.size());
    for(auto& stop : fill.m_stops) {
      write(stop);
    }
    write_enum(fill.m_units);
    write(fill.m_transform);
  }

  void Encoder::write(const ImageFill& fill) {
    write_reference(fill.m_image);
    write(fill.m_size);
    write_enum(fill.m_scale_behavior);
    write(fill.m_offset);
    m_writer.write_number(fill.m_scale);
    m_writer.write_number(fill.m_horizontal_scale);
    m_writer.write_number(fill.m_vertical_scale);
    m_writer.write_byte(static_cast<std::uint8_t>(
      (fill.m_is_flipped_horizontally ? 1 : 0) |
      (fill.m_is_flipped_vertically ? 2 : 0)));
  }

  void Encoder::write(const Stroke& stroke) {
    write(stroke.m_color);
    m_writer.write_number(stroke.m_width);
    write_enum(stroke.m_alignment);
    write_enum(stroke.m_cap);
    write_enum(stroke.m_join);
    m_writer.write_number(stroke.m_miter_limit);
    write_count(stroke.m_dashes.size());
    for(auto dash : stroke.m_dashes) {
      m_writer.write_number(dash);
    }
    m_writer.write_bool(stroke.m_is_enabled);
  }

  void Encoder::write(const DropShadow& shadow) {
    write(shadow.m_offset);
    m_writer.write_number(shadow.m_blur);
    write(shadow.m_color);
    m_writer.write_bool(shadow.m_is_visible);
  }

  void Encoder::write(const InnerShadow& shadow) {
    write(shadow.m_offset);
    m_writer.write_number(shadow.m_blur);
    write(shadow.m_color);
    m_writer.write_bool(shadow.m_is_visible);
  }

  void Encoder::write(const Blur& blur) {
    m_writer.write_number(blur.m_amount);
    m_writer.write_number(blur.m_brightness);
    m_writer.write_number(blur.m_fill_opacity);
    m_writer.write_byte(static_cast<std::uint8_t>(
      (blur.m_is_background ? 1 : 0) | (blur.m_is_visible ? 2 : 0)));
  }

  void Encoder::write(const Style& style) {
    write(style.m_fill);
    m_writer.write_bool(static_cast<bool>(style.m_stroke));
    if(style.m_stroke) {
      write(*style.m_stroke);
    }
    write_count(style.m_effects.size());
    for(auto& effect : style.m_effects) {
      write(effect);
    }
  }

  void Encoder::write(const RectangleShape& shape) {
    write(shape.m_bounds);
    write(shape.m_radii);
  }

  void Encoder::write(const EllipseShape& shape) {
    write(shape.m_center);
    m_writer.write_number(shape.m_x_radius);
    m_writer.write_number(shape.m_y_radius);
  }

  void Encoder::write(const LineShape& shape) {
    write(shape.m_start);
    write(shape.m_end);
  }

  void Encoder::write(const PolygonShape& shape) {
    m_writer.write_signed(shape.m_corner_count);
    write(shape.m_size);
    m_writer.write_number(shape.m_corner_radius);
    write_count(shape.m_vertices.size());
    for(auto& vertex : shape.m_vertices) {
      write(vertex);
    }
  }

  void Encoder::write(const PathShape& shape) {
    write(*shape.m_path);
    write_enum(shape.m_fill_rule);
  }

  void Encoder::write(const CompoundShape& shape) {
    write_enum(shape.m_operation);
    write(*shape.m_path);
  }

  void Encoder::write(const Font& font) {
    write_text(font.m_family);
    write_text(font.m_style);
    write_text(font.m_postscript_name);
    m_writer.write_number(font.m_size);
  }

  void Encoder::write(const TextStyle& style) {
    write(style.m_font);
    m_writer.write_number(style.m_letter_spacing);
    write(style.m_color);
    m_writer.write_byte(static_cast<std::uint8_t>(
      (style.m_is_underlined ? 1 : 0) | (style.m_is_struck_through ? 2 : 0) |
      (static_cast<int>(style.m_transform) << 2) |
      (static_cast<int>(style.m_script) << 4)));
  }

  void Encoder::write(const TextRun& run) {
    m_writer.write_varint(run.m_length);
    write(run.m_style);
    write_reference(run.m_character_style);
  }

  void Encoder::write(const AreaFrame& frame) {
    write(frame.m_size);
  }

  void Encoder::write(const AutoHeightFrame& frame) {
    m_writer.write_number(frame.m_width);
  }

  void Encoder::write(const ParagraphStyle& style) {
    write_enum(style.m_alignment);
    m_writer.write_bool(static_cast<bool>(style.m_line_height));
    if(style.m_line_height) {
      m_writer.write_number(*style.m_line_height);
    }
    m_writer.write_number(style.m_paragraph_spacing);
  }

  void Encoder::write(const StoredRun& run) {
    m_writer.write_varint(run.m_from);
    m_writer.write_varint(run.m_to);
    write(run.m_position);
    m_writer.write_byte(static_cast<std::uint8_t>(
      (run.m_font ? 1 : 0) | (run.m_color ? 2 : 0)));
    if(run.m_font) {
      write(*run.m_font);
    }
    if(run.m_color) {
      write(*run.m_color);
    }
  }

  void Encoder::write(const StoredLayout& layout) {
    write_count(layout.m_paragraphs.size());
    for(auto& paragraph : layout.m_paragraphs) {
      write_count(paragraph.m_lines.size());
      for(auto& line : paragraph.m_lines) {
        write_count(line.m_runs.size());
        for(auto& run : line.m_runs) {
          write(run);
        }
      }
    }
  }

  void Encoder::write(const Text& text) {
    auto& content = *text.m_content;
    write_count(content.size());
    if constexpr(std::endian::native == std::endian::little) {
      m_writer.write_bytes(content.data(), content.size() * 2);
    } else {
      for(auto unit : content) {
        m_writer.write_u16(static_cast<std::uint16_t>(unit));
      }
    }
    write(text.m_frame);
    write(text.m_style);
    write(text.m_base_style);
    write_count(text.m_runs.size());
    for(auto& run : text.m_runs) {
      write(run);
    }
    write(text.m_paragraph_style);
    m_writer.write_bool(static_cast<bool>(text.m_layout));
    if(text.m_layout) {
      write(**text.m_layout);
    }
  }

  void Encoder::write(const LayoutGrid& grid) {
    write_enum(grid.m_type);
    m_writer.write_bool(grid.m_is_visible);
    m_writer.write_signed(grid.m_column_count);
    m_writer.write_number(grid.m_gutter);
    m_writer.write_number(grid.m_default_width);
    write(grid.m_margins);
    write(grid.m_column_color);
    write(grid.m_row_color);
    write(grid.m_square_size);
    write(grid.m_square_column_color);
    write(grid.m_square_row_color);
  }

  void Encoder::write(const Guide& guide) {
    write_enum(guide.m_orientation);
    m_writer.write_number(guide.m_position);
  }

  void Encoder::write(const Artboard& artboard) {
    write(artboard.m_frame);
    write(artboard.m_background);
    m_writer.write_byte(static_cast<std::uint8_t>(
      (artboard.m_viewport_height ? 1 : 0) |
      (artboard.m_layout_grid ? 2 : 0)));
    if(artboard.m_viewport_height) {
      m_writer.write_number(*artboard.m_viewport_height);
    }
    if(artboard.m_layout_grid) {
      write(*artboard.m_layout_grid);
    }
    write_count(artboard.m_guides.size());
    for(auto& guide : artboard.m_guides) {
      write(guide);
    }
  }

  void Encoder::write(const MaskShape& shape) {
    write(shape.m_transform);
    write(shape.m_geometry);
  }

  void Encoder::write(const Mask& mask) {
    write_count(mask.m_shapes.size());
    for(auto& shape : mask.m_shapes) {
      write(shape);
    }
  }

  void Encoder::write(const RepeatGrid& grid) {
    write(grid.m_cell_size);
    m_writer.write_number(grid.m_column_spacing);
    m_writer.write_number(grid.m_row_spacing);
    m_writer.write_signed(grid.m_column_count);
    m_writer.write_signed(grid.m_row_count);
    write(grid.m_size);
  }

  void Encoder::write(const ScrollGroup& group) {
    write_enum(group.m_direction);
    write(group.m_viewport);
    write(group.m_offset);
  }

  void Encoder::write(const Stack& stack) {
    write_enum(stack.m_orientation);
    write_count(stack.m_spacing.size());
    for(auto& entry : stack.m_spacing) {
      write_reference(entry.first);
      m_writer.write_number(entry.second);
    }
    write(stack.m_padding);
    m_writer.write_bool(stack.m_has_background);
  }

  void Encoder::write(const Padding& padding) {
    write(padding.m_padding);
    m_writer.write_byte(static_cast<std::uint8_t>(
      (padding.m_is_content_aware ? 1 : 0) |
      (padding.m_has_background ? 2 : 0)));
  }

  void Encoder::write(const Overrides& overrides) {
    write_count(overrides.m_added.size());
    for(auto& id : overrides.m_added) {
      write_reference(id);
    }
    write_count(overrides.m_removed.size());
    for(auto& id : overrides.m_removed) {
      write_reference(id);
    }
    write_map(overrides.m_reordered);
    write_count(overrides.m_properties.size());
    for(auto& entry : overrides.m_properties) {
      write_reference(entry.first);
      write_count(entry.second.size());
      for(auto& property : entry.second) {
        write_text(property);
      }
    }
  }

  void Encoder::write(const ComponentState& state) {
    write_reference(state.m_id);
    write_text(state.m_name);
    write_reference(state.m_tree);
    write_map(state.m_sync_map);
    write(state.m_overrides);
  }

  void Encoder::write(const ComponentInstance& instance) {
    write_reference(instance.m_component);
    write_reference(instance.m_state);
    write(instance.m_size);
    write_map(instance.m_sync_map);
    write(instance.m_overrides);
    write_count(instance.m_states.size());
    for(auto& state : instance.m_states) {
      write(state);
    }
  }

  void Encoder::write(const Group& group) {
    auto roles = std::uint8_t(0);
    auto set = [&] (const auto& role, std::uint8_t flag) {
      if(role) {
        roles |= flag;
      }
    };
    set(group.m_mask, HAS_MASK);
    set(group.m_repeat_grid, HAS_REPEAT_GRID);
    set(group.m_scroll_group, HAS_SCROLL_GROUP);
    set(group.m_stack, HAS_STACK);
    set(group.m_padding, HAS_PADDING);
    set(group.m_component_instance, HAS_COMPONENT_INSTANCE);
    m_writer.write_byte(roles);
    if(group.m_mask) {
      write(*group.m_mask);
    }
    if(group.m_repeat_grid) {
      write(*group.m_repeat_grid);
    }
    if(group.m_scroll_group) {
      write(*group.m_scroll_group);
    }
    if(group.m_stack) {
      write(*group.m_stack);
    }
    if(group.m_padding) {
      write(*group.m_padding);
    }
    if(group.m_component_instance) {
      write(*group.m_component_instance);
    }
  }

  void Encoder::write(const Shape& shape) {
    write(shape.m_geometry);
    write(shape.m_style);
  }

  void Encoder::write(const Reference& reference) {
    write_reference(reference.m_source);
    m_writer.write_byte(static_cast<std::uint8_t>(
      (reference.m_transform ? 1 : 0) | (reference.m_has_children ? 2 : 0)));
    if(reference.m_transform) {
      write(*reference.m_transform);
    }
  }

  void Encoder::write(const Component& component) {
    write_reference(component.m_id);
    write_text(component.m_name);
    write_reference(component.m_master);
    write_count(component.m_states.size());
    for(auto& state : component.m_states) {
      write(state);
    }
    write_reference(component.m_default_state);
    m_writer.write_bool(static_cast<bool>(component.m_link));
    if(component.m_link) {
      write_text(component.m_link->m_reference);
      write_text(component.m_link->m_source_name);
      m_writer.write_number(component.m_link->m_last_modified);
    }
  }

  void Encoder::write(const ColorSwatch& swatch) {
    write_reference(swatch.m_id);
    write_text(swatch.m_name);
    write(swatch.m_color);
    write_count(swatch.m_gradient.size());
    for(auto& stop : swatch.m_gradient) {
      write(stop);
    }
  }

  void Encoder::write(const CharacterStyle& style) {
    write_reference(style.m_id);
    write_text(style.m_name);
    write(style.m_style);
    m_writer.write_bool(static_cast<bool>(style.m_line_height));
    if(style.m_line_height) {
      m_writer.write_number(*style.m_line_height);
    }
    m_writer.write_number(style.m_paragraph_spacing);
  }

  void Encoder::write(const ImageResource& image) {
    write_reference(image.m_id);
    write_text(image.m_media_type);
    m_writer.write_varint(image.m_data->size());
  }

  void Encoder::write(const TimeTrigger& trigger) {
    m_writer.write_number(trigger.m_delay);
  }

  void Encoder::write(const KeyTrigger& trigger) {
    write_text(trigger.m_keys);
    write_enum(trigger.m_device);
  }

  void Encoder::write(const Animation& animation) {
    write_enum(animation.m_transition);
    write_enum(animation.m_easing);
    m_writer.write_number(animation.m_duration);
  }

  void Encoder::write(const ArtboardTransition& action) {
    write_reference(action.m_destination);
    write(action.m_animation);
    m_writer.write_bool(action.m_preserve_scroll);
  }

  void Encoder::write(const Overlay& action) {
    write_reference(action.m_destination);
    write(action.m_position);
    write(action.m_animation);
  }

  void Encoder::write(const StateTransition& action) {
    write_reference(action.m_state);
    write(action.m_animation);
  }

  void Encoder::write(const VideoPlayback& action) {
    write_reference(action.m_target);
    write_enum(action.m_command);
  }

  void Encoder::write(const Interaction& interaction) {
    write_reference(interaction.m_id);
    write_reference(interaction.m_owner);
    write(interaction.m_trigger);
    write(interaction.m_action);
    m_writer.write_bool(interaction.m_is_enabled);
  }

  void Encoder::write(const Flow& flow) {
    write_reference(flow.m_artboard);
    write_text(flow.m_name);
  }

  void Encoder::write(const Prototype& prototype) {
    write_count(prototype.m_interactions.size());
    for(auto& interaction : prototype.m_interactions) {
      write(interaction);
    }
    write_reference(prototype.m_home_artboard);
    write_count(prototype.m_flows.size());
    for(auto& flow : prototype.m_flows) {
      write(flow);
    }
  }

  template<typename T> requires std::is_empty_v<T>
  void Encoder::write(const T&) {}

  template<typename... T>
  void Encoder::write(const std::variant<T...>& value) {
    m_writer.write_byte(static_cast<std::uint8_t>(value.index()));
    std::visit([&] (const auto& alternative) { write(alternative); }, value);
  }

  void Encoder::write_map(const std::map<NodeId, NodeId>& map) {
    write_count(map.size());
    for(auto& entry : map) {
      write_reference(entry.first);
      write_reference(entry.second);
    }
  }

  void Encoder::write_count(std::size_t count) {
    m_writer.write_varint(count);
  }

  void Encoder::write_enum(auto value) {
    m_writer.write_byte(static_cast<std::uint8_t>(value));
  }

  Decoder::Decoder(std::span<const std::byte> data,
    std::span<const NodeId> ids, std::uint32_t version,
    std::string_view context) noexcept
    : m_reader(data, context),
      m_ids(ids),
      m_version(version) {}

  ByteReader& Decoder::get_reader() {
    return m_reader;
  }

  NodeId Decoder::read_reference() {
    auto value = m_reader.read_varint();
    if(value == 0) {
      return NodeId();
    } else if(value == 1) {
      auto bytes = m_reader.read_bytes(NodeId::SIZE);
      auto id = std::array<std::uint8_t, NodeId::SIZE>();
      std::memcpy(id.data(), bytes.data(), NodeId::SIZE);
      return NodeId(id);
    } else if(value - 2 >= m_ids.size()) {
      m_reader.fail("identifier out of range");
    }
    return m_ids[static_cast<std::size_t>(value - 2)];
  }

  std::string Decoder::read_text() {
    auto value = m_reader.read_varint();
    if(value == 0) {
      return m_strings.emplace_back(m_reader.read_string());
    } else if(value - 1 >= m_strings.size()) {
      m_reader.fail("string out of range");
    }
    return m_strings[static_cast<std::size_t>(value - 1)];
  }

  void Decoder::read_matrix_values(Matrix& matrix, MatrixForm form) {
    matrix = Matrix();
    if(form == MatrixForm::GENERAL) {
      matrix.m_a = m_reader.read_number();
      matrix.m_b = m_reader.read_number();
      matrix.m_c = m_reader.read_number();
      matrix.m_d = m_reader.read_number();
    }
    if(form != MatrixForm::IDENTITY) {
      matrix.m_tx = m_reader.read_number();
      matrix.m_ty = m_reader.read_number();
    }
  }

  NodeRecord Decoder::read_node(const NodeId& id, std::size_t node_count) {
    auto record = NodeRecord();
    auto& header = record.m_header;
    header.m_id = id;
    auto flags = m_reader.read_u16();
    auto form = (flags >> FORM_SHIFT) & 3;
    auto kind = (flags >> KIND_SHIFT) & 7;
    if((flags & RESERVED_FLAGS) ||
        form > static_cast<int>(MatrixForm::GENERAL) ||
        kind >= std::variant_size_v<Payload>) {
      m_reader.fail("invalid node flags");
    }
    record.m_child_count = static_cast<std::size_t>(m_reader.read_varint());
    if(record.m_child_count > node_count) {
      m_reader.fail("child count out of range");
    }
    header.m_is_visible = (flags & IS_VISIBLE) != 0;
    header.m_is_locked = (flags & IS_LOCKED) != 0;
    header.m_is_isolated = (flags & IS_ISOLATED) != 0;
    header.m_is_marked_for_export = (flags & IS_MARKED_FOR_EXPORT) != 0;
    if(flags & HAS_NAME) {
      header.m_name = read_text();
    }
    read_matrix_values(header.m_transform, static_cast<MatrixForm>(form));
    if(flags & HAS_OPACITY) {
      header.m_opacity = m_reader.read_number();
    }
    if(flags & HAS_BLEND_MODE) {
      read_enum(header.m_blend_mode, BlendMode::LUMINOSITY);
    }
    if(flags & HAS_CONSTRAINTS) {
      header.m_constraints = to_constraints(m_reader.read_byte());
    }
    read_alternative(record.m_payload, kind,
      std::make_index_sequence<std::variant_size_v<Payload>>());
    return record;
  }

  void Decoder::read(Point& point) {
    point.m_x = m_reader.read_number();
    point.m_y = m_reader.read_number();
  }

  void Decoder::read(Size& size) {
    size.m_width = m_reader.read_number();
    size.m_height = m_reader.read_number();
  }

  void Decoder::read(Rect& rect) {
    rect.m_x = m_reader.read_number();
    rect.m_y = m_reader.read_number();
    rect.m_width = m_reader.read_number();
    rect.m_height = m_reader.read_number();
  }

  void Decoder::read(Matrix& matrix) {
    auto form = MatrixForm();
    read_enum(form, MatrixForm::GENERAL);
    read_matrix_values(matrix, form);
  }

  void Decoder::read(Color& color) {
    color.m_red = m_reader.read_number();
    color.m_green = m_reader.read_number();
    color.m_blue = m_reader.read_number();
    color.m_alpha = m_reader.read_number();
  }

  void Decoder::read(CornerRadii& radii) {
    radii.m_top_left = m_reader.read_number();
    radii.m_top_right = m_reader.read_number();
    radii.m_bottom_right = m_reader.read_number();
    radii.m_bottom_left = m_reader.read_number();
  }

  void Decoder::read(Margins& margins) {
    margins.m_top = m_reader.read_number();
    margins.m_right = m_reader.read_number();
    margins.m_bottom = m_reader.read_number();
    margins.m_left = m_reader.read_number();
  }

  void Decoder::read(GradientStop& stop) {
    stop.m_offset = m_reader.read_number();
    read(stop.m_color);
  }

  void Decoder::read(LinearGradient& gradient) {
    read(gradient.m_start);
    read(gradient.m_end);
  }

  void Decoder::read(RadialGradient& gradient) {
    read(gradient.m_center);
    gradient.m_radius = m_reader.read_number();
    read(gradient.m_focus);
  }

  void Decoder::read(Path& path) {
    auto rule = FillRule();
    read_enum(rule, FillRule::EVEN_ODD);
    path = Path(rule);
    auto count = read_count();
    for(auto i = std::size_t(0); i != count; ++i) {
      auto type = SegmentType();
      read_enum(type, SegmentType::CLOSE);
      auto points = std::array<Point, 3>();
      auto point_count = get_point_count(type);
      for(auto j = std::size_t(0); j != point_count; ++j) {
        read(points[j]);
      }
      if(type == SegmentType::MOVE) {
        path.move_to(points[0]);
      } else if(type == SegmentType::LINE) {
        path.line_to(points[0]);
      } else if(type == SegmentType::QUADRATIC) {
        path.quadratic_to(points[0], points[1]);
      } else if(type == SegmentType::CUBIC) {
        path.cubic_to(points[0], points[1], points[2]);
      } else {
        path.close();
      }
    }
  }

  void Decoder::read(SolidFill& fill) {
    read(fill.m_color);
    fill.m_swatch = read_reference();
  }

  void Decoder::read(GradientFill& fill) {
    read(fill.m_geometry);
    auto count = read_count();
    fill.m_stops.resize(count);
    for(auto& stop : fill.m_stops) {
      read(stop);
    }
    read_enum(fill.m_units, GradientUnits::USER_SPACE);
    read(fill.m_transform);
  }

  void Decoder::read(ImageFill& fill) {
    fill.m_image = read_reference();
    read(fill.m_size);
    read_enum(fill.m_scale_behavior, ScaleBehavior::COVER);
    read(fill.m_offset);
    fill.m_scale = m_reader.read_number();
    fill.m_horizontal_scale = m_reader.read_number();
    fill.m_vertical_scale = m_reader.read_number();
    auto flags = m_reader.read_byte();
    if(flags > 3) {
      m_reader.fail("invalid image fill flags");
    }
    fill.m_is_flipped_horizontally = (flags & 1) != 0;
    fill.m_is_flipped_vertically = (flags & 2) != 0;
  }

  void Decoder::read(Stroke& stroke) {
    read(stroke.m_color);
    stroke.m_width = m_reader.read_number();
    read_enum(stroke.m_alignment, StrokeAlignment::OUTSIDE);
    read_enum(stroke.m_cap, StrokeCap::SQUARE);
    read_enum(stroke.m_join, StrokeJoin::BEVEL);
    stroke.m_miter_limit = m_reader.read_number();
    auto count = read_count();
    stroke.m_dashes.resize(count);
    for(auto& dash : stroke.m_dashes) {
      dash = m_reader.read_number();
    }
    stroke.m_is_enabled = m_reader.read_bool();
  }

  void Decoder::read(DropShadow& shadow) {
    read(shadow.m_offset);
    shadow.m_blur = m_reader.read_number();
    read(shadow.m_color);
    shadow.m_is_visible = m_reader.read_bool();
  }

  void Decoder::read(InnerShadow& shadow) {
    read(shadow.m_offset);
    shadow.m_blur = m_reader.read_number();
    read(shadow.m_color);
    shadow.m_is_visible = m_reader.read_bool();
  }

  void Decoder::read(Blur& blur) {
    blur.m_amount = m_reader.read_number();
    blur.m_brightness = m_reader.read_number();
    blur.m_fill_opacity = m_reader.read_number();
    auto flags = m_reader.read_byte();
    if(flags > 3) {
      m_reader.fail("invalid blur flags");
    }
    blur.m_is_background = (flags & 1) != 0;
    blur.m_is_visible = (flags & 2) != 0;
  }

  void Decoder::read(Style& style) {
    read(style.m_fill);
    if(m_reader.read_bool()) {
      read(style.m_stroke.emplace());
    }
    auto count = read_count();
    style.m_effects.resize(count);
    for(auto& effect : style.m_effects) {
      read(effect);
    }
  }

  void Decoder::read(RectangleShape& shape) {
    read(shape.m_bounds);
    read(shape.m_radii);
  }

  void Decoder::read(EllipseShape& shape) {
    read(shape.m_center);
    shape.m_x_radius = m_reader.read_number();
    shape.m_y_radius = m_reader.read_number();
  }

  void Decoder::read(LineShape& shape) {
    read(shape.m_start);
    read(shape.m_end);
  }

  void Decoder::read(PolygonShape& shape) {
    shape.m_corner_count = m_reader.read_int();
    read(shape.m_size);
    shape.m_corner_radius = m_reader.read_number();
    auto count = read_count();
    shape.m_vertices.resize(count);
    for(auto& vertex : shape.m_vertices) {
      read(vertex);
    }
  }

  void Decoder::read(PathShape& shape) {
    auto path = Path();
    read(path);
    shape.m_path = Shared<Path>(std::move(path));
    read_enum(shape.m_fill_rule, FillRule::EVEN_ODD);
  }

  void Decoder::read(CompoundShape& shape) {
    read_enum(shape.m_operation, CompoundOperation::EXCLUDE);
    auto path = Path();
    read(path);
    shape.m_path = Shared<Path>(std::move(path));
  }

  void Decoder::read(Font& font) {
    font.m_family = read_text();
    font.m_style = read_text();
    font.m_postscript_name = read_text();
    font.m_size = m_reader.read_number();
  }

  void Decoder::read(TextStyle& style) {
    read(style.m_font);
    style.m_letter_spacing = m_reader.read_number();
    read(style.m_color);
    auto flags = m_reader.read_byte();
    auto transform = (flags >> 2) & 3;
    auto script = (flags >> 4) & 3;
    if(flags > 0x3F || script > static_cast<int>(TextScript::SUBSCRIPT)) {
      m_reader.fail("invalid text style flags");
    }
    style.m_is_underlined = (flags & 1) != 0;
    style.m_is_struck_through = (flags & 2) != 0;
    style.m_transform = static_cast<TextTransform>(transform);
    style.m_script = static_cast<TextScript>(script);
  }

  void Decoder::read(TextRun& run) {
    run.m_length = static_cast<std::size_t>(m_reader.read_varint());
    read(run.m_style);
    run.m_character_style = read_reference();
  }

  void Decoder::read(AreaFrame& frame) {
    read(frame.m_size);
  }

  void Decoder::read(AutoHeightFrame& frame) {
    frame.m_width = m_reader.read_number();
  }

  void Decoder::read(ParagraphStyle& style) {
    read_enum(style.m_alignment, TextAlignment::RIGHT);
    if(m_reader.read_bool()) {
      style.m_line_height = m_reader.read_number();
    }
    style.m_paragraph_spacing = m_reader.read_number();
  }

  void Decoder::read(StoredRun& run) {
    run.m_from = static_cast<std::size_t>(m_reader.read_varint());
    run.m_to = static_cast<std::size_t>(m_reader.read_varint());
    read(run.m_position);
    auto flags = m_reader.read_byte();
    if(flags > 3) {
      m_reader.fail("invalid stored run flags");
    }
    if(flags & 1) {
      read(run.m_font.emplace());
    }
    if(flags & 2) {
      read(run.m_color.emplace());
    }
  }

  void Decoder::read(StoredLayout& layout) {
    layout.m_paragraphs.resize(read_count());
    for(auto& paragraph : layout.m_paragraphs) {
      paragraph.m_lines.resize(read_count());
      for(auto& line : paragraph.m_lines) {
        line.m_runs.resize(read_count());
        for(auto& run : line.m_runs) {
          read(run);
        }
      }
    }
  }

  void Decoder::read(Text& text) {
    auto size = m_reader.read_count(2);
    auto bytes = m_reader.read_bytes(2 * size);
    auto content = std::u16string(size, u'\0');
    if constexpr(std::endian::native == std::endian::little) {
      std::memcpy(content.data(), bytes.data(), bytes.size());
    } else {
      for(auto i = std::size_t(0); i != size; ++i) {
        content[i] = static_cast<char16_t>(
          static_cast<std::uint8_t>(bytes[2 * i]) |
          (static_cast<std::uint8_t>(bytes[2 * i + 1]) << 8));
      }
    }
    text.m_content = Shared<std::u16string>(std::move(content));
    read(text.m_frame);
    read(text.m_style);
    read(text.m_base_style);
    text.m_runs.resize(read_count());
    auto remaining = size;
    for(auto& run : text.m_runs) {
      read(run);
      if(run.m_length > remaining) {
        m_reader.fail("text runs exceed the content");
      }
      remaining -= run.m_length;
    }
    read(text.m_paragraph_style);
    if(m_reader.read_bool()) {
      auto layout = StoredLayout();
      read(layout);
      text.m_layout = Shared<StoredLayout>(std::move(layout));
    }
  }

  void Decoder::read(LayoutGrid& grid) {
    read_enum(grid.m_type, GridType::LAYOUT);
    grid.m_is_visible = m_reader.read_bool();
    grid.m_column_count = m_reader.read_int();
    grid.m_gutter = m_reader.read_number();
    grid.m_default_width = m_reader.read_number();
    read(grid.m_margins);
    read(grid.m_column_color);
    read(grid.m_row_color);
    read(grid.m_square_size);
    read(grid.m_square_column_color);
    read(grid.m_square_row_color);
  }

  void Decoder::read(Guide& guide) {
    read_enum(guide.m_orientation, Orientation::VERTICAL);
    guide.m_position = m_reader.read_number();
  }

  void Decoder::read(Artboard& artboard) {
    read(artboard.m_frame);
    read(artboard.m_background);
    auto flags = m_reader.read_byte();
    if(flags > 3) {
      m_reader.fail("invalid artboard flags");
    }
    if(flags & 1) {
      artboard.m_viewport_height = m_reader.read_number();
    }
    if(flags & 2) {
      read(artboard.m_layout_grid.emplace());
    }
    artboard.m_guides.resize(read_count());
    for(auto& guide : artboard.m_guides) {
      read(guide);
    }
  }

  void Decoder::read(MaskShape& shape) {
    read(shape.m_transform);
    read(shape.m_geometry);
  }

  void Decoder::read(Mask& mask) {
    mask.m_shapes.resize(read_count());
    for(auto& shape : mask.m_shapes) {
      read(shape);
    }
  }

  void Decoder::read(RepeatGrid& grid) {
    read(grid.m_cell_size);
    grid.m_column_spacing = m_reader.read_number();
    grid.m_row_spacing = m_reader.read_number();
    grid.m_column_count = m_reader.read_int();
    grid.m_row_count = m_reader.read_int();
    read(grid.m_size);
  }

  void Decoder::read(ScrollGroup& group) {
    read_enum(group.m_direction, ScrollDirection::PANNING);
    read(group.m_viewport);
    read(group.m_offset);
  }

  void Decoder::read(Stack& stack) {
    read_enum(stack.m_orientation, Orientation::VERTICAL);
    auto count = read_count();
    for(auto i = std::size_t(0); i != count; ++i) {
      auto id = read_reference();
      auto spacing = m_reader.read_number();
      stack.m_spacing.emplace_hint(stack.m_spacing.end(), id, spacing);
    }
    read(stack.m_padding);
    stack.m_has_background = m_reader.read_bool();
  }

  void Decoder::read(Padding& padding) {
    read(padding.m_padding);
    auto flags = m_reader.read_byte();
    if(flags > 3) {
      m_reader.fail("invalid padding flags");
    }
    padding.m_is_content_aware = (flags & 1) != 0;
    padding.m_has_background = (flags & 2) != 0;
  }

  void Decoder::read(Overrides& overrides) {
    overrides.m_added.resize(read_count());
    for(auto& id : overrides.m_added) {
      id = read_reference();
    }
    overrides.m_removed.resize(read_count());
    for(auto& id : overrides.m_removed) {
      id = read_reference();
    }
    read_map(overrides.m_reordered);
    auto count = read_count();
    for(auto i = std::size_t(0); i != count; ++i) {
      auto id = read_reference();
      auto properties = std::vector<std::string>(read_count());
      for(auto& property : properties) {
        property = read_text();
      }
      overrides.m_properties.emplace_hint(
        overrides.m_properties.end(), id, std::move(properties));
    }
  }

  void Decoder::read(ComponentState& state) {
    state.m_id = read_reference();
    state.m_name = read_text();
    state.m_tree = read_reference();
    read_map(state.m_sync_map);
    read(state.m_overrides);
  }

  void Decoder::read(ComponentInstance& instance) {
    instance.m_component = read_reference();
    instance.m_state = read_reference();
    read(instance.m_size);
    read_map(instance.m_sync_map);
    read(instance.m_overrides);
    instance.m_states.resize(read_count());
    for(auto& state : instance.m_states) {
      read(state);
    }
  }

  void Decoder::read(Group& group) {
    auto roles = m_reader.read_byte();
    if(roles > 0x3F) {
      m_reader.fail("invalid group roles");
    }
    if(roles & HAS_MASK) {
      read(group.m_mask.emplace());
    }
    if(roles & HAS_REPEAT_GRID) {
      read(group.m_repeat_grid.emplace());
    }
    if(roles & HAS_SCROLL_GROUP) {
      read(group.m_scroll_group.emplace());
    }
    if(roles & HAS_STACK) {
      read(group.m_stack.emplace());
    }
    if(roles & HAS_PADDING) {
      read(group.m_padding.emplace());
    }
    if(roles & HAS_COMPONENT_INSTANCE) {
      read(group.m_component_instance.emplace());
    }
  }

  void Decoder::read(Shape& shape) {
    read(shape.m_geometry);
    read(shape.m_style);
  }

  void Decoder::read(Reference& reference) {
    reference.m_source = read_reference();
    auto flags = m_reader.read_byte();
    if(flags > 3) {
      m_reader.fail("invalid reference flags");
    }
    reference.m_has_children = (flags & 2) != 0;
    if(flags & 1) {
      read(reference.m_transform.emplace());
    }
  }

  void Decoder::read(Component& component) {
    component.m_id = read_reference();
    component.m_name = read_text();
    component.m_master = read_reference();
    component.m_states.resize(read_count());
    for(auto& state : component.m_states) {
      read(state);
    }
    component.m_default_state = read_reference();
    if(m_reader.read_bool()) {
      auto& link = component.m_link.emplace();
      link.m_reference = read_text();
      link.m_source_name = read_text();
      link.m_last_modified = m_reader.read_number();
    }
  }

  void Decoder::read(ColorSwatch& swatch) {
    swatch.m_id = read_reference();
    swatch.m_name = read_text();
    read(swatch.m_color);
    swatch.m_gradient.resize(read_count());
    for(auto& stop : swatch.m_gradient) {
      read(stop);
    }
  }

  void Decoder::read(CharacterStyle& style) {
    style.m_id = read_reference();
    style.m_name = read_text();
    read(style.m_style);
    if(m_reader.read_bool()) {
      style.m_line_height = m_reader.read_number();
    }
    style.m_paragraph_spacing = m_reader.read_number();
  }

  void Decoder::read(ImageEntry& image) {
    image.m_id = read_reference();
    image.m_media_type = read_text();
    image.m_size = m_reader.read_varint();
  }

  void Decoder::read(TimeTrigger& trigger) {
    trigger.m_delay = m_reader.read_number();
  }

  void Decoder::read(KeyTrigger& trigger) {
    trigger.m_keys = read_text();
    read_enum(trigger.m_device, KeyDevice::GAMEPAD);
  }

  void Decoder::read(Animation& animation) {
    read_enum(animation.m_transition, Transition::AUTO_ANIMATE);
    read_enum(animation.m_easing, Easing::SNAP);
    animation.m_duration = m_reader.read_number();
  }

  void Decoder::read(ArtboardTransition& action) {
    action.m_destination = read_reference();
    read(action.m_animation);
    action.m_preserve_scroll = m_reader.read_bool();
  }

  void Decoder::read(Overlay& action) {
    action.m_destination = read_reference();
    read(action.m_position);
    read(action.m_animation);
  }

  void Decoder::read(StateTransition& action) {
    action.m_state = read_reference();
    read(action.m_animation);
  }

  void Decoder::read(VideoPlayback& action) {
    action.m_target = read_reference();
    read_enum(action.m_command, VideoCommand::STOP);
  }

  void Decoder::read(Interaction& interaction) {
    interaction.m_id = read_reference();
    interaction.m_owner = read_reference();
    read(interaction.m_trigger);
    read(interaction.m_action);
    interaction.m_is_enabled = m_reader.read_bool();
  }

  void Decoder::read(Flow& flow) {
    flow.m_artboard = read_reference();
    flow.m_name = read_text();
  }

  void Decoder::read(Prototype& prototype) {
    prototype.m_interactions.resize(read_count());
    for(auto& interaction : prototype.m_interactions) {
      read(interaction);
    }
    prototype.m_home_artboard = read_reference();
    prototype.m_flows.resize(read_count());
    for(auto& flow : prototype.m_flows) {
      read(flow);
    }
  }

  template<typename T> requires std::is_empty_v<T>
  void Decoder::read(T&) {}

  template<typename... T>
  void Decoder::read(std::variant<T...>& value) {
    auto index = m_reader.read_byte();
    if(index >= sizeof...(T)) {
      m_reader.fail("invalid alternative");
    }
    read_alternative(value, index, std::index_sequence_for<T...>());
  }

  void Decoder::read_map(std::map<NodeId, NodeId>& map) {
    auto count = read_count();
    for(auto i = std::size_t(0); i != count; ++i) {
      auto key = read_reference();
      auto value = read_reference();
      map.emplace_hint(map.end(), key, value);
    }
  }

  std::size_t Decoder::read_count() {
    return m_reader.read_count(1);
  }

  template<typename E>
  void Decoder::read_enum(E& value, E last) {
    value = m_reader.read_enum(last);
  }

  template<typename V, std::size_t... I>
  void Decoder::read_alternative(
      V& value, std::size_t index, std::index_sequence<I...>) {
    static_cast<void>(
      ((index == I && (read(value.template emplace<I>()), true)) || ...));
  }
}

std::vector<std::byte> Rellu::Details::encode_nodes(
    std::span<const Node* const> nodes, std::size_t first,
    const IdIndex& ids) {
  auto encoder = Encoder(ids);
  encoder.get_writer().write_varint(first);
  encoder.get_writer().write_varint(nodes.size());
  for(auto node : nodes) {
    encoder.write_node(*node);
  }
  return encoder.release();
}

NodeBlock Rellu::Details::decode_nodes(std::span<const std::byte> data,
    std::span<const NodeId> ids, std::size_t node_count,
    std::uint32_t version) {
  auto decoder = Decoder(data, ids, version, "node block");
  auto& reader = decoder.get_reader();
  auto block = NodeBlock();
  auto first = reader.read_varint();
  auto count = reader.read_count(2);
  if(first > node_count || count > node_count - first) {
    reader.fail("node range out of bounds");
  }
  block.m_first = static_cast<std::size_t>(first);
  block.m_records.reserve(count);
  for(auto i = std::size_t(0); i != count; ++i) {
    auto& id = ids[block.m_first + i];
    block.m_records.push_back(decoder.read_node(id, node_count));
  }
  reader.expect_end();
  return block;
}

std::vector<std::byte> Rellu::Details::encode_resources(
    const Resources& resources, const IdIndex& ids) {
  auto encoder = Encoder(ids);
  encoder.write_count(resources.m_components.size());
  for(auto& entry : resources.m_components) {
    encoder.write(entry.second);
  }
  encoder.write_count(resources.m_swatches.size());
  for(auto& swatch : resources.m_swatches) {
    encoder.write(swatch);
  }
  encoder.write_count(resources.m_character_styles.size());
  for(auto& style : resources.m_character_styles) {
    encoder.write(style);
  }
  encoder.write_count(resources.m_images.size());
  for(auto& entry : resources.m_images) {
    encoder.write(entry.second);
  }
  return encoder.release();
}

ResourceRecord Rellu::Details::decode_resources(
    std::span<const std::byte> data, std::span<const NodeId> ids,
    std::uint32_t version) {
  auto decoder = Decoder(data, ids, version, "resources");
  auto record = ResourceRecord();
  record.m_components.resize(decoder.read_count());
  for(auto& component : record.m_components) {
    decoder.read(component);
  }
  record.m_swatches.resize(decoder.read_count());
  for(auto& swatch : record.m_swatches) {
    decoder.read(swatch);
  }
  record.m_character_styles.resize(decoder.read_count());
  for(auto& style : record.m_character_styles) {
    decoder.read(style);
  }
  record.m_images.resize(decoder.read_count());
  for(auto& image : record.m_images) {
    decoder.read(image);
  }
  decoder.get_reader().expect_end();
  return record;
}

std::vector<std::byte> Rellu::Details::encode_prototype(
    const Prototype& prototype, const IdIndex& ids) {
  auto encoder = Encoder(ids);
  encoder.write(prototype);
  return encoder.release();
}

Prototype Rellu::Details::decode_prototype(std::span<const std::byte> data,
    std::span<const NodeId> ids, std::uint32_t version) {
  auto decoder = Decoder(data, ids, version, "prototype");
  auto prototype = Prototype();
  decoder.read(prototype);
  decoder.get_reader().expect_end();
  return prototype;
}
