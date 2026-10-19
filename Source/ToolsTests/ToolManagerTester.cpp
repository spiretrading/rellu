#include <memory>
#include <stdexcept>
#include <string>
#include <doctest/doctest.h>
#include "Rellu/Tools/ShapeTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"

using namespace Rellu;

namespace {
  struct ProbeTool : Tool {
    int* m_cancels;

    explicit ProbeTool(int& cancels)
      : m_cancels(&cancels) {}

    bool key_press(ToolContext& context, const KeyEvent& event) override {
      context.request_tool("rectangle");
      return true;
    }

    void cancel(ToolContext& context) override {
      ++*m_cancels;
    }
  };
}

TEST_SUITE("ToolManager") {
  TEST_CASE("registry") {
    auto document = Document();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    REQUIRE(tools.get_tool_name() == "select");
    for(auto name : {"select", "rectangle", "ellipse", "polygon", "line",
        "pen", "path_edit", "artboard", "zoom", "text"}) {
      REQUIRE(tools.has_tool(name));
    }
    REQUIRE(!tools.has_tool("missing"));
    REQUIRE_THROWS_AS(tools.set_tool("missing"), std::out_of_range);
    REQUIRE_THROWS_AS(tools.add_tool("zoom", std::make_unique<ShapeTool>(
      ShapeKind::LINE)), std::invalid_argument);
    auto changes = 0;
    auto subscription = tools.subscribe_tool([&] {
      ++changes;
    });
    tools.set_tool("polygon");
    REQUIRE(tools.get_tool_name() == "polygon");
    REQUIRE(dynamic_cast<ShapeTool&>(tools.get_tool()).get_kind() ==
      ShapeKind::POLYGON);
    REQUIRE(changes == 1);
    tools.set_tool("polygon");
    REQUIRE(changes == 1);
    auto cancels = 0;
    tools.add_tool("probe", std::make_unique<ProbeTool>(cancels));
    tools.set_tool("probe");
    REQUIRE(changes == 2);
    REQUIRE(tools.key_press(KeyEvent(Qt::Key_A, Qt::NoModifier)));
    REQUIRE(tools.get_tool_name() == "rectangle");
    REQUIRE(cancels == 1);
    REQUIRE(changes == 3);
  }
}
