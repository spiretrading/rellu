#include <chrono>
#include "Rellu/Io/XdImporters.hpp"

using namespace Rellu;

XdImporter Rellu::make_xd_importer() {
  auto importer = XdImporter();
  add_header_importer(importer);
  add_artboard_importer(importer);
  add_style_importer(importer);
  add_shape_importer(importer);
  add_text_importer(importer);
  add_group_importer(importer);
  add_mask_importer(importer);
  add_repeat_grid_importer(importer);
  add_scroll_group_importer(importer);
  add_layout_importer(importer);
  add_component_importer(importer);
  add_reference_importer(importer);
  add_prototype_importer(importer);
  add_resource_importer(importer);
  add_canvas_importer(importer);
  return importer;
}

ImportReport Rellu::import_xd(const XdPackage& package, Document& document) {
  static const auto importer = make_xd_importer();
  return importer.import(package, document);
}

ImportReport Rellu::import_xd(
    const std::filesystem::path& path, Document& document) {
  auto start = std::chrono::steady_clock::now();
  auto package = XdPackage(path);
  auto open_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - start);
  auto report = import_xd(package, document);
  report.m_read_time += open_time;
  return report;
}
