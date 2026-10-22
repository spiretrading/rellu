// Derived from Adobe's svgObjectModelGenerator (svgWriter), Copyright (c)
// 2014, 2015 Adobe Systems Incorporated, licensed under the Apache License 2.0
// (see Licenses/svgObjectModelGenerator-LICENSE.txt); modified (ported to C++)
// for Rellu.
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <QString>
#include "Rellu/Io/SvgWriter.hpp"

using namespace Rellu;

namespace {
  constexpr auto NOT_A_NUMBER = std::numeric_limits<double>::quiet_NaN();

  using JsValue =
    std::variant<std::monostate, double, std::string, std::vector<double>>;

  bool is_truthy(double value) {
    return value != 0 && !std::isnan(value);
  }

  std::string to_js_string(double value) {
    if(std::isnan(value)) {
      return "NaN";
    } else if(std::isinf(value)) {
      return value < 0 ? "-Infinity" : "Infinity";
    }
    auto out = std::string();
    append_number(value, out);
    return out;
  }

  std::string to_js_string(const JsValue& value) {
    if(auto number = std::get_if<double>(&value)) {
      return to_js_string(*number);
    } else if(auto text = std::get_if<std::string>(&value)) {
      return *text;
    } else if(auto list = std::get_if<std::vector<double>>(&value)) {
      auto out = std::string();
      for(auto i = std::size_t(0); i != list->size(); ++i) {
        if(i != 0) {
          out += ',';
        }
        out += to_js_string((*list)[i]);
      }
      return out;
    }
    return "undefined";
  }

  bool is_undefined(const JsValue& value) {
    return std::holds_alternative<std::monostate>(value);
  }

  bool is_truthy(const JsValue& value) {
    if(auto number = std::get_if<double>(&value)) {
      return *number != 0 && !std::isnan(*number);
    } else if(auto text = std::get_if<std::string>(&value)) {
      return !text->empty();
    }
    return std::holds_alternative<std::vector<double>>(value);
  }

  bool is_js_space(char16_t c) {
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\v' ||
      c == u'\f' || c == u'\r' || c == 0xA0 || c == 0x1680 ||
      (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
      c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
  }

  std::string_view trim_ascii(std::string_view text) {
    auto is_space = [] (char c) {
      return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
        c == '\r';
    };
    while(!text.empty() && is_space(text.front())) {
      text.remove_prefix(1);
    }
    while(!text.empty() && is_space(text.back())) {
      text.remove_suffix(1);
    }
    return text;
  }

  bool is_digit(char c) {
    return c >= '0' && c <= '9';
  }

  std::size_t scan_decimal(std::string_view text) {
    auto i = std::size_t(0);
    if(i < text.size() && (text[i] == '+' || text[i] == '-')) {
      ++i;
    }
    if(text.substr(i).starts_with("Infinity")) {
      return i + 8;
    }
    auto start = i;
    while(i < text.size() && is_digit(text[i])) {
      ++i;
    }
    auto integer_digits = i - start;
    auto fraction_digits = std::size_t(0);
    if(i < text.size() && text[i] == '.') {
      auto point = i;
      ++i;
      while(i < text.size() && is_digit(text[i])) {
        ++i;
        ++fraction_digits;
      }
      if(integer_digits == 0 && fraction_digits == 0) {
        return 0;
      }
      if(fraction_digits == 0 && integer_digits == 0) {
        i = point;
      }
    } else if(integer_digits == 0) {
      return 0;
    }
    if(i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
      auto j = i + 1;
      if(j < text.size() && (text[j] == '+' || text[j] == '-')) {
        ++j;
      }
      if(j < text.size() && is_digit(text[j])) {
        while(j < text.size() && is_digit(text[j])) {
          ++j;
        }
        i = j;
      }
    }
    return i;
  }

  double parse_decimal(std::string_view text) {
    auto is_negative = false;
    if(!text.empty() && (text.front() == '+' || text.front() == '-')) {
      is_negative = text.front() == '-';
      text.remove_prefix(1);
    }
    if(text == "Infinity") {
      return is_negative ? -std::numeric_limits<double>::infinity() :
        std::numeric_limits<double>::infinity();
    }
    auto value = 0.0;
    auto buffer = std::string(text);
    if(!buffer.empty() && buffer.back() == '.') {
      buffer.pop_back();
    }
    std::from_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return is_negative ? -value : value;
  }

  double parse_float(std::string_view text) {
    while(!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
        text.front() == '\n' || text.front() == '\r' ||
        text.front() == '\v' || text.front() == '\f')) {
      text.remove_prefix(1);
    }
    auto length = scan_decimal(text);
    if(length == 0) {
      return NOT_A_NUMBER;
    }
    return parse_decimal(text.substr(0, length));
  }

  double parse_js_number(std::string_view text) {
    text = trim_ascii(text);
    if(text.empty()) {
      return 0;
    }
    if(text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
      auto value = std::uint64_t(0);
      auto result = std::from_chars(
        text.data() + 2, text.data() + text.size(), value, 16);
      if(result.ptr != text.data() + text.size()) {
        return NOT_A_NUMBER;
      }
      return static_cast<double>(value);
    }
    auto length = scan_decimal(text);
    if(length != text.size()) {
      return NOT_A_NUMBER;
    }
    return parse_decimal(text);
  }

  double to_js_number(const JsValue& value) {
    if(auto number = std::get_if<double>(&value)) {
      return *number;
    } else if(auto text = std::get_if<std::string>(&value)) {
      return parse_js_number(*text);
    } else if(std::holds_alternative<std::vector<double>>(value)) {
      return parse_js_number(to_js_string(value));
    }
    return NOT_A_NUMBER;
  }

  bool loosely_equals(const JsValue& left, const JsValue& right) {
    if(is_undefined(left) || is_undefined(right)) {
      return is_undefined(left) && is_undefined(right);
    }
    if(std::holds_alternative<std::vector<double>>(left) &&
        std::holds_alternative<std::vector<double>>(right)) {
      return false;
    }
    if(std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::string>(right)) {
      return std::get<std::string>(left) == std::get<std::string>(right);
    }
    if(std::holds_alternative<std::vector<double>>(left) &&
        std::holds_alternative<std::string>(right) ||
        std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::vector<double>>(right)) {
      return to_js_string(left) == to_js_string(right);
    }
    return to_js_number(left) == to_js_number(right);
  }

  void increment_decimal(std::string& digits) {
    for(auto i = digits.size(); i-- != 0;) {
      if(digits[i] == '.') {
        continue;
      }
      if(digits[i] == '9') {
        digits[i] = '0';
      } else {
        ++digits[i];
        return;
      }
    }
    digits.insert(digits.begin(), '1');
  }

  std::string format_fixed(double value, int digits) {
    auto buffer = std::array<char, 400>();
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
      value, std::chars_format::fixed, digits);
    return std::string(buffer.data(), result.ptr);
  }

  std::string to_fixed(double value, int digits) {
    if(!std::isfinite(value) || std::abs(value) >= 1e21) {
      return to_js_string(value);
    }
    auto is_negative = value < 0;
    auto magnitude = std::abs(value);
    auto rounded = format_fixed(magnitude, digits);
    auto finer = format_fixed(magnitude, digits + 1);
    if(finer.back() == '5') {
      auto exact = std::vector<char>(1200);
      auto result = std::to_chars(exact.data(), exact.data() + exact.size(),
        magnitude, std::chars_format::fixed, 1100);
      auto text = std::string_view(exact.data(), result.ptr);
      auto point = text.find('.');
      auto position = point + 1 + static_cast<std::size_t>(digits);
      auto is_tie = position < text.size() && text[position] == '5' &&
        std::all_of(text.begin() + position + 1, text.end(),
          [] (auto c) { return c == '0'; });
      if(is_tie) {
        rounded = finer.substr(0, finer.size() - 1);
        if(!rounded.empty() && rounded.back() == '.') {
          rounded.pop_back();
        }
        increment_decimal(rounded);
      }
    }
    if(is_negative) {
      return "-" + rounded;
    }
    return rounded;
  }

  double round_fixed(double value, int digits) {
    return parse_js_number(to_fixed(value, digits));
  }

  double js_round(double value) {
    if(!std::isfinite(value)) {
      return value;
    }
    auto floor = std::floor(value);
    if(value - floor >= 0.5) {
      return floor + 1;
    }
    return floor;
  }

  template<typename V>
  class OrderedMap {
    public:
      using Entry = std::pair<std::string, V>;

      V* find(std::string_view key) {
        for(auto& entry : m_entries) {
          if(entry.first == key) {
            return &entry.second;
          }
        }
        return nullptr;
      }

      const V* find(std::string_view key) const {
        for(auto& entry : m_entries) {
          if(entry.first == key) {
            return &entry.second;
          }
        }
        return nullptr;
      }

      bool contains(std::string_view key) const {
        return find(key) != nullptr;
      }

      V& set(std::string_view key, V value) {
        if(auto existing = find(key)) {
          *existing = std::move(value);
          return *existing;
        }
        m_entries.emplace_back(std::string(key), std::move(value));
        return m_entries.back().second;
      }

      bool erase(std::string_view key) {
        auto i = std::find_if(m_entries.begin(), m_entries.end(),
          [&] (const auto& entry) { return entry.first == key; });
        if(i == m_entries.end()) {
          return false;
        }
        m_entries.erase(i);
        return true;
      }

      std::vector<std::string> get_keys() const {
        auto keys = std::vector<std::string>();
        keys.reserve(m_entries.size());
        for(auto& entry : m_entries) {
          keys.push_back(entry.first);
        }
        return keys;
      }

      std::vector<Entry>& get_entries() {
        return m_entries;
      }

      const std::vector<Entry>& get_entries() const {
        return m_entries;
      }

    private:
      std::vector<Entry> m_entries;
  };

  JsValue to_js_value(const XdValue* value) {
    if(!value) {
      return JsValue();
    }
    switch(value->get_type()) {
      case XdValue::Type::NUMBER:
        return value->to_number();
      case XdValue::Type::STRING:
        return std::string(value->to_string());
      case XdValue::Type::BOOL:
        return std::string(value->to_bool() ? "true" : "false");
      case XdValue::Type::ARRAY: {
        auto list = std::vector<double>();
        for(auto& element : value->get_elements()) {
          list.push_back(element.to_number());
        }
        return list;
      }
      case XdValue::Type::OBJECT:
        return std::string("[object Object]");
      default:
        return JsValue();
    }
  }

  bool is_truthy(const XdValue* value) {
    if(!value) {
      return false;
    }
    switch(value->get_type()) {
      case XdValue::Type::NUL:
        return false;
      case XdValue::Type::BOOL:
        return value->to_bool();
      case XdValue::Type::NUMBER:
        return value->to_number() != 0 && !std::isnan(value->to_number());
      case XdValue::Type::STRING:
        return !value->to_string().empty();
      default:
        return true;
    }
  }

  double get_number(const XdValue* value) {
    if(!value) {
      return NOT_A_NUMBER;
    }
    if(value->get_type() == XdValue::Type::NUMBER) {
      return value->to_number();
    } else if(value->get_type() == XdValue::Type::STRING) {
      return parse_js_number(value->to_string());
    } else if(value->get_type() == XdValue::Type::BOOL) {
      return value->to_bool() ? 1 : 0;
    } else if(value->get_type() == XdValue::Type::NUL) {
      return 0;
    }
    return NOT_A_NUMBER;
  }

  double get_number(const XdValue& object, std::string_view key) {
    return get_number(object.find(key));
  }

  JsValue get_value(const XdValue& object, std::string_view key) {
    return to_js_value(object.find(key));
  }

  const XdValue* find_path(const XdValue* value,
      std::initializer_list<std::string_view> keys) {
    for(auto key : keys) {
      if(!value || !value->is_object()) {
        return nullptr;
      }
      value = value->find(key);
    }
    return value;
  }

  std::u16string to_utf16(std::string_view text) {
    auto string = QString::fromUtf8(text.data(),
      static_cast<qsizetype>(text.size()));
    return std::u16string(reinterpret_cast<const char16_t*>(string.utf16()),
      static_cast<std::size_t>(string.size()));
  }

  std::string to_utf8(std::u16string_view text) {
    return QString::fromUtf16(text.data(),
      static_cast<qsizetype>(text.size())).toStdString();
  }

  std::string encode_text(std::string_view text) {
    auto out = std::string();
    out.reserve(text.size());
    for(auto i = std::size_t(0); i < text.size(); ++i) {
      auto c = static_cast<unsigned char>(text[i]);
      if(c == '&') {
        out += "&amp;";
      } else if(c == '<') {
        out += "&lt;";
      } else if(c == '>') {
        out += "&gt;";
      } else if(c == '"') {
        out += "&quot;";
      } else if(c == '\'') {
        out += "&apos;";
      } else if(c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D) {
        continue;
      } else if(c == 0xEF && i + 2 < text.size() &&
          static_cast<unsigned char>(text[i + 1]) == 0xBF &&
          (static_cast<unsigned char>(text[i + 2]) == 0xBE ||
          static_cast<unsigned char>(text[i + 2]) == 0xBF)) {
        i += 2;
      } else {
        out += static_cast<char>(c);
      }
    }
    return out;
  }

  std::string encode_uri(std::string_view text) {
    static constexpr auto HEX = std::string_view("0123456789ABCDEF");
    auto out = std::string();
    for(auto character : text) {
      auto c = static_cast<unsigned char>(character);
      if(c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' ||
          std::string_view(";,/?:@&=+$-_.!~*'()#").find(character) !=
          std::string_view::npos) {
        if(c == '#' || c == '&' || c == '?') {
          out += '%';
          auto code = std::array<char, 4>();
          auto result = std::to_chars(
            code.data(), code.data() + code.size(), c, 16);
          out.append(code.data(), result.ptr);
        } else {
          out += character;
        }
      } else {
        out += '%';
        out += HEX[c >> 4];
        out += HEX[c & 0xF];
      }
    }
    return out;
  }

  struct CharacterRange {
    char16_t m_first;
    char16_t m_last;
  };

  const auto LETTER_RANGES = std::vector<CharacterRange>({
    {0x41, 0x5A}, {0x61, 0x7A}, {0xC0, 0xD6}, {0xD8, 0xF6}, {0xF8, 0xFF},
    {0x100, 0x131}, {0x134, 0x13E}, {0x141, 0x148}, {0x14A, 0x17E},
    {0x180, 0x1C3}, {0x1CD, 0x1F0}, {0x1F4, 0x1F5}, {0x1FA, 0x217},
    {0x250, 0x2A8}, {0x2BB, 0x2C1}, {0x386, 0x386}, {0x388, 0x38A},
    {0x38C, 0x38C}, {0x38E, 0x3A1}, {0x3A3, 0x3CE}, {0x3D0, 0x3D6},
    {0x3DA, 0x3DA}, {0x3DC, 0x3DC}, {0x3DE, 0x3DE}, {0x3E0, 0x3E0},
    {0x3E2, 0x3F3}, {0x401, 0x40C}, {0x40E, 0x44F}, {0x451, 0x45C},
    {0x45E, 0x481}, {0x490, 0x4C4}, {0x4C7, 0x4C8}, {0x4CB, 0x4CC},
    {0x4D0, 0x4EB}, {0x4EE, 0x4F5}, {0x4F8, 0x4F9}, {0x531, 0x556},
    {0x559, 0x559}, {0x561, 0x586}, {0x5D0, 0x5EA}, {0x5F0, 0x5F2},
    {0x621, 0x63A}, {0x641, 0x64A}, {0x671, 0x6B7}, {0x6BA, 0x6BE},
    {0x6C0, 0x6CE}, {0x6D0, 0x6D3}, {0x6D5, 0x6D5}, {0x6E5, 0x6E6},
    {0x905, 0x939}, {0x93D, 0x93D}, {0x958, 0x961}, {0x985, 0x98C},
    {0x98F, 0x990}, {0x993, 0x9A8}, {0x9AA, 0x9B0}, {0x9B2, 0x9B2},
    {0x9B6, 0x9B9}, {0x9DC, 0x9DD}, {0x9DF, 0x9E1}, {0x9F0, 0x9F1},
    {0xA05, 0xA0A}, {0xA0F, 0xA10}, {0xA13, 0xA28}, {0xA2A, 0xA30},
    {0xA32, 0xA33}, {0xA35, 0xA36}, {0xA38, 0xA39}, {0xA59, 0xA5C},
    {0xA5E, 0xA5E}, {0xA72, 0xA74}, {0xA85, 0xA8B}, {0xA8D, 0xA8D},
    {0xA8F, 0xA91}, {0xA93, 0xAA8}, {0xAAA, 0xAB0}, {0xAB2, 0xAB3},
    {0xAB5, 0xAB9}, {0xABD, 0xABD}, {0xAE0, 0xAE0}, {0xB05, 0xB0C},
    {0xB0F, 0xB10}, {0xB13, 0xB28}, {0xB2A, 0xB30}, {0xB32, 0xB33},
    {0xB36, 0xB39}, {0xB3D, 0xB3D}, {0xB5C, 0xB5D}, {0xB5F, 0xB61},
    {0xB85, 0xB8A}, {0xB8E, 0xB90}, {0xB92, 0xB95}, {0xB99, 0xB9A},
    {0xB9C, 0xB9C}, {0xB9E, 0xB9F}, {0xBA3, 0xBA4}, {0xBA8, 0xBAA},
    {0xBAE, 0xBB5}, {0xBB7, 0xBB9}, {0xC05, 0xC0C}, {0xC0E, 0xC10},
    {0xC12, 0xC28}, {0xC2A, 0xC33}, {0xC35, 0xC39}, {0xC60, 0xC61},
    {0xC85, 0xC8C}, {0xC8E, 0xC90}, {0xC92, 0xCA8}, {0xCAA, 0xCB3},
    {0xCB5, 0xCB9}, {0xCDE, 0xCDE}, {0xCE0, 0xCE1}, {0xD05, 0xD0C},
    {0xD0E, 0xD10}, {0xD12, 0xD28}, {0xD2A, 0xD39}, {0xD60, 0xD61},
    {0xE01, 0xE2E}, {0xE30, 0xE30}, {0xE32, 0xE33}, {0xE40, 0xE45},
    {0xE81, 0xE82}, {0xE84, 0xE84}, {0xE87, 0xE88}, {0xE8A, 0xE8A},
    {0xE8D, 0xE8D}, {0xE94, 0xE97}, {0xE99, 0xE9F}, {0xEA1, 0xEA3},
    {0xEA5, 0xEA5}, {0xEA7, 0xEA7}, {0xEAA, 0xEAB}, {0xEAD, 0xEAE},
    {0xEB0, 0xEB0}, {0xEB2, 0xEB3}, {0xEBD, 0xEBD}, {0xEC0, 0xEC4},
    {0xF40, 0xF47}, {0xF49, 0xF69}, {0x10A0, 0x10C5}, {0x10D0, 0x10F6},
    {0x1100, 0x1100}, {0x1102, 0x1103}, {0x1105, 0x1107}, {0x1109, 0x1109},
    {0x110B, 0x110C}, {0x110E, 0x1112}, {0x113C, 0x113C}, {0x113E, 0x113E},
    {0x1140, 0x1140}, {0x114C, 0x114C}, {0x114E, 0x114E}, {0x1150, 0x1150},
    {0x1154, 0x1155}, {0x1159, 0x1159}, {0x115F, 0x1161}, {0x1163, 0x1163},
    {0x1165, 0x1165}, {0x1167, 0x1167}, {0x1169, 0x1169}, {0x116D, 0x116E},
    {0x1172, 0x1173}, {0x1175, 0x1175}, {0x119E, 0x119E}, {0x11A8, 0x11A8},
    {0x11AB, 0x11AB}, {0x11AE, 0x11AF}, {0x11B7, 0x11B8}, {0x11BA, 0x11BA},
    {0x11BC, 0x11C2}, {0x11EB, 0x11EB}, {0x11F0, 0x11F0}, {0x11F9, 0x11F9},
    {0x1E00, 0x1E9B}, {0x1EA0, 0x1EF9}, {0x1F00, 0x1F15}, {0x1F18, 0x1F1D},
    {0x1F20, 0x1F45}, {0x1F48, 0x1F4D}, {0x1F50, 0x1F57}, {0x1F59, 0x1F59},
    {0x1F5B, 0x1F5B}, {0x1F5D, 0x1F5D}, {0x1F5F, 0x1F7D}, {0x1F80, 0x1FB4},
    {0x1FB6, 0x1FBC}, {0x1FBE, 0x1FBE}, {0x1FC2, 0x1FC4}, {0x1FC6, 0x1FCC},
    {0x1FD0, 0x1FD3}, {0x1FD6, 0x1FDB}, {0x1FE0, 0x1FEC}, {0x1FF2, 0x1FF4},
    {0x1FF6, 0x1FFC}, {0x2126, 0x2126}, {0x212A, 0x212B}, {0x212E, 0x212E},
    {0x2180, 0x2182}, {0x3041, 0x3094}, {0x30A1, 0x30FA}, {0x3105, 0x312C},
    {0xAC00, 0xD7A3}, {0x4E00, 0x9FA5}, {0x3007, 0x3007}, {0x3021, 0x3029}});

  const auto NAME_RANGES = std::vector<CharacterRange>({
    {0x30, 0x39}, {0x660, 0x669}, {0x6F0, 0x6F9}, {0x966, 0x96F},
    {0x9E6, 0x9EF}, {0xA66, 0xA6F}, {0xAE6, 0xAEF}, {0xB66, 0xB6F},
    {0xBE7, 0xBEF}, {0xC66, 0xC6F}, {0xCE6, 0xCEF}, {0xD66, 0xD6F},
    {0xE50, 0xE59}, {0xED0, 0xED9}, {0xF20, 0xF29}, {0x300, 0x345},
    {0x360, 0x361}, {0x483, 0x486}, {0x591, 0x5A1}, {0x5A3, 0x5B9},
    {0x5BB, 0x5BD}, {0x5BF, 0x5BF}, {0x5C1, 0x5C2}, {0x5C4, 0x5C4},
    {0x64B, 0x652}, {0x670, 0x670}, {0x6D6, 0x6DC}, {0x6DD, 0x6DF},
    {0x6E0, 0x6E4}, {0x6E7, 0x6E8}, {0x6EA, 0x6ED}, {0x901, 0x903},
    {0x93C, 0x93C}, {0x93E, 0x94C}, {0x94D, 0x94D}, {0x951, 0x954},
    {0x962, 0x963}, {0x981, 0x983}, {0x9BC, 0x9BC}, {0x9BE, 0x9BE},
    {0x9BF, 0x9BF}, {0x9C0, 0x9C4}, {0x9C7, 0x9C8}, {0x9CB, 0x9CD},
    {0x9D7, 0x9D7}, {0x9E2, 0x9E3}, {0xA02, 0xA02}, {0xA3C, 0xA3C},
    {0xA3E, 0xA3E}, {0xA3F, 0xA3F}, {0xA40, 0xA42}, {0xA47, 0xA48},
    {0xA4B, 0xA4D}, {0xA70, 0xA71}, {0xA81, 0xA83}, {0xABC, 0xABC},
    {0xABE, 0xAC5}, {0xAC7, 0xAC9}, {0xACB, 0xACD}, {0xB01, 0xB03},
    {0xB3C, 0xB3C}, {0xB3E, 0xB43}, {0xB47, 0xB48}, {0xB4B, 0xB4D},
    {0xB56, 0xB57}, {0xB82, 0xB83}, {0xBBE, 0xBC2}, {0xBC6, 0xBC8},
    {0xBCA, 0xBCD}, {0xBD7, 0xBD7}, {0xC01, 0xC03}, {0xC3E, 0xC44},
    {0xC46, 0xC48}, {0xC4A, 0xC4D}, {0xC55, 0xC56}, {0xC82, 0xC83},
    {0xCBE, 0xCC4}, {0xCC6, 0xCC8}, {0xCCA, 0xCCD}, {0xCD5, 0xCD6},
    {0xD02, 0xD03}, {0xD3E, 0xD43}, {0xD46, 0xD48}, {0xD4A, 0xD4D},
    {0xD57, 0xD57}, {0xE31, 0xE31}, {0xE34, 0xE3A}, {0xE47, 0xE4E},
    {0xEB1, 0xEB1}, {0xEB4, 0xEB9}, {0xEBB, 0xEBC}, {0xEC8, 0xECD},
    {0xF18, 0xF19}, {0xF35, 0xF35}, {0xF37, 0xF37}, {0xF39, 0xF39},
    {0xF3E, 0xF3E}, {0xF3F, 0xF3F}, {0xF71, 0xF84}, {0xF86, 0xF8B},
    {0xF90, 0xF95}, {0xF97, 0xF97}, {0xF99, 0xFAD}, {0xFB1, 0xFB7},
    {0xFB9, 0xFB9}, {0x20D0, 0x20DC}, {0x20E1, 0x20E1}, {0x302A, 0x302F},
    {0x3099, 0x3099}, {0x309A, 0x309A}, {0xB7, 0xB7}, {0x2D0, 0x2D0},
    {0x2D1, 0x2D1}, {0x387, 0x387}, {0x640, 0x640}, {0xE46, 0xE46},
    {0xEC6, 0xEC6}, {0x3005, 0x3005}, {0x3031, 0x3035}, {0x309D, 0x309E},
    {0x30FC, 0x30FE}, {u'.', u'.'}, {u'_', u'_'}, {u':', u':'},
    {u'-', u'-'}});

  bool is_in(char16_t c, const std::vector<CharacterRange>& ranges) {
    return std::any_of(ranges.begin(), ranges.end(), [&] (const auto& range) {
      return c >= range.m_first && c <= range.m_last;
    });
  }

  bool is_name_start(char16_t c) {
    return is_in(c, LETTER_RANGES) || c == u'_' || c == u':';
  }

  bool is_name_character(char16_t c) {
    return is_in(c, LETTER_RANGES) || is_in(c, NAME_RANGES);
  }

  std::string to_lower(std::string_view text) {
    return QString::fromUtf8(text.data(),
      static_cast<qsizetype>(text.size())).toLower().toStdString();
  }

  class IdGenerator {
    public:
      explicit IdGenerator(bool is_minimal)
        : m_is_minimal(is_minimal),
          m_minimal("`") {}

      std::string get_unique(std::string_view kind,
          const std::optional<std::string>& name = std::nullopt) {
        if(m_is_minimal) {
          return make_minimal();
        }
        return make_named(kind, name);
      }

    private:
      bool m_is_minimal;
      std::string m_minimal;
      std::unordered_map<std::string, int> m_counts;

      std::string make_minimal() {
        auto s = m_minimal;
        auto position = static_cast<int>(s.size()) - 1;
        do {
          auto n = s[position];
          if(n < 'z') {
            s[position] = static_cast<char>(n + 1);
            m_minimal = s;
            return s;
          }
          s[position] = 'a';
          --position;
        } while(position >= 0);
        m_minimal = "a" + s;
        return m_minimal;
      }

      int& get_count(const std::string& key) {
        return m_counts[key];
      }

      std::string make_named(
          std::string_view kind, const std::optional<std::string>& name) {
        auto s = std::string(kind);
        if(name) {
          auto text = to_utf16(*name);
          auto is_valid = !text.empty() && is_name_start(text.front()) &&
            std::all_of(text.begin() + 1, text.end(), is_name_character);
          if(!is_valid) {
            auto replaced = std::u16string();
            auto is_run = false;
            for(auto c : text) {
              if(is_name_character(c)) {
                replaced += c;
                is_run = false;
              } else if(!is_run) {
                replaced += u'_';
                is_run = true;
              }
            }
            if(!replaced.empty() && !is_name_start(replaced.front())) {
              replaced.insert(replaced.begin(), u'_');
            }
            s = to_utf8(replaced);
          } else {
            s = *name;
          }
          if(s.empty()) {
            s = std::string(kind);
          }
        }
        auto key = to_lower(s);
        auto& count = get_count(key);
        if(count == 0) {
          count = 1;
        }
        auto i = count++;
        auto id = s;
        if(kind == "cls" || i > 1) {
          if(s.empty() || s.back() != '_') {
            id += '-';
          }
          id += std::to_string(i);
        }
        key = to_lower(id);
        if(s != id && m_counts.contains(key) && m_counts[key] != 0) {
          id += "-" + std::to_string(m_counts[key]++);
        } else if(m_counts[key] == 0) {
          m_counts[key] = 2;
        }
        return id;
      }
  };

  enum class AttributeType : unsigned char {
    STRING,
    NUMBER,
    NUMBER_SEQUENCE,
    COLOR
  };

  struct AttributeDescriptor {
    std::string_view m_default;
    AttributeType m_type;
    bool m_is_unlimited = false;
  };

  using DescriptorTable =
    std::unordered_map<std::string_view, AttributeDescriptor>;

  const std::unordered_map<std::string_view, DescriptorTable>& get_tables() {
    using enum AttributeType;
    static const auto tables =
        std::unordered_map<std::string_view, DescriptorTable>({
      {"feFuncA", {{"amplitude", {"1", NUMBER}}, {"exponent", {"1", NUMBER}},
        {"intercept", {"0", NUMBER}}, {"offset", {"0", NUMBER}},
        {"slope", {"1", NUMBER}}}},
      {"feFuncB", {{"amplitude", {"1", NUMBER}}, {"exponent", {"1", NUMBER}},
        {"intercept", {"0", NUMBER}}, {"offset", {"0", NUMBER}},
        {"slope", {"1", NUMBER}}}},
      {"feFuncG", {{"amplitude", {"1", NUMBER}}, {"exponent", {"1", NUMBER}},
        {"intercept", {"0", NUMBER}}, {"offset", {"0", NUMBER}},
        {"slope", {"1", NUMBER}}}},
      {"feFuncR", {{"amplitude", {"1", NUMBER}}, {"exponent", {"1", NUMBER}},
        {"intercept", {"0", NUMBER}}, {"offset", {"0", NUMBER}},
        {"slope", {"1", NUMBER}}}},
      {"feDistantLight", {{"azimuth", {"0", NUMBER}},
        {"elevation", {"0", NUMBER}}}},
      {"feTurbulence", {{"baseFrequency", {"0", NUMBER_SEQUENCE}},
        {"seed", {"0", NUMBER}}, {"stitchTiles", {"noStitch", STRING}},
        {"type", {"turbulence", STRING}}}},
      {"feConvolveMatrix", {{"bias", {"0", NUMBER}},
        {"divisor", {"1", NUMBER}}, {"edgeMode", {"duplicate", STRING}},
        {"order", {"3", NUMBER_SEQUENCE}},
        {"preserveAlpha", {"false", STRING}}}},
      {"clipPath", {{"clipPathUnits", {"userSpaceOnUse", STRING}}}},
      {"circle", {{"cx", {"0", NUMBER, true}}, {"cy", {"0", NUMBER, true}},
        {"r", {"0", NUMBER, true}}}},
      {"ellipse", {{"cx", {"0", NUMBER, true}}, {"cy", {"0", NUMBER, true}},
        {"rx", {"0", NUMBER, true}}, {"ry", {"0", NUMBER, true}}}},
      {"feImage", {{"x", {"0%", STRING}}, {"y", {"0%", STRING}}}},
      {"radialGradient", {{"cx", {"50%", NUMBER}}, {"cy", {"50%", NUMBER}},
        {"fx", {"50%", STRING}}, {"fy", {"50%", STRING}},
        {"r", {"50%", NUMBER}}}},
      {"feDiffuseLighting", {{"diffuseConstant", {"1", NUMBER}}}},
      {"feOffset", {{"dx", {"0", NUMBER}}, {"dy", {"0", NUMBER}}}},
      {"text", {{"dx", {"0", NUMBER_SEQUENCE, true}},
        {"dy", {"0", NUMBER_SEQUENCE, true}},
        {"lengthAdjust", {"spacing", STRING}},
        {"rotate", {"0", NUMBER_SEQUENCE, true}}}},
      {"tspan", {{"dx", {"0", NUMBER_SEQUENCE, true}},
        {"dy", {"0", NUMBER_SEQUENCE, true}},
        {"lengthAdjust", {"spacing", STRING}},
        {"rotate", {"0", NUMBER_SEQUENCE, true}},
        {"x", {"", NUMBER, true}}, {"y", {"", NUMBER, true}}}},
      {"filter", {{"filterRes", {"null", NUMBER_SEQUENCE}},
        {"filterUnits", {"objectBoundingBox", STRING}},
        {"primitiveUnits", {"userSpaceOnUse", STRING}},
        {"x", {"-10%", NUMBER}}, {"y", {"-10%", NUMBER}},
        {"width", {"120%", NUMBER}}, {"height", {"120%", NUMBER}}}},
      {"svg", {{"height", {"100%", NUMBER, true}},
        {"width", {"100%", NUMBER, true}}}},
      {"feComposite", {{"k1", {"0", NUMBER}}, {"k2", {"0", NUMBER}},
        {"k3", {"0", NUMBER}}, {"k4", {"0", NUMBER}},
        {"operator", {"over", STRING}}}},
      {"textPath", {{"lengthAdjust", {"spacing", STRING}},
        {"method", {"align", STRING}}, {"spacing", {"exact", STRING}},
        {"startOffset", {"0", NUMBER, true}}}},
      {"marker", {{"markerHeight", {"3", NUMBER}},
        {"markerUnits", {"strokeWidth", STRING}},
        {"markerWidth", {"3", NUMBER}}, {"orient", {"0", NUMBER}},
        {"refX", {"0", NUMBER}}, {"refY", {"0", NUMBER}}}},
      {"mask", {{"maskContentUnits", {"userSpaceOnUse", STRING}},
        {"maskUnits", {"objectBoundingBox", STRING}},
        {"x", {"-10%", NUMBER}}, {"y", {"-10%", NUMBER}},
        {"width", {"120%", NUMBER}}, {"height", {"120%", NUMBER}}}},
      {"feBlend", {{"mode", {"normal", STRING}}}},
      {"feMorphology", {{"operator", {"erode", STRING}},
        {"radius", {"0", NUMBER_SEQUENCE}}}},
      {"pattern", {{"patternContentUnits", {"userSpaceOnUse", STRING}},
        {"patternUnits", {"objectBoundingBox", STRING}}}},
      {"feSpotLight", {{"pointsAtX", {"0", NUMBER}},
        {"pointsAtY", {"0", NUMBER}}, {"pointsAtZ", {"0", NUMBER}},
        {"specularExponent", {"1", NUMBER}}, {"z", {"0", NUMBER}}}},
      {"rect", {{"rx", {"0", NUMBER, true}}, {"ry", {"0", NUMBER, true}}}},
      {"feDisplacementMap", {{"scale", {"0", NUMBER}},
        {"xChannelSelector", {"A", STRING}},
        {"yChannelSelector", {"A", STRING}}}},
      {"feSpecularLighting", {{"specularConstant", {"1", NUMBER}},
        {"specularExponent", {"1", NUMBER}}}},
      {"feGaussianBlur", {{"stdDeviation", {"0", NUMBER_SEQUENCE}}}},
      {"a", {{"target", {"_self", STRING}}}},
      {"feColorMatrix", {{"type", {"matrix", STRING}},
        {"values", {"", NUMBER_SEQUENCE}}}},
      {"line", {{"x1", {"0", NUMBER, true}}, {"x2", {"0", NUMBER, true}},
        {"y1", {"0", NUMBER, true}}, {"y2", {"0", NUMBER, true}}}},
      {"linearGradient", {{"x1", {"0", NUMBER}}, {"x2", {"100%", NUMBER}},
        {"y1", {"0", NUMBER}}, {"y2", {"0", NUMBER}}}},
      {"fePointLight", {{"z", {"0", NUMBER}}}},
      {"*", {{"height", {"0", NUMBER}},
        {"points", {"", NUMBER_SEQUENCE, true}},
        {"preserveAspectRatio", {"xMidYMid", STRING}},
        {"spreadMethod", {"pad", STRING}},
        {"surfaceScale", {"1", NUMBER}},
        {"tableValues", {"", NUMBER_SEQUENCE}},
        {"viewBox", {"", NUMBER_SEQUENCE, true}},
        {"width", {"0", NUMBER, true}}, {"x", {"0", NUMBER, true}},
        {"y", {"0", NUMBER, true}},
        {"alignment-baseline", {"auto", STRING}},
        {"baseline-shift", {"baseline", STRING}},
        {"clip-path", {"none", STRING}}, {"clip-rule", {"nonzero", STRING}},
        {"clip", {"auto", STRING}},
        {"color-interpolation-filters", {"linearRGB", STRING}},
        {"color-interpolation", {"sRGB", STRING}},
        {"color-profile", {"auto", STRING}},
        {"color-rendering", {"auto", STRING}}, {"cursor", {"auto", STRING}},
        {"direction", {"ltr", STRING}}, {"display", {"inline", STRING}},
        {"dominant-baseline", {"auto", STRING}},
        {"enable-background", {"accumulate", STRING}},
        {"fill-opacity", {"1", NUMBER}}, {"fill", {"#000", COLOR}},
        {"filter", {"none", STRING}}, {"flood-color", {"#000", COLOR}},
        {"flood-opacity", {"1", NUMBER}}, {"font-size", {"0", NUMBER}},
        {"glyph-orientation-horizontal", {"auto", STRING}},
        {"glyph-orientation-vertical", {"auto", STRING}},
        {"image-rendering", {"auto", STRING}}, {"kerning", {"auto", STRING}},
        {"letter-spacing", {"0", NUMBER}},
        {"lighting-color", {"#fff", COLOR}},
        {"marker-end", {"none", STRING}}, {"marker-mid", {"none", STRING}},
        {"marker-start", {"none", STRING}}, {"mask", {"none", STRING}},
        {"opacity", {"1", NUMBER}},
        {"pointer-events", {"visiblePainted", STRING}},
        {"shape-rendering", {"auto", STRING}},
        {"stop-color", {"#000", COLOR}}, {"stop-opacity", {"1", NUMBER}},
        {"offset", {"", NUMBER}},
        {"stroke-dasharray", {"none", NUMBER_SEQUENCE}},
        {"stroke-dashoffset", {"0", NUMBER_SEQUENCE}},
        {"stroke-linecap", {"butt", STRING}},
        {"stroke-linejoin", {"miter", STRING}},
        {"stroke-miterlimit", {"4", NUMBER}},
        {"stroke-opacity", {"1", NUMBER}},
        {"stroke-width", {"1px", NUMBER}}, {"stroke", {"none", COLOR}},
        {"text-anchor", {"start", STRING}},
        {"text-decoration", {"none", STRING}},
        {"text-orientation", {"mixed", STRING}},
        {"text-rendering", {"auto", STRING}},
        {"unicode-bidi", {"normal", STRING}},
        {"visibility", {"visible", STRING}},
        {"word-spacing", {"normal", STRING}},
        {"writing-mode", {"lr-tb", STRING}}}}});
    return tables;
  }

  AttributeDescriptor get_descriptor(
      std::string_view tag, std::string_view attribute) {
    auto& tables = get_tables();
    if(auto table = tables.find(tag); table != tables.end()) {
      if(auto entry = table->second.find(attribute);
          entry != table->second.end()) {
        return entry->second;
      }
    }
    auto& common = tables.at("*");
    if(auto entry = common.find(attribute); entry != common.end()) {
      return entry->second;
    }
    return AttributeDescriptor("", AttributeType::STRING);
  }

  int resolve_precision(std::optional<int> precision, bool is_unlimited) {
    auto value = precision.value_or(0);
    if(value < 0) {
      return 3;
    }
    if(is_unlimited) {
      return value;
    }
    return value < 2 ? 2 : value;
  }

  std::string_view find_units(std::string_view text) {
    auto end = text.size();
    auto start = end;
    while(start != 0) {
      auto c = text[start - 1];
      if(c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c == '%' ||
          c == '-') {
        --start;
      } else {
        break;
      }
    }
    return text.substr(start);
  }

  JsValue parse_number(const JsValue& value, bool is_attribute,
      int precision) {
    auto text = to_js_string(value);
    auto digival = parse_float(text);
    auto units = find_units(text);
    auto number = to_js_number(value);
    if(!is_undefined(value) && loosely_equals(value, JsValue(number))) {
      return round_fixed(number, precision);
    } else if(digival == 0) {
      return 0.0;
    } else if(std::isfinite(digival)) {
      auto rounded = round_fixed(digival, precision);
      if(!units.empty() && (to_lower(units) != "px" || !is_attribute)) {
        return to_js_string(rounded) + std::string(units);
      }
      return rounded;
    }
    return value;
  }

  std::vector<std::string> split_sequence(std::string_view text) {
    text = trim_ascii(text);
    auto parts = std::vector<std::string>();
    auto current = std::string();
    auto i = std::size_t(0);
    while(i < text.size()) {
      auto c = text[i];
      if(c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
          c == '\v' || c == '\f') {
        parts.push_back(current);
        current.clear();
        while(i < text.size() && (text[i] == ',' || text[i] == ' ' ||
            text[i] == '\t' || text[i] == '\n' || text[i] == '\r' ||
            text[i] == '\v' || text[i] == '\f')) {
          ++i;
        }
        continue;
      }
      current += c;
      ++i;
    }
    parts.push_back(current);
    return parts;
  }

  JsValue get_attribute_value(std::string_view tag, std::string_view name,
      const JsValue& value, std::optional<int> precision) {
    auto descriptor = get_descriptor(tag, name);
    auto digits = resolve_precision(precision, descriptor.m_is_unlimited);
    if(descriptor.m_type == AttributeType::NUMBER) {
      return parse_number(value, tag != "*", digits);
    } else if(descriptor.m_type == AttributeType::NUMBER_SEQUENCE) {
      auto parts = std::vector<JsValue>();
      if(auto list = std::get_if<std::vector<double>>(&value)) {
        for(auto element : *list) {
          parts.push_back(element);
        }
      } else {
        for(auto& part : split_sequence(to_js_string(value))) {
          parts.push_back(part);
        }
      }
      auto out = std::string();
      for(auto i = std::size_t(0); i != parts.size(); ++i) {
        if(i != 0) {
          out += ' ';
        }
        out += to_js_string(parse_number(parts[i], tag != "*", digits));
      }
      return out;
    }
    return value;
  }

  struct Matrix4 {
    std::array<std::array<double, 4>, 4> m_values;

    Matrix4() {
      for(auto i = 0; i != 4; ++i) {
        for(auto j = 0; j != 4; ++j) {
          m_values[i][j] = i == j ? 1 : 0;
        }
      }
    }

    double& at(int i, int j) {
      return m_values[i][j];
    }

    double at(int i, int j) const {
      return m_values[i][j];
    }

    bool is_identity() const {
      auto& v = m_values;
      auto is_zero = [] (double x) {
        return !is_truthy(x);
      };
      return v[0][0] == 1 && is_zero(v[0][1]) && is_zero(v[0][2]) &&
        is_zero(v[0][3]) && is_zero(v[1][0]) && v[1][1] == 1 &&
        is_zero(v[1][2]) && is_zero(v[1][3]) && is_zero(v[2][0]) &&
        is_zero(v[2][1]) && v[2][2] == 1 && is_zero(v[2][3]) &&
        is_zero(v[3][0]) && is_zero(v[3][1]) && is_zero(v[3][2]) &&
        v[3][3] == 1;
    }

    Matrix4& pre_multiply(const Matrix4& other) {
      auto a = other.m_values;
      auto b = m_values;
      for(auto i = 0; i != 4; ++i) {
        for(auto j = 0; j != 4; ++j) {
          m_values[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] +
            a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
      }
      return *this;
    }

    Matrix4& scale(double x, double y, double z) {
      if(x != 1 || y != 1 || z != 1) {
        auto scale = Matrix4();
        scale.at(0, 0) = x;
        scale.at(1, 1) = y;
        scale.at(2, 2) = z;
        return pre_multiply(scale);
      }
      return *this;
    }

    double get_determinant() const {
      auto m00 = at(0, 0), m01 = at(0, 1), m02 = at(0, 2), m03 = at(0, 3);
      auto m10 = at(1, 0), m11 = at(1, 1), m12 = at(1, 2), m13 = at(1, 3);
      auto m20 = at(2, 0), m21 = at(2, 1), m22 = at(2, 2), m23 = at(2, 3);
      auto m30 = at(3, 0), m31 = at(3, 1), m32 = at(3, 2), m33 = at(3, 3);
      return m03 * m12 * m21 * m30 - m02 * m13 * m21 * m30 -
        m03 * m11 * m22 * m30 + m01 * m13 * m22 * m30 +
        m02 * m11 * m23 * m30 - m01 * m12 * m23 * m30 -
        m03 * m12 * m20 * m31 + m02 * m13 * m20 * m31 +
        m03 * m10 * m22 * m31 - m00 * m13 * m22 * m31 -
        m02 * m10 * m23 * m31 + m00 * m12 * m23 * m31 +
        m03 * m11 * m20 * m32 - m01 * m13 * m20 * m32 -
        m03 * m10 * m21 * m32 + m00 * m13 * m21 * m32 +
        m01 * m10 * m23 * m32 - m00 * m11 * m23 * m32 -
        m02 * m11 * m20 * m33 + m01 * m12 * m20 * m33 +
        m02 * m10 * m21 * m33 - m00 * m12 * m21 * m33 -
        m01 * m10 * m22 * m33 + m00 * m11 * m22 * m33;
    }

    std::optional<Matrix4> get_inverse() const {
      auto m00 = at(0, 0), m01 = at(0, 1), m02 = at(0, 2), m03 = at(0, 3);
      auto m10 = at(1, 0), m11 = at(1, 1), m12 = at(1, 2), m13 = at(1, 3);
      auto m20 = at(2, 0), m21 = at(2, 1), m22 = at(2, 2), m23 = at(2, 3);
      auto m30 = at(3, 0), m31 = at(3, 1), m32 = at(3, 2), m33 = at(3, 3);
      auto d10_21_11_20 = m10 * m21 - m11 * m20;
      auto d10_22_12_20 = m10 * m22 - m12 * m20;
      auto d10_23_13_20 = m10 * m23 - m13 * m20;
      auto d10_31_11_30 = m10 * m31 - m11 * m30;
      auto d10_32_12_30 = m10 * m32 - m12 * m30;
      auto d10_33_13_20 = m10 * m33 - m13 * m20;
      auto d10_33_13_30 = m10 * m33 - m13 * m30;
      auto d11_22_12_21 = m11 * m22 - m12 * m21;
      auto d11_32_12_31 = m11 * m32 - m12 * m31;
      auto d11_32_13_21 = m11 * m32 - m13 * m21;
      auto d11_33_13_21 = m11 * m33 - m13 * m21;
      auto d11_33_13_31 = m11 * m33 - m13 * m31;
      auto d12_23_13_22 = m12 * m23 - m13 * m22;
      auto d12_32_13_22 = m12 * m32 - m13 * m22;
      auto d12_33_13_32 = m12 * m33 - m13 * m32;
      auto d20_31_21_30 = m20 * m31 - m21 * m30;
      auto d20_32_22_30 = m20 * m32 - m22 * m30;
      auto d20_33_23_30 = m20 * m33 - m23 * m30;
      auto d21_32_22_31 = m21 * m32 - m22 * m31;
      auto d21_33_23_31 = m21 * m33 - m23 * m31;
      auto d22_33_23_32 = m22 * m33 - m23 * m32;
      auto c00 = m11 * d22_33_23_32 - m12 * d21_33_23_31 + m13 * d21_32_22_31;
      auto c01 = m10 * d22_33_23_32 - m12 * d20_33_23_30 + m13 * d20_32_22_30;
      auto c02 = m10 * d21_33_23_31 - m11 * d20_33_23_30 + m13 * d20_31_21_30;
      auto c03 = m10 * d21_32_22_31 - m11 * d20_32_22_30 + m12 * d20_31_21_30;
      auto c10 = m01 * d22_33_23_32 - m02 * d21_33_23_31 + m03 * d21_32_22_31;
      auto c11 = m00 * d22_33_23_32 - m02 * d20_33_23_30 + m03 * d20_32_22_30;
      auto c12 = m00 * d21_33_23_31 - m01 * d20_33_23_30 + m03 * d20_31_21_30;
      auto c13 = m00 * d21_32_22_31 - m01 * d20_32_22_30 + m02 * d20_31_21_30;
      auto c20 = m01 * d12_33_13_32 - m02 * d11_33_13_31 + m03 * d11_32_12_31;
      auto c21 = m00 * d12_33_13_32 - m02 * d10_33_13_30 + m03 * d10_32_12_30;
      auto c22 = m00 * d11_33_13_31 - m01 * d10_33_13_30 + m03 * d10_31_11_30;
      auto c23 = m00 * d11_33_13_31 - m01 * d10_33_13_30 + m03 * d10_31_11_30;
      auto c30 = m01 * d12_32_13_22 - m02 * d11_32_13_21 + m03 * d11_22_12_21;
      auto c31 = m00 * d12_23_13_22 - m02 * d10_23_13_20 + m03 * d10_22_12_20;
      auto c32 = m00 * d11_33_13_21 - m01 * d10_33_13_20 + m03 * d10_21_11_20;
      auto c33 = m00 * d11_22_12_21 - m01 * d10_22_12_20 + m02 * d10_21_11_20;
      c01 = -c01;
      c03 = -c03;
      c10 = -c10;
      c12 = -c12;
      c21 = -c21;
      c23 = -c23;
      c30 = -c30;
      c32 = -c32;
      auto determinant = m00 * c00 + m01 * c01 + m02 * c02 + m03 * c03;
      if(determinant == 0) {
        return std::nullopt;
      }
      auto inverse = Matrix4();
      auto factor = 1 / determinant;
      inverse.at(0, 0) = c00 * factor;
      inverse.at(0, 1) = c10 * factor;
      inverse.at(0, 2) = c20 * factor;
      inverse.at(0, 3) = c30 * factor;
      inverse.at(1, 0) = c01 * factor;
      inverse.at(1, 1) = c11 * factor;
      inverse.at(1, 2) = c21 * factor;
      inverse.at(1, 3) = c31 * factor;
      inverse.at(2, 0) = c02 * factor;
      inverse.at(2, 1) = c12 * factor;
      inverse.at(2, 2) = c22 * factor;
      inverse.at(2, 3) = c32 * factor;
      inverse.at(3, 0) = c03 * factor;
      inverse.at(3, 1) = c13 * factor;
      inverse.at(3, 2) = c23 * factor;
      inverse.at(3, 3) = c33 * factor;
      return inverse;
    }
  };

  Matrix4 to_matrix(const XdValue* value) {
    auto matrix = Matrix4();
    if(!is_truthy(value) || !value->is_object()) {
      return matrix;
    }
    auto is_number = [&] (std::string_view key) {
      auto member = value->find(key);
      return member && member->get_type() == XdValue::Type::NUMBER;
    };
    if(is_number("a") && is_number("b") && is_number("c") &&
        is_number("d") && is_number("tx") && is_number("ty")) {
      matrix.at(0, 0) = get_number(*value, "a");
      matrix.at(0, 1) = get_number(*value, "b");
      matrix.at(1, 0) = get_number(*value, "c");
      matrix.at(1, 1) = get_number(*value, "d");
      auto tx = get_number(*value, "tx");
      auto ty = get_number(*value, "ty");
      matrix.at(3, 0) = std::isnan(tx) ? 0 : tx;
      matrix.at(3, 1) = std::isnan(ty) ? 0 : ty;
      return matrix;
    }
    for(auto i = 0; i != 4; ++i) {
      auto row = value->find(std::to_string(i));
      for(auto j = 0; j != 4; ++j) {
        auto element = row && row->is_array() &&
          static_cast<std::size_t>(j) < row->get_elements().size() ?
            &row->get_elements()[j] : nullptr;
        matrix.at(i, j) = get_number(element);
      }
    }
    return matrix;
  }

  double get_norm(const std::array<double, 3>& v) {
    auto sum = 0.0;
    for(auto x : v) {
      sum += x * x;
    }
    return std::sqrt(sum);
  }

  std::array<double, 3> normalize(const std::array<double, 3>& v) {
    auto norm = get_norm(v);
    if(norm == 0) {
      norm = 1;
    }
    return std::array<double, 3>({v[0] / norm, v[1] / norm, v[2] / norm});
  }

  double get_inner_product(
      const std::array<double, 3>& a, const std::array<double, 3>& b) {
    auto sum = 0.0;
    for(auto i = 0; i != 3; ++i) {
      sum += a[i] * b[i];
    }
    return sum;
  }

  std::array<double, 3> combine(const std::array<double, 3>& a,
      const std::array<double, 3>& b, double a_scale, double b_scale) {
    return std::array<double, 3>({a_scale * a[0] + b_scale * b[0],
      a_scale * a[1] + b_scale * b[1],
      a_scale * a[2] + b_scale * b[2]});
  }

  struct Decomposition {
    std::array<double, 3> m_translation;
    std::array<double, 3> m_rotation;
    std::array<double, 3> m_scale;
    std::array<double, 3> m_skew;
  };

  Decomposition decompose(const Matrix4& matrix) {
    auto m = matrix;
    auto perspective = matrix;
    if(m.at(3, 3) == 0) {
      throw std::runtime_error("svgWriter failed!");
    }
    for(auto i = 0; i != 4; ++i) {
      for(auto j = 0; j != 4; ++j) {
        m.at(i, j) /= m.at(3, 3);
      }
    }
    for(auto i = 0; i != 3; ++i) {
      perspective.at(i, 3) = 0;
    }
    perspective.at(3, 3) = 1;
    if(perspective.get_determinant() == 0) {
      throw std::runtime_error("svgWriter failed!");
    }
    if(m.at(0, 3) != 0 || m.at(1, 3) != 0 || m.at(2, 3) != 0) {
      m.at(0, 3) = 0;
      m.at(1, 3) = 0;
      m.at(2, 3) = 0;
      m.at(3, 3) = 1;
    }
    auto decomposition = Decomposition();
    decomposition.m_translation =
      std::array<double, 3>({m.at(3, 0), m.at(3, 1), m.at(3, 2)});
    m.at(3, 0) = 0;
    m.at(3, 1) = 0;
    m.at(3, 2) = 0;
    auto row = std::array<std::array<double, 3>, 3>();
    for(auto j = 0; j != 3; ++j) {
      row[j] = std::array<double, 3>({m.at(j, 0), m.at(j, 1), m.at(j, 2)});
    }
    auto& scale = decomposition.m_scale;
    auto& skew = decomposition.m_skew;
    scale[0] = get_norm(row[0]);
    row[0] = normalize(row[0]);
    skew[0] = get_inner_product(row[0], row[1]);
    row[1] = combine(row[1], row[0], 1.0, -skew[0]);
    scale[1] = get_norm(row[1]);
    row[1] = normalize(row[1]);
    if(scale[1] != 0) {
      skew[0] /= scale[1];
    }
    skew[1] = get_inner_product(row[0], row[2]);
    row[2] = combine(row[2], row[0], 1.0, -skew[1]);
    skew[2] = get_inner_product(row[1], row[2]);
    row[2] = combine(row[2], row[1], 1.0, -skew[2]);
    scale[2] = get_norm(row[2]);
    if(scale[2] != 0) {
      row[2] = normalize(row[2]);
    }
    if(scale[2] != 0) {
      skew[1] /= scale[2];
      skew[2] /= scale[2];
    }
    auto cross = std::array<double, 3>({
      row[1][1] * row[2][2] - row[1][2] * row[2][1],
      row[1][2] * row[2][0] - row[1][0] * row[2][2],
      row[1][0] * row[2][1] - row[1][1] * row[2][0]});
    if(get_inner_product(row[0], cross) < 0) {
      for(auto k = 0; k != 3; ++k) {
        scale[k] *= -1;
        row[k][0] *= -1;
        row[k][1] *= -1;
        row[k][2] *= -1;
      }
    }
    auto& rotation = decomposition.m_rotation;
    rotation[1] = std::asin(-row[0][2]);
    if(std::cos(rotation[1]) != 0) {
      rotation[0] = std::atan2(row[1][2], row[2][2]);
      rotation[2] = std::atan2(row[0][1], row[0][0]);
    } else {
      rotation[0] = std::atan2(-row[2][0], row[1][1]);
      rotation[2] = 0;
    }
    return decomposition;
  }

  double round2(double value) {
    return round_fixed(value, 2);
  }

  std::string write_raw_matrix(
      const Matrix4& matrix, double x, double y, double precision) {
    auto digits = std::isfinite(precision) ?
      std::max(2, static_cast<int>(precision)) : 2;
    auto out = std::string("matrix(");
    out += to_js_string(round_fixed(matrix.at(0, 0), digits)) + ", ";
    out += to_js_string(round_fixed(matrix.at(0, 1), digits)) + ", ";
    out += to_js_string(round_fixed(matrix.at(1, 0), digits)) + ", ";
    out += to_js_string(round_fixed(matrix.at(1, 1), digits)) + ", ";
    out += to_js_string(round_fixed(matrix.at(3, 0) + x, digits)) + ", ";
    out += to_js_string(round_fixed(matrix.at(3, 1) + y, digits)) + ")";
    return out;
  }

  std::string write_decomposed(
      const Decomposition& decomposition, double precision) {
    constexpr auto DEGREES = 180 / 3.141592653589793;
    auto digits = std::isfinite(precision) ? static_cast<int>(precision) : 2;
    auto out = std::string();
    auto separator = std::string();
    auto& translation = decomposition.m_translation;
    if(is_truthy(translation[0]) || is_truthy(translation[1])) {
      if(is_truthy(translation[1])) {
        out += separator + "translate(" +
          to_js_string(round_fixed(translation[0], digits)) + " " +
          to_js_string(round_fixed(translation[1], digits)) + ")";
      } else {
        out += separator + "translate(" +
          to_js_string(round_fixed(translation[0], digits)) + ")";
      }
      separator = " ";
    }
    if(is_truthy(round2(DEGREES * decomposition.m_rotation[2]))) {
      out += separator + "rotate(" + to_js_string(
        round_fixed(DEGREES * decomposition.m_rotation[2], digits)) + ")";
      separator = " ";
    }
    if(is_truthy(round2(DEGREES * decomposition.m_skew[0]))) {
      out += separator + "skewX(" + to_js_string(
        round_fixed(DEGREES * decomposition.m_skew[0], digits)) + ")";
      separator = " ";
    }
    if(is_truthy(round2(DEGREES * decomposition.m_skew[1]))) {
      out += separator + "skewY(" + to_js_string(
        round_fixed(DEGREES * decomposition.m_skew[1], digits)) + ")";
      separator = " ";
    }
    auto& scale = decomposition.m_scale;
    if(round2(scale[0]) != 1 || round2(scale[1]) != 1) {
      digits = std::max(2, digits);
      if(scale[0] != scale[1]) {
        out += separator + "scale(" +
          to_js_string(round_fixed(scale[0], digits)) + " " +
          to_js_string(round_fixed(scale[1], digits)) + ")";
      } else {
        out += separator + "scale(" +
          to_js_string(round_fixed(scale[0], digits)) + ")";
      }
      separator = " ";
    }
    return out;
  }

  std::string write_transform(
      const Matrix4& matrix, double x, double y, double precision) {
    auto decomposition = decompose(matrix);
    if(is_truthy(decomposition.m_translation[2]) ||
        is_truthy(decomposition.m_rotation[0]) ||
        is_truthy(decomposition.m_rotation[1]) ||
        is_truthy(decomposition.m_skew[0]) ||
        is_truthy(decomposition.m_skew[1]) ||
        round2(decomposition.m_scale[2]) != 1) {
      return write_raw_matrix(matrix, x, y, precision);
    }
    decomposition.m_translation[0] += x;
    decomposition.m_translation[1] += y;
    return write_decomposed(decomposition, precision);
  }

  std::string get_transform(
      const std::optional<Matrix4>& matrix, double precision) {
    if(!matrix) {
      return "";
    }
    return write_transform(*matrix, 0, 0, precision);
  }

  std::optional<Matrix4> get_node_transform(const XdValue& node) {
    auto transform = node.find("transform");
    if(!is_truthy(transform)) {
      return std::nullopt;
    }
    return to_matrix(transform);
  }

  double at(const std::vector<double>& values, int index) {
    if(index < 0 || index >= static_cast<int>(values.size())) {
      return NOT_A_NUMBER;
    }
    return values[index];
  }

  struct PathSegment {
    std::string m_command;
    std::vector<double> m_absolute;
    std::vector<double> m_relative;
    double m_x = 0;
    double m_y = 0;
    double m_end_x = 0;
    double m_end_y = 0;
    double m_radius = NOT_A_NUMBER;
    double m_center_x = NOT_A_NUMBER;
    double m_center_y = NOT_A_NUMBER;
    double m_angle = NOT_A_NUMBER;
    std::optional<double> m_quadratic_x;
    std::optional<double> m_quadratic_y;
  };

  bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
      c == '\f';
  }

  std::vector<std::string> split_prepared_path(std::string_view path) {
    auto spaced = std::string();
    for(auto c : path) {
      if(std::string_view("MmLlCcZz").find(c) != std::string_view::npos) {
        spaced += ' ';
        spaced += c;
        spaced += ' ';
      } else {
        spaced += c;
      }
    }
    auto tokens = std::vector<std::string>();
    auto current = std::string();
    auto i = std::size_t(0);
    while(i < spaced.size()) {
      auto j = i;
      while(j < spaced.size() && is_space(spaced[j])) {
        ++j;
      }
      auto end = std::size_t(0);
      if(j < spaced.size() && spaced[j] == ',') {
        end = j + 1;
        while(end < spaced.size() && is_space(spaced[end])) {
          ++end;
        }
      } else if(j > i) {
        end = j;
      }
      if(end != 0) {
        tokens.push_back(current);
        current.clear();
        i = end;
        continue;
      }
      current += spaced[i];
      ++i;
    }
    tokens.push_back(current);
    return tokens;
  }

  std::vector<PathSegment> parse_prepared_path(std::string_view path) {
    auto tokens = split_prepared_path(path);
    auto position = std::size_t(0);
    auto next = [&] {
      if(position >= tokens.size()) {
        ++position;
        return NOT_A_NUMBER;
      }
      return parse_js_number(tokens[position++]);
    };
    auto segments = std::vector<PathSegment>();
    auto x = 0.0;
    auto y = 0.0;
    auto mx = 0.0;
    auto my = 0.0;
    while(position < tokens.size()) {
      auto token = tokens[position++];
      if(token.empty()) {
        continue;
      }
      auto segment = PathSegment();
      segment.m_command = token;
      for(auto& c : segment.m_command) {
        if(c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }
      segment.m_x = x;
      segment.m_y = y;
      auto& absolute = segment.m_absolute;
      auto& relative = segment.m_relative;
      if(token == "M") {
        mx = next();
        my = next();
        absolute = {mx, my};
        relative = {mx - x, my - y};
        x = absolute[0];
        y = absolute[1];
      } else if(token == "m") {
        mx = next();
        my = next();
        absolute = {mx + x, my + y};
        relative = {mx, my};
        x = absolute[0];
        y = absolute[1];
      } else if(token == "L") {
        auto ax = next();
        auto ay = next();
        absolute = {ax, ay};
        relative = {ax - x, ay - y};
        x = ax;
        y = ay;
      } else if(token == "l") {
        auto rx = next();
        auto ry = next();
        relative = {rx, ry};
        absolute = {rx + x, ry + y};
        x = absolute[0];
        y = absolute[1];
      } else if(token == "Q" || token == "C") {
        auto count = token == "Q" ? 4 : 6;
        for(auto i = 0; i != count; ++i) {
          absolute.push_back(next());
        }
        for(auto i = 0; i != count; ++i) {
          relative.push_back(absolute[i] - (i % 2 ? y : x));
        }
        x = absolute[count - 2];
        y = absolute[count - 1];
      } else if(token == "q" || token == "c") {
        auto count = token == "q" ? 4 : 6;
        for(auto i = 0; i != count; ++i) {
          relative.push_back(next());
        }
        for(auto i = 0; i != count; ++i) {
          absolute.push_back(relative[i] + (i % 2 ? y : x));
        }
        x = absolute[count - 2];
        y = absolute[count - 1];
      } else if(token == "Z" || token == "z") {
        x = mx;
        y = my;
      }
      segment.m_end_x = x;
      segment.m_end_y = y;
      segments.push_back(std::move(segment));
    }
    return segments;
  }

  bool is_path_letter(char c) {
    return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z';
  }

  std::size_t match_path_number(std::string_view text, std::size_t i) {
    auto start = i;
    if(i < text.size() && text[i] == '-') {
      ++i;
    }
    while(i < text.size() && is_digit(text[i])) {
      ++i;
    }
    if(i < text.size() && text[i] == '.') {
      ++i;
    }
    while(i < text.size() && is_digit(text[i])) {
      ++i;
    }
    if(i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
      auto j = i + 1;
      if(j < text.size() && (text[j] == '-' || text[j] == '+')) {
        ++j;
      }
      if(j < text.size() && is_digit(text[j])) {
        while(j < text.size() && is_digit(text[j])) {
          ++j;
        }
        i = j;
      }
    }
    return i - start;
  }

  int get_parameter_count(char name) {
    switch(name) {
      case 'a':
        return 7;
      case 'c':
        return 6;
      case 'h':
        return 1;
      case 'l':
        return 2;
      case 'm':
        return 2;
      case 'q':
        return 4;
      case 's':
        return 4;
      case 't':
        return 2;
      case 'v':
        return 1;
      case 'z':
        return 0;
      default:
        return -1;
    }
  }

  std::vector<PathSegment> parse_path(std::string_view path) {
    auto segments = std::vector<PathSegment>();
    auto x = 0.0;
    auto y = 0.0;
    auto mx = 0.0;
    auto my = 0.0;
    auto i = std::size_t(0);
    while(i < path.size()) {
      if(!is_path_letter(path[i])) {
        ++i;
        continue;
      }
      auto letter = path[i];
      ++i;
      while(i < path.size() && (is_space(path[i]) || path[i] == ',')) {
        ++i;
      }
      auto params = std::vector<double>();
      while(true) {
        auto length = match_path_number(path, i);
        auto number = path.substr(i, length);
        auto j = i + length;
        while(j < path.size() && is_space(path[j])) {
          ++j;
        }
        if(j < path.size() && path[j] == ',') {
          ++j;
        }
        while(j < path.size() && is_space(path[j])) {
          ++j;
        }
        if(!number.empty()) {
          params.push_back(parse_js_number(number));
        }
        if(j == i) {
          break;
        }
        i = j;
      }
      auto name = static_cast<char>(letter >= 'A' && letter <= 'Z' ?
        letter - 'A' + 'a' : letter);
      auto is_absolute = name != letter;
      auto sx = x;
      auto sy = y;
      if(name == 'm') {
        if(is_absolute) {
          mx = at(params, 0);
          my = at(params, 1);
        } else {
          mx = at(params, 0) + x;
          my = at(params, 1) + y;
        }
      }
      if(name == 'm' && params.size() > 2) {
        auto segment = PathSegment();
        segment.m_command = "m";
        if(is_absolute) {
          segment.m_relative = {params[0] - x, params[1] - y};
          x = params[0];
          y = params[1];
          segment.m_absolute = {params[0], params[1]};
        } else {
          segment.m_absolute = {params[0] + x, params[1] + y};
          x += params[0];
          y += params[1];
          segment.m_relative = {params[0], params[1]};
        }
        params.erase(params.begin(), params.begin() + 2);
        segment.m_x = sx;
        segment.m_y = sy;
        segment.m_end_x = x;
        segment.m_end_y = y;
        segments.push_back(std::move(segment));
        name = 'l';
      }
      auto count = get_parameter_count(name);
      if(count < 0) {
        continue;
      }
      auto absolute = std::vector<double>();
      auto relative = std::vector<double>();
      while(static_cast<int>(params.size()) >= count) {
        sx = x;
        sy = y;
        auto args = std::vector<double>(params.begin(),
          params.begin() + count);
        params.erase(params.begin(), params.begin() + count);
        if(is_absolute) {
          switch(name) {
            case 'a':
            case 'm':
            case 'l':
              relative = args;
              relative[relative.size() - 2] -= x;
              relative[relative.size() - 1] -= y;
              absolute = args;
              x = absolute[absolute.size() - 2];
              y = absolute[absolute.size() - 1];
              break;
            case 'c':
            case 's':
            case 't':
            case 'q':
              relative = args;
              for(auto k = std::size_t(0); k != relative.size(); ++k) {
                relative[k] -= k % 2 ? y : x;
              }
              absolute = args;
              x = absolute[absolute.size() - 2];
              y = absolute[absolute.size() - 1];
              break;
            case 'h':
              relative = {args[0] - x};
              absolute = args;
              x = absolute[0];
              break;
            case 'v':
              relative = {args[0] - y};
              absolute = args;
              y = absolute[0];
              break;
            case 'z':
              sx = x = mx;
              sy = y = my;
              break;
          }
        } else {
          switch(name) {
            case 'a':
            case 'm':
            case 'l':
              absolute = args;
              x = absolute[absolute.size() - 2] += x;
              y = absolute[absolute.size() - 1] += y;
              relative = args;
              break;
            case 'c':
            case 's':
            case 't':
            case 'q':
              absolute = args;
              for(auto k = std::size_t(0); k != absolute.size(); ++k) {
                absolute[k] += k % 2 ? y : x;
              }
              relative = args;
              x = absolute[absolute.size() - 2];
              y = absolute[absolute.size() - 1];
              break;
            case 'h':
              absolute = {args[0] + x};
              relative = args;
              x = absolute[0];
              break;
            case 'v':
              absolute = {args[0] + y};
              relative = args;
              y = absolute[0];
              break;
            case 'z':
              sx = x = mx;
              sy = y = my;
              break;
          }
        }
        auto segment = PathSegment();
        segment.m_command = std::string(1, name);
        segment.m_absolute = absolute;
        segment.m_relative = relative;
        segment.m_x = sx;
        segment.m_y = sy;
        segment.m_end_x = x;
        segment.m_end_y = y;
        segments.push_back(std::move(segment));
        if(count == 0) {
          break;
        }
      }
    }
    return segments;
  }

  double get_length(double x1, double y1, double x2, double y2) {
    x1 -= x2;
    y1 -= y2;
    return std::sqrt(x1 * x1 + y1 * y1);
  }

  double get_angle(double x1, double y1, double x2, double y2) {
    if(x1 == x2) {
      if(y2 < y1) {
        return 270;
      }
      return 90;
    } else if(y1 == y2) {
      if(x2 < x1) {
        return 180;
      }
      return 0;
    }
    auto angle = std::atan2(y2 - y1, x2 - x1) * 180 / 3.141592653589793;
    if(angle < 0) {
      angle += 360;
    }
    return angle;
  }

  bool are_close_enough(double a, double b) {
    return std::abs(a - b) < (a + b) / 2 / 100;
  }

  struct Arc {
    double m_center_x = NOT_A_NUMBER;
    double m_center_y = NOT_A_NUMBER;
    double m_radius = NOT_A_NUMBER;
    double m_angle = NOT_A_NUMBER;
    double m_large = NOT_A_NUMBER;
    double m_sweep = NOT_A_NUMBER;
  };

  std::array<double, 4> get_bisector(
      double x1, double y1, double x2, double y2) {
    auto dx = x2 - x1;
    auto dy = y2 - y1;
    if(dy == 0) {
      return std::array<double, 4>({x1 + dx / 2, 0, x1 + dx / 2, 1});
    } else if(dx == 0) {
      return std::array<double, 4>({0, y1 + dy / 2, 1, y1 + dy / 2});
    }
    auto m = -dx / dy;
    auto x3 = x1 + dx / 2;
    auto y3 = y1 + dy / 2;
    auto b = y3 - m * x3;
    return std::array<double, 4>({0, b, 1, m + b});
  }

  Arc make_arc(double x1, double y1, double x2, double y2, double x3,
      double y3) {
    auto arc = Arc();
    if(x1 == x2 && y1 == y2 || x3 == x2 && y3 == y2) {
      return arc;
    }
    if(x1 == x3 && y1 == y3) {
      arc.m_radius = get_length(x1, y1, x2, y2) / 2;
      arc.m_angle = 360;
      arc.m_large = 0;
      arc.m_sweep = 0;
      return arc;
    }
    auto b1 = get_bisector(x1, y1, x2, y2);
    auto b2 = get_bisector(x2, y2, x3, y3);
    auto nx = (b1[0] * b1[3] - b1[1] * b1[2]) * (b2[0] - b2[2]) -
      (b1[0] - b1[2]) * (b2[0] * b2[3] - b2[1] * b2[2]);
    auto ny = (b1[0] * b1[3] - b1[1] * b1[2]) * (b2[1] - b2[3]) -
      (b1[1] - b1[3]) * (b2[0] * b2[3] - b2[1] * b2[2]);
    auto denominator =
      (b1[0] - b1[2]) * (b2[1] - b2[3]) - (b1[1] - b1[3]) * (b2[0] - b2[2]);
    if(!is_truthy(denominator)) {
      return arc;
    }
    auto px = nx / denominator;
    auto py = ny / denominator;
    auto start = get_angle(px, py, x1, y1);
    auto middle = get_angle(px, py, x2, y2);
    auto end = get_angle(px, py, x3, y3);
    auto angle = end - start;
    if(middle < start) {
      if(start < end) {
        angle -= 360;
      } else if(middle < end) {
        angle += 360;
      }
    } else {
      if(end < start) {
        angle += 360;
      } else if(end < middle) {
        angle -= 360;
      }
    }
    arc.m_center_x = px;
    arc.m_center_y = py;
    arc.m_radius = get_length(x1, y1, px, py);
    arc.m_angle = angle;
    arc.m_large = std::abs(angle) > 180 ? 1 : 0;
    arc.m_sweep = angle > 0 ? 1 : 0;
    return arc;
  }

  const auto BEZIER_TABLE = std::array<std::array<double, 4>, 10>({{
    {1, 0, 0, 0}, {0.729, 0.243, 0.027, 0.001}, {0.512, 0.384, 0.096, 0.008},
    {0.343, 0.441, 0.189, 0.027}, {0.216, 0.432, 0.288, 0.064},
    {0.125, 0.375, 0.375, 0.125}, {0.064, 0.288, 0.432, 0.216},
    {0.027, 0.189, 0.441, 0.343}, {0.008, 0.096, 0.384, 0.512},
    {0.001, 0.027, 0.243, 0.729}}});

  std::optional<Arc> as_arc(double p1x, double p1y, double c1x, double c1y,
      double c2x, double c2y, double p2x, double p2y) {
    auto dot = [&] (int t) {
      auto& a = BEZIER_TABLE[t];
      return std::array<double, 2>({
        a[0] * p1x + a[1] * c1x + a[2] * c2x + a[3] * p2x,
        a[0] * p1y + a[1] * c1y + a[2] * c2y + a[3] * p2y});
    };
    auto m = dot(5);
    auto arc = make_arc(p1x, p1y, m[0], m[1], p2x, p2y);
    auto dimension = std::max(std::abs(p1x - p2x), std::abs(p1y - p2y));
    auto sigma = std::min(.05, dimension / 50);
    if(!is_truthy(arc.m_radius)) {
      return std::nullopt;
    }
    for(auto i = 1; i < 10; ++i) {
      if(i != 5) {
        auto point = dot(i);
        if(std::abs(get_length(arc.m_center_x, arc.m_center_y, point[0],
            point[1]) - arc.m_radius) > sigma) {
          return std::nullopt;
        }
      }
    }
    return arc;
  }

  std::string clean_numbers(std::string_view text) {
    auto first = std::string();
    auto i = std::size_t(0);
    while(i < text.size()) {
      if(text[i] == '.' && i + 1 < text.size() && is_digit(text[i + 1])) {
        auto j = i + 1;
        while(j < text.size() && is_digit(text[j])) {
          ++j;
        }
        first.append(text.substr(i, j - i));
        if(j + 2 < text.size() && text[j] == ',' && text[j + 1] == '0' &&
            text[j + 2] == '.') {
          i = j + 2;
        } else {
          i = j;
        }
        continue;
      }
      first += text[i];
      ++i;
    }
    auto out = std::string();
    for(auto k = std::size_t(0); k < first.size(); ++k) {
      if(first[k] == '0' && k + 1 < first.size() && first[k + 1] == '.' &&
          (k == 0 || !is_digit(first[k - 1]))) {
        continue;
      }
      out += first[k];
    }
    return out;
  }

  std::string remove_comma_dots(std::string_view text) {
    auto out = std::string();
    auto i = std::size_t(0);
    while(i < text.size()) {
      if(text[i] == '.' && i + 1 < text.size() && is_digit(text[i + 1])) {
        auto j = i + 1;
        while(j < text.size() && is_digit(text[j])) {
          ++j;
        }
        out.append(text.substr(i, j - i));
        if(j + 1 < text.size() && text[j] == ',' && text[j + 1] == '.') {
          i = j + 1;
        } else {
          i = j;
        }
        continue;
      }
      out += text[i];
      ++i;
    }
    return out;
  }

  double get_sign(double value) {
    if(std::isnan(value)) {
      return NOT_A_NUMBER;
    }
    if(value > 0) {
      return 1;
    } else if(value < 0) {
      return -1;
    }
    return value;
  }

  void drop_front(std::vector<double>& values, int count) {
    auto n = std::min(static_cast<int>(values.size()), count);
    values.erase(values.begin(), values.begin() + n);
  }

  void set_at(std::vector<double>& values, int index, double value) {
    if(index >= static_cast<int>(values.size())) {
      values.resize(index + 1, NOT_A_NUMBER);
    }
    values[index] = value;
  }

  class PathOptimizer {
    public:
      explicit PathOptimizer(int precision)
        : m_prec(std::pow(10.0, precision)),
          m_prec1(std::pow(10.0, std::max(precision - 1, 0))),
          m_prec2(std::pow(10.0, std::max(precision - 2, 0))),
          m_sigma(std::pow(10.0, -precision)),
          m_gamma(std::pow(10.0, 1 - precision)) {}

      std::string optimize(std::string_view path, bool is_prepared) {
        auto segments =
          is_prepared ? parse_prepared_path(path) : parse_path(path);
        expand_shorthands(segments);
        for(auto i = 0; i < static_cast<int>(segments.size()); ++i) {
          to_line(segments[i]);
          to_horizontal(segments[i]);
          to_vertical(segments[i]);
          if(to_arc(get_previous(segments, i), segments[i])) {
            segments.erase(segments.begin() + i);
            --i;
          }
          to_smooth(get_previous(segments, i), segments[i]);
          to_quadratic(segments[i]);
          to_smooth_quadratic(get_previous(segments, i), segments[i]);
          if(unite_lines(get_previous(segments, i), segments[i])) {
            segments.erase(segments.begin() + i);
            --i;
          }
        }
        return remove_comma_dots(write(segments));
      }

    private:
      double m_prec;
      double m_prec1;
      double m_prec2;
      double m_sigma;
      double m_gamma;

      double number(double value) const {
        auto rounded = js_round(value * m_prec) / m_prec;
        if(m_prec1 != m_prec2) {
          auto rounded1 = js_round(value * m_prec1) / m_prec1;
          auto rounded2 = js_round(value * m_prec2) / m_prec2;
          if(rounded2 == rounded1) {
            return rounded1;
          }
        }
        return rounded;
      }

      bool is_small(double value) const {
        return std::abs(js_round(value * m_prec) / m_prec) <= m_sigma;
      }

      bool is_good_enough(double value) const {
        return std::abs(js_round(value * m_prec1) / m_prec1) <= m_gamma;
      }

      static PathSegment* get_previous(
          std::vector<PathSegment>& segments, int i) {
        if(i <= 0 || i > static_cast<int>(segments.size())) {
          return nullptr;
        }
        return &segments[i - 1];
      }

      static void expand_shorthands(std::vector<PathSegment>& segments) {
        for(auto i = std::size_t(1); i < segments.size(); ++i) {
          auto& segment = segments[i];
          auto& previous = segments[i - 1];
          if(segment.m_command == "s") {
            if(previous.m_command == "c") {
              auto dx = segment.m_x - at(previous.m_absolute, 2);
              auto dy = segment.m_y - at(previous.m_absolute, 3);
              segment.m_absolute.insert(segment.m_absolute.begin(),
                {segment.m_x + dx, segment.m_y + dy});
              segment.m_relative.insert(segment.m_relative.begin(), {dx, dy});
            } else {
              segment.m_absolute.insert(segment.m_absolute.begin(),
                {segment.m_x, segment.m_y});
              segment.m_relative.insert(
                segment.m_relative.begin(), {0.0, 0.0});
            }
            segment.m_command = "c";
          }
          if(segment.m_command == "t") {
            if(previous.m_command == "q") {
              auto dx = segment.m_x - at(previous.m_absolute, 0);
              auto dy = segment.m_y - at(previous.m_absolute, 1);
              segment.m_absolute.insert(segment.m_absolute.begin(),
                {segment.m_x + dx, segment.m_y + dy});
              segment.m_relative.insert(segment.m_relative.begin(), {dx, dy});
            } else {
              segment.m_absolute.insert(segment.m_absolute.begin(),
                {segment.m_x, segment.m_y});
              segment.m_relative.insert(
                segment.m_relative.begin(), {0.0, 0.0});
            }
            segment.m_command = "q";
          }
        }
      }

      bool is_line(const PathSegment& segment) const {
        auto& absolute = segment.m_absolute;
        auto i = static_cast<int>(absolute.size()) - 1;
        auto xn = NOT_A_NUMBER;
        auto yn = NOT_A_NUMBER;
        while(i >= 0) {
          yn = at(absolute, i);
          xn = at(absolute, --i);
          --i;
          if(segment.m_x != xn || segment.m_y != yn) {
            break;
          }
        }
        if(i < 0) {
          return true;
        }
        if(segment.m_x == xn) {
          while(i >= 0) {
            if(segment.m_x != at(absolute, --i)) {
              return false;
            }
            --i;
          }
          return true;
        }
        auto a = (yn - segment.m_y) / (xn - segment.m_x);
        while(i >= 0) {
          if(std::abs(a * (at(absolute, i - 1) - segment.m_x) + segment.m_y -
              at(absolute, i)) > m_sigma) {
            return false;
          }
          i -= 2;
        }
        return true;
      }

      void to_line(PathSegment& segment) const {
        if(segment.m_command == "c" && is_line(segment)) {
          segment.m_command = "l";
          drop_front(segment.m_relative, 4);
          drop_front(segment.m_absolute, 4);
        }
      }

      void to_quadratic(PathSegment& segment) const {
        if(segment.m_command != "c") {
          return;
        }
        auto x1 = segment.m_x;
        auto y1 = segment.m_y;
        auto x2 = at(segment.m_absolute, 0);
        auto y2 = at(segment.m_absolute, 1);
        auto x3 = at(segment.m_absolute, 2);
        auto y3 = at(segment.m_absolute, 3);
        auto x4 = at(segment.m_absolute, 4);
        auto y4 = at(segment.m_absolute, 5);
        auto cx1 = (x2 - x1) * 1.5 + x1;
        auto cy1 = (y2 - y1) * 1.5 + y1;
        auto cx2 = (x3 - x4) * 1.5 + x4;
        auto cy2 = (y3 - y4) * 1.5 + y4;
        auto tolerance = (std::abs(x2 - x1) + std::abs(y2 - y1) +
          std::abs(x3 - x4) + std::abs(y3 - y4)) / 400 / 2;
        if(std::abs(cx2 - cx1) <= tolerance &&
            std::abs(cy2 - cy1) <= tolerance) {
          segment.m_command = "q";
          segment.m_absolute = {cx1, cy1, x4, y4};
          segment.m_relative = {cx1 - x1, cy1 - y1, x4 - x1, y4 - y1};
        }
      }

      void to_horizontal(PathSegment& segment) const {
        if(segment.m_command == "l" &&
            is_small(at(segment.m_relative, 1))) {
          segment.m_command = "h";
          if(!segment.m_absolute.empty()) {
            segment.m_absolute.pop_back();
          }
          if(!segment.m_relative.empty()) {
            segment.m_relative.pop_back();
          }
        }
      }

      void to_vertical(PathSegment& segment) const {
        if(segment.m_command == "l" &&
            is_small(at(segment.m_relative, 0))) {
          segment.m_command = "v";
          drop_front(segment.m_absolute, 1);
          drop_front(segment.m_relative, 1);
        }
      }

      bool to_arc(PathSegment* previous, PathSegment& segment) const {
        if(segment.m_command != "c") {
          return false;
        }
        auto& rest = segment.m_absolute;
        auto x = segment.m_x;
        auto y = segment.m_y;
        auto X = at(rest, 4);
        auto Y = at(rest, 5);
        auto arc = as_arc(x, y, at(rest, 0), at(rest, 1), at(rest, 2),
          at(rest, 3), X, Y);
        if(!arc || !is_truthy(arc->m_radius) ||
            !(arc->m_radius < get_length(x, y, X, Y) * 10)) {
          return false;
        }
        if(previous && is_truthy(previous->m_angle) &&
            get_sign(previous->m_angle) == get_sign(arc->m_angle) &&
            are_close_enough(previous->m_radius, arc->m_radius) &&
            are_close_enough(previous->m_center_x, arc->m_center_x) &&
            are_close_enough(previous->m_center_y, arc->m_center_y)) {
          previous->m_angle += arc->m_angle;
          if(!is_truthy(number(std::abs(X - previous->m_x))) &&
              !is_truthy(number(std::abs(Y - previous->m_y)))) {
            segment.m_command = "a";
            set_at(rest, 0, previous->m_radius);
            set_at(rest, 1, previous->m_radius);
            set_at(rest, 2, 0);
            set_at(rest, 3, arc->m_large);
            set_at(rest, 4, arc->m_sweep);
            set_at(rest, 5, previous->m_x);
            rest.push_back(previous->m_y);
            auto& relative = segment.m_relative;
            for(auto k = 0; k != 5; ++k) {
              set_at(relative, k, rest[k]);
            }
            set_at(relative, 5, rest[5] - x);
            set_at(relative, 6, rest[6] - y);
            return false;
          }
          auto renewed = make_arc(previous->m_x, previous->m_y, segment.m_x,
            segment.m_y, X, Y);
          if(is_truthy(renewed.m_radius) && is_truthy(renewed.m_angle)) {
            previous->m_angle = renewed.m_angle;
            auto& absolute = previous->m_absolute;
            set_at(absolute, 0, renewed.m_radius);
            set_at(absolute, 1, renewed.m_radius);
            set_at(absolute, 3, std::abs(renewed.m_angle) > 180 ? 1 : 0);
            set_at(absolute, 4, renewed.m_angle > 0 ? 1 : 0);
            set_at(absolute, 5, X);
            set_at(absolute, 6, Y);
            auto& relative = previous->m_relative;
            for(auto k = 0; k != 5; ++k) {
              set_at(relative, k, absolute[k]);
            }
            set_at(relative, 5, absolute[5] - previous->m_x);
            set_at(relative, 6, absolute[6] - previous->m_y);
            return true;
          }
        }
        auto n = number(arc->m_radius);
        segment.m_command = "a";
        set_at(rest, 0, n);
        set_at(rest, 1, n);
        set_at(rest, 2, 0);
        set_at(rest, 3, arc->m_large);
        rest.push_back(at(rest, 5));
        set_at(rest, 5, at(rest, 4));
        set_at(rest, 4, arc->m_sweep);
        auto& relative = segment.m_relative;
        for(auto k = 0; k != 5; ++k) {
          set_at(relative, k, rest[k]);
        }
        set_at(relative, 5, rest[5] - x);
        relative.push_back(rest[6] - y);
        segment.m_radius = arc->m_radius;
        segment.m_center_x = arc->m_center_x;
        segment.m_center_y = arc->m_center_y;
        segment.m_angle = arc->m_angle;
        return false;
      }

      void to_smooth(PathSegment* previous, PathSegment& segment) const {
        if(segment.m_command != "c") {
          return;
        }
        if(!previous ||
            previous->m_command != "c" && previous->m_command != "s") {
          if(!is_truthy(number(at(segment.m_relative, 0))) &&
              !is_truthy(number(at(segment.m_relative, 1)))) {
            drop_front(segment.m_absolute, 2);
            drop_front(segment.m_relative, 2);
            segment.m_command = "s";
          }
        } else {
          auto size = static_cast<int>(previous->m_absolute.size());
          auto anchor_x =
            2 * segment.m_x - at(previous->m_absolute, size - 4);
          auto anchor_y =
            2 * segment.m_y - at(previous->m_absolute, size - 3);
          if(is_good_enough(at(segment.m_absolute, 0) - anchor_x) &&
              is_good_enough(at(segment.m_absolute, 1) - anchor_y)) {
            drop_front(segment.m_absolute, 2);
            drop_front(segment.m_relative, 2);
            segment.m_command = "s";
          }
        }
      }

      void to_smooth_quadratic(
          PathSegment* previous, PathSegment& segment) const {
        if(segment.m_command != "q") {
          return;
        }
        if(!previous ||
            previous->m_command != "q" && previous->m_command != "t") {
          if(!is_truthy(number(at(segment.m_relative, 0))) &&
              !is_truthy(number(at(segment.m_relative, 1)))) {
            drop_front(segment.m_absolute, 2);
            drop_front(segment.m_relative, 2);
            segment.m_command = "t";
          }
        } else {
          auto previous_x = segment.m_quadratic_x ?
            *segment.m_quadratic_x : at(previous->m_absolute, 0);
          auto previous_y = segment.m_quadratic_y ?
            *segment.m_quadratic_y : at(previous->m_absolute, 1);
          auto anchor_x = 2 * segment.m_x - previous_x;
          auto anchor_y = 2 * segment.m_y - previous_y;
          if(is_good_enough(at(segment.m_absolute, 0) - anchor_x) &&
              is_good_enough(at(segment.m_absolute, 1) - anchor_y)) {
            segment.m_quadratic_x = anchor_x;
            segment.m_quadratic_y = anchor_y;
            drop_front(segment.m_absolute, 2);
            drop_front(segment.m_relative, 2);
            segment.m_command = "t";
          }
        }
      }

      static bool unite_lines(PathSegment* previous, PathSegment& segment) {
        if(previous && previous->m_command == segment.m_command &&
            (previous->m_command == "h" || previous->m_command == "v")) {
          set_at(previous->m_absolute, 0, at(segment.m_absolute, 0));
          set_at(previous->m_relative, 0,
            at(previous->m_relative, 0) + at(segment.m_relative, 0));
          return true;
        }
        return false;
      }

      std::string write(const std::vector<PathSegment>& segments) const {
        auto result = std::string();
        auto previous = std::string();
        for(auto i = std::size_t(0); i < segments.size(); ++i) {
          auto& segment = segments[i];
          auto command = segment.m_command;
          auto next = i + 1 < segments.size() ? &segments[i + 1] : nullptr;
          auto is_loop = next && next->m_command == "z" &&
            segment.m_end_x == next->m_end_x &&
            segment.m_end_y == next->m_end_y;
          auto absolute = std::string();
          auto relative = std::string();
          for(auto j = std::size_t(0); j < segment.m_absolute.size(); ++j) {
            auto a = number(segment.m_absolute[j]);
            auto r = number(at(segment.m_relative, static_cast<int>(j)));
            absolute += (a < 0 || j == 0 ? "" : ",") + to_js_string(a);
            relative += (r < 0 || j == 0 ? "" : ",") + to_js_string(r);
          }
          absolute = clean_numbers(absolute);
          relative = clean_numbers(relative);
          auto argument = std::string();
          if(absolute.size() <= relative.size() || is_loop) {
            for(auto& c : command) {
              if(c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
              }
            }
            argument = absolute;
            if(is_loop && (command == "H" || command == "V")) {
              command = "L";
            }
          } else {
            argument = relative;
          }
          if(command == "L" && is_loop) {
            continue;
          }
          if(previous != command && !(previous == "m" && command == "l" ||
              previous == "M" && command == "L")) {
            result += command;
          } else {
            result += !argument.empty() && argument.front() == '-' ? "" : ",";
          }
          previous = command;
          result += argument;
        }
        return result;
      }
  };

  std::string optimize_path(
      std::string_view path, int precision, bool is_prepared) {
    return PathOptimizer(precision).optimize(path, is_prepared);
  }

  std::string join_numbers(std::initializer_list<double> values) {
    auto out = std::string();
    auto is_first = true;
    for(auto value : values) {
      if(!is_first) {
        out += ',';
      }
      is_first = false;
      out += to_js_string(value);
    }
    return out;
  }

  std::string make_round_rect_path(double x, double y, double width,
      double height, std::array<double, 4> r) {
    auto top = 0.0;
    auto right = 0.0;
    auto bottom = 0.0;
    auto left = 0.0;
    auto is_ok = false;
    auto small_length = std::numeric_limits<double>::infinity();
    auto small_fraction = NOT_A_NUMBER;
    auto check = [&] (double total, int i, int j) {
      auto length = total - r[i] - r[j];
      if(small_length > length) {
        small_length = length;
        small_fraction = total / (r[i] + r[j]);
      }
      return length;
    };
    while(!is_ok) {
      top = check(width, 0, 1);
      right = check(height, 1, 2);
      bottom = check(width, 2, 3);
      left = check(height, 0, 3);
      if(small_length + .01 < 0) {
        for(auto& radius : r) {
          radius *= small_fraction;
        }
        small_length = 0;
        small_fraction = NOT_A_NUMBER;
      } else {
        is_ok = true;
      }
    }
    return "M" + join_numbers({x + r[0], y}) + "h" + to_js_string(top) +
      "a" + join_numbers({r[1], r[1], 0, 0, 1, r[1], r[1]}) + "v" +
      to_js_string(right) + "a" +
      join_numbers({r[2], r[2], 0, 0, 1, -r[2], r[2]}) + "h" +
      to_js_string(-bottom) + "a" +
      join_numbers({r[3], r[3], 0, 0, 1, -r[3], -r[3]}) + "v" +
      to_js_string(-left) + "a" +
      join_numbers({r[0], r[0], 0, 0, 1, r[0], -r[0]}) + "z";
  }

  struct StyleBlock {
    std::vector<std::string> m_classes;
    OrderedMap<JsValue> m_rules;
    OrderedMap<bool> m_defaults;
    std::vector<int> m_tags;
  };

  bool is_default(const StyleBlock& block, std::string_view name) {
    auto flag = block.m_defaults.find(name);
    return flag && *flag;
  }

  void add_rule(StyleBlock& block, std::string_view property, JsValue value) {
    auto descriptor = get_descriptor("*", property);
    auto rounded = get_attribute_value("*", property, value, 3);
    block.m_defaults.set(
      property, to_js_string(rounded) == descriptor.m_default);
    block.m_rules.set(property, std::move(value));
  }

  void enforce_rule(StyleBlock& block, const std::string& name) {
    auto value = JsValue();
    if(auto rule = block.m_rules.find(name)) {
      value = *rule;
    }
    block.m_rules.set(name + "!", value);
    block.m_defaults.set(name + "!", false);
    block.m_defaults.erase(name);
    block.m_rules.erase(name);
  }

  void remove_rule(StyleBlock& block, std::string_view name) {
    block.m_rules.erase(name);
  }

  bool has_rules(const StyleBlock& block) {
    for(auto& entry : block.m_rules.get_entries()) {
      if(!is_default(block, entry.first)) {
        return true;
      }
    }
    return false;
  }

  JsValue get_rule(const StyleBlock& block, std::string_view name) {
    if(auto rule = block.m_rules.find(name)) {
      return *rule;
    }
    return JsValue();
  }

  bool is_equal(const StyleBlock& left, const StyleBlock& right) {
    for(auto& entry : left.m_rules.get_entries()) {
      if(!loosely_equals(get_rule(right, entry.first), entry.second)) {
        return false;
      }
    }
    for(auto& entry : right.m_rules.get_entries()) {
      if(!loosely_equals(entry.second, get_rule(left, entry.first))) {
        return false;
      }
    }
    return true;
  }

  struct OmState {
    std::string m_class_name;
    StyleBlock* m_style_block = nullptr;
    bool m_is_filter_set = false;
    bool m_is_processed = false;
  };

  enum class TagKind : unsigned char {
    NORMAL,
    DEFS,
    STYLE
  };

  struct Tag {
    std::string m_name;
    std::string m_text;
    OrderedMap<JsValue> m_attributes;
    std::vector<Tag*> m_children;
    int m_id = 0;
    StyleBlock* m_style_block = nullptr;
    bool m_is_filter_set = false;
    bool m_has_global_filter = false;
    bool m_is_custom = false;
    bool m_is_artboard = false;
    bool m_is_in_defs = false;
    std::optional<Matrix4> m_transform;
    TagKind m_kind = TagKind::NORMAL;
  };

  JsValue get_attribute_entry(const Tag& tag, std::string_view name) {
    if(auto attribute = tag.m_attributes.find(name)) {
      return *attribute;
    }
    return JsValue();
  }

  int get_attribute_weight(std::string_view name) {
    static const auto weights = std::unordered_map<std::string_view, int>({
      {"id", -21}, {"data-name", -20}, {"class", -19}, {"xmlns", -18},
      {"xmlns:xlink", -17}, {"preserveAspectRatio", -16}, {"x", -15},
      {"y", -14}, {"x1", -13}, {"y1", -12}, {"x2", -11}, {"y2", -10},
      {"width", -9}, {"height", -8}, {"cx", -7}, {"cy", -6}, {"fx", -5},
      {"fy", -4}, {"r", -3}, {"rx", -2}, {"ry", -1}, {"viewBox", 10},
      {"xlink:href", 99}, {"style", 100}});
    if(auto weight = weights.find(name); weight != weights.end()) {
      return weight->second;
    }
    return 0;
  }

  struct Output {
    std::string m_text;
    std::string m_current_indent;
    std::string m_indent = "  ";
    std::string m_space = " ";
    std::string m_terminator = "\n";
    int m_precision = 3;
    bool m_is_detached = true;

    void write(std::string_view text) {
      m_text += text;
    }

    void write_line(std::string_view text = {}) {
      m_text += text;
      m_text += m_terminator;
    }

    void indent() {
      m_current_indent += m_indent;
    }

    void undent() {
      m_current_indent.resize(
        m_current_indent.size() - std::min(
          m_current_indent.size(), m_indent.size()));
    }
  };

  struct Definition {
    Tag* m_tag = nullptr;
    std::vector<std::function<void (Tag&)>> m_callbacks;
  };

  struct GradientLink {
    std::string m_id;
    std::string m_transform;
    double m_fx = NOT_A_NUMBER;
    double m_fy = NOT_A_NUMBER;
  };

  const auto DROP_SHADOW = std::string_view("dropShadow");
  const auto DROP_SHADOW_ONLY = std::string_view("dropShadowOnly");
  const auto INNER_SHADOW = std::string_view("uxdesign#innerShadow");
  const auto SVG_FILTER = std::string_view("svgFilter");

  std::string_view get_type(const XdValue* value) {
    if(!value) {
      return {};
    }
    if(auto type = value->find("type")) {
      return type->to_string();
    }
    return {};
  }

  bool has_member(const XdValue& value, std::string_view key) {
    return value.is_object() && value.find(key);
  }

  XdValue* find_object(XdValue& value, std::string_view key) {
    if(!value.is_object()) {
      return nullptr;
    }
    auto member = value.find(key);
    if(!member || !is_truthy(member)) {
      return nullptr;
    }
    return member;
  }

  const XdValue* find_object(const XdValue& value, std::string_view key) {
    if(!value.is_object()) {
      return nullptr;
    }
    auto member = value.find(key);
    if(!member || !is_truthy(member)) {
      return nullptr;
    }
    return member;
  }

  std::optional<std::string> get_name(const XdValue* value) {
    if(!value || !value->is_object()) {
      return std::nullopt;
    }
    auto name = value->find("name");
    if(!name || name->get_type() != XdValue::Type::STRING) {
      return std::nullopt;
    }
    return std::string(name->to_string());
  }

  XdValue make_white_fill() {
    auto fill = XdValue::make_object();
    fill.set("type", "solid");
    auto& color = fill.set("color", XdValue::make_object());
    color.set("mode", "RGB");
    color.set("alpha", 1);
    auto& value = color.set("value", XdValue::make_object());
    value.set("r", 255);
    value.set("g", 255);
    value.set("b", 255);
    return fill;
  }

  void merge(XdValue& target, const XdValue& source) {
    if(source.is_object()) {
      for(auto& member : source.get_members()) {
        if(target.is_object()) {
          if(auto existing = target.find(member.m_key)) {
            if((existing->is_object() || existing->is_array()) &&
                (member.m_value.is_object() || member.m_value.is_array())) {
              merge(*existing, member.m_value);
            }
          } else {
            target.set(member.m_key, member.m_value);
          }
        } else if(target.is_array()) {
          auto index = std::size_t(0);
          auto [end, error] = std::from_chars(member.m_key.data(),
            member.m_key.data() + member.m_key.size(), index);
          if(error == std::errc() &&
              end == member.m_key.data() + member.m_key.size()) {
            auto& elements = target.get_elements();
            if(index < elements.size()) {
              if((elements[index].is_object() ||
                  elements[index].is_array()) &&
                  (member.m_value.is_object() || member.m_value.is_array())) {
                merge(elements[index], member.m_value);
              }
            } else if(index == elements.size()) {
              elements.push_back(member.m_value);
            }
          }
        }
      }
    } else if(source.is_array()) {
      auto& sources = source.get_elements();
      for(auto i = std::size_t(0); i != sources.size(); ++i) {
        if(target.is_array()) {
          auto& elements = target.get_elements();
          if(i < elements.size()) {
            if((elements[i].is_object() || elements[i].is_array()) &&
                (sources[i].is_object() || sources[i].is_array())) {
              merge(elements[i], sources[i]);
            }
          } else {
            elements.push_back(sources[i]);
          }
        } else if(target.is_object()) {
          auto key = std::to_string(i);
          if(auto existing = target.find(key)) {
            if((existing->is_object() || existing->is_array()) &&
                (sources[i].is_object() || sources[i].is_array())) {
              merge(*existing, sources[i]);
            }
          } else {
            target.set(key, sources[i]);
          }
        }
      }
    }
  }

  std::string escape_css(std::string_view name) {
    auto text = to_utf16(name);
    auto replaced = std::u16string();
    auto is_run = false;
    for(auto c : text) {
      if(is_js_space(c)) {
        if(!is_run) {
          replaced += u'-';
        }
        is_run = true;
      } else {
        replaced += c;
        is_run = false;
      }
    }
    auto is_dash = !replaced.empty() && replaced.front() == u'-';
    auto out = std::u16string();
    for(auto i = std::size_t(0); i < replaced.size(); ++i) {
      auto code = replaced[i];
      auto is_number = code >= u'0' && code <= u'9';
      if(code >= 1 && code <= 31 || code == 127 || i == 0 && is_number ||
          i == 1 && is_dash && is_number) {
        auto hex = std::array<char, 8>();
        auto result = std::to_chars(hex.data(), hex.data() + hex.size(),
          static_cast<int>(code), 16);
        out += u'\\';
        for(auto p = hex.data(); p != result.ptr; ++p) {
          out += static_cast<char16_t>(*p);
        }
        out += u' ';
      } else if(code > 127 || code == u'-' || code == u'_' || is_number ||
          code >= u'a' && code <= u'z' || code >= u'A' && code <= u'Z') {
        out += code;
      } else {
        out += u'\\';
        out += code;
      }
    }
    return encode_text(to_utf8(out));
  }

  std::string quote_font_name(std::string_view name) {
    auto escaped = std::string();
    auto start = std::size_t(0);
    while(true) {
      auto space = name.find(' ', start);
      auto word = name.substr(start, space == std::string_view::npos ?
        std::string_view::npos : space - start);
      if(start != 0) {
        escaped += ' ';
      }
      escaped += escape_css(word);
      if(space == std::string_view::npos) {
        break;
      }
      start = space + 1;
    }
    if(escaped != name) {
      return "'" + escaped + "'";
    }
    return escaped;
  }

  std::string quote_font_family(std::string_view family) {
    auto text = std::string();
    for(auto c : family) {
      if(c != '"' && c != '\'') {
        text += c;
      }
    }
    auto out = std::string();
    auto i = std::size_t(0);
    auto start = std::size_t(0);
    while(i <= text.size()) {
      if(i == text.size()) {
        out += quote_font_name(std::string_view(text).substr(start));
        break;
      }
      if(text[i] == ',') {
        auto separator_start = i;
        while(separator_start > start &&
            is_space(text[separator_start - 1])) {
          --separator_start;
        }
        auto separator_end = i + 1;
        while(separator_end < text.size() && is_space(text[separator_end])) {
          ++separator_end;
        }
        out += quote_font_name(
          std::string_view(text).substr(start, separator_start - start));
        out += text.substr(separator_start, separator_end - separator_start);
        start = separator_end;
        i = separator_end;
        continue;
      }
      ++i;
    }
    return out;
  }

  const auto FONT_WEIGHTS = std::vector<std::pair<std::string_view, int>>({
    {"hairline", 100}, {"ultra-light", 100}, {"ultralight", 100},
    {"ultra-thin", 100}, {"ultrathin", 100}, {"extra-light", 200},
    {"extralight", 200}, {"thin", 200}, {"light", 300}, {"demi", 300},
    {"normal", 400}, {"regular", 400}, {"book", 400}, {"roman", 400},
    {"plain", 400}, {"medium", 500}, {"md", 500}, {"semibold", 600},
    {"demibold", 600}, {"demi-bold", 600}, {"bold", 700}, {"black", 800},
    {"heavy", 800}, {"heavyface", 800}, {"extra-bold", 800},
    {"extrabold", 800}, {"extra-black", 900}, {"extrablack", 900},
    {"fat", 900}, {"poster", 900}, {"ultra-black", 900},
    {"ultrablack", 900}, {"100", 100}, {"200", 200}, {"300", 300},
    {"400", 400}, {"500", 500}, {"600", 600}, {"700", 700}, {"800", 800},
    {"900", 900}});

  const auto FONT_ITALICS =
    std::vector<std::pair<std::string_view, std::string_view>>({
      {"italic", "italic"}, {"ital", "italic"}, {"it", "italic"},
      {"kursiv", "italic"}, {"oblique", "oblique"}, {"obl", "oblique"},
      {"slanted", "oblique"}});

  template<typename T>
  const std::pair<std::string_view, T>* find_leftmost(std::string_view text,
      const std::vector<std::pair<std::string_view, T>>& alternatives) {
    for(auto position = std::size_t(0); position <= text.size(); ++position) {
      for(auto& alternative : alternatives) {
        if(text.substr(position).starts_with(alternative.first)) {
          return &alternative;
        }
      }
    }
    return nullptr;
  }

  std::string to_font_style_key(std::string_view style) {
    auto text = to_utf16(to_lower(style));
    for(auto i = std::size_t(0); i < text.size(); ++i) {
      if(is_js_space(text[i])) {
        auto j = i;
        while(j < text.size() && is_js_space(text[j])) {
          ++j;
        }
        text.erase(i, j - i);
        break;
      }
    }
    return to_utf8(text);
  }

  const std::unordered_map<std::string_view, std::string_view>&
      get_color_names() {
    static const auto names =
        std::unordered_map<std::string_view, std::string_view>({
      {"#fa8072", "salmon"}, {"#ff0000", "red"}, {"#ffc0cb", "pink"},
      {"#ff7f50", "coral"}, {"#ff6347", "tomato"}, {"#ffa500", "orange"},
      {"#ffd700", "gold"}, {"#f0e68c", "khaki"}, {"#dda0dd", "plum"},
      {"#ee82ee", "violet"}, {"#da70d6", "orchid"}, {"#800080", "purple"},
      {"#4b0082", "indigo"}, {"#00ff00", "lime"}, {"#008000", "green"},
      {"#808000", "olive"}, {"#008080", "teal"}, {"#00ffff", "aqua"},
      {"#0000ff", "blue"}, {"#000080", "navy"}, {"#ffe4c4", "bisque"},
      {"#f5deb3", "wheat"}, {"#d2b48c", "tan"}, {"#cd853f", "peru"},
      {"#a0522d", "sienna"}, {"#a52a2a", "brown"}, {"#800000", "maroon"},
      {"#fffafa", "snow"}, {"#f0ffff", "azure"}, {"#f5f5dc", "beige"},
      {"#fffff0", "ivory"}, {"#faf0e6", "linen"}, {"#c0c0c0", "silver"},
      {"#808080", "gray"}});
    return names;
  }

  std::string to_hex_component(double value) {
    auto rounded = js_round(value);
    if(!std::isfinite(rounded)) {
      return to_js_string(rounded);
    }
    auto is_negative = rounded < 0;
    auto magnitude = static_cast<long long>(std::abs(rounded));
    auto buffer = std::array<char, 32>();
    auto result = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), magnitude, 16);
    auto hex = std::string(buffer.data(), result.ptr);
    if(is_negative) {
      hex = "-" + hex;
    }
    if(hex.size() == 1) {
      return "0" + hex;
    }
    return hex;
  }

  std::string get_scale_behavior(const XdValue* behavior) {
    auto key = std::string("undefined");
    if(behavior) {
      key = to_js_string(to_js_value(behavior));
    }
    if(key == "fill" || key == "none") {
      return "none";
    } else if(key == "contain") {
      return "xMidYMid";
    } else if(key == "cover" || key == "undefined") {
      return "xMidYMid slice";
    }
    return "";
  }

  using Attributes = std::vector<std::pair<std::string, JsValue>>;

  JsValue optional_number(double value) {
    return value;
  }

  JsValue to_attribute(const XdValue* value) {
    return to_js_value(value);
  }

  class Writer {
    public:
      Writer(XdValue& document, const SvgWriterOptions& options)
          : m_document(&document),
            m_options(&options),
            m_styling(static_cast<int>(options.m_styling)),
            m_is_minified(options.m_is_minified),
            m_precision(options.m_precision),
            m_ids(options.m_is_minified),
            m_current(&document) {
        m_output.m_is_detached = false;
        m_output.m_precision = m_precision;
        if(m_is_minified) {
          m_output.m_indent.clear();
          m_output.m_space.clear();
          m_output.m_terminator.clear();
        } else if(options.m_has_carriage_returns) {
          m_output.m_terminator = "\r\n";
        }
        if(auto source = find_object(document, "viewSource")) {
          m_left = get_number(*source, "x");
          m_top = get_number(*source, "y");
          m_right = get_number(*source, "x") + get_number(*source, "width");
          m_bottom =
            get_number(*source, "y") + get_number(*source, "height");
        }
      }

      std::string write() {
        process_document();
        auto svg = make(*m_document, 0);
        auto has_class_rules = m_styling == 0 && has_stylesheet_rules();
        auto has_definitions = !m_definitions.get_entries().empty();
        if(has_class_rules || has_definitions) {
          svg->m_children.insert(svg->m_children.begin(), &make_defs());
        }
        post_process(*svg);
        if(!has_definitions && !m_is_xlink_required) {
          svg->m_attributes.erase("xmlns:xlink");
        }
        write_tag(*svg, m_output);
        return std::move(m_output.m_text);
      }

    private:
      XdValue* m_document;
      const SvgWriterOptions* m_options;
      int m_styling;
      bool m_is_minified;
      int m_precision;
      Output m_output;
      IdGenerator m_ids;
      XdValue* m_current;
      double m_left = NOT_A_NUMBER;
      double m_top = NOT_A_NUMBER;
      double m_right = NOT_A_NUMBER;
      double m_bottom = NOT_A_NUMBER;
      double m_x = NOT_A_NUMBER;
      double m_y = NOT_A_NUMBER;
      double m_width = NOT_A_NUMBER;
      double m_height = NOT_A_NUMBER;
      std::unordered_map<const XdValue*, OmState> m_states;
      std::deque<OmState> m_fake_states;
      std::deque<Tag> m_tags;
      std::deque<StyleBlock> m_style_blocks;
      std::unordered_map<std::string, Tag*> m_dom_ids;
      std::unordered_map<std::string, Tag*> m_images;
      OrderedMap<StyleBlock*> m_blocks;
      OrderedMap<Definition> m_definitions;
      std::unordered_map<std::string, GradientLink> m_gradient_stops;
      int m_class_count = 1;
      bool m_is_xlink_required = false;
      int m_inside_clip_path = 0;

      bool equals(double left, double right) const {
        return round(left) == round(right);
      }

      double round(double value) const {
        if(!std::isfinite(value)) {
          return value;
        }
        auto ten = std::pow(10.0, m_precision);
        return js_round(value * ten) / ten;
      }

      OmState& get_state(const XdValue& node) {
        return m_states[&node];
      }

      OmState& make_fake_state() {
        return m_fake_states.emplace_back();
      }

      StyleBlock& get_style_block(OmState& state) {
        if(state.m_class_name.empty()) {
          state.m_class_name = "cls-" + std::to_string(m_class_count++);
        }
        if(auto block = m_blocks.find(state.m_class_name)) {
          state.m_style_block = *block;
        } else if(!state.m_style_block) {
          auto& created = m_style_blocks.emplace_back();
          created.m_classes = {state.m_class_name};
          state.m_style_block = &created;
        }
        m_blocks.set(state.m_class_name, state.m_style_block);
        return *state.m_style_block;
      }

      StyleBlock& make_block(std::vector<std::string> classes) {
        auto& block = m_style_blocks.emplace_back();
        block.m_classes = std::move(classes);
        return block;
      }

      Tag& create_tag(std::string name, const Attributes& attributes = {}) {
        auto& tag = m_tags.emplace_back();
        tag.m_name = std::move(name);
        tag.m_id = static_cast<int>(m_tags.size()) - 1;
        for(auto& attribute : attributes) {
          set_attribute(tag, attribute.first, attribute.second);
        }
        return tag;
      }

      Tag& create_tag(
          std::string name, const Attributes& attributes, OmState& state) {
        auto& tag = create_tag(std::move(name), attributes);
        tag.m_is_filter_set = state.m_is_filter_set;
        set_style_block(tag, state);
        return tag;
      }

      Tag& create_text(std::string text) {
        auto& tag = m_tags.emplace_back();
        tag.m_name = "#text";
        tag.m_text = std::move(text);
        tag.m_id = static_cast<int>(m_tags.size()) - 1;
        return tag;
      }

      void set_style_block(Tag& tag, OmState& state) {
        auto& block = get_style_block(state);
        tag.m_style_block = &block;
        block.m_tags.push_back(tag.m_id);
      }

      void set_attribute(Tag& tag, std::string_view name, JsValue value) {
        if(is_undefined(value) || std::holds_alternative<std::string>(value) &&
            std::get<std::string>(value).empty()) {
          if(name == "id") {
            m_dom_ids.erase(to_js_string(get_attribute_entry(tag, "id")));
          }
          tag.m_attributes.erase(name);
          return;
        }
        if(name == "id") {
          m_dom_ids[to_js_string(value)] = &tag;
        }
        if(name == "#text") {
          tag.m_text = to_js_string(value);
        } else {
          tag.m_attributes.set(name, std::move(value));
        }
      }

      JsValue get_attribute(const Tag& tag, std::string_view name) const {
        if(tag.m_style_block) {
          auto rule = get_rule(*tag.m_style_block, name);
          if(is_truthy(rule)) {
            return rule;
          }
        }
        auto attribute = get_attribute_entry(tag, name);
        if(is_truthy(attribute)) {
          return attribute;
        }
        return std::string();
      }

      Tag* get_by_dom_id(std::string id) {
        if(!id.empty() && id.front() == '#') {
          id.erase(0, 1);
        }
        if(auto tag = m_dom_ids.find(id); tag != m_dom_ids.end()) {
          return tag->second;
        }
        return nullptr;
      }

      std::string write_attribute(Tag& tag, Output* output, std::string name,
          JsValue value = JsValue()) {
        if(is_undefined(value)) {
          value = get_attribute_entry(tag, name);
        }
        if(!is_undefined(value)) {
          value = get_attribute_value(tag.m_name, name, value,
            output ? std::optional(output->m_precision) : std::nullopt);
        }
        auto default_value = get_descriptor(tag.m_name, name).m_default;
        auto is_written =
          !is_undefined(value) && to_js_string(value) != default_value;
        if(!name.empty() && name.back() == '!') {
          name.pop_back();
        }
        if((tag.m_name == "linearGradient" ||
            tag.m_name == "radialGradient" || tag.m_name == "filter" ||
            tag.m_name == "pattern") &&
            is_truthy(get_attribute_entry(tag, "xlink:href"))) {
          auto link = &tag;
          while(is_truthy(get_attribute_entry(*link, "xlink:href"))) {
            link = get_by_dom_id(
              to_js_string(get_attribute_entry(*link, "xlink:href")));
            if(link && link->m_name == tag.m_name) {
              if(auto linked = link->m_attributes.find(name)) {
                is_written = !loosely_equals(*linked, value);
                break;
              }
            } else {
              is_written = true;
              break;
            }
          }
        }
        if(is_written) {
          auto text = to_js_string(value);
          if(name == "data-name") {
            text = encode_text(text);
          }
          if(tag.m_name == "image" && name == "xlink:href" &&
              text.substr(0, 5) != "data:") {
            text = encode_uri(text);
          }
          auto out = " " + name + "=\"" + text + "\"";
          if(output) {
            output->write(out);
            return "";
          }
          return out;
        }
        return "";
      }

      void set_class(Tag& tag) {
        auto block = tag.m_style_block;
        if(!block || !has_rules(*block)) {
          return;
        }
        if(m_styling == 0) {
          auto style = std::string();
          for(auto& name : block->m_classes) {
            style += ' ';
            auto replaced = std::string();
            auto is_run = false;
            for(auto c : name) {
              if(is_space(c)) {
                if(!is_run) {
                  replaced += '-';
                }
                is_run = true;
              } else {
                replaced += c;
                is_run = false;
              }
            }
            style += replaced;
          }
          set_attribute(tag, "class", encode_text(style.substr(1)));
          return;
        }
        if(m_styling == 1) {
          auto style = std::string();
          for(auto& entry : block->m_rules.get_entries()) {
            if(!is_default(*block, entry.first)) {
              style += entry.first + ":" + m_output.m_space +
                to_js_string(entry.second) + ";";
            }
          }
          if(!style.empty()) {
            style.pop_back();
          }
          set_attribute(tag, "style", style);
          return;
        }
        auto style = std::string();
        auto entries = block->m_rules.get_entries();
        for(auto& entry : entries) {
          if(entry.first == "isolation" || entry.first == "mix-blend-mode" ||
              entry.first == "text-orientation" ||
              entry.first == "white-space") {
            style += entry.first + ":" + m_output.m_space +
              to_js_string(entry.second) + ";";
            continue;
          }
          set_attribute(tag, entry.first, entry.second);
        }
        if(!style.empty()) {
          style.pop_back();
          set_attribute(tag, "style", style);
        }
      }

      std::string to_string(Tag& tag) {
        auto output = Output();
        write_tag(tag, output);
        return output.m_text;
      }

      void write_tag(Tag& tag, Output& output) {
        if(tag.m_kind == TagKind::STYLE) {
          if(!output.m_is_detached) {
            write_sheet(output);
          }
          return;
        }
        if(tag.m_kind == TagKind::DEFS && !output.m_is_detached) {
          auto has_class_rules = m_styling == 0 && has_stylesheet_rules();
          if(!has_class_rules && m_definitions.get_entries().empty()) {
            return;
          }
        }
        auto is_detached = output.m_is_detached;
        if(!is_detached) {
          set_class(tag);
        }
        auto child_count = tag.m_children.size();
        if(!tag.m_name.empty()) {
          if(tag.m_name == "#text") {
            output.write(encode_text(tag.m_text));
            return;
          }
          if(tag.m_name == "#comment") {
            output.write_line(output.m_current_indent + "<!-- " +
              encode_text(tag.m_text) + " -->");
            return;
          }
          auto indent = output.m_current_indent;
          if(tag.m_name == "tspan" || tag.m_name == "textPath") {
            indent.clear();
          }
          output.write(indent + "<" + tag.m_name);
          auto keys = tag.m_attributes.get_keys();
          std::stable_sort(keys.begin(), keys.end(),
            [] (const auto& left, const auto& right) {
              return get_attribute_weight(left) < get_attribute_weight(right);
            });
          for(auto& key : keys) {
            if(!is_detached || key != "id" && key != "data-name") {
              write_attribute(tag, &output, key);
            }
          }
          if(is_detached && tag.m_style_block) {
            auto entries = tag.m_style_block->m_rules.get_entries();
            for(auto& entry : entries) {
              write_attribute(tag, &output, entry.first, entry.second);
            }
          }
          if(child_count == 0 && tag.m_name != "script") {
            output.write("/");
          }
          if(tag.m_name == "text" || tag.m_name == "tspan" ||
              tag.m_name == "textPath" || tag.m_name == "desc" ||
              tag.m_name == "title") {
            output.write(">");
          } else {
            output.write_line(">");
            if(child_count != 0) {
              output.indent();
            }
          }
        }
        for(auto i = std::size_t(0); i != child_count; ++i) {
          if(is_detached) {
            output.write(to_string(*tag.m_children[i]));
          } else {
            write_tag(*tag.m_children[i], output);
          }
        }
        if(child_count == 0 && tag.m_name != "script" || tag.m_name.empty()) {
          return;
        }
        if(tag.m_name == "text" || tag.m_name == "desc" ||
            tag.m_name == "title") {
          output.write_line("</" + tag.m_name + ">");
        } else if(tag.m_name == "tspan" || tag.m_name == "textPath") {
          output.write("</" + tag.m_name + ">");
        } else {
          output.undent();
          output.write_line(output.m_current_indent + "</" + tag.m_name + ">");
        }
      }

      void append_child(Tag& parent, Tag* child) {
        if(!child->m_name.empty()) {
          parent.m_children.push_back(child);
        } else {
          parent.m_children.insert(parent.m_children.end(),
            child->m_children.begin(), child->m_children.end());
        }
      }

      void define(Tag& tag, std::function<void (Tag&)> callback = {},
          std::optional<std::string> fingerprint = std::nullopt) {
        if(!fingerprint) {
          auto id = get_attribute(tag, "id");
          auto name = get_attribute(tag, "data-name");
          set_attribute(tag, "id", std::string());
          set_attribute(tag, "data-name", std::string());
          fingerprint = to_string(tag);
          set_attribute(tag, "id", id);
          set_attribute(tag, "data-name", name);
        }
        if(auto definition = m_definitions.find(*fingerprint)) {
          if(callback) {
            definition->m_callbacks.push_back(std::move(callback));
          }
        } else {
          auto entry = Definition();
          entry.m_tag = &tag;
          if(callback) {
            entry.m_callbacks.push_back(std::move(callback));
          }
          m_definitions.set(*fingerprint, std::move(entry));
        }
      }

      bool has_stylesheet_rules() const {
        for(auto& entry : m_blocks.get_entries()) {
          if(entry.second && has_rules(*entry.second)) {
            return true;
          }
        }
        return false;
      }

      Tag& make_defs() {
        auto& defs = create_tag("defs");
        defs.m_kind = TagKind::DEFS;
        auto& style = create_tag("style");
        style.m_kind = TagKind::STYLE;
        defs.m_children.push_back(&style);
        m_gradient_stops.clear();
        for(auto i = std::size_t(0);
            i < m_definitions.get_entries().size(); ++i) {
          auto tag = m_definitions.get_entries()[i].second.m_tag;
          defs.m_children.push_back(tag);
          auto callbacks = m_definitions.get_entries()[i].second.m_callbacks;
          for(auto& callback : callbacks) {
            callback(*tag);
          }
        }
        return defs;
      }

      void write_block(const StyleBlock& block, Output& output) {
        auto selector = std::string();
        for(auto i = std::size_t(0); i != block.m_classes.size(); ++i) {
          if(i != 0) {
            selector += "," + output.m_space + ".";
          }
          selector += escape_css(block.m_classes[i]);
        }
        output.write_line(output.m_current_indent + "." + selector +
          output.m_space + "{");
        output.indent();
        for(auto& entry : block.m_rules.get_entries()) {
          auto name = entry.first;
          if(!name.empty() && name.back() == '!') {
            name.pop_back();
          }
          if(!is_default(block, name)) {
            auto value = get_attribute_value(
              "*", name, entry.second, output.m_precision);
            output.write_line(output.m_current_indent + name + ":" +
              output.m_space + to_js_string(value) + ";");
          }
        }
        output.undent();
        output.write_line(output.m_current_indent + "}");
      }

      std::vector<StyleBlock*> extract(const std::vector<StyleBlock*>& blocks) {
        struct Rule {
          std::string m_name;
          JsValue m_value;
          std::vector<std::string> m_classes;
        };
        auto rules = OrderedMap<Rule>();
        for(auto block : blocks) {
          for(auto& entry : block->m_rules.get_entries()) {
            if(!is_default(*block, entry.first)) {
              auto name = entry.first + ":" + to_js_string(entry.second);
              auto rule = rules.find(name);
              if(!rule) {
                auto created = Rule();
                created.m_name = entry.first;
                created.m_value = entry.second;
                rule = &rules.set(name, std::move(created));
              }
              rule->m_classes.insert(rule->m_classes.end(),
                block->m_classes.begin(), block->m_classes.end());
            }
          }
        }
        auto result = std::vector<StyleBlock*>();
        auto by_class = std::unordered_map<std::string, StyleBlock*>();
        for(auto& entry : rules.get_entries()) {
          auto& rule = entry.second;
          auto classes = rule.m_classes;
          std::sort(classes.begin(), classes.end(),
            [] (const auto& left, const auto& right) {
              return to_utf16(left) < to_utf16(right);
            });
          auto key = std::string();
          for(auto i = std::size_t(0); i != classes.size(); ++i) {
            if(i != 0) {
              key += ',';
            }
            key += classes[i];
          }
          if(auto block = by_class.find(key); block != by_class.end()) {
            add_rule(*block->second, rule.m_name, rule.m_value);
          } else {
            auto& created = make_block(classes);
            add_rule(created, rule.m_name, rule.m_value);
            result.push_back(&created);
            by_class[key] = &created;
          }
        }
        return result;
      }

      void write_sheet(Output& output) {
        if(m_styling != 0) {
          return;
        }
        auto blocks = std::vector<StyleBlock*>();
        for(auto& entry : m_blocks.get_entries()) {
          if(entry.second && has_rules(*entry.second) &&
              !entry.second->m_tags.empty()) {
            blocks.push_back(entry.second);
          }
        }
        if(blocks.empty()) {
          return;
        }
        output.write_line(output.m_current_indent + "<style>");
        output.indent();
        blocks = extract(blocks);
        for(auto i = std::size_t(0); i != blocks.size(); ++i) {
          if(i != 0) {
            output.write_line();
          }
          write_block(*blocks[i], output);
        }
        output.undent();
        output.write_line(output.m_current_indent + "</style>");
      }

      XdValue* get_children(XdValue& node) {
        auto container = &node;
        if(auto artboard = node.is_object() ? node.find("artboard") : nullptr;
            is_truthy(artboard)) {
          container = artboard;
        } else if(auto group = node.is_object() ? node.find("group") :
            nullptr; is_truthy(group)) {
          container = group;
        }
        if(!container->is_object()) {
          return nullptr;
        }
        auto children = container->find("children");
        if(!children || !children->is_array()) {
          return nullptr;
        }
        return children;
      }

      bool is_visible(const XdValue& node) const {
        if(&node == m_document) {
          return true;
        }
        auto visible = node.is_object() ? node.find("visible") : nullptr;
        return !visible || is_truthy(visible);
      }

      static bool is_filter_visible(const XdValue* filter) {
        if(!is_truthy(filter)) {
          return false;
        }
        auto visible = filter->find("visible");
        return !visible || is_truthy(visible);
      }

      static std::string_view get_first_applicable_filter(
          const XdValue& node) {
        auto style = find_object(node, "style");
        auto filters = style ? find_object(*style, "filters") : nullptr;
        if(!filters || !filters->is_array()) {
          return {};
        }
        for(auto& filter : filters->get_elements()) {
          auto params = find_object(filter, "params");
          auto visible = filter.find("visible");
          if(!params || visible &&
              visible->get_type() == XdValue::Type::BOOL &&
              !visible->to_bool()) {
            continue;
          }
          auto type = get_type(&filter);
          if(type == DROP_SHADOW) {
            if(!find_object(*params, "dropShadows")) {
              continue;
            }
          } else if(type == INNER_SHADOW) {
            if(!find_object(*params, "innerShadows")) {
              continue;
            }
          } else if(type != SVG_FILTER) {
            continue;
          }
          return type;
        }
        return {};
      }

      static bool has_fill(const XdValue& node) {
        if(get_type(&node) == "image") {
          return true;
        }
        auto style = find_object(node, "style");
        auto fill = style ? find_object(*style, "fill") : nullptr;
        return fill && get_type(fill) != "none";
      }

      static bool has_stroke(const XdValue& node) {
        auto style = find_object(node, "style");
        auto stroke = style ? find_object(*style, "stroke") : nullptr;
        return stroke && get_type(stroke) != "none";
      }

      static void set_property(
          XdValue& node, std::string_view property, const XdValue& value) {
        node.find("style")->set(property, value);
        auto text = find_object(node, "text");
        auto paragraphs = text ? find_object(*text, "paragraphs") : nullptr;
        if(!paragraphs || !paragraphs->is_array()) {
          return;
        }
        for(auto& paragraph : paragraphs->get_elements()) {
          if(auto lines = find_object(paragraph, "lines")) {
            for(auto& line : lines->get_elements()) {
              for(auto& run : line.get_elements()) {
                if(auto style = find_object(run, "style")) {
                  style->set(property, value);
                }
              }
            }
          }
          if(auto style = find_object(paragraph, "style")) {
            style->set(property, value);
          }
        }
      }

      static double get_text_alpha(const XdValue& node) {
        auto paragraphs = find_path(&node, {"text", "paragraphs"});
        if(!paragraphs || !paragraphs->is_array() ||
            paragraphs->get_elements().empty()) {
          return 1;
        }
        auto lines = find_path(&paragraphs->get_elements()[0], {"lines"});
        if(!lines || !lines->is_array() || lines->get_elements().empty() ||
            lines->get_elements()[0].get_elements().empty()) {
          return 1;
        }
        auto& run = lines->get_elements()[0].get_elements()[0];
        auto style = find_object(run, "style");
        auto fill = style ? find_object(*style, "fill") : nullptr;
        auto color = fill ? find_object(*fill, "color") : nullptr;
        if(color) {
          if(auto alpha = color->find("alpha")) {
            return get_number(alpha);
          }
          return 1;
        }
        return 1;
      }

      bool replace_with_inner_shadow(XdValue& slot) {
        auto& node = slot;
        auto is_filled = has_fill(node);
        auto is_stroked = has_stroke(node);
        auto& filters = node.find("style")->find("filters")->get_elements();
        auto inner = std::optional<std::size_t>();
        auto drop = std::optional<std::size_t>();
        for(auto i = std::size_t(0); i != filters.size(); ++i) {
          auto params = find_object(filters[i], "params");
          if(!inner && get_type(&filters[i]) == INNER_SHADOW && params &&
              find_object(*params, "innerShadows")) {
            inner = i;
          }
          if(!drop && get_type(&filters[i]) == DROP_SHADOW && params &&
              find_object(*params, "dropShadows")) {
            drop = i;
          }
        }
        auto drop_filter = drop ? std::optional(filters[*drop]) : std::nullopt;
        auto inner_filter = inner ? filters[*inner] : XdValue();
        auto is_drop_visible = drop_filter && is_filter_visible(&*drop_filter);
        auto allows_drop_only = m_options->m_has_drop_shadow_only;
        if(!is_filled && !is_stroked &&
            (!drop_filter || !allows_drop_only || !is_drop_visible)) {
          set_property(node, "fill", make_white_fill());
          auto list = XdValue::make_array();
          list.append(inner_filter);
          node.find("style")->set("filters", std::move(list));
          return false;
        }
        auto group = XdValue::make_object();
        group.set("type", "group");
        group.set("innerShadowGroup", true);
        auto children = XdValue::make_array();
        auto set_filters = [] (XdValue& target, const XdValue& filter) {
          auto list = XdValue::make_array();
          list.append(filter);
          target.find("style")->set("filters", std::move(list));
        };
        if(is_stroked && drop_filter && is_drop_visible) {
          auto copy = node;
          drop_filter->set("type", DROP_SHADOW_ONLY);
          filters[*drop].set("type", DROP_SHADOW_ONLY);
          set_filters(copy, *drop_filter);
          children.append(std::move(copy));
        }
        if(is_filled) {
          auto copy = node;
          if(drop_filter && !is_stroked) {
            set_filters(copy, *drop_filter);
          } else {
            copy.find("style")->erase("filters");
          }
          set_property(copy, "stroke", XdValue());
          children.append(std::move(copy));
          auto fill = find_object(*node.find("style"), "fill");
          if(fill && get_type(fill) == "solid") {
            auto shadows = inner_filter.find("params")->find("innerShadows");
            auto& shadow = shadows->get_elements()[0];
            auto color = shadow.find("color");
            auto shadow_alpha = 1.0;
            if(auto alpha = color->find("alpha")) {
              shadow_alpha = get_number(alpha);
            }
            auto fill_alpha = 1.0;
            if(find_object(node, "text")) {
              fill_alpha = get_text_alpha(node);
            } else if(auto alpha = fill->find("color") ?
                fill->find("color")->find("alpha") : nullptr) {
              fill_alpha = get_number(alpha);
            }
            color->set("alpha", shadow_alpha * fill_alpha);
          }
        } else if(allows_drop_only && drop_filter && is_drop_visible &&
            !is_stroked) {
          auto copy = node;
          drop_filter->set("type", DROP_SHADOW_ONLY);
          set_filters(copy, *drop_filter);
          set_property(copy, "fill", make_white_fill());
          set_property(copy, "stroke", XdValue());
          children.append(std::move(copy));
        }
        set_property(node, "fill", make_white_fill());
        set_filters(node, inner_filter);
        auto original = children.get_elements().size();
        children.append(std::move(node));
        if(is_stroked) {
          auto copy = children.get_elements()[original];
          copy.find("style")->erase("filters");
          set_property(copy, "fill", XdValue());
          children.append(std::move(copy));
          set_property(
            children.get_elements()[original], "stroke", XdValue());
        }
        group.set("group", XdValue::make_object()).set(
          "children", std::move(children));
        slot = std::move(group);
        m_inner_shadow_original = original;
        return true;
      }

      std::size_t m_inner_shadow_original = 0;

      void update_for_drop_shadow(XdValue& node) {
        if(has_fill(node) || has_stroke(node)) {
          return;
        }
        node.find("style")->set("fill", make_white_fill());
        auto filters = node.find("style")->find("filters");
        if(!filters || !filters->is_array()) {
          return;
        }
        for(auto& filter : filters->get_elements()) {
          auto params = find_object(filter, "params");
          if(get_type(&filter) == DROP_SHADOW && params &&
              find_object(*params, "dropShadows")) {
            filter.set("type", DROP_SHADOW_ONLY);
            return;
          }
        }
      }

      void preprocess(XdValue& slot, bool has_parent) {
        if(!is_visible(slot)) {
          return;
        }
        auto node = &slot;
        auto first = get_first_applicable_filter(slot);
        auto style = find_object(slot, "style");
        auto filters = style ? find_object(*style, "filters") : nullptr;
        auto has_visible_inner = false;
        if(filters && filters->is_array()) {
          for(auto& filter : filters->get_elements()) {
            auto params = find_object(filter, "params");
            if(get_type(&filter) == INNER_SHADOW && params &&
                find_object(*params, "innerShadows") &&
                is_filter_visible(&filter)) {
              has_visible_inner = true;
            }
          }
        }
        if(first == INNER_SHADOW ||
            first == DROP_SHADOW && has_visible_inner) {
          if(has_parent) {
            if(replace_with_inner_shadow(slot)) {
              node = &slot.find("group")->find("children")->get_elements()[
                m_inner_shadow_original];
            }
          } else {
            auto copy = slot;
            replace_with_inner_shadow(copy);
          }
        } else if(first == DROP_SHADOW &&
            m_options->m_has_drop_shadow_only) {
          update_for_drop_shadow(slot);
        }
        if(auto children = get_children(*node)) {
          for(auto& child : children->get_elements()) {
            preprocess(child, true);
          }
        }
      }

      std::string write_color(const XdValue* value, bool has_opacity) {
        auto color = std::string();
        if(!is_truthy(value)) {
          color = "transparent";
        } else if(value->get_type() == XdValue::Type::STRING) {
          color = std::string(value->to_string());
        } else if(value->is_object()) {
          auto rgb = value->find("value");
          if(!is_truthy(rgb)) {
            rgb = value;
          }
          auto alpha = get_number(value->find("alpha"));
          if(has_opacity && value->find("alpha") && std::isfinite(alpha) &&
              !equals(alpha, 1)) {
            return "rgba(" + to_js_string(std::ceil(get_number(*rgb, "r"))) +
              "," + to_js_string(std::ceil(get_number(*rgb, "g"))) + "," +
              to_js_string(std::ceil(get_number(*rgb, "b"))) + "," +
              to_js_string(round2(alpha)) + ")";
          }
          color = "#" + to_hex_component(get_number(*rgb, "r")) +
            to_hex_component(get_number(*rgb, "g")) +
            to_hex_component(get_number(*rgb, "b"));
        } else {
          color = to_js_string(to_js_value(value));
        }
        auto& names = get_color_names();
        if(auto name = names.find(to_lower(color)); name != names.end()) {
          return std::string(name->second);
        }
        if(color.size() == 7 && color[0] == '#' && color[1] == color[2] &&
            color[3] == color[4] && color[5] == color[6]) {
          return std::string("#") + color[1] + color[3] + color[5];
        }
        return color;
      }

      void write_gradient(XdValue& node, StyleBlock& block,
          const XdValue& reference, std::string flavor) {
        auto resources = find_path(m_document, {"resources", "gradients"});
        auto key = to_js_string(get_value(reference, "ref"));
        auto gradient = resources && resources->is_object() ?
          const_cast<XdValue*>(resources->find(key)) : nullptr;
        if(!is_truthy(gradient)) {
          return;
        }
        auto type = std::string(get_type(gradient));
        auto name = get_name(gradient);
        if(name && (name->empty() || name->substr(0, 7) == "Unnamed")) {
          name = std::nullopt;
        }
        auto id = m_ids.get_unique(type + "-gradient", name);
        auto units = reference.find("units");
        auto space = is_truthy(units) ? to_js_string(to_js_value(units)) :
          std::string("userSpaceOnUse");
        auto saved = m_current;
        m_current = gradient;
        auto& tag = create_tag(
          type + "Gradient", {{"id", id}}, get_state(*gradient));
        m_current = saved;
        if(!m_is_minified && name && id != *name) {
          set_attribute(tag, "data-name", *name);
        }
        auto stops = gradient->find("stops");
        if(!is_truthy(stops)) {
          return;
        }
        auto lines = std::vector<Tag*>();
        for(auto& stop : stops->get_elements()) {
          auto color = XdValue();
          auto alpha = 1.0;
          if(auto source = stop.find("color")) {
            color = *source;
            if(color.is_object()) {
              color.erase("alpha");
            }
            auto value = get_number(source->find("alpha"));
            if(source->find("alpha") && std::isfinite(value)) {
              alpha = value;
            }
          }
          lines.push_back(&create_tag("stop", {
            {"offset", to_js_value(stop.find("offset"))},
            {"stop-color", write_color(&color, true)},
            {"stop-opacity", alpha}}));
        }
        auto lines_key = std::string();
        for(auto i = std::size_t(0); i != lines.size(); ++i) {
          if(i != 0) {
            lines_key += ',';
          }
          lines_key += to_string(*lines[i]);
        }
        auto link = std::optional<GradientLink>();
        if(auto found = m_gradient_stops.find(lines_key);
            found != m_gradient_stops.end()) {
          link = found->second;
        }
        auto transform = get_transform(
          get_node_transform(reference), m_precision);
        auto remove_duplicates = [&] {
          auto out = std::vector<Tag*>();
          if(lines.empty()) {
            return out;
          }
          out.push_back(lines[0]);
          for(auto i = std::size_t(1); i < lines.size(); ++i) {
            if(to_string(*lines[i - 1]) != to_string(*lines[i])) {
              out.push_back(lines[i]);
            }
          }
          return out;
        };
        if(type == "linear") {
          auto attributes = Attributes({
            {"x1", get_number(reference, "x1")},
            {"y1", get_number(reference, "y1")},
            {"x2", get_number(reference, "x2")},
            {"y2", get_number(reference, "y2")},
            {"gradientTransform", transform}});
          if(link) {
            if(!link->m_transform.empty() && transform.empty()) {
              attributes[4].second = std::string("matrix(1, 0, 0, 1, 0, 0)");
            }
            attributes.emplace_back("xlink:href", "#" + link->m_id);
            for(auto& attribute : attributes) {
              set_attribute(tag, attribute.first, attribute.second);
            }
          } else {
            m_gradient_stops[lines_key] = GradientLink(id, transform);
            attributes.emplace_back("gradientUnits", space);
            for(auto& attribute : attributes) {
              set_attribute(tag, attribute.first, attribute.second);
            }
            tag.m_children = remove_duplicates();
          }
        } else {
          auto cx = get_number(reference, "cx");
          auto cy = get_number(reference, "cy");
          auto fx = is_truthy(reference.find("fx")) ?
            get_number(reference, "fx") : cx;
          auto fy = is_truthy(reference.find("fy")) ?
            get_number(reference, "fy") : cy;
          auto r = get_value(reference, "r");
          auto attribute_fx = std::optional<double>();
          auto attribute_fy = std::optional<double>();
          if(std::isfinite(fx) && !equals(fx, cx)) {
            attribute_fx = fx;
          }
          if(std::isfinite(fy) && !equals(fy, cy)) {
            attribute_fy = fy;
          }
          auto delta_x = attribute_fx.value_or(NOT_A_NUMBER) - cx;
          auto delta_y = attribute_fy.value_or(NOT_A_NUMBER) -
            attribute_fy.value_or(NOT_A_NUMBER);
          auto maximum = 0.99 * to_js_number(r);
          if(std::sqrt(delta_x * delta_x + delta_y * delta_y) > maximum) {
            auto angle = std::atan(delta_y);
            delta_x = std::cos(angle) * maximum;
            delta_y = std::sin(angle) * maximum;
            attribute_fx = delta_x + cx;
            attribute_fy = delta_y + cy;
          }
          if(link) {
            if(std::isfinite(link->m_fx) &&
                !std::isfinite(attribute_fx.value_or(NOT_A_NUMBER)) &&
                std::isfinite(cx)) {
              attribute_fx = cx;
            }
            if(std::isfinite(link->m_fy) &&
                !std::isfinite(attribute_fy.value_or(NOT_A_NUMBER)) &&
                std::isfinite(cy)) {
              attribute_fy = cy;
            }
          }
          auto attributes = Attributes({{"cx", cx}, {"cy", cy}, {"r", r},
            {"gradientTransform", transform}});
          if(attribute_fx) {
            attributes.emplace_back("fx", *attribute_fx);
          }
          if(attribute_fy) {
            attributes.emplace_back("fy", *attribute_fy);
          }
          if(link) {
            if(!link->m_transform.empty() && transform.empty()) {
              attributes[3].second = std::string("matrix(1, 0, 0, 1, 0, 0)");
            }
            attributes.emplace_back("xlink:href", "#" + link->m_id);
            for(auto& attribute : attributes) {
              set_attribute(tag, attribute.first, attribute.second);
            }
          } else {
            m_gradient_stops[lines_key] = GradientLink(id, transform,
              attribute_fx.value_or(NOT_A_NUMBER),
              attribute_fy.value_or(NOT_A_NUMBER));
            attributes.emplace_back("gradientUnits", space);
            for(auto& attribute : attributes) {
              set_attribute(tag, attribute.first, attribute.second);
            }
            tag.m_children = remove_duplicates();
          }
        }
        auto fingerprint = XdValue::make_object();
        for(auto key : {"cx", "cy", "x1", "y1", "x2", "y2", "fx", "fy", "r"}) {
          fingerprint.set(key, round_fixed(get_number(reference, key), 4));
        }
        if(auto value = reference.find("transform")) {
          fingerprint.set("transform", *value);
        }
        fingerprint.set("stops", *stops);
        fingerprint.set("gradientSpace", space);
        auto target = &block;
        define(tag, [this, target, flavor] (Tag& definition) {
          add_rule(*target, flavor, "url(#" +
            to_js_string(get_attribute(definition, "id")) + ")");
        }, to_json(fingerprint));
      }

      void write_pattern(
          XdValue& node, const XdValue& reference, std::string flavor) {
        auto& state = get_state(node);
        auto ref = reference.find("ref");
        auto patterns = find_path(m_document, {"resources", "patterns"});
        if(is_truthy(ref) && patterns && patterns->is_object()) {
          if(auto pattern = const_cast<XdValue*>(
              patterns->find(to_js_string(to_js_value(ref))))) {
            if(auto transform = reference.find("transform")) {
              pattern->set("transform", *transform);
            } else {
              pattern->erase("transform");
            }
            auto name = get_name(pattern);
            auto id = m_ids.get_unique("pattern", name);
            auto saved = m_current;
            m_current = pattern;
            auto tag = make(*pattern, 0);
            m_current = saved;
            if(tag) {
              set_attribute(*tag, "id", id);
              if(!m_is_minified && name && !name->empty() && id != *name) {
                set_attribute(*tag, "data-name", *name);
              }
              auto target = &state;
              define(*tag, [this, target, flavor] (Tag& definition) {
                add_rule(get_style_block(*target), flavor, "url(#" +
                  to_js_string(get_attribute(definition, "id")) + ")");
              });
            }
          }
        }
        auto href = reference.find("href");
        if(href && href->get_type() == XdValue::Type::STRING) {
          auto id = m_ids.get_unique("pattern");
          auto width = parse_float(to_js_string(get_value(reference, "width")));
          auto height =
            parse_float(to_js_string(get_value(reference, "height")));
          auto behavior = get_scale_behavior(
            find_path(&reference, {"meta", "ux", "scaleBehavior"}));
          auto& tag = create_tag("pattern", {{"id", id},
            {"width", std::string("100%")}, {"height", std::string("100%")},
            {"patternContentUnits", std::string("userSpaceOnUse")},
            {"preserveAspectRatio",
              behavior.empty() ? JsValue() : JsValue(behavior)},
            {"viewBox", std::vector<double>({0, 0, width, height})}});
          append_child(tag, &create_tag("image", {
            {"xlink:href", std::string(href->to_string())},
            {"width", width}, {"height", height}}));
          auto target = &state;
          define(tag, [this, target, flavor] (Tag& definition) {
            add_rule(get_style_block(*target), flavor, "url(#" +
              to_js_string(get_attribute(definition, "id")) + ")");
          });
        }
      }

      void externalize_fill(XdValue& node) {
        auto& block = get_style_block(get_state(node));
        auto style = find_object(node, "style");
        auto fill = style ? find_object(*style, "fill") : nullptr;
        if(!fill) {
          if(get_type(&node) == "shape" ||
              has_member(node, "from") && has_member(node, "to")) {
            add_rule(block, "fill", std::string("none"));
          }
          return;
        }
        auto type = get_type(fill);
        if(type == "gradient") {
          auto reference = *fill->find("gradient");
          write_gradient(node, block, reference, "fill");
        } else if(type == "pattern") {
          auto reference = *fill->find("pattern");
          write_pattern(node, reference, "fill");
        } else if(type == "none") {
          add_rule(block, "fill", std::string("none"));
        } else {
          add_rule(block, "fill", write_color(fill->find("color"), true));
        }
        fill = find_object(*find_object(node, "style"), "fill");
        if(fill) {
          if(auto opacity = fill->find("opacity");
              opacity && std::isfinite(get_number(opacity))) {
            add_rule(block, "fill-opacity", to_js_value(opacity));
          }
        }
      }

      void externalize_stroke(XdValue& node) {
        auto& block = get_style_block(get_state(node));
        if(!has_stroke(node)) {
          return;
        }
        auto stroke = *find_object(*find_object(node, "style"), "stroke");
        auto type = get_type(&stroke);
        if(type == "gradient") {
          write_gradient(node, block, *stroke.find("gradient"), "stroke");
        } else if(type == "pattern") {
          write_pattern(node, *stroke.find("pattern"), "stroke");
        } else {
          add_rule(block, "stroke", write_color(stroke.find("color"), true));
        }
        if(is_truthy(stroke.find("cap"))) {
          add_rule(block, "stroke-linecap", get_value(stroke, "cap"));
        }
        if(is_truthy(stroke.find("join"))) {
          add_rule(block, "stroke-linejoin", get_value(stroke, "join"));
        }
        if(!equals(get_number(stroke.find("miterLimit")), 100)) {
          if(is_truthy(stroke.find("miterLimit"))) {
            add_rule(block, "stroke-miterlimit",
              get_value(stroke, "miterLimit"));
          } else {
            add_rule(block, "stroke-miterlimit", 4.0);
          }
        }
        if(is_truthy(stroke.find("dashOffset"))) {
          add_rule(block, "stroke-dashoffset",
            to_js_string(get_value(stroke, "dashOffset")) + "px");
        }
        if(auto opacity = stroke.find("opacity");
            opacity && std::isfinite(get_number(opacity))) {
          add_rule(block, "stroke-opacity", to_js_value(opacity));
        }
        if(auto width = stroke.find("width");
            width && std::isfinite(get_number(width))) {
          add_rule(block, "stroke-width",
            to_js_string(to_js_value(width)) + "px");
        }
        if(auto dash = stroke.find("dash");
            is_truthy(dash) && dash->is_array() &&
            !dash->get_elements().empty()) {
          auto text = std::string();
          for(auto i = std::size_t(0); i != dash->get_elements().size(); ++i) {
            if(i != 0) {
              text += ',';
            }
            text += to_js_string(to_js_value(&dash->get_elements()[i]));
          }
          add_rule(block, "stroke-dasharray", text);
        }
      }

      Tag& make_base_shadow_filter(const XdValue& node) {
        auto& filter = create_tag("filter");
        if(auto bounds = find_object(node, "visualBounds")) {
          set_attribute(filter, "x", get_value(*bounds, "x"));
          set_attribute(filter, "y", get_value(*bounds, "y"));
          set_attribute(filter, "width", get_value(*bounds, "width"));
          set_attribute(filter, "height", get_value(*bounds, "height"));
          set_attribute(
            filter, "filterUnits", std::string("userSpaceOnUse"));
        }
        return filter;
      }

      void append_shadow(Tag& filter, const XdValue& parameters,
          bool is_inner) {
        auto blur = m_ids.get_unique("blur");
        auto dx = get_value(parameters, "dx");
        auto dy = get_value(parameters, "dy");
        auto r = get_value(parameters, "r");
        append_child(filter, &create_tag("feOffset", {
          {"dx", is_truthy(dx) ? dx : JsValue(0.0)},
          {"dy", is_truthy(dy) ? dy : JsValue(0.0)},
          {"input", std::string("SourceAlpha")}}));
        append_child(filter, &create_tag("feGaussianBlur", {
          {"stdDeviation", is_truthy(r) ? r : JsValue(0.0)},
          {"result", blur}}));
        auto color = parameters.find("color");
        auto flood_opacity = 1.0;
        if(color && color->is_object()) {
          auto alpha = get_number(color->find("alpha"));
          if(color->find("alpha") && std::isfinite(alpha)) {
            flood_opacity = alpha;
          }
        }
        auto flood_color = write_color(color, false);
        if(!is_inner) {
          append_child(filter, &create_tag("feFlood", {
            {"flood-color", flood_color},
            {"flood-opacity", flood_opacity}}));
          append_child(filter, &create_tag("feComposite", {
            {"operator", std::string("in")}, {"in2", blur}}));
          return;
        }
        auto color_id = m_ids.get_unique("color");
        append_child(filter, &create_tag("feFlood", {
          {"flood-color", flood_color}, {"flood-opacity", flood_opacity},
          {"result", color_id}}));
        append_child(filter, &create_tag("feComposite", {
          {"operator", std::string("out")},
          {"in", std::string("SourceGraphic")}, {"in2", blur}}));
        append_child(filter, &create_tag("feComposite", {
          {"operator", std::string("in")}, {"in", color_id}}));
        append_child(filter, &create_tag("feComposite", {
          {"operator", std::string("in")},
          {"in2", std::string("SourceGraphic")}}));
      }

      void externalize_effects(XdValue& node) {
        auto style = find_object(node, "style");
        auto filters = style ? find_object(*style, "filters") : nullptr;
        if(!filters || !filters->is_array() ||
            filters->get_elements().empty()) {
          return;
        }
        for(auto& filter : filters->get_elements()) {
          if(is_truthy(filter.find("ref"))) {
            continue;
          }
          auto params = find_object(filter, "params");
          auto visible = filter.find("visible");
          if(!params || visible &&
              visible->get_type() == XdValue::Type::BOOL &&
              !visible->to_bool()) {
            continue;
          }
          auto type = get_type(&filter);
          auto id = std::string();
          auto tag = static_cast<Tag*>(nullptr);
          if(type == DROP_SHADOW || type == DROP_SHADOW_ONLY) {
            auto shadows = find_object(*params, "dropShadows");
            if(!shadows) {
              continue;
            }
            id = m_ids.get_unique("filter", get_name(m_current));
            tag = &make_base_shadow_filter(node);
            if(!shadows->get_elements().empty()) {
              append_shadow(*tag, shadows->get_elements()[0], false);
            }
            if(type != DROP_SHADOW_ONLY) {
              append_child(*tag, &create_tag("feComposite", {
                {"operator", std::string("over")},
                {"in", std::string("SourceGraphic")}}));
            }
          } else if(type == INNER_SHADOW) {
            auto shadows = find_object(*params, "innerShadows");
            if(!shadows) {
              continue;
            }
            id = m_ids.get_unique("filter", get_name(m_current));
            tag = &make_base_shadow_filter(node);
            if(!shadows->get_elements().empty()) {
              append_shadow(*tag, shadows->get_elements()[0], true);
            }
          } else {
            continue;
          }
          auto& state = get_state(node);
          state.m_is_filter_set = true;
          set_attribute(*tag, "id", id);
          auto target = &state;
          define(*tag, [this, target] (Tag& definition) {
            add_rule(get_style_block(*target), "filter", "url(#" +
              to_js_string(get_attribute(definition, "id")) + ")");
          });
          break;
        }
      }

      void externalize_clip_path(XdValue& node) {
        auto style = find_object(node, "style");
        auto clip = style ? find_object(*style, "clipPath") : nullptr;
        auto ref = clip && clip->is_object() ? clip->find("ref") : nullptr;
        if(!is_truthy(ref)) {
          return;
        }
        auto clips = find_path(m_document, {"resources", "clipPaths"});
        auto resource = clips && clips->is_object() ? const_cast<XdValue*>(
          clips->find(to_js_string(to_js_value(ref)))) : nullptr;
        if(!is_truthy(resource)) {
          return;
        }
        auto saved = m_current;
        m_current = resource;
        auto name = get_name(resource);
        auto id = m_ids.get_unique("clip-path", name);
        ++m_inside_clip_path;
        auto tag = make(*resource, 0);
        --m_inside_clip_path;
        m_current = saved;
        if(!tag) {
          return;
        }
        set_attribute(*tag, "id", id);
        if(!m_is_minified && name && !name->empty() && id != *name) {
          set_attribute(*tag, "data-name", *name);
        }
        auto target = &get_state(node);
        define(*tag, [this, target] (Tag& definition) {
          add_rule(get_style_block(*target), "clip-path", "url(#" +
            to_js_string(get_attribute(definition, "id")) + ")");
        });
      }

      void externalize_styles(XdValue& node) {
        m_current = &node;
        externalize_fill(node);
        m_current = &node;
        externalize_stroke(node);
        m_current = &node;
        externalize_effects(node);
        m_current = &node;
        externalize_clip_path(node);
        m_current = &node;
        auto& block = get_style_block(get_state(node));
        if(auto shape = find_object(node, "shape")) {
          if(auto winding = shape->find("winding"); is_truthy(winding)) {
            add_rule(block, "fill-rule", to_js_value(winding));
          }
        }
        auto style = find_object(node, "style");
        if(!style || !style->is_object()) {
          return;
        }
        auto font = style->find("font");
        if(font && font->is_object()) {
          if(auto size = font->find("size");
              size && std::isfinite(get_number(size))) {
            add_rule(block, "font-size",
              to_js_string(to_js_value(size)) + "px");
          }
          auto family = to_js_value(font->find("family"));
          auto postscript = to_js_value(font->find("postscriptName"));
          auto font_style = to_js_value(font->find("style"));
          auto type = std::string();
          if(is_truthy(postscript)) {
            auto name = to_js_string(postscript);
            type += quote_font_family(name);
            auto dash = name.rfind('-');
            if(dash != std::string::npos && dash + 1 < name.size()) {
              font_style = name.substr(dash + 1);
            } else {
              font_style = std::string("normal");
            }
          }
          if(is_truthy(family) && !loosely_equals(family, postscript)) {
            if(!type.empty()) {
              type += ", ";
            }
            type += quote_font_family(to_js_string(family));
          }
          if(!type.empty()) {
            add_rule(block, "font-family", type);
          }
          if(to_js_string(to_js_value(font->find("variant"))) ==
              "small-caps") {
            add_rule(block, "font-variant", std::string("small-caps"));
          }
          if(is_truthy(font_style)) {
            auto key = to_font_style_key(to_js_string(font_style));
            auto weight = find_leftmost(key, FONT_WEIGHTS);
            auto italic = find_leftmost(key, FONT_ITALICS);
            add_rule(block, "font-weight",
              weight ? static_cast<double>(weight->second) : 400.0);
            add_rule(block, "font-style", italic ?
              std::string(italic->second) : std::string("normal"));
          }
        }
        if(auto attributes = find_object(*style, "textAttributes")) {
          if(auto decoration = find_object(*attributes, "decoration")) {
            auto text = std::string();
            if(decoration->is_array()) {
              for(auto i = std::size_t(0);
                  i != decoration->get_elements().size(); ++i) {
                if(i != 0) {
                  text += ' ';
                }
                text += to_js_string(
                  to_js_value(&decoration->get_elements()[i]));
              }
            } else {
              text = to_js_string(to_js_value(decoration));
            }
            add_rule(block, "text-decoration", text);
          }
          if(auto spacing = attributes->find("letterSpacing");
              is_truthy(spacing)) {
            auto value = get_number(spacing) / 1000;
            if(is_truthy(value)) {
              add_rule(block, "letter-spacing", to_js_string(value) + "em");
            } else {
              add_rule(block, "letter-spacing", value + 0);
            }
          }
          if(to_js_string(to_js_value(attributes->find("glyphOrientation"))) ==
              "sideways-right") {
            add_rule(block, "text-orientation", std::string("sideways-right"));
          }
          if(auto align = attributes->find("verticalAlign");
              is_truthy(align)) {
            auto value = to_js_value(align);
            auto text = to_js_string(value);
            if(text == "super") {
              add_rule(block, "baseline-shift", std::string("super"));
            } else if(text == "sub") {
              add_rule(block, "baseline-shift", std::string("sub"));
            } else if(is_truthy(parse_float(text))) {
              add_rule(block, "baseline-shift", value);
            }
          }
        }
        auto entries = style->get_members();
        for(auto& member : entries) {
          auto& property = member.m_key;
          if(property == "fill" || property == "stroke" ||
              property == "filters" || property == "meta" ||
              property == "mask" || property == "clipPath" ||
              property == "name" ||
              property == "font" && member.m_value.is_object() ||
              property == "textAttributes") {
            continue;
          }
          if(property == "font-size") {
            add_rule(block, property,
              to_js_string(to_js_value(&member.m_value)) + "px");
            continue;
          }
          if(property == "font-family") {
            add_rule(block, property,
              quote_font_family(to_js_string(to_js_value(&member.m_value))));
            continue;
          }
          if(property == "blendMode") {
            add_rule(block, "mix-blend-mode", to_js_value(&member.m_value));
            continue;
          }
          if(!property.starts_with("_")) {
            add_rule(block, property, to_js_value(&member.m_value));
          }
        }
      }

      void ensure_style(XdValue& node) {
        if(!is_truthy(node.find("style"))) {
          node.set("style", XdValue::make_object());
        }
        auto& style = *node.find("style");
        for(auto key : {"filters", "clipPath", "mask"}) {
          if(!is_truthy(style.find(key))) {
            style.set(key, "");
          }
        }
      }

      void process(XdValue& node, bool is_nested) {
        if(!is_visible(node)) {
          return;
        }
        auto& state = get_state(node);
        if(state.m_is_processed) {
          return;
        }
        state.m_is_processed = true;
        externalize_styles(node);
        if(auto children = get_children(node)) {
          for(auto& child : children->get_elements()) {
            process(child, &node != m_document);
          }
        }
        auto text = find_object(node, "text");
        auto paragraphs = text ? find_object(*text, "paragraphs") : nullptr;
        if(paragraphs && paragraphs->is_array()) {
          auto style = XdValue::make_object();
          if(auto own = find_object(node, "style")) {
            style = *own;
          }
          for(auto& paragraph : paragraphs->get_elements()) {
            ensure_style(paragraph);
            merge(*paragraph.find("style"), style);
            process(paragraph, true);
            if(auto lines = find_object(paragraph, "lines")) {
              auto paragraph_style = *paragraph.find("style");
              for(auto& line : lines->get_elements()) {
                for(auto& run : line.get_elements()) {
                  ensure_style(run);
                  merge(*run.find("style"), paragraph_style);
                  process(run, true);
                }
              }
            }
          }
        }
      }

      void process_document() {
        preprocess(*m_document, false);
        m_x = m_left;
        m_y = m_top;
        m_width = m_right - m_left;
        m_height = m_bottom - m_top;
        for(auto key : {"masks", "clipPaths", "patterns", "symbols"}) {
          auto resources = find_path(m_document, {"resources", key});
          if(!resources || !resources->is_object()) {
            continue;
          }
          for(auto& member : const_cast<XdValue*>(
              resources)->get_members()) {
            process(member.m_value, false);
          }
        }
        process(*m_document, false);
      }

      bool has_global_effect(const XdValue& node) const {
        auto style = find_object(node, "style");
        auto filters = style ? find_object(*style, "filters") : nullptr;
        if(!filters || !filters->is_array() ||
            filters->get_elements().empty()) {
          return false;
        }
        return is_truthy(filters->get_elements()[0].find("global"));
      }

      Tag& use_trick(Tag& tag, const XdValue& node) {
        tag.m_has_global_filter =
          tag.m_is_filter_set && has_global_effect(node);
        return tag;
      }

      std::string get_node_transform_text(const XdValue& node) {
        return get_transform(get_node_transform(node), m_precision);
      }

      Tag* make_shape(std::string_view type, XdValue& node) {
        auto& state = get_state(node);
        auto& shape = *node.find("shape");
        if(type == "circle") {
          return &use_trick(create_tag("circle", {
            {"cx", get_value(shape, "cx")}, {"cy", get_value(shape, "cy")},
            {"r", get_value(shape, "r")},
            {"transform", get_node_transform_text(node)}}, state), node);
        } else if(type == "compound" || type == "path") {
          auto prepared = shape.find("preparedPath");
          auto is_prepared = !prepared ||
            prepared->get_type() != XdValue::Type::BOOL ||
            prepared->to_bool();
          return &use_trick(create_tag("path", {
            {"d", optimize_path(to_js_string(get_value(shape, "path")),
              m_precision, is_prepared)},
            {"transform", get_node_transform_text(node)}}, state), node);
        } else if(type == "ellipse") {
          return &use_trick(create_tag("ellipse", {
            {"cx", get_value(shape, "cx")}, {"cy", get_value(shape, "cy")},
            {"rx", get_value(shape, "rx")}, {"ry", get_value(shape, "ry")},
            {"transform", get_node_transform_text(node)}}, state), node);
        } else if(type == "line") {
          return &use_trick(create_tag("line", {
            {"x1", get_value(shape, "x1")}, {"y1", get_value(shape, "y1")},
            {"x2", get_value(shape, "x2")}, {"y2", get_value(shape, "y2")},
            {"transform", get_node_transform_text(node)}}, state), node);
        } else if(type == "polygon" || type == "polyline") {
          auto points = std::string();
          if(auto list = shape.find("points"); list && list->is_array()) {
            for(auto i = std::size_t(0); i != list->get_elements().size();
                ++i) {
              if(i != 0) {
                points += ',';
              }
              auto& point = list->get_elements()[i];
              points += to_js_string(round_fixed(get_number(point, "x"), 3)) +
                " " + to_js_string(round_fixed(get_number(point, "y"), 3));
            }
          }
          return &use_trick(create_tag(std::string(type), {
            {"points", points},
            {"transform", get_node_transform_text(node)}}, state), node);
        } else if(type == "rect") {
          auto radii = shape.find("r");
          auto width = get_value(shape, "width");
          auto height = get_value(shape, "height");
          auto r = JsValue(0.0);
          if(is_truthy(radii)) {
            auto values = std::array<double, 4>();
            for(auto i = 0; i != 4; ++i) {
              auto element = radii->is_array() &&
                static_cast<std::size_t>(i) < radii->get_elements().size() ?
                  &radii->get_elements()[i] : nullptr;
              values[i] = std::abs(
                parse_float(to_js_string(to_js_value(element))));
            }
            if(!equals(values[0], values[1]) ||
                !equals(values[1], values[2]) ||
                !equals(values[2], values[3])) {
              auto path = make_round_rect_path(get_number(shape, "x"),
                get_number(shape, "y"), to_js_number(width),
                to_js_number(height), values);
              return &use_trick(create_tag("path", {
                {"d", optimize_path(path, m_precision, false)},
                {"transform", get_node_transform_text(node)}}, state), node);
            }
            auto radius = values[0];
            auto half_width = to_js_number(width) / 2;
            auto half_height = to_js_number(height) / 2;
            if(std::isnan(radius) || std::isnan(half_width) ||
                std::isnan(half_height)) {
              r = NOT_A_NUMBER;
            } else {
              r = std::min({radius, half_width, half_height});
            }
          }
          return &use_trick(create_tag("rect", {
            {"x", get_value(shape, "x")}, {"y", get_value(shape, "y")},
            {"width", width}, {"height", height}, {"rx", r},
            {"transform", get_node_transform_text(node)}}, state), node);
        }
        return nullptr;
      }

      static bool is_preserving_space(const std::u16string& text) {
        auto is_blank = [] (char16_t c) {
          return c == u' ' || c == u'\t' || c == u'\v';
        };
        auto is_line_end = [] (char16_t c) {
          return c == u'\n' || c == u'\r' || c == 0x2028 || c == 0x2029;
        };
        if(text.size() >= 2 && is_blank(text[0]) && !is_line_end(text[1])) {
          return true;
        }
        for(auto i = std::size_t(1); i < text.size(); ++i) {
          if(is_blank(text[i - 1]) && is_blank(text[i])) {
            return true;
          }
        }
        return false;
      }

      static bool has_trailing_space(const std::u16string& text) {
        if(text.size() < 2) {
          return false;
        }
        auto last = text.back();
        auto previous = text[text.size() - 2];
        return (last == u' ' || last == u'\t' || last == u'\v') &&
          previous != u'\n' && previous != u'\r' && previous != 0x2028 &&
          previous != 0x2029;
      }

      static std::u16string substring(
          const std::u16string& text, double from, double to) {
        auto clamp = [&] (double value) {
          if(std::isnan(value)) {
            return std::size_t(0);
          }
          value = std::trunc(value);
          if(value < 0) {
            return std::size_t(0);
          }
          if(value > static_cast<double>(text.size())) {
            return text.size();
          }
          return static_cast<std::size_t>(value);
        };
        auto start = clamp(from);
        auto end = clamp(to);
        if(start > end) {
          std::swap(start, end);
        }
        return text.substr(start, end - start);
      }

      void set_glyph_styling(Tag& tag, bool is_vertical, bool is_preserved) {
        if(is_preserved) {
          set_attribute(tag, "xml:space", std::string("preserve"));
        }
        if(!tag.m_style_block) {
          return;
        }
        auto& style = *tag.m_style_block;
        if(!is_vertical) {
          remove_rule(style, "text-orientation");
          return;
        }
        if(to_js_string(get_rule(style, "text-orientation")) !=
            "sideways-right") {
          add_rule(style, "text-orientation", std::string("upright"));
          add_rule(style, "glyph-orientation-vertical", std::string("0deg"));
        } else {
          add_rule(style, "glyph-orientation-vertical", std::string("90deg"));
        }
      }

      Tag* make_text(XdValue& node) {
        auto text = find_object(node, "text");
        if(!text) {
          return nullptr;
        }
        auto frame = find_object(*text, "frame");
        auto& tag = create_tag("text", {
          {"x", frame ? get_value(*frame, "x") : JsValue()},
          {"y", frame ? get_value(*frame, "y") : JsValue()},
          {"transform", get_node_transform_text(node)}}, get_state(node));
        auto paragraphs = find_object(*text, "paragraphs");
        auto is_vertical = to_js_string(get_value(*text, "orientation")).
          starts_with("vertical");
        if(is_vertical) {
          add_rule(*tag.m_style_block, "writing-mode", std::string("tb"));
        }
        auto raw = to_utf16(to_js_string(get_value(*text, "rawText")));
        if(!paragraphs) {
          append_child(tag, &create_text(to_utf8(raw)));
          set_glyph_styling(tag, is_vertical, is_preserving_space(raw));
          return &use_trick(tag, node);
        }
        set_glyph_styling(tag, is_vertical, false);
        for(auto& paragraph : paragraphs->get_elements()) {
          auto& span = create_tag("tspan", {}, get_state(paragraph));
          set_glyph_styling(span, is_vertical, false);
          auto lines = find_object(paragraph, "lines");
          if(lines && lines->is_array()) {
            auto& list = lines->get_elements();
            for(auto j = std::size_t(0); j != list.size(); ++j) {
              auto& runs = list[j].get_elements();
              for(auto k = std::size_t(0); k != runs.size(); ++k) {
                auto& run = runs[k];
                auto glyphs = substring(raw, get_number(run, "from"),
                  get_number(run, "to"));
                auto& glyph = create_tag("tspan", {
                  {"x", get_value(run, "x")}, {"y", get_value(run, "y")},
                  {"rotate", get_value(run, "rotate")}}, get_state(run));
                auto is_preserved = is_preserving_space(glyphs);
                if(!is_preserved && j == list.size() - 1 &&
                    k == runs.size() - 1 && has_trailing_space(glyphs)) {
                  auto decoration = find_path(&run,
                    {"style", "textAttributes", "decoration"});
                  if(is_truthy(decoration) && decoration->is_array() &&
                      !decoration->get_elements().empty()) {
                    is_preserved = true;
                  }
                }
                append_child(glyph, &create_text(to_utf8(glyphs)));
                set_glyph_styling(glyph, is_vertical, is_preserved);
                append_child(span, &glyph);
              }
            }
          }
          if(!span.m_children.empty()) {
            append_child(tag, &span);
          }
        }
        return &use_trick(tag, node);
      }

      Tag* make_image(XdValue& node) {
        auto image = find_object(node, "image");
        if(!image) {
          return nullptr;
        }
        auto href = to_js_string(get_value(*image, "href"));
        auto x = is_truthy(image->find("x")) ? get_number(*image, "x") : 0.0;
        auto y = is_truthy(image->find("y")) ? get_number(*image, "y") : 0.0;
        auto w = parse_float(to_js_string(get_value(*image, "width")));
        auto h = parse_float(to_js_string(get_value(*image, "height")));
        m_is_xlink_required = true;
        auto& state = get_state(node);
        if(auto found = m_images.find(href); found != m_images.end()) {
          auto image_tag = found->second;
          auto id = std::string();
          if(!image_tag->m_is_in_defs) {
            id = m_ids.get_unique("image");
            auto& definition = create_tag("image", {
              {"width", get_attribute(*image_tag, "width")},
              {"height", get_attribute(*image_tag, "height")},
              {"xlink:href", get_attribute(*image_tag, "xlink:href")},
              {"preserveAspectRatio",
                get_attribute(*image_tag, "preserveAspectRatio")},
              {"id", id}});
            image_tag->m_name = "use";
            set_attribute(*image_tag, "width", std::string());
            set_attribute(*image_tag, "height", std::string());
            set_attribute(*image_tag, "xlink:href", "#" + id);
            m_images[href] = &definition;
            definition.m_is_in_defs = true;
            define(definition);
            image_tag = &definition;
          } else {
            id = to_js_string(get_attribute(*image_tag, "id"));
          }
          auto sx = w / to_js_number(get_attribute(*image_tag, "width"));
          auto sy = h / to_js_number(get_attribute(*image_tag, "height"));
          auto matrix = to_matrix(node.find("transform"));
          matrix.scale(sx, sy, 1);
          return &use_trick(create_tag("use", {{"x", x / sx}, {"y", y / sy},
            {"transform", write_transform(matrix, 0, 0, m_precision)},
            {"xlink:href", "#" + id}}, state), node);
        }
        auto& tag = create_tag("image", {{"x", x}, {"y", y}, {"width", w},
          {"height", h}, {"transform", get_node_transform_text(node)},
          {"xlink:href", href}}, state);
        if(auto behavior = find_path(&node, {"meta", "ux", "scaleBehavior"});
            is_truthy(behavior)) {
          set_attribute(tag, "preserveAspectRatio",
            get_scale_behavior(behavior));
        }
        m_images[href] = &tag;
        return &use_trick(tag, node);
      }

      Tag& make_group(XdValue& node) {
        auto& tag = create_tag("g",
          {{"transform", get_node_transform_text(node)}}, get_state(node));
        if(is_truthy(node.find("innerShadowGroup"))) {
          set_attribute(tag, "data-type", std::string("innerShadowGroup"));
        }
        return use_trick(tag, node);
      }

      Tag* make_artboard(XdValue& node) {
        auto ref = to_js_string(to_js_value(find_path(&node,
          {"artboard", "ref"})));
        auto boards = find_path(m_document, {"artboards"});
        auto bounds = boards && boards->is_object() ? boards->find(ref) :
          nullptr;
        if(!bounds) {
          return nullptr;
        }
        auto name = std::optional<std::string>();
        auto node_name = node.find("name");
        if(is_truthy(node_name)) {
          if(node_name->get_type() == XdValue::Type::STRING) {
            name = std::string(node_name->to_string());
          }
        } else if(auto board_name = bounds->find("name");
            board_name && board_name->get_type() == XdValue::Type::STRING) {
          name = std::string(board_name->to_string());
        }
        auto id = m_ids.get_unique("artboard", name);
        auto clip_id = m_ids.get_unique("clip-path", "clip-" + id);
        auto& artboard = create_tag("g", {{"id", id}}, get_state(node));
        if(!m_is_minified && name && !name->empty() && *name != id) {
          set_attribute(artboard, "data-name", *name);
        }
        auto x = get_number(*bounds, "x");
        auto y = get_number(*bounds, "y");
        auto width = get_value(*bounds, "width");
        auto height = get_value(*bounds, "height");
        auto& clip = create_tag("clipPath", {{"id", clip_id}});
        append_child(clip, &create_tag("rect", {{"x", x}, {"y", y},
          {"width", width}, {"height", height}}));
        auto board = &artboard;
        define(clip, [this, board, x, y, width, height] (Tag& definition) {
          auto fill = get_rule(*board->m_style_block, "fill");
          if(is_truthy(fill) && to_js_string(fill) != "none") {
            auto& block = get_style_block(make_fake_state());
            auto& background = create_tag("rect", {{"x", x}, {"y", y},
              {"width", width}, {"height", height}});
            background.m_style_block = &block;
            add_rule(block, "fill", fill);
            block.m_tags = {background.m_id};
            board->m_children.insert(board->m_children.begin(), &background);
          }
          remove_rule(*board->m_style_block, "fill");
          add_rule(*board->m_style_block, "clip-path",
            "url(#" + to_js_string(get_attribute(definition, "id")) + ")");
        });
        artboard.m_is_artboard = true;
        return &artboard;
      }

      Tag* make_clip_path(XdValue& node) {
        return &create_tag("clipPath", {
          {"clipPathUnits", get_value(node, "clipPathUnits")},
          {"transform", get_node_transform_text(node)}}, get_state(node));
      }

      Tag* make_pattern(XdValue& node) {
        auto attributes = Attributes();
        attributes.emplace_back("x", get_number(node, "x") + 0);
        attributes.emplace_back("y", get_number(node, "y") + 0);
        auto width = get_value(node, "width");
        auto height = get_value(node, "height");
        attributes.emplace_back("width", width);
        attributes.emplace_back("height", height);
        if(auto box = find_object(node, "viewBox")) {
          auto x = box->find("x");
          auto y = box->find("y");
          attributes.emplace_back("viewBox", std::vector<double>({
            get_number(x), get_number(y), get_number(*box, "width"),
            get_number(*box, "height")}));
        }
        attributes.emplace_back(
          "patternTransform", get_node_transform_text(node));
        auto units = get_value(node, "units");
        if(!is_truthy(width) && !is_truthy(height) && !is_truthy(units)) {
          attributes.emplace_back("patternUnits", JsValue());
        } else {
          attributes.emplace_back("patternUnits", is_truthy(units) ? units :
            JsValue(std::string("userSpaceOnUse")));
        }
        attributes.emplace_back(
          "patternContentUnits", get_value(node, "contentUnits"));
        return &create_tag("pattern", attributes, get_state(node));
      }

      Tag& make_svg() {
        auto attributes = Attributes();
        attributes.emplace_back(
          "xmlns", std::string("http://www.w3.org/2000/svg"));
        attributes.emplace_back(
          "xmlns:xlink", std::string("http://www.w3.org/1999/xlink"));
        attributes.emplace_back("preserveAspectRatio",
          m_options->m_preserve_aspect_ratio.empty() ? std::string("none") :
            m_options->m_preserve_aspect_ratio);
        if(std::isfinite(m_width)) {
          attributes.emplace_back("width", m_width);
        }
        if(std::isfinite(m_height)) {
          attributes.emplace_back("height", m_height);
        }
        if(std::isfinite(m_x) && std::isfinite(m_y) &&
            std::isfinite(m_width) && std::isfinite(m_height)) {
          attributes.emplace_back("viewBox",
            std::vector<double>({m_x, m_y, m_width, m_height}));
        }
        return create_tag("svg", attributes);
      }

      Tag* make_aligned_stroke(XdValue& node, std::string_view type) {
        auto& tag = make_group(node);
        tag.m_is_custom = true;
        tag.m_transform = get_node_transform(node);
        node.erase("transform");
        auto& state = get_state(node);
        auto stroke_path = find_path(&node, {"meta", "ux", "strokePath"});
        auto fill = static_cast<Tag*>(nullptr);
        auto stroke = static_cast<Tag*>(nullptr);
        auto is_clip = false;
        if(is_truthy(stroke_path)) {
          auto color = get_rule(*tag.m_style_block, "stroke");
          remove_rule(*tag.m_style_block, "stroke");
          remove_rule(*tag.m_style_block, "stroke-width");
          if(auto offset = find_path(&node, {"meta", "ux", "offsetPath"});
              is_truthy(offset)) {
            fill = &create_tag("path", {{"d", to_js_value(offset)}});
          } else {
            fill = make_shape(type, node);
          }
          set_style_block(*fill, make_fake_state());
          add_rule(*fill->m_style_block, "stroke!", std::string("none"));
          fill->m_has_global_filter = false;
          stroke = &create_tag("path", {{"d", to_js_value(stroke_path)}});
          set_style_block(*stroke, make_fake_state());
          add_rule(*stroke->m_style_block, "stroke!", std::string("none"));
          add_rule(*stroke->m_style_block, "fill!", color);
          stroke->m_has_global_filter = false;
        } else if(type == "rect" || type == "ellipse" || type == "circle") {
          auto& style_stroke = *node.find("style")->find("stroke");
          auto width = get_number(style_stroke, "width");
          auto half = width / 2;
          auto factor = get_value(style_stroke, "align") ==
            JsValue(std::string("inside")) ? -1.0 : 1.0;
          fill = make_shape(type, node);
          set_style_block(*fill, make_fake_state());
          add_rule(*fill->m_style_block, "stroke!", std::string("none"));
          fill->m_has_global_filter = false;
          auto& shape = *node.find("shape");
          auto X = 0.0;
          auto Y = 0.0;
          auto W = 0.0;
          auto H = 0.0;
          auto x = 0.0;
          auto y = 0.0;
          auto w = 0.0;
          auto h = 0.0;
          if(type == "rect") {
            w = W = get_number(shape, "width");
            h = H = get_number(shape, "height");
            x = X = get_number(shape, "x");
            y = Y = get_number(shape, "y");
            x -= half * factor;
            y -= half * factor;
            w += width * factor;
            h += width * factor;
            if(w < 0) {
              w = -w;
              x -= w;
              is_clip = true;
            }
            if(h < 0) {
              h = -h;
              y -= h;
              is_clip = true;
            }
            shape.set("x", x);
            shape.set("y", y);
            shape.set("width", w);
            shape.set("height", h);
            if(auto radii = shape.find("r"); is_truthy(radii) &&
                radii->is_array()) {
              for(auto& radius : radii->get_elements()) {
                if(is_truthy(&radius)) {
                  radius = XdValue(get_number(&radius) + half * factor);
                }
              }
            }
          } else if(type == "circle") {
            shape.set("r", std::abs(get_number(shape, "r") + half * factor));
          } else {
            shape.set("rx", std::abs(get_number(shape, "rx") + half * factor));
            shape.set("ry", std::abs(get_number(shape, "ry") + half * factor));
          }
          if(is_clip && type == "rect") {
            stroke = &create_tag("path", {{"d", "M" + join_numbers({X, y}) +
              "h" + to_js_string(W) + "M" + join_numbers({x + w, Y}) + "v" +
              to_js_string(H) + "M" + join_numbers({X + W, y + h}) + "h" +
              to_js_string(-W) + "M" + join_numbers({x, Y + H}) + "v" +
              to_js_string(-H)}});
          } else {
            stroke = make_shape(type, node);
          }
          set_style_block(*stroke, make_fake_state());
          add_rule(*stroke->m_style_block, "fill", std::string("none"));
          stroke->m_has_global_filter = false;
        }
        if(fill && stroke) {
          if(is_clip) {
            auto fill_id = m_ids.get_unique("fill");
            auto clip_id = m_ids.get_unique("clip");
            auto& clip = create_tag("clipPath", {{"id", clip_id}});
            set_attribute(*fill, "id", fill_id);
            add_rule(*stroke->m_style_block, "clip-path",
              "url(#" + clip_id + ")");
            clip.m_children.push_back(
              &create_tag("use", {{"xlink:href", "#" + fill_id}}));
            define(clip);
          }
          get_style_block(state).m_tags = {tag.m_id};
          tag.m_children = {fill, stroke};
          return &tag;
        }
        return nullptr;
      }

      Tag* make(XdValue& node, int sibling) {
        auto tag = static_cast<Tag*>(nullptr);
        if(&node == m_document) {
          tag = &make_svg();
          if(auto name = get_name(m_document); name && !name->empty()) {
            auto& title = create_tag("title");
            append_child(title, &create_text(encode_text(*name)));
            append_child(*tag, &title);
          }
        } else {
          if(auto visible = node.find("visible"); visible &&
              !is_truthy(visible)) {
            return nullptr;
          }
          auto type = get_type(&node);
          if(type == "shape") {
            auto shape_type = std::string(get_type(find_object(node, "shape")));
            auto stroke_path = find_path(&node, {"meta", "ux", "strokePath"});
            auto style = find_object(node, "style");
            auto stroke = style ? find_object(*style, "stroke") : nullptr;
            auto align = stroke ? to_js_string(get_value(*stroke, "align")) :
              std::string();
            auto is_aligned = stroke && get_type(stroke) != "none" &&
              (align == "inside" || align == "outside");
            auto is_known = shape_type == "circle" ||
              shape_type == "compound" || shape_type == "ellipse" ||
              shape_type == "line" || shape_type == "path" ||
              shape_type == "polygon" || shape_type == "polyline" ||
              shape_type == "rect";
            if(is_known && m_inside_clip_path == 0 &&
                (is_truthy(stroke_path) || is_aligned)) {
              tag = make_aligned_stroke(node, shape_type);
            }
            if(is_known && !tag) {
              tag = make_shape(shape_type, node);
            }
          } else if(type == "group") {
            tag = &make_group(node);
          } else if(type == "artboard") {
            tag = make_artboard(node);
          } else if(type == "text") {
            tag = make_text(node);
          } else if(type == "image") {
            tag = make_image(node);
          } else if(type == "clipPath") {
            tag = make_clip_path(node);
          } else if(type == "pattern") {
            tag = make_pattern(node);
          }
          auto name = node.find("name");
          if(!m_is_minified && name &&
              name->get_type() == XdValue::Type::STRING &&
              !name->to_string().empty() && tag &&
              !is_truthy(get_attribute(*tag, "id"))) {
            auto text = std::string(name->to_string());
            auto id = m_ids.get_unique("", text);
            set_attribute(*tag, "id", id);
            if(text != id) {
              set_attribute(*tag, "data-name", text);
            }
          }
        }
        if(tag && tag->m_name != "tspan") {
          if(auto children = get_children(node)) {
            auto& elements = children->get_elements();
            for(auto i = std::size_t(0); i < elements.size(); ++i) {
              m_current = &elements[i];
              if(auto child = make(elements[i], static_cast<int>(i))) {
                append_child(*tag, child);
              }
            }
          }
        }
        if(tag && is_truthy(node.find("transform"))) {
          tag->m_transform = to_matrix(node.find("transform"));
        }
        if(tag && tag->m_name == "text" && tag->m_style_block) {
          auto& style = *tag->m_style_block;
          if(to_js_string(get_rule(style, "font-style")) == "normal") {
            remove_rule(style, "font-style");
          }
          if(loosely_equals(get_rule(style, "font-weight"), 400.0)) {
            remove_rule(style, "font-weight");
          }
        }
        return tag;
      }

      bool has_relevant_group_style(const Tag& tag) const {
        if(tag.m_is_custom) {
          return true;
        }
        auto style = tag.m_style_block;
        if(!style) {
          return false;
        }
        auto has = [&] (std::string_view name) {
          return style->m_rules.contains(name);
        };
        auto value = [&] (std::string_view name) {
          return to_js_string(get_rule(*style, name));
        };
        return has("mix-blend-mode") && value("mix-blend-mode") != "normal" ||
          has("isolation") ||
          has("clip-path") && value("clip-path") != "none" ||
          has("filter") && value("filter") != "none" ||
          has("mask") && value("mask") != "none" ||
          has("opacity") &&
            !equals(to_js_number(get_rule(*style, "opacity")), 1);
      }

      void remove_superfluous_group(
          Tag& tag, const std::vector<Tag*>& parents, int number) {
        if(parents.empty() || tag.m_name != "g" || tag.m_is_artboard) {
          return;
        }
        auto& mom = *parents.back();
        if(tag.m_children.empty()) {
          mom.m_children.erase(mom.m_children.begin() + number);
          return;
        }
        if(to_js_string(get_attribute(tag, "data-type")) ==
            "innerShadowGroup") {
          return;
        }
        auto is_resource_group = mom.m_children.size() == 1 &&
          (mom.m_name == "pattern" || mom.m_name == "symbol" ||
            mom.m_name == "mask");
        auto is_root_group = false;
        if(mom.m_name == "svg") {
          is_root_group = true;
          for(auto i = 0; i < static_cast<int>(mom.m_children.size()); ++i) {
            if(i == number) {
              continue;
            }
            auto& name = mom.m_children[i]->m_name;
            if(!name.empty() && name != "defs" && name != "title" &&
                name != "#comment" && name != "desc") {
              is_root_group = false;
              break;
            }
          }
        }
        auto is_set = [&] (std::string_view name) {
          return to_js_string(get_attribute(tag, name)) != "";
        };
        if(has_relevant_group_style(tag) || is_set("transform") ||
            is_set("clip-path") || is_set("id") && !is_root_group) {
          return;
        }
        if(tag.m_children.size() > 1 && !m_is_minified &&
            !is_resource_group && !is_root_group) {
          return;
        }
        if(is_root_group) {
          set_attribute(mom, "id", get_attribute(tag, "id"));
          set_attribute(mom, "data-name", get_attribute(tag, "data-name"));
        }
        auto children = tag.m_children;
        mom.m_children.erase(mom.m_children.begin() + number);
        mom.m_children.insert(
          mom.m_children.begin() + number, children.begin(), children.end());
      }

      void convert_clip_rule(Tag& tag, const std::vector<Tag*>& parents) {
        if(!tag.m_style_block) {
          return;
        }
        auto rule = get_rule(*tag.m_style_block, "fill-rule");
        if(!is_truthy(rule)) {
          return;
        }
        auto is_clipped = std::any_of(parents.begin(), parents.end(),
          [] (auto parent) { return parent->m_name == "clipPath"; });
        if(!is_clipped) {
          return;
        }
        add_rule(*tag.m_style_block, "clip-rule", rule);
        remove_rule(*tag.m_style_block, "fill-rule");
      }

      void fix_span_fills(Tag& tag, const std::vector<Tag*>& parents) {
        if(tag.m_name != "tspan" || !tag.m_style_block) {
          return;
        }
        auto& block = *tag.m_style_block;
        for(auto& name : block.m_rules.get_keys()) {
          auto value = get_rule(block, name);
          if(!is_truthy(value) || !is_default(block, name)) {
            continue;
          }
          auto is_inherited = false;
          for(auto i = parents.size(); i-- != 0;) {
            auto style = parents[i]->m_style_block;
            if(style && (is_truthy(get_rule(*style, name)) &&
                !is_default(*style, name) ||
                is_truthy(get_rule(*style, name + "!")) &&
                !is_default(*style, name + "!"))) {
              is_inherited = true;
              break;
            }
          }
          if(is_inherited) {
            enforce_rule(block, name);
          }
        }
      }

      void remove_chains(std::vector<Tag*>& children) {
        for(auto i = 0; i < static_cast<int>(children.size()) - 1; ++i) {
          auto is_passed = false;
          while(i + 1 < static_cast<int>(children.size()) &&
              children[i]->m_style_block &&
              children[i + 1]->m_style_block &&
              is_equal(*children[i]->m_style_block,
                *children[i + 1]->m_style_block)) {
            for(auto& name : children[i]->m_attributes.get_keys()) {
              if(!write_attribute(*children[i], nullptr, name).empty()) {
                is_passed = true;
                break;
              }
            }
            if(is_passed) {
              break;
            }
            for(auto& name : children[i + 1]->m_attributes.get_keys()) {
              if(!write_attribute(*children[i + 1], nullptr, name).empty()) {
                is_passed = true;
                break;
              }
            }
            if(is_passed) {
              break;
            }
            auto& merged = children[i]->m_children;
            merged.insert(merged.end(), children[i + 1]->m_children.begin(),
              children[i + 1]->m_children.end());
            remove_chains(merged);
            children.erase(children.begin() + i + 1);
          }
        }
      }

      void collapse_span_classes(Tag& tag) {
        if(tag.m_name != "tspan" && tag.m_name != "text") {
          return;
        }
        auto common = OrderedMap<JsValue>();
        for(auto i = std::size_t(0); i < tag.m_children.size(); ++i) {
          auto style = tag.m_children[i]->m_style_block;
          if(!style) {
            return;
          }
          if(i != 0) {
            for(auto& name : common.get_keys()) {
              if(is_undefined(get_rule(*style, name))) {
                common.erase(name);
              }
            }
          } else {
            for(auto& entry : style->m_rules.get_entries()) {
              common.set(entry.first, entry.second);
            }
          }
        }
        auto collapsed = std::vector<Tag*>();
        for(auto child : tag.m_children) {
          auto style = child->m_style_block;
          if(!style) {
            continue;
          }
          for(auto& name : style->m_rules.get_keys()) {
            if(!style->m_rules.contains(name)) {
              continue;
            }
            auto shared = common.find(name);
            if(loosely_equals(shared ? *shared : JsValue(),
                get_rule(*style, name))) {
              style->m_rules.erase(name);
              style->m_defaults.erase(name);
            } else if(is_default(*style, name)) {
              enforce_rule(*style, name);
            }
          }
          auto is_kept = has_rules(*style);
          if(!is_kept) {
            for(auto& name : child->m_attributes.get_keys()) {
              if(!write_attribute(*child, nullptr, name).empty()) {
                is_kept = true;
                break;
              }
            }
          }
          if(!is_kept) {
            collapsed.push_back(child);
          }
        }
        for(auto child : collapsed) {
          auto position = std::find(
            tag.m_children.begin(), tag.m_children.end(), child);
          if(position != tag.m_children.end()) {
            auto index = position - tag.m_children.begin();
            auto grandchildren = child->m_children;
            tag.m_children.erase(position);
            tag.m_children.insert(tag.m_children.begin() + index,
              grandchildren.begin(), grandchildren.end());
          }
        }
        remove_chains(tag.m_children);
        if(tag.m_style_block) {
          for(auto& entry : common.get_entries()) {
            add_rule(*tag.m_style_block, entry.first, entry.second);
          }
          if(tag.m_name == "text") {
            auto& style = *tag.m_style_block;
            if(to_js_string(get_rule(style, "font-style")) == "normal") {
              remove_rule(style, "font-style");
            }
            if(loosely_equals(get_rule(style, "font-weight"), 400.0)) {
              remove_rule(style, "font-weight");
            }
          }
        }
      }

      void apply_global_filter(Tag& tag, const std::vector<Tag*>& parents) {
        if(!tag.m_has_global_filter || parents.empty()) {
          return;
        }
        auto& mom = *parents.back();
        auto transform = Matrix4();
        for(auto parent : parents) {
          if(parent->m_transform) {
            transform.pre_multiply(*parent->m_transform);
          }
        }
        auto inverse = transform.get_inverse();
        if(!inverse) {
          return;
        }
        auto filter = get_attribute(tag, "filter");
        auto& group = create_tag("g",
          {{"transform", write_transform(*inverse, 0, 0, NOT_A_NUMBER)}});
        set_style_block(group, make_fake_state());
        add_rule(*group.m_style_block, "filter", filter);
        auto blend = get_rule(*tag.m_style_block, "mix-blend-mode");
        if(is_truthy(blend)) {
          add_rule(*group.m_style_block, "mix-blend-mode", blend);
          remove_rule(*tag.m_style_block, "mix-blend-mode");
        }
        auto isolation = get_rule(*tag.m_style_block, "isolation");
        if(is_truthy(isolation)) {
          add_rule(*group.m_style_block, "isolation", isolation);
          remove_rule(*tag.m_style_block, "isolation");
        }
        append_child(group, &tag);
        transform.pre_multiply(tag.m_transform.value_or(Matrix4()));
        remove_rule(*tag.m_style_block, "filter");
        set_attribute(
          tag, "transform", write_transform(transform, 0, 0, NOT_A_NUMBER));
        for(auto& child : mom.m_children) {
          if(child == &tag) {
            child = &group;
            break;
          }
        }
      }

      void post_process(Tag& tag, std::vector<Tag*> parents, int number) {
        parents.push_back(&tag);
        for(auto i = static_cast<int>(tag.m_children.size()) - 1; i >= 0;
            --i) {
          if(i < static_cast<int>(tag.m_children.size())) {
            post_process(*tag.m_children[i], parents, i);
          }
        }
        parents.pop_back();
        remove_superfluous_group(tag, parents, number);
        convert_clip_rule(tag, parents);
        fix_span_fills(tag, parents);
        collapse_span_classes(tag);
        apply_global_filter(tag, parents);
      }

      void post_process(Tag& root) {
        post_process(root, {}, 0);
        consolidate_style_blocks();
        auto ids = IdGenerator(m_is_minified);
        for(auto& entry : m_blocks.get_entries()) {
          auto block = entry.second;
          if(!block->m_tags.empty() && has_rules(*block)) {
            block->m_classes[0] = ids.get_unique("cls");
          }
        }
      }

      void consolidate_style_blocks() {
        auto duplicates = OrderedMap<std::vector<StyleBlock*>>();
        for(auto& key : m_blocks.get_keys()) {
          auto block = m_blocks.find(key);
          if(!block) {
            continue;
          }
          if(!has_rules(**block)) {
            m_blocks.erase(key);
            continue;
          }
          auto rules = XdValue::make_object();
          for(auto& entry : (*block)->m_rules.get_entries()) {
            if(auto number = std::get_if<double>(&entry.second)) {
              rules.set(entry.first, *number);
            } else if(auto text = std::get_if<std::string>(&entry.second)) {
              rules.set(entry.first, *text);
            } else if(auto list =
                std::get_if<std::vector<double>>(&entry.second)) {
              auto array = XdValue::make_array();
              for(auto value : *list) {
                array.append(value);
              }
              rules.set(entry.first, std::move(array));
            }
          }
          auto fingerprint = to_json(rules);
          auto list = duplicates.find(fingerprint);
          if(!list) {
            list = &duplicates.set(fingerprint, {});
          }
          list->push_back(*block);
        }
        for(auto& entry : duplicates.get_entries()) {
          auto& blocks = entry.second;
          for(auto i = std::size_t(1); i < blocks.size(); ++i) {
            auto tags = blocks[i]->m_tags;
            for(auto id : tags) {
              if(id < 0 || id >= static_cast<int>(m_tags.size())) {
                continue;
              }
              auto& tag = m_tags[id];
              if(tag.m_style_block) {
                auto key = std::string();
                for(auto j = std::size_t(0);
                    j != tag.m_style_block->m_classes.size(); ++j) {
                  if(j != 0) {
                    key += ',';
                  }
                  key += tag.m_style_block->m_classes[j];
                }
                m_blocks.erase(key);
              }
              tag.m_style_block = blocks[0];
              blocks[0]->m_tags.push_back(id);
            }
          }
        }
      }
  };
}

std::string Rellu::write_svg(
    XdValue document, const SvgWriterOptions& options) {
  auto writer = Writer(document, options);
  return writer.write();
}
