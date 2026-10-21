#include <algorithm>
#include <atomic>
#include <cstddef>
#include <tuple>
#include <unordered_map>
#include "Rellu/Localization/Localization.hpp"
#include "Translations.hpp"

using namespace Rellu;

namespace {
  constexpr auto APP_PREFIX = std::string_view("app:");
  constexpr auto NUMBER_SEPARATOR = std::string_view(" \xE2\x80\x93 ");
  std::atomic<Language> current_language = Language::ENGLISH;

  const Translation* find_translation(
      std::string_view context, std::string_view text) {
    auto translations = get_translations();
    auto key = std::tuple(context, text);
    auto i = std::lower_bound(translations.begin(), translations.end(), key,
      [] (const auto& translation, const auto& key) {
        return std::tuple(translation.m_context, translation.m_text) < key;
      });
    if(i == translations.end() || i->m_context != context ||
        i->m_text != text) {
      return nullptr;
    }
    return &*i;
  }

  const Translation* find_name_translation(std::string_view key) {
    static const auto names = [] {
      auto names = std::unordered_map<std::string_view, const Translation*>();
      for(auto& translation : get_translations()) {
        if((translation.m_context == "name" ||
            translation.m_context.starts_with("name.")) &&
            translation.m_id.starts_with(APP_PREFIX) &&
            translation.m_text.find('%') == std::string_view::npos) {
          names.emplace(
            translation.m_id.substr(APP_PREFIX.size()), &translation);
        }
      }
      return names;
    }();
    auto name = names.find(key);
    if(name == names.end()) {
      return nullptr;
    }
    return name->second;
  }

  std::size_t count_digits(std::string_view text) {
    return static_cast<std::size_t>(std::find_if(text.begin(), text.end(),
      [] (const auto& c) { return c < '0' || c > '9'; }) - text.begin());
  }

  bool is_number(std::string_view text) {
    if(!text.starts_with(' ')) {
      return false;
    }
    text.remove_prefix(1);
    auto digits = count_digits(text);
    if(digits == 0) {
      return false;
    } else if(digits == text.size()) {
      return true;
    }
    text.remove_prefix(digits);
    if(!text.starts_with(NUMBER_SEPARATOR)) {
      return false;
    }
    text.remove_prefix(NUMBER_SEPARATOR.size());
    digits = count_digits(text);
    return digits != 0 && digits == text.size();
  }

  std::string_view get_number(std::string_view name) {
    for(auto i = std::size_t(0); i != name.size(); ++i) {
      if(is_number(name.substr(i))) {
        return name.substr(i);
      }
    }
    return std::string_view();
  }
}

std::string_view Rellu::get_code(Language language) {
  switch(language) {
    case Language::GERMAN:
      return "german";
    case Language::SPANISH:
      return "spanish";
    case Language::FRENCH:
      return "french";
    case Language::ITALIAN:
      return "italian";
    case Language::JAPANESE:
      return "japanese";
    case Language::KOREAN:
      return "korean";
    case Language::PORTUGUESE:
      return "portuguese";
    case Language::RUSSIAN:
      return "russian";
    case Language::CHINESE:
      return "chinese";
    default:
      return "english";
  }
}

std::optional<Language> Rellu::parse_language(std::string_view code) {
  for(auto language : LANGUAGES) {
    if(get_code(language) == code) {
      return language;
    }
  }
  return std::nullopt;
}

std::string_view Rellu::get_native_name(Language language) {
  switch(language) {
    case Language::GERMAN:
      return "Deutsch";
    case Language::SPANISH:
      return "Espa\xC3\xB1ol";
    case Language::FRENCH:
      return "Fran\xC3\xA7" "ais";
    case Language::ITALIAN:
      return "Italiano";
    case Language::JAPANESE:
      return "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    case Language::KOREAN:
      return "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4";
    case Language::PORTUGUESE:
      return "Portugu\xC3\xAA" "s do Brasil";
    case Language::RUSSIAN:
      return "P\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9";
    case Language::CHINESE:
      return "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87";
    default:
      return "English";
  }
}

Language Rellu::get_language() {
  return current_language.load();
}

void Rellu::set_language(Language language) {
  current_language.store(language);
}

std::string_view Rellu::localize(
    std::string_view context, std::string_view text, Language language) {
  if(language == Language::ENGLISH) {
    return text;
  }
  auto index = static_cast<std::size_t>(language) - 1;
  while(true) {
    if(auto translation = find_translation(context, text)) {
      if(!translation->m_translations[index].empty()) {
        return translation->m_translations[index];
      }
    }
    if(context.empty()) {
      return text;
    }
    auto dot = context.rfind('.');
    if(dot == std::string_view::npos) {
      context = std::string_view();
    } else {
      context = context.substr(0, dot);
    }
  }
}

std::string_view Rellu::localize(
    std::string_view context, std::string_view text) {
  return localize(context, text, get_language());
}

std::string_view Rellu::localize(std::string_view text) {
  return localize(std::string_view(), text);
}

std::string Rellu::localize_default_name(
    std::string_view name, std::string_view key, Language language) {
  auto translation = find_name_translation(key);
  if(!translation) {
    return std::string(name);
  }
  auto prefix = translation->m_text;
  if(language != Language::ENGLISH) {
    auto& localized =
      translation->m_translations[static_cast<std::size_t>(language) - 1];
    if(!localized.empty()) {
      prefix = localized;
    }
  }
  return std::string(prefix) + std::string(get_number(name));
}
