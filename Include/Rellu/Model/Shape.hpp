#ifndef RELLU_MODEL_SHAPE_HPP
#define RELLU_MODEL_SHAPE_HPP
#include <optional>
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /** A video shown in a shape's bounds. */
  struct Video {

    /** The image shown while the video is not playing. */
    ImageFill m_poster;

    /** The id of the resource holding the video's media file. */
    ImageId m_media;

    /** Whether playback starts over when it reaches the end. */
    bool m_is_looping = false;

    bool operator ==(const Video&) const = default;
  };

  /** The payload of a shape node. */
  struct Shape {

    /** The outline. */
    ShapeGeometry m_geometry;

    /** The paint and effects. */
    Style m_style;

    /** The video the shape shows in its bounds, if any. */
    std::optional<Video> m_video;

    bool operator ==(const Shape&) const = default;
  };
}

#endif
