#ifndef RELLU_TEXT_SYSTEM_FONT_FALLBACK_HPP
#define RELLU_TEXT_SYSTEM_FONT_FALLBACK_HPP
#include <filesystem>
#include <optional>
#include <string>

namespace Rellu::Details {

  /** A face chosen by the system to display a character. */
  struct SystemFace {

    /** The path of the font file. */
    std::filesystem::path m_path;

    /** The index of the face within the font file. */
    unsigned int m_index = 0;

    /** Whether the system emboldens the face to reach the weight. */
    bool m_is_emboldened = false;
  };

  /**
   * Finds the face the system's font fallback displays a character with.
   * @param character The Unicode code point.
   * @param family The family of the font missing the character.
   * @param weight The weight of the font, in the range [1, 1000].
   * @param is_italic Whether the font is italic.
   * @return The face, or none where the system has no font fallback or no
   *         face maps the <i>character</i>.
   */
  std::optional<SystemFace> find_system_fallback(char32_t character,
    const std::string& family, double weight, bool is_italic);
}

#endif
