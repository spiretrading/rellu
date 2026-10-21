#ifndef RELLU_TOOLS_ARTBOARD_TITLES_HPP
#define RELLU_TOOLS_ARTBOARD_TITLES_HPP
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <QFont>
#include <QImage>
#include <QString>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"

namespace Rellu {

  /**
   * The names drawn above the artboards in view, at a fixed size on screen
   * and cut short to their artboard's width, which select their artboard
   * when clicked.
   */
  class ArtboardTitles {
    public:

      /** The height of a title in logical units. */
      static constexpr auto HEIGHT = 14.0;

      /** The distance in logical units between a title and its artboard. */
      static constexpr auto GAP = 3.0;

      /**
       * The distance in logical units between an artboard and the bottom of
       * the region in which its title is clicked, which reaches below the
       * title's text.
       */
      static constexpr auto HIT_GAP = 4.5;

      /** The size of a title's font in logical units. */
      static constexpr auto FONT_SIZE = 11.0;

      /** A title laid out in a view. */
      struct Title {

        /** The artboard named. */
        NodeId m_artboard;

        /** The text shown. */
        QString m_text;

        /** The region the title covers, in logical units. */
        Rect m_bounds;
      };

      /** Constructs ArtboardTitles with nothing laid out. */
      ArtboardTitles();

      /**
       * Lays out the titles of the visible artboards in view.
       * @param document The document whose artboards are named.
       * @param view The transform from document space to logical units.
       * @param size The size of the view in logical units.
       */
      void lay_out(const Document& document, const Matrix& view, Size size);

      /** Returns the titles most recently laid out, in z order. */
      const std::vector<Title>& get_titles() const;

      /**
       * Finds the title clicked at a position, within the width of its text
       * and from the top of its text down to HIT_GAP above its artboard.
       * @param position The position in logical units.
       * @return The artboard whose title is top-most at the <i>position</i>,
       *         or nil if there is none.
       */
      NodeId find(Point position) const;

      /** Returns the font titles are drawn with, at one pixel per unit. */
      const QFont& get_font() const;

      /**
       * Sets the artboard whose title is not drawn, as while it is renamed
       * in place.
       * @param artboard The artboard, or nil for none.
       */
      void set_hidden(const NodeId& artboard);

      /**
       * Draws the titles most recently laid out.
       * @param overlay The overlay to draw into.
       * @param scale The number of pixels per logical unit.
       * @param highlights The artboards whose titles are highlighted.
       */
      void draw(CanvasOverlay& overlay, double scale,
        const std::unordered_set<NodeId>& highlights);

    private:
      struct Image {
        std::shared_ptr<const QImage> m_image;
        std::uint64_t m_generation;
      };
      QFont m_font;
      NodeId m_hidden;
      std::vector<Title> m_titles;
      std::unordered_map<std::string, double> m_widths;
      std::unordered_map<QString, Image> m_images;
      std::uint64_t m_generation;

      ArtboardTitles(const ArtboardTitles&) = delete;
      ArtboardTitles& operator =(const ArtboardTitles&) = delete;
  };
}

#endif
