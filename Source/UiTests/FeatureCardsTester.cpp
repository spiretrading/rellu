#include <cmath>
#include <doctest/doctest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEventLoop>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/FeatureCards.hpp"
#include "Rellu/Ui/ToolTips.hpp"
#include "Rellu/Ui/Toolbar.hpp"

using namespace Rellu;

namespace {
  void wait(int milliseconds) {
    auto loop = QEventLoop();
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
  }

  void enter(QWidget& widget) {
    auto event = QEnterEvent(QPointF(1, 1), QPointF(1, 1),
      QPointF(widget.mapToGlobal(QPoint(1, 1))));
    QCoreApplication::sendEvent(&widget, &event);
  }

  void leave(QWidget& widget) {
    auto event = QEvent(QEvent::Leave);
    QCoreApplication::sendEvent(&widget, &event);
  }

  QToolButton* find_button(QWidget& parent, const QString& name) {
    for(auto button : parent.findChildren<QToolButton*>()) {
      if(button->accessibleName() == name) {
        return button;
      }
    }
    return nullptr;
  }
}

TEST_SUITE("FeatureCards") {
  TEST_CASE("feature_card") {
    auto card = get_feature_card(Feature::REPEAT_GRID);
    REQUIRE(card.m_title.toStdString() == "Repeat Grid (Ctrl + R)");
    REQUIRE(card.m_description.toStdString() ==
      "Create repeated objects and customize with text and images.");
    REQUIRE(card.m_placement == CardPlacement::LEFT_TOP);
    REQUIRE(card.m_offset == 15);
    REQUIRE(!card.m_is_click_ignored);
    REQUIRE(get_feature_card(Feature::DESKTOP_PREVIEW).m_title.toStdString() ==
      "Desktop preview");
    REQUIRE(get_feature_card(Feature::LAYOUT).m_is_click_ignored);
  }

  TEST_CASE("card_position") {
    auto anchor = QRectF(100, 100, 40, 20);
    auto size = QSizeF(280, 300);
    auto position = [&] (Feature feature) {
      return get_card_position(get_feature_card(feature), anchor, size);
    };
    REQUIRE(position(Feature::DESKTOP_PREVIEW) == QPointF(-140, 120));
    auto bottom_left =
      FeatureCard("", "", CardPlacement::BOTTOM_LEFT, 13, false);
    REQUIRE(get_card_position(bottom_left, anchor, size) == QPointF(100, 133));
    REQUIRE(position(Feature::LAYERS_PANEL) == QPointF(148, -180));
    REQUIRE(position(Feature::REPEAT_GRID) == QPointF(-195, 100));
    REQUIRE(position(Feature::LAYOUT) == QPointF(-140, -204));
  }

  TEST_CASE("xd_card_placement") {
    auto place = [] (Feature feature, double left, double top, double right,
        double bottom, double height) {
      auto widget = QRectF(left / 1.5, top / 1.5, (right - left) / 1.5,
        (bottom - top) / 1.5);
      return get_card_position(get_feature_card(feature),
        get_card_anchor(feature, widget), QSizeF(280, height / 1.5)) * 1.5;
    };
    auto require_near = [] (QPointF actual, QPointF expected) {
      REQUIRE(std::abs(actual.x() - expected.x()) < 0.01);
      REQUIRE(std::abs(actual.y() - expected.y()) < 0.01);
    };
    require_near(place(Feature::DESKTOP_PREVIEW, 3648, 50, 3708, 109, 512),
      QPointF(3288, 109));
    require_near(place(Feature::REPEAT_GRID, 3429, 210, 3586, 258, 538),
      QPointF(2985, 209));
    require_near(place(Feature::LAYERS_PANEL, 6, 1962, 66, 2022, 538),
      QPointF(78, 1484));
    require_near(place(Feature::TRANSFORM, 3540, 356, 3558, 374, 538),
      QPointF(3144, 383));
    require_near(place(Feature::LAYOUT, 3507, 661, 3525, 679, 538),
      QPointF(3111, 111));
  }

  TEST_CASE("anchors") {
    auto bar = AppBar();
    REQUIRE(get_feature(*find_button(bar, "Desktop Preview")) ==
      Feature::DESKTOP_PREVIEW);
    REQUIRE(get_feature(*find_button(bar, "Device Preview")) ==
      Feature::DEVICE_PREVIEW);
    REQUIRE(!get_feature(*find_button(bar, "Invite to Document")));
    REQUIRE(!get_feature(*find_button(bar, "Version History")));
    auto toolbar = Toolbar(make_xd_tools(), make_xd_panels());
    REQUIRE(get_feature(*toolbar.find_button("layers")) ==
      Feature::LAYERS_PANEL);
    REQUIRE(get_feature(*toolbar.find_button("assets")) ==
      Feature::ASSETS_PANEL);
    REQUIRE(get_feature(*toolbar.find_button("plugins")) ==
      Feature::PLUGINS_PANEL);
    REQUIRE(!get_feature(*toolbar.find_button("rectangle")));
  }

  TEST_CASE("feature_card_filter") {
    auto tips = ToolTipFilter();
    auto filter = FeatureCardFilter();
    QCoreApplication::instance()->installEventFilter(&tips);
    QCoreApplication::instance()->installEventFilter(&filter);
    auto window = QWidget();
    window.resize(600, 500);
    auto anchor = new QWidget(&window);
    anchor->setGeometry(400, 20, 40, 40);
    anchor->setToolTip("Desktop Preview");
    set_feature(*anchor, Feature::DESKTOP_PREVIEW);
    window.show();
    QCoreApplication::processEvents();
    auto help = QHelpEvent(
      QEvent::ToolTip, QPoint(5, 5), anchor->mapToGlobal(QPoint(5, 5)));
    QCoreApplication::sendEvent(anchor, &help);
    for(auto widget : QApplication::topLevelWidgets()) {
      REQUIRE(!(widget->objectName() == "rellu_tool_tip" &&
        widget->isVisible()));
    }
    enter(*anchor);
    wait(FeatureCardFilter::SHOW_DELAY / 2);
    REQUIRE(!filter.get_card());
    leave(*anchor);
    wait(FeatureCardFilter::SHOW_DELAY);
    REQUIRE(!filter.get_card());
    enter(*anchor);
    wait(FeatureCardFilter::SHOW_DELAY + 100);
    auto card = filter.get_card();
    REQUIRE(card);
    REQUIRE(card->width() > 280);
    leave(*anchor);
    enter(*card);
    wait(FeatureCardFilter::CLOSE_DELAY + 100);
    REQUIRE(filter.get_card());
    leave(*card);
    wait(FeatureCardFilter::CLOSE_DELAY + 100);
    REQUIRE(!filter.get_card());
    enter(*anchor);
    wait(FeatureCardFilter::SHOW_DELAY + 100);
    REQUIRE(filter.get_card());
    auto key = QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &key);
    REQUIRE(!filter.get_card());
    QCoreApplication::instance()->removeEventFilter(&filter);
    QCoreApplication::instance()->removeEventFilter(&tips);
  }
}
