#ifndef RELLU_TEXT_FONT_LIBRARY_HPP
#define RELLU_TEXT_FONT_LIBRARY_HPP
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Rellu/Model/Text.hpp"
#include "Rellu/Text/Typeface.hpp"

namespace Rellu {

  /** A face found by a FontLibrary. */
  struct FontFace {

    /** The path of the font file. */
    std::filesystem::path m_path;

    /** The index of the face within the font file. */
    unsigned int m_index = 0;

    /** The named instance of a variable font, or none for its default. */
    std::optional<unsigned int> m_instance;

    /** The names identifying the face. */
    TypefaceNames m_names;
  };

  /** Lists the ways a Font is matched to a face. */
  enum class FontMatch : unsigned char {

    /** A face has the requested PostScript name. */
    POSTSCRIPT_NAME,

    /** A face has the requested family and style. */
    FAMILY_AND_STYLE,

    /**
     * A face of the requested family has the nearest weight and slant to the
     * requested style.
     */
    FAMILY,

    /** A face of the family designated to replace the requested one. */
    SUBSTITUTE,

    /** No face of the requested family exists, so a fallback face is used. */
    FALLBACK,

    /** No face is available at all. */
    NONE
  };

  /** The face chosen to display a Font. */
  struct FontResolution {

    /** The face, or a null pointer if no face is available. */
    std::shared_ptr<const Typeface> m_typeface;

    /** How the face was matched. */
    FontMatch m_match = FontMatch::NONE;
  };

  /** A face displaying characters missing from a font. */
  struct FontFallback {

    /** The face, or a null pointer if no face maps the character. */
    std::shared_ptr<const Typeface> m_typeface;

    /**
     * Whether the face stands in for a bold weight its family lacks, which
     * widens each glyph's advance by a fiftieth of an em.
     */
    bool m_is_emboldened = false;
  };

  /**
   * Finds and loads the faces in a list of font directories. A Font resolves
   * by PostScript name first, then by family and style, then by the face of
   * the same family nearest in weight and slant. As in Adobe XD on Windows, a
   * family without faces is replaced by Arial if it is a Helvetica family or
   * Arial MT, and otherwise by the first available of Segoe UI, Arial and
   * Roboto, then any face. A FontLibrary is safe to use from multiple
   * threads.
   */
  class FontLibrary {
    public:

      /**
       * Constructs a FontLibrary.
       * @param directories The directories searched recursively for font
       *        files, in order of precedence.
       */
      explicit FontLibrary(std::vector<std::filesystem::path> directories);

      /** Returns the directories searched, in order of precedence. */
      const std::vector<std::filesystem::path>& get_directories() const;

      /** Returns every face found, in order of precedence. */
      std::vector<FontFace> get_faces() const;

      /**
       * Resolves a Font to a face.
       * @param font The font to display.
       * @return The face displaying the <i>font</i>.
       */
      FontResolution resolve(const Font& font) const;

      /**
       * Finds a face to display a character missing from a face, the one
       * the system's font fallback chooses where there is one, as Adobe XD
       * does on Windows, and otherwise the face nearest in weight and slant
       * of the first family in a fallback chain mapping the character.
       * @param character The Unicode code point.
       * @param names The names of the face missing the <i>character</i>.
       * @return The face displaying the <i>character</i>.
       */
      FontFallback find_fallback(
        char32_t character, const TypefaceNames& names) const;

    private:
      std::vector<std::filesystem::path> m_directories;
      mutable std::mutex m_mutex;
      mutable bool m_is_scanned;
      mutable std::vector<FontFace> m_faces;
      mutable std::unordered_map<std::string, std::vector<std::size_t>>
        m_postscript_names;
      mutable std::unordered_map<std::string, std::vector<std::size_t>>
        m_families;
      mutable std::vector<std::shared_ptr<const Typeface>> m_typefaces;
      mutable std::map<std::tuple<std::string, std::string, std::string>,
        FontResolution> m_resolutions;
      mutable std::map<std::pair<double, bool>, std::vector<FontFallback>>
        m_fallbacks;
      mutable std::map<std::tuple<char32_t, std::string, double, bool>,
        FontFallback> m_system_fallbacks;
      mutable std::map<std::pair<std::filesystem::path, unsigned int>,
        std::shared_ptr<const Typeface>> m_system_faces;

      FontLibrary(const FontLibrary&) = delete;
      FontLibrary& operator =(const FontLibrary&) = delete;
      void scan() const;
      void add(FontFace face) const;
      std::shared_ptr<const Typeface> load(std::size_t index) const;
      std::shared_ptr<const Typeface> load(
        const std::filesystem::path& path, unsigned int index) const;
      std::optional<std::size_t> find_face(const std::string& family,
        const std::string& style, bool is_nearest) const;
      std::optional<std::size_t> find_nearest(
        const std::vector<std::size_t>& faces, double weight,
        bool is_italic) const;
      FontResolution find(const Font& font) const;
  };

  /** Returns the system's font directories, in order of precedence. */
  std::vector<std::filesystem::path> get_system_font_directories();

  /**
   * Sets the directories the default FontLibrary searches before the
   * system's font directories, which must be done before the first call to
   * get_default_font_library.
   * @param directories The directories, in order of precedence.
   */
  void set_default_font_directories(
    std::vector<std::filesystem::path> directories);

  /**
   * Returns the FontLibrary shared by default, created when first called. It
   * searches the directories given to set_default_font_directories, if any,
   * followed by the system's font directories.
   */
  const FontLibrary& get_default_font_library();

  /**
   * Returns the name of a FontMatch.
   * @param match The FontMatch to name.
   */
  const char* to_string(FontMatch match);
}

#endif
