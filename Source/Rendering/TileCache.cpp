#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <QMatrix4x4>
#include "Rellu/Rendering/Shaders.hpp"
#include "Rellu/Rendering/TileCache.hpp"

using namespace Rellu;

namespace {
  const auto CORNERS = std::array<float, 8>({0, 0, 1, 0, 0, 1, 1, 1});
  constexpr auto UNIFORMS_SIZE = 24 * sizeof(float);
  constexpr auto MINIMUM_UNIFORM_COUNT = std::size_t(64);
  constexpr auto MAXIMUM_RENDERERS = std::size_t(2);
  constexpr auto MAXIMUM_POOL = std::size_t(64);
  constexpr auto MINIMUM_MARGIN = 512;
  constexpr auto DEFAULT_BUDGET = std::chrono::milliseconds(10);

  using Uniforms = std::array<float, 24>;

  struct Quad {
    QRhiTexture* m_texture;
    bool m_is_linear;
    Uniforms m_uniforms;
  };

  struct Need {
    QRect m_tiles;
    int m_priority;
    double m_distance;
  };

  void create(auto& resource) {
    if(!resource.create()) {
      throw std::runtime_error("Unable to create a graphics resource.");
    }
  }

  std::uint64_t to_key(int column, int row) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(column)) <<
      32) | static_cast<std::uint32_t>(row);
  }

  QPoint to_tile(std::uint64_t key) {
    return QPoint(static_cast<int>(static_cast<std::uint32_t>(key >> 32)),
      static_cast<int>(static_cast<std::uint32_t>(key)));
  }

  int divide(int value, int divisor) {
    auto quotient = value / divisor;
    if(value % divisor != 0 && value < 0) {
      --quotient;
    }
    return quotient;
  }

  QRect to_tiles(const QRect& pixels, int size) {
    return QRect(QPoint(divide(pixels.left(), size),
      divide(pixels.top(), size)), QPoint(divide(pixels.right(), size),
      divide(pixels.bottom(), size)));
  }
}

TileCache::TileCache()
  : m_tile_size(DEFAULT_TILE_SIZE),
    m_batch_size(DEFAULT_BATCH_SIZE),
    m_budget(DEFAULT_BUDGET),
    m_is_view_complete(false),
    m_is_complete(false),
    m_frame(0),
    m_rhi(nullptr),
    m_descriptor(nullptr),
    m_sample_count(1),
    m_uniform_stride(0),
    m_uniform_capacity(0),
    m_is_corners_uploaded(false) {}

TileCache::~TileCache() {
  release();
}

int TileCache::get_tile_size() const {
  return m_tile_size;
}

void TileCache::set_tile_size(int size) {
  clear();
  m_bindings.clear();
  m_textures.clear();
  m_tile_size = std::max(1, size);
}

std::size_t TileCache::get_batch_size() const {
  return m_batch_size;
}

void TileCache::set_batch_size(std::size_t size) {
  m_batch_size = size;
}

std::chrono::nanoseconds TileCache::get_budget() const {
  return m_budget;
}

void TileCache::set_budget(std::chrono::nanoseconds budget) {
  m_budget = budget;
}

bool TileCache::is_view_complete() const {
  return m_is_view_complete;
}

bool TileCache::is_complete() const {
  return m_is_complete;
}

void TileCache::clear() {
  if(m_epoch) {
    discard(*m_epoch);
    m_epoch.reset();
  }
  if(m_placeholder) {
    discard(*m_placeholder);
    m_placeholder.reset();
  }
  m_is_view_complete = false;
  m_is_complete = false;
}

void TileCache::invalidate(const std::vector<DrawRegion>& regions) {
  if(!m_epoch || regions.empty()) {
    return;
  }
  auto& epoch = *m_epoch;
  auto view = make_translation(-epoch.m_anchor.x(), -epoch.m_anchor.y()) *
    make_scale(epoch.m_scale, epoch.m_scale);
  auto frame = QRect(QPoint(0, 0), epoch.m_size);
  for(auto& region : regions) {
    auto pixels = to_pixels(region, view) & frame;
    if(pixels.isEmpty()) {
      continue;
    }
    auto tiles = to_tiles(pixels, m_tile_size);
    if(tiles.width() * static_cast<long long>(tiles.height()) >
        static_cast<long long>(epoch.m_tiles.size())) {
      for(auto& entry : epoch.m_tiles) {
        if(tiles.contains(to_tile(entry.first))) {
          entry.second.m_is_stale = true;
        }
      }
      continue;
    }
    for(auto row = tiles.top(); row <= tiles.bottom(); ++row) {
      for(auto column = tiles.left(); column <= tiles.right(); ++column) {
        auto tile = epoch.m_tiles.find(to_key(column, row));
        if(tile != epoch.m_tiles.end()) {
          tile->second.m_is_stale = true;
        }
      }
    }
  }
  m_is_view_complete = false;
  m_is_complete = false;
}

void TileCache::initialize(
    QRhi& rhi, QRhiRenderPassDescriptor& descriptor, int sample_count) {
  release();
  m_rhi = &rhi;
  m_descriptor = &descriptor;
  m_sample_count = sample_count;
  m_corners.reset(rhi.newBuffer(QRhiBuffer::Immutable,
    QRhiBuffer::VertexBuffer, sizeof(CORNERS)));
  create(*m_corners);
  m_uniform_stride =
    static_cast<std::uint32_t>(rhi.ubufAligned(UNIFORMS_SIZE));
  reserve_uniforms(MINIMUM_UNIFORM_COUNT);
  m_nearest.reset(rhi.newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
    QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  create(*m_nearest);
  m_linear.reset(rhi.newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
    QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  create(*m_linear);
  m_blank.reset(rhi.newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
  create(*m_blank);
  m_layout_uniforms.reset(rhi.newBuffer(QRhiBuffer::Dynamic,
    QRhiBuffer::UniformBuffer, m_uniform_stride));
  create(*m_layout_uniforms);
  m_layout.reset(rhi.newShaderResourceBindings());
  m_layout->setBindings({
    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
      QRhiShaderResourceBinding::VertexStage, m_layout_uniforms.get(),
      UNIFORMS_SIZE),
    QRhiShaderResourceBinding::sampledTexture(1,
      QRhiShaderResourceBinding::FragmentStage, m_blank.get(),
      m_nearest.get())});
  create(*m_layout);
  m_pipeline.reset(rhi.newGraphicsPipeline());
  m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
  m_pipeline->setShaderStages({
    QRhiShaderStage(QRhiShaderStage::Vertex, load_shader("blit.vert")),
    QRhiShaderStage(QRhiShaderStage::Fragment, load_shader("blit.frag"))});
  auto layout = QRhiVertexInputLayout();
  layout.setBindings({QRhiVertexInputBinding(2 * sizeof(float))});
  layout.setAttributes({
    QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0)});
  m_pipeline->setVertexInputLayout(layout);
  m_pipeline->setShaderResourceBindings(m_layout.get());
  m_pipeline->setRenderPassDescriptor(&descriptor);
  m_pipeline->setSampleCount(sample_count);
  create(*m_pipeline);
}

void TileCache::release() {
  m_epoch.reset();
  m_placeholder.reset();
  m_is_view_complete = false;
  m_is_complete = false;
  m_bindings.clear();
  m_textures.clear();
  for(auto& renderer : m_renderers) {
    renderer->release();
  }
  m_renderers.clear();
  m_pipeline.reset();
  m_layout.reset();
  m_blank.reset();
  m_linear.reset();
  m_nearest.reset();
  m_layout_uniforms.reset();
  m_uniforms.reset();
  m_corners.reset();
  m_uniform_capacity = 0;
  m_is_corners_uploaded = false;
  m_descriptor = nullptr;
  m_rhi = nullptr;
}

void TileCache::render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
    const Drawing& drawing, const Matrix& view, const QColor& background) {
  ++m_frame;
  if(m_background != background) {
    clear();
    m_background = background;
  }
  auto size = target.pixelSize();
  auto scale = view.m_a;
  auto translation = QPoint(static_cast<int>(std::lround(view.m_tx)),
    static_cast<int>(std::lround(view.m_ty)));
  auto get_viewport = [&] (const Epoch& epoch) {
    return QRect(-translation - epoch.m_anchor, size);
  };
  if(!m_epoch || m_epoch->m_scale != scale ||
      !QRect(QPoint(0, 0), m_epoch->m_size).contains(
        get_viewport(*m_epoch))) {
    start(scale, translation, size);
  }
  auto& epoch = *m_epoch;
  auto viewport = get_viewport(epoch);
  auto grid = QRect(0, 0,
    (epoch.m_size.width() + m_tile_size - 1) / m_tile_size,
    (epoch.m_size.height() + m_tile_size - 1) / m_tile_size);
  auto visible = to_tiles(viewport, m_tile_size) & grid;
  auto ring = visible.adjusted(-1, -1, 1, 1) & grid;
  auto center = QPointF(visible.center());
  auto needs = std::vector<Need>();
  for(auto row = ring.top(); row <= ring.bottom(); ++row) {
    auto previous = -1;
    for(auto column = ring.left(); column <= ring.right(); ++column) {
      auto priority = 2 * !visible.contains(QPoint(column, row));
      auto tile = epoch.m_tiles.find(to_key(column, row));
      if(tile != epoch.m_tiles.end()) {
        if(!tile->second.m_is_stale) {
          previous = -1;
          continue;
        }
        ++priority;
      }
      if(previous >= 0 && needs[previous].m_priority == priority) {
        needs[previous].m_tiles.setRight(column);
        continue;
      }
      previous = static_cast<int>(needs.size());
      needs.push_back(Need(QRect(column, row, 1, 1), priority, 0));
    }
  }
  auto merged = std::vector<Need>();
  for(auto& need : needs) {
    auto match = std::find_if(merged.begin(), merged.end(),
      [&] (const auto& candidate) {
        return candidate.m_priority == need.m_priority &&
          candidate.m_tiles.left() == need.m_tiles.left() &&
          candidate.m_tiles.right() == need.m_tiles.right() &&
          candidate.m_tiles.bottom() + 1 == need.m_tiles.top();
      });
    if(match != merged.end()) {
      match->m_tiles.setBottom(need.m_tiles.bottom());
    } else {
      merged.push_back(need);
    }
  }
  for(auto& need : merged) {
    auto offset = QPointF(need.m_tiles.center()) - center;
    need.m_distance = offset.x() * offset.x() + offset.y() * offset.y();
  }
  std::sort(merged.begin(), merged.end(), [] (const auto& left,
      const auto& right) {
    if(left.m_priority != right.m_priority) {
      return left.m_priority < right.m_priority;
    }
    return left.m_distance < right.m_distance;
  });
  auto start_time = std::chrono::steady_clock::now();
  auto rendered = std::size_t(0);
  auto commands = DrawSequence();
  auto is_prefetching = merged.empty() || merged.front().m_priority >= 2;
  while(rendered != merged.size() && rendered != MAXIMUM_RENDERERS &&
      (is_prefetching || merged[rendered].m_priority < 2) &&
      (rendered == 0 ||
        std::chrono::steady_clock::now() - start_time < m_budget)) {
    auto& need = merged[rendered];
    auto tiles = need.m_tiles;
    auto extent = query(drawing, tiles, commands);
    if(commands.size() > m_batch_size &&
        (tiles.width() > 1 || tiles.height() > 1)) {
      auto is_vertical = tiles.height() > 1;
      auto length = is_vertical ? tiles.height() : tiles.width();
      auto count = static_cast<int>(std::min<std::size_t>(length,
        (commands.size() + m_batch_size - 1) / m_batch_size));
      auto parts = std::vector<Need>();
      for(auto i = 0; i != count; ++i) {
        auto first = length * i / count;
        auto last = length * (i + 1) / count - 1;
        auto part = need;
        if(is_vertical) {
          part.m_tiles.setTop(tiles.top() + first);
          part.m_tiles.setBottom(tiles.top() + last);
        } else {
          part.m_tiles.setLeft(tiles.left() + first);
          part.m_tiles.setRight(tiles.left() + last);
        }
        parts.push_back(part);
      }
      merged[rendered] = parts.front();
      merged.insert(
        merged.begin() + rendered + 1, parts.begin() + 1, parts.end());
      continue;
    }
    if(m_renderers.size() == rendered) {
      m_renderers.push_back(std::make_unique<PathRenderer>());
      m_renderers.back()->initialize(*m_rhi, *m_descriptor, m_sample_count);
    }
    render_tiles(buffer, tiles, commands, extent, *m_renderers[rendered],
      background);
    ++rendered;
  }
  m_is_view_complete = std::all_of(merged.begin() + rendered,
    merged.end(), [] (const auto& need) {
      return need.m_priority >= 2;
    });
  m_is_complete = rendered == merged.size();
  if(m_is_view_complete && m_placeholder) {
    discard(*m_placeholder);
    m_placeholder.reset();
  }
  auto capacity = static_cast<std::size_t>(2 * ring.width() * ring.height());
  if(epoch.m_tiles.size() > capacity) {
    auto evictions = std::vector<std::pair<std::uint64_t, std::uint64_t>>();
    for(auto& entry : epoch.m_tiles) {
      if(!ring.contains(to_tile(entry.first))) {
        evictions.emplace_back(entry.second.m_frame, entry.first);
      }
    }
    std::sort(evictions.begin(), evictions.end());
    for(auto& eviction : evictions) {
      if(epoch.m_tiles.size() <= capacity) {
        break;
      }
      auto tile = epoch.m_tiles.find(eviction.second);
      if(m_textures.size() < MAXIMUM_POOL) {
        m_textures.push_back(std::move(tile->second.m_texture));
      } else {
        auto texture = tile->second.m_texture.get();
        std::erase_if(m_bindings, [&] (const auto& binding) {
          return binding.first.first == texture;
        });
      }
      epoch.m_tiles.erase(tile);
    }
  }
  auto projection = m_rhi->clipSpaceCorrMatrix();
  projection.ortho(0, static_cast<float>(size.width()),
    static_cast<float>(size.height()), 0, -1, 1);
  auto source = std::array<float, 4>({0, 0, 1, 1});
  if(m_rhi->isYUpInFramebuffer()) {
    source = std::array<float, 4>({0, 1, 1, 0});
  }
  auto quads = std::vector<Quad>();
  auto add = [&] (QRhiTexture* texture, bool is_linear, const QRectF& rect) {
    auto quad = Quad(texture, is_linear, Uniforms());
    std::copy(projection.constData(), projection.constData() + 16,
      quad.m_uniforms.begin());
    quad.m_uniforms[16] = static_cast<float>(rect.x());
    quad.m_uniforms[17] = static_cast<float>(rect.y());
    quad.m_uniforms[18] = static_cast<float>(rect.width());
    quad.m_uniforms[19] = static_cast<float>(rect.height());
    std::copy(source.begin(), source.end(), quad.m_uniforms.begin() + 20);
    quads.push_back(quad);
  };
  auto screen = QRectF(QPointF(0, 0), QSizeF(size));
  if(m_placeholder) {
    auto& placeholder = *m_placeholder;
    auto factor = scale / placeholder.m_scale;
    for(auto& entry : placeholder.m_tiles) {
      auto tile = to_tile(entry.first);
      auto rect = QRectF(
        factor * (tile.x() * m_tile_size + placeholder.m_anchor.x()) +
          translation.x(),
        factor * (tile.y() * m_tile_size + placeholder.m_anchor.y()) +
          translation.y(), factor * m_tile_size, factor * m_tile_size);
      if(rect.intersects(screen)) {
        add(entry.second.m_texture.get(), true, rect);
      }
    }
  }
  for(auto row = visible.top(); row <= visible.bottom(); ++row) {
    for(auto column = visible.left(); column <= visible.right(); ++column) {
      auto tile = epoch.m_tiles.find(to_key(column, row));
      if(tile == epoch.m_tiles.end()) {
        continue;
      }
      tile->second.m_frame = m_frame;
      add(tile->second.m_texture.get(), false, QRectF(
        column * m_tile_size + epoch.m_anchor.x() + translation.x(),
        row * m_tile_size + epoch.m_anchor.y() + translation.y(),
        m_tile_size, m_tile_size));
    }
  }
  reserve_uniforms(quads.size());
  auto batch = m_rhi->nextResourceUpdateBatch();
  if(!m_is_corners_uploaded) {
    batch->uploadStaticBuffer(m_corners.get(), CORNERS.data());
    m_is_corners_uploaded = true;
  }
  for(auto i = std::size_t(0); i != quads.size(); ++i) {
    batch->updateDynamicBuffer(m_uniforms.get(),
      static_cast<quint32>(i * m_uniform_stride), UNIFORMS_SIZE,
      quads[i].m_uniforms.data());
  }
  buffer.beginPass(&target, background, QRhiDepthStencilClearValue(1, 0),
    batch);
  if(!quads.empty()) {
    buffer.setGraphicsPipeline(m_pipeline.get());
    buffer.setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()),
      static_cast<float>(size.height())));
    auto input = QRhiCommandBuffer::VertexInput(m_corners.get(), 0);
    for(auto i = std::size_t(0); i != quads.size(); ++i) {
      auto offset = QRhiCommandBuffer::DynamicOffset(
        0, static_cast<quint32>(i * m_uniform_stride));
      buffer.setShaderResources(
        &get_bindings(*quads[i].m_texture, quads[i].m_is_linear), 1, &offset);
      buffer.setVertexInput(0, 1, &input);
      buffer.draw(4);
    }
  }
  buffer.endPass();
}

void TileCache::discard(Epoch& epoch) {
  for(auto& entry : epoch.m_tiles) {
    if(!entry.second.m_texture) {
      continue;
    }
    if(m_textures.size() < MAXIMUM_POOL) {
      m_textures.push_back(std::move(entry.second.m_texture));
    } else {
      auto texture = entry.second.m_texture.get();
      std::erase_if(m_bindings, [&] (const auto& binding) {
        return binding.first.first == texture;
      });
    }
  }
  epoch.m_tiles.clear();
}

void TileCache::start(double scale, QPoint translation, QSize size) {
  if(m_epoch) {
    if(m_is_view_complete || !m_placeholder) {
      if(m_placeholder) {
        discard(*m_placeholder);
      }
      m_placeholder = std::move(m_epoch);
    } else {
      discard(*m_epoch);
    }
    m_epoch.reset();
  }
  auto limit = m_rhi->resourceLimit(QRhi::TextureSizeMax);
  auto margin = std::max(
    MINIMUM_MARGIN, std::max(size.width(), size.height()) / 2);
  auto frame = QSize(std::min(limit, size.width() + 2 * margin),
    std::min(limit, size.height() + 2 * margin));
  auto anchor = -translation - QPoint((frame.width() - size.width()) / 2,
    (frame.height() - size.height()) / 2);
  m_epoch.emplace(scale, anchor, frame,
    std::unordered_map<std::uint64_t, Tile>());
  m_is_view_complete = false;
  m_is_complete = false;
}

QRect TileCache::query(
    const Drawing& drawing, const QRect& tiles, DrawSequence& commands) {
  auto& epoch = *m_epoch;
  auto scale = epoch.m_scale;
  auto anchor = epoch.m_anchor;
  auto pixels = QRect(tiles.x() * m_tile_size, tiles.y() * m_tile_size,
    tiles.width() * m_tile_size, tiles.height() * m_tile_size);
  auto region = Rect((pixels.x() + anchor.x()) / scale,
    (pixels.y() + anchor.y()) / scale, pixels.width() / scale,
    pixels.height() / scale);
  auto area = drawing.query(inflate(region, 1 / scale), scale, commands);
  return (to_pixels(DrawRegion(area, 1), get_view()) &
    QRect(QPoint(0, 0), epoch.m_size)) | pixels;
}

Matrix TileCache::get_view() const {
  return make_translation(-m_epoch->m_anchor.x(), -m_epoch->m_anchor.y()) *
    make_scale(m_epoch->m_scale, m_epoch->m_scale);
}

void TileCache::render_tiles(QRhiCommandBuffer& buffer, const QRect& tiles,
    const DrawSequence& commands, const QRect& extent, PathRenderer& renderer,
    const QColor& background) {
  auto& epoch = *m_epoch;
  auto view = get_view();
  auto batch = m_rhi->nextResourceUpdateBatch();
  renderer.prepare(*batch, commands, view, epoch.m_size, extent);
  renderer.render(buffer, background, batch);
  auto& texture = renderer.get_region_texture();
  auto height = texture.pixelSize().height();
  auto copies = m_rhi->nextResourceUpdateBatch();
  for(auto row = tiles.top(); row <= tiles.bottom(); ++row) {
    for(auto column = tiles.left(); column <= tiles.right(); ++column) {
      auto& tile = epoch.m_tiles[to_key(column, row)];
      if(!tile.m_texture) {
        tile.m_texture = make_texture();
      }
      tile.m_is_stale = false;
      tile.m_frame = m_frame;
      auto source = QPoint(column * m_tile_size - extent.x(),
        row * m_tile_size - extent.y());
      if(m_rhi->isYUpInFramebuffer()) {
        source.setY(height - source.y() - m_tile_size);
      }
      auto description = QRhiTextureCopyDescription();
      description.setPixelSize(QSize(m_tile_size, m_tile_size));
      description.setSourceTopLeft(source);
      description.setDestinationTopLeft(QPoint(0, 0));
      copies->copyTexture(tile.m_texture.get(), &texture, description);
    }
  }
  buffer.resourceUpdate(copies);
}

std::unique_ptr<QRhiTexture> TileCache::make_texture() {
  if(!m_textures.empty()) {
    auto texture = std::move(m_textures.back());
    m_textures.pop_back();
    return texture;
  }
  auto texture = std::unique_ptr<QRhiTexture>(m_rhi->newTexture(
    QRhiTexture::RGBA8, QSize(m_tile_size, m_tile_size)));
  create(*texture);
  return texture;
}

void TileCache::reserve_uniforms(std::size_t count) {
  if(m_uniforms && m_uniform_capacity >= count) {
    return;
  }
  auto capacity = std::bit_ceil(std::max(count, MINIMUM_UNIFORM_COUNT));
  m_uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
    QRhiBuffer::UniformBuffer,
    static_cast<quint32>(capacity * m_uniform_stride)));
  create(*m_uniforms);
  m_uniform_capacity = capacity;
  m_bindings.clear();
}

QRhiShaderResourceBindings& TileCache::get_bindings(
    QRhiTexture& texture, bool is_linear) {
  auto& bindings = m_bindings[std::pair(&texture, is_linear)];
  if(bindings) {
    return *bindings;
  }
  bindings.reset(m_rhi->newShaderResourceBindings());
  bindings->setBindings({
    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
      QRhiShaderResourceBinding::VertexStage, m_uniforms.get(),
      UNIFORMS_SIZE),
    QRhiShaderResourceBinding::sampledTexture(1,
      QRhiShaderResourceBinding::FragmentStage, &texture,
      is_linear ? m_linear.get() : m_nearest.get())});
  create(*bindings);
  return *bindings;
}
