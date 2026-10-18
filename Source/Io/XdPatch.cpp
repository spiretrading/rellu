#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <QJsonArray>
#include <QJsonObject>
#include <private/qjson_p.h>
#include "Rellu/Io/XdPatch.hpp"

using namespace Rellu;

namespace {
  enum class PatchKind : unsigned char {
    REPLACE,
    OBJECT,
    INDEX_ARRAY,
    ID_ARRAY
  };

  enum class ValueTag : unsigned char {
    NUL,
    FALSE,
    TRUE,
    DOUBLE,
    POSITIVE,
    NEGATIVE,
    STRING,
    ARRAY,
    OBJECT
  };

  constexpr auto HAS_TYPE_GUARD = std::uint64_t(1);
  constexpr auto HAS_ADDS = std::uint64_t(2);
  constexpr auto HAS_REMOVES = std::uint64_t(4);
  constexpr auto HAS_CHILDREN = std::uint64_t(8);

  const auto ELEMENTS = std::string_view("[]");

  bool is_guarded_child(std::string_view key) {
    return key == "paragraphs";
  }

  bool is_identifier(std::string_view key) {
    if(key.size() != 36) {
      return false;
    }
    for(auto i = std::size_t(0); i != key.size(); ++i) {
      auto c = key[i];
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

  bool is_ascii(std::string_view text) {
    for(auto c : text) {
      if(static_cast<unsigned char>(c) >= 0x80) {
        return false;
      }
    }
    return true;
  }

  QJsonObject::const_iterator find_key(
      const QJsonObject& object, std::string_view key) {
    if(is_ascii(key)) {
      return object.constFind(
        QLatin1StringView(key.data(), static_cast<qsizetype>(key.size())));
    }
    return object.constFind(QString::fromUtf8(
      key.data(), static_cast<qsizetype>(key.size())));
  }

  std::string to_utf8(QAnyStringView view) {
    return view.toString().toStdString();
  }

  std::uint64_t hash_discriminators(const XdValue& value) {
    auto hash = std::uint64_t(0x9E3779B97F4A7C15ULL);
    auto is_present = false;
    for(auto key : {std::string_view("type"), std::string_view("action"),
        std::string_view("triggerEvent")}) {
      if(auto member = value.find(key)) {
        is_present = true;
        hash = (hash * 31) ^ hash_value(XdValue(key));
        hash = (hash * 31) ^ hash_value(*member);
      }
    }
    if(!is_present) {
      return 0;
    }
    return hash;
  }

  bool has_discriminator(const XdValue& value) {
    for(auto key : {std::string_view("type"), std::string_view("action"),
        std::string_view("triggerEvent")}) {
      if(value.find(key)) {
        return true;
      }
    }
    return false;
  }

  bool is_equal_string(const QJsonValue& original, std::string_view text) {
    return original.toString() ==
      QUtf8StringView(text.data(), static_cast<qsizetype>(text.size()));
  }

  bool is_equal_string(QJsonValueConstRef original, std::string_view text) {
    auto& element = QJsonPrivate::Value::elementHelper(original);
    auto data = QJsonPrivate::Value::container(original)->byteData(element);
    if(!data) {
      return text.empty();
    } else if(element.flags & QtCbor::Element::StringIsUtf16) {
      return data->asStringView() ==
        QUtf8StringView(text.data(), static_cast<qsizetype>(text.size()));
    }
    return std::string_view(
      data->byte(), static_cast<std::size_t>(data->len)) == text;
  }

  template<typename V>
  bool is_equal_scalar(const V& original, const XdValue& generated) {
    switch(original.type()) {
      case QJsonValue::Null:
        return generated.get_type() == XdValue::Type::NUL;
      case QJsonValue::Bool:
        return generated.get_type() == XdValue::Type::BOOL &&
          generated.to_bool() == original.toBool();
      case QJsonValue::Double:
        return generated.get_type() == XdValue::Type::NUMBER &&
          generated.to_number() == original.toDouble();
      case QJsonValue::String:
        return generated.get_type() == XdValue::Type::STRING &&
          is_equal_string(original, generated.to_string());
      default:
        return false;
    }
  }

  class Differ {
    public:
      Differ(const XdStructure& structure, std::string_view root,
        XdPatchCounts* counts);

      bool has_cache() const;
      template<typename V>
      bool diff(const V& original, const XdValue& generated,
        std::string& out);

    private:
      const XdStructure* m_structure;
      std::string_view m_root;
      XdPatchCounts* m_counts;
      std::vector<std::string_view> m_keys;
      bool m_has_cache;

      bool is_structural(std::string_view key) const;
      void count(std::string_view kind, std::string_view key);
      bool diff_object(const QJsonObject& original, const XdValue& generated,
        std::string& out);
      bool diff_array(const QJsonArray& original, const XdValue& generated,
        std::string& out);
      std::optional<std::string_view> get_id_key(
        const QJsonArray& original, const XdValue& generated) const;
      void replace(const QJsonValue& original, const XdValue& generated,
        std::string& out);
  };

  Differ::Differ(const XdStructure& structure, std::string_view root,
    XdPatchCounts* counts)
    : m_structure(&structure),
      m_root(root),
      m_counts(counts),
      m_has_cache(false) {}

  bool Differ::has_cache() const {
    return m_has_cache;
  }

  bool Differ::is_structural(std::string_view key) const {
    for(auto& path : m_structure->m_paths) {
      if(path.size() != m_keys.size() + 1 || path.back() != key) {
        continue;
      }
      if(std::equal(m_keys.begin(), m_keys.end(), path.begin())) {
        return true;
      }
    }
    return false;
  }

  void append_segment(std::string_view key, std::string& path) {
    if(key.data() == ELEMENTS.data()) {
      path += ELEMENTS;
      return;
    }
    path += '.';
    if(is_identifier(key)) {
      path += "<id>";
    } else {
      path += key;
    }
  }

  void Differ::count(std::string_view kind, std::string_view key) {
    if(!m_counts) {
      return;
    }
    auto path = std::string(kind);
    path += ' ';
    path += m_root;
    for(auto segment : m_keys) {
      append_segment(segment, path);
    }
    if(!key.empty()) {
      append_segment(key, path);
    }
    ++(*m_counts)[path];
  }

  void Differ::replace(const QJsonValue& original, const XdValue& generated,
      std::string& out) {
    out += static_cast<char>(PatchKind::REPLACE);
    encode_value(original, out);
    encode_fixed(hash_value(generated), out);
  }

  template<typename V>
  bool Differ::diff(const V& original, const XdValue& generated,
      std::string& out) {
    if(original.isObject() && generated.is_object()) {
      return diff_object(original.toObject(), generated, out);
    } else if(original.isArray() && generated.is_array()) {
      return diff_array(original.toArray(), generated, out);
    } else if(is_equal_scalar(original, generated)) {
      return false;
    }
    replace(QJsonValue(original), generated, out);
    count("replace", "");
    return true;
  }

  bool Differ::diff_object(const QJsonObject& original,
      const XdValue& generated, std::string& out) {
    auto size = static_cast<std::size_t>(original.size());
    auto mask = std::uint64_t(0);
    auto overflow = std::vector<bool>();
    if(size > 64) {
      overflow.resize(size);
    }
    auto removes = std::string();
    auto remove_count = std::size_t(0);
    auto children = std::string();
    auto child_count = std::size_t(0);
    auto child = std::string();
    auto begin = original.constBegin();
    for(auto& member : generated.get_members()) {
      auto entry = find_key(original, member.m_key);
      if(entry == original.constEnd()) {
        if(member.m_value.is_array() && is_structural(member.m_key)) {
          continue;
        }
        encode_string(member.m_key, removes);
        encode_fixed(hash_value(member.m_value), removes);
        ++remove_count;
        count("remove", member.m_key);
        continue;
      }
      auto index = static_cast<std::size_t>(entry - begin);
      if(size > 64) {
        overflow[index] = true;
      } else {
        mask |= std::uint64_t(1) << index;
      }
      auto value = entry.value();
      if(value.isArray() && member.m_value.is_array() &&
          is_structural(member.m_key)) {
        continue;
      }
      child.clear();
      m_keys.push_back(member.m_key);
      auto is_different = diff(value, member.m_value, child);
      m_keys.pop_back();
      if(!is_different) {
        continue;
      }
      encode_string(member.m_key, children);
      if(is_guarded_child(member.m_key)) {
        children += '\1';
        encode_fixed(hash_value(member.m_value), children);
      } else {
        children += '\0';
      }
      encode_varint(child.size(), children);
      children += child;
      ++child_count;
    }
    auto adds = std::string();
    auto add_count = std::size_t(0);
    for(auto i = std::size_t(0); i != size; ++i) {
      if(size > 64 ? overflow[i] : ((mask >> i) & 1) != 0) {
        continue;
      }
      auto entry = begin + static_cast<qsizetype>(i);
      auto key = to_utf8(entry.keyView());
      encode_string(key, adds);
      encode_value(entry.value(), adds);
      ++add_count;
      if(is_cache_key(key)) {
        m_has_cache = true;
      }
      count("add", key);
    }
    if(add_count == 0 && remove_count == 0 && child_count == 0) {
      return false;
    }
    out += static_cast<char>(PatchKind::OBJECT);
    auto flags = std::uint64_t(0);
    if(has_discriminator(generated)) {
      flags |= HAS_TYPE_GUARD;
    }
    if(add_count != 0) {
      flags |= HAS_ADDS;
    }
    if(remove_count != 0) {
      flags |= HAS_REMOVES;
    }
    if(child_count != 0) {
      flags |= HAS_CHILDREN;
    }
    encode_varint(flags, out);
    if(flags & HAS_TYPE_GUARD) {
      encode_fixed(hash_discriminators(generated), out);
    }
    if(add_count != 0) {
      encode_varint(add_count, out);
      out += adds;
    }
    if(remove_count != 0) {
      encode_varint(remove_count, out);
      out += removes;
    }
    if(child_count != 0) {
      encode_varint(child_count, out);
      out += children;
    }
    return true;
  }

  std::optional<std::string_view> Differ::get_id_key(
      const QJsonArray& original, const XdValue& generated) const {
    if(original.isEmpty()) {
      return std::nullopt;
    }
    for(auto key : {std::string_view("id"), std::string_view("guid")}) {
      auto qkey =
        QLatin1StringView(key.data(), static_cast<qsizetype>(key.size()));
      auto ids = std::unordered_set<QString>();
      auto is_valid = true;
      for(auto element : original) {
        auto id = element.toObject().value(qkey);
        if(!element.isObject() || !id.isString() ||
            !ids.insert(id.toString()).second) {
          is_valid = false;
          break;
        }
      }
      if(!is_valid) {
        continue;
      }
      auto generated_ids = std::unordered_set<std::string_view>();
      for(auto& element : generated.get_elements()) {
        auto id = element.find(key);
        if(!element.is_object() || !id ||
            id->get_type() != XdValue::Type::STRING ||
            !generated_ids.insert(id->to_string()).second) {
          is_valid = false;
          break;
        }
      }
      if(!is_valid) {
        continue;
      }
      for(auto element : original) {
        auto id = element.toObject().value(qkey).toString().toUtf8();
        if(generated_ids.contains(std::string_view(id.constData(),
            static_cast<std::size_t>(id.size())))) {
          return key;
        }
      }
    }
    return std::nullopt;
  }

  bool Differ::diff_array(const QJsonArray& original, const XdValue& generated,
      std::string& out) {
    auto& elements = generated.get_elements();
    m_keys.push_back(ELEMENTS);
    auto result = [&] {
      if(auto id_key = get_id_key(original, generated)) {
        auto qkey = QLatin1StringView(
          id_key->data(), static_cast<qsizetype>(id_key->size()));
        auto generated_index =
          std::unordered_map<std::string_view, std::size_t>();
        for(auto i = std::size_t(0); i != elements.size(); ++i) {
          generated_index.emplace(elements[i].find(*id_key)->to_string(), i);
        }
        auto original_ids = std::vector<std::string>();
        original_ids.reserve(static_cast<std::size_t>(original.size()));
        auto patches = std::string();
        auto patch_count = std::size_t(0);
        auto extras = std::string();
        auto extra_count = std::size_t(0);
        auto matched = std::vector<bool>(elements.size(), false);
        auto common = std::vector<std::size_t>();
        auto child = std::string();
        for(auto element : original) {
          auto id = element.toObject().value(qkey).toString().toStdString();
          original_ids.push_back(id);
          auto entry = generated_index.find(id);
          if(entry == generated_index.end()) {
            encode_string(id, extras);
            encode_value(element, extras);
            ++extra_count;
            count("add", "");
            continue;
          }
          matched[entry->second] = true;
          common.push_back(entry->second);
          child.clear();
          if(diff(element, elements[entry->second], child)) {
            encode_string(id, patches);
            encode_varint(child.size(), patches);
            patches += child;
            ++patch_count;
          }
        }
        auto removes = std::string();
        auto remove_count = std::size_t(0);
        for(auto i = std::size_t(0); i != elements.size(); ++i) {
          if(!matched[i]) {
            encode_string(elements[i].find(*id_key)->to_string(), removes);
            encode_fixed(hash_value(elements[i]), removes);
            ++remove_count;
            count("remove", "");
          }
        }
        auto is_ordered = extra_count == 0 &&
          std::is_sorted(common.begin(), common.end());
        if(is_ordered && patch_count == 0 && remove_count == 0) {
          return false;
        }
        out += static_cast<char>(PatchKind::ID_ARRAY);
        encode_string(*id_key, out);
        if(is_ordered) {
          encode_varint(0, out);
        } else {
          encode_varint(original_ids.size(), out);
          for(auto& id : original_ids) {
            encode_string(id, out);
          }
          count("reorder", "");
        }
        encode_varint(patch_count, out);
        out += patches;
        encode_varint(extra_count, out);
        out += extras;
        encode_varint(remove_count, out);
        out += removes;
        return true;
      }
      if(static_cast<std::size_t>(original.size()) != elements.size()) {
        replace(QJsonValue(original), generated, out);
        count("replace", "");
        return true;
      }
      auto patches = std::string();
      auto patch_count = std::size_t(0);
      auto child = std::string();
      for(auto i = std::size_t(0); i != elements.size(); ++i) {
        child.clear();
        if(diff(original.constBegin()[static_cast<qsizetype>(i)],
            elements[i], child)) {
          encode_varint(i, patches);
          encode_varint(child.size(), patches);
          patches += child;
          ++patch_count;
        }
      }
      if(patch_count == 0) {
        return false;
      }
      out += static_cast<char>(PatchKind::INDEX_ARRAY);
      encode_varint(elements.size(), out);
      encode_varint(patch_count, out);
      out += patches;
      return true;
    }();
    m_keys.pop_back();
    return result;
  }

  void apply(std::string_view& patch, XdValue& value, bool is_cache_valid);

  void skip(std::string_view& data, std::size_t size) {
    data.remove_prefix(std::min(size, data.size()));
  }

  void apply_object(std::string_view& patch, XdValue& value,
      bool is_cache_valid) {
    auto flags = decode_varint(patch);
    auto is_valid = value.is_object();
    if(flags & HAS_TYPE_GUARD) {
      auto guard = decode_fixed(patch);
      if(is_valid && hash_discriminators(value) != guard) {
        is_valid = false;
      }
    }
    auto adds = std::vector<XdMember>();
    if(flags & HAS_ADDS) {
      auto count = decode_varint(patch);
      for(auto i = std::uint64_t(0); i != count && !patch.empty(); ++i) {
        auto key = decode_string(patch);
        auto member = decode_value(patch);
        if(is_valid && !value.find(key) &&
            (is_cache_valid || !is_cache_key(key))) {
          adds.push_back(XdMember(std::string(key), std::move(member)));
        }
      }
    }
    if(flags & HAS_REMOVES) {
      auto count = decode_varint(patch);
      for(auto i = std::uint64_t(0); i != count && !patch.empty(); ++i) {
        auto key = decode_string(patch);
        auto guard = decode_fixed(patch);
        if(!is_valid) {
          continue;
        }
        if(auto member = value.find(key)) {
          if(hash_value(*member) == guard) {
            value.erase(key);
          }
        }
      }
    }
    if(flags & HAS_CHILDREN) {
      auto count = decode_varint(patch);
      for(auto i = std::uint64_t(0); i != count && !patch.empty(); ++i) {
        auto key = decode_string(patch);
        auto is_guarded = !patch.empty() && patch.front() == '\1';
        skip(patch, 1);
        auto guard = std::uint64_t(0);
        if(is_guarded) {
          guard = decode_fixed(patch);
        }
        auto size = decode_varint(patch);
        auto child = patch.substr(0, std::min<std::size_t>(size, patch.size()));
        skip(patch, size);
        if(!is_valid) {
          continue;
        }
        auto member = value.find(key);
        if(!member || (is_guarded && hash_value(*member) != guard)) {
          continue;
        }
        apply(child, *member, is_cache_valid);
      }
    }
    for(auto& member : adds) {
      value.get_members().push_back(std::move(member));
    }
  }

  void apply_index_array(std::string_view& patch, XdValue& value,
      bool is_cache_valid) {
    auto length = decode_varint(patch);
    auto count = decode_varint(patch);
    auto is_valid = value.is_array() && value.get_elements().size() == length;
    for(auto i = std::uint64_t(0); i != count && !patch.empty(); ++i) {
      auto index = decode_varint(patch);
      auto size = decode_varint(patch);
      auto child = patch.substr(0, std::min<std::size_t>(size, patch.size()));
      skip(patch, size);
      if(is_valid && index < length) {
        apply(child, value.get_elements()[index], is_cache_valid);
      }
    }
  }

  void apply_id_array(std::string_view& patch, XdValue& value,
      bool is_cache_valid) {
    auto id_key = std::string(decode_string(patch));
    auto order_count = decode_varint(patch);
    auto order = std::vector<std::string>();
    for(auto i = std::uint64_t(0); i != order_count && !patch.empty(); ++i) {
      order.push_back(std::string(decode_string(patch)));
    }
    auto is_valid = value.is_array();
    auto& elements = value.get_elements();
    auto find = [&] (std::string_view id) -> XdValue* {
      for(auto& element : elements) {
        if(auto member = element.find(id_key)) {
          if(member->to_string() == id) {
            return &element;
          }
        }
      }
      return nullptr;
    };
    auto patch_count = decode_varint(patch);
    for(auto i = std::uint64_t(0); i != patch_count && !patch.empty(); ++i) {
      auto id = decode_string(patch);
      auto size = decode_varint(patch);
      auto child = patch.substr(0, std::min<std::size_t>(size, patch.size()));
      skip(patch, size);
      if(!is_valid) {
        continue;
      }
      if(auto element = find(id)) {
        apply(child, *element, is_cache_valid);
      }
    }
    auto extras = std::vector<XdValue>();
    auto extra_count = decode_varint(patch);
    for(auto i = std::uint64_t(0); i != extra_count && !patch.empty(); ++i) {
      auto id = decode_string(patch);
      auto element = decode_value(patch);
      if(is_valid && !find(id)) {
        extras.push_back(std::move(element));
      }
    }
    auto remove_count = decode_varint(patch);
    for(auto i = std::uint64_t(0); i != remove_count && !patch.empty(); ++i) {
      auto id = decode_string(patch);
      auto guard = decode_fixed(patch);
      if(!is_valid) {
        continue;
      }
      for(auto j = elements.begin(); j != elements.end(); ++j) {
        if(auto member = j->find(id_key); member && member->to_string() == id) {
          if(hash_value(*j) == guard) {
            elements.erase(j);
          }
          break;
        }
      }
    }
    if(!is_valid) {
      return;
    }
    for(auto& extra : extras) {
      elements.push_back(std::move(extra));
    }
    if(order.empty()) {
      return;
    }
    auto rank = std::unordered_map<std::string, std::size_t>();
    for(auto i = std::size_t(0); i != order.size(); ++i) {
      rank.emplace(order[i], i);
    }
    auto get_rank = [&] (const XdValue& element) {
      if(auto member = element.find(id_key)) {
        auto entry = rank.find(std::string(member->to_string()));
        if(entry != rank.end()) {
          return entry->second;
        }
      }
      return order.size();
    };
    std::stable_sort(elements.begin(), elements.end(),
      [&] (const auto& left, const auto& right) {
        return get_rank(left) < get_rank(right);
      });
  }

  void apply(std::string_view& patch, XdValue& value, bool is_cache_valid) {
    if(patch.empty()) {
      return;
    }
    auto kind = static_cast<PatchKind>(patch.front());
    patch.remove_prefix(1);
    switch(kind) {
      case PatchKind::REPLACE:
        {
          auto replacement = decode_value(patch);
          auto guard = decode_fixed(patch);
          if(hash_value(value) == guard) {
            value = std::move(replacement);
          }
        }
        break;
      case PatchKind::OBJECT:
        apply_object(patch, value, is_cache_valid);
        break;
      case PatchKind::INDEX_ARRAY:
        apply_index_array(patch, value, is_cache_valid);
        break;
      case PatchKind::ID_ARRAY:
        apply_id_array(patch, value, is_cache_valid);
        break;
      default:
        patch = std::string_view();
        break;
    }
  }
}

const XdStructure& XdStructure::get_node() {
  static const auto structure = XdStructure({{"group", "children"},
    {"artboard", "children"}, {"shape", "children"}, {"meta", "ux", "states"}});
  return structure;
}

const XdStructure& XdStructure::get_part() {
  static const auto structure = XdStructure({{"children"}});
  return structure;
}

const XdStructure& XdStructure::get_resources() {
  static const auto structure =
    XdStructure({{"resources", "meta", "ux", "symbols"}});
  return structure;
}

const XdStructure& XdStructure::get_none() {
  static const auto structure = XdStructure();
  return structure;
}

XdDifference Rellu::make_patch(const QJsonValue& original,
    const XdValue& generated, const XdStructure& structure,
    std::string_view root, XdPatchCounts* counts) {
  auto differ = Differ(structure, root, counts);
  auto difference = XdDifference();
  differ.diff(original, generated, difference.m_patch);
  difference.m_has_cache = differ.has_cache();
  return difference;
}

void Rellu::apply_patch(
    std::string_view patch, XdValue& value, bool is_cache_valid) {
  apply(patch, value, is_cache_valid);
}

void Rellu::encode_value(const QJsonValue& value, std::string& out) {
  switch(value.type()) {
    case QJsonValue::Bool:
      out += static_cast<char>(
        value.toBool() ? ValueTag::TRUE : ValueTag::FALSE);
      break;
    case QJsonValue::Double:
      {
        auto number = value.toDouble();
        if(std::floor(number) == number && std::abs(number) < 9.0e15 &&
            !(number == 0 && std::signbit(number))) {
          if(number >= 0) {
            out += static_cast<char>(ValueTag::POSITIVE);
            encode_varint(static_cast<std::uint64_t>(number), out);
          } else {
            out += static_cast<char>(ValueTag::NEGATIVE);
            encode_varint(static_cast<std::uint64_t>(-number - 1), out);
          }
        } else {
          out += static_cast<char>(ValueTag::DOUBLE);
          encode_fixed(std::bit_cast<std::uint64_t>(number), out);
        }
      }
      break;
    case QJsonValue::String:
      {
        out += static_cast<char>(ValueTag::STRING);
        auto text = value.toString().toUtf8();
        encode_string(std::string_view(text.constData(),
          static_cast<std::size_t>(text.size())), out);
      }
      break;
    case QJsonValue::Array:
      {
        out += static_cast<char>(ValueTag::ARRAY);
        auto array = value.toArray();
        encode_varint(static_cast<std::uint64_t>(array.size()), out);
        for(auto element : array) {
          encode_value(element, out);
        }
      }
      break;
    case QJsonValue::Object:
      {
        out += static_cast<char>(ValueTag::OBJECT);
        auto object = value.toObject();
        encode_varint(static_cast<std::uint64_t>(object.size()), out);
        for(auto i = object.constBegin(); i != object.constEnd(); ++i) {
          auto key = i.key().toUtf8();
          encode_string(std::string_view(
            key.constData(), static_cast<std::size_t>(key.size())), out);
          encode_value(i.value(), out);
        }
      }
      break;
    default:
      out += static_cast<char>(ValueTag::NUL);
      break;
  }
}

XdValue Rellu::decode_value(std::string_view& data) {
  if(data.empty()) {
    return XdValue();
  }
  auto tag = static_cast<ValueTag>(data.front());
  data.remove_prefix(1);
  switch(tag) {
    case ValueTag::FALSE:
      return XdValue(false);
    case ValueTag::TRUE:
      return XdValue(true);
    case ValueTag::DOUBLE:
      return XdValue(std::bit_cast<double>(decode_fixed(data)));
    case ValueTag::POSITIVE:
      return XdValue(static_cast<double>(decode_varint(data)));
    case ValueTag::NEGATIVE:
      return XdValue(-static_cast<double>(decode_varint(data)) - 1);
    case ValueTag::STRING:
      return XdValue(std::string(decode_string(data)));
    case ValueTag::ARRAY:
      {
        auto count = decode_varint(data);
        auto result = XdValue::make_array();
        auto& elements = result.get_elements();
        elements.reserve(static_cast<std::size_t>(
          std::min<std::uint64_t>(count, data.size())));
        for(auto i = std::uint64_t(0); i != count && !data.empty(); ++i) {
          elements.push_back(decode_value(data));
        }
        return result;
      }
    case ValueTag::OBJECT:
      {
        auto count = decode_varint(data);
        auto result = XdValue::make_object();
        auto& members = result.get_members();
        members.reserve(static_cast<std::size_t>(
          std::min<std::uint64_t>(count, data.size())));
        for(auto i = std::uint64_t(0); i != count && !data.empty(); ++i) {
          auto key = std::string(decode_string(data));
          members.push_back(XdMember(std::move(key), decode_value(data)));
        }
        return result;
      }
    default:
      return XdValue();
  }
}

void Rellu::encode_varint(std::uint64_t value, std::string& out) {
  while(value >= 0x80) {
    out += static_cast<char>((value & 0x7F) | 0x80);
    value >>= 7;
  }
  out += static_cast<char>(value);
}

std::uint64_t Rellu::decode_varint(std::string_view& data) {
  auto value = std::uint64_t(0);
  auto shift = 0;
  while(!data.empty() && shift < 64) {
    auto byte = static_cast<unsigned char>(data.front());
    data.remove_prefix(1);
    value |= std::uint64_t(byte & 0x7F) << shift;
    if(!(byte & 0x80)) {
      return value;
    }
    shift += 7;
  }
  return value;
}

void Rellu::encode_string(std::string_view text, std::string& out) {
  encode_varint(text.size(), out);
  out += text;
}

std::string_view Rellu::decode_string(std::string_view& data) {
  auto size = static_cast<std::size_t>(decode_varint(data));
  auto text = data.substr(0, std::min(size, data.size()));
  data.remove_prefix(text.size());
  return text;
}

void Rellu::encode_fixed(std::uint64_t value, std::string& out) {
  for(auto i = 0; i != 8; ++i) {
    out += static_cast<char>((value >> (8 * i)) & 0xFF);
  }
}

std::uint64_t Rellu::decode_fixed(std::string_view& data) {
  if(data.size() < 8) {
    data = std::string_view();
    return 0;
  }
  auto value = std::uint64_t(0);
  for(auto i = 0; i != 8; ++i) {
    value |= std::uint64_t(static_cast<unsigned char>(data[i])) << (8 * i);
  }
  data.remove_prefix(8);
  return value;
}

bool Rellu::is_cache_key(std::string_view key) {
  return key == "visualBounds" || key == "rotation" ||
    key == "localTransform";
}
