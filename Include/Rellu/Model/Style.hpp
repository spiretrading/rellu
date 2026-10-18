#ifndef RELLU_MODEL_STYLE_HPP
#define RELLU_MODEL_STYLE_HPP
#include <optional>
#include <variant>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Gradient.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Geometry/Stroke.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** A fill that paints nothing. */
  struct NoFill {
    bool operator ==(const NoFill&) const = default;
  };

  /** A fill of a single color. */
  struct SolidFill {

    /** The color painted. */
    Color m_color = Color(0, 0, 0, 1);

    /** The swatch the color is linked to, or nil if it is unlinked. */
    SwatchId m_swatch;

    bool operator ==(const SolidFill&) const = default;
  };

  /** Lists the coordinate systems a gradient's geometry is expressed in. */
  enum class GradientUnits : unsigned char {

    /** Fractions of the bounding box of the painted node. */
    OBJECT_BOUNDING_BOX,

    /** The painted node's local space. */
    USER_SPACE
  };

  /** A fill blending between colors. */
  struct GradientFill {

    /** The geometry of the gradient. */
    GradientGeometry m_geometry;

    /** The colors in increasing offset order. */
    std::vector<GradientStop> m_stops;

    /** The coordinate system of the geometry. */
    GradientUnits m_units = GradientUnits::OBJECT_BOUNDING_BOX;

    /** A transform applied to the geometry. */
    Matrix m_transform;

    bool operator ==(const GradientFill&) const = default;
  };

  /** Lists the ways an image is fit to the shape it fills. */
  enum class ScaleBehavior : unsigned char {

    /** Scales the image to the shape's bounds. */
    FILL,

    /** Scales the image uniformly so that it covers the shape's bounds. */
    COVER
  };

  /** A fill painting an image resource. */
  struct ImageFill {

    /** The image painted. */
    ImageId m_image;

    /** The size of the image, in pixels. */
    Size m_size = Size(0, 0);

    /** How the image is fit to the shape. */
    ScaleBehavior m_scale_behavior = ScaleBehavior::FILL;

    /** The displacement of the image within the shape. */
    Point m_offset = Point(0, 0);

    /** The uniform scale applied to the image. */
    double m_scale = 1;

    /** The horizontal scale applied to the image. */
    double m_horizontal_scale = 1;

    /** The vertical scale applied to the image. */
    double m_vertical_scale = 1;

    /** Whether the image is mirrored horizontally. */
    bool m_is_flipped_horizontally = false;

    /** Whether the image is mirrored vertically. */
    bool m_is_flipped_vertically = false;

    bool operator ==(const ImageFill& fill) const;
  };

  /** The paint filling the interior of a node. */
  using Fill = std::variant<NoFill, SolidFill, GradientFill, ImageFill>;

  /** The line painted along a node's outline. */
  struct Stroke {

    /** The color painted. */
    Color m_color = Color(0, 0, 0, 1);

    /** The width of the line. */
    double m_width = 1;

    /** Where the line lies relative to the outline. */
    StrokeAlignment m_alignment = StrokeAlignment::CENTER;

    /** The shape at the ends of open lines and dashes. */
    StrokeCap m_cap = StrokeCap::BUTT;

    /** The shape where segments meet. */
    StrokeJoin m_join = StrokeJoin::MITER;

    /** The ratio of miter length to width beyond which miters are beveled. */
    double m_miter_limit = 4;

    /** Alternating dash and gap lengths, empty for a solid line. */
    std::vector<double> m_dashes;

    /** Whether the stroke is drawn. */
    bool m_is_enabled = true;

    bool operator ==(const Stroke& stroke) const;
  };

  /** A shadow cast outside a node's outline. */
  struct DropShadow {

    /** The displacement of the shadow. */
    Point m_offset = Point(0, 0);

    /** The blur radius. */
    double m_blur = 0;

    /** The color of the shadow. */
    Color m_color = Color(0, 0, 0, 1);

    /** Whether the shadow is drawn. */
    bool m_is_visible = true;

    bool operator ==(const DropShadow& shadow) const;
  };

  /** A shadow cast inside a node's outline. */
  struct InnerShadow {

    /** The displacement of the shadow. */
    Point m_offset = Point(0, 0);

    /** The blur radius. */
    double m_blur = 0;

    /** The color of the shadow. */
    Color m_color = Color(0, 0, 0, 1);

    /** Whether the shadow is drawn. */
    bool m_is_visible = true;

    bool operator ==(const InnerShadow& shadow) const;
  };

  /** A blur of a node or of what lies behind it. */
  struct Blur {

    /** The blur radius. */
    double m_amount = 0;

    /** The brightness adjustment applied to the blurred pixels. */
    double m_brightness = 0;

    /** The opacity of the node's fill over a background blur. */
    double m_fill_opacity = 1;

    /** Whether the backdrop is blurred rather than the node itself. */
    bool m_is_background = false;

    /** Whether the blur is applied. */
    bool m_is_visible = true;

    bool operator ==(const Blur& blur) const;
  };

  /** A visual effect applied to a node. */
  using Effect = std::variant<DropShadow, InnerShadow, Blur>;

  /** The paint and effects of a shape, text or artboard. */
  struct Style {

    /** The fill. */
    Fill m_fill;

    /** The stroke, if any. */
    std::optional<Stroke> m_stroke;

    /** The effects in the order they apply. */
    std::vector<Effect> m_effects;

    bool operator ==(const Style&) const = default;
  };

  inline bool ImageFill::operator ==(const ImageFill& fill) const {
    return m_image == fill.m_image && m_size == fill.m_size &&
      m_scale_behavior == fill.m_scale_behavior &&
      m_offset == fill.m_offset && equals(m_scale, fill.m_scale) &&
      equals(m_horizontal_scale, fill.m_horizontal_scale) &&
      equals(m_vertical_scale, fill.m_vertical_scale) &&
      m_is_flipped_horizontally == fill.m_is_flipped_horizontally &&
      m_is_flipped_vertically == fill.m_is_flipped_vertically;
  }

  inline bool Stroke::operator ==(const Stroke& stroke) const {
    return m_color == stroke.m_color && equals(m_width, stroke.m_width) &&
      m_alignment == stroke.m_alignment && m_cap == stroke.m_cap &&
      m_join == stroke.m_join &&
      equals(m_miter_limit, stroke.m_miter_limit) &&
      equals(m_dashes, stroke.m_dashes) &&
      m_is_enabled == stroke.m_is_enabled;
  }

  inline bool DropShadow::operator ==(const DropShadow& shadow) const {
    return m_offset == shadow.m_offset && equals(m_blur, shadow.m_blur) &&
      m_color == shadow.m_color && m_is_visible == shadow.m_is_visible;
  }

  inline bool InnerShadow::operator ==(const InnerShadow& shadow) const {
    return m_offset == shadow.m_offset && equals(m_blur, shadow.m_blur) &&
      m_color == shadow.m_color && m_is_visible == shadow.m_is_visible;
  }

  inline bool Blur::operator ==(const Blur& blur) const {
    return equals(m_amount, blur.m_amount) &&
      equals(m_brightness, blur.m_brightness) &&
      equals(m_fill_opacity, blur.m_fill_opacity) &&
      m_is_background == blur.m_is_background &&
      m_is_visible == blur.m_is_visible;
  }
}

#endif
