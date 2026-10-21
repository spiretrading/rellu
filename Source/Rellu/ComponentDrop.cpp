#include <exception>
#include <optional>
#include <utility>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include "Rellu/Tools/ComponentDrops.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "ComponentDrop.hpp"

using namespace Rellu;

namespace {
  Point to_point(QPointF position) {
    return Point(position.x(), position.y());
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
  auto tools = m_view->get_tools();
  if(watched != m_view || !m_editor || !tools) {
    return QObject::eventFilter(watched, event);
  }
  if(event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
    auto drag = static_cast<QDragMoveEvent*>(event);
    auto component = drag->mimeData() ?
      read_component_mime_data(*drag->mimeData()) : std::nullopt;
    if(component) {
      auto target = find_swap_target(
        tools->get_context(), *component, to_point(drag->position()));
      m_editor->get_context().set_hover(target);
      tools->set_drop_target(target);
      m_view->update();
      drag->setDropAction(get_component_drop_action(target));
      drag->accept();
      return true;
    }
  } else if(event->type() == QEvent::DragLeave) {
    m_editor->get_context().set_hover(NodeId());
    tools->set_drop_target(NodeId());
    m_view->update();
  } else if(event->type() == QEvent::Drop) {
    auto drop = static_cast<QDropEvent*>(event);
    auto component = drop->mimeData() ?
      read_component_mime_data(*drop->mimeData()) : std::nullopt;
    if(!component) {
      return QObject::eventFilter(watched, event);
    }
    m_editor->get_context().set_hover(NodeId());
    tools->set_drop_target(NodeId());
    auto position = to_point(drop->position());
    auto target = find_swap_target(tools->get_context(), *component, position);
    try {
      if(auto command = make_component_drop(
          tools->get_context(), *component, position, drop->modifiers())) {
        m_editor->execute(std::move(command));
      }
    } catch(const std::exception&) {}
    drop->setDropAction(get_component_drop_action(target));
    drop->accept();
    m_view->setFocus();
    return true;
  }
  return QObject::eventFilter(watched, event);
}