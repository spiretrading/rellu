#ifndef RELLU_LOCALIZATION_TRANSLATIONS_HPP
#define RELLU_LOCALIZATION_TRANSLATIONS_HPP
#include <array>
#include <span>
#include <string_view>

namespace Rellu {

  /** The translations of English user interface text taken from Adobe XD. */
  struct Translation {

    /** The part of the interface the translations apply to, or empty. */
    std::string_view m_context;

    /** The English text. */
    std::string_view m_text;

    /** The id of Adobe XD's string the translations are taken from. */
    std::string_view m_id;

    /**
     * The translations in UTF-8, one for each language after English in the
     * order Language declares them, empty where there is none.
     */
    std::array<std::string_view, 9> m_translations;

    /**
     * Constructs a Translation.
     * @param context The part of the interface the translations apply to.
     * @param text The English text.
     * @param id The id of Adobe XD's string.
     * @param translations The translations.
     */
    constexpr Translation(std::string_view context, std::string_view text,
      std::string_view id,
      const std::array<std::string_view, 9>& translations) noexcept;
  };

  constexpr Translation::Translation(std::string_view context,
    std::string_view text, std::string_view id,
    const std::array<std::string_view, 9>& translations) noexcept
    : m_context(context),
      m_text(text),
      m_id(id),
      m_translations(translations) {}

  /** Returns every Translation, ordered by context and then by text. */
  std::span<const Translation> get_translations();
}

#endif
