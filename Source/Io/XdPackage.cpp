#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <private/qzipreader_p.h>
#include "Rellu/Io/XdError.hpp"
#include "Rellu/Io/XdPackage.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

struct XdPackage::Data {
  struct Entry {
    qint64 m_size;
    std::uint32_t m_crc;
  };
  QByteArray m_bytes;
  QBuffer m_buffer;
  std::unique_ptr<QZipReader> m_reader;
  std::mutex m_mutex;
  std::vector<std::string> m_entries;
  std::unordered_map<std::string, Entry> m_sizes;
  std::vector<std::string> m_artboard_entries;
  std::vector<std::string> m_image_entries;
  QJsonObject m_manifest;
};

namespace {
  using CrcTable = std::array<std::array<std::uint32_t, 256>, 8>;

  CrcTable make_crc_table() {
    auto table = CrcTable();
    for(auto i = std::uint32_t(0); i != 256; ++i) {
      auto value = i;
      for(auto bit = 0; bit != 8; ++bit) {
        value = (value & 1) ? (value >> 1) ^ 0xEDB88320U : value >> 1;
      }
      table[0][i] = value;
    }
    for(auto i = std::size_t(0); i != 256; ++i) {
      for(auto slice = std::size_t(1); slice != 8; ++slice) {
        auto previous = table[slice - 1][i];
        table[slice][i] = (previous >> 8) ^ table[0][previous & 0xFF];
      }
    }
    return table;
  }

  std::uint32_t compute_crc(const QByteArray& bytes) {
    static const auto table = make_crc_table();
    auto crc = ~std::uint32_t(0);
    auto data = reinterpret_cast<const unsigned char*>(bytes.constData());
    auto size = static_cast<std::size_t>(bytes.size());
    while(size >= 8) {
      auto low = crc ^ (std::uint32_t(data[0]) |
        (std::uint32_t(data[1]) << 8) | (std::uint32_t(data[2]) << 16) |
        (std::uint32_t(data[3]) << 24));
      crc = table[7][low & 0xFF] ^ table[6][(low >> 8) & 0xFF] ^
        table[5][(low >> 16) & 0xFF] ^ table[4][low >> 24] ^
        table[3][data[4]] ^ table[2][data[5]] ^ table[1][data[6]] ^
        table[0][data[7]];
      data += 8;
      size -= 8;
    }
    while(size != 0) {
      crc = (crc >> 8) ^ table[0][(crc ^ *data) & 0xFF];
      ++data;
      --size;
    }
    return ~crc;
  }

  bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
  }

  bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
      text.substr(text.size() - suffix.size()) == suffix;
  }

  bool is_artboard_entry(std::string_view name) {
    return starts_with(name, "artwork/artboard-") &&
      ends_with(name, "/graphics/graphicContent.agc");
  }

  bool is_image_entry(std::string_view name) {
    return starts_with(name, "resources/") &&
      !starts_with(name, "resources/graphics/") && !ends_with(name, "/");
  }

  void collect_artboard_paths(const QJsonObject& node,
      std::vector<std::string>& paths) {
    auto children = node.value("children"_L1).toArray();
    for(auto child : children) {
      auto object = child.toObject();
      auto path = object.value("path"_L1).toString();
      if(path.startsWith("artboard-"_L1)) {
        paths.push_back(
          "artwork/" + path.toStdString() + "/graphics/graphicContent.agc");
      }
    }
  }
}

XdPackage::XdPackage(const std::filesystem::path& path)
    : m_data(std::make_unique<Data>()) {
  auto file = QFile(QString::fromStdU16String(path.u16string()));
  if(!file.open(QIODevice::ReadOnly)) {
    throw XdError("The package cannot be opened: " +
      file.errorString().toStdString());
  }
  m_data->m_bytes = file.readAll();
  if(m_data->m_bytes.size() != file.size()) {
    throw XdError("The package cannot be read: " +
      file.errorString().toStdString());
  }
  initialize();
}

XdPackage::XdPackage(QByteArray data)
    : m_data(std::make_unique<Data>()) {
  m_data->m_bytes = std::move(data);
  initialize();
}

XdPackage::XdPackage(XdPackage&& package) noexcept = default;

XdPackage::~XdPackage() = default;

const std::vector<std::string>& XdPackage::get_entries() const {
  return m_data->m_entries;
}

bool XdPackage::contains(std::string_view name) const {
  return m_data->m_sizes.contains(std::string(name));
}

QByteArray XdPackage::read(std::string_view name) const {
  auto key = std::string(name);
  auto size = m_data->m_sizes.find(key);
  if(size == m_data->m_sizes.end()) {
    throw XdError("The package is missing the entry " + key + ".");
  }
  auto bytes = [&] {
    auto lock = std::lock_guard(m_data->m_mutex);
    return m_data->m_reader->fileData(QString::fromStdString(key));
  }();
  if(bytes.size() != size->second.m_size ||
      compute_crc(bytes) != size->second.m_crc) {
    throw XdError("The package's entry " + key + " is corrupt.");
  }
  return bytes;
}

QJsonObject XdPackage::read_json(std::string_view name) const {
  auto bytes = read(name);
  auto error = QJsonParseError();
  auto document = QJsonDocument::fromJson(bytes, &error);
  if(error.error != QJsonParseError::NoError) {
    throw XdError("The package's entry " + std::string(name) +
      " is not valid JSON: " + error.errorString().toStdString() +
      " at offset " + std::to_string(error.offset) + ".");
  }
  if(!document.isObject()) {
    throw XdError(
      "The package's entry " + std::string(name) + " is not a JSON object.");
  }
  return document.object();
}

const QJsonObject& XdPackage::get_manifest() const {
  return m_data->m_manifest;
}

QJsonObject XdPackage::read_resources() const {
  return read_json(RESOURCES);
}

QJsonObject XdPackage::read_pasteboard() const {
  return read_json(PASTEBOARD);
}

const std::vector<std::string>& XdPackage::get_artboard_entries() const {
  return m_data->m_artboard_entries;
}

QJsonObject XdPackage::read_interactions() const {
  if(!contains(INTERACTIONS)) {
    return QJsonObject();
  }
  return read_json(INTERACTIONS);
}

const std::vector<std::string>& XdPackage::get_image_entries() const {
  return m_data->m_image_entries;
}

XdPackage& XdPackage::operator =(XdPackage&& package) noexcept = default;

void XdPackage::initialize() {
  auto& data = *m_data;
  data.m_buffer.setBuffer(&data.m_bytes);
  if(!data.m_buffer.open(QIODevice::ReadOnly)) {
    throw XdError("The package cannot be read.");
  }
  data.m_reader = std::make_unique<QZipReader>(&data.m_buffer);
  if(data.m_reader->status() != QZipReader::NoError ||
      !data.m_reader->isReadable()) {
    throw XdError("The package is not a readable ZIP archive.");
  }
  auto entries = data.m_reader->fileInfoList();
  if(entries.isEmpty()) {
    throw XdError("The package is not a ZIP archive or is empty.");
  }
  data.m_entries.reserve(static_cast<std::size_t>(entries.size()));
  for(auto& entry : entries) {
    if(!entry.isFile) {
      continue;
    }
    auto name = entry.filePath.toStdString();
    data.m_sizes.emplace(name, Data::Entry(entry.size, entry.crc));
    data.m_entries.push_back(name);
    if(is_image_entry(name)) {
      data.m_image_entries.push_back(name);
    }
  }
  data.m_manifest = read_json(MANIFEST);
  auto ordered = std::vector<std::string>();
  for(auto child : data.m_manifest.value("children"_L1).toArray()) {
    auto object = child.toObject();
    if(object.value("path"_L1).toString() == "artwork"_L1) {
      collect_artboard_paths(object, ordered);
    }
  }
  auto added = std::unordered_set<std::string>();
  for(auto& path : ordered) {
    if(data.m_sizes.contains(path) && added.insert(path).second) {
      data.m_artboard_entries.push_back(path);
    }
  }
  auto remaining = std::vector<std::string>();
  for(auto& entry : data.m_entries) {
    if(is_artboard_entry(entry) && !added.contains(entry)) {
      remaining.push_back(entry);
    }
  }
  std::sort(remaining.begin(), remaining.end());
  data.m_artboard_entries.insert(
    data.m_artboard_entries.end(), remaining.begin(), remaining.end());
}
