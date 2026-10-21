#ifndef RELLU_LOCALIZATION_LOCALIZATION_HPP
#define RELLU_LOCALIZATION_LOCALIZATION_HPP
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace Rellu {

  /** Lists the languages the user interface is shown in. */
  enum class Language : unsigned char {

    /** English, the language the interface is written in. */
    ENGLISH,

    /** German. */
    GERMAN,

    /** Spanish. */
    SPANISH,

    /** French. */
    FRENCH,

    /** Italian. */
    ITALIAN,

    /** Japanese. */
    JAPANESE,

    /** Korean. */
    KOREAN,

    /** Brazilian Portuguese. */
    PORTUGUESE,

    /** Russian. */
    RUSSIAN,

    /** Simplified Chinese. */
    CHINESE
  };

  /** Lists every Language in the order Adobe XD lists them. */
  inline constexpr auto LANGUAGES = std::to_array({Language::GERMAN,
    Language::ENGLISH, Language::SPANISH, Language::FRENCH, Language::ITALIAN,
    Language::JAPANESE, Language::KOREAN, Language::PORTUGUESE,
    Language::RUSSIAN, Language::CHINESE});

  /**
   * Returns the name a language is stored under in the preferences, such as
   * <code>german</code>.
   * @param language The language.
   */
  std::string_view get_code(Language language);

  /**
   * Returns the language stored under a name in the preferences.
   * @param code The name, such as <code>german</code>.
   * @return The language, or <code>std::nullopt</code> if there is none.
   */
  std::optional<Language> parse_language(std::string_view code);

  /**
   * Returns a language's name in UTF-8 as Adobe XD lists it in its Language
   * menu.
   * @param language The language.
   */
  std::string_view get_native_name(Language language);

  /** Returns the language the user interface is shown in. */
  Language get_language();

  /**
   * Sets the language the user interface is shown in.
   * @param language The language.
   */
  void set_language(Language language);

  /**
   * Translates English user interface text into a language, falling back to
   * the text itself where there is no translation.
   * @param context The dot separated part of the interface showing the text,
   *        such as <code>inspector.appearance</code>, whose translation is
   *        preferred, then that of each enclosing part, then that of no part.
   * @param text The English text, in UTF-8.
   * @param language The language to translate into.
   * @return The translation, in UTF-8, referring to static storage or to
   *         <code>text</code>.
   */
  std::string_view localize(
    std::string_view context, std::string_view text, Language language);

  /**
   * Translates English user interface text into the language of the user
   * interface, falling back to the text itself where there is no
   * translation.
   * @param context The dot separated part of the interface showing the text.
   * @param text The English text, in UTF-8.
   * @return The translation, in UTF-8, referring to static storage or to
   *         <code>text</code>.
   */
  std::string_view localize(std::string_view context, std::string_view text);

  /**
   * Translates English user interface text shown in no particular part of
   * the interface into the language of the user interface.
   * @param text The English text, in UTF-8.
   * @return The translation, in UTF-8, referring to static storage or to
   *         <code>text</code>.
   */
  std::string_view localize(std::string_view text);

  /**
   * Returns the name Adobe XD gives in a language to a layer read with a
   * nameL10N key, the key's default name followed by the number its name
   * ends with.
   * @param name The layer's name, in UTF-8.
   * @param key XD's nameL10N key of the layer's kind, such as
   *        <code>SHAPE_RECTANGLE</code>.
   * @param language The language.
   * @return The name in UTF-8, <code>name</code> itself if XD has no string
   *         for <code>key</code>.
   */
  std::string localize_default_name(
    std::string_view name, std::string_view key, Language language);
}

#endif
