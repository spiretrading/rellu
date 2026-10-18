#include <utility>
#include "Rellu/Io/XdObject.hpp"

using namespace Rellu;

namespace {
  bool is_hexadecimal(QChar character) {
    auto code = character.unicode();
    return (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') ||
      (code >= 'A' && code <= 'F');
  }

  bool is_identifier(QStringView key) {
    if(key.size() != 36) {
      return false;
    }
    for(auto i = qsizetype(0); i != key.size(); ++i) {
      if(i == 8 || i == 13 || i == 18 || i == 23) {
        if(key[i] != u'-') {
          return false;
        }
      } else if(!is_hexadecimal(key[i])) {
        return false;
      }
    }
    return true;
  }

  void append_key(std::string& path, QStringView key) {
    path += '.';
    if(is_identifier(key)) {
      path += "<id>";
    } else {
      path += key.toUtf8().toStdString();
    }
  }

  void append_type(std::string& path, const QJsonValue& value) {
    switch(value.type()) {
      case QJsonValue::Bool:
        path += ":bool";
        break;
      case QJsonValue::Double:
        path += ":number";
        break;
      case QJsonValue::String:
        path += ":str";
        break;
      case QJsonValue::Null:
        path += ":null";
        break;
      case QJsonValue::Array:
        path += "[]";
        break;
      default:
        break;
    }
  }

  std::string make_path(const std::string& root,
      const std::vector<std::pair<const QJsonObject*, qsizetype>>& segments) {
    auto path = root;
    for(auto& segment : segments) {
      if(segment.first) {
        append_key(path, (segment.first->constBegin() + segment.second).key());
      } else {
        path += "[]";
      }
    }
    return path;
  }
}

XdObject::XdObject(QJsonObject json, std::string path,
    ImportReport& report) noexcept
  : m_json(std::move(json)),
    m_path(std::move(path)),
    m_report(&report),
    m_mask(0),
    m_is_consumed(false) {}

XdObject::XdObject(QJsonObject json) noexcept
  : m_json(std::move(json)),
    m_report(nullptr),
    m_mask(0),
    m_is_consumed(false) {}

XdObject::~XdObject() {
  if(m_report) {
    auto segments = std::vector<std::pair<const QJsonObject*, qsizetype>>();
    flush(m_path, segments, *m_report);
  }
}

const QJsonObject& XdObject::get_json() const {
  return m_json;
}

bool XdObject::contains(QLatin1StringView key) const {
  return m_json.contains(key);
}

QJsonValue XdObject::consume(QLatin1StringView key) {
  auto entry = m_json.constFind(key);
  if(entry == m_json.constEnd()) {
    return QJsonValue(QJsonValue::Undefined);
  }
  mark(entry - m_json.constBegin());
  return entry.value();
}

QJsonValue XdObject::consume(const QString& key) {
  auto entry = m_json.constFind(key);
  if(entry == m_json.constEnd()) {
    return QJsonValue(QJsonValue::Undefined);
  }
  mark(entry - m_json.constBegin());
  return entry.value();
}

std::optional<double> XdObject::get_number(QLatin1StringView key) {
  auto entry = m_json.constFind(key);
  if(entry == m_json.constEnd() || !entry.value().isDouble()) {
    return std::nullopt;
  }
  mark(entry - m_json.constBegin());
  return entry.value().toDouble();
}

double XdObject::get_number(QLatin1StringView key, double fallback) {
  if(auto number = get_number(key)) {
    return *number;
  }
  return fallback;
}

std::optional<bool> XdObject::get_bool(QLatin1StringView key) {
  auto entry = m_json.constFind(key);
  if(entry == m_json.constEnd() || !entry.value().isBool()) {
    return std::nullopt;
  }
  mark(entry - m_json.constBegin());
  return entry.value().toBool();
}

bool XdObject::get_bool(QLatin1StringView key, bool fallback) {
  if(auto value = get_bool(key)) {
    return *value;
  }
  return fallback;
}

std::optional<QString> XdObject::get_string(QLatin1StringView key) {
  auto entry = m_json.constFind(key);
  if(entry == m_json.constEnd() || !entry.value().isString()) {
    return std::nullopt;
  }
  mark(entry - m_json.constBegin());
  return entry.value().toString();
}

XdObject* XdObject::get_object(QLatin1StringView key) {
  return get_object(m_json.constFind(key));
}

XdObject* XdObject::get_object(const QString& key) {
  return get_object(m_json.constFind(key));
}

XdArray* XdObject::get_array(QLatin1StringView key) {
  return get_array(m_json.constFind(key));
}

XdArray* XdObject::get_array(const QString& key) {
  return get_array(m_json.constFind(key));
}

void XdObject::ignore(std::initializer_list<QLatin1StringView> keys) {
  for(auto key : keys) {
    auto entry = m_json.constFind(key);
    if(entry != m_json.constEnd()) {
      mark(entry - m_json.constBegin());
    }
  }
}

void XdObject::consume_all() {
  m_is_consumed = true;
}

bool XdObject::is_consumed(qsizetype index) const {
  if(m_is_consumed) {
    return true;
  } else if(index < 64) {
    return (m_mask & (std::uint64_t(1) << index)) != 0;
  }
  auto overflow = static_cast<std::size_t>(index - 64);
  return overflow < m_overflow.size() && m_overflow[overflow];
}

void XdObject::mark(qsizetype index) {
  if(index < 64) {
    m_mask |= std::uint64_t(1) << index;
    return;
  }
  auto overflow = static_cast<std::size_t>(index - 64);
  if(overflow >= m_overflow.size()) {
    m_overflow.resize(static_cast<std::size_t>(m_json.size() - 64));
  }
  m_overflow[overflow] = true;
}

XdObject* XdObject::get_object(QJsonObject::const_iterator entry) {
  if(entry == m_json.constEnd()) {
    return nullptr;
  }
  auto index = entry - m_json.constBegin();
  if(entry.value().isNull()) {
    mark(index);
    return nullptr;
  } else if(!entry.value().isObject()) {
    return nullptr;
  }
  for(auto& child : m_children) {
    if(child.m_index == index) {
      return child.m_object.get();
    }
  }
  mark(index);
  auto object =
    std::unique_ptr<XdObject>(new XdObject(entry.value().toObject()));
  auto result = object.get();
  m_children.push_back(Child(index, std::move(object), nullptr));
  return result;
}

XdArray* XdObject::get_array(QJsonObject::const_iterator entry) {
  if(entry == m_json.constEnd() || !entry.value().isArray()) {
    return nullptr;
  }
  auto index = entry - m_json.constBegin();
  for(auto& child : m_children) {
    if(child.m_index == index) {
      return child.m_array.get();
    }
  }
  mark(index);
  auto array = std::unique_ptr<XdArray>(new XdArray(entry.value().toArray()));
  auto result = array.get();
  m_children.push_back(Child(index, nullptr, std::move(array)));
  return result;
}

void XdObject::flush(const std::string& root,
    std::vector<std::pair<const QJsonObject*, qsizetype>>& segments,
    ImportReport& report) const {
  if(!m_is_consumed) {
    auto size = m_json.size();
    for(auto i = qsizetype(0); i != size; ++i) {
      if(!is_consumed(i)) {
        auto path = make_path(root, segments);
        auto entry = m_json.constBegin() + i;
        append_key(path, entry.key());
        append_type(path, entry.value());
        ++report.m_unconsumed[path];
      }
    }
  }
  for(auto& child : m_children) {
    segments.emplace_back(&m_json, child.m_index);
    if(child.m_object) {
      child.m_object->flush(root, segments, report);
    } else {
      segments.emplace_back(nullptr, 0);
      child.m_array->flush(root, segments, report);
      segments.pop_back();
    }
    segments.pop_back();
  }
}

XdArray::XdArray(QJsonArray json) noexcept
  : m_json(std::move(json)) {}

std::size_t XdArray::get_size() const {
  return static_cast<std::size_t>(m_json.size());
}

const QJsonArray& XdArray::get_json() const {
  return m_json;
}

XdObject* XdArray::get_object(std::size_t index) {
  if(index >= get_size()) {
    return nullptr;
  }
  if(m_objects.empty()) {
    m_objects.resize(get_size());
  }
  auto& object = m_objects[index];
  if(!object) {
    auto value = m_json.at(static_cast<qsizetype>(index));
    if(!value.isObject()) {
      return nullptr;
    }
    object = std::unique_ptr<XdObject>(new XdObject(value.toObject()));
  }
  return object.get();
}

XdArray* XdArray::get_array(std::size_t index) {
  if(index >= get_size()) {
    return nullptr;
  }
  if(m_arrays.empty()) {
    m_arrays.resize(get_size());
  }
  auto& array = m_arrays[index];
  if(!array) {
    auto value = m_json.at(static_cast<qsizetype>(index));
    if(!value.isArray()) {
      return nullptr;
    }
    array = std::unique_ptr<XdArray>(new XdArray(value.toArray()));
  }
  return array.get();
}

void XdArray::flush(const std::string& root,
    std::vector<std::pair<const QJsonObject*, qsizetype>>& segments,
    ImportReport& report) const {
  for(auto& object : m_objects) {
    if(object) {
      object->flush(root, segments, report);
    }
  }
  for(auto& array : m_arrays) {
    if(array) {
      segments.emplace_back(nullptr, 0);
      array->flush(root, segments, report);
      segments.pop_back();
    }
  }
}
