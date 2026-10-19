#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <QJsonArray>
#include <QJsonObject>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Prototype/PreviewPlayer.hpp"
#include "Rellu/Prototype/StageDocument.hpp"

using namespace Rellu;

namespace {
  struct Usage {
    std::size_t m_count = 0;
    std::size_t m_documents = 0;
    std::string m_example;
  };

  using Usages = std::map<std::string, Usage, std::less<>>;

  const auto SUPPORTED_ACTIONS = std::vector<std::string>({
    "artboard-transition", "auto-animate", "hyperlink", "overlay-transition",
    "previous-artboard", "state-transition"});

  const auto SUPPORTED_TRIGGERS =
    std::vector<std::string>({"tap", "hover", "drag", "time", "keys-gamepad"});

  std::string to_name(const std::filesystem::path& path) {
    auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
  }

  std::vector<std::filesystem::path> list_documents(
      const std::filesystem::path& corpus, const char* list) {
    auto documents = std::vector<std::filesystem::path>();
    if(list) {
      auto file = std::ifstream(list);
      if(!file) {
        throw std::runtime_error(std::string("Cannot read ") + list + ".");
      }
      auto line = std::string();
      while(std::getline(file, line)) {
        while(!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
          line.pop_back();
        }
        if(!line.empty()) {
          documents.push_back(std::filesystem::path(
            std::u8string(line.begin(), line.end()) + u8".xd"));
        }
      }
      return documents;
    }
    for(auto& entry : std::filesystem::recursive_directory_iterator(corpus)) {
      if(entry.is_regular_file() && entry.path().extension() == ".xd") {
        documents.push_back(std::filesystem::relative(entry.path(), corpus));
      }
    }
    std::sort(documents.begin(), documents.end());
    return documents;
  }

  void count(Usages& usages, std::map<std::string, std::size_t>& local,
      const std::string& key) {
    ++usages[key].m_count;
    ++local[key];
  }

  void finish(Usages& usages, const std::map<std::string, std::size_t>& local,
      const std::string& name) {
    for(auto& entry : local) {
      auto& usage = usages[entry.first];
      ++usage.m_documents;
      if(usage.m_example.empty()) {
        usage.m_example = name;
      }
    }
  }

  NodeId find_artboard(const Document& document, const NodeId& owner) {
    auto current = owner;
    for(auto depth = 0; depth != 512; ++depth) {
      auto node = document.find(current);
      if(!node) {
        return NodeId();
      }
      if(node->m_parent.is_nil()) {
        if(document.is_on_canvas(current)) {
          return node->get_artboard() ? current : NodeId();
        }
        auto container = NodeId();
        document.get_nodes().for_each([&] (const auto& candidate) {
          if(!container.is_nil()) {
            return;
          }
          if(auto group = candidate.get_group()) {
            if(auto& instance = group->m_component_instance) {
              for(auto& state : instance->m_states) {
                if(state.m_tree == current) {
                  container = candidate.get_id();
                }
              }
            }
          }
        });
        if(container.is_nil()) {
          return NodeId();
        }
        current = container;
        continue;
      }
      current = node->m_parent;
    }
    return NodeId();
  }

  std::string get_category(const std::string& report) {
    auto colon = report.find(':');
    if(colon == std::string::npos) {
      return report;
    }
    return report.substr(0, colon);
  }

  void print(const std::string& title, const Usages& usages) {
    std::cout << title << '\n';
    for(auto& entry : usages) {
      std::cout << "  " << entry.first << ": " << entry.second.m_count <<
        " in " << entry.second.m_documents << " documents";
      if(!entry.second.m_example.empty()) {
        std::cout << " (e.g. " << entry.second.m_example << ')';
      }
      std::cout << '\n';
    }
  }
}

int main(int argc, char** argv) {
  if(argc < 2) {
    std::cerr << "Usage: PrototypeReport <corpus> [list]\n";
    return 1;
  }
  auto corpus = std::filesystem::path(argv[1]);
  auto documents = std::vector<std::filesystem::path>();
  try {
    documents = list_documents(corpus, argc > 2 ? argv[2] : nullptr);
  } catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  auto actions = Usages();
  auto triggers = Usages();
  auto reports = Usages();
  auto warnings = Usages();
  auto failures = std::vector<std::string>();
  auto document_count = std::size_t(0);
  auto prototype_count = std::size_t(0);
  auto interaction_count = std::size_t(0);
  auto played_count = std::size_t(0);
  auto unplaced_count = std::size_t(0);
  auto boardless_count = std::size_t(0);
  for(auto& relative : documents) {
    auto name = to_name(relative);
    ++document_count;
    try {
      auto package = XdPackage(corpus / relative);
      auto raw = package.read_interactions();
      auto local_actions = std::map<std::string, std::size_t>();
      auto local_triggers = std::map<std::string, std::size_t>();
      auto owners = raw.value("interactions").toObject();
      for(auto owner = owners.begin(); owner != owners.end(); ++owner) {
        for(auto entry : owner.value().toArray()) {
          auto interaction = entry.toObject();
          count(actions, local_actions,
            interaction.value("action").toString().toStdString());
          count(triggers, local_triggers,
            interaction.value("triggerEvent").toString().toStdString());
        }
      }
      finish(actions, local_actions, name);
      finish(triggers, local_triggers, name);
      auto document = Document();
      auto import = import_xd(package, document);
      auto local_warnings = std::map<std::string, std::size_t>();
      for(auto& warning : import.m_warnings) {
        if(warning.first.find("nteraction") != std::string::npos ||
            warning.first.find("artboard") != std::string::npos) {
          warnings[warning.first].m_count += warning.second;
          local_warnings[warning.first] += warning.second;
        }
      }
      finish(warnings, local_warnings, name);
      auto& interactions = document.get_prototype().m_interactions;
      if(interactions.empty()) {
        continue;
      }
      ++prototype_count;
      auto player = PreviewPlayer(copy_document(document));
      auto& stage = player.get_stage();
      auto first = NodeId();
      for(auto& id : stage.get_canvas()) {
        if(stage.get(id).get_artboard()) {
          first = id;
          break;
        }
      }
      if(first.is_nil()) {
        boardless_count += interactions.size();
        continue;
      }
      auto local_reports = std::map<std::string, std::size_t>();
      auto ids = std::vector<InteractionId>();
      for(auto& interaction : stage.get_prototype().m_interactions) {
        ids.push_back(interaction.m_id);
      }
      for(auto& id : ids) {
        ++interaction_count;
        auto owner = NodeId();
        for(auto& interaction : stage.get_prototype().m_interactions) {
          if(interaction.m_id == id) {
            owner = interaction.m_owner;
          }
        }
        auto artboard = find_artboard(stage, owner);
        auto is_unplaced = artboard.is_nil();
        if(is_unplaced) {
          ++unplaced_count;
          artboard = first;
        }
        player.start(artboard);
        auto reported = player.get_reports().size();
        if(player.trigger(id)) {
          ++played_count;
        }
        for(auto step = 0; step != 600 && player.is_animating(); ++step) {
          player.advance(1.0 / 60);
        }
        player.advance(10);
        for(auto step = 0; step != 600 && player.is_animating(); ++step) {
          player.advance(1.0 / 60);
        }
        auto& messages = player.get_reports();
        for(auto i = reported; i != messages.size(); ++i) {
          auto category = get_category(messages[i]);
          if(is_unplaced) {
            category += " (outside any artboard)";
          }
          count(reports, local_reports, category);
        }
      }
      finish(reports, local_reports, name);
    } catch(const std::exception& error) {
      failures.push_back(name + ": " + error.what());
    }
  }
  std::cout << "Documents: " << document_count << '\n';
  std::cout << "Documents with interactions: " << prototype_count << '\n';
  std::cout << "Interactions played: " << interaction_count << '\n';
  std::cout << "Interactions whose action did something: " << played_count <<
    '\n';
  std::cout << "Interactions outside any artboard: " << unplaced_count << '\n';
  std::cout << "Interactions of documents without artboards: " <<
    boardless_count << '\n';
  print("Actions:", actions);
  std::cout << "Unsupported actions:\n";
  for(auto& entry : actions) {
    if(!std::ranges::contains(SUPPORTED_ACTIONS, entry.first)) {
      std::cout << "  " << entry.first << ": " << entry.second.m_count <<
        '\n';
    }
  }
  print("Triggers:", triggers);
  std::cout << "Unsupported triggers:\n";
  for(auto& entry : triggers) {
    if(!std::ranges::contains(SUPPORTED_TRIGGERS, entry.first)) {
      std::cout << "  " << entry.first << ": " << entry.second.m_count <<
        '\n';
    }
  }
  print("Import warnings:", warnings);
  print("Player reports:", reports);
  std::cout << "Failures: " << failures.size() << '\n';
  for(auto& failure : failures) {
    std::cout << "  " << failure << '\n';
  }
  return failures.empty() ? 0 : 1;
}
