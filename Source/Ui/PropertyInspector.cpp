#include <algorithm>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QVBoxLayout>
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"

using namespace Rellu;

namespace {
  constexpr auto PATH_ROLE = Qt::UserRole;

  QIcon make_swatch(const Color& color) {
    auto pixmap = QPixmap(28, 28);
    pixmap.fill(Qt::transparent);
    auto painter = QPainter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    auto bounds = QRectF(3, 3, 22, 22);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(bounds, 4, 4);
    painter.setBrush(QColor(0xD0, 0xD3, 0xD8));
    painter.drawRect(QRectF(3, 3, 11, 11));
    painter.drawRect(QRectF(14, 14, 11, 11));
    painter.setBrush(QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha)));
    painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
    painter.drawRoundedRect(bounds, 4, 4);
    painter.end();
    return QIcon(pixmap);
  }

  NodeId get_source(const Document& document, const NodeId& id) {
    if(auto resolution = resolve_reference(document.get_nodes(), id)) {
      return resolution->m_source;
    }
    return NodeId();
  }
}

PropertyInspector::PropertyInspector(QWidget* parent)
  : QWidget(parent),
    m_document(nullptr),
    m_stack(new QStackedWidget()),
    m_view(new QTreeWidget()) {
  auto placeholder = new QLabel("Select a layer to inspect it.");
  placeholder->setObjectName("placeholder");
  placeholder->setAlignment(Qt::AlignCenter);
  placeholder->setWordWrap(true);
  m_view->setObjectName("properties");
  m_view->setColumnCount(2);
  m_view->setHeaderHidden(true);
  m_view->setUniformRowHeights(true);
  m_view->setIndentation(12);
  m_view->setSelectionMode(QAbstractItemView::NoSelection);
  m_view->setFocusPolicy(Qt::NoFocus);
  m_view->setFrameShape(QFrame::NoFrame);
  m_view->header()->setStretchLastSection(true);
  m_view->header()->setSectionResizeMode(0, QHeaderView::Interactive);
  m_view->header()->resizeSection(0, 120);
  m_stack->addWidget(placeholder);
  m_stack->addWidget(m_view);
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_stack);
  connect(m_view, &QTreeWidget::itemCollapsed, this,
    [this] (QTreeWidgetItem* item) {
      m_collapsed.insert(item->data(0, PATH_ROLE).toString().toStdString());
    });
  connect(m_view, &QTreeWidget::itemExpanded, this,
    [this] (QTreeWidgetItem* item) {
      m_collapsed.erase(item->data(0, PATH_ROLE).toString().toStdString());
    });
}

void PropertyInspector::set_document(Document* document) {
  m_subscription.release();
  m_document = document;
  m_current = NodeId();
  if(m_document) {
    m_subscription = m_document->subscribe([this] (const auto& changes) {
      on_change(changes);
    });
  }
  refresh();
}

const NodeId& PropertyInspector::get_current() const {
  return m_current;
}

void PropertyInspector::set_current(const NodeId& id) {
  m_current = id;
  refresh();
}

const std::vector<PropertyDescriptor>&
    PropertyInspector::get_properties() const {
  return m_properties;
}

QTreeWidget& PropertyInspector::get_view() {
  return *m_view;
}

void PropertyInspector::refresh() {
  if(m_document && !m_current.is_nil()) {
    m_properties = extract_properties(*m_document, m_current);
    m_source = get_source(*m_document, m_current);
  } else {
    m_properties.clear();
    m_source = NodeId();
  }
  auto scroll = m_view->verticalScrollBar()->value();
  m_view->setUpdatesEnabled(false);
  m_view->clear();
  auto root = m_view->invisibleRootItem();
  for(auto& section : m_properties) {
    add(*root, section);
  }
  m_view->setUpdatesEnabled(true);
  m_view->verticalScrollBar()->setValue(scroll);
  m_stack->setCurrentIndex(m_properties.empty() ? 0 : 1);
}

void PropertyInspector::add(
    QTreeWidgetItem& parent, const PropertyDescriptor& property) {
  auto item = new QTreeWidgetItem(&parent);
  item->setText(0, property.m_label);
  item->setData(0, PATH_ROLE, QString::fromStdString(property.m_path));
  item->setText(1, to_text(property.m_value));
  item->setToolTip(1, item->text(1));
  if(auto color = std::get_if<Color>(&property.m_value)) {
    item->setIcon(1, make_swatch(*color));
  }
  if(&parent == m_view->invisibleRootItem()) {
    auto font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setFont(1, font);
  }
  for(auto& child : property.m_children) {
    add(*item, child);
  }
  if(!property.m_children.empty()) {
    item->setExpanded(!m_collapsed.contains(property.m_path));
  }
}

void PropertyInspector::on_change(const ChangeSet& changes) {
  if(m_current.is_nil()) {
    return;
  }
  if(std::find(changes.m_removed.begin(), changes.m_removed.end(),
      m_current) != changes.m_removed.end()) {
    set_current(NodeId());
    return;
  }
  auto is_affected = std::any_of(changes.m_modified.begin(),
    changes.m_modified.end(), [&] (const auto& change) {
      return change.m_id == m_current ||
        (!m_source.is_nil() && change.m_id == m_source);
    }) || std::find(changes.m_moved.begin(), changes.m_moved.end(),
      m_current) != changes.m_moved.end();
  if(is_affected) {
    refresh();
  }
}
