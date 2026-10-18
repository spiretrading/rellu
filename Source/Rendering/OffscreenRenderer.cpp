#include <stdexcept>
#include <rhi/qrhi_platform.h>
#include "Rellu/Rendering/OffscreenRenderer.hpp"

using namespace Rellu;

namespace {
  auto make_rhi(
      RenderBackend backend, std::unique_ptr<QOffscreenSurface>& surface) {
    auto flags = QRhi::Flags();
    if(backend.m_is_software) {
      flags |= QRhi::PreferSoftwareRenderer;
    }
    auto rhi = std::unique_ptr<QRhi>();
    if(backend.m_implementation == QRhi::Null) {
      auto parameters = QRhiNullInitParams();
      rhi.reset(QRhi::create(QRhi::Null, &parameters, flags));
#if QT_CONFIG(opengl)
    } else if(backend.m_implementation == QRhi::OpenGLES2) {
      surface.reset(QRhiGles2InitParams::newFallbackSurface());
      auto parameters = QRhiGles2InitParams();
      parameters.fallbackSurface = surface.get();
      rhi.reset(QRhi::create(QRhi::OpenGLES2, &parameters, flags));
#endif
#ifdef Q_OS_WIN
    } else if(backend.m_implementation == QRhi::D3D11) {
      auto parameters = QRhiD3D11InitParams();
      rhi.reset(QRhi::create(QRhi::D3D11, &parameters, flags));
    } else if(backend.m_implementation == QRhi::D3D12) {
      auto parameters = QRhiD3D12InitParams();
      rhi.reset(QRhi::create(QRhi::D3D12, &parameters, flags));
#endif
    } else {
      throw std::invalid_argument("The render backend is not supported.");
    }
    if(!rhi) {
      throw std::runtime_error("Unable to create the QRhi.");
    }
    return rhi;
  }

  void create(auto& resource) {
    if(!resource.create()) {
      throw std::runtime_error("Unable to create a graphics resource.");
    }
  }

  QImage to_image(const QRhiReadbackResult& result, bool is_y_up) {
    if(result.format != QRhiTexture::RGBA8) {
      throw std::runtime_error("The readback format is not supported.");
    }
    auto width = result.pixelSize.width();
    auto height = result.pixelSize.height();
    if(result.data.size() < qsizetype(4) * width * height) {
      throw std::runtime_error("The readback is incomplete.");
    }
    auto wrapper = QImage(
      reinterpret_cast<const uchar*>(result.data.constData()), width, height,
      4 * width, QImage::Format_RGBA8888_Premultiplied);
    if(is_y_up) {
      return wrapper.flipped(Qt::Vertical);
    }
    return wrapper.copy();
  }
}

RenderBackend Rellu::get_default_render_backend() {
#ifdef Q_OS_WIN
  return RenderBackend(QRhi::D3D11, true);
#else
  return RenderBackend(QRhi::OpenGLES2, false);
#endif
}

OffscreenRenderer::OffscreenRenderer(Renderer& renderer)
  : OffscreenRenderer(renderer, get_default_render_backend()) {}

OffscreenRenderer::OffscreenRenderer(Renderer& renderer, RenderBackend backend)
  : m_renderer(&renderer),
    m_backend(backend),
    m_rhi(make_rhi(backend, m_surface)) {}

OffscreenRenderer::~OffscreenRenderer() {
  if(m_target) {
    m_renderer->release();
  }
}

RenderBackend OffscreenRenderer::get_backend() const {
  return m_backend;
}

QRhi& OffscreenRenderer::get_rhi() {
  return *m_rhi;
}

QImage OffscreenRenderer::render(QSize size, double scale) {
  prepare(size);
  auto buffer = static_cast<QRhiCommandBuffer*>(nullptr);
  if(m_rhi->beginOffscreenFrame(&buffer) != QRhi::FrameOpSuccess) {
    throw std::runtime_error("Unable to begin an offscreen frame.");
  }
  auto result = QRhiReadbackResult();
  try {
    m_renderer->render(*buffer, *m_target, scale);
    auto batch = m_rhi->nextResourceUpdateBatch();
    batch->readBackTexture(QRhiReadbackDescription(m_texture.get()), &result);
    buffer->resourceUpdate(batch);
  } catch(...) {
    m_rhi->endOffscreenFrame();
    throw;
  }
  if(m_rhi->endOffscreenFrame() != QRhi::FrameOpSuccess) {
    throw std::runtime_error("Unable to end an offscreen frame.");
  }
  auto image = to_image(result, m_rhi->isYUpInFramebuffer());
  image.setDevicePixelRatio(scale);
  return image;
}

void OffscreenRenderer::prepare(QSize size) {
  auto limit = m_rhi->resourceLimit(QRhi::TextureSizeMax);
  if(size.isEmpty() || size.width() > limit || size.height() > limit) {
    throw std::invalid_argument("The frame size is not supported.");
  }
  if(m_target) {
    if(m_texture->pixelSize() != size) {
      m_texture->setPixelSize(size);
      create(*m_texture);
      m_depth_stencil->setPixelSize(size);
      create(*m_depth_stencil);
      create(*m_target);
    }
    return;
  }
  m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, size, 1,
    QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
  create(*m_texture);
  m_depth_stencil.reset(
    m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
  create(*m_depth_stencil);
  auto target = std::unique_ptr<QRhiTextureRenderTarget>(
    m_rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(
      QRhiColorAttachment(m_texture.get()), m_depth_stencil.get())));
  m_descriptor.reset(target->newCompatibleRenderPassDescriptor());
  target->setRenderPassDescriptor(m_descriptor.get());
  create(*target);
  m_renderer->initialize(*m_rhi, *m_descriptor, 1);
  m_target = std::move(target);
}
