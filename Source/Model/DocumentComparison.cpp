#include <cmath>
#include <format>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include "Rellu/Model/DocumentComparison.hpp"

using namespace Rellu;

namespace {
  class Collector {
    public:
      explicit Collector(std::size_t limit) noexcept;

      bool is_full() const;
      void add(std::string description);
      void check(bool is_same, const std::string& subject,
        std::string_view field);
      std::vector<std::string> release();

    private:
      std::size_t m_limit;
      std::vector<std::string> m_differences;
  };

  Collector::Collector(std::size_t limit) noexcept
    : m_limit(limit) {}

  bool Collector::is_full() const {
    return m_differences.size() >= m_limit;
  }

  void Collector::add(std::string description) {
    if(!is_full()) {
      m_differences.push_back(std::move(description));
    }
  }

  void Collector::check(
      bool is_same, const std::string& subject, std::string_view field) {
    if(!is_same) {
      add(subject + ": " + std::string(field) + " differs");
    }
  }

  std::vector<std::string> Collector::release() {
    return std::move(m_differences);
  }

  bool is_same(double left, double right) {
    return left == right || (std::isnan(left) && std::isnan(right));
  }

  bool is_same(
      const std::optional<double>& left, const std::optional<double>& right) {
    if(!left || !right) {
      return !left == !right;
    }
    return is_same(*left, *right);
  }

  void compare(const NodeHeader& left, const NodeHeader& right,
      const std::string& subject, Collector& collector) {
    collector.check(left.m_name == right.m_name, subject, "name");
    collector.check(
      left.m_is_visible == right.m_is_visible, subject, "visibility");
    collector.check(left.m_is_locked == right.m_is_locked, subject, "lock");
    collector.check(
      left.m_transform == right.m_transform, subject, "transform");
    collector.check(left.m_transform_3d == right.m_transform_3d, subject,
      "3D transform");
    collector.check(
      is_same(left.m_opacity, right.m_opacity), subject, "opacity");
    collector.check(
      left.m_blend_mode == right.m_blend_mode, subject, "blend mode");
    collector.check(
      left.m_is_isolated == right.m_is_isolated, subject, "isolation");
    collector.check(
      left.m_constraints == right.m_constraints, subject, "constraints");
    collector.check(left.m_is_marked_for_export ==
      right.m_is_marked_for_export, subject, "export marking");
    collector.check(left.m_is_fixed_position == right.m_is_fixed_position,
      subject, "fixed position");
    collector.check(
      left.m_aspect_lock == right.m_aspect_lock, subject, "aspect lock");
    collector.check(left.m_opaque == right.m_opaque, subject, "opaque data");
  }

  void compare(const Style& left, const Style& right,
      const std::string& subject, Collector& collector) {
    collector.check(left.m_fill == right.m_fill, subject, "fill");
    collector.check(left.m_stroke == right.m_stroke, subject, "stroke");
    collector.check(left.m_effects == right.m_effects, subject, "effects");
  }

  void compare(const Artboard& left, const Artboard& right,
      const std::string& subject, Collector& collector) {
    collector.check(left.m_frame == right.m_frame, subject, "frame");
    collector.check(
      left.m_background == right.m_background, subject, "background");
    collector.check(is_same(left.m_viewport_height, right.m_viewport_height),
      subject, "viewport height");
    collector.check(
      left.m_layout_grid == right.m_layout_grid, subject, "layout grid");
    collector.check(left.m_guides == right.m_guides, subject, "guides");
  }

  void compare(const Group& left, const Group& right,
      const std::string& subject, Collector& collector) {
    collector.check(left.m_mask == right.m_mask, subject, "mask");
    collector.check(
      left.m_repeat_grid == right.m_repeat_grid, subject, "repeat grid");
    collector.check(
      left.m_scroll_group == right.m_scroll_group, subject, "scroll group");
    collector.check(left.m_stack == right.m_stack, subject, "stack");
    collector.check(left.m_padding == right.m_padding, subject, "padding");
    if(!left.m_component_instance != !right.m_component_instance) {
      collector.add(subject + ": component instance differs");
      return;
    } else if(!left.m_component_instance) {
      return;
    }
    auto& left_instance = *left.m_component_instance;
    auto& right_instance = *right.m_component_instance;
    auto instance = subject + ": component instance";
    collector.check(left_instance.m_component == right_instance.m_component,
      instance, "component");
    collector.check(
      left_instance.m_state == right_instance.m_state, instance, "state");
    collector.check(
      left_instance.m_size == right_instance.m_size, instance, "size");
    collector.check(left_instance.m_sync_map == right_instance.m_sync_map,
      instance, "sync map");
    collector.check(left_instance.m_overrides == right_instance.m_overrides,
      instance, "overrides");
    collector.check(
      left_instance.m_states == right_instance.m_states, instance, "states");
    collector.check(
      left_instance.m_is_main == right_instance.m_is_main, instance, "main");
  }

  void compare(const Shape& left, const Shape& right,
      const std::string& subject, Collector& collector) {
    collector.check(
      left.m_geometry == right.m_geometry, subject, "geometry");
    compare(left.m_style, right.m_style, subject + ": style", collector);
  }

  void compare(const Text& left, const Text& right,
      const std::string& subject, Collector& collector) {
    collector.check(left.m_content == right.m_content, subject, "content");
    collector.check(left.m_frame == right.m_frame, subject, "frame");
    compare(left.m_style, right.m_style, subject + ": style", collector);
    collector.check(
      left.m_base_style == right.m_base_style, subject, "base style");
    collector.check(left.m_runs == right.m_runs, subject, "runs");
    collector.check(left.m_paragraph_style == right.m_paragraph_style,
      subject, "paragraph style");
    collector.check(left.m_layout == right.m_layout, subject, "layout");
    collector.check(left.m_repeat_data == right.m_repeat_data, subject,
      "repeat data");
  }

  void compare(const Reference& left, const Reference& right,
      const std::string& subject, Collector& collector) {
    collector.check(left.m_source == right.m_source, subject, "source");
    collector.check(
      left.m_transform == right.m_transform, subject, "transform override");
    collector.check(left.m_has_children == right.m_has_children, subject,
      "children override");
  }

  template<typename T>
  void compare(const Box<T>& left, const Box<T>& right,
      const std::string& subject, Collector& collector) {
    compare(*left, *right, subject, collector);
  }

  void compare(const Node& left, const Node& right, Collector& collector) {
    if(left == right) {
      return;
    }
    auto subject = "node " + to_string(left.get_id());
    compare(left.m_header, right.m_header, subject, collector);
    if(left.m_payload.index() != right.m_payload.index()) {
      collector.add(subject + ": kind differs");
    } else {
      std::visit([&] (const auto& payload) {
        using Type = std::decay_t<decltype(payload)>;
        compare(payload, std::get<Type>(right.m_payload), subject, collector);
      }, left.m_payload);
    }
    collector.check(left.m_parent == right.m_parent, subject, "parent");
    collector.check(
      left.m_children == right.m_children, subject, "children");
  }

  void compare(const Component& left, const Component& right,
      Collector& collector) {
    auto subject = "component " + to_string(left.m_id);
    collector.check(left.m_name == right.m_name, subject, "name");
    collector.check(left.m_master == right.m_master, subject, "master");
    collector.check(left.m_states == right.m_states, subject, "states");
    collector.check(left.m_default_state == right.m_default_state, subject,
      "default state");
    collector.check(left.m_size == right.m_size, subject, "size");
    collector.check(left.m_link == right.m_link, subject, "link");
  }

  void compare(const ImageResource& left, const ImageResource& right,
      Collector& collector) {
    auto subject = "image " + to_string(left.m_id);
    collector.check(
      left.m_media_type == right.m_media_type, subject, "media type");
    if(left.m_data->size() != right.m_data->size()) {
      collector.add(std::format("{}: size differs ({} and {} bytes)",
        subject, left.m_data->size(), right.m_data->size()));
    } else {
      collector.check(left.m_data == right.m_data, subject, "data");
    }
  }

  template<typename K, typename V>
  void compare_maps(const std::map<K, V>& left, const std::map<K, V>& right,
      std::string_view name, Collector& collector) {
    for(auto& entry : left) {
      if(collector.is_full()) {
        return;
      }
      auto other = right.find(entry.first);
      if(other == right.end()) {
        collector.add(std::format(
          "{} {}: only in the first document", name, to_string(entry.first)));
      } else if(!(entry.second == other->second)) {
        compare(entry.second, other->second, collector);
      }
    }
    for(auto& entry : right) {
      if(!left.contains(entry.first)) {
        collector.add(std::format("{} {}: only in the second document",
          name, to_string(entry.first)));
      }
    }
  }

  template<typename T>
  void compare_lists(const std::vector<T>& left, const std::vector<T>& right,
      std::string_view name, Collector& collector) {
    if(left.size() != right.size()) {
      collector.add(std::format(
        "{}: count differs ({} and {})", name, left.size(), right.size()));
      return;
    }
    for(auto i = std::size_t(0); i != left.size(); ++i) {
      if(!(left[i] == right[i])) {
        collector.add(std::format("{} {} ({}) differs", name, i,
          to_string(left[i].m_id)));
      }
    }
  }
}

std::vector<std::string> Rellu::find_differences(
    const Document& left, const Document& right, std::size_t limit) {
  auto collector = Collector(limit);
  if(left.get_canvas() != right.get_canvas()) {
    collector.add(std::format("canvas differs ({} and {} nodes)",
      left.get_canvas().size(), right.get_canvas().size()));
  }
  auto& right_nodes = right.get_nodes();
  left.get_nodes().for_each([&] (const auto& node) {
    if(collector.is_full()) {
      return;
    }
    if(auto other = right_nodes.find(node.get_id())) {
      compare(node, *other, collector);
    } else {
      collector.add(
        "node " + to_string(node.get_id()) + ": only in the first document");
    }
  });
  auto& left_nodes = left.get_nodes();
  right_nodes.for_each([&] (const auto& node) {
    if(!collector.is_full() && !left_nodes.contains(node.get_id())) {
      collector.add(
        "node " + to_string(node.get_id()) + ": only in the second document");
    }
  });
  auto& left_resources = left.get_resources();
  auto& right_resources = right.get_resources();
  compare_maps(left_resources.m_components, right_resources.m_components,
    "component", collector);
  compare_maps(left_resources.m_images, right_resources.m_images, "image",
    collector);
  compare_lists(left_resources.m_swatches, right_resources.m_swatches,
    "swatch", collector);
  compare_lists(left_resources.m_character_styles,
    right_resources.m_character_styles, "character style", collector);
  auto& left_prototype = left.get_prototype();
  auto& right_prototype = right.get_prototype();
  compare_lists(left_prototype.m_interactions, right_prototype.m_interactions,
    "interaction", collector);
  if(left_prototype.m_home_artboard != right_prototype.m_home_artboard) {
    collector.add("home artboard differs");
  }
  if(left_prototype.m_flows != right_prototype.m_flows) {
    collector.add("flows differ");
  }
  if(left.get_opaque() != right.get_opaque()) {
    collector.add("document opaque data differs");
  }
  return collector.release();
}

bool Rellu::is_equal(const Document& left, const Document& right) {
  return find_differences(left, right, 1).empty();
}
