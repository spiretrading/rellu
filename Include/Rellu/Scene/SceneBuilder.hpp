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
      const Document* m_document;
      double m_tolerance;
      std::uint64_t m_generation;
      std::unordered_map<NodeId, Outline> m_outlines;

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
  };
}

#endif
