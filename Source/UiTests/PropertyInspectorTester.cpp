#include <doctest/doctest.h>
#include "Rellu/Ui/PropertyInspector.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  QString get_text(const PropertyInspector& inspector, const char* path) {
    auto property = find_property(inspector.get_properties(), path);
    REQUIRE(property);
    return to_text(property->m_value);
  }
}

TEST_SUITE("PropertyInspector") {
  TEST_CASE("current") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    REQUIRE(inspector.get_properties().empty());
    inspector.set_current(ids.m_background);
    REQUIRE(inspector.get_current() == ids.m_background);
    REQUIRE(get_text(inspector, "layer.name") == "Background");
    REQUIRE(inspector.get_view().topLevelItemCount() ==
      static_cast<int>(inspector.get_properties().size()));
    REQUIRE(inspector.get_view().topLevelItem(0)->text(0) == "Layer");
    inspector.set_current(NodeId());
    REQUIRE(inspector.get_properties().empty());
    REQUIRE(inspector.get_view().topLevelItemCount() == 0);
  }

  TEST_CASE("updates") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    inspector.set_current(ids.m_reference);
    REQUIRE(get_text(inspector, "fill.color") == "#0000FF");
    {
      auto transaction = Transaction(document);
      auto shape = *document.get(ids.m_master_shape).get_shape();
      shape.m_style.m_fill = SolidFill(make_color(0, 255, 0));
      transaction.replace_payload(ids.m_master_shape, shape);
      transaction.commit();
    }
    REQUIRE(get_text(inspector, "fill.color") == "#00FF00");
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_reference).m_header;
      header.m_name = "Renamed";
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(get_text(inspector, "layer.name") == "Renamed");
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_instance);
      transaction.commit();
    }
    REQUIRE(inspector.get_current().is_nil());
    REQUIRE(inspector.get_properties().empty());
  }

  TEST_CASE("collapsed_sections") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    inspector.set_current(ids.m_background);
    auto& view = inspector.get_view();
    REQUIRE(view.topLevelItem(0)->isExpanded());
    view.collapseItem(view.topLevelItem(0));
    inspector.set_current(ids.m_ellipse);
    REQUIRE(!view.topLevelItem(0)->isExpanded());
    REQUIRE(view.topLevelItem(1)->isExpanded());
  }
}
