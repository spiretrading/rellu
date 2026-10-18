#ifndef RELLU_TEXT_HARF_BUZZ_HPP
#define RELLU_TEXT_HARF_BUZZ_HPP
#include <filesystem>
#include <memory>
#include <optional>
#include <hb.h>
#include <hb-ot.h>
#include "Rellu/Text/Typeface.hpp"

namespace Rellu::Details {

  /**
   * Maps a font file into memory.
   * @param path The path of the font file.
   * @param owner Receives the object keeping the mapping alive.
   * @return A blob viewing the mapping, or a null pointer on failure.
   */
  hb_blob_t* map_font_file(
    const std::filesystem::path& path, std::shared_ptr<const void>& owner);

  /**
   * Reads the names of a face.
   * @param face The face.
   * @param font A font of the <i>face</i> with its variation selected.
   * @param instance The named instance selected, if any.
   */
  TypefaceNames read_names(
    hb_face_t* face, hb_font_t* font, std::optional<unsigned int> instance);
}

#endif
