#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"

using namespace Rellu;

namespace {
  constexpr auto PATH_ROLE = Qt::UserRole;
  constexpr auto LARGE_STEP = 10.0;
  const auto MIXED = QString("Mixed");

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

  QString get_display_text(const PropertyDescriptor& property) {
    if(property.m_is_mixed) {
      return MIXED;
    }
    return to_text(property.m_value);
  }

  bool is_same_structure(const std::vector<PropertyDescriptor>& left,
      const std::vector<PropertyDescriptor>& right) {
    if(left.size() != right.size()) {
      return false;
    }
    for(auto i = std::size_t(0); i != left.size(); ++i) {
      if(left[i].m_path != right[i].m_path ||
          left[i].m_label != right[i].m_label ||
          left[i].m_value.index() != right[i].m_value.index() ||
          !is_same_structure(left[i].m_children, right[i].m_children)) {
        return false;
      }
    }
    return true;
  }

  std::optional<Color> parse_color(QString text) {
    text = text.trimmed();
    auto alpha = 1.0;
    if(auto space = text.indexOf(' '); space != -1) {
      auto percent = text.mid(space + 1).trimmed();
      if(percent.endsWith('%')) {
        percent.chop(1);
      }
      auto is_valid = false;
      alpha = percent.toDouble(&is_valid) / 100;
      if(!is_valid || !(alpha >= 0 && alpha <= 1)) {
        return std::nullopt;
      }
      text = text.left(space);
    }
    if(text.startsWith('#')) {
      text.remove(0, 1);
    }
    if(text.size() == 3) {
      text = QString("%1%1%2%2%3%3").arg(text[0]).arg(text[1]).arg(text[2]);
    }
    if(text.size() != 6) {
      return std::nullopt;
    }
    auto is_valid = false;
    auto rgb = text.toUInt(&is_valid, 16);
    if(!is_valid) {
      return std::nullopt;
    }
    return make_color(static_cast<int>((rgb >> 16) & 0xFF),
      static_cast<int>((rgb >> 8) & 0xFF), static_cast<int>(rgb & 0xFF), alpha);
  }

  class FieldLine : public QLineEdit {
    public:
      std::function<void (int steps)> m_step;
      std::function<void ()> m_leave;

    protected:
      void keyPressEvent(QKeyEvent* event) override {
        if(m_step &&
            (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
          auto steps = event->key() == Qt::Key_Up ? 1 : -1;
          if(event->modifiers() & Qt::ShiftModifier) {
            steps *= static_cast<int>(LARGE_STEP);
          }
          m_step(steps);
          event->accept();
          return;
        }
        QLineEdit::keyPressEvent(event);
      }

      void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        if(m_leave) {
          m_leave();
        }
      }
  };
}

struct PropertyInspector::Field {
  QWidget* m_widget;
  QWidget* m_input;
  std::function<void (const PropertyDescriptor&)> m_update;
};

PropertyInspector::PropertyInspector(QWidget* parent)
  : QWidget(parent),
    m_document(nullptr),
    m_editor(nullptr),
    m_measurer(estimate_text_bounds),
    m_generation(0),
    m_is_committing(false),
    m_is_rebuild_pending(false),
    m_stack(new QStackedWidget()),
    m_view(new QTreeWidget()) {
  auto placeholder = new QLabel("Select a layer to inspect it.");
  placeholder->setObjectName("placeholder");
  placeholder->setAlignment(Qt::AlignCenter);
  placeholder->setWordWrap(true);
  m_view->setObjectName("properties");
  m_view->setColumnCount(2);
  m_view->setHeaderHidden(true);
  m_view->setUniformRowHeights(false);
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

PropertyInspector::~PropertyInspector() = default;

void PropertyInspector::set_document(Document* document) {
  m_selection_subscription.release();
  m_editor = nullptr;
  attach(document);
  rebuild();
}

Editor* PropertyInspector::get_editor() const {
  return m_editor;
}

void PropertyInspector::set_editor(Editor* editor) {
  m_selection_subscription.release();
  m_editor = editor;
  if(!m_editor) {
    attach(nullptr);
    rebuild();
    return;
  }
  attach(&m_editor->get_document());
  auto follow = [this] {
    auto& selection = m_editor->get_selection();
    auto nodes = selection.get_nodes();
    auto primary =
      std::find(nodes.begin(), nodes.end(), selection.get_primary());
    if(primary != nodes.end()) {
      std::rotate(primary, primary + 1, nodes.end());
    }
    set_nodes(std::move(nodes));
  };
  m_selection_subscription = m_editor->get_selection().subscribe(follow);
  follow();
}

void PropertyInspector::set_property_editors(PropertyEditors editors) {
  m_property_editors = std::move(editors);
  rebuild();
}

void PropertyInspector::set_measurer(TextMeasurer measurer) {
  m_measurer = std::move(measurer);
  refresh();
}

const NodeId& PropertyInspector::get_current() const {
  return m_current;
}

void PropertyInspector::set_current(const NodeId& id) {
  if(id.is_nil()) {
    set_nodes({});
  } else {
    set_nodes({id});
  }
}

const std::vector<NodeId>& PropertyInspector::get_nodes() const {
  return m_nodes;
}

void PropertyInspector::set_nodes(std::vector<NodeId> nodes) {
  m_nodes = std::move(nodes);
  m_current = m_nodes.empty() ? NodeId() : m_nodes.back();
  m_edited_path.clear();
  refresh();
}

const std::vector<PropertyDescriptor>&
    PropertyInspector::get_properties() const {
  return m_properties;
}

QWidget* PropertyInspector::find_field(std::string_view path) const {
  if(auto field = m_fields.find(std::string(path)); field != m_fields.end()) {
    return field->second->m_input;
  }
  return nullptr;
}

QTreeWidget& PropertyInspector::get_view() {
  return *m_view;
}

void PropertyInspector::attach(Document* document) {
  m_subscription.release();
  m_document = document;
  m_nodes.clear();
  m_current = NodeId();
  m_edited_path.clear();
  if(m_document) {
    m_subscription = m_document->subscribe([this] (const auto& changes) {
      on_change(changes);
    });
  }
}

void PropertyInspector::refresh() {
  auto properties = std::vector<PropertyDescriptor>();
  m_sources.clear();
  if(m_document) {
    std::erase_if(m_nodes, [&] (const auto& id) {
      return !m_document->find(id);
    });
    properties = extract_common_properties(*m_document, m_nodes, m_measurer);
    for(auto& id : m_nodes) {
      if(auto source = get_source(*m_document, id); !source.is_nil()) {
        m_sources.push_back(source);
      }
    }
  } else {
    m_nodes.clear();
  }
  m_current = m_nodes.empty() ? NodeId() : m_nodes.back();
  auto is_same = is_same_structure(properties, m_properties);
  m_properties = std::move(properties);
  if(is_same && !m_is_rebuild_pending) {
    update_rows();
  } else if(m_is_committing) {
    if(!m_is_rebuild_pending) {
      m_is_rebuild_pending = true;
      QTimer::singleShot(0, this, [this] {
        if(m_is_rebuild_pending) {
          rebuild();
        }
      });
    }
  } else {
    rebuild();
  }
}

void PropertyInspector::rebuild() {
  m_is_rebuild_pending = false;
  ++m_generation;
  if(!m_document) {
    m_properties.clear();
  }
  auto scroll = m_view->verticalScrollBar()->value();
  m_view->setUpdatesEnabled(false);
  m_fields.clear();
  m_items.clear();
  m_view->clear();
  auto root = m_view->invisibleRootItem();
  for(auto& section : m_properties) {
    add(*root, section);
  }
  m_view->setUpdatesEnabled(true);
  m_view->verticalScrollBar()->setValue(scroll);
  m_stack->setCurrentIndex(m_properties.empty() ? 0 : 1);
}

void PropertyInspector::update_rows() {
  for(auto& section : m_properties) {
    update_row(section);
  }
  m_stack->setCurrentIndex(m_properties.empty() ? 0 : 1);
}

void PropertyInspector::update_row(const PropertyDescriptor& property) {
  if(auto field = m_fields.find(property.m_path); field != m_fields.end()) {
    if(m_is_committing || !field->second->m_input->hasFocus()) {
      field->second->m_update(property);
    }
  } else if(auto item = m_items.find(property.m_path);
      item != m_items.end()) {
    auto text = get_display_text(property);
    if(item->second->text(1) != text) {
      item->second->setText(1, text);
      item->second->setToolTip(1, text);
      if(auto color = std::get_if<Color>(&property.m_value)) {
        item->second->setIcon(1, make_swatch(*color));
      }
    }
  }
  for(auto& child : property.m_children) {
    update_row(child);
  }
}

void PropertyInspector::add(
    QTreeWidgetItem& parent, const PropertyDescriptor& property) {
  auto item = new QTreeWidgetItem(&parent);
  item->setText(0, property.m_label);
  item->setData(0, PATH_ROLE, QString::fromStdString(property.m_path));
  m_items.emplace(property.m_path, item);
  auto editor = static_cast<const PropertyEditor*>(nullptr);
  if(m_editor) {
    editor = m_property_editors.find(property.m_path);
  }
  auto field = editor ? make_field(property, *editor) : nullptr;
  if(field) {
    m_view->setItemWidget(item, 1, field->m_widget);
    field->m_update(property);
    m_fields.emplace(property.m_path, std::move(field));
  } else {
    item->setText(1, get_display_text(property));
    item->setToolTip(1, item->text(1));
    if(auto color = std::get_if<Color>(&property.m_value)) {
      item->setIcon(1, make_swatch(*color));
    }
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

std::unique_ptr<PropertyInspector::Field> PropertyInspector::make_field(
    const PropertyDescriptor& property, const PropertyEditor& editor) {
  auto generation = m_generation;
  auto path = property.m_path;
  auto commit = [=, this] (const PropertyValue& value) {
    this->commit(generation, path, value);
  };
  auto finish = [=, this] {
    this->finish(generation, path);
  };
  if(std::holds_alternative<double>(property.m_value)) {
    auto line = new FieldLine();
    auto scale = editor.m_scale;
    auto suffix = editor.m_suffix;
    auto minimum = editor.m_minimum;
    auto maximum = editor.m_maximum;
    auto shown = std::make_shared<QString>();
    auto parse = [=] (QString text) -> std::optional<double> {
      text = text.trimmed();
      if(!suffix.isEmpty() && text.endsWith(suffix)) {
        text.chop(suffix.size());
      }
      auto is_valid = false;
      auto number = text.trimmed().toDouble(&is_valid);
      if(!is_valid || !std::isfinite(number)) {
        return std::nullopt;
      }
      number /= scale;
      if(minimum) {
        number = std::max(number, *minimum);
      }
      if(maximum) {
        number = std::min(number, *maximum);
      }
      return number;
    };
    auto enter = [=] {
      if(line->text() == *shown) {
        return;
      }
      if(auto number = parse(line->text())) {
        commit(*number);
      } else {
        line->setText(*shown);
      }
    };
    QObject::connect(line, &QLineEdit::editingFinished, line, enter);
    line->m_step = [=] (int steps) {
      auto base = parse(line->text()).value_or(0);
      auto number = parse(QString::number(base * scale + steps));
      if(number) {
        commit(*number);
      }
    };
    line->m_leave = finish;
    auto update = [=] (const PropertyDescriptor& property) {
      auto text = QString();
      if(property.m_is_mixed) {
        line->setPlaceholderText(MIXED);
      } else if(auto number = std::get_if<double>(&property.m_value)) {
        line->setPlaceholderText(QString());
        text = to_text(*number * scale) + suffix;
      }
      *shown = text;
      if(line->text() != text) {
        line->setText(text);
      }
    };
    return std::make_unique<Field>(line, line, update);
  } else if(std::holds_alternative<QString>(property.m_value)) {
    auto line = new FieldLine();
    QObject::connect(line, &QLineEdit::textEdited, line,
      [=] (const QString& text) {
        commit(text);
      });
    QObject::connect(line, &QLineEdit::editingFinished, line, finish);
    auto update = [=] (const PropertyDescriptor& property) {
      line->setPlaceholderText(property.m_is_mixed ? MIXED : QString());
      auto text = property.m_is_mixed ? QString() : to_text(property.m_value);
      if(line->text() != text) {
        line->setText(text);
      }
    };
    return std::make_unique<Field>(line, line, update);
  } else if(std::holds_alternative<bool>(property.m_value)) {
    auto box = new QCheckBox();
    QObject::connect(box, &QCheckBox::clicked, box, [=] {
      auto is_checked = box->checkState() != Qt::Unchecked;
      box->setTristate(false);
      commit(is_checked);
      finish();
    });
    auto update = [=] (const PropertyDescriptor& property) {
      if(property.m_is_mixed) {
        box->setTristate(true);
        box->setCheckState(Qt::PartiallyChecked);
      } else {
        box->setTristate(false);
        box->setChecked(std::get<bool>(property.m_value));
      }
    };
    return std::make_unique<Field>(box, box, update);
  } else if(auto choice = std::get_if<Choice>(&property.m_value)) {
    auto box = new QComboBox();
    box->addItems(choice->m_options);
    box->setPlaceholderText(MIXED);
    auto options = choice->m_options;
    QObject::connect(box, &QComboBox::activated, box, [=] (int index) {
      commit(Choice(index, options));
      finish();
    });
    auto update = [=] (const PropertyDescriptor& property) {
      auto index = property.m_is_mixed ? -1 :
        std::get<Choice>(property.m_value).m_index;
      if(box->currentIndex() != index) {
        box->setCurrentIndex(index);
      }
    };
    return std::make_unique<Field>(box, box, update);
  } else if(std::holds_alternative<Color>(property.m_value)) {
    auto container = new QWidget();
    auto layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto button = new QToolButton();
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    auto line = new FieldLine();
    layout->addWidget(button);
    layout->addWidget(line, 1);
    auto shown = std::make_shared<QString>();
    auto color = std::make_shared<Color>();
    QObject::connect(line, &QLineEdit::editingFinished, line, [=] {
      if(line->text() == *shown) {
        return;
      }
      if(auto parsed = parse_color(line->text())) {
        commit(*parsed);
      } else {
        line->setText(*shown);
      }
    });
    line->m_leave = finish;
    QObject::connect(button, &QToolButton::clicked, button, [=] {
      auto initial = QColor::fromRgbF(static_cast<float>(color->m_red),
        static_cast<float>(color->m_green), static_cast<float>(color->m_blue),
        static_cast<float>(color->m_alpha));
      auto chosen = QColorDialog::getColor(
        initial, button, "Color", QColorDialog::ShowAlphaChannel);
      if(chosen.isValid()) {
        commit(Color(chosen.redF(), chosen.greenF(), chosen.blueF(),
          chosen.alphaF()));
        finish();
      }
    });
    auto update = [=] (const PropertyDescriptor& property) {
      auto text = QString();
      if(property.m_is_mixed) {
        line->setPlaceholderText(MIXED);
        button->setIcon(QIcon());
      } else {
        line->setPlaceholderText(QString());
        *color = std::get<Color>(property.m_value);
        text = to_text(*color);
        button->setIcon(make_swatch(*color));
      }
      *shown = text;
      if(line->text() != text) {
        line->setText(text);
      }
    };
    return std::make_unique<Field>(container, line, update);
  }
  return nullptr;
}

void PropertyInspector::commit(
    int generation, const std::string& path, const PropertyValue& value) {
  if(generation != m_generation || !m_editor) {
    return;
  }
  auto editor = m_property_editors.find(path);
  if(!editor) {
    return;
  }
  auto& history = m_editor->get_history();
  if(path != m_edited_path) {
    history.seal();
    m_edited_path = path;
  }
  auto command = editor->m_setter(m_editor->get_document(), m_nodes, value);
  m_is_committing = true;
  if(command) {
    try {
      m_editor->merge(std::move(command));
    } catch(const std::exception&) {}
  }
  if(!m_is_rebuild_pending) {
    update_rows();
  }
  m_is_committing = false;
}

void PropertyInspector::finish(int generation, const std::string& path) {
  if(generation != m_generation || !m_editor || path != m_edited_path) {
    return;
  }
  m_editor->get_history().seal();
  m_edited_path.clear();
}

bool PropertyInspector::is_affected(const ChangeSet& changes) const {
  auto is_related = [&] (const NodeId& id) {
    for(auto& node : m_nodes) {
      if(node == id || is_ancestor(*m_document, node, id)) {
        return true;
      }
    }
    return std::find(m_sources.begin(), m_sources.end(), id) !=
      m_sources.end();
  };
  for(auto& change : changes.m_modified) {
    if(is_related(change.m_id)) {
      return true;
    }
  }
  for(auto& id : changes.m_moved) {
    if(is_related(id)) {
      return true;
    }
  }
  for(auto& id : changes.m_inserted) {
    if(is_related(id)) {
      return true;
    }
  }
  return !changes.m_removed.empty() && std::any_of(m_nodes.begin(),
    m_nodes.end(), [&] (const auto& id) {
      return !m_document->get(id).m_children.empty();
    });
}

void PropertyInspector::on_change(const ChangeSet& changes) {
  if(m_nodes.empty()) {
    return;
  }
  auto is_removed = std::any_of(m_nodes.begin(), m_nodes.end(),
    [&] (const auto& id) {
      return !m_document->find(id);
    });
  if(is_removed || is_affected(changes)) {
    refresh();
  }
}
