#include <optional>
#include "Rellu/Text/TextLayout.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"

using namespace Rellu;

TextMeasurer Rellu::make_text_measurer(const FontLibrary& library) {
  return [library = &library] (const Text& text) {
    return measure_text(text, *library);
  };
}

TextMeasurer Rellu::make_painted_text_measurer(const FontLibrary& library) {
  return [library = &library] (const Text& text) {
    return measure_painted_text(text, *library);
  };
}

TextOutliner Rellu::make_text_outliner(const FontLibrary& library) {
  return [library = &library] (const Text& text) {
    auto glyphs = Path();
    for(auto& fill : outline(lay_out(text, *library))) {
      for(auto& segment : fill.m_path.get_segments()) {
        auto& points = segment.m_points;
        if(segment.m_type == SegmentType::MOVE) {
          glyphs.move_to(points[0]);
        } else if(segment.m_type == SegmentType::LINE) {
          glyphs.line_to(points[0]);
        } else if(segment.m_type == SegmentType::QUADRATIC) {
          glyphs.quadratic_to(points[0], points[1]);
        } else if(segment.m_type == SegmentType::CUBIC) {
          glyphs.cubic_to(points[0], points[1], points[2]);
        } else {
          glyphs.close();
        }
      }
    }
    if(!text.m_is_flipped_vertically) {
      return glyphs;
    }
    auto bounds = measure_text(text, *library);
    return transform(
      glyphs, Matrix(1, 0, 0, -1, 0, 2 * bounds.m_y + bounds.m_height));
  };
}

XdTextLayout Rellu::make_xd_text_layout(const FontLibrary& library) {
  return [library = &library] (const Text& text) {
    return std::optional(store_layout(text, *library));
  };
}
