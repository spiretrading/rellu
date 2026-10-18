#include <vector>
#include "Rellu/Io/XdImporters.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  void read_content(XdObject& content, std::vector<QJsonObject>& nodes) {
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
        nodes.push_back(child.toObject());
      }
    }
  }
}

void Rellu::add_canvas_importer(XdImporter& importer) {
  importer.add_document_importer(XdPhase::CANVAS, [] (XdImport& import) {
    auto nodes = std::vector<QJsonObject>();
    for(auto& artboard : import.get_artboards()) {
      read_content(*artboard, nodes);
    }
    read_content(import.get_pasteboard(), nodes);
    import.import_nodes(nodes, XdNodeRole::NODE);
  });
}
