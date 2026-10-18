#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include "Rellu/Text/FontLibrary.hpp"
#include "HarfBuzz.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
  struct Substitute {
    std::string_view m_prefix;
    std::string_view m_family;
  };

  constexpr auto SUBSTITUTES = std::array<Substitute, 12>({
    {"helveticaneue", "Arial"}, {"helvetica", "Arial"}, {"arialmt", "Arial"},
    {"lucidagrande", "Segoe UI"}, {".applesystemuifont", "Segoe UI"},
    {".sfns", "Segoe UI"}, {"sanfrancisco", "Segoe UI"},
    {"sfmono", "Consolas"}, {"sfpro", "Segoe UI"},
    {"pingfang", "Microsoft YaHei"}, {"applecoloremoji", "Segoe UI Emoji"},
    {"zapfdingbats", "Segoe UI Symbol"}});

  constexpr auto FALLBACK_FAMILIES =
    std::array<std::string_view, 3>({"Segoe UI", "Arial", "Roboto"});

  constexpr auto CHARACTER_FALLBACKS = std::array<std::string_view, 7>({
    "Segoe UI", "Segoe UI Symbol", "Segoe UI Emoji", "Segoe MDL2 Assets",
    "Arial", "Microsoft YaHei", "Roboto"});

  struct Weight {
    std::string_view m_word;
    double m_weight;
  };

  constexpr auto WEIGHTS = std::array<Weight, 19>({{"thin", 100},
    {"hairline", 100}, {"extralight", 200}, {"ultralight", 200},
    {"semilight", 350}, {"light", 300}, {"regular", 400}, {"normal", 400},
    {"book", 400}, {"medium", 500}, {"semibold", 600}, {"demibold", 600},
    {"extrabold", 800}, {"ultrabold", 800}, {"extrablack", 950},
    {"ultrablack", 950}, {"bold", 700}, {"black", 900}, {"heavy", 900}});

  std::string normalize(std::string_view name) {
    auto result = std::string();
    result.reserve(name.size());
    for(auto c : name) {
      if(c != ' ' && c != '-' && c != '_') {
        result.push_back(static_cast<char>(
          std::tolower(static_cast<unsigned char>(c))));
      }
    }
    return result;
  }

  std::string normalize_style(std::string_view style) {
    auto result = normalize(style);
    if(result.empty()) {
      return "regular";
    }
    if(result != "regular") {
      if(auto position = result.find("regular");
          position != std::string::npos) {
        result.erase(position, 7);
      }
    }
    if(result == "oblique") {
      return "italic";
    }
    return result;
  }

  double to_weight(std::string_view style) {
    auto name = normalize(style);
    for(auto& weight : WEIGHTS) {
      if(name.find(weight.m_word) != std::string::npos) {
        return weight.m_weight;
      }
    }
    return 400;
  }

  bool is_italic(std::string_view style) {
    auto name = normalize(style);
    return name.find("italic") != std::string::npos ||
      name.find("oblique") != std::string::npos;
  }

  bool is_font_file(const std::filesystem::path& path) {
    auto extension = normalize(path.extension().string());
    return extension == ".ttf" || extension == ".otf" ||
      extension == ".ttc" || extension == ".otc";
  }

  bool is_match(const TypefaceNames& names, const std::string& family,
      const std::string& style) {
    return (normalize(names.m_family) == family &&
      normalize_style(names.m_style) == style) ||
      (normalize(names.m_typographic_family) == family &&
      normalize_style(names.m_typographic_style) == style);
  }

  std::filesystem::path to_path(const QString& path) {
    return std::filesystem::path(QDir::toNativeSeparators(path).toStdWString());
  }

  std::optional<std::vector<std::filesystem::path>>&
      get_default_directories() {
    static auto directories =
      std::optional<std::vector<std::filesystem::path>>();
    return directories;
  }
}

FontLibrary::FontLibrary(std::vector<std::filesystem::path> directories)
  : m_directories(std::move(directories)),
    m_is_scanned(false),
    m_is_fallback_loaded(false) {}

const std::vector<std::filesystem::path>&
    FontLibrary::get_directories() const {
  return m_directories;
}

std::vector<FontFace> FontLibrary::get_faces() const {
  auto lock = std::lock_guard(m_mutex);
  scan();
  return m_faces;
}

FontResolution FontLibrary::resolve(const Font& font) const {
  auto lock = std::lock_guard(m_mutex);
  scan();
  auto key =
    std::tuple(font.m_family, font.m_style, font.m_postscript_name);
  auto resolution = m_resolutions.find(key);
  if(resolution != m_resolutions.end()) {
    return resolution->second;
  }
  return m_resolutions.emplace(key, find(font)).first->second;
}

std::shared_ptr<const Typeface> FontLibrary::find_fallback(
    char32_t character) const {
  auto lock = std::lock_guard(m_mutex);
  scan();
  if(!m_is_fallback_loaded) {
    m_is_fallback_loaded = true;
    for(auto family : CHARACTER_FALLBACKS) {
      if(auto face = find_face(normalize(family), "regular", true)) {
        if(auto typeface = load(*face)) {
          m_fallbacks.push_back(std::move(typeface));
        }
      }
    }
  }
  for(auto& fallback : m_fallbacks) {
    if(fallback->has_glyph(character)) {
      return fallback;
    }
  }
  return nullptr;
}

void FontLibrary::scan() const {
  if(m_is_scanned) {
    return;
  }
  m_is_scanned = true;
  for(auto& directory : m_directories) {
    auto files = std::vector<std::filesystem::path>();
    auto error = std::error_code();
    for(auto i = std::filesystem::recursive_directory_iterator(directory,
        std::filesystem::directory_options::skip_permission_denied, error);
        !error && i != std::filesystem::recursive_directory_iterator();
        i.increment(error)) {
      if(i->is_regular_file(error) && is_font_file(i->path())) {
        files.push_back(i->path());
      }
    }
    std::sort(files.begin(), files.end());
    for(auto& file : files) {
      auto owner = std::shared_ptr<const void>();
      auto blob = map_font_file(file, owner);
      if(!blob) {
        continue;
      }
      auto count = hb_face_count(blob);
      for(auto index = 0u; index != count; ++index) {
        auto face = hb_face_create(blob, index);
        auto font = hb_font_create(face);
        add(FontFace(file, index, std::nullopt,
          read_names(face, font, std::nullopt)));
        auto instances = hb_ot_var_get_named_instance_count(face);
        for(auto instance = 0u; instance != instances; ++instance) {
          hb_font_set_var_named_instance(font, instance);
          add(FontFace(file, index, instance,
            read_names(face, font, instance)));
        }
        hb_font_destroy(font);
        hb_face_destroy(face);
      }
      hb_blob_destroy(blob);
    }
  }
}

void FontLibrary::add(FontFace face) const {
  auto& names = face.m_names;
  if(names.m_family.empty() && names.m_postscript_name.empty()) {
    return;
  }
  auto index = m_faces.size();
  if(!names.m_postscript_name.empty()) {
    m_postscript_names[normalize(names.m_postscript_name)].push_back(index);
  }
  auto family = normalize(names.m_family);
  m_families[family].push_back(index);
  auto typographic_family = normalize(names.m_typographic_family);
  if(typographic_family != family) {
    m_families[typographic_family].push_back(index);
  }
  m_faces.push_back(std::move(face));
  m_typefaces.emplace_back();
}

std::shared_ptr<const Typeface> FontLibrary::load(std::size_t index) const {
  auto& typeface = m_typefaces[index];
  if(!typeface) {
    auto& face = m_faces[index];
    typeface = Typeface::load(face.m_path, face.m_index, face.m_instance);
  }
  return typeface;
}

std::optional<std::size_t> FontLibrary::find_face(const std::string& family,
    const std::string& style, bool is_nearest) const {
  auto faces = m_families.find(family);
  if(faces == m_families.end()) {
    return std::nullopt;
  }
  auto normalized_style = normalize_style(style);
  for(auto index : faces->second) {
    if(is_match(m_faces[index].m_names, family, normalized_style)) {
      return index;
    }
  }
  if(!is_nearest) {
    return std::nullopt;
  }
  auto weight = to_weight(style);
  auto italic = is_italic(style);
  auto best = std::optional<std::size_t>();
  auto best_score = std::numeric_limits<double>::infinity();
  for(auto index : faces->second) {
    auto& names = m_faces[index].m_names;
    auto score = std::abs(names.m_weight - weight) +
      (names.m_is_italic != italic ? 1000 : 0);
    if(score < best_score) {
      best_score = score;
      best = index;
    }
  }
  return best;
}

FontResolution FontLibrary::find(const Font& font) const {
  auto family = normalize(font.m_family);
  auto style = normalize_style(font.m_style);
  auto from_postscript_name = [&] (const std::string& name) {
    auto faces = m_postscript_names.find(normalize(name));
    if(faces == m_postscript_names.end()) {
      return std::optional<std::size_t>();
    }
    for(auto index : faces->second) {
      if(is_match(m_faces[index].m_names, family, style)) {
        return std::optional(index);
      }
    }
    return std::optional(faces->second.front());
  };
  auto make = [&] (std::optional<std::size_t> index, FontMatch match) {
    if(!index) {
      return FontResolution();
    }
    auto typeface = load(*index);
    if(!typeface) {
      return FontResolution();
    }
    return FontResolution(std::move(typeface), match);
  };
  if(!font.m_postscript_name.empty()) {
    if(auto resolution = make(from_postscript_name(font.m_postscript_name),
        FontMatch::POSTSCRIPT_NAME); resolution.m_typeface) {
      return resolution;
    }
  }
  if(auto resolution = make(find_face(family, font.m_style, false),
      FontMatch::FAMILY_AND_STYLE); resolution.m_typeface) {
    return resolution;
  }
  if(auto resolution = make(from_postscript_name(font.m_family),
      FontMatch::POSTSCRIPT_NAME); resolution.m_typeface) {
    return resolution;
  }
  if(auto resolution = make(find_face(family, font.m_style, true),
      FontMatch::FAMILY); resolution.m_typeface) {
    return resolution;
  }
  for(auto& substitute : SUBSTITUTES) {
    if(family.starts_with(substitute.m_prefix)) {
      if(auto resolution = make(find_face(normalize(substitute.m_family),
          font.m_style, true), FontMatch::SUBSTITUTE); resolution.m_typeface) {
        return resolution;
      }
      break;
    }
  }
  for(auto fallback : FALLBACK_FAMILIES) {
    if(auto resolution = make(find_face(normalize(fallback), font.m_style,
        true), FontMatch::FALLBACK); resolution.m_typeface) {
      return resolution;
    }
  }
  for(auto index = std::size_t(0); index != m_faces.size(); ++index) {
    if(auto resolution = make(index, FontMatch::FALLBACK);
        resolution.m_typeface) {
      return resolution;
    }
  }
  return FontResolution();
}

std::vector<std::filesystem::path> Rellu::get_system_font_directories() {
  auto directories = std::vector<std::filesystem::path>();
  auto add = [&] (const QString& directory) {
    if(directory.isEmpty()) {
      return;
    }
    auto path = to_path(directory);
    if(std::find(directories.begin(), directories.end(), path) ==
        directories.end()) {
      directories.push_back(std::move(path));
    }
  };
  for(auto& directory :
      QStandardPaths::standardLocations(QStandardPaths::FontsLocation)) {
    add(directory);
  }
#ifdef Q_OS_WIN
  add(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
    "/Microsoft/Windows/Fonts");
#endif
  return directories;
}

std::filesystem::path Rellu::get_bundled_font_directory() {
  if(!QCoreApplication::instance()) {
    return std::filesystem::path();
  }
  return to_path(QCoreApplication::applicationDirPath() + "/Fonts");
}

void Rellu::set_default_font_directories(
    std::vector<std::filesystem::path> directories) {
  get_default_directories() = std::move(directories);
}

const FontLibrary& Rellu::get_default_font_library() {
  static auto library = [] {
    auto directories = std::vector<std::filesystem::path>();
    if(auto& defaults = get_default_directories()) {
      directories = *defaults;
    } else {
      auto bundled = get_bundled_font_directory();
      auto error = std::error_code();
      if(std::filesystem::is_directory(bundled, error)) {
        directories.push_back(std::move(bundled));
      }
    }
    for(auto& directory : get_system_font_directories()) {
      if(std::find(directories.begin(), directories.end(), directory) ==
          directories.end()) {
        directories.push_back(std::move(directory));
      }
    }
    return FontLibrary(std::move(directories));
  }();
  return library;
}

const char* Rellu::to_string(FontMatch match) {
  if(match == FontMatch::POSTSCRIPT_NAME) {
    return "postscript_name";
  } else if(match == FontMatch::FAMILY_AND_STYLE) {
    return "family_and_style";
  } else if(match == FontMatch::FAMILY) {
    return "family";
  } else if(match == FontMatch::SUBSTITUTE) {
    return "substitute";
  } else if(match == FontMatch::FALLBACK) {
    return "fallback";
  }
  return "none";
}
