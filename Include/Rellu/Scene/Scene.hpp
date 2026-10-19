#ifndef RELLU_SCENE_SCENE_HPP
#define RELLU_SCENE_SCENE_HPP
#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
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

      /** Returns the drawing, which changes only when updated. */
      const std::shared_ptr<const Drawing>& get_drawing() const;

      /**
       * Returns <code>true</code> iff the drawing reflects the document and
       * the tolerance.
       */
      bool is_current() const;

      /**
       * Brings the drawing up to date.
       * @return The regions whose drawing changed.
       */
      std::vector<DrawRegion> update();

      /**
       * Brings the drawing up to date, converting the top-level nodes drawn
       * within a region first and leaving those remaining once a budget of
       * time is spent to later updates. At least one top-level node is
       * converted if any needs to be.
       * @param region The region in document coordinates converted first.
       * @param budget The time to spend converting.
       * @return The regions whose drawing changed.
       */
      std::vector<DrawRegion> update(
        const Rect& region, std::chrono::nanoseconds budget);

    private:
      struct Fragment {
        std::vector<std::shared_ptr<const DrawList>> m_lists;
        SceneDependencies m_dependencies;
        bool m_is_dirty;
      };
      const Document* m_document;
      SceneBuilder m_builder;
      std::shared_ptr<Drawing> m_drawing;
      std::shared_ptr<const Drawing> m_view;
      std::unordered_map<NodeId, Fragment> m_fragments;
      std::unordered_map<NodeId, std::unordered_set<NodeId>> m_dependents;
      std::vector<NodeId> m_canvas;
      std::vector<NodeId> m_removed;
      bool m_is_changed;
      Subscription m_subscription;

      Scene(const Scene&) = delete;
      Scene& operator =(const Scene&) = delete;
      void on_change(const ChangeSet& changes);
      void mark(const NodeId& id);
      void mark_dependents(const NodeId& key);
      void forget(const NodeId& id, Fragment& fragment);
      std::vector<DrawRegion> update(
        const Rect* region, std::chrono::nanoseconds budget);
  };
}

#endif
