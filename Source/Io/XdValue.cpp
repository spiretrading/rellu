#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <system_error>
#include <QJsonArray>
#include <QJsonObject>
#include "Rellu/Io/XdValue.hpp"

using namespace Rellu;

namespace {
  const auto EMPTY_STRING = std::string();
  const auto EMPTY_ELEMENTS = std::vector<XdValue>();
  const auto EMPTY_MEMBERS = std::vector<XdMember>();

  constexpr auto FNV_OFFSET = std::uint64_t(0xCBF29CE484222325ULL);
  constexpr auto FNV_PRIME = std::uint64_t(0x100000001B3ULL);

  void mix(std::uint64_t& hash, std::uint64_t value) {
    for(auto i = 0; i != 8; ++i) {
      hash ^= (value >> (8 * i)) & 0xFF;
      hash *= FNV_PRIME;
    }
  }

  void mix(std::uint64_t& hash, std::string_view text) {
    mix(hash, text.size());
    for(auto character : text) {
      hash ^= static_cast<unsigned char>(character);
      hash *= FNV_PRIME;
    }
  }

  void hash_into(const XdValue& value, std::uint64_t& hash) {
    mix(hash, static_cast<std::uint64_t>(value.get_type()) + 0x51);
    switch(value.get_type()) {
      case XdValue::Type::BOOL:
        mix(hash, value.to_bool() ? 1 : 0);
        break;
      case XdValue::Type::NUMBER:
        {
          auto number = value.to_number();
          if(number == 0) {
            number = 0;
          }
          mix(hash, std::bit_cast<std::uint64_t>(number));
        }
        break;
      case XdValue::Type::STRING:
        mix(hash, value.to_string());
        break;
      case XdValue::Type::ARRAY:
        mix(hash, value.get_elements().size());
        for(auto& element : value.get_elements()) {
          hash_into(element, hash);
        }
        break;
      case XdValue::Type::OBJECT:
        mix(hash, value.get_members().size());
        for(auto& member : value.get_members()) {
          mix(hash, member.m_key);
          hash_into(member.m_value, hash);
        }
        break;
      default:
        break;
    }
  }

  void append_escaped(std::string_view text, std::string& out) {
    static constexpr auto HEX = std::string_view("0123456789abcdef");
    out += '"';
    auto start = std::size_t(0);
    for(auto i = std::size_t(0); i != text.size(); ++i) {
      auto character = static_cast<unsigned char>(text[i]);
      if(character >= 0x20 && character != '"' && character != '\\') {
        continue;
      }
      out.append(text.data() + start, i - start);
      start = i + 1;
      switch(character) {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\b':
          out += "\\b";
          break;
        case '\f':
          out += "\\f";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          out += "\\u00";
          out += HEX[character >> 4];
          out += HEX[character & 0xF];
          break;
      }
    }
    out.append(text.data() + start, text.size() - start);
    out += '"';
  }

  class Parser {
    public:
      explicit Parser(std::string_view text) noexcept;

      std::optional<XdValue> parse();

    private:
      static constexpr auto MAXIMUM_DEPTH = 512;
      std::string_view m_text;
      std::size_t m_position;
      int m_depth;

      void skip_space();
      bool consume(std::string_view token);
      std::optional<XdValue> parse_value();
      std::optional<std::string> parse_string();
      std::optional<std::uint32_t> parse_hex();
      std::optional<XdValue> parse_number();
      std::optional<XdValue> parse_array();
      std::optional<XdValue> parse_object();
  };

  void append_utf8(std::uint32_t code, std::string& out) {
    if(code < 0x80) {
      out += static_cast<char>(code);
    } else if(code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else if(code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18));
      out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }

  Parser::Parser(std::string_view text) noexcept
    : m_text(text),
      m_position(0),
      m_depth(0) {}

  std::optional<XdValue> Parser::parse() {
    auto value = parse_value();
    skip_space();
    if(!value || m_position != m_text.size()) {
      return std::nullopt;
    }
    return value;
  }

  void Parser::skip_space() {
    while(m_position != m_text.size()) {
      auto c = m_text[m_position];
      if(c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        return;
      }
      ++m_position;
    }
  }

  bool Parser::consume(std::string_view token) {
    if(m_text.substr(m_position, token.size()) != token) {
      return false;
    }
    m_position += token.size();
    return true;
  }

  std::optional<XdValue> Parser::parse_value() {
    skip_space();
    if(m_position == m_text.size()) {
      return std::nullopt;
    }
    auto c = m_text[m_position];
    if(c == '{') {
      return parse_object();
    } else if(c == '[') {
      return parse_array();
    } else if(c == '"') {
      if(auto text = parse_string()) {
        return XdValue(std::move(*text));
      }
      return std::nullopt;
    } else if(consume("true")) {
      return XdValue(true);
    } else if(consume("false")) {
      return XdValue(false);
    } else if(consume("null")) {
      return XdValue();
    }
    return parse_number();
  }

  std::optional<std::uint32_t> Parser::parse_hex() {
    if(m_text.size() - m_position < 4) {
      return std::nullopt;
    }
    auto code = std::uint32_t(0);
    auto result = std::from_chars(m_text.data() + m_position,
      m_text.data() + m_position + 4, code, 16);
    if(result.ec != std::errc() ||
        result.ptr != m_text.data() + m_position + 4) {
      return std::nullopt;
    }
    m_position += 4;
    return code;
  }

  std::optional<std::string> Parser::parse_string() {
    ++m_position;
    auto out = std::string();
    while(m_position != m_text.size()) {
      auto c = m_text[m_position];
      if(c == '"') {
        ++m_position;
        return out;
      } else if(static_cast<unsigned char>(c) < 0x20) {
        return std::nullopt;
      } else if(c != '\\') {
        out += c;
        ++m_position;
        continue;
      }
      ++m_position;
      if(m_position == m_text.size()) {
        return std::nullopt;
      }
      auto escape = m_text[m_position];
      ++m_position;
      switch(escape) {
        case '"':
        case '\\':
        case '/':
          out += escape;
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u':
          {
            auto code = parse_hex();
            if(!code) {
              return std::nullopt;
            }
            if(*code >= 0xD800 && *code < 0xDC00 && consume("\\u")) {
              auto low = parse_hex();
              if(!low) {
                return std::nullopt;
              }
              if(*low >= 0xDC00 && *low < 0xE000) {
                append_utf8(
                  0x10000 + ((*code - 0xD800) << 10) + (*low - 0xDC00), out);
              } else {
                append_utf8(*code, out);
                append_utf8(*low, out);
              }
            } else {
              append_utf8(*code, out);
            }
          }
          break;
        default:
          return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<XdValue> Parser::parse_number() {
    auto start = m_position;
    if(m_position != m_text.size() && m_text[m_position] == '-') {
      ++m_position;
    }
    while(m_position != m_text.size()) {
      auto c = m_text[m_position];
      if(!(c >= '0' && c <= '9') && c != '.' && c != 'e' && c != 'E' &&
          c != '+' && c != '-') {
        break;
      }
      ++m_position;
    }
    auto number = 0.0;
    auto result = std::from_chars(
      m_text.data() + start, m_text.data() + m_position, number);
    if(start == m_position || result.ec != std::errc() ||
        result.ptr != m_text.data() + m_position) {
      return std::nullopt;
    }
    return XdValue(number);
  }

  std::optional<XdValue> Parser::parse_array() {
    if(++m_depth > MAXIMUM_DEPTH) {
      return std::nullopt;
    }
    ++m_position;
    auto array = XdValue::make_array();
    skip_space();
    if(!consume("]")) {
      while(true) {
        auto element = parse_value();
        if(!element) {
          return std::nullopt;
        }
        array.append(std::move(*element));
        skip_space();
        if(consume("]")) {
          break;
        } else if(!consume(",")) {
          return std::nullopt;
        }
      }
    }
    --m_depth;
    return array;
  }

  std::optional<XdValue> Parser::parse_object() {
    if(++m_depth > MAXIMUM_DEPTH) {
      return std::nullopt;
    }
    ++m_position;
    auto object = XdValue::make_object();
    auto& members = object.get_members();
    skip_space();
    if(!consume("}")) {
      while(true) {
        skip_space();
        if(m_position == m_text.size() || m_text[m_position] != '"') {
          return std::nullopt;
        }
        auto key = parse_string();
        skip_space();
        if(!key || !consume(":")) {
          return std::nullopt;
        }
        auto value = parse_value();
        if(!value) {
          return std::nullopt;
        }
        members.push_back(XdMember(std::move(*key), std::move(*value)));
        skip_space();
        if(consume("}")) {
          break;
        } else if(!consume(",")) {
          return std::nullopt;
        }
      }
    }
    --m_depth;
    return object;
  }
}

bool XdValue::to_bool() const {
  if(auto value = std::get_if<bool>(&m_value)) {
    return *value;
  }
  return false;
}

double XdValue::to_number() const {
  if(auto value = std::get_if<double>(&m_value)) {
    return *value;
  }
  return 0;
}

std::string_view XdValue::to_string() const {
  if(auto value = std::get_if<std::string>(&m_value)) {
    return *value;
  }
  return EMPTY_STRING;
}

const std::vector<XdValue>& XdValue::get_elements() const {
  if(auto value = std::get_if<std::vector<XdValue>>(&m_value)) {
    return *value;
  }
  return EMPTY_ELEMENTS;
}

std::vector<XdValue>& XdValue::get_elements() {
  if(auto value = std::get_if<std::vector<XdValue>>(&m_value)) {
    return *value;
  }
  return m_value.emplace<std::vector<XdValue>>();
}

const std::vector<XdMember>& XdValue::get_members() const {
  if(auto value = std::get_if<std::vector<XdMember>>(&m_value)) {
    return *value;
  }
  return EMPTY_MEMBERS;
}

std::vector<XdMember>& XdValue::get_members() {
  if(auto value = std::get_if<std::vector<XdMember>>(&m_value)) {
    return *value;
  }
  return m_value.emplace<std::vector<XdMember>>();
}

const XdValue* XdValue::find(std::string_view key) const {
  if(auto members = std::get_if<std::vector<XdMember>>(&m_value)) {
    for(auto& member : *members) {
      if(member.m_key == key) {
        return &member.m_value;
      }
    }
  }
  return nullptr;
}

XdValue* XdValue::find(std::string_view key) {
  if(auto members = std::get_if<std::vector<XdMember>>(&m_value)) {
    for(auto& member : *members) {
      if(member.m_key == key) {
        return &member.m_value;
      }
    }
  }
  return nullptr;
}

XdValue& XdValue::set(std::string_view key, XdValue value) {
  auto& members = get_members();
  for(auto& member : members) {
    if(member.m_key == key) {
      member.m_value = std::move(value);
      return member.m_value;
    }
  }
  members.push_back(XdMember(std::string(key), std::move(value)));
  return members.back().m_value;
}

bool XdValue::erase(std::string_view key) {
  if(auto members = std::get_if<std::vector<XdMember>>(&m_value)) {
    for(auto i = members->begin(); i != members->end(); ++i) {
      if(i->m_key == key) {
        members->erase(i);
        return true;
      }
    }
  }
  return false;
}

XdValue& XdValue::append(XdValue value) {
  auto& elements = get_elements();
  elements.push_back(std::move(value));
  return elements.back();
}

bool XdValue::operator ==(const XdValue& value) const {
  return m_value == value.m_value;
}

XdValue Rellu::to_xd_value(const QJsonValue& value) {
  switch(value.type()) {
    case QJsonValue::Bool:
      return XdValue(value.toBool());
    case QJsonValue::Double:
      return XdValue(value.toDouble());
    case QJsonValue::String:
      return XdValue(value.toString().toStdString());
    case QJsonValue::Array:
      {
        auto array = value.toArray();
        auto result = XdValue::make_array();
        auto& elements = result.get_elements();
        elements.reserve(static_cast<std::size_t>(array.size()));
        for(auto element : array) {
          elements.push_back(to_xd_value(element));
        }
        return result;
      }
    case QJsonValue::Object:
      {
        auto object = value.toObject();
        auto result = XdValue::make_object();
        auto& members = result.get_members();
        members.reserve(static_cast<std::size_t>(object.size()));
        for(auto i = object.constBegin(); i != object.constEnd(); ++i) {
          members.push_back(
            XdMember(i.key().toStdString(), to_xd_value(i.value())));
        }
        return result;
      }
    default:
      return XdValue();
  }
}

QJsonValue Rellu::to_json_value(const XdValue& value) {
  switch(value.get_type()) {
    case XdValue::Type::BOOL:
      return QJsonValue(value.to_bool());
    case XdValue::Type::NUMBER:
      return QJsonValue(value.to_number());
    case XdValue::Type::STRING:
      {
        auto text = value.to_string();
        return QJsonValue(QString::fromUtf8(
          text.data(), static_cast<qsizetype>(text.size())));
      }
    case XdValue::Type::ARRAY:
      {
        auto array = QJsonArray();
        for(auto& element : value.get_elements()) {
          array.append(to_json_value(element));
        }
        return array;
      }
    case XdValue::Type::OBJECT:
      {
        auto object = QJsonObject();
        for(auto& member : value.get_members()) {
          object.insert(QString::fromStdString(member.m_key),
            to_json_value(member.m_value));
        }
        return object;
      }
    default:
      return QJsonValue(QJsonValue::Null);
  }
}

std::optional<XdValue> Rellu::parse_xd_value(std::string_view text) {
  return Parser(text).parse();
}

void Rellu::append_number(double value, std::string& out) {
  if(!std::isfinite(value)) {
    out += "null";
    return;
  }
  if(value == 0) {
    out += '0';
    return;
  }
  auto buffer = std::array<char, 64>();
  auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
    value, std::chars_format::scientific);
  auto text = std::string_view(buffer.data(), result.ptr - buffer.data());
  auto is_negative = text.front() == '-';
  if(is_negative) {
    text.remove_prefix(1);
  }
  auto exponent_position = text.find('e');
  auto mantissa = text.substr(0, exponent_position);
  auto exponent = 0;
  auto exponent_start = text.data() + exponent_position + 1;
  if(*exponent_start == '+') {
    ++exponent_start;
  }
  std::from_chars(exponent_start, text.data() + text.size(), exponent);
  auto digits = std::string();
  for(auto character : mantissa) {
    if(character != '.') {
      digits += character;
    }
  }
  auto count = static_cast<int>(digits.size());
  auto point = exponent + 1;
  if(is_negative) {
    out += '-';
  }
  if(count <= point && point <= 21) {
    out += digits;
    out.append(static_cast<std::size_t>(point - count), '0');
  } else if(0 < point && point <= 21) {
    out.append(digits, 0, static_cast<std::size_t>(point));
    out += '.';
    out.append(digits, static_cast<std::size_t>(point));
  } else if(-6 < point && point <= 0) {
    out += "0.";
    out.append(static_cast<std::size_t>(-point), '0');
    out += digits;
  } else {
    out += digits[0];
    if(count > 1) {
      out += '.';
      out.append(digits, 1);
    }
    out += 'e';
    out += point - 1 < 0 ? '-' : '+';
    out += std::to_string(std::abs(point - 1));
  }
}

void Rellu::append_json(const XdValue& value, std::string& out) {
  switch(value.get_type()) {
    case XdValue::Type::NUL:
      out += "null";
      break;
    case XdValue::Type::BOOL:
      out += value.to_bool() ? "true" : "false";
      break;
    case XdValue::Type::NUMBER:
      append_number(value.to_number(), out);
      break;
    case XdValue::Type::STRING:
      append_escaped(value.to_string(), out);
      break;
    case XdValue::Type::ARRAY:
      {
        out += '[';
        auto is_first = true;
        for(auto& element : value.get_elements()) {
          if(!is_first) {
            out += ',';
          }
          is_first = false;
          append_json(element, out);
        }
        out += ']';
      }
      break;
    case XdValue::Type::OBJECT:
      {
        out += '{';
        auto is_first = true;
        for(auto& member : value.get_members()) {
          if(!is_first) {
            out += ',';
          }
          is_first = false;
          append_escaped(member.m_key, out);
          out += ':';
          append_json(member.m_value, out);
        }
        out += '}';
      }
      break;
  }
}

std::string Rellu::to_json(const XdValue& value) {
  auto out = std::string();
  append_json(value, out);
  return out;
}

std::uint64_t Rellu::hash_value(const XdValue& value) {
  auto hash = FNV_OFFSET;
  hash_into(value, hash);
  return hash;
}
