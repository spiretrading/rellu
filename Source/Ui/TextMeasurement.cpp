#include <optional>
#include "Rellu/Text/TextLayout.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"

using namespace Rellu;

TextMeasurer Rellu::make_text_measurer(const FontLibrary& library) {
  return [library = &library] (const Text& text) {
    return measure_text(text, *library);
  };
}

XdTextLayout Rellu::make_xd_text_layout(const FontLibrary& library) {
  return [library = &library] (const Text& text) {
    return std::optional(store_layout(text, *library));
  };
}
