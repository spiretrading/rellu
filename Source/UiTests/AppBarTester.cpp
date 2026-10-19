#include <array>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Ui/AppBar.hpp"

using namespace Rellu;

TEST_SUITE("AppBar") {
  TEST_CASE("zoom_text") {
    REQUIRE(format_zoom(0.306) == "30.6%");
    REQUIRE(format_zoom(1.689) == "168.9%");
    REQUIRE(format_zoom(1.93) == "193%");
    REQUIRE(format_zoom(1) == "100%");
    REQUIRE(format_zoom(0.25) == "25%");
    REQUIRE(parse_zoom("150%") == 1.5);
    REQUIRE(*parse_zoom(" 30.6 ") == doctest::Approx(0.306));
    REQUIRE(!parse_zoom("wide"));
    REQUIRE(!parse_zoom("0%"));
    REQUIRE(!parse_zoom("-50"));
  }

  TEST_CASE("zoom_field") {
    auto bar = AppBar();
    auto zooms = std::vector<double>();
    bar.set_zoom_handler([&] (auto zoom) {
      zooms.push_back(zoom);
    });
    bar.set_zoom(0.306);
    REQUIRE(bar.get_zoom_field().text() == "30.6%");
    REQUIRE(zooms.empty());
    bar.get_zoom_field().setText("200");
    Q_EMIT bar.get_zoom_field().editingFinished();
    REQUIRE(zooms == std::vector<double>({2}));
    REQUIRE(bar.get_zoom_field().text() == "200%");
    bar.get_zoom_field().setText("big");
    Q_EMIT bar.get_zoom_field().editingFinished();
    REQUIRE(zooms.size() == 1);
    REQUIRE(bar.get_zoom_field().text() == "200%");
    REQUIRE(AppBar::ZOOM_PRESETS ==
      std::to_array({0.25, 0.33, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0}));
  }

  TEST_CASE("workspaces") {
    auto bar = AppBar();
    auto workspaces = std::vector<Workspace>();
    bar.set_workspace_handler([&] (auto workspace) {
      workspaces.push_back(workspace);
    });
    REQUIRE(bar.get_workspace() == Workspace::DESIGN);
    REQUIRE(bar.get_workspace_button(Workspace::DESIGN).isChecked());
    bar.get_workspace_button(Workspace::PROTOTYPE).click();
    REQUIRE(bar.get_workspace() == Workspace::PROTOTYPE);
    REQUIRE(!bar.get_workspace_button(Workspace::DESIGN).isChecked());
    REQUIRE(bar.get_workspace_button(Workspace::PROTOTYPE).isChecked());
    bar.set_workspace(Workspace::SHARE);
    REQUIRE(bar.get_workspace_button(Workspace::SHARE).isChecked());
    REQUIRE(workspaces == std::vector<Workspace>({Workspace::PROTOTYPE}));
  }

  TEST_CASE("title") {
    auto bar = AppBar();
    bar.set_title("shapes", false);
    REQUIRE(bar.get_title() == "shapes");
    bar.set_title("shapes", true);
    REQUIRE(bar.get_title() == "shapes *");
  }
}
