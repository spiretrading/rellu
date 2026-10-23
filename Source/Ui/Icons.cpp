#include <array>
#include <cmath>
#include <concepts>
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>
#include <variant>
#include <QFont>
#include <QImage>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QPolygonF>
#include <QRect>
#include "Colors.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto GRID = 24.0;
  constexpr auto WEIGHT = 1.5;
  const auto LAYER_INK = QColor(0xBD, 0xBD, 0xBD);
  const auto LAYER_LINE_INK = QColor(0x9A, 0x9A, 0x9A);
  const auto ARTBOARD_INK = QColor(0x66, 0x66, 0x66);
  const auto RECTANGLE_INK = QColor(0xC4, 0xC4, 0xC4);

  QPen make_pen(const QColor& color, double width = WEIGHT,
      Qt::PenCapStyle cap = Qt::FlatCap,
      Qt::PenJoinStyle join = Qt::MiterJoin) {
    auto pen = QPen(color, width);
    pen.setCapStyle(cap);
    pen.setJoinStyle(join);
    return pen;
  }

  QPolygonF make_polygon(std::initializer_list<QPointF> points) {
    return QPolygonF(QList<QPointF>(points));
  }

  void stroke(QPainter& painter, const QColor& color, const QPainterPath& path,
      double width = WEIGHT) {
    painter.setPen(make_pen(color, width));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
  }

  void fill(QPainter& painter, const QColor& color, const QPainterPath& path) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(path);
  }

  void polyline(QPainter& painter, const QColor& color,
      std::initializer_list<QPointF> points, double width = WEIGHT) {
    painter.setPen(make_pen(color, width, Qt::FlatCap, Qt::MiterJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(make_polygon(points));
  }

  void fill_polygon(QPainter& painter, const QColor& color,
      std::initializer_list<QPointF> points) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(make_polygon(points));
  }

  void fill_rect(QPainter& painter, const QColor& color, const QRectF& rect) {
    painter.fillRect(rect, color);
  }

  void stroke_rect(
      QPainter& painter, const QColor& color, const QRectF& rect) {
    painter.setPen(make_pen(color));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
  }

  void line(QPainter& painter, const QColor& color, QPointF from, QPointF to,
      double width = WEIGHT, Qt::PenCapStyle cap = Qt::FlatCap) {
    painter.setPen(make_pen(color, width, cap));
    painter.drawLine(from, to);
  }

  void text(QPainter& painter, const QColor& color, const QRectF& rect,
      const QString& label, double size, bool is_serif = true,
      int flags = Qt::AlignCenter) {
    auto font = QFont(is_serif ? "Times New Roman" : "Segoe UI");
    font.setPixelSize(static_cast<int>(std::round(size * 4)));
    painter.save();
    painter.scale(0.25, 0.25);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QRectF(rect.x() * 4, rect.y() * 4, rect.width() * 4,
      rect.height() * 4), flags, label);
    painter.restore();
  }

  void arrow_head(QPainter& painter, const QColor& color, QPointF tip,
      double dx, double dy) {
    auto length = std::hypot(dx, dy);
    auto ux = dx / length;
    auto uy = dy / length;
    auto size = 2.5;
    auto back = QPointF(tip.x() - ux * size, tip.y() - uy * size);
    fill_polygon(painter, color, {tip,
      QPointF(back.x() - uy * size, back.y() + ux * size),
      QPointF(back.x() + uy * size, back.y() - ux * size)});
  }

  void double_arrow(
      QPainter& painter, const QColor& color, QPointF from, QPointF to) {
    line(painter, color, from, to);
    arrow_head(painter, color, to, to.x() - from.x(), to.y() - from.y());
    arrow_head(painter, color, from, from.x() - to.x(), from.y() - to.y());
  }

  void paint_boolean(QPainter& painter, const QColor& color, Glyph glyph) {
    auto back = QPainterPath();
    back.addRect(QRectF(4.75, 4.75, 10, 10));
    auto front = QPainterPath();
    front.addRect(QRectF(9.25, 9.25, 10, 10));
    auto faint = color;
    faint.setAlphaF(0.35f * color.alphaF());
    if(glyph == Glyph::BOOLEAN_ADD) {
      fill(painter, faint, back.united(front));
      stroke(painter, color, back.united(front));
    } else if(glyph == Glyph::BOOLEAN_SUBTRACT) {
      fill(painter, faint, back.subtracted(front));
      stroke(painter, color, back.subtracted(front));
      painter.setPen(make_pen(color, 1, Qt::FlatCap));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(QRectF(9.25, 9.25, 10, 10));
    } else if(glyph == Glyph::BOOLEAN_INTERSECT) {
      fill(painter, faint, back.intersected(front));
      painter.setPen(make_pen(color, 1));
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(back);
      painter.drawPath(front);
      stroke(painter, color, back.intersected(front));
    } else {
      fill(painter, faint, back.united(front).subtracted(
        back.intersected(front)));
      painter.setPen(make_pen(color, 1));
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(back);
      painter.drawPath(front);
    }
  }

  void paint_stroke_alignment(
      QPainter& painter, const QColor& color, double offset) {
    painter.setPen(make_pen(color, 3.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(make_polygon({QPointF(8 + offset, 4),
      QPointF(8 + offset, 16 - offset), QPointF(20, 16 - offset)}));
    auto thin = color;
    thin.setAlphaF(0.6f * color.alphaF());
    painter.setPen(make_pen(thin, 1));
    painter.drawPolyline(make_polygon({QPointF(8, 4), QPointF(8, 16),
      QPointF(20, 16)}));
  }

  void paint_cap(QPainter& painter, const QColor& color, Qt::PenCapStyle cap) {
    auto path = QPainterPath(QPointF(3, 12));
    path.lineTo(QPointF(13, 12));
    auto stroker = QPainterPathStroker();
    stroker.setWidth(9);
    stroker.setCapStyle(cap);
    auto outline = stroker.createStroke(path);
    auto clip = QPainterPath();
    clip.addRect(QRectF(4, 0, 20, GRID));
    auto faint = color;
    faint.setAlphaF(0.3f * color.alphaF());
    fill(painter, faint, outline.intersected(clip));
    stroke(painter, color, outline.intersected(clip), 1.2);
    line(painter, color, QPointF(4, 12), QPointF(20, 12), 1.2);
  }

  void paint_join(
      QPainter& painter, const QColor& color, Qt::PenJoinStyle join) {
    painter.setPen(make_pen(color, 6, Qt::FlatCap, join));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(make_polygon(
      {QPointF(8, 21), QPointF(8, 8), QPointF(21, 8)}));
  }

  void paint_lines(QPainter& painter, const QColor& color,
      std::initializer_list<std::pair<double, double>> spans) {
    auto y = 6.0;
    for(auto& span : spans) {
      line(painter, color, QPointF(span.first, y), QPointF(span.second, y));
      y += 4;
    }
  }

  void paint_glyph(QPainter& painter, Glyph glyph, const QColor& color) {
    switch(glyph) {
      case Glyph::SELECT:
        fill_polygon(painter, color, {QPointF(7, 3), QPointF(7, 19.5),
          QPointF(11, 15.5), QPointF(13.8, 21.2), QPointF(16.4, 20),
          QPointF(13.6, 14.4), QPointF(19, 14.4)});
        break;
      case Glyph::RECTANGLE:
        stroke_rect(painter, color, QRectF(4.75, 4.75, 14.5, 14.5));
        break;
      case Glyph::ELLIPSE:
        painter.setPen(make_pen(color));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(12, 12), 7.5, 7.5);
        break;
      case Glyph::POLYGON:
        painter.setPen(make_pen(color));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(make_polygon(
          {QPointF(12, 4.25), QPointF(20, 19), QPointF(4, 19)}));
        break;
      case Glyph::LINE:
        line(painter, color, QPointF(5, 19), QPointF(19, 5));
        break;
      case Glyph::PEN:
      {
        painter.save();
        painter.translate(12, 12);
        painter.rotate(45);
        painter.translate(-12, -12);
        auto nib = QPainterPath(QPointF(12, 21));
        nib.lineTo(6.5, 11.5);
        nib.lineTo(9, 5.5);
        nib.lineTo(15, 5.5);
        nib.lineTo(17.5, 11.5);
        nib.closeSubpath();
        stroke(painter, color, nib);
        line(painter, color, QPointF(9, 3), QPointF(15, 3));
        line(painter, color, QPointF(12, 21), QPointF(12, 14.5), 1.2);
        painter.setPen(make_pen(color, 1.2));
        painter.drawEllipse(QPointF(12, 12.8), 1.5, 1.5);
        painter.restore();
        break;
      }
      case Glyph::TEXT:
        fill_rect(painter, color, QRectF(5, 4, 14, 2.4));
        fill_rect(painter, color, QRectF(5, 4, 1.8, 4.5));
        fill_rect(painter, color, QRectF(17.2, 4, 1.8, 4.5));
        fill_rect(painter, color, QRectF(10.8, 4, 2.4, 15.5));
        fill_rect(painter, color, QRectF(8.8, 18.4, 6.4, 1.8));
        break;
      case Glyph::ARTBOARD:
        stroke_rect(painter, color, QRectF(7.75, 7.75, 11.5, 11.5));
        line(painter, color, QPointF(7.75, 3), QPointF(7.75, 6.5));
        line(painter, color, QPointF(3, 7.75), QPointF(6.5, 7.75));
        break;
      case Glyph::ZOOM:
        painter.setPen(make_pen(color));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(10.5, 10.5), 6, 6);
        line(painter, color, QPointF(14.8, 14.8), QPointF(20, 20), 2);
        break;
      case Glyph::ASSETS:
        stroke_rect(painter, color, QRectF(8.75, 6.75, 10.5, 13.5));
        polyline(painter, color, {QPointF(5.25, 17), QPointF(5.25, 3.25),
          QPointF(15.5, 3.25)});
        fill_polygon(painter, color, {QPointF(12.5, 6.75),
          QPointF(16.5, 6.75), QPointF(16.5, 12.75), QPointF(14.5, 11),
          QPointF(12.5, 12.75)});
        break;
      case Glyph::LAYERS:
        fill_polygon(painter, color, {QPointF(12, 3.5), QPointF(20.5, 8.5),
          QPointF(12, 13.5), QPointF(3.5, 8.5)});
        fill_polygon(painter, color, {QPointF(3.5, 12.5), QPointF(12, 17.5),
          QPointF(20.5, 12.5), QPointF(20.5, 14.8), QPointF(12, 19.8),
          QPointF(3.5, 14.8)});
        break;
      case Glyph::PLUGINS:
        fill_rect(painter, color, QRectF(3.5, 9.5, 17, 10));
        fill_rect(painter, color, QRectF(6, 6, 4, 3));
        fill_rect(painter, color, QRectF(14, 6, 4, 3));
        break;
      case Glyph::HOME:
        fill_polygon(painter, color, {QPointF(12, 3.5), QPointF(21, 11.5),
          QPointF(18.5, 11.5), QPointF(18.5, 20.5), QPointF(14, 20.5),
          QPointF(14, 15), QPointF(10, 15), QPointF(10, 20.5),
          QPointF(5.5, 20.5), QPointF(5.5, 11.5), QPointF(3, 11.5)});
        break;
      case Glyph::INVITE:
      {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(10.5, 8.5), 4.25, 4.25);
        auto body = QPainterPath(QPointF(3.5, 20.5));
        body.cubicTo(3.5, 15.5, 6.5, 13.8, 10.5, 13.8);
        body.cubicTo(12.5, 13.8, 14, 14.3, 15, 15);
        body.lineTo(15, 20.5);
        body.closeSubpath();
        fill(painter, color, body);
        line(painter, color, QPointF(18.5, 14.5), QPointF(18.5, 21.5));
        line(painter, color, QPointF(15, 18), QPointF(22, 18));
        break;
      }
      case Glyph::DEVICE:
        painter.setPen(make_pen(color));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(7.25, 3.25, 9.5, 17.5), 1.5, 1.5);
        line(painter, color, QPointF(10.5, 17.5), QPointF(13.5, 17.5));
        break;
      case Glyph::PLAY:
        fill_polygon(painter, color,
          {QPointF(7, 4), QPointF(19.5, 12), QPointF(7, 20)});
        break;
      case Glyph::CHEVRON_DOWN:
        polyline(painter, color,
          {QPointF(7, 9.5), QPointF(12, 14.5), QPointF(17, 9.5)});
        break;
      case Glyph::CHEVRON_LEFT:
        polyline(painter, color,
          {QPointF(14.5, 7), QPointF(9.5, 12), QPointF(14.5, 17)});
        break;
      case Glyph::CHEVRON_RIGHT:
        polyline(painter, color,
          {QPointF(9.5, 7), QPointF(14.5, 12), QPointF(9.5, 17)});
        break;
      case Glyph::SEARCH:
        painter.setPen(make_pen(color));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(10.5, 10.5), 5, 5);
        line(painter, color, QPointF(14.2, 14.2), QPointF(18.5, 18.5));
        break;
      case Glyph::FILTER:
        fill_polygon(painter, color, {QPointF(4, 5), QPointF(20, 5),
          QPointF(14, 12.5), QPointF(14, 19.5), QPointF(10, 17.5),
          QPointF(10, 12.5)});
        break;
      case Glyph::SORT:
        line(painter, color, QPointF(4, 6), QPointF(14, 6));
        line(painter, color, QPointF(4, 11), QPointF(12, 11));
        line(painter, color, QPointF(4, 16), QPointF(10, 16));
        line(painter, color, QPointF(18, 4), QPointF(18, 18.5));
        arrow_head(painter, color, QPointF(18, 20), 0, 1);
        break;
      case Glyph::LIST_VIEW:
        line(painter, color, QPointF(4, 19), QPointF(9, 5));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for(auto x : {12.0, 15.5, 19.0}) {
          painter.drawEllipse(QPointF(x, 18), 1, 1);
        }
        break;
      case Glyph::GRID_VIEW:
        fill_rect(painter, color, QRectF(5, 5, 6, 6));
        fill_rect(painter, color, QRectF(13, 5, 6, 6));
        fill_rect(painter, color, QRectF(5, 13, 6, 6));
        fill_rect(painter, color, QRectF(13, 13, 6, 6));
        break;
      case Glyph::SHARE:
        polyline(painter, color, {QPointF(8.5, 9.5), QPointF(5.25, 9.5),
          QPointF(5.25, 20.25), QPointF(18.75, 20.25), QPointF(18.75, 9.5),
          QPointF(15.5, 9.5)});
        line(painter, color, QPointF(12, 15), QPointF(12, 4.5));
        polyline(painter, color,
          {QPointF(8.5, 7.5), QPointF(12, 4), QPointF(15.5, 7.5)});
        break;
      case Glyph::PLUS:
        line(painter, color, QPointF(12, 5), QPointF(12, 19));
        line(painter, color, QPointF(5, 12), QPointF(19, 12));
        break;
      case Glyph::INFO:
        painter.setPen(make_pen(color, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(12, 12), 8, 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(12, 8.2), 1.1, 1.1);
        line(painter, color, QPointF(12, 10.8), QPointF(12, 16.5), 1.6);
        break;
      case Glyph::ALIGN_TOP:
        line(painter, color, QPointF(4, 4.5), QPointF(20, 4.5), 2);
        fill_rect(painter, color, QRectF(6.5, 6, 4, 12));
        fill_rect(painter, color, QRectF(13.5, 6, 4, 8));
        break;
      case Glyph::ALIGN_MIDDLE:
        line(painter, color, QPointF(4, 12), QPointF(20, 12), 2);
        fill_rect(painter, color, QRectF(6.5, 6, 4, 12));
        fill_rect(painter, color, QRectF(13.5, 8, 4, 8));
        break;
      case Glyph::ALIGN_BOTTOM:
        line(painter, color, QPointF(4, 19.5), QPointF(20, 19.5), 2);
        fill_rect(painter, color, QRectF(6.5, 6, 4, 12));
        fill_rect(painter, color, QRectF(13.5, 10, 4, 8));
        break;
      case Glyph::DISTRIBUTE_HORIZONTALLY:
        line(painter, color, QPointF(5, 4), QPointF(5, 20));
        line(painter, color, QPointF(19, 4), QPointF(19, 20));
        fill_rect(painter, color, QRectF(10, 7, 4, 10));
        break;
      case Glyph::ALIGN_LEFT:
        line(painter, color, QPointF(4.5, 4), QPointF(4.5, 20), 2);
        fill_rect(painter, color, QRectF(6, 6.5, 12, 4));
        fill_rect(painter, color, QRectF(6, 13.5, 8, 4));
        break;
      case Glyph::ALIGN_CENTER:
        line(painter, color, QPointF(12, 4), QPointF(12, 20), 2);
        fill_rect(painter, color, QRectF(6, 6.5, 12, 4));
        fill_rect(painter, color, QRectF(8, 13.5, 8, 4));
        break;
      case Glyph::ALIGN_RIGHT:
        line(painter, color, QPointF(19.5, 4), QPointF(19.5, 20), 2);
        fill_rect(painter, color, QRectF(6, 6.5, 12, 4));
        fill_rect(painter, color, QRectF(10, 13.5, 8, 4));
        break;
      case Glyph::DISTRIBUTE_VERTICALLY:
        line(painter, color, QPointF(4, 5), QPointF(20, 5));
        line(painter, color, QPointF(4, 19), QPointF(20, 19));
        fill_rect(painter, color, QRectF(7, 10, 10, 4));
        break;
      case Glyph::REPEAT_GRID:
        stroke_rect(painter, color, QRectF(4.75, 4.75, 6, 6));
        stroke_rect(painter, color, QRectF(13.25, 4.75, 6, 6));
        stroke_rect(painter, color, QRectF(4.75, 13.25, 6, 6));
        stroke_rect(painter, color, QRectF(13.25, 13.25, 6, 6));
        break;
      case Glyph::BOOLEAN_ADD:
      case Glyph::BOOLEAN_SUBTRACT:
      case Glyph::BOOLEAN_INTERSECT:
      case Glyph::BOOLEAN_EXCLUDE:
        paint_boolean(painter, color, glyph);
        break;
      case Glyph::CUBE:
        painter.setPen(make_pen(color, 1.4, Qt::FlatCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(make_polygon({QPointF(12, 3.5),
          QPointF(19.5, 7.75), QPointF(19.5, 16.25), QPointF(12, 20.5),
          QPointF(4.5, 16.25), QPointF(4.5, 7.75)}));
        painter.drawPolyline(make_polygon(
          {QPointF(4.5, 7.75), QPointF(12, 12), QPointF(19.5, 7.75)}));
        painter.drawLine(QPointF(12, 12), QPointF(12, 20.5));
        break;
      case Glyph::ROTATION:
      {
        auto arc = QPainterPath();
        auto bounds = QRectF(5, 5, 14, 14);
        arc.arcMoveTo(bounds, 70);
        arc.arcTo(bounds, 70, 290);
        stroke(painter, color, arc, 1.4);
        fill_polygon(painter, color,
          {QPointF(14.5, 2.5), QPointF(17.5, 6.5), QPointF(12.5, 7.5)});
        break;
      }
      case Glyph::LOCK_ASPECT:
      {
        fill_rect(painter, color, QRectF(8, 11.5, 8, 6.5));
        auto shackle = QPainterPath(QPointF(9.5, 11.5));
        shackle.lineTo(9.5, 9);
        shackle.arcTo(QRectF(9.5, 6.5, 5, 5), 180, -180);
        shackle.lineTo(14.5, 11.5);
        stroke(painter, color, shackle, 1.4);
        break;
      }
      case Glyph::FLIP_HORIZONTAL:
      case Glyph::FLIP_VERTICAL:
      {
        painter.save();
        if(glyph == Glyph::FLIP_VERTICAL) {
          painter.translate(12, 12);
          painter.rotate(90);
          painter.translate(-12, -12);
        }
        line(painter, color, QPointF(12, 4), QPointF(12, 20), 1.2);
        auto faint = color;
        faint.setAlphaF(0.35f * color.alphaF());
        fill_polygon(painter, faint, {QPointF(10, 6.5), QPointF(10, 17.5),
          QPointF(3.5, 17.5)});
        painter.setPen(make_pen(color, 1.2, Qt::FlatCap, Qt::MiterJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(make_polygon({QPointF(14, 6.5),
          QPointF(14, 17.5), QPointF(20.5, 17.5)}));
        painter.restore();
        break;
      }
      case Glyph::SCROLL_HORIZONTAL:
        line(painter, color, QPointF(4.5, 6), QPointF(4.5, 18));
        line(painter, color, QPointF(19.5, 6), QPointF(19.5, 18));
        double_arrow(painter, color, QPointF(7.5, 12), QPointF(16.5, 12));
        break;
      case Glyph::SCROLL_VERTICAL:
        line(painter, color, QPointF(6, 4.5), QPointF(18, 4.5));
        line(painter, color, QPointF(6, 19.5), QPointF(18, 19.5));
        double_arrow(painter, color, QPointF(12, 7.5), QPointF(12, 16.5));
        break;
      case Glyph::SCROLL_PANNING:
        stroke_rect(painter, color, QRectF(4.75, 4.75, 14.5, 14.5));
        double_arrow(painter, color, QPointF(8, 12), QPointF(16, 12));
        double_arrow(painter, color, QPointF(12, 8), QPointF(12, 16));
        break;
      case Glyph::PORTRAIT:
        polyline(painter, color, {QPointF(13.5, 4.75), QPointF(6.75, 4.75),
          QPointF(6.75, 19.25), QPointF(17.25, 19.25), QPointF(17.25, 8.5),
          QPointF(13.5, 4.75), QPointF(13.5, 8.5), QPointF(17.25, 8.5)}, 1.3);
        break;
      case Glyph::LANDSCAPE:
        polyline(painter, color, {QPointF(16, 6.75), QPointF(4.75, 6.75),
          QPointF(4.75, 17.25), QPointF(19.25, 17.25), QPointF(19.25, 10),
          QPointF(16, 6.75), QPointF(16, 10), QPointF(19.25, 10)}, 1.3);
        break;
      case Glyph::CORNERS_SAME:
        painter.setPen(make_pen(color, 1.3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(5.5, 5.5, 13, 13), 3, 3);
        break;
      case Glyph::CORNERS_DIFFERENT:
      {
        auto corners = QPainterPath();
        corners.moveTo(5.5, 10);
        corners.arcTo(QRectF(5.5, 5.5, 8, 8), 180, -90);
        corners.lineTo(10, 5.5);
        corners.moveTo(14, 5.5);
        corners.arcTo(QRectF(10.5, 5.5, 8, 8), 90, -90);
        corners.moveTo(18.5, 14);
        corners.arcTo(QRectF(10.5, 10.5, 8, 8), 0, -90);
        corners.moveTo(10, 18.5);
        corners.arcTo(QRectF(5.5, 10.5, 8, 8), 270, -90);
        stroke(painter, color, corners, 1.3);
        break;
      }
      case Glyph::EYEDROPPER:
        line(painter, color, QPointF(5, 19), QPointF(13.5, 10.5), 1.4,
          Qt::RoundCap);
        line(painter, color, QPointF(11.5, 12.5), QPointF(16, 8), 3.4,
          Qt::RoundCap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(17.3, 6.7), 2.6, 2.6);
        break;
      case Glyph::STROKE_INNER:
        paint_stroke_alignment(painter, color, 2.5);
        break;
      case Glyph::STROKE_OUTER:
        paint_stroke_alignment(painter, color, -2.5);
        break;
      case Glyph::STROKE_CENTER:
        paint_stroke_alignment(painter, color, 0);
        break;
      case Glyph::CAP_BUTT:
        paint_cap(painter, color, Qt::FlatCap);
        break;
      case Glyph::CAP_ROUND:
        paint_cap(painter, color, Qt::RoundCap);
        break;
      case Glyph::CAP_PROJECTING:
        paint_cap(painter, color, Qt::SquareCap);
        break;
      case Glyph::JOIN_MITER:
        paint_join(painter, color, Qt::MiterJoin);
        break;
      case Glyph::JOIN_ROUND:
        paint_join(painter, color, Qt::RoundJoin);
        break;
      case Glyph::JOIN_BEVEL:
        paint_join(painter, color, Qt::BevelJoin);
        break;
      case Glyph::CHARACTER_SPACING:
        text(painter, color, QRectF(2, 1, 20, 14), "AV", 10, false);
        double_arrow(painter, color, QPointF(4, 19), QPointF(20, 19));
        break;
      case Glyph::LINE_SPACING:
        double_arrow(painter, color, QPointF(6, 4.5), QPointF(6, 19.5));
        line(painter, color, QPointF(10, 7), QPointF(20, 7));
        line(painter, color, QPointF(10, 12), QPointF(20, 12));
        line(painter, color, QPointF(10, 17), QPointF(20, 17));
        break;
      case Glyph::PARAGRAPH_SPACING:
        line(painter, color, QPointF(4, 12), QPointF(10, 12));
        arrow_head(painter, color, QPointF(11, 12), 1, 0);
        line(painter, color, QPointF(13, 5), QPointF(20, 5));
        line(painter, color, QPointF(13, 9), QPointF(20, 9));
        line(painter, color, QPointF(13, 15), QPointF(20, 15));
        line(painter, color, QPointF(13, 19), QPointF(20, 19));
        break;
      case Glyph::TEXT_LEFT:
        paint_lines(painter, color, {{4, 20}, {4, 15}, {4, 20}, {4, 13}});
        break;
      case Glyph::TEXT_CENTER:
        paint_lines(painter, color, {{4, 20}, {7, 17}, {4, 20}, {8, 16}});
        break;
      case Glyph::TEXT_RIGHT:
        paint_lines(painter, color, {{4, 20}, {9, 20}, {4, 20}, {11, 20}});
        break;
      case Glyph::AUTO_WIDTH:
        double_arrow(painter, color, QPointF(4, 12), QPointF(20, 12));
        line(painter, color, QPointF(4, 8), QPointF(4, 16), 1.2);
        line(painter, color, QPointF(20, 8), QPointF(20, 16), 1.2);
        break;
      case Glyph::AUTO_HEIGHT:
        line(painter, color, QPointF(4, 4.5), QPointF(20, 4.5));
        line(painter, color, QPointF(4, 8.5), QPointF(14, 8.5));
        line(painter, color, QPointF(4, 12.5), QPointF(14, 12.5));
        double_arrow(painter, color, QPointF(18, 8), QPointF(18, 20));
        break;
      case Glyph::FIXED_SIZE:
        stroke_rect(painter, color, QRectF(4.75, 4.75, 14.5, 14.5));
        line(painter, color, QPointF(8, 9), QPointF(16, 9));
        line(painter, color, QPointF(8, 12.5), QPointF(16, 12.5));
        line(painter, color, QPointF(8, 16), QPointF(13, 16));
        break;
      case Glyph::UPPERCASE:
        text(painter, color, QRectF(0, 0, 24, 24), "TT", 13);
        break;
      case Glyph::LOWERCASE:
        text(painter, color, QRectF(0, 0, 24, 24), "tt", 13);
        break;
      case Glyph::TITLE_CASE:
        text(painter, color, QRectF(0, 0, 24, 24), "Tt", 13);
        break;
      case Glyph::SUPERSCRIPT:
        text(painter, color, QRectF(0, 3, 18, 20), "T", 14);
        text(painter, color, QRectF(14, 1, 8, 10), "1", 7, false);
        break;
      case Glyph::SUBSCRIPT:
        text(painter, color, QRectF(0, 0, 18, 20), "T", 14);
        text(painter, color, QRectF(14, 12, 8, 10), "1", 7, false);
        break;
      case Glyph::UNDERLINE:
        text(painter, color, QRectF(0, 0, 24, 20), "T", 14);
        line(painter, color, QPointF(6.5, 20), QPointF(17.5, 20), 1.2);
        break;
      case Glyph::STRIKETHROUGH:
        text(painter, color, QRectF(0, 1, 24, 22), "T", 14);
        line(painter, color, QPointF(5.5, 12.5), QPointF(18.5, 12.5), 1.2);
        break;
      case Glyph::HYPERLINK:
        polyline(painter, color, {QPointF(11, 6.75), QPointF(4.75, 6.75),
          QPointF(4.75, 19.25), QPointF(17.25, 19.25), QPointF(17.25, 13)},
          1.3);
        line(painter, color, QPointF(10, 14), QPointF(19, 5), 1.3);
        polyline(painter, color,
          {QPointF(14, 4.75), QPointF(19.25, 4.75), QPointF(19.25, 10)}, 1.3);
        break;
      case Glyph::MARGINS_LINKED:
        fill_rect(painter, color, QRectF(8, 4.5, 8, 15));
        break;
      case Glyph::MARGINS_SEPARATE:
        stroke_rect(painter, color, QRectF(4.75, 4.75, 14.5, 14.5));
        stroke_rect(painter, color, QRectF(8.75, 8.75, 6.5, 6.5));
        break;
      case Glyph::LAYER_ARTBOARD:
        painter.setPen(make_pen(color, 1.5, Qt::FlatCap, Qt::MiterJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(make_polygon({QPointF(6.75, 6.75),
          QPointF(15, 6.75), QPointF(18.75, 10.5), QPointF(18.75, 18.75),
          QPointF(6.75, 18.75)}));
        fill_polygon(painter, color,
          {QPointF(15, 6.75), QPointF(18.75, 10.5), QPointF(15, 10.5)});
        break;
      case Glyph::LAYER_RECTANGLE:
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(6, 4.5, 15, 15), 1, 1);
        break;
      case Glyph::LAYER_ELLIPSE:
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(12, 12), 6.5, 6.5);
        break;
      case Glyph::LAYER_POLYGON:
        fill_polygon(painter, color, {QPointF(8.5, 6), QPointF(15.5, 6),
          QPointF(19, 12), QPointF(15.5, 18), QPointF(8.5, 18),
          QPointF(5, 12)});
        break;
      case Glyph::LAYER_LINE:
        line(painter, color, QPointF(5.5, 18.5), QPointF(18.5, 5.5), 1.2);
        break;
      case Glyph::LAYER_PATH:
      {
        painter.save();
        painter.translate(12, 12);
        painter.rotate(45);
        painter.scale(0.75, 0.75);
        painter.translate(-12, -12);
        auto nib = QPainterPath(QPointF(12, 21));
        nib.lineTo(6.5, 11.5);
        nib.lineTo(9, 5.5);
        nib.lineTo(15, 5.5);
        nib.lineTo(17.5, 11.5);
        nib.closeSubpath();
        stroke(painter, color, nib, 1.6);
        painter.restore();
        break;
      }
      case Glyph::LAYER_BOOLEAN:
      {
        auto back = QPainterPath();
        back.addRect(QRectF(5, 5, 9, 9));
        auto front = QPainterPath();
        front.addRect(QRectF(10, 10, 9, 9));
        fill(painter, color.darker(130), back.subtracted(front));
        painter.setPen(make_pen(color.darker(130), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(10.6, 10.6, 7.8, 7.8));
        break;
      }
      case Glyph::LAYER_TEXT:
        text(painter, color.darker(120), QRectF(0, 0, 24, 24), "T", 15);
        break;
      case Glyph::LAYER_GROUP:
      {
        auto folder = QPainterPath(QPointF(4.5, 7));
        folder.lineTo(9.5, 7);
        folder.lineTo(11, 8.75);
        folder.lineTo(19.5, 8.75);
        folder.lineTo(19.5, 18);
        folder.lineTo(4.5, 18);
        folder.closeSubpath();
        fill(painter, color, folder);
        break;
      }
      case Glyph::LAYER_OPEN_GROUP:
      {
        auto folder = QPainterPath(QPointF(5, 7.5));
        folder.lineTo(9.5, 7.5);
        folder.lineTo(11, 9.25);
        folder.lineTo(19, 9.25);
        folder.lineTo(19, 17.5);
        folder.lineTo(5, 17.5);
        folder.closeSubpath();
        stroke(painter, color, folder, 1.2);
        break;
      }
      case Glyph::LAYER_PASTEBOARD:
        stroke_rect(painter, color, QRectF(5.25, 5.75, 13.5, 12.5));
        fill_rect(painter, color, QRectF(7.5, 8, 4.5, 8));
        fill_rect(painter, color, QRectF(13, 8, 3.5, 3.5));
        fill_rect(painter, color, QRectF(13, 12.5, 3.5, 3.5));
        break;
      case Glyph::LAYER_COMPONENT:
      case Glyph::LAYER_REFERENCE:
      {
        auto diamond = QPainterPath(QPointF(12, 4.5));
        diamond.lineTo(19.5, 12);
        diamond.lineTo(12, 19.5);
        diamond.lineTo(4.5, 12);
        diamond.closeSubpath();
        if(glyph == Glyph::LAYER_COMPONENT) {
          fill(painter, color, diamond);
        } else {
          stroke(painter, color, diamond, 1.4);
        }
        break;
      }
      case Glyph::EYE:
      case Glyph::EYE_CLOSED:
      {
        auto eye = QPainterPath(QPointF(3, 12));
        eye.quadTo(12, 3, 21, 12);
        eye.quadTo(12, 21, 3, 12);
        stroke(painter, color, eye, 1.4);
        if(glyph == Glyph::EYE) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawEllipse(QPointF(12, 12), 2.6, 2.6);
        } else {
          line(painter, color, QPointF(5, 19), QPointF(19, 5), 1.4);
        }
        break;
      }
      case Glyph::LOCK:
      {
        fill_rect(painter, color, QRectF(6.5, 11, 11, 9));
        auto shackle = QPainterPath(QPointF(8.75, 11));
        shackle.lineTo(8.75, 8);
        shackle.arcTo(QRectF(8.75, 4.5, 6.5, 7), 180, -180);
        shackle.lineTo(15.25, 11);
        stroke(painter, color, shackle, 1.6);
        break;
      }
      case Glyph::TRIGGER_TAP:
      case Glyph::TRIGGER_DRAG:
      {
        auto hand = QPainterPath(QPointF(9.5, 13));
        hand.lineTo(9.5, 6);
        hand.arcTo(QRectF(9.5, 4.5, 3, 3), 180, -180);
        hand.lineTo(12.5, 11);
        hand.lineTo(17, 12);
        hand.quadTo(18.5, 12.5, 18, 14.5);
        hand.lineTo(17, 19.5);
        hand.lineTo(11, 19.5);
        hand.lineTo(7, 15);
        hand.quadTo(6.5, 13.5, 8, 13.5);
        hand.closeSubpath();
        stroke(painter, color, hand, 1.2);
        if(glyph == Glyph::TRIGGER_DRAG) {
          polyline(painter, color,
            {QPointF(3, 6), QPointF(6.5, 6)});
          polyline(painter, color,
            {QPointF(4.5, 4.5), QPointF(3, 6), QPointF(4.5, 7.5)});
          polyline(painter, color,
            {QPointF(15.5, 6), QPointF(20, 6)});
          polyline(painter, color,
            {QPointF(18.5, 4.5), QPointF(20, 6), QPointF(18.5, 7.5)});
        }
        break;
      }
      case Glyph::TRIGGER_HOVER:
        fill_polygon(painter, color, {QPointF(8, 4), QPointF(8, 18),
          QPointF(11.5, 14.5), QPointF(14, 20), QPointF(16, 19),
          QPointF(13.5, 13.5), QPointF(18, 13.5)});
        break;
      case Glyph::TRIGGER_TIME:
      {
        auto face = QPainterPath();
        face.addEllipse(QPointF(12, 12), 7.5, 7.5);
        stroke(painter, color, face, 1.4);
        polyline(painter, color,
          {QPointF(12, 7.5), QPointF(12, 12), QPointF(15, 14)});
        break;
      }
      case Glyph::TRIGGER_KEYS:
      {
        stroke_rect(painter, color, QRectF(3.5, 7, 17, 10.5));
        for(auto row : {9.5, 12.25}) {
          for(auto column = 0; column != 5; ++column) {
            fill_rect(painter, color,
              QRectF(5.5 + 2.8 * column, row, 1.6, 1.4));
          }
        }
        fill_rect(painter, color, QRectF(8, 15, 8, 1.4));
        break;
      }
      case Glyph::EXPORT_MARK:
      {
        polyline(painter, color, {QPointF(11.5, 6), QPointF(5.5, 6),
          QPointF(5.5, 18.5), QPointF(18, 18.5), QPointF(18, 12.5)}, 1.3);
        line(painter, color, QPointF(11, 13), QPointF(18.5, 5.5), 1.3);
        polyline(painter, color, {QPointF(13.5, 5.5), QPointF(18.5, 5.5),
          QPointF(18.5, 10.5)}, 1.3);
        break;
      }
    }
  }

  class GlyphEngine : public QIconEngine {
    public:
      GlyphEngine(Glyph glyph, std::optional<QColor> color) noexcept
        : m_glyph(glyph),
          m_color(std::move(color)) {}

      void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
          QIcon::State state) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / GRID, rect.height() / GRID);
        paint_glyph(*painter, m_glyph, get_color(mode, state));
        painter->restore();
      }

      QPixmap pixmap(
          const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
      }

      QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode,
          QIcon::State state, qreal scale) override {
        auto pixmap = QPixmap(size * scale);
        pixmap.fill(Qt::transparent);
        {
          auto painter = QPainter(&pixmap);
          paint(&painter, QRect(QPoint(0, 0), size * scale), mode, state);
        }
        pixmap.setDevicePixelRatio(scale);
        return pixmap;
      }

      QIconEngine* clone() const override {
        return new GlyphEngine(m_glyph, m_color);
      }

      QString key() const override {
        return "rellu.glyph";
      }

    private:
      Glyph m_glyph;
      std::optional<QColor> m_color;

      QColor get_color(QIcon::Mode mode, QIcon::State state) const {
        if(m_color) {
          if(mode == QIcon::Disabled) {
            auto color = *m_color;
            color.setAlphaF(0.4f);
            return color;
          }
          return *m_color;
        }
        if(mode == QIcon::Disabled) {
          return Colors::DISABLED_TEXT;
        } else if(state == QIcon::On) {
          return Colors::ACTIVE;
        }
        return Colors::ICON;
      }
  };

  Glyph get_layer_glyph(const Node& node, bool is_reference) {
    if(is_reference) {
      return Glyph::LAYER_REFERENCE;
    }
    if(auto shape = node.get_shape()) {
      return std::visit([] <typename T> (const T&) {
        if constexpr(std::same_as<T, RectangleShape>) {
          return Glyph::LAYER_RECTANGLE;
        } else if constexpr(std::same_as<T, EllipseShape>) {
          return Glyph::LAYER_ELLIPSE;
        } else if constexpr(std::same_as<T, LineShape>) {
          return Glyph::LAYER_LINE;
        } else if constexpr(std::same_as<T, PolygonShape>) {
          return Glyph::LAYER_POLYGON;
        } else if constexpr(std::same_as<T, PathShape>) {
          return Glyph::LAYER_PATH;
        } else {
          return Glyph::LAYER_BOOLEAN;
        }
      }, shape->m_geometry);
    } else if(auto group = node.get_group()) {
      if(group->m_component_instance) {
        return Glyph::LAYER_COMPONENT;
      }
      return Glyph::LAYER_GROUP;
    } else if(node.get_text()) {
      return Glyph::LAYER_TEXT;
    } else if(node.get_artboard()) {
      return Glyph::LAYER_ARTBOARD;
    }
    return Glyph::LAYER_REFERENCE;
  }

  QColor get_layer_color(Glyph glyph) {
    if(glyph == Glyph::LAYER_ARTBOARD) {
      return ARTBOARD_INK;
    } else if(glyph == Glyph::LAYER_RECTANGLE) {
      return RECTANGLE_INK;
    } else if(glyph == Glyph::LAYER_LINE ||
        glyph == Glyph::LAYER_PATH || glyph == Glyph::LAYER_TEXT ||
        glyph == Glyph::LAYER_REFERENCE) {
      return LAYER_LINE_INK;
    }
    return LAYER_INK;
  }
}

const QIcon& Rellu::get_icon(Glyph glyph) {
  static auto icons = std::map<Glyph, QIcon>();
  auto i = icons.find(glyph);
  if(i == icons.end()) {
    i = icons.emplace(
      glyph, QIcon(new GlyphEngine(glyph, std::nullopt))).first;
  }
  return i->second;
}

QPixmap Rellu::make_fitted_glyph(Glyph glyph, QSize size, double ratio) {
  constexpr auto RESOLUTION = 128;
  auto image = get_icon(glyph).pixmap(QSize(RESOLUTION, RESOLUTION), 1).
    toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
  auto bounds = QRect();
  for(auto y = 0; y != image.height(); ++y) {
    for(auto x = 0; x != image.width(); ++x) {
      if(qAlpha(image.pixel(x, y)) != 0) {
        bounds |= QRect(x, y, 1, 1);
      }
    }
  }
  if(bounds.isEmpty() || size.isEmpty()) {
    return QPixmap();
  }
  auto fitted = QPixmap::fromImage(image.copy(bounds).scaled(
    size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
  fitted.setDevicePixelRatio(ratio);
  return fitted;
}

QIcon Rellu::make_icon(Glyph glyph, const QColor& color) {
  return QIcon(new GlyphEngine(glyph, color));
}

const QIcon& Rellu::get_layer_icon(const Node& node, bool is_reference) {
  static auto icons = std::map<Glyph, QIcon>();
  auto glyph = get_layer_glyph(node, is_reference);
  auto i = icons.find(glyph);
  if(i == icons.end()) {
    i = icons.emplace(glyph, make_icon(glyph, get_layer_color(glyph))).first;
  }
  return i->second;
}

const QIcon& Rellu::get_open_group_icon() {
  static auto icon = make_icon(
    Glyph::LAYER_OPEN_GROUP, get_layer_color(Glyph::LAYER_OPEN_GROUP));
  return icon;
}

const QIcon& Rellu::get_pasteboard_icon() {
  static auto icon = make_icon(
    Glyph::LAYER_PASTEBOARD, get_layer_color(Glyph::LAYER_PASTEBOARD));
  return icon;
}

const QIcon& Rellu::get_kind_icon(NodeKind kind, bool is_reference) {
  static auto icons = [] {
    auto glyphs = std::array<Glyph, 5>({Glyph::LAYER_ARTBOARD,
      Glyph::LAYER_GROUP, Glyph::LAYER_RECTANGLE, Glyph::LAYER_TEXT,
      Glyph::LAYER_REFERENCE});
    auto icons = std::array<QIcon, 5>();
    for(auto i = std::size_t(0); i != glyphs.size(); ++i) {
      icons[i] = make_icon(glyphs[i], get_layer_color(glyphs[i]));
    }
    return icons;
  }();
  if(is_reference) {
    return icons[static_cast<int>(NodeKind::REFERENCE)];
  }
  return icons[static_cast<int>(kind)];
}

const QIcon& Rellu::get_visibility_icon(bool is_visible) {
  static auto visible =
    make_icon(Glyph::EYE_CLOSED, QColor(0xB8, 0xBC, 0xC0));
  static auto hidden = make_icon(Glyph::EYE_CLOSED, Colors::SECONDARY_TEXT);
  if(is_visible) {
    return visible;
  }
  return hidden;
}

const QIcon& Rellu::get_export_mark_icon(bool is_marked) {
  static auto marked =
    make_icon(Glyph::EXPORT_MARK, QColor(0x65, 0x65, 0x65));
  static auto unmarked =
    make_icon(Glyph::EXPORT_MARK, QColor(0xB7, 0xBB, 0xBF));
  if(is_marked) {
    return marked;
  }
  return unmarked;
}

const QIcon& Rellu::get_lock_icon() {
  static auto icon = make_icon(Glyph::LOCK, Colors::ICON);
  return icon;
}
