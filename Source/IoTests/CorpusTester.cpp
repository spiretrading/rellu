#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QString>
#include <QtGlobal>
#include "Rellu/Io/XdImporters.hpp"

using namespace Rellu;

namespace {
  std::filesystem::path get_path(const char* variable, const char* fallback) {
    auto value = qEnvironmentVariable(variable);
    if(value.isEmpty()) {
      value = QString::fromUtf8(fallback);
    }
    return std::filesystem::path(value.toStdU16String());
  }

  std::vector<std::string> read_golden_list(
      const std::filesystem::path& references) {
    auto paths = std::vector<std::string>();
    auto file = std::ifstream(references / "exported.txt");
    auto line = std::string();
    while(std::getline(file, line)) {
      while(!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
      }
      if(!line.empty()) {
        paths.push_back(line);
      }
    }
    return paths;
  }
}

TEST_SUITE("Corpus") {
  TEST_CASE("golden_documents") {
    auto corpus =
      get_path("RELLU_CORPUS", "C:/Users/Kranar/Nextcloud/Spire Desktop");
    auto references = get_path(
      "RELLU_REFERENCES", "C:/Users/Kranar/Nextcloud/rellu_references");
    if(!std::filesystem::is_directory(corpus) ||
        !std::filesystem::exists(references / "exported.txt")) {
      MESSAGE("The Adobe XD corpus is not available.");
      return;
    }
    auto paths = read_golden_list(references);
    REQUIRE(!paths.empty());
    for(auto& path : paths) {
      CAPTURE(path);
      auto document = Document();
      auto file = corpus / std::filesystem::path(
        std::u8string(path.begin(), path.end()) + u8".xd");
      auto report = ImportReport();
      REQUIRE_NOTHROW(report = import_xd(file, document));
      REQUIRE(report.m_node_count == document.get_nodes().get_size());
      REQUIRE(!document.get_canvas().empty());
    }
  }
}
