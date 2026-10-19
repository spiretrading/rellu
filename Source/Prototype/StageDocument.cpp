#include <optional>
#include <vector>
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdPatch.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Prototype/StageDocument.hpp"

using namespace Rellu;

namespace {
  void copy_tree(const Document& document, Transaction& transaction,
      const NodeId& root, bool is_on_canvas) {
    auto& node = document.get(root);
    if(is_on_canvas) {
      transaction.append(NodeId(), node.m_header, node.m_payload);
    } else {
      transaction.insert_root(node.m_header, node.m_payload);
    }
    auto pending = std::vector<const Node*>({&node});
    while(!pending.empty()) {
      auto current = pending.back();
      pending.pop_back();
      for(auto& id : current->m_children) {
        auto& child = document.get(id);
        transaction.append(current->get_id(), child.m_header, child.m_payload);
      }
      for(auto i = current->m_children.rbegin();
          i != current->m_children.rend(); ++i) {
        pending.push_back(&document.get(*i));
      }
    }
  }

  bool is_marked(const XdValue& json) {
    auto meta = json.find("meta");
    if(!meta) {
      return false;
    }
    auto ux = meta->find("ux");
    if(!ux) {
      return false;
    }
    auto fixed = ux->find("fixed");
    return fixed && fixed->to_bool();
  }

  bool is_candidate(const Node& node) {
    auto bytes = node.m_header.m_opaque.get_bytes();
    if(bytes.find("fixed") == std::string_view::npos) {
      return false;
    }
    return decode_node_data(bytes).m_patch.find("fixed") !=
      std::string_view::npos;
  }

  bool is_marked(const XdGenerator& generator, const Node& node) {
    auto json = generator.make_node(node);
    apply_patch(decode_node_data(node.m_header.m_opaque.get_bytes()).m_patch,
      json, false);
    return is_marked(json);
  }
}

std::unique_ptr<Document> Rellu::copy_document(const Document& document) {
  auto copy = std::make_unique<Document>();
  auto roots = std::vector<NodeId>();
  document.get_nodes().for_each([&] (const auto& node) {
    if(node.m_parent.is_nil() && !document.is_on_canvas(node.get_id())) {
      roots.push_back(node.get_id());
    }
  });
  auto transaction = Transaction(*copy);
  for(auto& root : roots) {
    copy_tree(document, transaction, root, false);
  }
  for(auto& id : document.get_canvas()) {
    copy_tree(document, transaction, id, true);
  }
  auto& resources = document.get_resources();
  for(auto& component : resources.m_components) {
    transaction.add_component(component.second);
  }
  for(auto& image : resources.m_images) {
    transaction.add_image(image.second);
  }
  for(auto& swatch : resources.m_swatches) {
    transaction.add_swatch(swatch);
  }
  for(auto& style : resources.m_character_styles) {
    transaction.add_character_style(style);
  }
  auto& prototype = document.get_prototype();
  for(auto& interaction : prototype.m_interactions) {
    transaction.add_interaction(interaction);
  }
  if(!prototype.m_flows.empty()) {
    transaction.set_flows(prototype.m_flows);
  }
  if(!prototype.m_home_artboard.is_nil()) {
    transaction.set_home_artboard(prototype.m_home_artboard);
  }
  transaction.commit();
  copy->clear_history();
  copy->set_opaque(document.get_opaque());
  return copy;
}

std::unordered_set<NodeId> Rellu::find_fixed_nodes(const Document& document) {
  auto fixed = std::unordered_set<NodeId>();
  auto generator = std::optional<XdGenerator>();
  document.get_nodes().for_each([&] (const auto& node) {
    if(is_candidate(node)) {
      if(!generator) {
        generator.emplace(document, XdTextLayout());
      }
      if(is_marked(*generator, node)) {
        fixed.insert(node.get_id());
      }
    }
  });
  return fixed;
}

bool Rellu::is_fixed(const Document& document, const NodeId& id) {
  auto node = document.find(id);
  if(!node || !is_candidate(*node)) {
    return false;
  }
  auto generator = XdGenerator(document, XdTextLayout());
  return is_marked(generator, *node);
}
