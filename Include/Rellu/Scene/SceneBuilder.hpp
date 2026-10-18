#ifndef RELLU_SCENE_SCENE_BUILDER_HPP
#define RELLU_SCENE_SCENE_BUILDER_HPP
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Scene/PaintConverter.hpp"
#include "Rellu/Text/TextLayout.hpp"
#include "Rellu/Text/TextRasterizer.hpp"

namespace Rellu {

  /**
   * Converts the nodes of a Document into DrawLists in document coordinates,
   * reusing the outlines of shapes left unchanged since an earlier
   * conversion.
   */
  class SceneBuilder {
    public:

      /**
       * Constructs a SceneBuilder.
       * @param document The document to convert, which must outlive this
       *        SceneBuilder.
       * @param tolerance The largest distance in document units between a
       *        curve and the lines approximating it.
       */
      SceneBuilder(const Document& document, double tolerance);

      /**
       * Returns the largest distance in document units between a curve and
       * the lines approximating it.
       */
      double get_tolerance() const;

      /**
       * Sets the largest distance in document units between a curve and the
       * lines approximating it.
       * @param tolerance The positive tolerance.
       */
      void set_tolerance(double tolerance);

      /**
       * Returns the transform from document coordinates to the device pixels
       * that text is rasterized for, if any.
       */
      const std::optional<Matrix>& get_device_transform() const;

      /**
       * Sets the transform from document coordinates to the device pixels
       * that text is rasterized for, in which case text whose transform to
       * device pixels is supported by TextRasterizer is drawn as pixel
       * aligned coverage the way Adobe XD draws it, and other text as
       * outlines.
       * @param transform The transform to device pixels, or none to draw all
       *        text as outlines.
       */
      void set_device_transform(const std::optional<Matrix>& transform);

      /** Converts every node on the canvas, in z order. */
      DrawList build();

      /**
       * Converts a node and its descendants as they appear within their
       * ancestors.
       * @param id The node's id.
       * @return The node's drawing, empty if there is no such node.
       */
      DrawList build(const NodeId& id);

    private:
      struct Placement {
        Matrix m_transform;
        double m_opacity;
        std::optional<Rect> m_clip;
      };
      struct Entry {
        const Node* m_source;
        const std::vector<NodeId>* m_children;
        Placement m_placement;
      };
      struct Outline {
        std::shared_ptr<const Node> m_record;
        Path m_path;
        Path m_stroke;
        double m_tolerance;
        std::uint64_t m_generation;
      };
      struct TextOutline {
        std::shared_ptr<const Node> m_record;
        TextLayout m_layout;
        std::vector<TextFill> m_fills;
        std::vector<std::pair<Matrix, std::vector<TextRaster>>> m_rasters;
        std::uint64_t m_generation;
      };
      const Document* m_document;
      double m_tolerance;
      std::optional<Matrix> m_device_transform;
      std::uint64_t m_generation;
      std::unordered_map<NodeId, Outline> m_outlines;
      PaintConverter m_paints;
      std::unordered_map<NodeId, TextOutline> m_texts;

      SceneBuilder(const SceneBuilder&) = delete;
      SceneBuilder& operator =(const SceneBuilder&) = delete;
      std::optional<Entry> enter(const Node& node, const Placement& parent);
      std::optional<Placement> get_placement(const NodeId& id, int depth);
      void add_node(const NodeId& id, const Placement& parent, int depth,
        DrawList& list);
      void add_artboard(const Node& node, const Placement& placement,
        DrawList& list);
      void add_shape(const Node& node, const Placement& placement,
        DrawList& list);
      Outline& get_outline(const Node& node, double scale);
      void add_text(const Node& node, const Placement& placement,
        DrawList& list);
      TextOutline& get_text_outline(const Node& node);
      const std::vector<TextRaster>& get_rasters(
        TextOutline& outline, const Matrix& transform);
  };
}

#endif
