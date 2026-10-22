#ifndef RELLU_RENDERING_OFFSCREEN_RENDERER_HPP
#define RELLU_RENDERING_OFFSCREEN_RENDERER_HPP
#include <cstdint>
#include <memory>
#include <QImage>
#include <QOffscreenSurface>
#include <QSize>
#include <rhi/qrhi.h>
#include "Rellu/Rendering/Renderer.hpp"

namespace Rellu {

  /** Specifies the graphics API and adapter used to render. */
  struct RenderBackend {

    /** The graphics API to render with. */
    QRhi::Implementation m_implementation;

    /** Whether a software adapter is preferred over a hardware one. */
    bool m_is_software;
  };

  /** The largest number of pixels in an image Adobe XD exports. */
  inline constexpr auto MAXIMUM_EXPORT_AREA = std::int64_t(256000000);

  /**
   * Tests whether Adobe XD exports an image, which it refuses when the image
   * has more than MAXIMUM_EXPORT_AREA pixels.
   * @param size The size of the image in pixels.
   */
  bool is_exportable(QSize size);

  /**
   * Returns the default RenderBackend, chosen for deterministic output:
   * Direct3D 11 with a software adapter (WARP) on Windows, and OpenGL
   * elsewhere.
   */
  RenderBackend get_default_render_backend();

  /**
   * Renders frames into a texture and reads them back into images. The
   * OpenGL backend requires a QGuiApplication; the Direct3D and Null backends
   * do not.
   */
  class OffscreenRenderer {
    public:

      /**
       * Constructs an OffscreenRenderer using the default RenderBackend.
       * @param renderer The Renderer recording frames, which must outlive this
       *        OffscreenRenderer.
       */
      explicit OffscreenRenderer(Renderer& renderer);

      /**
       * Constructs an OffscreenRenderer.
       * @param renderer The Renderer recording frames, which must outlive this
       *        OffscreenRenderer.
       * @param backend The backend to render with.
       */
      OffscreenRenderer(Renderer& renderer, RenderBackend backend);

      ~OffscreenRenderer();

      /** Returns the backend rendered with. */
      RenderBackend get_backend() const;

      /** Returns the QRhi rendered with. */
      QRhi& get_rhi();

      /**
       * Returns the largest width and height of a frame rendered at once,
       * larger frames being rendered in regions no larger, by default half
       * the QRhi's texture size limit.
       */
      int get_tile_size() const;

      /**
       * Sets the largest width and height of a frame rendered at once.
       * @param size The tile size, positive and at most the QRhi's texture
       *        size limit.
       */
      void set_tile_size(int size);

      /**
       * Renders a frame and reads it back. A frame larger than the tile size
       * requires a Renderer that records regions.
       * @param size The size of the frame in pixels.
       * @param scale The number of pixels per logical unit.
       * @return The frame as an RGBA image with premultiplied alpha whose
       *         first row is the top of the frame and whose device pixel
       *         ratio is the <i>scale</i>.
       */
      QImage render(QSize size, double scale);

    private:
      Renderer* m_renderer;
      RenderBackend m_backend;
      std::unique_ptr<QOffscreenSurface> m_surface;
      std::unique_ptr<QRhi> m_rhi;
      int m_tile_size;
      std::unique_ptr<QRhiTexture> m_texture;
      std::unique_ptr<QRhiRenderBuffer> m_depth_stencil;
      std::unique_ptr<QRhiRenderPassDescriptor> m_descriptor;
      std::unique_ptr<QRhiTextureRenderTarget> m_target;

      OffscreenRenderer(const OffscreenRenderer&) = delete;
      OffscreenRenderer& operator =(const OffscreenRenderer&) = delete;
      void prepare(QSize size);
  };
}

#endif
