#ifndef RELLU_TOOLS_SMART_GUIDES_HPP
#define RELLU_TOOLS_SMART_GUIDES_HPP
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <QImage>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Tools/SnapService.hpp"
#include "Rellu/Tools/ToolContext.hpp"

namespace Rellu {

  /** A line drawn by smart guides, in document space. */
  struct GuideLine {

    /** The start of the line. */
    Point m_start = Point(0, 0);

    /** The end of the line. */
    Point m_end = Point(0, 0);

    bool operator ==(const GuideLine&) const = default;
  };

  /**
   * A distance drawn by smart guides, a line labelled with its length, in
   * document space.
   */
  struct GuideDistance {

    /** The start of the line. */
    Point m_start = Point(0, 0);

    /** The end of the line. */
    Point m_end = Point(0, 0);

    /** Returns the length labelled. */
    double get_length() const;

    bool operator ==(const GuideDistance&) const = default;
  };

  /**
   * Snaps as Adobe XD's smart guides do. Within a distance on screen, the
   * edges and centers of what is moved, resized or drawn snap to those of
   * the other children of its container and of its artboard, to the
   * artboard's guides and visible layout or square grid, and to the spacing
   * between its neighbours. Magenta lines show what lined up and labelled
   * distances show equal spacing. Holding Ctrl suspends snapping.
   */
  class SmartGuides : public SnapService {
    public:

      /**
       * The distance in logical units within which an edge or center
       * snaps.
       */
      static constexpr auto THRESHOLD = 5.0;

      /** The color of the lines and labels. */
      static constexpr auto COLOR = make_color(255, 0, 255);

      /** The width in logical units of a line. */
      static constexpr auto LINE_WIDTH = 1.0;

      /** The length in logical units of the caps ending a distance. */
      static constexpr auto CAP_LENGTH = 6.0;

      /** The size in logical units of the font of a label. */
      static constexpr auto FONT_SIZE = 10.0;

      /** The height in logical units of a label. */
      static constexpr auto LABEL_HEIGHT = 15.0;

      /** The space in logical units on either side of a label's text. */
      static constexpr auto LABEL_PADDING = 4.0;

      /**
       * Constructs SmartGuides.
       * @param context The context snapped in, which must outlive these
       *        SmartGuides.
       */
      explicit SmartGuides(const ToolContext& context);

      /**
       * Returns the lines showing what the most recent adjustment lined
       * up.
       */
      const std::vector<GuideLine>& get_lines() const;

      /**
       * Returns the distances showing the equal spacing the most recent
       * adjustment found.
       */
      const std::vector<GuideDistance>& get_distances() const;

      /** Returns the lines extending the edges measured to. */
      const std::vector<GuideLine>& get_measured_lines() const;

      /** Returns the distances measured. */
      const std::vector<GuideDistance>& get_measured_distances() const;

      void begin(const NodeId& container,
        const std::vector<NodeId>& nodes) override;
      Point snap_move(const Rect& bounds, Point delta,
        Qt::KeyboardModifiers modifiers) override;
      Point snap_point(Point point, SnapAxes axes,
        Qt::KeyboardModifiers modifiers) override;
      void end() override;
      void measure(
        const std::optional<Rect>& selection, const Rect& target) override;
      void draw(const Matrix& view, CanvasOverlay& overlay) const override;

    private:
      struct Entry {
        double m_value;
        std::size_t m_target;
      };
      struct Target {
        Rect m_bounds;
        bool m_is_shown;
      };
      struct SquareGrid {
        Rect m_frame;
        Size m_size;
      };
      struct RowIndex {
        double m_origin = 0;
        double m_bin_width = 1;
        std::vector<std::vector<std::size_t>> m_bins;
        std::vector<std::size_t> m_wide;
      };
      const ToolContext* m_context;
      bool m_is_active;
      bool m_is_collected;
      NodeId m_container;
      std::vector<NodeId> m_nodes;
      std::vector<Target> m_targets;
      std::vector<std::size_t> m_objects;
      std::vector<Entry> m_x_entries;
      std::vector<Entry> m_y_entries;
      std::optional<SquareGrid> m_square_grid;
      RowIndex m_rows;
      RowIndex m_columns;
      mutable std::vector<std::uint64_t> m_marks;
      mutable std::uint64_t m_mark;
      std::vector<GuideLine> m_lines;
      std::vector<GuideDistance> m_distances;
      std::vector<GuideLine> m_measured_lines;
      std::vector<GuideDistance> m_measured_distances;
      mutable double m_label_scale;
      mutable std::unordered_map<std::string, std::shared_ptr<const QImage>>
        m_labels;

      SmartGuides(const SmartGuides&) = delete;
      SmartGuides& operator =(const SmartGuides&) = delete;
      void collect();
      void add_target(const Rect& bounds, bool is_shown, bool is_object);
      void add_line(bool is_x, double value);
      void add_artboard(const NodeId& id);
      void clear_guides();
      void index(bool is_x);
      std::vector<std::size_t> find_row(
        bool is_x, double start, double end) const;
      double get_threshold() const;
      std::optional<double> find_alignment(bool is_x,
        const std::vector<double>& values, double threshold) const;
      std::optional<double> find_spacing(
        bool is_x, const Rect& rect, double threshold) const;
      void add_lines(bool is_x, const Rect& rect,
        const std::vector<double>& values);
      void add_spacing(bool is_x, const Rect& rect);
      std::shared_ptr<const QImage> get_label(
        const std::string& text, double scale) const;
      void draw_line(const Matrix& view, const GuideLine& line,
        CanvasOverlay& overlay) const;
      void draw_distance(const Matrix& view, const GuideDistance& distance,
        double scale, CanvasOverlay& overlay) const;
  };

  /**
   * Formats a distance as smart guides label it, rounded to two decimals
   * without trailing zeros.
   * @param distance The distance in document units.
   */
  std::string format_distance(double distance);
}

#endif
