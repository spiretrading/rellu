#ifndef RELLU_IO_XD_PREVIEW_HPP
#define RELLU_IO_XD_PREVIEW_HPP
#include <optional>
#include <QImage>
#include <QSize>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** The largest width and height in pixels of a package's preview. */
  inline constexpr auto XD_PREVIEW_SIZE = 2048;

  /** The largest width and height in pixels of a package's thumbnail. */
  inline constexpr auto XD_THUMBNAIL_SIZE = 512;

  /** Stores what the images an Adobe XD package keeps of it depict. */
  struct XdPreviewSource {

    /** The artboard depicted, or nil for the pasteboard's content. */
    NodeId m_artboard;

    /** The region depicted, in document coordinates. */
    Rect m_bounds;

    bool operator ==(const XdPreviewSource&) const = default;
  };

  /** Stores the images an Adobe XD package keeps of its content. */
  struct XdPreviews {

    /** The preview, at most XD_PREVIEW_SIZE pixels wide and high. */
    QImage m_preview;

    /** The thumbnail, at most XD_THUMBNAIL_SIZE pixels wide and high. */
    QImage m_thumbnail;
  };

  /** The width and height in pixels of the images of a blank document. */
  inline constexpr auto XD_BLANK_PREVIEW_SIZE = 512;

  /**
   * Returns what Adobe XD's preview of a document depicts: the topmost of
   * the artboards starting a flow, the leftmost of those at the same
   * height, else the topmost artboard, else the drawn content of the
   * pasteboard.
   * @param document The document.
   * @param measurer The function measuring the region text paints.
   * @return The source, or none if the document draws nothing.
   */
  std::optional<XdPreviewSource> get_xd_preview_source(
    const Document& document,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Returns the size in pixels of an image of a region scaled down to fit a
   * limit, never scaled up.
   * @param bounds The region.
   * @param limit The largest width and height in pixels.
   */
  QSize get_xd_preview_size(const Rect& bounds, int limit);

  /**
   * Renders the preview and thumbnail an Adobe XD package keeps of a
   * document, opaque over white, or opaque white images
   * XD_BLANK_PREVIEW_SIZE pixels square for a document drawing nothing or
   * when rendering fails.
   * @param document The document.
   * @param measurer The function measuring the region text paints.
   * @return The images.
   */
  XdPreviews render_xd_previews(const Document& document,
    const TextMeasurer& measurer = estimate_text_bounds);
}

#endif
