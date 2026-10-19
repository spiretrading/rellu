#ifndef RELLU_SCENE_SCENE_BUILDER_HPP
#define RELLU_SCENE_SCENE_BUILDER_HPP
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Projection.hpp"
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
   * Records what a conversion depends on beyond the node converted and its
   * descendants.
   */
  struct SceneDependencies {

    /**
     * The ids of the nodes and images whose change, along with any change
     * within the tree of a listed node, may change the conversion.
     */
    std::vector<NodeId> m_keys;

    /** The smallest tolerance producing the same conversion. */
    double m_minimum_tolerance = 0;

    /** The largest tolerance producing the same conversion. */
    double m_maximum_tolerance = 0;
  };

  /**
   * Converts the nodes of a Document into DrawLists in document coordinates,
   * reusing the outlines of shapes left unchanged since an earlier
   * conversion and sharing them with the draw lists. Conversions made with
   * build_lists are kept, so that converting again after a change converts
   * only what the change affects.
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

      /**
       * Converts every node on the canvas, in z order, discarding what is
       * cached of nodes it does not reach.
       */
      DrawList build();

      /**
       * Converts a node and its descendants as they appear within their
       * ancestors.
       * @param id The node's id.
       * @return The node's drawing, empty if there is no such node.
       */
      DrawList build(const NodeId& id);

      /**
       * Converts a node and its descendants as they appear within their
       * ancestors, recording what the conversion depends on.
       * @param id The node's id.
       * @param dependencies Receives what the conversion depends on.
       * @return The node's drawing, empty if there is no such node.
       */
      DrawList build(const NodeId& id, SceneDependencies& dependencies);

      /**
       * Converts a node and its descendants as they appear within their
       * ancestors into draw lists that together draw what build converts the
       * node into, reusing the draw lists kept by earlier calls for the
       * subtrees that no change reported through invalidate has affected
       * since. Every change to the document that affects its drawing must be
       * reported through invalidate before the next call.
       * @param id The node's id.
       * @param dependencies Receives what the conversion depends on.
       * @return The draw lists in z order, none if there is no such node.
       */
      std::vector<std::shared_ptr<const DrawList>> build_lists(
        const NodeId& id, SceneDependencies& dependencies);

      /**
       * Reports a change to a node or an image, discarding what build_lists
       * keeps of the conversions the change affects.
       * @param id The id of the node changed, inserted or removed, or of the
       *        image added or removed.
       */
      void invalidate(const NodeId& id);

      /**
       * Reports a change to a node's transform alone. Where the change moves
       * the node by a translation that its kept draw lists can be moved by
       * exactly, build_lists reuses them moved; otherwise the change is
       * treated as invalidate treats it.
       * @param id The id of the node whose transform changed.
       */
      void invalidate_transform(const NodeId& id);

      /**
       * Discards what is cached of a node, as when it is removed.
       * @param id The node's id.
       */
      void discard(const NodeId& id);

    private:
      struct Placement {
        Matrix m_transform;
        double m_opacity;
        std::optional<Rect> m_clip;
        std::optional<Projection> m_projection;

        bool operator ==(const Placement&) const = default;
      };
      struct Entry {
        const Node* m_source;
        const std::vector<NodeId>* m_children;
        Placement m_placement;
      };
      struct Outline {
        std::shared_ptr<const Node> m_record;
        std::shared_ptr<const Path> m_path;
        std::shared_ptr<const Path> m_line;
        std::shared_ptr<const Path> m_stroke;
        std::shared_ptr<const Path> m_layered;
        double m_tolerance;
        std::uint64_t m_generation;
      };
      struct Background {
        std::shared_ptr<const Node> m_record;
        std::shared_ptr<const Path> m_path;
        std::uint64_t m_generation;
      };
      struct MaskOutline {
        std::shared_ptr<const Node> m_record;
        Matrix m_transform;
        double m_tolerance;
        std::optional<LayerMask> m_mask;
        std::optional<Rect> m_clip;
        std::uint64_t m_generation;
      };
      struct TextOutline {
        std::shared_ptr<const Node> m_record;
        TextLayout m_layout;
        std::vector<TextFill> m_fills;
        std::vector<std::shared_ptr<const Path>> m_paths;
        std::vector<std::pair<Matrix, std::vector<TextRaster>>> m_rasters;
        Matrix m_mirror;
        std::uint64_t m_generation;
      };
      struct Segment {
        std::size_t m_begin;
        std::size_t m_end;
        bool m_is_retained;
        bool m_is_stale;
        std::vector<std::shared_ptr<const DrawList>> m_lists;
        std::size_t m_size;
        SceneDependencies m_dependencies;
        double m_tolerance;
      };
      struct Subtree {
        Placement m_parent;
        Placement m_placement;
        int m_depth;
        bool m_is_stale;
        bool m_is_moved;
        bool m_is_surveyed;
        bool m_is_rigid;
        std::vector<std::vector<NodeId>> m_clips;
        double m_tolerance;
        SceneDependencies m_dependencies;
        std::vector<Segment> m_segments;
        std::vector<std::shared_ptr<const DrawList>> m_lists;
        std::size_t m_size;
        SceneDependencies m_total;
      };
      const Document* m_document;
      double m_tolerance;
      std::optional<Matrix> m_device_transform;
      std::uint64_t m_generation;
      std::unordered_map<NodeId, Outline> m_outlines;
      std::unordered_map<NodeId, Background> m_backgrounds;
      std::unordered_map<NodeId, MaskOutline> m_masks;
      PaintConverter m_paints;
      std::unordered_map<NodeId, TextOutline> m_texts;
      SceneDependencies* m_dependencies;
      std::unordered_map<NodeId, NodeId> m_roots;
      bool m_is_retaining;
      std::unordered_map<NodeId, Subtree> m_subtrees;
      std::unordered_set<NodeId> m_large;
      std::unordered_map<NodeId, std::unordered_set<NodeId>> m_dependents;

      SceneBuilder(const SceneBuilder&) = delete;
      SceneBuilder& operator =(const SceneBuilder&) = delete;
      std::optional<Entry> enter(const Node& node, const Placement& parent);
      std::optional<Placement> get_placement(const NodeId& id, int depth);
      void add_node(const NodeId& id, const Placement& parent, int depth,
        DrawList& list);
      void add_entry(const Node& node, const Entry& entry, int depth,
        DrawList& list);
      std::optional<LayerMask> get_group_mask(
        const Node& source, Placement& placement);
      void add_composites(const Node& source, const Placement& placement,
        const std::optional<Rect>& clip, std::optional<LayerMask> mask,
        std::size_t first, DrawList& list);
      void retain(const NodeId& id, const Placement& parent, int depth,
        Segment& segment);
      void add_segments(const Node& source, const std::vector<NodeId>& children,
        bool is_own, std::size_t begin, std::size_t end, std::size_t limit,
        const Placement& placement, int depth, std::vector<Segment>& segments);
      bool replace(const NodeId& id, Subtree& tree, const Placement& parent);
      void survey(const Node& node, Subtree& tree) const;
      void report(const NodeId& id, bool is_moved);
      void forget(const NodeId& id);
      void mark_ancestors(const Node& node);
      void invalidate_dependents(const NodeId& key);
      void clear_retained();
      void add_dependencies(const Node& reference);
      void add_tolerance(double minimum, double maximum);
      std::optional<Paint> convert(const Fill& fill, const Path& outline);
      std::optional<LayerMask> get_mask(const Node& node,
        const Matrix& transform, std::optional<Rect>& clip);
      std::optional<LayerMask> get_mask(
        const Mask& mask, const Placement& placement);
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
