#include <utility>
#include <vector>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  struct ReferenceSources {
    std::vector<NodeId> m_sources;

    ReferenceSources fork() const {
      return ReferenceSources();
    }

    void merge(ReferenceSources fork) {
      m_sources.insert(
        m_sources.end(), fork.m_sources.begin(), fork.m_sources.end());
    }
  };
}

void Rellu::add_reference_importer(XdImporter& importer) {
  importer.set_node_importer("syncRef"_L1, [] (XdNode& node) {
    auto& json = *node.m_json;
    auto& import = *node.m_import;
    auto source = json.get_string("syncSourceGuid"_L1);
    if(!source) {
      import.warn("Reference without a source");
      return;
    }
    auto reference = Reference();
    reference.m_source = to_node_id(*source);
    if(auto guid = json.get_string("guid"_L1)) {
      node.m_header.m_id = to_node_id(*guid);
    }
    if(auto transform = json.get_object("transform"_L1)) {
      if(auto matrix = read_matrix(*transform)) {
        reference.m_transform = *matrix;
      } else {
        import.warn("Unsupported transform");
      }
    }
    auto children = json.get_object("group"_L1);
    if(!children) {
      children = json.get_object("shape"_L1);
    }
    if(children) {
      reference.m_has_children = true;
      node.m_children = children->get_array("children"_L1);
    }
    import.get_states().get<ReferenceSources>().m_sources.push_back(
      reference.m_source);
    node.m_payload = std::move(reference);
  });
  importer.add_document_importer(XdPhase::LINKS, [] (XdImport& import) {
    auto sources = import.get_states().find<ReferenceSources>();
    if(!sources) {
      return;
    }
    for(auto& source : sources->m_sources) {
      if(!import.get_document().find(source)) {
        import.warn("Unresolved reference source");
      }
    }
  });
}
