#ifndef RELLU_RENDERING_TILE_CACHE_HPP
#define RELLU_RENDERING_TILE_CACHE_HPP
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <rhi/qrhi.h>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/Drawing.hpp"
#include "Rellu/Rendering/PathRenderer.hpp"

namespace Rellu {

  /**
   * Draws a Drawing through tiles of pixels kept across frames, so that a
   * frame only renders the tiles that become visible or whose drawing
   * changed. Tiles are rendered for a frame of pixels anchored at the scale
   * drawn at, and each pixel of a tile is the same as rendering that whole
   * frame at once draws it. While tiles are missing, the tiles of the most
   * recent complete frame at another scale or position stand in for them.
   */
  class TileCache {
    public:

      /** The default size in pixels of a tile's side. */
      static constexpr auto DEFAULT_TILE_SIZE = 256;

      /** The default largest number of commands rendered at once. */
      static constexpr auto DEFAULT_BATCH_SIZE = std::size_t(200000);

      /** Constructs a TileCache. */
      TileCache();

      ~TileCache();

      /** Returns the size in pixels of a tile's side. */
      int get_tile_size() const;

      /**
       * Sets the size in pixels of a tile's side, discarding every tile.
       * @param size The positive size.
       */
      void set_tile_size(int size);

      /**
       * Returns the largest number of commands a group of tiles is rendered
       * with at once.
       */
      std::size_t get_batch_size() const;

      /**
       * Sets the largest number of commands a group of tiles is rendered
       * with at once, beyond which the group is split, down to single
       * tiles.
       * @param size The number of commands.
       */
      void set_batch_size(std::size_t size);

      /** Returns the processor time a frame spends rendering tiles. */
      std::chrono::nanoseconds get_budget() const;

      /**
       * Sets the processor time a frame spends rendering tiles, beyond which
       * the remaining tiles are left to later frames. Every frame renders at
       * least one group of tiles, and a frame with no earlier tiles to stand
       * in for the missing ones spends four times as long.
       * @param budget The time per frame.
       */
      void set_budget(std::chrono::nanoseconds budget);

      /**
       * Returns <code>true</code> iff the most recent frame drew every tile
       * of the view at the final scale and position.
       */
      bool is_view_complete() const;

      /**
       * Returns <code>true</code> iff the most recent frame drew every tile
       * of the view, and of the margin prefetched around it, at the final
       * scale and position, so that no further frame is needed.
       */
      bool is_complete() const;

      /** Discards every tile. */
      void clear();

      /**
       * Marks the tiles covering regions of the drawing, and the backdrop
       * blurs reading them, as needing to be rendered again, drawing them as
       * they are until then.
       * @param drawing The drawing after the change.
       * @param regions The regions whose drawing changed.
       */
      void invalidate(
        const Drawing& drawing, const std::vector<DrawRegion>& regions);

      /**
       * Creates the graphics resources used to draw frames.
       * @param rhi The QRhi that frames are recorded with.
       * @param descriptor The render pass descriptor compatible with every
       *        render target drawn to.
       * @param sample_count The sample count of every render target drawn to.
       */
      void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
        int sample_count);

      /** Releases the graphics resources created by initialize. */
      void release();

      /**
       * Records a frame, including its render passes.
       * @param buffer The command buffer, without an active render pass.
       * @param target The render target.
       * @param drawing The drawing, whose changes since the previous frame
       *        must have been passed to invalidate.
       * @param view The transform from document coordinates to pixels of the
       *        render target, which scales both axes alike by a positive
       *        factor without rotating; its translation is rounded to whole
       *        pixels.
       * @param background The color behind the drawing, whose components
       *        are taken as premultiplied by its alpha.
       */
      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
        const Drawing& drawing, const Matrix& view, const QColor& background);

    private:
      struct Tile {
        int m_slot = -1;
        std::uint64_t m_frame;
        bool m_is_stale;
      };
      struct Epoch {
        double m_scale;
        QPoint m_anchor;
        QSize m_size;
        std::unordered_map<std::uint64_t, Tile> m_tiles;
        double m_density;
      };
      int m_tile_size;
      std::size_t m_batch_size;
      std::chrono::nanoseconds m_budget;
      bool m_is_view_complete;
      bool m_is_complete;
      std::uint64_t m_frame;
      std::optional<Epoch> m_epoch;
      std::optional<Epoch> m_placeholder;
      std::optional<QColor> m_background;
      QRhi* m_rhi;
      QRhiRenderPassDescriptor* m_descriptor;
      int m_sample_count;
      std::vector<std::unique_ptr<PathRenderer>> m_renderers;
      std::vector<std::unique_ptr<QRhiTexture>> m_pages;
      std::vector<int> m_slots;
      std::unique_ptr<QRhiTexture> m_blank;
      std::unique_ptr<QRhiTexture> m_fill;
      bool m_is_fill_current;
      std::unique_ptr<QRhiBuffer> m_corners;
      std::unique_ptr<QRhiBuffer> m_uniforms;
      std::unique_ptr<QRhiBuffer> m_layout_uniforms;
      std::unique_ptr<QRhiSampler> m_nearest;
      std::unique_ptr<QRhiSampler> m_linear;
      std::unique_ptr<QRhiShaderResourceBindings> m_layout;
      std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
      std::map<std::pair<QRhiTexture*, bool>,
        std::unique_ptr<QRhiShaderResourceBindings>> m_bindings;
      std::uint32_t m_uniform_stride;
      std::size_t m_uniform_capacity;
      bool m_is_corners_uploaded;

      TileCache(const TileCache&) = delete;
      TileCache& operator =(const TileCache&) = delete;
      void discard(Epoch& epoch);
      void recycle(int& slot);
      void start(double scale, QPoint translation, QSize size);
      Rect get_region(const QRect& tiles) const;
      QRect trim(const Drawing& drawing, const QRect& tiles);
      QRect query(
        const Drawing& drawing, const QRect& tiles, DrawSequence& commands);
      Matrix get_view() const;
      void render_tiles(QRhiCommandBuffer& buffer, const QRect& tiles,
        const DrawSequence& commands, const QRect& extent,
        PathRenderer& renderer, const QColor& background);
      int allocate();
      QRhiTexture& get_page(int slot) const;
      QPoint get_origin(int slot) const;
      void reserve_uniforms(std::size_t count);
      QRhiShaderResourceBindings& get_bindings(
        QRhiTexture& texture, bool is_linear);
  };
}

#endif
