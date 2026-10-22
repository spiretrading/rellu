#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  std::uint64_t hash(QStringView text, std::uint64_t seed) {
    auto value = seed;
    for(auto character : text) {
      value ^= character.unicode();
      value *= 0x100000001B3ULL;
    }
    return value;
  }

  NodeId make_derived_id(std::uint64_t high, std::uint64_t low) {
    auto bytes = std::array<std::uint8_t, NodeId::SIZE>();
    for(auto i = std::size_t(0); i != 8; ++i) {
      bytes[i] = static_cast<std::uint8_t>(high >> (56 - 8 * i));
      bytes[i + 8] = static_cast<std::uint8_t>(low >> (56 - 8 * i));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x80);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
    return NodeId(bytes);
  }
}

NodeId Rellu::to_node_id(QStringView text) {
  if(text.isEmpty()) {
    return NodeId();
  }
  if(text.size() <= 36) {
    auto buffer = std::array<char, 36>();
    auto is_ascii = true;
    for(auto i = qsizetype(0); i != text.size(); ++i) {
      auto code = text[i].unicode();
      if(code > 127) {
        is_ascii = false;
        break;
      }
      buffer[static_cast<std::size_t>(i)] = static_cast<char>(code);
    }
    if(is_ascii) {
      if(auto id = parse_node_id(std::string_view(
          buffer.data(), static_cast<std::size_t>(text.size())))) {
        return *id;
      }
    }
  }
  return make_derived_id(hash(text, 0xCBF29CE484222325ULL),
    hash(text, 0x84222325CBF29CE4ULL));
}

NodeId Rellu::derive_node_id(QStringView text, QStringView qualifier) {
  auto high = hash(qualifier, hash(text, 0xCBF29CE484222325ULL) ^ 0x2F);
  auto low = hash(qualifier, hash(text, 0x84222325CBF29CE4ULL) ^ 0x5D);
  return make_derived_id(high, low);
}

std::string Rellu::to_std_string(const QString& text) {
  return text.toStdString();
}

std::optional<Color> Rellu::to_argb_color(double value) {
  if(!(value >= 0) || value > 4294967295.0 || std::floor(value) != value) {
    return std::nullopt;
  }
  return make_argb_color(static_cast<std::uint32_t>(value));
}

std::optional<TextTransform> Rellu::to_text_transform(QStringView name) {
  if(name == u"none") {
    return TextTransform::NONE;
  } else if(name == u"uppercase") {
    return TextTransform::UPPERCASE;
  } else if(name == u"lowercase") {
    return TextTransform::LOWERCASE;
  } else if(name == u"titlecase") {
    return TextTransform::TITLE_CASE;
  }
  return std::nullopt;
}

std::optional<TextScript> Rellu::to_text_script(QStringView name) {
  if(name == u"none") {
    return TextScript::NORMAL;
  } else if(name == u"superscript") {
    return TextScript::SUPERSCRIPT;
  } else if(name == u"subscript") {
    return TextScript::SUBSCRIPT;
  }
  return std::nullopt;
}

double Rellu::get_vertical_align(const TextStyle& style) {
  if(style.m_script == TextScript::NORMAL) {
    return 0;
  }
  auto shift = style.m_font.m_size / 0.5833 * 0.3333;
  if(style.m_script == TextScript::SUBSCRIPT) {
    return -shift;
  }
  return shift;
}

std::optional<Color> Rellu::read_color(XdObject& color) {
  if(color.contains("mode"_L1)) {
    auto mode = color.get_string("mode"_L1);
    if(!mode || *mode != "RGB"_L1) {
      return std::nullopt;
    }
  }
  auto value = color.get_object("value"_L1);
  if(!value) {
    return std::nullopt;
  }
  auto red = value->get_number("r"_L1);
  auto green = value->get_number("g"_L1);
  auto blue = value->get_number("b"_L1);
  if(!red || !green || !blue) {
    return std::nullopt;
  }
  auto alpha = color.get_number("alpha"_L1, 1);
  return Color(*red / 255, *green / 255, *blue / 255, alpha);
}

std::optional<Matrix> Rellu::read_matrix(XdObject& transform) {
  auto& json = transform.get_json();
  if(!json.value("a"_L1).isDouble() || !json.value("b"_L1).isDouble() ||
      !json.value("c"_L1).isDouble() || !json.value("d"_L1).isDouble() ||
      !json.value("tx"_L1).isDouble() || !json.value("ty"_L1).isDouble()) {
    return std::nullopt;
  }
  return Matrix(*transform.get_number("a"_L1), *transform.get_number("b"_L1),
    *transform.get_number("c"_L1), *transform.get_number("d"_L1),
    *transform.get_number("tx"_L1), *transform.get_number("ty"_L1));
}

XdObject* Rellu::get_ux(XdObject& json) {
  if(auto meta = json.get_object("meta"_L1)) {
    return meta->get_object("ux"_L1);
  }
  return nullptr;
}
