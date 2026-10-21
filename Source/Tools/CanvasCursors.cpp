#include <cmath>
#include <initializer_list>
#include <numbers>
#include <vector>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QTransform>
#include "Rellu/Tools/CanvasCursors.hpp"

using namespace Rellu;

namespace {
  constexpr auto OUTLINE = 2.0;
  constexpr auto LINE_WIDTH = 1.2;
  constexpr auto BADGE_WIDTH = 1.4;
  constexpr auto CROSSHAIR_REACH = 9.3;
  constexpr auto CROSSHAIR_GAP = 2.2;
  constexpr auto DOT_SIZE = 1.2;
  constexpr auto PEN_SLANT = -30.0;
  constexpr auto EIGHTH = 45.0;

  struct Glyph {
    std::vector<QPainterPath> m_ink;
    QPainterPath m_paper;
  };

  QPainterPath stroke(const QPainterPath& path, double width,
      Qt::PenCapStyle cap = Qt::FlatCap) {
    auto stroker = QPainterPathStroker();
    stroker.setWidth(width);
    stroker.setCapStyle(cap);
    stroker.setJoinStyle(Qt::MiterJoin);
    return stroker.createStroke(path);
  }

  QPainterPath make_line(QPointF start, QPointF end, double width,
      Qt::PenCapStyle cap = Qt::FlatCap) {
    auto path = QPainterPath(start);
    path.lineTo(end);
    return stroke(path, width, cap);
  }

  QPainterPath make_polygon(std::initializer_list<QPointF> points) {
    auto path = QPainterPath();
    path.addPolygon(QPolygonF(points));
    path.closeSubpath();
    return path;
  }

  QPainterPath make_rect(double left, double top, double right,
      double bottom) {
    auto path = QPainterPath();
    path.addRect(QRectF(QPointF(left, top), QPointF(right, bottom)));
    return path;
  }

  QPainterPath make_circle(QPointF center, double radius) {
    auto path = QPainterPath();
    path.addEllipse(center, radius, radius);
    return path;
  }

  QPainterPath make_capsule(QPointF start, QPointF end, double width) {
    return make_line(start, end, width, Qt::RoundCap);
  }

  Glyph make_arrow() {
    auto glyph = Glyph();
    glyph.m_ink.push_back(make_polygon({QPointF(0, 0), QPointF(0, 15),
      QPointF(3.5, 11.6), QPointF(5.9, 17), QPointF(8.1, 16.1),
      QPointF(5.8, 10.8), QPointF(10.6, 10.6)}));
    return glyph;
  }

  Glyph make_resize() {
    auto glyph = Glyph();
    glyph.m_ink.push_back(make_polygon(
      {QPointF(-9.5, 0), QPointF(-4.5, -4.3), QPointF(-4.5, 4.3)}));
    glyph.m_ink.push_back(make_polygon(
      {QPointF(9.5, 0), QPointF(4.5, -4.3), QPointF(4.5, 4.3)}));
    glyph.m_ink.push_back(make_rect(-4.6, -0.6, -1.8, 0.6));
    glyph.m_ink.push_back(make_rect(1.8, -0.6, 4.6, 0.6));
    glyph.m_ink.push_back(make_rect(-0.6, -3.4, 0.6, 3.4));
    return glyph;
  }

  Glyph make_rotate() {
    auto center = QPointF(-4.67, -4.67);
    auto radius = 8.67;
    auto bounds = QRectF(center.x() - radius, center.y() - radius,
      2 * radius, 2 * radius);
    auto arc = QPainterPath();
    arc.arcMoveTo(bounds, 0);
    arc.arcTo(bounds, 0, -90);
    auto glyph = Glyph();
    glyph.m_ink.push_back(stroke(arc, LINE_WIDTH));
    auto end = center.x() + radius;
    glyph.m_ink.push_back(make_polygon({QPointF(end, -7.33),
      QPointF(end - 3.33, center.y()), QPointF(end + 3.33, center.y())}));
    glyph.m_ink.push_back(make_polygon({QPointF(-7.33, end),
      QPointF(center.x(), end - 3.33), QPointF(center.x(), end + 3.33)}));
    return glyph;
  }

  Glyph make_crosshair(CursorKind kind) {
    auto glyph = Glyph();
    glyph.m_ink.push_back(make_line(QPointF(-CROSSHAIR_REACH, 0),
      QPointF(-CROSSHAIR_GAP, 0), LINE_WIDTH));
    glyph.m_ink.push_back(make_line(QPointF(0, -CROSSHAIR_REACH),
      QPointF(0, -CROSSHAIR_GAP), LINE_WIDTH));
    glyph.m_ink.push_back(make_line(QPointF(CROSSHAIR_GAP, 0),
      QPointF(CROSSHAIR_REACH, 0), LINE_WIDTH));
    glyph.m_ink.push_back(make_line(QPointF(0, CROSSHAIR_GAP),
      QPointF(0, CROSSHAIR_REACH), LINE_WIDTH));
    glyph.m_ink.push_back(make_rect(-DOT_SIZE / 2, -DOT_SIZE / 2,
      DOT_SIZE / 2, DOT_SIZE / 2));
    if(kind == CursorKind::RECTANGLE) {
      auto square = QPainterPath();
      square.addRect(QRectF(5.5, 5.5, 5.5, 5.5));
      glyph.m_paper = square;
      glyph.m_ink.push_back(stroke(square, BADGE_WIDTH));
    } else if(kind == CursorKind::ELLIPSE) {
      auto circle = make_circle(QPointF(8.3, 8.3), 2.8);
      glyph.m_paper = circle;
      glyph.m_ink.push_back(stroke(circle, BADGE_WIDTH));
      glyph.m_ink.push_back(make_circle(QPointF(8.3, 8.3), 0.9));
    } else if(kind == CursorKind::POLYGON) {
      auto triangle = make_polygon(
        {QPointF(8.3, 4.6), QPointF(11.7, 10.8), QPointF(4.9, 10.8)});
      glyph.m_paper = triangle;
      glyph.m_ink.push_back(stroke(triangle, BADGE_WIDTH));
    } else {
      glyph.m_ink.push_back(make_line(
        QPointF(5, 12), QPointF(11.5, 5.5), BADGE_WIDTH, Qt::RoundCap));
    }
    return glyph;
  }

  Glyph make_artboard() {
    auto glyph = Glyph();
    glyph.m_ink.push_back(
      make_line(QPointF(-8, 0), QPointF(-CROSSHAIR_GAP, 0), LINE_WIDTH));
    glyph.m_ink.push_back(
      make_line(QPointF(0, -8), QPointF(0, -CROSSHAIR_GAP), LINE_WIDTH));
    auto frame = QPainterPath(QPointF(CROSSHAIR_GAP, 0));
    frame.lineTo(10, 0);
    frame.lineTo(10, 10);
    frame.lineTo(0, 10);
    frame.lineTo(0, CROSSHAIR_GAP);
    glyph.m_ink.push_back(stroke(frame, LINE_WIDTH));
    return glyph;
  }

  Glyph make_pen() {
    auto nib = make_polygon({QPointF(0, 0), QPointF(5.2, 8.5),
      QPointF(3.6, 12), QPointF(-3.6, 12), QPointF(-5.2, 8.5)});
    nib = nib.subtracted(make_rect(-0.4, 1.8, 0.4, 7.4));
    nib = nib.subtracted(make_circle(QPointF(0, 8.2), 1.3));
    auto glyph = Glyph();
    glyph.m_ink.push_back(nib);
    glyph.m_ink.push_back(make_rect(-3.8, 13.2, 3.8, 16));
    return glyph;
  }

  Glyph make_text() {
    auto beam = QPainterPath(QPointF(-3.3, -8.5));
    beam.quadTo(-0.3, -8.5, 0, -6.5);
    beam.quadTo(0.3, -8.5, 3.3, -8.5);
    beam.moveTo(0, -6.5);
    beam.lineTo(0, 6.5);
    beam.moveTo(-3.3, 8.5);
    beam.quadTo(-0.3, 8.5, 0, 6.5);
    beam.quadTo(0.3, 8.5, 3.3, 8.5);
    auto glyph = Glyph();
    glyph.m_ink.push_back(stroke(beam, LINE_WIDTH));
    return glyph;
  }

  Glyph make_zoom(bool is_zooming_out) {
    auto lens = make_circle(QPointF(0, 0), 5);
    auto glyph = Glyph();
    glyph.m_paper = lens;
    glyph.m_ink.push_back(stroke(lens, 1.4));
    glyph.m_ink.push_back(make_line(
      QPointF(4.2, 4.2), QPointF(10, 10), 2.6, Qt::RoundCap));
    glyph.m_ink.push_back(make_line(QPointF(-2.7, 0), QPointF(2.7, 0), 1.4));
    if(!is_zooming_out) {
      glyph.m_ink.push_back(
        make_line(QPointF(0, -2.7), QPointF(0, 2.7), 1.4));
    }
    return glyph;
  }

  Glyph make_hand(bool is_closed) {
    auto columns = std::initializer_list<double>({-3.9, -1.3, 1.3, 3.9});
    auto open = std::initializer_list<double>({-7, -8, -7.6, -5.8});
    auto closed = std::initializer_list<double>({-3.6, -4.1, -3.9, -3.2});
    auto tops = is_closed ? closed : open;
    auto hand = QPainterPath();
    hand.addRoundedRect(QRectF(-5.1, -1.5, 10.2, is_closed ? 8 : 8.5),
      3.2, 3.2);
    auto top = tops.begin();
    for(auto column : columns) {
      hand = hand.united(
        make_capsule(QPointF(column, *top), QPointF(column, 1), 2.4));
      ++top;
    }
    if(is_closed) {
      hand = hand.united(
        make_capsule(QPointF(-4.3, 2.8), QPointF(-6.3, 0.6), 2.4));
    } else {
      hand = hand.united(
        make_capsule(QPointF(-4.5, 3.5), QPointF(-8, -0.5), 2.6));
    }
    auto glyph = Glyph();
    glyph.m_paper = hand;
    glyph.m_ink.push_back(stroke(hand, 1.1));
    return glyph;
  }

  Glyph make_guide() {
    auto glyph = Glyph();
    glyph.m_ink.push_back(make_polygon(
      {QPointF(0, -7.5), QPointF(4.7, -2.3), QPointF(-4.7, -2.3)}));
    glyph.m_ink.push_back(make_polygon(
      {QPointF(0, 7.5), QPointF(4.7, 2.3), QPointF(-4.7, 2.3)}));
    glyph.m_ink.push_back(make_rect(-7, -0.7, 7, 0.7));
    return glyph;
  }

  Glyph make_glyph(const ToolCursor& cursor) {
    if(cursor.m_kind == CursorKind::RESIZE) {
      return make_resize();
    } else if(cursor.m_kind == CursorKind::ROTATE) {
      return make_rotate();
    } else if(cursor.m_kind == CursorKind::RECTANGLE ||
        cursor.m_kind == CursorKind::ELLIPSE ||
        cursor.m_kind == CursorKind::POLYGON ||
        cursor.m_kind == CursorKind::LINE) {
      return make_crosshair(cursor.m_kind);
    } else if(cursor.m_kind == CursorKind::ARTBOARD) {
      return make_artboard();
    } else if(cursor.m_kind == CursorKind::PEN) {
      return make_pen();
    } else if(cursor.m_kind == CursorKind::TEXT) {
      return make_text();
    } else if(cursor.m_kind == CursorKind::ZOOM_IN ||
        cursor.m_kind == CursorKind::ZOOM_OUT) {
      return make_zoom(cursor.m_kind == CursorKind::ZOOM_OUT);
    } else if(cursor.m_kind == CursorKind::OPEN_HAND ||
        cursor.m_kind == CursorKind::CLOSED_HAND) {
      return make_hand(cursor.m_kind == CursorKind::CLOSED_HAND);
    } else if(cursor.m_kind == CursorKind::HORIZONTAL_GUIDE ||
        cursor.m_kind == CursorKind::VERTICAL_GUIDE) {
      return make_guide();
    }
    return make_arrow();
  }

  QTransform get_placement(const ToolCursor& cursor) {
    auto hot_spot = QPointF(get_hot_spot(cursor));
    if(cursor.m_kind == CursorKind::ARROW) {
      return QTransform::fromTranslate(hot_spot.x(), hot_spot.y());
    } else if(cursor.m_kind == CursorKind::PEN) {
      return QTransform::fromTranslate(hot_spot.x(), hot_spot.y()).rotate(
        PEN_SLANT);
    }
    auto placement =
      QTransform::fromTranslate(hot_spot.x() + 0.5, hot_spot.y() + 0.5);
    if(cursor.m_kind == CursorKind::RESIZE) {
      placement.rotate(EIGHTH * get_orientation(cursor));
    } else if(cursor.m_kind == CursorKind::ROTATE) {
      placement.rotate(EIGHTH * (get_orientation(cursor) - 1));
    } else if(cursor.m_kind == CursorKind::VERTICAL_GUIDE) {
      placement.rotate(2 * EIGHTH);
    }
    return placement;
  }
}

int Rellu::get_orientation(const ToolCursor& cursor) {
  if(cursor.m_kind != CursorKind::RESIZE &&
      cursor.m_kind != CursorKind::ROTATE) {
    return 0;
  }
  auto index = static_cast<int>(
    std::lround(cursor.m_angle / (std::numbers::pi / 4)) % 8);
  if(index < 0) {
    index += 8;
  }
  if(cursor.m_kind == CursorKind::RESIZE) {
    return index % 4;
  }
  return index;
}

QPoint Rellu::get_hot_spot(const ToolCursor& cursor) {
  if(cursor.m_kind == CursorKind::RESIZE ||
      cursor.m_kind == CursorKind::RECTANGLE ||
      cursor.m_kind == CursorKind::ELLIPSE ||
      cursor.m_kind == CursorKind::POLYGON ||
      cursor.m_kind == CursorKind::LINE) {
    return QPoint(9, 9);
  } else if(cursor.m_kind == CursorKind::ROTATE) {
    return QPoint(7, 7);
  } else if(cursor.m_kind == CursorKind::ARTBOARD ||
      cursor.m_kind == CursorKind::ZOOM_IN ||
      cursor.m_kind == CursorKind::ZOOM_OUT) {
    return QPoint(8, 8);
  } else if(cursor.m_kind == CursorKind::TEXT) {
    return QPoint(4, 10);
  } else if(cursor.m_kind == CursorKind::OPEN_HAND ||
      cursor.m_kind == CursorKind::CLOSED_HAND) {
    return QPoint(11, 11);
  } else if(cursor.m_kind == CursorKind::HORIZONTAL_GUIDE) {
    return QPoint(7, 8);
  } else if(cursor.m_kind == CursorKind::VERTICAL_GUIDE) {
    return QPoint(8, 7);
  }
  return QPoint(1, 1);
}

QImage Rellu::draw_cursor(const ToolCursor& cursor, double ratio) {
  auto size = static_cast<int>(std::ceil(CANVAS_CURSOR_SIZE * ratio));
  auto image = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(ratio);
  image.fill(Qt::transparent);
  auto glyph = make_glyph(cursor);
  auto placement = get_placement(cursor);
  auto painter = QPainter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  auto outline = QPen(
    Qt::white, OUTLINE, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  for(const auto& ink : glyph.m_ink) {
    painter.strokePath(placement.map(ink), outline);
  }
  painter.fillPath(placement.map(glyph.m_paper), Qt::white);
  for(const auto& ink : glyph.m_ink) {
    painter.fillPath(placement.map(ink), Qt::black);
  }
  painter.end();
  return image;
}
