#include <algorithm>
#include <doctest/doctest.h>
#include <QChar>
#include <QColor>
#include <QFile>
#include <QGlyphRun>
#include <QImage>
#include <QPainter>
#include <QRawFont>
#include <private/qfont_p.h>
#include <private/qfontengine_p.h>
#include <private/qpaintengine_raster_p.h>
#include <private/qrawfont_p.h>
#include <private/qwindowsdirectwritefontdatabase_p.h>
#include "Rellu/Ui/GreyscaleText.hpp"

using namespace Rellu;

namespace {
  int count_colored(const QImage& image) {
    auto count = 0;
    for(auto y = 0; y != image.height(); ++y) {
      for(auto x = 0; x != image.width(); ++x) {
        auto color = image.pixelColor(x, y);
        auto high = std::max({color.red(), color.green(), color.blue()});
        auto low = std::min({color.red(), color.green(), color.blue()});
        if(high - low > 20) {
          ++count;
        }
      }
    }
    return count;
  }

  int count_dark(const QImage& image) {
    auto count = 0;
    for(auto y = 0; y != image.height(); ++y) {
      for(auto x = 0; x != image.width(); ++x) {
        if(image.pixelColor(x, y).value() < 128) {
          ++count;
        }
      }
    }
    return count;
  }

  QImage draw(const QRawFont& font, const QString& text) {
    auto image = QImage(240, 40, QImage::Format_RGB32);
    image.fill(Qt::white);
    auto glyphs = font.glyphIndexesForString(text);
    auto advances = font.advancesForGlyphIndexes(glyphs);
    auto positions = QList<QPointF>();
    auto x = 0.0;
    for(auto& advance : advances) {
      positions.push_back(QPointF(x, 0));
      x += advance.x();
    }
    auto run = QGlyphRun();
    run.setRawFont(font);
    run.setGlyphIndexes(glyphs);
    run.setPositions(positions);
    auto painter = QPainter(&image);
    painter.setPen(Qt::black);
    painter.drawGlyphRun(QPointF(4, 26), run);
    return image;
  }
}

TEST_SUITE("GreyscaleText") {
  TEST_CASE("make_greyscale_fonts") {
    auto file =
      QFile(QString(RELLU_FONTS_DIRECTORY) + "/OpenSans-Regular.ttf");
    REQUIRE(file.open(QIODevice::ReadOnly));
    auto database = QWindowsDirectWriteFontDatabase();
    auto engine =
      database.fontEngine(file.readAll(), 18, QFont::PreferDefaultHinting);
    REQUIRE(engine);
    REQUIRE(engine->glyphFormat == QFontEngine::Format_None);
    auto font = QRawFont();
    QRawFontPrivate::get(font)->setFontEngine(engine);
    auto text = QString("All Items ARTBOARDS");
    if(QRasterPaintEngine::clearTypeFontsEnabled()) {
      REQUIRE(count_colored(draw(font, text)) > 0);
    }
    QFontCache::instance()->insertEngine(
      QFontCache::Key(engine->fontDef, QChar::Script_Common), engine);
    REQUIRE(make_greyscale_fonts());
    REQUIRE(engine->glyphFormat == QFontEngine::Format_A8);
    REQUIRE(!make_greyscale_fonts());
    auto image = draw(font, text);
    REQUIRE(count_dark(image) > 100);
    REQUIRE(count_colored(image) == 0);
  }
}
