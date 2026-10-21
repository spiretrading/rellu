#ifndef RELLU_TOOLS_TOOL_CONTEXT_HPP
#define RELLU_TOOLS_TOOL_CONTEXT_HPP
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Tools/ArtboardTitles.hpp"
#include "Rellu/Tools/GridSettings.hpp"
#include "Rellu/Tools/GuideSettings.hpp"
#include "Rellu/Tools/HitTester.hpp"
#include "Rellu/Tools/SnapService.hpp"

namespace Rellu {

  /** Changes the view of the canvas at the request of a tool. */
  struct ViewNavigator {

    /** Zooms by a factor about a point given in logical units. */
    std::function<void (double factor, Point center)> m_zoom;

    /** Zooms and centers the view on a region in document space. */
    std::function<void (const Rect& region)> m_zoom_to;
  };

  /**
   * What tools work with: the editing state of a document, the services
   * finding and snapping nodes, and the view of the canvas, whose logical
   * units are the canvas widget's.
   */
  class ToolContext {
    public:

      /**
       * Constructs a ToolContext viewing the document unscaled, snapping
       * with SmartGuides.
       * @param editor The editing state, which must outlive this ToolContext.
       * @param hit_tester The hit tester of the <i>editor</i>'s document,
       *        which must outlive this ToolContext.
       * @param titles The artboard titles, which must outlive this
       *        ToolContext.
       */
      ToolContext(
        Editor& editor, HitTester& hit_tester, ArtboardTitles& titles);

      /** Returns the editing state. */
      Editor& get_editor() const;

      /** Returns the document edited. */
      Document& get_document() const;

      /** Returns the hit tester. */
      HitTester& get_hit_tester() const;

      /** Returns the artboard titles laid out in the current view. */
      ArtboardTitles& get_titles() const;

      /** Returns the snap service. */
      SnapService& get_snap_service() const;

      /**
       * Sets the snap service.
       * @param service The service, which must outlive its use.
       */
      void set_snap_service(SnapService& service);

      /** Returns the function measuring text. */
      const TextMeasurer& get_text_measurer() const;

      /**
       * Sets the function measuring text.
       * @param measurer The function measuring text.
       */
      void set_text_measurer(TextMeasurer measurer);

      /** Returns the transform from document space to logical units. */
      const Matrix& get_view() const;

      /** Returns the number of pixels per logical unit. */
      double get_scale() const;

      /** Returns the size of the view in logical units. */
      Size get_size() const;

      /**
       * Sets the view, laying out the artboard titles again.
       * @param view The invertible transform from document space to logical
       *        units.
       * @param scale The number of pixels per logical unit.
       * @param size The size of the view in logical units.
       */
      void set_view(const Matrix& view, double scale, Size size);

      /** Returns the number of logical units per document unit. */
      double get_zoom() const;

      /**
       * Maps a point from logical units to document space.
       * @param point The point in logical units.
       */
      Point to_document(Point point) const;

      /**
       * Maps a point from document space to logical units.
       * @param point The point in document space.
       */
      Point to_view(Point point) const;

      /**
       * Sets the navigator changing the view, which otherwise changes only
       * this ToolContext's view.
       * @param navigator The navigator.
       */
      void set_navigator(ViewNavigator navigator);

      /**
       * Zooms the view by a factor about a point.
       * @param factor The factor multiplying the zoom.
       * @param center The point in logical units that stays in place.
       */
      void zoom(double factor, Point center);

      /**
       * Zooms and centers the view so that a region is visible.
       * @param region The region in document space.
       */
      void zoom_to(const Rect& region);

      /**
       * Asks for a tool to become active once the event being handled is
       * done.
       * @param name The name of the tool.
       */
      void request_tool(std::string name);

      /** Returns and clears the name of the tool requested, if any. */
      std::optional<std::string> take_tool_request();

      /** Lays out the artboard titles again, as when the document changes. */
      void update_titles();

      /**
       * Returns the number of changes published by the document since this
       * ToolContext was constructed.
       */
      std::uint64_t get_revision() const;

      /** Returns how artboard guides are shown. */
      const GuideSettings& get_guide_settings() const;

      /**
       * Sets how artboard guides are shown.
       * @param settings The settings.
       */
      void set_guide_settings(GuideSettings settings);

      /** Returns how the design grids of artboards are shown. */
      const GridSettings& get_grid_settings() const;

      /**
       * Sets how the design grids of artboards are shown.
       * @param settings The settings.
       */
      void set_grid_settings(GridSettings settings);

      /**
       * Returns the guide being dragged, drawn by the tool dragging it in
       * place of where the document has it, or none.
       */
      const std::optional<GuideReference>& get_dragged_guide() const;

      /**
       * Sets the guide being dragged.
       * @param guide The guide, or none.
       */
      void set_dragged_guide(std::optional<GuideReference> guide);

    private:
      Editor* m_editor;
      HitTester* m_hit_tester;
      ArtboardTitles* m_titles;
      std::unique_ptr<SnapService> m_default_snap_service;
      SnapService* m_snap_service;
      TextMeasurer m_measurer;
      Matrix m_view;
      Matrix m_inverse;
      double m_scale;
      Size m_size;
      ViewNavigator m_navigator;
      std::optional<std::string> m_tool_request;
      std::uint64_t m_revision;
      GuideSettings m_guide_settings;
      GridSettings m_grid_settings;
      std::optional<GuideReference> m_dragged_guide;
      Subscription m_subscription;

      ToolContext(const ToolContext&) = delete;
      ToolContext& operator =(const ToolContext&) = delete;
  };
}

#endif
