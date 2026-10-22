#include "Rellu/Io/ExportFormat.hpp"

using namespace Rellu;

std::string_view Rellu::get_extension(ExportFormat format) {
  if(format == ExportFormat::SVG) {
    return "svg";
  } else if(format == ExportFormat::PDF) {
    return "pdf";
  } else if(format == ExportFormat::JPG) {
    return "jpg";
  }
  return "png";
}

bool Rellu::is_vector(ExportFormat format) {
  return format == ExportFormat::SVG || format == ExportFormat::PDF;
}
