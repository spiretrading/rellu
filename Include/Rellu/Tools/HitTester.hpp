#ifndef RELLU_TOOLS_HIT_TESTER_HPP
#define RELLU_TOOLS_HIT_TESTER_HPP
#include <chrono>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * Finds the nodes of a document drawn at a point or within a region, using
   * their real geometry: fills, strokes, text bounds and artboard frames,
   * within the masks and clips that apply, through references, and leaving
   * out hidden nodes and, unless asked otherwise, locked ones. Each node's
   * extent in its own space is
   * kept until the document changes it, so that only the subtrees whose
   * extent covers the point are visited.
   */
  class HitTester {
    public:

      /**
       * Constructs a HitTester measuring text by its estimated bounds.
       * @param document The document to test, which must outlive this
       *        HitTester.
       */
      explicit HitTester(Document& document);

      /**
       * Constructs a HitTester.
       * @param document The document to test, which must outlive this
       *        HitTester.
       * @param measurer The function measuring text.
       */
      HitTester(Document& document, TextMeasurer measurer);

      /** Returns the document tested. */
      Document& get_document() const;

      /**
       * Finds the top-most node drawn at a point.
       * @param point The point in document space.
       * @param tolerance The distance in document units within which an
       *        outline counts as hit.
       * @param is_locked_hit Whether locked nodes and their descendants can
       *        be hit, as by a click selecting them.
       * @return The deepest node drawn at the <i>point</i>, which is the
       *         reference itself for what a reference draws of its source,
       *         an artboard where only its background is drawn, or nil if
       *         nothing is.
       */
      NodeId hit(
        Point point, double tolerance, bool is_locked_hit = false) const;

      /**
       * Finds the top-most node drawn at a point within a node, as hit does,
       * but without the clipping of the node's artboard, so that the part
       * of the node lying outside its artboard can be hit.
       * @param id The node.
       * @param point The point in document space.
       * @param tolerance The distance in document units within which an
       *        outline counts as hit.
       * @param is_locked_hit Whether locked nodes and their descendants can
       *        be hit.
       * @return The deepest node within the node <i>id</i> drawn at the
       *         <i>point</i>, as hit finds it, or nil if there is none.
       */
      NodeId hit_within(const NodeId& id, Point point, double tolerance,
        bool is_locked_hit = false) const;

      /**
       * Lists the visible children of a container, locked ones included,
       * whose drawing meets a region.
       * @param container The container, or nil for the canvas.
       * @param region The region in document space.
       * @param is_enclosed Whether a child must lie entirely within the
       *        <i>region</i> rather than intersect it.
       * @return The children in z order.
       */
      std::vector<NodeId> query(const NodeId& container, const Rect& region,
        bool is_enclosed = false) const;

      /**
       * Returns the extent of what a node draws, strokes included, in its
       * own space, or <code>std::nullopt</code> if it draws nothing that
       * can be hit.
       * @param id The node's id.
       */
      std::optional<Rect> get_extent(const NodeId& id) const;

      /**
       * Measures the extents of the canvas's nodes ahead of their first
       * test, deepest first, until a budget of time is spent, resuming
       * where the previous call stopped.
       * @param budget The time to spend.
       * @return <code>true</code> iff every node on the canvas is measured.
       */
      bool prepare(std::chrono::nanoseconds budget);

      /** Discards every extent kept. */
      void clear();

    private:
      struct Placement {
        const Node* m_source;
        const std::vector<NodeId>* m_children;
        Matrix m_transform;
        bool m_owns_children;
      };
      struct Extent {
        std::optional<Rect> m_local;
        std::optional<Rect> m_parent;
      };
      Document* m_document;
      TextMeasurer m_measurer;
      mutable std::unordered_map<NodeId, Extent> m_extents;
      std::vector<std::pair<NodeId, std::size_t>> m_pending;
      std::size_t m_next_root;
      Subscription m_subscription;

      HitTester(const HitTester&) = delete;
      HitTester& operator =(const HitTester&) = delete;
      std::optional<Placement> place(const Node& node) const;
      const Extent& measure(const NodeId& id, int depth) const;
      NodeId hit(const NodeId& id, Point point, const Matrix& parent,
        double tolerance, bool is_locked_hit, int depth) const;
      NodeId hit_children(const std::vector<NodeId>& children, Point point,
        const Matrix& world, const Matrix& inverse, double tolerance,
        bool is_locked_hit, int depth) const;
      void on_change(const ChangeSet& changes);
  };
}

#endif
