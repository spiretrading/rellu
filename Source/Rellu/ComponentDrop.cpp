#include <exception>
#include <memory>
#include <optional>
#include <vector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "ComponentDrop.hpp"

using namespace Rellu;

namespace {
  std::optional<Point> to_document(DocumentView& view, QPointF position) {
    auto inverse = invert(view.get_renderer().get_view());
    if(!inverse) {
      return std::nullopt;
    }
    return inverse->map(Point(position.x(), position.y()));
  }

  NodeId find_swap_target(DocumentView& view, Editor& editor,
      const ComponentId& component, QPointF position) {
    auto tools = view.get_tools();
    auto point = to_document(view, position);
    if(!tools || !point) {
      return NodeId();
    }
    auto& document = editor.get_document();
    auto& container = editor.get_context().get_container();
    auto target = NodeId();
    auto current = tools->get_hit_tester().hit(*point, 0);
    for(auto depth = 0; depth != 256 && !current.is_nil() &&
        current != container; ++depth) {
      auto node = document.find(current);
      if(!node) {
        return NodeId();
      }
      if(get_instance_role(*node)) {
        target = current;
      }
      current = node->m_parent;
    }
    if(target.is_nil() ||
        get_instance_role(document.get(target))->m_component == component) {
      return NodeId();
    }
    return target;
  }
}

ComponentDrop::ComponentDrop(DocumentView& view)
    : m_view(&view),
      m_editor(nullptr) {
  view.setAcceptDrops(true);
  view.installEventFilter(this);
}

void ComponentDrop::set_editor(Editor* editor) {
  m_editor = editor;
}

bool ComponentDrop::eventFilter(QObject* watched, QEvent* event) {
  if(watched != m_view || !m_editor) {
    return QObject::eventFilter(watched, event);
  }
  if(event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
    auto drag = static_cast<QDragMoveEvent*>(event);
    auto component = drag->mimeData() ?
      read_component_mime_data(*drag->mimeData()) : std::nullopt;
    if(component) {
      m_editor->get_context().set_hover(find_swap_target(
        *m_view, *m_editor, *component, drag->position()));
      m_view->update();
      drag->acceptProposedAction();
      return true;
    }
  } else if(event->type() == QEvent::DragLeave) {
    m_editor->get_context().set_hover(NodeId());
    m_view->update();
  } else if(event->type() == QEvent::Drop) {
    auto drop = static_cast<QDropEvent*>(event);
    auto component = drop->mimeData() ?
      read_component_mime_data(*drop->mimeData()) : std::nullopt;
    if(!component) {
      return QObject::eventFilter(watched, event);
    }
    m_editor->get_context().set_hover(NodeId());
    auto& document = m_editor->get_document();
    auto& components = document.get_resources().m_components;
    auto entry = components.find(*component);
    auto point = to_document(*m_view, drop->position());
    if(entry == components.end() || !point) {
      return true;
    }
    auto target =
      find_swap_target(*m_view, *m_editor, *component, drop->position());
    try {
      if(!target.is_nil()) {
        m_editor->execute(std::make_unique<SwapComponentCommand>(
          std::vector{target}, *component));
      } else {
        auto parent = NodeId();
        auto origin = Point(0, 0);
        auto& canvas = document.get_canvas();
        for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
          if(auto artboard = document.get(*i).get_artboard();
              artboard && artboard->m_frame.contains(*point)) {
            parent = *i;
            origin = artboard->m_frame.get_top_left();
            break;
          }
        }
        auto center = Point(0, 0);
        if(auto bounds = get_local_bounds(document, entry->second.m_master)) {
          center = bounds->get_center();
        }
        m_editor->execute(std::make_unique<InsertInstanceCommand>(*component,
          parent, make_translation(point->m_x - origin.m_x - center.m_x,
            point->m_y - origin.m_y - center.m_y)));
      }
    } catch(const std::exception&) {}
    drop->acceptProposedAction();
    m_view->setFocus();
    return true;
  }
  return QObject::eventFilter(watched, event);
}
