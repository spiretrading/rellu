#include "Rellu/Io/XdImporters.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  void import_content(XdObject& content, XdImport& import) {
    content.ignore({"version"_L1});
    for(auto key : {"resources"_L1, "artboards"_L1}) {
      if(auto reference = content.get_object(key)) {
        reference->ignore({"href"_L1});
      }
    }
    auto children = content.get_array("children"_L1);
    if(!children) {
      return;
    }
    for(auto child : children->get_json()) {
      if(child.isObject()) {
        auto json = import.make_node_json(child.toObject());
        import.import_node(*json, nullptr, XdNodeRole::NODE);
      }
    }
  }
}

void Rellu::add_canvas_importer(XdImporter& importer) {
  importer.add_document_importer(XdPhase::CANVAS, [] (XdImport& import) {
    for(auto& artboard : import.get_artboards()) {
      import_content(*artboard, import);
    }
    import_content(import.get_pasteboard(), import);
  });
}
