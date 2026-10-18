#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "Rellu/Io/XdImporters.hpp"

using namespace Rellu;

namespace {
  struct Usage {
    std::size_t m_count = 0;
    std::size_t m_documents = 0;
  };

  struct Result {
    std::string m_name;
    bool m_is_imported = false;
    std::string m_error;
    double m_read_time = 0;
    double m_build_time = 0;
    std::size_t m_node_count = 0;
  };

  double to_milliseconds(std::chrono::nanoseconds duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

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
        documents.push_back(
          std::filesystem::relative(entry.path(), corpus));
      }
    }
    std::sort(documents.begin(), documents.end());
    return documents;
  }

  void add(std::map<std::string, Usage, std::less<>>& usages,
      const std::map<std::string, std::size_t, std::less<>>& counts) {
    for(auto& entry : counts) {
      auto& usage = usages[entry.first];
      usage.m_count += entry.second;
      ++usage.m_documents;
    }
  }

  void print_table(const std::string& title,
      const std::map<std::string, Usage, std::less<>>& usages) {
    auto rows = std::vector<std::pair<std::string, Usage>>(
      usages.begin(), usages.end());
    std::stable_sort(rows.begin(), rows.end(),
      [] (const auto& left, const auto& right) {
        return left.second.m_count > right.second.m_count;
      });
    std::cout << std::format("\n== {} ({})\n{:>10} {:>9}  {}\n", title,
      rows.size(), "count", "documents", "path");
    for(auto& row : rows) {
      std::cout << std::format("{:>10} {:>9}  {}\n", row.second.m_count,
        row.second.m_documents, row.first);
    }
  }
}

int main(int argc, char** argv) {
  if(argc < 2 || argc > 3) {
    std::cerr << "Usage: XdReport <corpus directory> [list file]\n";
    return 2;
  }
  auto corpus = std::filesystem::path(argv[1]);
  auto documents = std::vector<std::filesystem::path>();
  try {
    documents = list_documents(corpus, argc == 3 ? argv[2] : nullptr);
  } catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
  auto unconsumed = std::map<std::string, Usage, std::less<>>();
  auto warnings = std::map<std::string, Usage, std::less<>>();
  auto results = std::vector<Result>();
  results.reserve(documents.size());
  auto start = std::chrono::steady_clock::now();
  for(auto& document_path : documents) {
    auto result = Result();
    result.m_name = to_name(document_path);
    try {
      auto document = Document();
      auto report = import_xd(corpus / document_path, document);
      result.m_is_imported = true;
      result.m_read_time = to_milliseconds(report.m_read_time);
      result.m_build_time = to_milliseconds(report.m_build_time);
      result.m_node_count = report.m_node_count;
      add(unconsumed, report.m_unconsumed);
      add(warnings, report.m_warnings);
      std::cout << std::format("ok   {:10.1f} ms (read {:9.1f}, build "
        "{:9.1f}) {:9} nodes  {}\n", result.m_read_time + result.m_build_time,
        result.m_read_time, result.m_build_time, result.m_node_count,
        result.m_name);
    } catch(const std::exception& error) {
      result.m_error = error.what();
      std::cout << std::format("FAIL {}: {}\n", result.m_name, result.m_error);
    }
    std::cout.flush();
    results.push_back(std::move(result));
  }
  auto elapsed = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - start).count();
  auto imported = std::count_if(results.begin(), results.end(),
    [] (const auto& result) { return result.m_is_imported; });
  auto total_read = 0.0;
  auto total_build = 0.0;
  auto total_nodes = std::size_t(0);
  for(auto& result : results) {
    total_read += result.m_read_time;
    total_build += result.m_build_time;
    total_nodes += result.m_node_count;
  }
  std::cout << std::format("\n== summary\nimported {} of {} documents, {} "
    "nodes, in {:.1f} s (read {:.1f} s, build {:.1f} s)\n", imported,
    results.size(), total_nodes, elapsed, total_read / 1000,
    total_build / 1000);
  auto slowest = results;
  std::sort(slowest.begin(), slowest.end(),
    [] (const auto& left, const auto& right) {
      return left.m_read_time + left.m_build_time >
        right.m_read_time + right.m_build_time;
    });
  slowest.resize(std::min<std::size_t>(slowest.size(), 10));
  std::cout << "\n== slowest\n";
  for(auto& result : slowest) {
    std::cout << std::format("{:10.1f} ms (read {:9.1f}, build {:9.1f}) "
      "{:9} nodes  {}\n", result.m_read_time + result.m_build_time,
      result.m_read_time, result.m_build_time, result.m_node_count,
      result.m_name);
  }
  print_table("warnings", warnings);
  print_table("unconsumed keys", unconsumed);
  return imported == static_cast<std::ptrdiff_t>(results.size()) ? 0 : 1;
}
