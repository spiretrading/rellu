#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/DocumentComparison.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  auto live_bytes = std::atomic<std::int64_t>(0);
  auto peak_bytes = std::atomic<std::int64_t>(0);

  constexpr auto HEADER = std::size_t(16);

  void* allocate(std::size_t size) {
    auto block = static_cast<char*>(std::malloc(size + HEADER));
    if(!block) {
      throw std::bad_alloc();
    }
    *reinterpret_cast<std::size_t*>(block) = size;
    auto live = live_bytes.fetch_add(static_cast<std::int64_t>(size)) +
      static_cast<std::int64_t>(size);
    auto peak = peak_bytes.load();
    while(live > peak && !peak_bytes.compare_exchange_weak(peak, live)) {}
    return block + HEADER;
  }

  void deallocate(void* pointer) noexcept {
    if(!pointer) {
      return;
    }
    auto block = static_cast<char*>(pointer) - HEADER;
    live_bytes.fetch_sub(
      static_cast<std::int64_t>(*reinterpret_cast<std::size_t*>(block)));
    std::free(block);
  }
}

void* operator new(std::size_t size) {
  return allocate(size);
}

void* operator new[](std::size_t size) {
  return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return allocate(size);
  } catch(...) {
    return nullptr;
  }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return allocate(size);
  } catch(...) {
    return nullptr;
  }
}

void operator delete(void* pointer) noexcept {
  deallocate(pointer);
}

void operator delete[](void* pointer) noexcept {
  deallocate(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
  deallocate(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
  deallocate(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
  deallocate(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
  deallocate(pointer);
}

namespace {
  struct Usage {
    std::size_t m_count = 0;
    std::size_t m_documents = 0;
  };

  using Usages = std::map<std::string, Usage, std::less<>>;
  using Counts = std::map<std::string, std::size_t, std::less<>>;

  struct Result {
    std::string m_name;
    bool m_is_complete = false;
    std::string m_error;
    std::size_t m_node_count = 0;
    std::size_t m_original_size = 0;
    std::size_t m_written_size = 0;
    double m_import_time = 0;
    double m_preservation_time = 0;
    double m_write_time = 0;
    double m_reimport_time = 0;
    double m_retained = 0;
    double m_peak = 0;
    std::size_t m_opaque_size = 0;
    std::size_t m_package_size = 0;
    std::vector<std::string> m_structural;
    Counts m_json;
  };

  double to_milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  double to_megabytes(std::int64_t bytes) {
    return static_cast<double>(bytes) / (1024 * 1024);
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

  bool is_json(std::string_view name) {
    return name == "manifest" || name.ends_with(".agc") ||
      name.ends_with(".json");
  }

  std::string get_root(std::string_view name) {
    if(name == "manifest") {
      return "manifest";
    } else if(name.ends_with(".agc")) {
      return "agc";
    }
    return std::string(name);
  }

  bool is_identifier(const QString& key) {
    if(key.size() != 36) {
      return false;
    }
    for(auto i = qsizetype(0); i != key.size(); ++i) {
      auto c = key[i].unicode();
      if(i == 8 || i == 13 || i == 18 || i == 23) {
        if(c != '-') {
          return false;
        }
      } else if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
        return false;
      }
    }
    return true;
  }

  std::string get_type_name(const QJsonValue& value) {
    switch(value.type()) {
      case QJsonValue::Bool:
        return "bool";
      case QJsonValue::Double:
        return "number";
      case QJsonValue::String:
        return "str";
      case QJsonValue::Null:
        return "null";
      case QJsonValue::Array:
        return "array";
      case QJsonValue::Object:
        return "object";
      default:
        return "undefined";
    }
  }

  void compare(const QJsonValue& original, const QJsonValue& written,
      std::string path, Counts& differences) {
    if(original.isObject() && written.isObject()) {
      auto left = original.toObject();
      auto right = written.toObject();
      auto type = left.value("type"_L1).toString();
      if(type == "shape"_L1 || type == "group"_L1 || type == "text"_L1 ||
          type == "artboard"_L1 || type == "syncRef"_L1) {
        path = "<" + type.toStdString() + ">";
      }
      for(auto i = left.constBegin(); i != left.constEnd(); ++i) {
        auto key = is_identifier(i.key()) ? std::string("<id>") :
          i.key().toStdString();
        auto other = right.constFind(i.key());
        if(other == right.constEnd()) {
          ++differences["missing " + path + "." + key];
        } else {
          compare(i.value(), other.value(), path + "." + key, differences);
        }
      }
      for(auto i = right.constBegin(); i != right.constEnd(); ++i) {
        if(!left.contains(i.key())) {
          auto key = is_identifier(i.key()) ? std::string("<id>") :
            i.key().toStdString();
          ++differences["extra " + path + "." + key];
        }
      }
    } else if(original.isArray() && written.isArray()) {
      auto left = original.toArray();
      auto right = written.toArray();
      if(left.size() != right.size()) {
        ++differences["length " + path];
        return;
      }
      for(auto i = qsizetype(0); i != left.size(); ++i) {
        compare(left.at(i), right.at(i), path + "[]", differences);
      }
    } else if(original.type() != written.type()) {
      ++differences["type " + path + " " + get_type_name(original) + "/" +
        get_type_name(written)];
    } else if(original.isDouble()) {
      auto left = original.toDouble();
      auto right = written.toDouble();
      if(left != right && !(std::isnan(left) && std::isnan(right))) {
        if(std::abs(left - right) <=
            1e-12 * std::max(std::abs(left), std::abs(right))) {
          ++differences["rounding " + path];
        } else {
          ++differences["value " + path];
        }
      }
    } else if(original != written) {
      ++differences["value " + path];
    }
  }

  QJsonValue parse(const QByteArray& bytes) {
    auto error = QJsonParseError();
    auto document = QJsonDocument::fromJson(bytes, &error);
    if(error.error != QJsonParseError::NoError) {
      return QJsonValue(QJsonValue::Undefined);
    }
    if(document.isArray()) {
      return document.array();
    }
    return document.object();
  }

  void compare_packages(const XdPackage& original, const XdPackage& written,
      Counts& differences) {
    auto names = std::unordered_set<std::string>(
      written.get_entries().begin(), written.get_entries().end());
    for(auto& name : original.get_entries()) {
      if(!names.contains(name)) {
        ++differences["missing entry " + name];
        continue;
      }
      auto left = original.read(name);
      auto right = written.read(name);
      if(is_json(name)) {
        auto left_json = parse(left);
        auto right_json = parse(right);
        if(right_json.isUndefined()) {
          ++differences["malformed " + name];
          continue;
        }
        compare(left_json, right_json, get_root(name), differences);
        if(name == "manifest" && left != right) {
          ++differences["bytes manifest"];
        }
      } else if(left != right) {
        ++differences["bytes " + name];
      }
    }
    auto original_names = std::unordered_set<std::string>(
      original.get_entries().begin(), original.get_entries().end());
    for(auto& name : written.get_entries()) {
      if(!original_names.contains(name)) {
        ++differences["extra entry " + name];
      }
    }
    if(original.get_entries() != written.get_entries()) {
      auto common = std::vector<std::string>();
      for(auto& name : original.get_entries()) {
        if(names.contains(name)) {
          common.push_back(name);
        }
      }
      auto written_common = std::vector<std::string>();
      for(auto& name : written.get_entries()) {
        if(original_names.contains(name)) {
          written_common.push_back(name);
        }
      }
      if(common != written_common) {
        ++differences["entry order"];
      }
    }
  }

  void add(Usages& usages, const Counts& counts) {
    for(auto& entry : counts) {
      auto& usage = usages[entry.first];
      usage.m_count += entry.second;
      ++usage.m_documents;
    }
  }

  void print_table(const std::string& title, const Usages& usages,
      std::size_t limit) {
    auto rows = std::vector<std::pair<std::string, Usage>>(
      usages.begin(), usages.end());
    std::stable_sort(rows.begin(), rows.end(),
      [] (const auto& left, const auto& right) {
        return left.second.m_count > right.second.m_count;
      });
    std::cout << std::format("\n== {} ({})\n{:>10} {:>9}  {}\n", title,
      rows.size(), "count", "documents", "path");
    for(auto i = std::size_t(0); i != std::min(rows.size(), limit); ++i) {
      auto& row = rows[i];
      std::cout << std::format("{:>10} {:>9}  {}\n", row.second.m_count,
        row.second.m_documents, row.first);
    }
  }

  XdImporter make_baseline_importer() {
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

  Result run(const std::filesystem::path& source,
      const std::filesystem::path& temporary, bool is_baseline,
      Usages& preserved) {
    std::filesystem::create_directories(temporary.parent_path());
    auto result = Result();
    result.m_original_size =
      static_cast<std::size_t>(std::filesystem::file_size(source));
    auto base = live_bytes.load();
    peak_bytes = base;
    auto start = std::chrono::steady_clock::now();
    auto document = Document();
    auto report = ImportReport();
    {
      auto package = XdPackage(source);
      if(is_baseline) {
        static const auto importer = make_baseline_importer();
        report = importer.import(package, document);
      } else {
        report = import_xd(package, document);
      }
    }
    result.m_import_time =
      to_milliseconds(std::chrono::steady_clock::now() - start);
    result.m_retained = to_megabytes(live_bytes.load() - base);
    result.m_peak = to_megabytes(peak_bytes.load() - base);
    result.m_node_count = report.m_node_count;
    result.m_preservation_time = std::chrono::duration<double, std::milli>(
      report.m_preservation_time).count();
    result.m_opaque_size = report.m_opaque_size;
    result.m_package_size = report.m_package_size;
    add(preserved, report.m_preserved);
    if(is_baseline) {
      result.m_is_complete = true;
      return result;
    }
    start = std::chrono::steady_clock::now();
    write_xd(document, temporary);
    result.m_write_time =
      to_milliseconds(std::chrono::steady_clock::now() - start);
    result.m_written_size =
      static_cast<std::size_t>(std::filesystem::file_size(temporary));
    start = std::chrono::steady_clock::now();
    auto reimported = Document();
    import_xd(temporary, reimported);
    result.m_reimport_time =
      to_milliseconds(std::chrono::steady_clock::now() - start);
    result.m_structural = find_differences(document, reimported, 10);
    compare_packages(XdPackage(source), XdPackage(temporary), result.m_json);
    result.m_is_complete = true;
    return result;
  }
}

int main(int argc, char** argv) {
  auto arguments = std::vector<std::string>(argv + 1, argv + argc);
  auto is_baseline = false;
  auto is_kept = false;
  auto output = std::filesystem::path();
  while(!arguments.empty() && arguments.front().starts_with("--")) {
    if(arguments.front() == "--baseline") {
      is_baseline = true;
    } else if(arguments.front() == "--keep") {
      is_kept = true;
    } else if(arguments.front() == "--output" && arguments.size() >= 2) {
      arguments.erase(arguments.begin());
      output = arguments.front();
    } else {
      break;
    }
    arguments.erase(arguments.begin());
  }
  if(arguments.empty() || arguments.size() > 3) {
    std::cerr << "Usage: XdRoundTrip [--baseline] [--keep] <corpus directory> "
      "[list file] [temporary directory]\n";
    return 2;
  }
  auto corpus = std::filesystem::path(arguments[0]);
  auto directory = std::filesystem::path(".");
  if(arguments.size() == 3) {
    directory = arguments[2];
  }
  auto temporary = directory / "XdRoundTrip.tmp.xd";
  auto documents = std::vector<std::filesystem::path>();
  try {
    documents = list_documents(corpus,
      arguments.size() >= 2 && !arguments[1].empty() ?
        arguments[1].c_str() : nullptr);
  } catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
  auto structural = Usages();
  auto json = Usages();
  auto preserved = Usages();
  auto results = std::vector<Result>();
  auto start = std::chrono::steady_clock::now();
  for(auto& document_path : documents) {
    auto result = Result();
    try {
      result = run(corpus / document_path,
        output.empty() ? temporary : output / document_path, is_baseline,
        preserved);
    } catch(const std::exception& error) {
      result.m_error = error.what();
    }
    result.m_name = to_name(document_path);
    if(!result.m_is_complete) {
      std::cout << std::format("FAIL {}: {}\n", result.m_name, result.m_error);
    } else {
      auto json_count = std::size_t(0);
      for(auto& entry : result.m_json) {
        json_count += entry.second;
      }
      auto status = std::string(result.m_structural.empty() ?
        (json_count == 0 ? "same" : "json") : "DIFF");
      if(is_baseline) {
        status = "base";
      }
      std::cout << std::format("{:4} {:8} nodes  import {:8.1f} ms (keep "
        "{:7.1f})  write {:8.1f}  reimport {:8.1f}  heap {:7.1f} MB (peak "
        "{:7.1f})  opaque {:7.1f} KB + {:7.1f} KB  size {:9} -> {:9}  "
        "json {:6}  {}\n", status, result.m_node_count, result.m_import_time,
        result.m_preservation_time, result.m_write_time,
        result.m_reimport_time, result.m_retained, result.m_peak,
        result.m_opaque_size / 1024.0, result.m_package_size / 1024.0,
        result.m_original_size, result.m_written_size, json_count,
        result.m_name);
      for(auto& difference : result.m_structural) {
        std::cout << "     " << difference << '\n';
      }
      auto counts = Counts();
      for(auto& difference : result.m_structural) {
        auto key = difference;
        if(auto colon = key.find(": "); colon != std::string::npos) {
          key = key.substr(colon + 2);
        }
        ++counts[key];
      }
      add(structural, counts);
      add(json, result.m_json);
    }
    std::cout.flush();
    results.push_back(std::move(result));
  }
  if(!is_kept) {
    auto error = std::error_code();
    std::filesystem::remove(temporary, error);
  }
  auto elapsed = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - start).count();
  auto complete = std::size_t(0);
  auto identical = std::size_t(0);
  auto equivalent = std::size_t(0);
  auto import = 0.0;
  auto keep = 0.0;
  auto write = 0.0;
  auto reimport = 0.0;
  auto original_size = std::size_t(0);
  auto written_size = std::size_t(0);
  auto opaque_size = std::size_t(0);
  for(auto& result : results) {
    if(!result.m_is_complete) {
      continue;
    }
    ++complete;
    if(result.m_structural.empty()) {
      ++identical;
      if(result.m_json.empty()) {
        ++equivalent;
      }
    }
    import += result.m_import_time;
    keep += result.m_preservation_time;
    write += result.m_write_time;
    reimport += result.m_reimport_time;
    original_size += result.m_original_size;
    written_size += result.m_written_size;
    opaque_size += result.m_opaque_size + result.m_package_size;
  }
  std::cout << std::format("\n== summary\n{} documents, {} completed, {} "
    "structurally identical, {} JSON-equivalent, in {:.1f} s\nimport {:.1f} "
    "s (preservation {:.1f} s), write {:.1f} s, reimport {:.1f} s\nsize "
    "{:.1f} MB -> {:.1f} MB, opaque data {:.1f} MB\n", results.size(),
    complete, identical, equivalent, elapsed, import / 1000, keep / 1000,
    write / 1000, reimport / 1000, original_size / 1048576.0,
    written_size / 1048576.0, opaque_size / 1048576.0);
  print_table("structural differences", structural, 100);
  print_table("JSON differences", json, 200);
  print_table("preserved entries", preserved, 100);
  return equivalent == results.size() || is_baseline &&
    complete == results.size() ? 0 : 1;
}
