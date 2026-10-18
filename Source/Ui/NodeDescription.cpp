#include <concepts>
#include <variant>
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Ui/NodeDescription.hpp"

using namespace Rellu;

namespace {
  constexpr auto TEXT_NAME_LENGTH = 40;

  QString get_text_name(const Text& text) {
    auto content = QString::fromStdU16String(*text.m_content);
    auto end = content.indexOf(QChar('\n'));
    auto carriage = content.indexOf(QChar('\r'));
    if(carriage >= 0 && (end < 0 || carriage < end)) {
      end = carriage;
    }
    if(end >= 0) {
      content.truncate(end);
    }
    if(content.size() > TEXT_NAME_LENGTH) {
      content.truncate(TEXT_NAME_LENGTH);
      content += "...";
    }
    return content.trimmed();
  }
}

const Node* Rellu::get_displayed_node(
    const Document& document, const Node& node) {
  if(node.get_kind() != NodeKind::REFERENCE) {
    return &node;
  }
  if(auto resolution = resolve_reference(document.get_nodes(), node.get_id())) {
    return document.find(resolution->m_source);
  }
  return nullptr;
}

QString Rellu::get_display_name(const Document& document, const Node& node) {
  if(!node.m_header.m_name.empty()) {
    return QString::fromStdString(node.m_header.m_name);
  }
  auto displayed = get_displayed_node(document, node);
  if(!displayed) {
    return get_kind_name(NodeKind::REFERENCE);
  }
  if(!displayed->m_header.m_name.empty()) {
    return QString::fromStdString(displayed->m_header.m_name);
  }
  if(auto text = displayed->get_text()) {
    auto name = get_text_name(*text);
    if(!name.isEmpty()) {
      return name;
    }
  }
  return get_type_name(*displayed);
}

QString Rellu::get_kind_name(NodeKind kind) {
  switch(kind) {
    case NodeKind::ARTBOARD:
      return "Artboard";
    case NodeKind::GROUP:
      return "Group";
    case NodeKind::SHAPE:
      return "Shape";
    case NodeKind::TEXT:
      return "Text";
    case NodeKind::REFERENCE:
      return "Reference";
  }
  return QString();
}

QString Rellu::get_type_name(const Node& node) {
  if(auto shape = node.get_shape()) {
    return std::visit([] <typename T> (const T&) -> QString {
      if constexpr(std::same_as<T, RectangleShape>) {
        return "Rectangle";
      } else if constexpr(std::same_as<T, EllipseShape>) {
        return "Ellipse";
      } else if constexpr(std::same_as<T, LineShape>) {
        return "Line";
      } else if constexpr(std::same_as<T, PolygonShape>) {
        return "Polygon";
      } else if constexpr(std::same_as<T, PathShape>) {
        return "Path";
      } else {
        return "Compound Shape";
      }
    }, shape->m_geometry);
  } else if(auto group = node.get_group()) {
    if(group->m_component_instance) {
      return "Component";
    } else if(group->m_repeat_grid) {
      return "Repeat Grid";
    } else if(group->m_scroll_group) {
      return "Scroll Group";
    } else if(group->m_stack) {
      return "Stack";
    } else if(group->m_mask) {
      return "Mask Group";
    }
  }
  return get_kind_name(node.get_kind());
}
