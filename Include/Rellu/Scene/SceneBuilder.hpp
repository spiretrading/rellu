#ifndef RELLU_SCENE_SCENE_BUILDER_HPP
#define RELLU_SCENE_SCENE_BUILDER_HPP
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
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

    /**
     * Whether the conversion draws text whose drawing depends on the
     * transform to device pixels.
     */
    bool m_is_device_dependent = false;

    /**
     * The transform to device pixels the conversion's text was drawn for,
     * none if it was drawn as outlines.
     */
    std::optional<Matrix> m_device_transform;

    /** Whether the conversion draws text rasterized into device pixels. */
    bool m_is_rasterized = false;

    /**
     * Whether parts of the conversion's text were drawn for different
     * transforms to device pixels.
     */
    bool m_is_device_mixed = false;
  };

  /** A region of a text node's drawing that a conversion fills as outlines. */
  struct TextRegion {

    /** The text node. */
    NodeId m_id;

    /**
     * The node's glyphs and decorations in the coordinates of the region's
     * path, valid until the next conversion.
     */
    const TextLayout* m_layout;

    /** The color the region is filled with. */
    Color m_color;
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
       * outlines, and shapes outside of masks and scroll groups are placed
       * in single precision as Adobe XD places them.
       * @param transform The transform to device pixels, or none to draw all
       *        text as outlines and place shapes exactly.
       */
      void set_device_transform(const std::optional<Matrix>& transform);

      /**
       * Returns the transform from document coordinates to the device pixels
       * that text alone is rasterized for while no device transform is set,
       * if any.
       */
      const std::optional<Matrix>& get_text_transform() const;

      /**
       * Sets the transform from document coordinates to the device pixels
       * that text is rasterized for while no device transform is set, in
       * which case text is drawn as it is for a device transform while
       * shapes are placed exactly. Conversions kept by build_lists that
       * draw no text for device pixels remain reusable.
       * @param transform The transform to device pixels, or none to draw
       *        text as outlines.
       */
      void set_text_transform(const std::optional<Matrix>& transform);

      /**
       * Returns <code>true</code> iff build_lists defers rasterizing text
       * for the text transform.
       */
      bool is_text_deferred() const;

      /**
       * Sets whether build_lists defers rasterizing text for the text
       * transform, drawing text not yet rasterized for its placement as
       * outlines until rasterize_text rasterizes it.
       * @param is_deferred <code>true</code> iff rasterizing is deferred.
       */
      void set_text_deferred(bool is_deferred);

      /** Returns <code>true</code> iff any text waits to be rasterized. */
      bool has_pending_text() const;

      /**
       * Rasterizes the text that build_lists drew as outlines while waiting
       * to be rasterized, oldest first, until a budget of time is spent,
       * rasterizing at least one text node if any waits.
       * @param budget The time to spend rasterizing.
       * @return The text nodes rasterized, whose changed drawing must be
       *         reported through invalidate.
       */
      std::vector<NodeId> rasterize_text(std::chrono::nanoseconds budget);

      /** Returns the text node drawn as it is while edited, or nil. */
      const NodeId& get_edited_text() const;

      /**
       * Sets the text node drawn as Adobe XD draws text while it is edited,
       * without its vertical flip.
       * @param id The text node, or nil for none.
       */
      void set_edited_text(const NodeId& id);

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
       * Converts a node and its descendants as build does, but clipped
       * neither to its artboard nor to the scroll groups holding it.
       * @param id The node's id.
       * @return The node's drawing, empty if there is no such node.
       */
      DrawList build_unclipped(const NodeId& id);

      /**
       * Converts a node and its descendants as they appear within their
       * ancestors into draw lists that together draw what build converts the
       * node into, but for text whose rasterizing is deferred, reusing the
       * draw lists kept by earlier calls for the subtrees that no change
       * reported through invalidate has affected since. Every change to the
       * document that affects its drawing must be reported through
       * invalidate before the next call.
       * @param id The node's id.
       * @param dependencies Receives what the conversion depends on.
       * @return The draw lists in z order, none if there is no such node.
       */
      std::vector<std::shared_ptr<const DrawList>> build_lists(
        const NodeId& id, SceneDependencies& dependencies);

      /**
       * Converts a node and its descendants as the other build_lists does,
       * except that once a deadline passes, the subtrees converted for
       * another text transform but otherwise unaffected are reused as they
       * are, leaving their text drawn for the transforms they were converted
       * for.
       * @param id The node's id.
       * @param dependencies Receives what the conversion depends on.
       * @param deadline The time past which such subtrees are reused.
       * @return The draw lists in z order, none if there is no such node.
       */
      std::vector<std::shared_ptr<const DrawList>> build_lists(
        const NodeId& id, SceneDependencies& dependencies,
        std::chrono::steady_clock::time_point deadline);

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

      /**
       * Finds the text region whose outlines a path drawn by the last
       * conversion holds.
       * @param path The path of a FillCommand of the conversion.
       * @return The region, or none if the path holds no text outlines.
       */
      std::optional<TextRegion> find_text(const Path& path) const;

    private:
      struct Placement {
        Matrix m_transform;
        double m_opacity;
        std::optional<Rect> m_clip;
        std::optional<Projection> m_projection;
        Matrix m_single;
        bool m_is_clipped = false;
        bool m_is_referenced = false;

        bool operator ==(const Placement&) const = default;
      };
      struct PendingText {
        NodeId m_id;
        Matrix m_transform;
        bool m_is_referenced;
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
        std::shared_ptr<const Path> m_dashes;
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
      struct TextLevel {
        std::shared_ptr<const Path> m_path;
        Rect m_bounds;
        Color m_color;
        int m_level;
      };
      struct TextOutline {
        std::shared_ptr<const Node> m_record;
        TextLayout m_layout;
        std::vector<TextFill> m_fills;
        std::vector<std::shared_ptr<const Path>> m_paths;
        std::vector<Rect> m_bounds;
        std::vector<std::pair<Matrix, std::vector<TextLevel>>> m_rasters;
        Matrix m_mirror;
        std::uint64_t m_raster_epoch;
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
      std::optional<Matrix> m_text_transform;
      std::optional<Matrix> m_raster_transform;
      std::uint64_t m_raster_epoch;
      bool m_is_text_deferred;
      std::optional<std::chrono::steady_clock::time_point> m_deadline;
      std::deque<PendingText> m_pending;
      std::unordered_map<NodeId, Matrix> m_latest;
      NodeId m_edited_text;
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
      bool is_late() const;
      const std::optional<Matrix>& get_text_device() const;
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
      const std::vector<TextLevel>* find_rasters(
        const TextOutline& outline, const Matrix& transform) const;
      const std::vector<TextLevel>& add_rasters(
        TextOutline& outline, const Matrix& transform, bool is_referenced);
      void defer(const NodeId& id, const Matrix& transform, bool is_referenced);
  };
}

#endif
