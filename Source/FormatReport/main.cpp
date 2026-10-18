#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include "Rellu/Format/DocumentComparison.hpp"
#include "Rellu/Format/DocumentFile.hpp"
#include "Rellu/Io/XdImporters.hpp"

using namespace Rellu;

namespace {
  struct Result {
    std::string m_name;
    bool m_is_imported = false;
    bool m_is_equal = false;
    std::string m_error;
    std::vector<std::string> m_differences;
    std::size_t m_node_count = 0;
    std::uintmax_t m_xd_size = 0;
    std::uintmax_t m_size = 0;
    double m_import_time = 0;
    double m_save_time = 0;
    double m_load_time = 0;
  };

  double get_milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
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
        documents.push_back(std::filesystem::relative(entry.path(), corpus));
      }
    }
    std::sort(documents.begin(), documents.end());
    return documents;
  }

  void test(const std::filesystem::path& source,
      const std::filesystem::path& target, Result& result) {
    result.m_xd_size = std::filesystem::file_size(source);
    auto document = Document();
    auto start = std::chrono::steady_clock::now();
    auto report = import_xd(source, document);
    result.m_import_time = get_milliseconds(start);
    result.m_is_imported = true;
    result.m_node_count = report.m_node_count;
    start = std::chrono::steady_clock::now();
    save(document, target);
    result.m_save_time = get_milliseconds(start);
    result.m_size = std::filesystem::file_size(target);
    auto loaded = Document();
    start = std::chrono::steady_clock::now();
    load(target, loaded);
    result.m_load_time = get_milliseconds(start);
    result.m_differences = find_differences(document, loaded, 5);
    result.m_is_equal = result.m_differences.empty();
  }

  double get_ratio(std::uintmax_t size, std::uintmax_t xd_size) {
    if(xd_size == 0) {
      return 0;
    }
    return static_cast<double>(size) / static_cast<double>(xd_size);
  }

  void print_slowest(std::string_view title, std::vector<Result> results,
      double Result::* time) {
    std::sort(results.begin(), results.end(),
      [&] (const auto& left, const auto& right) {
        return left.*time > right.*time;
      });
    results.resize(std::min<std::size_t>(results.size(), 10));
    std::cout << std::format("\n== slowest {}\n", title);
    for(auto& result : results) {
      std::cout << std::format("{:10.1f} ms (import {:9.1f}) {:9} nodes  {}\n",
        result.*time, result.m_import_time, result.m_node_count,
        result.m_name);
    }
  }
}

int main(int argc, char** argv) {
  if(argc < 2 || argc > 3) {
    std::cerr << "Usage: FormatReport <corpus directory> [list file]\n";
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
  auto target = std::filesystem::temp_directory_path() /
    ("FormatReport-" + to_string(generate_node_id()) + FORMAT_EXTENSION);
  auto results = std::vector<Result>();
  results.reserve(documents.size());
  std::cout << std::format("{:4} {:>9} {:>10} {:>10} {:>6} {:>9} {:>8} "
    "{:>8}  {}\n", "", "nodes", "xd KB", "rellu KB", "ratio", "import ms",
    "save ms", "load ms", "document");
  for(auto& document_path : documents) {
    auto result = Result();
    try {
      test(corpus / document_path, target, result);
    } catch(const std::exception& error) {
      result.m_error = error.what();
    }
    result.m_name = to_name(document_path);
    if(result.m_is_equal) {
      std::cout << std::format("ok   {:9} {:10.1f} {:10.1f} {:6.3f} {:9.1f} "
        "{:8.1f} {:8.1f}  {}\n", result.m_node_count,
        result.m_xd_size / 1024.0, result.m_size / 1024.0,
        get_ratio(result.m_size, result.m_xd_size), result.m_import_time,
        result.m_save_time, result.m_load_time, result.m_name);
    } else if(!result.m_error.empty()) {
      std::cout << std::format("FAIL {}: {}\n", result.m_name,
        result.m_error);
    } else {
      std::cout << std::format("DIFF {}\n", result.m_name);
      for(auto& difference : result.m_differences) {
        std::cout << "       " << difference << '\n';
      }
    }
    std::cout.flush();
    results.push_back(std::move(result));
  }
  auto error = std::error_code();
  std::filesystem::remove(target, error);
  auto imported = std::size_t(0);
  auto equal = std::size_t(0);
  auto xd_size = std::uintmax_t(0);
  auto size = std::uintmax_t(0);
  auto import_time = 0.0;
  auto save_time = 0.0;
  auto load_time = 0.0;
  auto node_count = std::size_t(0);
  auto ratios = std::vector<double>();
  auto round_trips = std::vector<Result>();
  for(auto& result : results) {
    if(result.m_is_imported) {
      ++imported;
    }
    if(!result.m_is_equal) {
      continue;
    }
    ++equal;
    xd_size += result.m_xd_size;
    size += result.m_size;
    import_time += result.m_import_time;
    save_time += result.m_save_time;
    load_time += result.m_load_time;
    node_count += result.m_node_count;
    ratios.push_back(get_ratio(result.m_size, result.m_xd_size));
    round_trips.push_back(result);
  }
  std::sort(ratios.begin(), ratios.end());
  auto get_percentile = [&] (double fraction) {
    if(ratios.empty()) {
      return 0.0;
    }
    return ratios[static_cast<std::size_t>(fraction * (ratios.size() - 1))];
  };
  std::cout << std::format("\n== summary\n{} documents, {} imported, {} "
    "round trips equal, {} nodes\n", results.size(), imported, equal,
    node_count);
  std::cout << std::format("size: xd {:.1f} MB, rellu {:.1f} MB, total ratio "
    "{:.3f}; per document ratio min {:.3f}, median {:.3f}, max {:.3f}\n",
    xd_size / 1048576.0, size / 1048576.0, get_ratio(size, xd_size),
    get_percentile(0), get_percentile(0.5), get_percentile(1));
  std::cout << std::format("time: import {:.1f} s, save {:.1f} s, load {:.1f} "
    "s\n", import_time / 1000, save_time / 1000, load_time / 1000);
  print_slowest("saves", round_trips, &Result::m_save_time);
  print_slowest("loads", round_trips, &Result::m_load_time);
  auto failures = std::count_if(results.begin(), results.end(),
    [] (const auto& result) { return !result.m_is_equal; });
  if(failures != 0) {
    std::cout << "\n== failures\n";
    for(auto& result : results) {
      if(!result.m_is_equal) {
        std::cout << result.m_name << ": " << (result.m_error.empty() ?
          result.m_differences.front() : result.m_error) << '\n';
      }
    }
  }
  return failures == 0 ? 0 : 1;
}
