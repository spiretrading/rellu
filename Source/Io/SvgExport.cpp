#include <stdexcept>
#include "Rellu/Io/SvgExport.hpp"

using namespace Rellu;

void Rellu::write_svg_export(const Document& document, const NodeId& node,
    const std::filesystem::path& path, const ExportOptions& options) {
  throw std::runtime_error("SVG export is not available.");
}
