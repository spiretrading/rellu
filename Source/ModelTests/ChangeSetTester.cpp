#include <algorithm>
#include <doctest/doctest.h>
#include "Rellu/Model/ChangeSet.hpp"
#include "SampleDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  ChangeMask find_mask(const ChangeSet& changes, const NodeId& id) {
    auto change = std::find_if(changes.m_modified.begin(),
      changes.m_modified.end(),
      [&] (const auto& change) { return change.m_id == id; });
    if(change == changes.m_modified.end()) {
      return ChangeMask::NONE;
    }
    return change->m_mask;
  }

  struct Recorder {
    std::vector<ChangeSet> m_changes;
    Subscription m_subscription;

    explicit Recorder(Document& document)
      : m_subscription(document.subscribe([this] (const auto& changes) {
          m_changes.push_back(changes);
        })) {}
  };
}

TEST_SUITE("ChangeSet") {
  TEST_CASE("mask") {
    auto mask = ChangeMask::NAME | ChangeMask::STYLE;
    REQUIRE(is_set(mask, ChangeMask::NAME));
    REQUIRE(is_set(mask, ChangeMask::STYLE | ChangeMask::TEXT));
    REQUIRE(!is_set(mask, ChangeMask::TEXT));
    REQUIRE((mask & ChangeMask::STYLE) == ChangeMask::STYLE);
    mask |= ChangeMask::TEXT;
    REQUIRE(is_set(mask, ChangeMask::TEXT));
    REQUIRE(!is_set(ChangeMask::NONE, mask));
  }

  TEST_CASE("is_empty") {
    auto changes = ChangeSet();
    REQUIRE(changes.is_empty());
    changes.m_is_flow_changed = true;
    REQUIRE(!changes.is_empty());
    changes = ChangeSet();
    changes.m_swatches.push_back(generate_node_id());
    REQUIRE(!changes.is_empty());
  }

  TEST_CASE("insertion") {
    auto document = Document();
    auto recorder = Recorder(document);
    auto sample = build_sample(document);
    REQUIRE(recorder.m_changes.size() == 1);
    auto& changes = recorder.m_changes.back();
    REQUIRE(changes.m_inserted.size() == 20);
    REQUIRE(changes.m_inserted.front() == sample.m_artboard);
    REQUIRE(changes.m_removed.empty());
    REQUIRE(changes.m_moved.empty());
    REQUIRE(changes.m_modified.empty());
    REQUIRE(changes.m_components == std::vector<NodeId>({sample.m_component}));
    REQUIRE(changes.m_images == std::vector<NodeId>({sample.m_image}));
    REQUIRE(changes.m_swatches == std::vector<NodeId>({sample.m_swatch}));
    REQUIRE(changes.m_character_styles ==
      std::vector<NodeId>({sample.m_character_style}));
    REQUIRE(changes.m_interactions ==
      std::vector<NodeId>({sample.m_interaction}));
    REQUIRE(changes.m_is_flow_changed);
  }

  TEST_CASE("ancestors_first") {
    auto document = Document();
    auto recorder = Recorder(document);
    auto transaction = Transaction(document);
    auto child = transaction.append(
      NodeId(), make_header("Child"), make_rectangle_shape(1, 1));
    auto parent = transaction.append(NodeId(), make_header("Parent"), Group());
    transaction.move(child, parent, 0);
    transaction.commit();
    REQUIRE(recorder.m_changes.back().m_inserted ==
      std::vector<NodeId>({parent, child}));
    REQUIRE(recorder.m_changes.back().m_moved.empty());
    document.undo();
    REQUIRE(recorder.m_changes.back().m_removed ==
      std::vector<NodeId>({parent, child}));
    REQUIRE(recorder.m_changes.back().m_inserted.empty());
  }

  TEST_CASE("removal") {
    auto document = Document();
    auto sample = build_sample(document);
    auto recorder = Recorder(document);
    auto transaction = Transaction(document);
    auto header = document.get(sample.m_rectangle).m_header;
    header.m_name = "Doomed";
    transaction.replace_header(header);
    transaction.remove(sample.m_stack);
    transaction.commit();
    auto& changes = recorder.m_changes.back();
    REQUIRE(changes.m_removed.size() == 3);
    REQUIRE(changes.m_removed.front() == sample.m_stack);
    REQUIRE(std::ranges::contains(changes.m_removed, sample.m_rectangle));
    REQUIRE(std::ranges::contains(changes.m_removed, sample.m_ellipse));
    REQUIRE(changes.m_modified ==
      std::vector<NodeChange>({NodeChange(sample.m_artboard,
        ChangeMask::CHILDREN)}));
    REQUIRE(changes.m_interactions ==
      std::vector<NodeId>({sample.m_interaction}));
  }

  TEST_CASE("transient") {
    auto document = Document();
    auto recorder = Recorder(document);
    auto transaction = Transaction(document);
    auto group = transaction.append(NodeId(), make_header("Group"), Group());
    transaction.remove(group);
    transaction.commit();
    REQUIRE(recorder.m_changes.empty());
    REQUIRE(!document.can_undo());
  }

  TEST_CASE("move") {
    auto document = Document();
    auto sample = build_sample(document);
    auto recorder = Recorder(document);
    auto transaction = Transaction(document);
    transaction.move(sample.m_ellipse, sample.m_stack, 0);
    transaction.move(sample.m_path, sample.m_scroll_group, 1);
    transaction.commit();
    auto& changes = recorder.m_changes.back();
    REQUIRE(changes.m_moved ==
      std::vector<NodeId>({sample.m_ellipse, sample.m_path}));
    REQUIRE(changes.m_inserted.empty());
    REQUIRE(changes.m_removed.empty());
    REQUIRE(find_mask(changes, sample.m_stack) == ChangeMask::CHILDREN);
    REQUIRE(find_mask(changes, sample.m_mask) == ChangeMask::CHILDREN);
    REQUIRE(find_mask(changes, sample.m_scroll_group) ==
      ChangeMask::CHILDREN);
    REQUIRE(find_mask(changes, sample.m_path) == ChangeMask::NONE);
  }

  TEST_CASE("header") {
    auto document = Document();
    auto sample = build_sample(document);
    auto recorder = Recorder(document);
    auto transaction = Transaction(document);
    auto modify = [&] (const NodeId& id, auto change) {
      auto header = document.get(id).m_header;
      change(header);
      transaction.replace_header(header);
    };
    modify(sample.m_rectangle, [] (auto& header) {
      header.m_name = "Renamed";
    });
    modify(sample.m_ellipse, [] (auto& header) {
      header.m_is_visible = false;
    });
    modify(sample.m_line, [] (auto& header) {
      header.m_is_locked = true;
    });
    modify(sample.m_polygon, [] (auto& header) {
      header.m_transform = make_scale(2, 2);
    });
    modify(sample.m_path, [] (auto& header) {
      header.m_opacity = 0.5;
    });
    modify(sample.m_text, [] (auto& header) {
      header.m_blend_mode = BlendMode::MULTIPLY;
    });
    modify(sample.m_stack, [] (auto& header) {
      header.m_is_isolated = true;
    });
    modify(sample.m_mask, [] (auto& header) {
      header.m_constraints.m_left = true;
    });
    modify(sample.m_artboard, [] (auto& header) {
      header.m_is_marked_for_export = true;
    });
    modify(sample.m_loose, [] (auto& header) {
      header.m_name = "Moved";
      header.m_transform = make_translation(1, 1);
    });
    transaction.commit();
    auto& changes = recorder.m_changes.back();
    REQUIRE(changes.m_modified.size() == 10);
    REQUIRE(find_mask(changes, sample.m_rectangle) == ChangeMask::NAME);
    REQUIRE(find_mask(changes, sample.m_ellipse) == ChangeMask::VISIBILITY);
    REQUIRE(find_mask(changes, sample.m_line) == ChangeMask::LOCK);
    REQUIRE(find_mask(changes, sample.m_polygon) == ChangeMask::TRANSFORM);
    REQUIRE(find_mask(changes, sample.m_path) == ChangeMask::OPACITY);
    REQUIRE(find_mask(changes, sample.m_text) == ChangeMask::BLEND_MODE);
    REQUIRE(find_mask(changes, sample.m_stack) == ChangeMask::ISOLATION);
    REQUIRE(find_mask(changes, sample.m_mask) == ChangeMask::CONSTRAINTS);
    REQUIRE(find_mask(changes, sample.m_artboard) == ChangeMask::EXPORT);
    REQUIRE(find_mask(changes, sample.m_loose) ==
      (ChangeMask::NAME | ChangeMask::TRANSFORM));
  }

  TEST_CASE("payload") {
    auto document = Document();
    auto sample = build_sample(document);
    auto recorder = Recorder(document);
    auto transaction = Transaction(document);
    auto shape = *document.get(sample.m_rectangle).get_shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, 50, 50));
    transaction.replace_payload(sample.m_rectangle, shape);
    shape = *document.get(sample.m_ellipse).get_shape();
    shape.m_style.m_stroke = Stroke();
    transaction.replace_payload(sample.m_ellipse, shape);
    auto text = *document.get(sample.m_text).get_text();
    text.m_paragraph_style.m_alignment = TextAlignment::CENTER;
    text.m_frame = PositionedFrame();
    transaction.replace_payload(sample.m_text, text);
    auto artboard = *document.get(sample.m_artboard).get_artboard();
    artboard.m_frame.m_width = 400;
    artboard.m_background = NoFill();
    artboard.m_guides.clear();
    transaction.replace_payload(sample.m_artboard, artboard);
    auto group = *document.get(sample.m_scroll_group).get_group();
    group.m_scroll_group->m_offset = Point(0, 10);
    transaction.replace_payload(sample.m_scroll_group, group);
    auto reference = *document.get(sample.m_reference).get_reference();
    reference.m_transform = make_translation(3, 3);
    transaction.replace_payload(sample.m_reference, reference);
    transaction.commit();
    auto& changes = recorder.m_changes.back();
    REQUIRE(find_mask(changes, sample.m_rectangle) == ChangeMask::GEOMETRY);
    REQUIRE(find_mask(changes, sample.m_ellipse) == ChangeMask::STYLE);
    REQUIRE(find_mask(changes, sample.m_text) ==
      (ChangeMask::TEXT | ChangeMask::GEOMETRY));
    REQUIRE(find_mask(changes, sample.m_artboard) ==
      (ChangeMask::GEOMETRY | ChangeMask::STYLE | ChangeMask::ARTBOARD));
    REQUIRE(find_mask(changes, sample.m_scroll_group) == ChangeMask::ROLES);
    REQUIRE(find_mask(changes, sample.m_reference) == ChangeMask::TRANSFORM);
    reference.m_source = sample.m_master;
    auto retarget = Transaction(document);
    retarget.replace_payload(sample.m_reference, reference);
    retarget.commit();
    REQUIRE(find_mask(recorder.m_changes.back(), sample.m_reference) ==
      ChangeMask::REFERENCE);
  }

  TEST_CASE("resources") {
    auto document = Document();
    auto sample = build_sample(document);
    auto recorder = Recorder(document);
    auto transaction = Transaction(document);
    auto component = document.get_resources().m_components.at(
      sample.m_component);
    component.m_name = "Renamed";
    transaction.replace_component(component);
    transaction.remove_image(sample.m_image);
    auto swatch = transaction.add_swatch(ColorSwatch());
    transaction.replace_character_style(
      CharacterStyle(sample.m_character_style, "Renamed"));
    transaction.remove_interaction(sample.m_interaction);
    transaction.set_flows(std::vector<Flow>());
    transaction.commit();
    auto& changes = recorder.m_changes.back();
    REQUIRE(changes.m_components == std::vector<NodeId>({sample.m_component}));
    REQUIRE(changes.m_images == std::vector<NodeId>({sample.m_image}));
    REQUIRE(changes.m_swatches == std::vector<NodeId>({swatch}));
    REQUIRE(changes.m_character_styles ==
      std::vector<NodeId>({sample.m_character_style}));
    REQUIRE(changes.m_interactions ==
      std::vector<NodeId>({sample.m_interaction}));
    REQUIRE(changes.m_is_flow_changed);
    REQUIRE(changes.m_modified.empty());
  }
}
