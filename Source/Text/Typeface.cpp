#include <utility>
#include "Rellu/Text/Typeface.hpp"
#include "HarfBuzz.hpp"

hb_unicode_funcs_t* hb_qt_get_unicode_funcs();

using namespace Rellu;
using namespace Rellu::Details;

namespace {
  constexpr auto SUBUNITS = 64;

  struct Outliner {
    Path* m_path;
    double m_scale;

    Point to_point(float x, float y) const {
      return Point(x * m_scale, -y * m_scale);
    }
  };

  void move_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x,
      float y, void*) {
    auto& outliner = *static_cast<Outliner*>(data);
    outliner.m_path->move_to(outliner.to_point(x, y));
  }

  void line_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x,
      float y, void*) {
    auto& outliner = *static_cast<Outliner*>(data);
    outliner.m_path->line_to(outliner.to_point(x, y));
  }

  void quadratic_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*,
      float control_x, float control_y, float x, float y, void*) {
    auto& outliner = *static_cast<Outliner*>(data);
    outliner.m_path->quadratic_to(outliner.to_point(control_x, control_y),
      outliner.to_point(x, y));
  }

  void cubic_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*,
      float first_x, float first_y, float second_x, float second_y, float x,
      float y, void*) {
    auto& outliner = *static_cast<Outliner*>(data);
    outliner.m_path->cubic_to(outliner.to_point(first_x, first_y),
      outliner.to_point(second_x, second_y), outliner.to_point(x, y));
  }

  void close_path(hb_draw_funcs_t*, void* data, hb_draw_state_t*, void*) {
    static_cast<Outliner*>(data)->m_path->close();
  }

  hb_draw_funcs_t* get_draw_funcs() {
    static auto funcs = [] {
      auto funcs = hb_draw_funcs_create();
      hb_draw_funcs_set_move_to_func(funcs, move_to, nullptr, nullptr);
      hb_draw_funcs_set_line_to_func(funcs, line_to, nullptr, nullptr);
      hb_draw_funcs_set_quadratic_to_func(
        funcs, quadratic_to, nullptr, nullptr);
      hb_draw_funcs_set_cubic_to_func(funcs, cubic_to, nullptr, nullptr);
      hb_draw_funcs_set_close_path_func(funcs, close_path, nullptr, nullptr);
      hb_draw_funcs_make_immutable(funcs);
      return funcs;
    }();
    return funcs;
  }

  double get_position(hb_font_t* font, hb_ot_metrics_tag_t tag,
      double scale, double fallback) {
    auto position = hb_position_t(0);
    if(!hb_ot_metrics_get_position(font, tag, &position)) {
      return fallback;
    }
    return position * scale;
  }

  bool is_using_typographic_metrics(hb_face_t* face) {
    auto table = hb_face_reference_table(face, HB_TAG('O', 'S', '/', '2'));
    auto length = 0u;
    auto data = hb_blob_get_data(table, &length);
    auto is_using = false;
    if(data && length >= 64) {
      auto selection = (static_cast<unsigned char>(data[62]) << 8) |
        static_cast<unsigned char>(data[63]);
      is_using = (selection & 0x80) != 0;
    }
    hb_blob_destroy(table);
    return is_using;
  }

  TypefaceMetrics read_metrics(hb_face_t* face, hb_font_t* font,
      double scale) {
    auto metrics = TypefaceMetrics();
    metrics.m_ascender = get_position(
      font, HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER, scale, 0.8);
    metrics.m_descender = get_position(
      font, HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER, scale, -0.2);
    metrics.m_line_gap = get_position(
      font, HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP, scale, 0);
    metrics.m_clipping_ascent = get_position(font,
      HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_ASCENT, scale,
      metrics.m_ascender);
    metrics.m_clipping_descent = get_position(font,
      HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_DESCENT, scale,
      -metrics.m_descender);
    metrics.m_underline_thickness = get_position(
      font, HB_OT_METRICS_TAG_UNDERLINE_SIZE, scale, 0.05);
    metrics.m_underline_offset = get_position(
      font, HB_OT_METRICS_TAG_UNDERLINE_OFFSET, scale, -0.1);
    metrics.m_strikeout_thickness = get_position(font,
      HB_OT_METRICS_TAG_STRIKEOUT_SIZE, scale, metrics.m_underline_thickness);
    metrics.m_strikeout_offset = get_position(
      font, HB_OT_METRICS_TAG_STRIKEOUT_OFFSET, scale, 0.3);
    metrics.m_superscript_offset = get_position(
      font, HB_OT_METRICS_TAG_SUPERSCRIPT_EM_Y_OFFSET, scale, 0.35);
    metrics.m_superscript_size = get_position(
      font, HB_OT_METRICS_TAG_SUPERSCRIPT_EM_Y_SIZE, scale, 0.65);
    metrics.m_subscript_offset = get_position(
      font, HB_OT_METRICS_TAG_SUBSCRIPT_EM_Y_OFFSET, scale, 0.15);
    metrics.m_subscript_size = get_position(
      font, HB_OT_METRICS_TAG_SUBSCRIPT_EM_Y_SIZE, scale, 0.65);
    if(is_using_typographic_metrics(face)) {
      metrics.m_line_ascent = metrics.m_ascender;
      metrics.m_line_descent = -metrics.m_descender;
    } else {
      metrics.m_line_ascent = metrics.m_clipping_ascent;
      metrics.m_line_descent = metrics.m_clipping_descent;
    }
    if(!(metrics.m_underline_thickness > 0)) {
      metrics.m_underline_thickness = 0.05;
    }
    if(!(metrics.m_strikeout_thickness > 0)) {
      metrics.m_strikeout_thickness = metrics.m_underline_thickness;
    }
    if(!(metrics.m_superscript_size > 0)) {
      metrics.m_superscript_size = 0.65;
    }
    if(!(metrics.m_subscript_size > 0)) {
      metrics.m_subscript_size = 0.65;
    }
    return metrics;
  }
}

std::shared_ptr<Typeface> Typeface::load(const std::filesystem::path& path,
    unsigned int index, std::optional<unsigned int> instance) {
  auto owner = std::shared_ptr<const void>();
  auto blob = map_font_file(path, owner);
  if(!blob) {
    return nullptr;
  }
  if(index >= hb_face_count(blob)) {
    hb_blob_destroy(blob);
    return nullptr;
  }
  return std::shared_ptr<Typeface>(
    new Typeface(std::move(owner), path, blob, index, instance));
}

std::shared_ptr<Typeface> Typeface::load(
    std::shared_ptr<const std::vector<char>> data, unsigned int index,
    std::optional<unsigned int> instance) {
  if(!data || data->empty()) {
    return nullptr;
  }
  auto blob = hb_blob_create(data->data(),
    static_cast<unsigned int>(data->size()), HB_MEMORY_MODE_READONLY, nullptr,
    nullptr);
  if(index >= hb_face_count(blob)) {
    hb_blob_destroy(blob);
    return nullptr;
  }
  return std::shared_ptr<Typeface>(new Typeface(
    std::move(data), std::filesystem::path(), blob, index, instance));
}

Typeface::Typeface(std::shared_ptr<const void> data,
    std::filesystem::path path, hb_blob_t* blob, unsigned int index,
    std::optional<unsigned int> instance)
    : m_data(std::move(data)),
      m_path(std::move(path)),
      m_index(index),
      m_blob(blob),
      m_face(hb_face_create(blob, index)),
      m_font(hb_font_create(m_face)) {
  auto units = static_cast<int>(hb_face_get_upem(m_face));
  hb_font_set_scale(m_font, units * SUBUNITS, units * SUBUNITS);
  if(instance &&
      *instance < hb_ot_var_get_named_instance_count(m_face)) {
    hb_font_set_var_named_instance(m_font, *instance);
  } else {
    instance = std::nullopt;
  }
  m_instance = instance;
  hb_font_make_immutable(m_font);
  m_scale = 1.0 / (static_cast<double>(units) * SUBUNITS);
  m_names = read_names(m_face, m_font, instance);
  m_metrics = read_metrics(m_face, m_font, m_scale);
}

Typeface::~Typeface() {
  hb_font_destroy(m_font);
  hb_face_destroy(m_face);
  hb_blob_destroy(m_blob);
}

const std::filesystem::path& Typeface::get_path() const {
  return m_path;
}

unsigned int Typeface::get_index() const {
  return m_index;
}

std::optional<unsigned int> Typeface::get_instance() const {
  return m_instance;
}

std::string_view Typeface::get_data() const {
  auto length = 0u;
  auto data = hb_blob_get_data(m_blob, &length);
  return std::string_view(data, length);
}

std::vector<std::pair<std::uint32_t, double>>
    Typeface::get_variations() const {
  auto variations = std::vector<std::pair<std::uint32_t, double>>();
  if(!m_instance) {
    return variations;
  }
  auto count = hb_ot_var_get_axis_count(m_face);
  auto axes = std::vector<hb_ot_var_axis_info_t>(count);
  hb_ot_var_get_axis_infos(m_face, 0, &count, axes.data());
  auto coordinates = std::vector<float>(count);
  hb_ot_var_named_instance_get_design_coords(
    m_face, *m_instance, &count, coordinates.data());
  for(auto i = 0u; i != count; ++i) {
    variations.emplace_back(axes[i].tag, coordinates[i]);
  }
  return variations;
}

const TypefaceNames& Typeface::get_names() const {
  return m_names;
}

const TypefaceMetrics& Typeface::get_metrics() const {
  return m_metrics;
}

bool Typeface::has_glyph(char32_t character) const {
  auto glyph = hb_codepoint_t(0);
  return hb_font_get_nominal_glyph(m_font, character, &glyph) && glyph != 0;
}

std::vector<ShapedGlyph> Typeface::shape(
    std::u16string_view text, std::size_t start, std::size_t length) const {
  auto buffer = hb_buffer_create();
  hb_buffer_set_unicode_funcs(buffer, hb_qt_get_unicode_funcs());
  hb_buffer_add_utf16(buffer,
    reinterpret_cast<const std::uint16_t*>(text.data()),
    static_cast<int>(text.size()), static_cast<unsigned int>(start),
    static_cast<int>(length));
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(m_font, buffer, nullptr, 0);
  auto count = 0u;
  auto infos = hb_buffer_get_glyph_infos(buffer, &count);
  auto positions = hb_buffer_get_glyph_positions(buffer, &count);
  auto glyphs = std::vector<ShapedGlyph>();
  glyphs.reserve(count);
  for(auto i = 0u; i != count; ++i) {
    glyphs.push_back(ShapedGlyph(infos[i].codepoint, infos[i].cluster,
      positions[i].x_advance * m_scale,
      Point(positions[i].x_offset * m_scale,
        -positions[i].y_offset * m_scale)));
  }
  hb_buffer_destroy(buffer);
  return glyphs;
}

const Path& Typeface::get_outline(std::uint32_t index) const {
  auto lock = std::lock_guard(m_mutex);
  auto outline = m_outlines.find(index);
  if(outline != m_outlines.end()) {
    return outline->second;
  }
  auto path = Path();
  auto outliner = Outliner(&path, m_scale);
  hb_font_draw_glyph(m_font, index, get_draw_funcs(), &outliner);
  return m_outlines.emplace(index, std::move(path)).first->second;
}
