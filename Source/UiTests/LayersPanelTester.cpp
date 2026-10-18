#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Ui/LayersPanel.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("LayersPanel") {
  TEST_CASE("current") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto panel = LayersPanel();
    auto changes = std::vector<NodeId>();
    panel.set_current_handler([&] (const auto& current) {
      changes.push_back(current);
    });
    panel.set_document(&document);
    REQUIRE(panel.get_current().is_nil());
    panel.set_current(ids.m_label);
    REQUIRE(panel.get_current() == ids.m_label);
    REQUIRE(changes == std::vector<NodeId>({ids.m_label}));
    auto& model = panel.get_model();
    REQUIRE(panel.get_view().isExpanded(model.find(ids.m_card)));
    REQUIRE(panel.get_view().isExpanded(model.find(ids.m_artboard)));
    panel.get_view().setCurrentIndex(model.find(ids.m_loose));
    REQUIRE(panel.get_current() == ids.m_loose);
    panel.set_current(ids.m_master_shape);
    REQUIRE(panel.get_current().is_nil());
    REQUIRE(changes ==
      std::vector<NodeId>({ids.m_label, ids.m_loose, NodeId()}));
  }

  TEST_CASE("removed_current") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto panel = LayersPanel();
    panel.set_document(&document);
    panel.set_current(ids.m_background);
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_card);
      transaction.commit();
    }
    REQUIRE(panel.get_current().is_nil());
    panel.set_current(ids.m_ellipse);
    REQUIRE(panel.get_current() == ids.m_ellipse);
    panel.set_document(nullptr);
    REQUIRE(panel.get_current().is_nil());
  }
}
