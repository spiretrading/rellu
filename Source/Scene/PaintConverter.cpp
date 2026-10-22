#include <algorithm>
#include <utility>
#include <variant>
#include "Rellu/Scene/PaintConverter.hpp"

using namespace Rellu;

PaintConverter::PaintConverter(const Document& document)
  : m_document(&document) {}

std::optional<Paint> PaintConverter::convert(
    const Fill& fill, const Path& outline) {
  if(auto solid = std::get_if<SolidFill>(&fill)) {
    return SolidPaint(solid->m_color);
  } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
    if(gradient->m_stops.empty()) {
      return std::nullopt;
    }
    auto transform = gradient->m_transform;
    auto geometry = gradient->m_geometry;
    if(gradient->m_units == GradientUnits::OBJECT_BOUNDING_BOX) {
      auto bounds = get_bounds(outline);
      if(std::holds_alternative<RadialGradient>(geometry) &&
          bounds.m_width > 0 && bounds.m_height > 0) {
        return GradientPaint(std::move(geometry), gradient->m_stops,
          get_radial_transform(*gradient, bounds));
      }
      transform = make_translation(bounds.m_x, bounds.m_y) *
        make_scale(bounds.m_width, bounds.m_height) * transform;
      if(auto linear = std::get_if<LinearGradient>(&geometry)) {
        linear->m_start = transform.map(linear->m_start);
        linear->m_end = transform.map(linear->m_end);
        transform = Matrix();
      }
    }
    return GradientPaint(std::move(geometry), gradient->m_stops, transform);
  } else if(auto image_fill = std::get_if<ImageFill>(&fill)) {
    auto image = get_image(image_fill->m_image);
    if(!image) {
      return std::nullopt;
    }
    return ImagePaint(image,
      get_image_transform(*image_fill, get_bounds(outline), image->size()));
  }
  return std::nullopt;
}

std::shared_ptr<const QImage> PaintConverter::get_image(const ImageId& id) {
  auto& images = m_document->get_resources().m_images;
  auto resource = images.find(id);
  if(resource == images.end()) {
    m_images.erase(id);
    return nullptr;
  }
  auto& data = resource->second.m_data.get_pointer();
  auto& entry = m_images[id];
  if(entry.m_data != data) {
    entry.m_data = data;
    auto image = QImage::fromData(
      reinterpret_cast<const uchar*>(data->data()),
      static_cast<int>(data->size()));
    if(image.isNull()) {
      entry.m_image = nullptr;
    } else {
      entry.m_image = std::make_shared<const QImage>(std::move(image));
    }
  }
  return entry.m_image;
}

Matrix Rellu::get_image_transform(
    const ImageFill& fill, const Rect& bounds, QSize size) {
  auto width = fill.m_size.m_width;
  auto height = fill.m_size.m_height;
  if(!(width > 0) || !(height > 0)) {
    width = size.width();
    height = size.height();
  }
  if(!(width > 0) || !(height > 0) || size.isEmpty()) {
    return Matrix();
  }
  auto horizontal = bounds.m_width / width;
  auto vertical = bounds.m_height / height;
  if(fill.m_scale_behavior == ScaleBehavior::COVER) {
    horizontal = std::max(horizontal, vertical);
    vertical = horizontal;
  }
  horizontal *= fill.m_scale * fill.m_horizontal_scale;
  vertical *= fill.m_scale * fill.m_vertical_scale;
  if(fill.m_is_flipped_horizontally) {
    horizontal = -horizontal;
  }
  if(fill.m_is_flipped_vertically) {
    vertical = -vertical;
  }
  auto center = bounds.get_center() + fill.m_offset;
  return make_translation(center.m_x, center.m_y) *
    make_scale(horizontal, vertical) *
    make_translation(-width / 2, -height / 2) *
    make_scale(width / size.width(), height / size.height());
}
