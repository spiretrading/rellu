#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <QFile>
#include "HarfBuzz.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
  constexpr auto STYLE_WORDS = std::array<std::string_view, 22>({"thin",
    "hairline", "extralight", "ultralight", "light", "semilight", "regular",
    "normal", "book", "medium", "semibold", "demibold", "bold", "extrabold",
    "ultrabold", "black", "heavy", "extrablack", "ultrablack", "italic",
    "oblique", "slanted"});

  struct MappedFile {
    QFile m_file;
    uchar* m_data = nullptr;

    ~MappedFile() {
      if(m_data) {
        m_file.unmap(m_data);
      }
    }
  };

  std::string read_name(hb_face_t* face, hb_ot_name_id_t id) {
    if(id == HB_OT_NAME_ID_INVALID) {
      return {};
    }
    auto size = hb_ot_name_get_utf8(
      face, id, HB_LANGUAGE_INVALID, nullptr, nullptr);
    if(size == 0) {
      return {};
    }
    auto name = std::string(size + 1, '\0');
    auto capacity = size + 1;
    hb_ot_name_get_utf8(face, id, HB_LANGUAGE_INVALID, &capacity, name.data());
    name.resize(capacity);
    return name;
  }

  bool is_style_word(std::string_view word) {
    auto lower = std::string(word);
    std::transform(lower.begin(), lower.end(), lower.begin(),
      [] (auto c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      });
    return std::find(STYLE_WORDS.begin(), STYLE_WORDS.end(), lower) !=
      STYLE_WORDS.end();
  }

  void split_instance(const std::string& name, std::string& family,
      std::string& style) {
    auto end = name.size();
    while(end != 0) {
      auto start = name.rfind(' ', end - 1);
      auto word_start = start == std::string::npos ? 0 : start + 1;
      if(!is_style_word(std::string_view(name).substr(
          word_start, end - word_start)) || word_start == 0) {
        break;
      }
      end = start;
    }
    family = name.substr(0, end);
    style = end == name.size() ? std::string() : name.substr(end + 1);
    if(style.empty()) {
      style = "Regular";
    }
  }
}

hb_blob_t* Details::map_font_file(
    const std::filesystem::path& path, std::shared_ptr<const void>& owner) {
  auto file = std::make_shared<MappedFile>();
  file->m_file.setFileName(QString::fromStdU16String(path.u16string()));
  if(!file->m_file.open(QIODevice::ReadOnly)) {
    return nullptr;
  }
  auto size = file->m_file.size();
  if(size <= 0 || size > 0x7FFFFFFF) {
    return nullptr;
  }
  file->m_data = file->m_file.map(0, size);
  if(!file->m_data) {
    return nullptr;
  }
  owner = file;
  return hb_blob_create(reinterpret_cast<const char*>(file->m_data),
    static_cast<unsigned int>(size), HB_MEMORY_MODE_READONLY, nullptr,
    nullptr);
}

TypefaceNames Details::read_names(
    hb_face_t* face, hb_font_t* font, std::optional<unsigned int> instance) {
  auto names = TypefaceNames();
  names.m_family = read_name(face, HB_OT_NAME_ID_FONT_FAMILY);
  names.m_style = read_name(face, HB_OT_NAME_ID_FONT_SUBFAMILY);
  names.m_typographic_family =
    read_name(face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY);
  if(names.m_typographic_family.empty()) {
    names.m_typographic_family = names.m_family;
  }
  names.m_typographic_style =
    read_name(face, HB_OT_NAME_ID_TYPOGRAPHIC_SUBFAMILY);
  if(names.m_typographic_style.empty()) {
    names.m_typographic_style = names.m_style;
  }
  names.m_postscript_name = read_name(face, HB_OT_NAME_ID_POSTSCRIPT_NAME);
  if(instance) {
    auto subfamily = read_name(face,
      hb_ot_var_named_instance_get_subfamily_name_id(face, *instance));
    names.m_typographic_style = subfamily;
    split_instance(names.m_typographic_family + ' ' + subfamily,
      names.m_family, names.m_style);
    names.m_postscript_name = read_name(face,
      hb_ot_var_named_instance_get_postscript_name_id(face, *instance));
  }
  names.m_weight = hb_style_get_value(font, HB_STYLE_TAG_WEIGHT);
  names.m_is_italic = hb_style_get_value(font, HB_STYLE_TAG_ITALIC) > 0.5 ||
    hb_style_get_value(font, HB_STYLE_TAG_SLANT_ANGLE) != 0;
  return names;
}
