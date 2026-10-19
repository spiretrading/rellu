#include <vector>
#include <doctest/doctest.h>
#include <QComboBox>
#include <QLineEdit>
#include <QToolButton>
#include "Rellu/Ui/ColorPicker.hpp"

using namespace Rellu;

namespace {
  QLineEdit& get_field(ColorPicker& picker, const QString& name) {
    auto found = static_cast<QLineEdit*>(nullptr);
    for(auto field : picker.findChildren<QLineEdit*>()) {
      if(field->accessibleName() == name) {
        found = field;
      }
    }
    REQUIRE(found);
    return *found;
  }

  void enter(ColorPicker& picker, const QString& name, const QString& text) {
    auto& field = get_field(picker, name);
    field.setText(text);
    Q_EMIT field.editingFinished();
  }
}

TEST_SUITE("ColorPicker") {
  TEST_CASE("solid") {
    auto picker = ColorPicker();
    auto fills = std::vector<Fill>();
    picker.set_change_handler([&] (const auto& fill) {
      fills.push_back(fill);
    });
    picker.set_fill(SolidFill(make_color(52, 120, 246)));
    REQUIRE(fills.empty());
    REQUIRE(get_field(picker, "Hex").text() == "#3478F6");
    REQUIRE(get_field(picker, "Opacity").text() == "100%");
    enter(picker, "Hex", "#FF0000");
    REQUIRE(fills.size() == 1);
    REQUIRE(fills.back() == Fill(SolidFill(make_color(255, 0, 0))));
    enter(picker, "Opacity", "50%");
    REQUIRE(fills.back() == Fill(SolidFill(make_color(255, 0, 0, 0.5))));
    picker.set_model(ColorModel::RGB);
    REQUIRE(get_field(picker, "Red").text() == "255");
    enter(picker, "Green", "128");
    REQUIRE(fills.back() == Fill(SolidFill(make_color(255, 128, 0, 0.5))));
    picker.set_model(ColorModel::HSB);
    REQUIRE(get_field(picker, "Red").text() == "30");
    REQUIRE(get_field(picker, "Green").text() == "100");
    REQUIRE(get_field(picker, "Blue").text() == "100");
  }

  TEST_CASE("gradient") {
    auto picker = ColorPicker();
    auto fill = Fill();
    picker.set_change_handler([&] (const auto& changed) {
      fill = changed;
    });
    picker.set_fill(SolidFill(make_color(255, 0, 0)));
    picker.apply_type(1);
    auto linear = std::get<GradientFill>(fill);
    REQUIRE(std::holds_alternative<LinearGradient>(linear.m_geometry));
    REQUIRE(linear.m_stops.size() == 2);
    REQUIRE(std::get<LinearGradient>(linear.m_geometry) ==
      LinearGradient(Point(0.5, 0), Point(0.5, 1)));
    REQUIRE(linear.m_stops[0].m_color == make_color(255, 0, 0));
    REQUIRE(linear.m_stops[1].m_color == make_color(128, 0, 0));
    picker.set_stop(1);
    picker.apply_color(make_color(0, 0, 255));
    REQUIRE(std::get<GradientFill>(fill).m_stops[1].m_color ==
      make_color(0, 0, 255));
    picker.apply_type(2);
    auto radial = std::get<GradientFill>(fill);
    REQUIRE(std::holds_alternative<RadialGradient>(radial.m_geometry));
    REQUIRE(radial.m_stops[1].m_color == make_color(0, 0, 255));
    picker.apply_type(3);
    auto angular = std::get<GradientFill>(fill);
    REQUIRE(angular.m_geometry == GradientGeometry(AngularGradient()));
    REQUIRE(angular.m_stops == radial.m_stops);
    picker.apply_type(0);
    REQUIRE(fill == Fill(SolidFill(make_color(255, 0, 0))));
    picker.set_gradient_enabled(false);
    REQUIRE(!picker.is_gradient_enabled());
  }

  TEST_CASE("default_stops") {
    auto stops = make_default_stops(make_color(255, 255, 255));
    REQUIRE(stops.size() == 2);
    REQUIRE(stops[0] == GradientStop(0, make_color(255, 255, 255)));
    REQUIRE(stops[1] == GradientStop(1, make_color(0x80, 0x80, 0x80)));
    REQUIRE(make_default_stops(make_color(51, 100, 7, 0.5))[1].m_color ==
      make_color(26, 50, 4, 0.5));
  }

  TEST_CASE("image_fill") {
    auto picker = ColorPicker();
    auto fill = Fill();
    picker.set_change_handler([&] (const auto& changed) {
      fill = changed;
    });
    auto image = ImageFill();
    image.m_image = generate_node_id();
    picker.set_fill(image);
    REQUIRE(picker.get_color() == make_color(255, 255, 255));
    REQUIRE(get_field(picker, "Hex").text() == "#FFFFFF");
    auto type = static_cast<QComboBox*>(nullptr);
    for(auto box : picker.findChildren<QComboBox*>()) {
      if(box->accessibleName() == "Fill Type") {
        type = box;
      }
    }
    REQUIRE(type);
    REQUIRE(type->count() == 4);
    REQUIRE(type->currentText() == "Solid Color");
    enter(picker, "Hex", "#00FF00");
    REQUIRE(fill == Fill(SolidFill(make_color(0, 255, 0))));
  }

  TEST_CASE("swatches") {
    auto picker = ColorPicker();
    auto fill = Fill();
    auto added = Fill();
    picker.set_change_handler([&] (const auto& changed) {
      fill = changed;
    });
    picker.set_add_handler([&] (const auto& value) {
      added = value;
    });
    auto swatch = ColorSwatch(generate_node_id(), "Brand", make_color(9, 9, 9));
    picker.set_swatches({swatch});
    picker.set_fill(SolidFill(make_color(1, 2, 3)));
    auto add = static_cast<QToolButton*>(nullptr);
    auto brand = static_cast<QToolButton*>(nullptr);
    for(auto button : picker.findChildren<QToolButton*>()) {
      if(button->accessibleName() == "Add to Assets") {
        add = button;
      } else if(button->accessibleName() == "Brand") {
        brand = button;
      }
    }
    REQUIRE(add);
    REQUIRE(brand);
    REQUIRE(add->isEnabled());
    add->click();
    REQUIRE(added == Fill(SolidFill(make_color(1, 2, 3))));
    brand->click();
    REQUIRE(fill == Fill(SolidFill(make_color(9, 9, 9), swatch.m_id)));
  }
}
