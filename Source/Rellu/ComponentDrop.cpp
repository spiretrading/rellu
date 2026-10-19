#include <exception>
#include <memory>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "ComponentDrop.hpp"

using namespace Rellu;

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
    if(drag->mimeData() && read_component_mime_data(*drag->mimeData())) {
      drag->acceptProposedAction();
      return true;
    }
  } else if(event->type() == QEvent::Drop) {
    auto drop = static_cast<QDropEvent*>(event);
    auto component = drop->mimeData() ?
      read_component_mime_data(*drop->mimeData()) : std::nullopt;
    if(!component) {
      return QObject::eventFilter(watched, event);
    }
    auto& document = m_editor->get_document();
    auto& components = document.get_resources().m_components;
    auto entry = components.find(*component);
    auto inverse = invert(m_view->get_renderer().get_view());
    if(entry == components.end() || !inverse) {
      return true;
    }
    auto point = inverse->map(
      Point(drop->position().x(), drop->position().y()));
    auto parent = NodeId();
    auto origin = Point(0, 0);
    auto& canvas = document.get_canvas();
    for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
      if(auto artboard = document.get(*i).get_artboard();
          artboard && artboard->m_frame.contains(point)) {
        parent = *i;
        origin = artboard->m_frame.get_top_left();
        break;
      }
    }
    auto center = Point(0, 0);
    if(auto bounds = get_local_bounds(document, entry->second.m_master)) {
      center = bounds->get_center();
    }
    try {
      m_editor->execute(std::make_unique<InsertInstanceCommand>(*component,
        parent, make_translation(point.m_x - origin.m_x - center.m_x,
          point.m_y - origin.m_y - center.m_y)));
    } catch(const std::exception&) {}
    drop->acceptProposedAction();
    m_view->setFocus();
    return true;
  }
  return QObject::eventFilter(watched, event);
}
