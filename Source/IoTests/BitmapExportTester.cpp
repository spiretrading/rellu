#include <filesystem>
#include <stdexcept>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtPlugin>
#include <doctest/doctest.h>
#include "Rellu/Io/BitmapExport.hpp"

using namespace Rellu;

Q_IMPORT_PLUGIN(QJpegPlugin)

namespace {
  std::filesystem::path to_path(const QTemporaryDir& directory,
      const QString& name) {
    return std::filesystem::path(directory.filePath(name).toStdU16String());
  }
}

TEST_SUITE("BitmapExport") {
  TEST_CASE("png_export") {
    auto directory = QTemporaryDir();
    auto image = QImage(3, 2, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    image.setPixelColor(0, 0, QColor(255, 0, 0));
    image.setPixelColor(1, 0, QColor(0, 0, 255, 128));
    write_png_export(image, to_path(directory, "a.png"));
    auto written = QImage(directory.filePath("a.png"));
    REQUIRE(written.size() == QSize(3, 2));
    REQUIRE(written.hasAlphaChannel());
    REQUIRE(written.pixelColor(0, 0) == QColor(255, 0, 0));
    REQUIRE(written.pixelColor(1, 0).alpha() == 128);
    REQUIRE(written.pixelColor(1, 0).blue() == 255);
    REQUIRE(written.pixelColor(2, 1).alpha() == 0);
    auto file = QFile(directory.filePath("a.png"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    auto bytes = file.readAll();
    REQUIRE(!bytes.contains("pHYs"));
    REQUIRE(bytes.contains("sBIT"));
  }

  TEST_CASE("jpg_export") {
    auto directory = QTemporaryDir();
    auto image = QImage(64, 64, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    write_jpg_export(image, to_path(directory, "white.jpg"), 100);
    auto white = QImage(directory.filePath("white.jpg"));
    REQUIRE(white.size() == QSize(64, 64));
    REQUIRE(!white.hasAlphaChannel());
    REQUIRE(white.pixelColor(10, 10).red() >= 254);
    REQUIRE(white.pixelColor(10, 10).blue() >= 254);
    auto generator = QRandomGenerator(7);
    for(auto y = 0; y != image.height(); ++y) {
      for(auto x = 0; x != image.width(); ++x) {
        image.setPixel(x, y, generator.generate() | 0xFF000000);
      }
    }
    write_jpg_export(image, to_path(directory, "low.jpg"), 20);
    write_jpg_export(image, to_path(directory, "high.jpg"), 100);
    REQUIRE(QFileInfo(directory.filePath("low.jpg")).size() <
      QFileInfo(directory.filePath("high.jpg")).size());
  }

  TEST_CASE("export_failure") {
    auto directory = QTemporaryDir();
    auto image = QImage(2, 2, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::black);
    auto path = to_path(directory, "missing/a.png");
    REQUIRE_THROWS_AS(write_png_export(image, path), std::runtime_error);
    REQUIRE_THROWS_AS(write_jpg_export(QImage(), path, 100),
      std::runtime_error);
  }
}
