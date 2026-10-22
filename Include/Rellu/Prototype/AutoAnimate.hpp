#ifndef RELLU_PROTOTYPE_AUTO_ANIMATE_HPP
#define RELLU_PROTOTYPE_AUTO_ANIMATE_HPP
#include <cstddef>
#include <utility>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** A layer of one tree paired with a layer of another. */
  struct LayerMatch {

    /** The layer in the tree animated from. */
    NodeId m_from;

    /** The layer in the tree animated to. */
    NodeId m_to;

    bool operator ==(const LayerMatch&) const = default;
  };

  /**
   * Pairs the layers of two trees as Adobe XD's auto-animate does: the
   * roots with each other, then within each pair of matched containers the
   * children of the same name and kind, in order of appearance.
   * @param document The document holding both trees.
   * @param from The root of the tree animated from.
   * @param to The root of the tree animated to.
   * @return The pairs, the roots first, every container before its
   *         children.
   */
  std::vector<LayerMatch> match_layers(
    const Document& document, const NodeId& from, const NodeId& to);

  /**
   * Tests whether a node of an artboard has a counterpart, as match_layers
   * pairs layers, in another artboard that an auto-animate interaction
   * links its artboard to or from, which keeps Adobe XD from moving it to
   * the pasteboard.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  bool is_auto_animate_matched(const Document& document, const NodeId& id);

  /**
   * Plays Adobe XD's auto-animate between two trees of a document by
   * changing the tree animated to: its layers matched in the other tree
   * move, turn, scale, resize, morph and change color and opacity from
   * their counterparts' values to their own, its other layers fade in, and
   * the layers only the other tree has are moved into it, under their
   * matched parent or the root, and fade out. Every change is made in a
   * transaction of its own and reverted by finish.
   */
  class AutoAnimation {
    public:

      /**
       * Constructs an AutoAnimation showing its start.
       * @param document The document, which must have no open transaction
       *        whenever the AutoAnimation changes it.
       * @param from The root of the tree animated from.
       * @param to The root of the tree animated to.
       * @param is_matching Whether the layers below the roots are matched,
       *        otherwise the trees dissolve into each other.
       */
      AutoAnimation(Document& document, const NodeId& from, const NodeId& to,
        bool is_matching = true);

      /** Returns the pairs of matched layers. */
      const std::vector<LayerMatch>& get_matches() const;

      /** Returns the layers moved from the tree animated from. */
      std::vector<NodeId> get_departing() const;

      /**
       * Shows the animation at a point of its progress.
       * @param progress The eased progress, 0 at the start and 1 at the end.
       */
      void apply(double progress);

      /**
       * Restores both trees, leaving the tree animated to as it was before
       * the animation. Does nothing once done.
       */
      void finish();

    private:
      struct Pair {
        NodeId m_id;
        NodeHeader m_start_header;
        Payload m_start_payload;
        NodeHeader m_end_header;
        Payload m_end_payload;
        Matrix m_start_transform;
        Matrix m_end_transform;
      };
      struct Fade {
        NodeId m_id;
        NodeHeader m_header;
      };
      struct Departure {
        NodeId m_id;
        NodeId m_parent;
        std::size_t m_index;
        NodeHeader m_header;
      };
      Document* m_document;
      std::vector<LayerMatch> m_matches;
      std::vector<Pair> m_pairs;
      std::vector<Fade> m_arrivals;
      std::vector<Departure> m_departures;
      bool m_is_finished;

      AutoAnimation(const AutoAnimation&) = delete;
      AutoAnimation& operator =(const AutoAnimation&) = delete;
  };
}

#endif
