#include <chrono>
#include <vector>
#include <doctest/doctest.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>
#include "Rellu/Ui/Banner.hpp"

using namespace Rellu;

namespace {
  void wait(std::chrono::milliseconds duration) {
    auto loop = QEventLoop();
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
  }

  void click(QWidget& widget, QPointF position) {
    auto release = QMouseEvent(QEvent::MouseButtonRelease, position,
      widget.mapToGlobal(position), Qt::LeftButton, Qt::NoButton,
      Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &release);
  }
}

TEST_SUITE("Banner") {
  TEST_CASE("parse_banner_message") {
    REQUIRE(parse_banner_message("").empty());
    REQUIRE(parse_banner_message("Plain [text") ==
      std::vector{BannerSpan("Plain [text", false)});
    REQUIRE(parse_banner_message(
      "Publish a library. [Learn more|https://example.com/a] ") ==
      std::vector{BannerSpan("Publish a library. ", false),
        BannerSpan("Learn more", true), BannerSpan(" ", false)});
    REQUIRE(parse_banner_message("[A|x][B|y] [c] d") == std::vector{
      BannerSpan("A", true), BannerSpan("B", true),
      BannerSpan(" [c] d", false)});
  }

  TEST_CASE("banner_layout") {
    auto host = QWidget();
    host.resize(2560, 1440);
    auto banner = Banner("Publish. [Learn more|https://example.com] ", &host);
    banner.setGeometry(280, 1392, 2000, Banner::HEIGHT);
    auto close = banner.get_close_region();
    REQUIRE(close.center().x() == doctest::Approx(2000 - 25 - 6));
    REQUIRE(close.center().y() == doctest::Approx(24));
    REQUIRE(close.width() == doctest::Approx(28));
    auto links = banner.get_link_regions();
    REQUIRE(links.size() == 1);
    auto origin = banner.get_text_origin();
    REQUIRE(origin.x() < links.front().left());
    REQUIRE(origin.x() < 1000 - 23);
    REQUIRE(links.front().right() > 1000 - 23);
    REQUIRE(origin.y() > 24);
    click(banner, links.front().center());
    REQUIRE(banner.is_dismissed());
    auto closing = Banner("Message", &host);
    closing.resize(974, Banner::HEIGHT);
    click(closing, QPointF(10, 10));
    REQUIRE(!closing.is_dismissed());
    click(closing, closing.get_close_region().center());
    REQUIRE(closing.is_dismissed());
  }

  TEST_CASE("banner_host") {
    auto host = QWidget();
    host.resize(1090, 700);
    auto banners = BannerHost(host, std::chrono::milliseconds(20),
      std::chrono::milliseconds(200));
    banners.show("First");
    REQUIRE(!banners.get_banner());
    wait(std::chrono::milliseconds(60));
    auto banner = banners.get_banner();
    REQUIRE(banner);
    REQUIRE(banner->get_message() == "First");
    REQUIRE(banner->geometry() == QRect(58, 652, 974, Banner::HEIGHT));
    host.resize(3000, 700);
    banners.place();
    REQUIRE(banner->geometry() == QRect(500, 652, 2000, Banner::HEIGHT));
    wait(std::chrono::milliseconds(120));
    banners.show("Second");
    wait(std::chrono::milliseconds(60));
    REQUIRE(banners.get_banner() == banner);
    REQUIRE(banner->get_message() == "Second");
    wait(std::chrono::milliseconds(120));
    REQUIRE(banners.get_banner() == banner);
    wait(std::chrono::milliseconds(200));
    REQUIRE(!banners.get_banner());
  }
}
