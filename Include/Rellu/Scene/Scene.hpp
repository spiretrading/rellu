#ifndef RELLU_SCENE_SCENE_HPP
#define RELLU_SCENE_SCENE_HPP
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Rendering/Drawing.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

namespace Rellu {

  /**
   * Keeps a Drawing of a Document's canvas up to date, holding draw lists
   * for each top-level node and converting again only the parts of the
   * top-level nodes that the document's changes affect, including through
   * references and image resources, or whose drawing depends on the
   * tolerance when it changes. Together the draw lists draw what
   * SceneBuilder converts the top-level nodes into, in z order.
   */
  class Scene {
    public:

      /**
       * Constructs a Scene, which converts the canvas on its first update.
       * @param document The document drawn and observed, which must outlive
       *        this Scene.
       * @param tolerance The largest distance in document units between a
       *        curve and the lines approximating it.
       */
      Scene(Document& document, double tolerance);

      ~Scene();

      /**
       * Returns the largest distance in document units between a curve and
       * the lines approximating it.
       */
      double get_tolerance() const;

      /**
       * Sets the largest distance in document units between a curve and the
       * lines approximating it, taking effect on the following updates.
       * @param tolerance The positive tolerance.
       */
      void set_tolerance(double tolerance);

      /**
       * Returns the transform from document coordinates to the device pixels
       * that text is rasterized for, if any.
       */
      const std::optional<Matrix>& get_text_transform() const;

      /**
       * Sets the transform from document coordinates to the device pixels
       * that text is rasterized for the way Adobe XD draws it, taking effect
       * on the following updates, which convert again only the parts drawing
       * such text.
       * @param transform The transform to device pixels, or none to draw
       *        text as outlines.
       */
      void set_text_transform(const std::optional<Matrix>& transform);

      /**
       * Returns the region in document coordinates that top-level nodes are
       * drawn near to draw their text for the text transform, if any.
       */
      const std::optional<Rect>& get_text_region() const;

      /**
       * Sets the region in document coordinates that top-level nodes are
       * drawn near to draw their text for the text transform, those drawn
       * far outside of it drawing their text as outlines, taking effect on
       * the following updates.
       * @param region The region, or none to draw all text for the text
       *        transform.
       */
      void set_text_region(const std::optional<Rect>& region);

      /**
       * Sets the text node drawn as Adobe XD draws text while it is edited,
       * without its vertical flip, taking effect on the following updates.
       * @param id The text node, or nil for none.
       */
      void set_edited_text(const NodeId& id);

      /** Returns the drawing, which changes only when updated. */
      const std::shared_ptr<const Drawing>& get_drawing() const;

      /**
       * Returns <code>true</code> iff the drawing reflects the document and
       * the tolerance.
       */
      bool is_current() const;

      /**
       * Returns <code>true</code> iff no text waits to be rasterized for the
       * text transform.
       */
      bool is_rasterized() const;

      /**
       * Brings the drawing up to date, with all text rasterized for the text
       * transform where it is drawn for it.
       * @return The regions whose drawing changed.
       */
      std::vector<DrawRegion> update();

      /**
       * Brings the drawing up to date, converting the top-level nodes drawn
       * within a region first and leaving those remaining once a budget of
       * time is spent to later updates. At least one top-level node is
       * converted if any needs to be. Text not yet rasterized for the text
       * transform is drawn as outlines until rasterized by rasterize, and
       * once half the budget is spent, parts drawing text for another text
       * transform are kept as they are for later updates.
       * @param region The region in document coordinates converted first.
       * @param budget The time to spend converting.
       * @return The regions whose drawing changed.
       */
      std::vector<DrawRegion> update(
        const Rect& region, std::chrono::nanoseconds budget);

      /**
       * Rasterizes text waiting to be rasterized for the text transform and
       * converts again what draws it, together spending about a budget of
       * time, leaving the remaining text and conversions to later calls.
       * @param region The region in document coordinates converted first.
       * @param budget The time to spend.
       * @return The regions whose drawing changed.
       */
      std::vector<DrawRegion> rasterize(
        const Rect& region, std::chrono::nanoseconds budget);

    private:
      struct Fragment {
        std::vector<std::shared_ptr<const DrawList>> m_lists;
        SceneDependencies m_dependencies;
        bool m_is_dirty;
        std::optional<Rect> m_bounds;
      };
      const Document* m_document;
      SceneBuilder m_builder;
      std::optional<Matrix> m_text_transform;
      std::optional<Rect> m_text_region;
      std::shared_ptr<Drawing> m_drawing;
      std::shared_ptr<const Drawing> m_view;
      std::unordered_map<NodeId, Fragment> m_fragments;
      std::unordered_map<NodeId, std::unordered_set<NodeId>> m_dependents;
      std::vector<NodeId> m_canvas;
      std::vector<NodeId> m_removed;
      std::deque<NodeId> m_rasterized;
      bool m_is_changed;
      double m_index_rate;
      Subscription m_subscription;

      Scene(const Scene&) = delete;
      Scene& operator =(const Scene&) = delete;
      void on_change(const ChangeSet& changes);
      void mark(const NodeId& id, bool is_moved);
      void mark_dependents(const NodeId& key);
      void forget(const NodeId& id, Fragment& fragment);
      const std::optional<Matrix>& get_text_transform(
        const Fragment& fragment) const;
      bool is_text_stale(const Fragment& fragment) const;
      void mark_text();
      void mark_rasterized(std::chrono::nanoseconds budget);
      std::vector<DrawRegion> update(
        const Rect* region, std::chrono::nanoseconds budget);
  };
}

#endif
