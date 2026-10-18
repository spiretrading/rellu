#ifndef RELLU_SCENE_PAINT_CONVERTER_HPP
#define RELLU_SCENE_PAINT_CONVERTER_HPP
#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <QImage>
#include <QSize>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Style.hpp"
#include "Rellu/Rendering/DrawList.hpp"

namespace Rellu {

  /**
   * Converts the fills of a Document into Paints, decoding each image
   * resource once.
   */
  class PaintConverter {
    public:

      /**
       * Constructs a PaintConverter.
       * @param document The document whose image resources are painted,
       *        which must outlive this PaintConverter.
       */
      explicit PaintConverter(const Document& document);

      /**
       * Converts a fill.
       * @param fill The fill to convert.
       * @param outline The outline filled, in its own coordinates.
       * @return The paint, or none if the fill paints nothing.
       */
      std::optional<Paint> convert(const Fill& fill, const Path& outline);

      /**
       * Returns the decoded image of a resource.
       * @param id The resource's id.
       * @return The image, or null if there is no such resource or it can
       *         not be decoded.
       */
      std::shared_ptr<const QImage> get_image(const ImageId& id);

    private:
      struct Entry {
        std::shared_ptr<const std::vector<std::byte>> m_data;
        std::shared_ptr<const QImage> m_image;
      };
      const Document* m_document;
      std::unordered_map<ImageId, Entry> m_images;

      PaintConverter(const PaintConverter&) = delete;
      PaintConverter& operator =(const PaintConverter&) = delete;
  };

  /**
   * Returns the transform from an image fill's image to the coordinates of
   * the region it fills, the way Adobe XD places it: stretched to the
   * region's bounds, or scaled uniformly to cover them and centered, then
   * scaled, displaced and mirrored as the fill specifies.
   * @param fill The image fill.
   * @param bounds The bounds of the region filled.
   * @param size The size in pixels of the decoded image.
   * @return The transform from the decoded image's pixel coordinates.
   */
  Matrix get_image_transform(
    const ImageFill& fill, const Rect& bounds, QSize size);
}

#endif
