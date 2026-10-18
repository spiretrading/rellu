#ifndef RELLU_RENDERING_CANVAS_RENDERER_HPP
#define RELLU_RENDERING_CANVAS_RENDERER_HPP
#include <memory>
#include <vector>
#include <QColor>
#include <QSize>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Rendering/Drawing.hpp"
#include "Rellu/Rendering/PathRenderer.hpp"
#include "Rellu/Rendering/Renderer.hpp"
#include "Rellu/Rendering/TileCache.hpp"

namespace Rellu {

  /** The color of the canvas behind the document. */
  inline const auto CANVAS_BACKGROUND = QColor(232, 232, 232);

  /**
   * Renders the contents of the canvas: either a draw list, rendered whole
   * every time the frame changes, or a drawing, rendered through a
   * TileCache so that a frame renders only what it has not drawn before.
   */
  class CanvasRenderer : public Renderer {
    public:

      /** Constructs a CanvasRenderer drawing an empty draw list. */
      CanvasRenderer();

      /** Returns the draw list drawn over the background. */
      const std::shared_ptr<const DrawList>& get_draw_list() const;

      /**
       * Sets the draw list drawn over the background in place of any
       * drawing, which must remain unchanged while it is set.
       * @param list The draw list to draw, or null to draw nothing.
       */
      void set_draw_list(std::shared_ptr<const DrawList> list);

      /** Returns the drawing drawn over the background, or null. */
      const std::shared_ptr<const Drawing>& get_drawing() const;

      /**
       * Sets the drawing drawn over the background in place of any draw
       * list, whose changes must be passed to invalidate.
       * @param drawing The drawing to draw, or null to draw the draw list.
       */
      void set_drawing(std::shared_ptr<const Drawing> drawing);

      /**
       * Redraws the regions of the drawing that changed.
       * @param regions The regions whose drawing changed.
       */
      void invalidate(const std::vector<DrawRegion>& regions);

      /** Returns the tiles that the drawing is rendered through. */
      TileCache& get_tiles();

      /**
       * Returns <code>true</code> iff the most recent frame drew everything
       * it shows in its final form, so that no further frame is needed.
       */
      bool is_complete() const;

      /** Returns the transform from document coordinates to logical units. */
      const Matrix& get_view() const;

      /**
       * Sets the transform from document coordinates to logical units, which
       * the frame's scale then maps to pixels. A drawing is drawn with the
       * translation rounded to whole pixels when the transform scales both
       * axes alike without rotating.
       * @param view The transform to use.
       */
      void set_view(const Matrix& view);

      /** Returns the color the frame is cleared to before drawing. */
      const QColor& get_background() const;

      /**
       * Sets the color the frame is cleared to before drawing.
       * @param background The color to clear to, whose components are taken
       *        as premultiplied by its alpha.
       */
      void set_background(const QColor& background);

      void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
        int sample_count) override;
      void release() override;
      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
        double scale) override;

      /**
       * Records a region of a frame into a texture, drawing each pixel of the
       * region the same as recording the whole frame does.
       * @param buffer The command buffer to record into, without an active
       *        render pass.
       * @param texture The RGBA8 texture receiving the region at its top left
       *        as rendered, at least as large as the region.
       * @param scale The number of pixels per logical unit.
       * @param size The size of the frame in pixels.
       * @param region The region of the frame to record.
       * @throws std::invalid_argument A drawing is set, or the view scaled
       *         by the <i>scale</i> does not scale both axes alike without
       *         rotating.
       */
      void render(QRhiCommandBuffer& buffer, QRhiTexture& texture,
        double scale, QSize size, const QRect& region) override;

    private:
      PathRenderer m_paths;
      TileCache m_tiles;
      std::shared_ptr<const DrawList> m_list;
      std::shared_ptr<const Drawing> m_drawing;
      std::unique_ptr<Drawing> m_list_drawing;
      DrawSequence m_commands;
      Matrix m_view;
      QColor m_background;
      QRhi* m_rhi;
      bool m_is_prepared;
      bool m_is_complete;
      QSize m_size;
      double m_scale;
  };
}

#endif
