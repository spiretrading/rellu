#include <algorithm>
#include <cmath>
#include <QFontMetricsF>
#include <QPainter>
#include "Rellu/Tools/ArtboardTitles.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_WIDTH = 12.0;
  constexpr auto MAXIMUM_WIDTHS = std::size_t(1 << 14);
  const auto TEXT_COLOR = QColor(112, 112, 112);
  const auto HIGHLIGHT_COLOR = QColor(38, 128, 235);
}

ArtboardTitles::ArtboardTitles()
    : m_generation(0) {
  m_font.setPixelSize(static_cast<int>(FONT_SIZE));
}

void ArtboardTitles::lay_out(
    const Document& document, const Matrix& view, Size size) {
  m_titles.clear();
  auto screen = Rect(0, 0, size.m_width, size.m_height);
  auto metrics = QFontMetricsF(m_font);
  if(m_widths.size() > MAXIMUM_WIDTHS) {
    m_widths.clear();
  }
  for(auto& id : document.get_canvas()) {
    auto node = document.find(id);
    if(!node || !node->m_header.m_is_visible) {
      continue;
    }
    auto artboard = node->get_artboard();
    if(!artboard) {
      continue;
    }
    auto frame = view.map(artboard->m_frame);
    if(frame.m_width < MINIMUM_WIDTH) {
      continue;
    }
    auto bounds = Rect(frame.m_x, frame.m_y - GAP - HEIGHT, frame.m_width,
      HEIGHT);
    if(!bounds.intersects(screen)) {
      continue;
    }
    auto& name = node->m_header.m_name;
    auto width = m_widths.find(name);
    if(width == m_widths.end()) {
      width = m_widths.emplace(name, metrics.horizontalAdvance(
        QString::fromStdString(name))).first;
    }
    auto text = QString::fromStdString(name);
    if(width->second > frame.m_width) {
      text = metrics.elidedText(text, Qt::ElideRight, frame.m_width);
      if(text.isEmpty()) {
        continue;
      }
      bounds.m_width = std::min(frame.m_width, metrics.horizontalAdvance(text));
    } else {
      bounds.m_width = width->second;
    }
    m_titles.push_back(Title(id, std::move(text), bounds));
  }
}

const std::vector<ArtboardTitles::Title>& ArtboardTitles::get_titles() const {
  return m_titles;
}

NodeId ArtboardTitles::find(Point position) const {
  for(auto i = m_titles.rbegin(); i != m_titles.rend(); ++i) {
    if(i->m_bounds.contains(position)) {
      return i->m_artboard;
    }
  }
  return NodeId();
}

const QFont& ArtboardTitles::get_font() const {
  return m_font;
}

void ArtboardTitles::set_hidden(const NodeId& artboard) {
  m_hidden = artboard;
}

void ArtboardTitles::draw(CanvasOverlay& overlay, double scale,
    const std::unordered_set<NodeId>& highlights) {
  ++m_generation;
  for(auto& title : m_titles) {
    if(title.m_artboard == m_hidden) {
      continue;
    }
    auto is_highlighted = highlights.contains(title.m_artboard);
    auto key = QString("%1|%2|%3").arg(title.m_text).arg(
      is_highlighted).arg(scale);
    auto& image = m_images[key];
    if(!image.m_image) {
      auto width = static_cast<int>(std::ceil(title.m_bounds.m_width * scale));
      auto height = static_cast<int>(std::ceil(HEIGHT * scale));
      auto raster = std::make_shared<QImage>(std::max(width, 1),
        std::max(height, 1), QImage::Format_RGBA8888_Premultiplied);
      raster->fill(Qt::transparent);
      auto painter = QPainter(raster.get());
      auto font = m_font;
      font.setPixelSize(std::max(1, static_cast<int>(
        std::lround(FONT_SIZE * scale))));
      painter.setFont(font);
      painter.setPen(is_highlighted ? HIGHLIGHT_COLOR : TEXT_COLOR);
      painter.drawText(QRectF(0, 0, raster->width(), raster->height()),
        Qt::AlignLeft | Qt::AlignBottom | Qt::TextSingleLine, title.m_text);
      painter.end();
      image.m_image = std::move(raster);
    }
    image.m_generation = m_generation;
    overlay.m_images.push_back(
      OverlayImage(image.m_image, title.m_bounds.get_top_left()));
  }
  std::erase_if(m_images, [&] (const auto& entry) {
    return entry.second.m_generation != m_generation;
  });
}
