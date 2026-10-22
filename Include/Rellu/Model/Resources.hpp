#ifndef RELLU_MODEL_RESOURCES_HPP
#define RELLU_MODEL_RESOURCES_HPP
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Model/Artboard.hpp"
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

  /** A named group of assets in the document's library. */
  struct AssetGroup {

    /** The group's identifier. */
    std::string m_id;

    /** The group's name. */
    std::string m_name;

    /** The key ordering the group among its parent's members. */
    std::string m_order;

    /** The id of the parent group, or of the section, such as groupColors. */
    std::string m_parent;

    /** The ids of the groups within the group. */
    std::vector<std::string> m_children;

    /** The ids of the assets within the group. */
    std::vector<std::string> m_elements;

    bool operator ==(const AssetGroup&) const = default;
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

    /** The places of the swatch among the groups of assets. */
    std::vector<AssetPlacement> m_placements;

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

    /** The places of the style among the groups of assets. */
    std::vector<AssetPlacement> m_placements;

    bool operator ==(const CharacterStyle& style) const;
  };

  /** Lists the color profiles a document's colors are managed with. */
  enum class ColorProfile : unsigned char {

    /** The document records no profile, which Adobe XD treats as UNMANAGED. */
    NONE,

    /** Colors are not managed. */
    UNMANAGED,

    /** Colors are managed as sRGB. */
    SRGB
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

    /** The groups of assets in library order. */
    std::vector<AssetGroup> m_asset_groups;

    /** The grids given to artboards. */
    GridDefaults m_grid_defaults;

    /** The color profile the document's colors are managed with. */
    ColorProfile m_color_profile = ColorProfile::NONE;

    bool operator ==(const Resources&) const = default;
  };

  inline bool CharacterStyle::operator ==(const CharacterStyle& style) const {
    return m_id == style.m_id && m_name == style.m_name &&
      m_style == style.m_style &&
      equals(m_line_height, style.m_line_height) &&
      equals(m_paragraph_spacing, style.m_paragraph_spacing) &&
      m_placements == style.m_placements;
  }
}

#endif
