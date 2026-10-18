#ifndef RELLU_MODEL_RESOURCES_HPP
#define RELLU_MODEL_RESOURCES_HPP
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Model/Component.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Shared.hpp"
#include "Rellu/Model/Style.hpp"
#include "Rellu/Model/Text.hpp"

namespace Rellu {

  /** An encoded image embedded in a document. */
  struct ImageResource {

    /** The image's identifier. */
    ImageId m_id;

    /** The media type of the encoding, such as image/png. */
    std::string m_media_type;

    /** The encoded bytes. */
    Shared<std::vector<std::byte>> m_data;

    bool operator ==(const ImageResource&) const = default;
  };

  /** A named color in the document's library. */
  struct ColorSwatch {

    /** The swatch's identifier. */
    SwatchId m_id;

    /** The swatch's name, empty if unnamed. */
    std::string m_name;

    /** The color. */
    Color m_color = Color(0, 0, 0, 1);

    /** The stops of a gradient swatch, or empty for a plain color. */
    std::vector<GradientStop> m_gradient;

    bool operator ==(const ColorSwatch&) const = default;
  };

  /** A named text style in the document's library. */
  struct CharacterStyle {

    /** The style's identifier. */
    CharacterStyleId m_id;

    /** The style's name. */
    std::string m_name;

    /** The character level styling. */
    TextStyle m_style;

    /** The distance between baselines, or none for automatic spacing. */
    std::optional<double> m_line_height;

    /** The extra space after each paragraph. */
    double m_paragraph_spacing = 0;

    bool operator ==(const CharacterStyle&) const = default;
  };

  /** The shared assets of a document. */
  struct Resources {

    /** The components by id. */
    std::map<ComponentId, Component> m_components;

    /** The images by id. */
    std::map<ImageId, ImageResource> m_images;

    /** The color swatches in library order. */
    std::vector<ColorSwatch> m_swatches;

    /** The character styles in library order. */
    std::vector<CharacterStyle> m_character_styles;

    bool operator ==(const Resources&) const = default;
  };
}

#endif
